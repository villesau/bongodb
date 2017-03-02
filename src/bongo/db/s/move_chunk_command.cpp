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

#include "bongo/client/remote_command_targeter.h"
#include "bongo/db/auth/action_set.h"
#include "bongo/db/auth/action_type.h"
#include "bongo/db/auth/authorization_manager.h"
#include "bongo/db/auth/authorization_session.h"
#include "bongo/db/commands.h"
#include "bongo/db/range_deleter_service.h"
#include "bongo/db/s/chunk_move_write_concern_options.h"
#include "bongo/db/s/collection_metadata.h"
#include "bongo/db/s/migration_source_manager.h"
#include "bongo/db/s/move_timing_helper.h"
#include "bongo/db/s/sharding_state.h"
#include "bongo/s/client/shard_registry.h"
#include "bongo/s/grid.h"
#include "bongo/s/migration_secondary_throttle_options.h"
#include "bongo/s/move_chunk_request.h"
#include "bongo/util/concurrency/notification.h"
#include "bongo/util/fail_point_service.h"
#include "bongo/util/log.h"

namespace bongo {

using std::string;

namespace {

/**
 * If the specified status is not OK logs a warning and throws a DBException corresponding to the
 * specified status.
 */
void uassertStatusOKWithWarning(const Status& status) {
    if (!status.isOK()) {
        warning() << "Chunk move failed" << causedBy(redact(status));
        uassertStatusOK(status);
    }
}

// Tests can pause and resume moveChunk's progress at each step by enabling/disabling each failpoint
BONGO_FP_DECLARE(moveChunkHangAtStep1);
BONGO_FP_DECLARE(moveChunkHangAtStep2);
BONGO_FP_DECLARE(moveChunkHangAtStep3);
BONGO_FP_DECLARE(moveChunkHangAtStep4);
BONGO_FP_DECLARE(moveChunkHangAtStep5);
BONGO_FP_DECLARE(moveChunkHangAtStep6);
BONGO_FP_DECLARE(moveChunkHangAtStep7);

class MoveChunkCommand : public Command {
public:
    MoveChunkCommand() : Command("moveChunk") {}

    void help(std::stringstream& help) const override {
        help << "should not be calling this directly";
    }

    bool slaveOk() const override {
        return false;
    }

    bool adminOnly() const override {
        return true;
    }

    bool supportsWriteConcern(const BSONObj& cmd) const override {
        return true;
    }

    Status checkAuthForCommand(Client* client,
                               const string& dbname,
                               const BSONObj& cmdObj) override {
        if (!AuthorizationSession::get(client)->isAuthorizedForActionsOnResource(
                ResourcePattern::forClusterResource(), ActionType::internal)) {
            return Status(ErrorCodes::Unauthorized, "Unauthorized");
        }
        return Status::OK();
    }

    string parseNs(const string& dbname, const BSONObj& cmdObj) const override {
        return parseNsFullyQualified(dbname, cmdObj);
    }

