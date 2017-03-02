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
 *    must comply with the GNU Affero General Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#include "bongo/platform/basic.h"

#include "bongo/s/query/store_possible_cursor.h"

#include "bongo/base/status_with.h"
#include "bongo/bson/bsonobj.h"
#include "bongo/db/query/cursor_response.h"
#include "bongo/s/query/cluster_client_cursor_impl.h"
#include "bongo/s/query/cluster_client_cursor_params.h"
#include "bongo/s/query/cluster_cursor_manager.h"

namespace bongo {

StatusWith<BSONObj> storePossibleCursor(OperationContext* txn,
                                        const HostAndPort& server,
                                        const BSONObj& cmdResult,
                                        const NamespaceString& requestedNss,
                                        executor::TaskExecutor* executor,
                                        ClusterCursorManager* cursorManager) {
    if (!cmdResult["ok"].trueValue() || !cmdResult.hasField("cursor")) {
        return cmdResult;
    }

    auto incomingCursorResponse = CursorResponse::parseFromBSON(cmdResult);
    if (!incomingCursorResponse.isOK()) {
        return incomingCursorResponse.getStatus();
    }

    if (incomingCursorResponse.getValue().getCursorId() == CursorId(0)) {
        return cmdResult;
    }

    ClusterClientCursorParams params(incomingCursorResponse.getValue().getNSS());
    params.remotes.emplace_back(server, incomingCursorResponse.getValue().getCursorId());


    auto ccc = ClusterClientCursorImpl::make(txn, executor, std::move(params));

    auto clusterCursorId =
        cursorManager->registerCursor(txn,
                                      ccc.releaseCursor(),
                                      requestedNss,
                                      ClusterCursorManager::CursorType::NamespaceNotSharded,
                                      ClusterCursorManager::CursorLifetime::Mortal);
    if (!clusterCursorId.isOK()) {
        return clusterCursorId.getStatus();
    }

    CursorResponse outgoingCursorResponse(
        requestedNss, clusterCursorId.getValue(), incomingCursorResponse.getValue().getBatch());
    return outgoingCursorResponse.toBSON(CursorResponse::ResponseType::InitialResponse);
}

}  // namespace bongo
