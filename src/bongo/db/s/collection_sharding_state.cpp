/**
 *    Copyright (C) 2015 BongoDB Inc.
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
 *    must comply with the GNU Affero General Public License in all respects
 *    for all of the code used other than as permitted herein. If you modify
 *    file(s) with this exception, you may extend this exception to your
 *    version of the file(s), but you are not obligated to do so. If you do not
 *    wish to do so, delete this exception statement from your version. If you
 *    delete this exception statement from all source files in the program,
 *    then also delete it in the license file.
 */

#define BONGO_LOG_DEFAULT_COMPONENT ::bongo::logger::LogComponent::kSharding

#include "bongo/platform/basic.h"

#include "bongo/db/s/collection_sharding_state.h"

#include "bongo/db/client.h"
#include "bongo/db/concurrency/lock_state.h"
#include "bongo/db/operation_context.h"
#include "bongo/db/repl/replication_coordinator_global.h"
#include "bongo/db/s/collection_metadata.h"
#include "bongo/db/s/migration_chunk_cloner_source.h"
#include "bongo/db/s/migration_source_manager.h"
#include "bongo/db/s/operation_sharding_state.h"
#include "bongo/db/s/shard_identity_rollback_notifier.h"
#include "bongo/db/s/sharded_connection_info.h"
#include "bongo/db/s/sharding_state.h"
#include "bongo/db/s/type_shard_identity.h"
#include "bongo/db/server_options.h"
#include "bongo/db/service_context.h"
#include "bongo/s/catalog/sharding_catalog_manager.h"
#include "bongo/s/catalog/type_config_version.h"
#include "bongo/s/catalog/type_shard.h"
#include "bongo/s/chunk_version.h"
#include "bongo/s/cluster_identity_loader.h"
#include "bongo/s/grid.h"
#include "bongo/s/stale_exception.h"
#include "bongo/util/log.h"

namespace bongo {

namespace {

using std::string;

/**
 * Used to perform shard identity initialization once it is certain that the document is committed.
 */
class ShardIdentityLogOpHandler final : public RecoveryUnit::Change {
public:
    ShardIdentityLogOpHandler(OperationContext* txn, ShardIdentityType shardIdentity)
        : _txn(txn), _shardIdentity(std::move(shardIdentity)) {}

    void commit() override {
        fassertNoTrace(40071,
                       ShardingState::get(_txn)->initializeFromShardIdentity(_txn, _shardIdentity));
    }

