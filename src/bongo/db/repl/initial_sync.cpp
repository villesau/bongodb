/**
*    Copyright (C) 2008 10gen Inc.
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

#define BONGO_LOG_DEFAULT_COMPONENT ::bongo::logger::LogComponent::kReplication

#include "bongo/platform/basic.h"

#include "bongo/db/repl/initial_sync.h"

#include "bongo/db/client.h"
#include "bongo/db/repl/bgsync.h"
#include "bongo/db/repl/oplog.h"
#include "bongo/db/repl/optime.h"
#include "bongo/db/repl/repl_client_info.h"
#include "bongo/db/repl/replication_coordinator_global.h"
#include "bongo/util/exit.h"
#include "bongo/util/log.h"

namespace bongo {
namespace repl {

unsigned replSetForceInitialSyncFailure = 0;

InitialSync::InitialSync(BackgroundSync* q, MultiSyncApplyFunc func) : SyncTail(q, func) {}

InitialSync::~InitialSync() {}

/* initial oplog application, during initial sync, after cloning.
*/
void InitialSync::oplogApplication(OperationContext* txn, const OpTime& endOpTime) {
    if (replSetForceInitialSyncFailure > 0) {
        log() << "test code invoked, forced InitialSync failure: "
              << replSetForceInitialSyncFailure;
        replSetForceInitialSyncFailure--;
        throw DBException("forced error", 0);
    }
    _applyOplogUntil(txn, endOpTime);
}


/* applies oplog from "now" until endOpTime using the applier threads for initial sync*/
void InitialSync::_applyOplogUntil(OperationContext* txn, const OpTime& endOpTime) {
    unsigned long long bytesApplied = 0;
    unsigned long long entriesApplied = 0;
    while (true) {
        OpQueue ops;

        auto replCoord = repl::ReplicationCoordinator::get(txn);
        while (!tryPopAndWaitForMore(txn, &ops, BatchLimits{})) {
            if (globalInShutdownDeprecated()) {
                return;
            }

            // This code is only prepared for this to happen after inShutdown() becomes true.
            invariant(!ops.mustShutdown());

            // nothing came back last time, so go again
            if (ops.empty())
                continue;

            // Check if we reached the end
            const BSONObj currentOp = ops.back().raw;
            const OpTime currentOpTime =
                fassertStatusOK(28772, OpTime::parseFromOplogEntry(currentOp));

            // When we reach the end return this batch
            if (currentOpTime == endOpTime) {
                break;
            } else if (currentOpTime > endOpTime) {
                severe() << "Applied past expected end " << endOpTime << " to " << currentOpTime
                         << " without seeing it. Rollback?";
                fassertFailedNoTrace(18693);
            }
        };

        if (ops.empty()) {
            // nothing came back last time, so go again
            continue;
        }

        const BSONObj lastOp = ops.back().raw.getOwned();

        // Tally operation information and apply batch. Don't use ops again after these lines.
        bytesApplied += ops.getBytes();
        entriesApplied += ops.getCount();
        const OpTime lastOpTime = multiApply(txn, ops.releaseBatch());

        replCoord->setMyLastAppliedOpTime(lastOpTime);
        setNewTimestamp(txn->getServiceContext(), lastOpTime.getTimestamp());

        if (globalInShutdownDeprecated()) {
            return;
        }

        // if the last op applied was our end, return
        if (lastOpTime == endOpTime) {
            LOG(1) << "SyncTail applied " << entriesApplied << " entries (" << bytesApplied
                   << " bytes) and finished at opTime " << endOpTime;
            return;
        }
    }  // end of while (true)
}
}  // namespace repl
}  // namespace bongo
