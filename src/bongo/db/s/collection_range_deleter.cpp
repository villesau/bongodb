/**
 *    Copyright (C) 2016 BongoDB Inc.
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

#define BONGO_LOG_DEFAULT_COMPONENT ::bongo::logger::LogComponent::kSharding

#include "bongo/platform/basic.h"

#include "bongo/db/s/collection_range_deleter.h"

#include <algorithm>

#include "bongo/db/catalog/collection.h"
#include "bongo/db/client.h"
#include "bongo/db/db_raii.h"
#include "bongo/db/dbhelpers.h"
#include "bongo/db/exec/working_set_common.h"
#include "bongo/db/index/index_descriptor.h"
#include "bongo/db/keypattern.h"
#include "bongo/db/query/internal_plans.h"
#include "bongo/db/query/query_knobs.h"
#include "bongo/db/query/query_planner.h"
#include "bongo/db/repl/repl_client_info.h"
#include "bongo/db/repl/replication_coordinator_global.h"
#include "bongo/db/s/collection_sharding_state.h"
#include "bongo/db/s/sharding_state.h"
#include "bongo/db/write_concern.h"
#include "bongo/executor/task_executor.h"
#include "bongo/util/log.h"
#include "bongo/util/bongoutils/str.h"
#include "bongo/util/scopeguard.h"

namespace bongo {

class ChunkRange;
class OldClientWriteContext;

using CallbackArgs = executor::TaskExecutor::CallbackArgs;
using logger::LogComponent;

namespace {

const WriteConcernOptions kMajorityWriteConcern(WriteConcernOptions::kMajority,
                                                WriteConcernOptions::SyncMode::UNSET,
                                                Seconds(60));

}  // unnamed namespace

CollectionRangeDeleter::CollectionRangeDeleter(NamespaceString nss) : _nss(std::move(nss)) {}

void CollectionRangeDeleter::run() {
    Client::initThread(getThreadName().c_str());
    ON_BLOCK_EXIT([&] { Client::destroy(); });
    auto txn = cc().makeOperationContext().get();

    const int maxToDelete = std::max(int(internalQueryExecYieldIterations.load()), 1);
    bool hasNextRangeToClean = cleanupNextRange(txn, maxToDelete);

    // If there are more ranges to run, we add <this> back onto the task executor to run again.
    if (hasNextRangeToClean) {
        auto executor = ShardingState::get(txn)->getRangeDeleterTaskExecutor();
        executor->scheduleWork([this](const CallbackArgs& cbArgs) { run(); });
    } else {
        delete this;
    }
}

bool CollectionRangeDeleter::cleanupNextRange(OperationContext* txn, int maxToDelete) {

    {
        AutoGetCollection autoColl(txn, _nss, MODE_IX);
        auto* collection = autoColl.getCollection();
        if (!collection) {
            return false;
        }
        auto* collectionShardingState = CollectionShardingState::get(txn, _nss);
        dassert(collectionShardingState != nullptr);  // every collection gets one

        auto& metadataManager = collectionShardingState->_metadataManager;

        if (!_rangeInProgress && !metadataManager.hasRangesToClean()) {
            // Nothing left to do
            return false;
        }

        if (!_rangeInProgress || !metadataManager.isInRangesToClean(_rangeInProgress.get())) {
            // No valid chunk in progress, get a new one
            if (!metadataManager.hasRangesToClean()) {
                return false;
            }
            _rangeInProgress = metadataManager.getNextRangeToClean();
        }

        auto scopedCollectionMetadata = collectionShardingState->getMetadata();
        int numDocumentsDeleted =
            _doDeletion(txn, collection, scopedCollectionMetadata->getKeyPattern(), maxToDelete);
        if (numDocumentsDeleted <= 0) {
            metadataManager.removeRangeToClean(_rangeInProgress.get());
            _rangeInProgress = boost::none;
            return metadataManager.hasRangesToClean();
        }
    }

    // wait for replication
    WriteConcernResult wcResult;
    auto currentClientOpTime = repl::ReplClientInfo::forClient(txn->getClient()).getLastOp();
    Status status = waitForWriteConcern(txn, currentClientOpTime, kMajorityWriteConcern, &wcResult);
    if (!status.isOK()) {
        warning() << "Error when waiting for write concern after removing chunks in " << _nss
                  << " : " << status.reason();
    }

    return true;
}

int CollectionRangeDeleter::_doDeletion(OperationContext* txn,
                                        Collection* collection,
                                        const BSONObj& keyPattern,
                                        int maxToDelete) {
    invariant(_rangeInProgress);
    invariant(collection);

    // The IndexChunk has a keyPattern that may apply to more than one index - we need to
    // select the index and get the full index keyPattern here.
    const IndexDescriptor* idx =
        collection->getIndexCatalog()->findShardKeyPrefixedIndex(txn, keyPattern, false);
    if (idx == NULL) {
        warning() << "Unable to find shard key index for " << keyPattern.toString() << " in "
                  << _nss;
        return -1;
    }

    KeyPattern indexKeyPattern(idx->keyPattern().getOwned());

    // Extend bounds to match the index we found
    const BSONObj& min =
        Helpers::toKeyFormat(indexKeyPattern.extendRangeBound(_rangeInProgress->getMin(), false));
    const BSONObj& max =
        Helpers::toKeyFormat(indexKeyPattern.extendRangeBound(_rangeInProgress->getMax(), false));

    LOG(1) << "begin removal of " << min << " to " << max << " in " << _nss;

    auto indexName = idx->indexName();
    IndexDescriptor* desc = collection->getIndexCatalog()->findIndexByName(txn, indexName);
    if (!desc) {
        warning() << "shard key index with name " << indexName << " on '" << _nss
                  << "' was dropped";
        return -1;
    }

    int numDeleted = 0;
    do {
        auto exec = InternalPlanner::indexScan(txn,
                                               collection,
                                               desc,
                                               min,
                                               max,
                                               BoundInclusion::kIncludeStartKeyOnly,
                                               PlanExecutor::YIELD_MANUAL,
                                               InternalPlanner::FORWARD,
                                               InternalPlanner::IXSCAN_FETCH);
        RecordId rloc;
        BSONObj obj;
        PlanExecutor::ExecState state = exec->getNext(&obj, &rloc);
        if (state == PlanExecutor::IS_EOF) {
            break;
        }
        if (state == PlanExecutor::FAILURE || state == PlanExecutor::DEAD) {
            warning(LogComponent::kSharding)
                << PlanExecutor::statestr(state) << " - cursor error while trying to delete " << min
                << " to " << max << " in " << _nss << ": " << WorkingSetCommon::toStatusString(obj)
                << ", stats: " << Explain::getWinningPlanStats(exec.get());
            break;
        }

        invariant(PlanExecutor::ADVANCED == state);
        WriteUnitOfWork wuow(txn);
        if (!repl::getGlobalReplicationCoordinator()->canAcceptWritesFor(_nss)) {
            warning() << "stepped down from primary while deleting chunk; orphaning data in "
                      << _nss << " in range [" << min << ", " << max << ")";
            break;
        }
        OpDebug* const nullOpDebug = nullptr;
        collection->deleteDocument(txn, rloc, nullOpDebug, true);
        wuow.commit();
    } while (++numDeleted < maxToDelete);
    return numDeleted;
}

}  // namespace bongo