    void rollback() override {}

private:
    OperationContext* _txn;
    const ShardIdentityType _shardIdentity;
};

}  // unnamed namespace

CollectionShardingState::CollectionShardingState(ServiceContext* sc, NamespaceString nss)
    : _nss(std::move(nss)), _metadataManager{sc, _nss} {}

CollectionShardingState::~CollectionShardingState() {
    invariant(!_sourceMgr);
}

CollectionShardingState* CollectionShardingState::get(OperationContext* txn,
                                                      const NamespaceString& nss) {
    return CollectionShardingState::get(txn, nss.ns());
}

CollectionShardingState* CollectionShardingState::get(OperationContext* txn,
                                                      const std::string& ns) {
    // Collection lock must be held to have a reference to the collection's sharding state
    dassert(txn->lockState()->isCollectionLockedForMode(ns, MODE_IS));

    ShardingState* const shardingState = ShardingState::get(txn);
    return shardingState->getNS(ns, txn);
}

ScopedCollectionMetadata CollectionShardingState::getMetadata() {
    return _metadataManager.getActiveMetadata();
}

void CollectionShardingState::refreshMetadata(OperationContext* txn,
                                              std::unique_ptr<CollectionMetadata> newMetadata) {
    invariant(txn->lockState()->isCollectionLockedForMode(_nss.ns(), MODE_X));

    _metadataManager.refreshActiveMetadata(std::move(newMetadata));
}

void CollectionShardingState::markNotShardedAtStepdown() {
    _metadataManager.refreshActiveMetadata(nullptr);
}

void CollectionShardingState::beginReceive(const ChunkRange& range) {
    _metadataManager.beginReceive(range);
}

void CollectionShardingState::forgetReceive(const ChunkRange& range) {
    _metadataManager.forgetReceive(range);
}

MigrationSourceManager* CollectionShardingState::getMigrationSourceManager() {
    return _sourceMgr;
}

void CollectionShardingState::setMigrationSourceManager(OperationContext* txn,
                                                        MigrationSourceManager* sourceMgr) {
    invariant(txn->lockState()->isCollectionLockedForMode(_nss.ns(), MODE_X));
    invariant(sourceMgr);
    invariant(!_sourceMgr);

    _sourceMgr = sourceMgr;
}

void CollectionShardingState::clearMigrationSourceManager(OperationContext* txn) {
    invariant(txn->lockState()->isCollectionLockedForMode(_nss.ns(), MODE_X));
    invariant(_sourceMgr);

    _sourceMgr = nullptr;
}

void CollectionShardingState::checkShardVersionOrThrow(OperationContext* txn) {
    string errmsg;
    ChunkVersion received;
    ChunkVersion wanted;
    if (!_checkShardVersionOk(txn, &errmsg, &received, &wanted)) {
        throw SendStaleConfigException(
            _nss.ns(),
            str::stream() << "[" << _nss.ns() << "] shard version not ok: " << errmsg,
            received,
            wanted);
    }
}

bool CollectionShardingState::collectionIsSharded() {
    auto metadata = getMetadata().getMetadata();
    if (metadata && (metadata->getCollVersion().isStrictlyEqualTo(ChunkVersion::UNSHARDED()))) {
        return false;
    }

    // If 'metadata' is null, then the shard doesn't know if this collection is sharded or not. In
    // this scenario we will assume this collection is sharded. We will know sharding state
    // definitively once SERVER-24960 has been fixed.
    return true;
}

bool CollectionShardingState::isDocumentInMigratingChunk(OperationContext* txn,
                                                         const BSONObj& doc) {
    dassert(txn->lockState()->isCollectionLockedForMode(_nss.ns(), MODE_IX));

    if (_sourceMgr) {
        return _sourceMgr->getCloner()->isDocumentInMigratingChunk(txn, doc);
    }

    return false;
}

void CollectionShardingState::onInsertOp(OperationContext* txn, const BSONObj& insertedDoc) {
    dassert(txn->lockState()->isCollectionLockedForMode(_nss.ns(), MODE_IX));

    if (serverGlobalParams.clusterRole == ClusterRole::ShardServer &&
        _nss == NamespaceString::kConfigCollectionNamespace) {
        if (auto idElem = insertedDoc["_id"]) {
            if (idElem.str() == ShardIdentityType::IdName) {
                auto shardIdentityDoc = uassertStatusOK(ShardIdentityType::fromBSON(insertedDoc));
                uassertStatusOK(shardIdentityDoc.validate());
                txn->recoveryUnit()->registerChange(
                    new ShardIdentityLogOpHandler(txn, std::move(shardIdentityDoc)));
            }
        }
    }

    checkShardVersionOrThrow(txn);

    if (_sourceMgr) {
        _sourceMgr->getCloner()->onInsertOp(txn, insertedDoc);
    }
}

void CollectionShardingState::onUpdateOp(OperationContext* txn, const BSONObj& updatedDoc) {
    dassert(txn->lockState()->isCollectionLockedForMode(_nss.ns(), MODE_IX));

    checkShardVersionOrThrow(txn);

    if (_sourceMgr) {
        _sourceMgr->getCloner()->onUpdateOp(txn, updatedDoc);
    }
}

void CollectionShardingState::onDeleteOp(OperationContext* txn,
                                         const CollectionShardingState::DeleteState& deleteState) {
    dassert(txn->lockState()->isCollectionLockedForMode(_nss.ns(), MODE_IX));

    if (serverGlobalParams.clusterRole == ClusterRole::ShardServer &&
        _nss == NamespaceString::kConfigCollectionNamespace) {

        if (auto idElem = deleteState.idDoc["_id"]) {
            auto idStr = idElem.str();
            if (idStr == ShardIdentityType::IdName) {
                if (!repl::ReplicationCoordinator::get(txn)->getMemberState().rollback()) {
                    uasserted(40070,
                              "cannot delete shardIdentity document while in --shardsvr mode");
                } else {
                    warning() << "Shard identity document rolled back.  Will shut down after "
                                 "finishing rollback.";
                    ShardIdentityRollbackNotifier::get(txn)->recordThatRollbackHappened();
                }
            }
        }
    }

    if (serverGlobalParams.clusterRole == ClusterRole::ConfigServer) {
        if (_nss == VersionType::ConfigNS) {
            if (!repl::ReplicationCoordinator::get(txn)->getMemberState().rollback()) {
                uasserted(40302, "cannot delete config.version document while in --configsvr mode");
            } else {
                // Throw out any cached information related to the cluster ID.
                Grid::get(txn)->catalogManager()->discardCachedConfigDatabaseInitializationState();
                ClusterIdentityLoader::get(txn)->discardCachedClusterId();
            }
        }
    }

    checkShardVersionOrThrow(txn);

    if (_sourceMgr && deleteState.isMigrating) {
        _sourceMgr->getCloner()->onDeleteOp(txn, deleteState.idDoc);
    }
}

void CollectionShardingState::onDropCollection(OperationContext* txn,
                                               const NamespaceString& collectionName) {
    dassert(txn->lockState()->isCollectionLockedForMode(_nss.ns(), MODE_IX));

    if (serverGlobalParams.clusterRole == ClusterRole::ShardServer &&
        _nss == NamespaceString::kConfigCollectionNamespace) {
        // Dropping system collections is not allowed for end users.
        invariant(!txn->writesAreReplicated());
        invariant(repl::ReplicationCoordinator::get(txn)->getMemberState().rollback());

        // Can't confirm whether there was a ShardIdentity document or not yet, so assume there was
        // one and shut down the process to clear the in-memory sharding state.
        warning() << "admin.system.version collection rolled back.  Will shut down after "
                     "finishing rollback";
        ShardIdentityRollbackNotifier::get(txn)->recordThatRollbackHappened();
    }

    if (serverGlobalParams.clusterRole == ClusterRole::ConfigServer) {
        if (_nss == VersionType::ConfigNS) {
            if (!repl::ReplicationCoordinator::get(txn)->getMemberState().rollback()) {
                uasserted(40303, "cannot drop config.version document while in --configsvr mode");
            } else {
                // Throw out any cached information related to the cluster ID.
                Grid::get(txn)->catalogManager()->discardCachedConfigDatabaseInitializationState();
                ClusterIdentityLoader::get(txn)->discardCachedClusterId();
            }
        }
    }
}

bool CollectionShardingState::_checkShardVersionOk(OperationContext* txn,
                                                   string* errmsg,
                                                   ChunkVersion* expectedShardVersion,
                                                   ChunkVersion* actualShardVersion) {
    Client* client = txn->getClient();

    // Operations using the DBDirectClient are unversioned.
    if (client->isInDirectClient()) {
        return true;
    }

    if (!repl::ReplicationCoordinator::get(txn)->canAcceptWritesForDatabase(_nss.db())) {
        // Right now connections to secondaries aren't versioned at all.
        return true;
    }

    const auto& oss = OperationShardingState::get(txn);

    // If there is a version attached to the OperationContext, use it as the received version.
    // Otherwise, get the received version from the ShardedConnectionInfo.
    if (oss.hasShardVersion()) {
        *expectedShardVersion = oss.getShardVersion(_nss);
    } else {
        ShardedConnectionInfo* info = ShardedConnectionInfo::get(client, false);
        if (!info) {
            // There is no shard version information on either 'txn' or 'client'. This means that
            // the operation represented by 'txn' is unversioned, and the shard version is always OK
            // for unversioned operations.
            return true;
        }

        *expectedShardVersion = info->getVersion(_nss.ns());
    }

    if (ChunkVersion::isIgnoredVersion(*expectedShardVersion)) {
        return true;
    }

    // Set this for error messaging purposes before potentially returning false.
    auto metadata = getMetadata();
    *actualShardVersion = metadata ? metadata->getShardVersion() : ChunkVersion::UNSHARDED();

    if (_sourceMgr && _sourceMgr->getMigrationCriticalSectionSignal()) {
        *errmsg = str::stream() << "migration commit in progress for " << _nss.ns();

        // Set migration critical section on operation sharding state: operation will wait for the
        // migration to finish before returning failure and retrying.
        OperationShardingState::get(txn).setMigrationCriticalSectionSignal(
            _sourceMgr->getMigrationCriticalSectionSignal());
        return false;
    }

    if (expectedShardVersion->isWriteCompatibleWith(*actualShardVersion)) {
        return true;
    }

    //
    // Figure out exactly why not compatible, send appropriate error message
    // The versions themselves are returned in the error, so not needed in messages here
    //

    // Check epoch first, to send more meaningful message, since other parameters probably won't
    // match either.
    if (actualShardVersion->epoch() != expectedShardVersion->epoch()) {
        *errmsg = str::stream() << "version epoch mismatch detected for " << _nss.ns() << ", "
                                << "the collection may have been dropped and recreated";
        return false;
    }

    if (!actualShardVersion->isSet() && expectedShardVersion->isSet()) {
        *errmsg = str::stream() << "this shard no longer contains chunks for " << _nss.ns() << ", "
                                << "the collection may have been dropped";
        return false;
    }

    if (actualShardVersion->isSet() && !expectedShardVersion->isSet()) {
        *errmsg = str::stream() << "this shard contains versioned chunks for " << _nss.ns() << ", "
                                << "but no version set in request";
        return false;
    }

    if (actualShardVersion->majorVersion() != expectedShardVersion->majorVersion()) {
        // Could be > or < - wanted is > if this is the source of a migration, wanted < if this is
        // the target of a migration
        *errmsg = str::stream() << "version mismatch detected for " << _nss.ns();
        return false;
    }

    // Those are all the reasons the versions can mismatch
    BONGO_UNREACHABLE;
}

}  // namespace bongo
