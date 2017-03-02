/**
 *    Copyright (C) 2008, 2013 10gen Inc.
 *
 *    This program is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the GNU Affero General Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#include "bongo/platform/basic.h"

#include "bongo/db/clientcursor.h"

#include <string>
#include <time.h>
#include <vector>

#include "bongo/base/counter.h"
#include "bongo/db/audit.h"
#include "bongo/db/auth/action_set.h"
#include "bongo/db/auth/action_type.h"
#include "bongo/db/auth/authorization_session.h"
#include "bongo/db/auth/privilege.h"
#include "bongo/db/catalog/collection.h"
#include "bongo/db/client.h"
#include "bongo/db/commands.h"
#include "bongo/db/commands/server_status.h"
#include "bongo/db/commands/server_status_metric.h"
#include "bongo/db/jsobj.h"
#include "bongo/db/repl/repl_client_info.h"
#include "bongo/db/repl/replication_coordinator_global.h"
#include "bongo/db/server_parameters.h"
#include "bongo/util/background.h"
#include "bongo/util/exit.h"

namespace bongo {

using std::string;
using std::stringstream;

static Counter64 cursorStatsOpen;           // gauge
static Counter64 cursorStatsOpenPinned;     // gauge
static Counter64 cursorStatsOpenNoTimeout;  // gauge
static Counter64 cursorStatsTimedOut;

static ServerStatusMetricField<Counter64> dCursorStatsOpen("cursor.open.total", &cursorStatsOpen);
static ServerStatusMetricField<Counter64> dCursorStatsOpenPinned("cursor.open.pinned",
                                                                 &cursorStatsOpenPinned);
static ServerStatusMetricField<Counter64> dCursorStatsOpenNoTimeout("cursor.open.noTimeout",
                                                                    &cursorStatsOpenNoTimeout);
static ServerStatusMetricField<Counter64> dCursorStatusTimedout("cursor.timedOut",
                                                                &cursorStatsTimedOut);

BONGO_EXPORT_SERVER_PARAMETER(cursorTimeoutMillis, int, 10 * 60 * 1000 /* 10 minutes */);
BONGO_EXPORT_SERVER_PARAMETER(clientCursorMonitorFrequencySecs, int, 4);

long long ClientCursor::totalOpen() {
    return cursorStatsOpen.get();
}

ClientCursor::ClientCursor(const ClientCursorParams& params,
                           CursorManager* cursorManager,
                           CursorId cursorId)
    : _cursorid(cursorId),
      _ns(params.ns),
      _isReadCommitted(params.isReadCommitted),
      _cursorManager(cursorManager),
      _query(params.query),
      _queryOptions(params.qopts),
      _isAggCursor(params.isAggCursor) {
    _exec.reset(params.exec);
    init();
}

ClientCursor::ClientCursor(const Collection* collection,
                           CursorManager* cursorManager,
                           CursorId cursorId)
    : _cursorid(cursorId),
      _ns(collection->ns().ns()),
      _cursorManager(cursorManager),
      _queryOptions(QueryOption_NoCursorTimeout) {
    init();
}

void ClientCursor::init() {
    invariant(_cursorManager);

    cursorStatsOpen.increment();

    if (_queryOptions & QueryOption_NoCursorTimeout) {
        // cursors normally timeout after an inactivity period to prevent excess memory use
        // setting this prevents timeout of the cursor in question.
        _isNoTimeout = true;
        cursorStatsOpenNoTimeout.increment();
    }
}

ClientCursor::~ClientCursor() {
    // Cursors must be unpinned and deregistered from their cursor manager before being deleted.
    invariant(!_isPinned);
    invariant(!_cursorManager);

    cursorStatsOpen.decrement();
    if (_isNoTimeout) {
        cursorStatsOpenNoTimeout.decrement();
    }
}

void ClientCursor::kill() {
    if (_exec.get())
        _exec->kill("cursor killed");

    _cursorManager = nullptr;
}

//
// Timing and timeouts
//

bool ClientCursor::shouldTimeout(int millis) {
    _idleAgeMillis += millis;
    if (_isNoTimeout || _isPinned) {
        return false;
    }
    return _idleAgeMillis > cursorTimeoutMillis.load();
}

void ClientCursor::resetIdleTime() {
    _idleAgeMillis = 0;
}

void ClientCursor::updateSlaveLocation(OperationContext* txn) {
    if (_slaveReadTill.isNull())
        return;

    verify(str::startsWith(_ns.c_str(), "local.oplog."));

    Client* c = txn->getClient();
    verify(c);
    OID rid = repl::ReplClientInfo::forClient(c).getRemoteID();
    if (!rid.isSet())
        return;

    repl::getGlobalReplicationCoordinator()->setLastOptimeForSlave(rid, _slaveReadTill);
}

