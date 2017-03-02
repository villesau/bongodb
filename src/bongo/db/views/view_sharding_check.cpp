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

#include "bongo/platform/basic.h"

#include "bongo/db/views/view_sharding_check.h"

#include "bongo/db/catalog/database.h"
#include "bongo/db/commands.h"
#include "bongo/db/db_raii.h"
#include "bongo/db/operation_context.h"
#include "bongo/db/repl/replication_coordinator_global.h"
#include "bongo/db/s/collection_sharding_state.h"
#include "bongo/db/s/sharding_state.h"
#include "bongo/db/server_options.h"
#include "bongo/db/views/view_catalog.h"
#include "bongo/s/stale_exception.h"

namespace bongo {

StatusWith<BSONObj> ViewShardingCheck::getResolvedViewIfSharded(OperationContext* opCtx,
                                                                Database* db,
                                                                const ViewDefinition* view) {
    invariant(opCtx);
    invariant(db);
    invariant(view);

    if (ClusterRole::ShardServer != serverGlobalParams.clusterRole) {
        // This node is not part of a sharded cluster, so the collection cannot be sharded.
        return BSONObj();
    }

    auto resolvedView = db->getViewCatalog()->resolveView(opCtx, view->name());
    if (!resolvedView.isOK()) {
        return resolvedView.getStatus();
    }

    const auto& sourceNss = resolvedView.getValue().getNamespace();
    const auto isPrimary =
        repl::ReplicationCoordinator::get(opCtx->getClient()->getServiceContext())
            ->canAcceptWritesForDatabase(db->name());

    if (isPrimary && !collectionIsSharded(opCtx, sourceNss)) {
        return BSONObj();
    }

    BSONObjBuilder viewDetailBob;
    viewDetailBob.append("ns", sourceNss.ns());
    viewDetailBob.append("pipeline", resolvedView.getValue().getPipeline());

    return viewDetailBob.obj();
}

void ViewShardingCheck::appendShardedViewStatus(const BSONObj& resolvedView, BSONObjBuilder* out) {
    invariant(out);
    invariant(!resolvedView.isEmpty());

    out->append("resolvedView", resolvedView);
    Status status{ErrorCodes::CommandOnShardedViewNotSupportedOnBongod,
                  str::stream() << "Command on view must be executed by bongos"};
    Command::appendCommandStatus(*out, status);
}

bool ViewShardingCheck::collectionIsSharded(OperationContext* opCtx, const NamespaceString& nss) {
    // The database is locked at this point but the collection underlying the given view is not
    // and must be for a sharding check.
    dassert(opCtx->lockState()->isDbLockedForMode(nss.db(), MODE_IS));
    AutoGetCollection autoGetCol(opCtx, nss, MODE_IS);
    return CollectionShardingState::get(opCtx, nss)->collectionIsSharded();
}

}  // namespace bongo
