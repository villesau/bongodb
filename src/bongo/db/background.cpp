// background.cpp

/**
*    Copyright (C) 2010 10gen Inc.
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

#include "bongo/db/background.h"

#include <iostream>
#include <string>

#include "bongo/base/disallow_copying.h"
#include "bongo/stdx/condition_variable.h"
#include "bongo/stdx/mutex.h"
#include "bongo/stdx/thread.h"
#include "bongo/util/assert_util.h"
#include "bongo/util/map_util.h"
#include "bongo/util/bongoutils/str.h"
#include "bongo/util/string_map.h"

namespace bongo {

using std::shared_ptr;

namespace {

class BgInfo {
    BONGO_DISALLOW_COPYING(BgInfo);

public:
    BgInfo() : _opsInProgCount(0) {}

    void recordBegin();
    int recordEnd();
    void awaitNoBgOps(stdx::unique_lock<stdx::mutex>& lk);

    int getOpsInProgCount() const {
        return _opsInProgCount;
    }

private:
    int _opsInProgCount;
    stdx::condition_variable _noOpsInProg;
};

typedef StringMap<std::shared_ptr<BgInfo>> BgInfoMap;
typedef BgInfoMap::const_iterator BgInfoMapIterator;

// Static data for this file is never destroyed.
stdx::mutex& m = *(new stdx::mutex());
BgInfoMap& dbsInProg = *(new BgInfoMap());
BgInfoMap& nsInProg = *(new BgInfoMap());

void BgInfo::recordBegin() {
    ++_opsInProgCount;
}

int BgInfo::recordEnd() {
    dassert(_opsInProgCount > 0);
    --_opsInProgCount;
    if (0 == _opsInProgCount) {
        _noOpsInProg.notify_all();
    }
    return _opsInProgCount;
}

void BgInfo::awaitNoBgOps(stdx::unique_lock<stdx::mutex>& lk) {
    while (_opsInProgCount > 0)
        _noOpsInProg.wait(lk);
}

void recordBeginAndInsert(BgInfoMap* bgiMap, StringData key) {
    std::shared_ptr<BgInfo>& bgInfo = bgiMap->get(key);
    if (!bgInfo)
        bgInfo.reset(new BgInfo);
    bgInfo->recordBegin();
}

void recordEndAndRemove(BgInfoMap* bgiMap, StringData key) {
    BgInfoMapIterator iter = bgiMap->find(key);
    fassert(17431, iter != bgiMap->end());
    if (0 == iter->second->recordEnd()) {
        bgiMap->erase(iter);
    }
}

void awaitNoBgOps(stdx::unique_lock<stdx::mutex>& lk, BgInfoMap* bgiMap, StringData key) {
    std::shared_ptr<BgInfo> bgInfo = mapFindWithDefault(*bgiMap, key, std::shared_ptr<BgInfo>());
    if (!bgInfo)
        return;
    bgInfo->awaitNoBgOps(lk);
}

}  // namespace

bool BackgroundOperation::inProgForDb(StringData db) {
    stdx::lock_guard<stdx::mutex> lk(m);
    return dbsInProg.find(db) != dbsInProg.end();
}

bool BackgroundOperation::inProgForNs(StringData ns) {
    stdx::lock_guard<stdx::mutex> lk(m);
    return nsInProg.find(ns) != nsInProg.end();
}

void BackgroundOperation::assertNoBgOpInProgForDb(StringData db) {
    uassert(ErrorCodes::BackgroundOperationInProgressForDatabase,
            bongoutils::str::stream()
                << "cannot perform operation: a background operation is currently running for "
                   "database "
                << db,
            !inProgForDb(db));
}

void BackgroundOperation::assertNoBgOpInProgForNs(StringData ns) {
    uassert(ErrorCodes::BackgroundOperationInProgressForNamespace,
            bongoutils::str::stream()
                << "cannot perform operation: a background operation is currently running for "
                   "collection "
                << ns,
            !inProgForNs(ns));
}

void BackgroundOperation::awaitNoBgOpInProgForDb(StringData db) {
    stdx::unique_lock<stdx::mutex> lk(m);
    awaitNoBgOps(lk, &dbsInProg, db);
}

void BackgroundOperation::awaitNoBgOpInProgForNs(StringData ns) {
    stdx::unique_lock<stdx::mutex> lk(m);
    awaitNoBgOps(lk, &nsInProg, ns);
}

BackgroundOperation::BackgroundOperation(StringData ns) : _ns(ns) {
    stdx::lock_guard<stdx::mutex> lk(m);
    recordBeginAndInsert(&dbsInProg, _ns.db());
    recordBeginAndInsert(&nsInProg, _ns.ns());
}

BackgroundOperation::~BackgroundOperation() {
    stdx::lock_guard<stdx::mutex> lk(m);
    recordEndAndRemove(&dbsInProg, _ns.db());
    recordEndAndRemove(&nsInProg, _ns.ns());
}

void BackgroundOperation::dump(std::ostream& ss) {
    stdx::lock_guard<stdx::mutex> lk(m);
    if (nsInProg.size()) {
        ss << "\n<b>Background Jobs in Progress</b>\n";
        for (BgInfoMapIterator i = nsInProg.begin(); i != nsInProg.end(); ++i)
            ss << "  " << i->first << '\n';
    }
    for (BgInfoMapIterator i = dbsInProg.begin(); i != dbsInProg.end(); ++i) {
        if (i->second->getOpsInProgCount())
            ss << "database " << i->first << ": " << i->second->getOpsInProgCount() << '\n';
    }
}

}  // namespace bongo