//
// Pin methods
//

ClientCursorPin::ClientCursorPin(ClientCursor* cursor) : _cursor(cursor) {
    invariant(_cursor);
    invariant(_cursor->_isPinned);
    invariant(_cursor->_cursorManager);

    // We keep track of the number of cursors currently pinned. The cursor can become unpinned
    // either by being released back to the cursor manager or by being deleted. A cursor may be
    // transferred to another pin object via move construction or move assignment, but in this case
    // it is still considered pinned.
    cursorStatsOpenPinned.increment();
}

ClientCursorPin::ClientCursorPin(ClientCursorPin&& other) : _cursor(other._cursor) {
    // The pinned cursor is being transferred to us from another pin. The 'other' pin must have a
    // pinned cursor.
    invariant(other._cursor);
    invariant(other._cursor->_isPinned);

    // Be sure to set the 'other' pin's cursor to null in order to transfer ownership to ourself.
    _cursor = other._cursor;
    other._cursor = nullptr;
}

ClientCursorPin& ClientCursorPin::operator=(ClientCursorPin&& other) {
    if (this == &other) {
        return *this;
    }

    // The pinned cursor is being transferred to us from another pin. The 'other' pin must have a
    // pinned cursor, and we must not have a cursor.
    invariant(!_cursor);
    invariant(other._cursor);
    invariant(other._cursor->_isPinned);

    // Copy the cursor pointer to ourselves, but also be sure to set the 'other' pin's cursor to
    // null so that it no longer has the cursor pinned.
    // Be sure to set the 'other' pin's cursor to null in order to transfer ownership to ourself.
    _cursor = other._cursor;
    other._cursor = nullptr;
    return *this;
}

ClientCursorPin::~ClientCursorPin() {
    release();
}

void ClientCursorPin::release() {
    if (!_cursor)
        return;

    invariant(_cursor->_isPinned);

    if (!_cursor->_cursorManager) {
        // The ClientCursor was killed while we had it.  Therefore, it is our responsibility to
        // kill it.
        deleteUnderlying();
    } else {
        // Unpin the cursor under the collection cursor manager lock.
        _cursor->_cursorManager->unpin(_cursor);
    }

    cursorStatsOpenPinned.decrement();
    _cursor = nullptr;
}

void ClientCursorPin::deleteUnderlying() {
    invariant(_cursor);
    invariant(_cursor->_isPinned);
    // Note the following subtleties of this method's implementation:
    // - We must unpin the cursor before destruction, since it is an error to destroy a pinned
    //   cursor.
    // - In addition, we must deregister the cursor before unpinning, since it is an
    //   error to unpin a registered cursor without holding the cursor manager lock (note that
    //   we can't simply unpin with the cursor manager lock here, since we need to guarantee
    //   exclusive ownership of the cursor when we are deleting it).
    if (_cursor->_cursorManager) {
        _cursor->_cursorManager->deregisterCursor(_cursor);
        _cursor->kill();
    }
    _cursor->_isPinned = false;
    delete _cursor;

    cursorStatsOpenPinned.decrement();
    _cursor = nullptr;
}

ClientCursor* ClientCursorPin::getCursor() const {
    return _cursor;
}

//
// ClientCursorMonitor
//

/**
 * Thread for timing out old cursors
 */
class ClientCursorMonitor : public BackgroundJob {
public:
    std::string name() const {
        return "ClientCursorMonitor";
    }

    void run() {
        Client::initThread("clientcursormon");
        Timer t;
        while (!globalInShutdownDeprecated()) {
            {
                const ServiceContext::UniqueOperationContext txnPtr = cc().makeOperationContext();
                OperationContext& txn = *txnPtr;
                cursorStatsTimedOut.increment(
                    CursorManager::timeoutCursorsGlobal(&txn, t.millisReset()));
            }
            sleepsecs(clientCursorMonitorFrequencySecs.load());
        }
    }
};

namespace {
// Only one instance of the ClientCursorMonitor exists
ClientCursorMonitor clientCursorMonitor;

void _appendCursorStats(BSONObjBuilder& b) {
    b.append("note", "deprecated, use server status metrics");
    b.appendNumber("clientCursors_size", cursorStatsOpen.get());
    b.appendNumber("totalOpen", cursorStatsOpen.get());
    b.appendNumber("pinned", cursorStatsOpenPinned.get());
    b.appendNumber("totalNoTimeout", cursorStatsOpenNoTimeout.get());
    b.appendNumber("timedOut", cursorStatsTimedOut.get());
}
}

void startClientCursorMonitor() {
    clientCursorMonitor.go();
}

}  // namespace bongo
