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

#include "bongo/db/catalog/create_collection.h"

#include "bongo/bson/bsonobj.h"
#include "bongo/db/concurrency/write_conflict_exception.h"
#include "bongo/db/curop.h"
#include "bongo/db/db_raii.h"
#include "bongo/db/namespace_string.h"
#include "bongo/db/operation_context.h"
#include "bongo/db/ops/insert.h"
#include "bongo/db/repl/replication_coordinator_global.h"

namespace bongo {
Status createCollection(OperationContext* txn,
                        const std::string& dbName,
                        const BSONObj& cmdObj,
                        const BSONObj& idIndex) {
    BSONObjIterator it(cmdObj);

    // Extract ns from first cmdObj element.
    BSONElement firstElt = it.next();
    uassert(ErrorCodes::TypeMismatch,
            str::stream() << "Expected first element to be of type String in: " << cmdObj,
            firstElt.type() == BSONType::String);
    uassert(15888, "must pass name of collection to create", !firstElt.valueStringData().empty());

    Status status = userAllowedCreateNS(dbName, firstElt.valueStringData());
    if (!status.isOK()) {
        return status;
    }

    const NamespaceString nss(dbName, firstElt.valueStringData());

    // Build options object from remaining cmdObj elements.
    BSONObjBuilder optionsBuilder;
    while (it.more()) {
        optionsBuilder.append(it.next());
    }

    BSONObj options = optionsBuilder.obj();
    uassert(14832,
            "specify size:<n> when capped is true",
            !options["capped"].trueValue() || options["size"].isNumber() ||
                options.hasField("$nExtents"));

    BONGO_WRITE_CONFLICT_RETRY_LOOP_BEGIN {
        ScopedTransaction transaction(txn, MODE_IX);
        Lock::DBLock dbXLock(txn->lockState(), dbName, MODE_X);
        OldClientContext ctx(txn, nss.ns());
        if (txn->writesAreReplicated() &&
            !repl::getGlobalReplicationCoordinator()->canAcceptWritesFor(nss)) {
            return Status(ErrorCodes::NotMaster,
                          str::stream() << "Not primary while creating collection " << nss.ns());
        }

        WriteUnitOfWork wunit(txn);

        // Create collection.
        const bool createDefaultIndexes = true;
        status = userCreateNS(txn, ctx.db(), nss.ns(), options, createDefaultIndexes, idIndex);
        if (!status.isOK()) {
            return status;
        }

        wunit.commit();
    }
    BONGO_WRITE_CONFLICT_RETRY_LOOP_END(txn, "create", nss.ns());
    return Status::OK();
}
}  // namespace bongo