    bool run(OperationContext* txn,
             const string& dbname,
             BSONObj& cmdObj,
             int options,
             string& errmsg,
             BSONObjBuilder& result) override {
        auto shardingState = ShardingState::get(txn);
        uassertStatusOK(shardingState->canAcceptShardedCommands());

        const MoveChunkRequest moveChunkRequest = uassertStatusOK(
            MoveChunkRequest::createFromCommand(NamespaceString(parseNs(dbname, cmdObj)), cmdObj));

        // Make sure we're as up-to-date as possible with shard information. This catches the case
        // where we might have changed a shard's host by removing/adding a shard with the same name.
        grid.shardRegistry()->reload(txn);

        auto scopedRegisterMigration =
            uassertStatusOK(shardingState->registerDonateChunk(moveChunkRequest));

        Status status = {ErrorCodes::InternalError, "Uninitialized value"};

        // Check if there is an existing migration running and if so, join it
        if (scopedRegisterMigration.mustExecute()) {
            try {
                _runImpl(txn, moveChunkRequest);
                status = Status::OK();
            } catch (const DBException& e) {
                status = e.toStatus();
            } catch (const std::exception& e) {
                scopedRegisterMigration.complete(
                    {ErrorCodes::InternalError,
                     str::stream() << "Severe error occurred while running moveChunk command: "
                                   << e.what()});
                throw;
            }

            scopedRegisterMigration.complete(status);
        } else {
            status = scopedRegisterMigration.waitForCompletion(txn);
        }

        if (status == ErrorCodes::ChunkTooBig) {
            // This code is for compatibility with pre-3.2 balancer, which does not recognize the
            // ChunkTooBig error code and instead uses the "chunkTooBig" field in the response,
            // and the 3.4 shard, which failed to set the ChunkTooBig status code.
            // TODO: Remove after 3.6 is released.
            result.appendBool("chunkTooBig", true);
            return appendCommandStatus(result, status);
        }

        uassertStatusOK(status);
        return true;
    }

private:
    static void _runImpl(OperationContext* txn, const MoveChunkRequest& moveChunkRequest) {
        const auto writeConcernForRangeDeleter =
            uassertStatusOK(ChunkMoveWriteConcernOptions::getEffectiveWriteConcern(
                txn, moveChunkRequest.getSecondaryThrottle()));

        // Resolve the donor and recipient shards and their connection string
        auto const shardRegistry = Grid::get(txn)->shardRegistry();

        const auto donorConnStr =
            uassertStatusOK(shardRegistry->getShard(txn, moveChunkRequest.getFromShardId()))
                ->getConnString();
        const auto recipientHost = uassertStatusOK([&] {
            auto recipientShard =
                uassertStatusOK(shardRegistry->getShard(txn, moveChunkRequest.getToShardId()));

            return recipientShard->getTargeter()->findHostNoWait(
                ReadPreferenceSetting{ReadPreference::PrimaryOnly});
        }());

        string unusedErrMsg;
        MoveTimingHelper moveTimingHelper(txn,
                                          "from",
                                          moveChunkRequest.getNss().ns(),
                                          moveChunkRequest.getMinKey(),
                                          moveChunkRequest.getMaxKey(),
                                          7,  // Total number of steps
                                          &unusedErrMsg,
                                          moveChunkRequest.getToShardId(),
                                          moveChunkRequest.getFromShardId());

        moveTimingHelper.done(1);
        BONGO_FAIL_POINT_PAUSE_WHILE_SET(moveChunkHangAtStep1);

        BSONObj shardKeyPattern;

        {
            MigrationSourceManager migrationSourceManager(
                txn, moveChunkRequest, donorConnStr, recipientHost);

            shardKeyPattern = migrationSourceManager.getKeyPattern().getOwned();

            moveTimingHelper.done(2);
            BONGO_FAIL_POINT_PAUSE_WHILE_SET(moveChunkHangAtStep2);

            uassertStatusOKWithWarning(migrationSourceManager.startClone(txn));
            moveTimingHelper.done(3);
            BONGO_FAIL_POINT_PAUSE_WHILE_SET(moveChunkHangAtStep3);

            uassertStatusOKWithWarning(migrationSourceManager.awaitToCatchUp(txn));
            moveTimingHelper.done(4);
            BONGO_FAIL_POINT_PAUSE_WHILE_SET(moveChunkHangAtStep4);

            uassertStatusOKWithWarning(migrationSourceManager.enterCriticalSection(txn));
            uassertStatusOKWithWarning(migrationSourceManager.commitChunkOnRecipient(txn));
            moveTimingHelper.done(5);
            BONGO_FAIL_POINT_PAUSE_WHILE_SET(moveChunkHangAtStep5);

            uassertStatusOKWithWarning(migrationSourceManager.commitChunkMetadataOnConfig(txn));
            moveTimingHelper.done(6);
            BONGO_FAIL_POINT_PAUSE_WHILE_SET(moveChunkHangAtStep6);
        }

        // Schedule the range deleter
        RangeDeleterOptions deleterOptions(KeyRange(moveChunkRequest.getNss().ns(),
                                                    moveChunkRequest.getMinKey().getOwned(),
                                                    moveChunkRequest.getMaxKey().getOwned(),
                                                    shardKeyPattern));
        deleterOptions.writeConcern = writeConcernForRangeDeleter;
        deleterOptions.waitForOpenCursors = true;
        deleterOptions.fromMigrate = true;
        deleterOptions.onlyRemoveOrphanedDocs = true;
        deleterOptions.removeSaverReason = "post-cleanup";

        if (moveChunkRequest.getWaitForDelete()) {
            log() << "doing delete inline for cleanup of chunk data";

            string errMsg;

            // This is an immediate delete, and as a consequence, there could be more
            // deletes happening simultaneously than there are deleter worker threads.
            if (!getDeleter()->deleteNow(txn, deleterOptions, &errMsg)) {
                log() << "Error occured while performing cleanup: " << redact(errMsg);
            }
        } else {
            log() << "forking for cleanup of chunk data";

            string errMsg;
            if (!getDeleter()->queueDelete(txn,
                                           deleterOptions,
                                           NULL,  // Don't want to be notified
                                           &errMsg)) {
                log() << "could not queue migration cleanup: " << redact(errMsg);
            }
        }

        moveTimingHelper.done(7);
        BONGO_FAIL_POINT_PAUSE_WHILE_SET(moveChunkHangAtStep7);
    }

} moveChunkCmd;

}  // namespace
}  // namespace bongo
