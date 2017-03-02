/**
 * Copyright (C) 2015 BongoDB Inc.
 *
 * This program is free software: you can redistribute it and/or  modify
 * it under the terms of the GNU Affero General Public License, version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, the copyright holders give permission to link the
 * code of portions of this program with the OpenSSL library under certain
 * conditions as described in each individual source file and distribute
 * linked combinations including the program with the OpenSSL library. You
 * must comply with the GNU Affero General Public License in all respects
 * for all of the code used other than as permitted herein. If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so. If you do not
 * wish to do so, delete this exception statement from your version. If you
 * delete this exception statement from all source files in the program,
 * then also delete it in the license file.
 */

#include "bongo/platform/basic.h"

#include "bongo/scripting/mozjs/cursor.h"

#include "bongo/scripting/mozjs/bson.h"
#include "bongo/scripting/mozjs/implscope.h"
#include "bongo/scripting/mozjs/internedstring.h"
#include "bongo/scripting/mozjs/objectwrapper.h"
#include "bongo/scripting/mozjs/valuereader.h"
#include "bongo/scripting/mozjs/wrapconstrainedmethod.h"

namespace bongo {
namespace mozjs {

const JSFunctionSpec CursorInfo::methods[6] = {
    BONGO_ATTACH_JS_CONSTRAINED_METHOD_NO_PROTO(close, CursorInfo),
    BONGO_ATTACH_JS_CONSTRAINED_METHOD_NO_PROTO(hasNext, CursorInfo),
    BONGO_ATTACH_JS_CONSTRAINED_METHOD_NO_PROTO(next, CursorInfo),
    BONGO_ATTACH_JS_CONSTRAINED_METHOD_NO_PROTO(objsLeftInBatch, CursorInfo),
    BONGO_ATTACH_JS_CONSTRAINED_METHOD_NO_PROTO(readOnly, CursorInfo),
    JS_FS_END,
};

const char* const CursorInfo::className = "Cursor";

namespace {

DBClientCursor* getCursor(JSObject* thisv) {
    return static_cast<CursorInfo::CursorHolder*>(JS_GetPrivate(thisv))->cursor.get();
}

DBClientCursor* getCursor(JS::CallArgs& args) {
    return getCursor(args.thisv().toObjectOrNull());
}

}  // namespace

void CursorInfo::finalize(JSFreeOp* fop, JSObject* obj) {
    auto cursor = static_cast<CursorInfo::CursorHolder*>(JS_GetPrivate(obj));

    if (cursor) {
        delete cursor;
    }
}

void CursorInfo::Functions::next::call(JSContext* cx, JS::CallArgs args) {
    auto cursor = getCursor(args);

    if (!cursor) {
        args.rval().setUndefined();
        return;
    }

    ObjectWrapper o(cx, args.thisv());

    BSONObj bson = cursor->next();
    bool ro = o.hasField(InternedString::_ro) ? o.getBoolean(InternedString::_ro) : false;

    // getOwned because cursor->next() gives us unowned bson from an internal
    // buffer and we need to make a copy
    ValueReader(cx, args.rval()).fromBSON(bson.getOwned(), nullptr, ro);
}

void CursorInfo::Functions::hasNext::call(JSContext* cx, JS::CallArgs args) {
    auto cursor = getCursor(args);

    if (!cursor) {
        args.rval().setBoolean(false);
        return;
    }

    args.rval().setBoolean(cursor->more());
}

void CursorInfo::Functions::objsLeftInBatch::call(JSContext* cx, JS::CallArgs args) {
    auto cursor = getCursor(args);

    if (!cursor) {
        args.rval().setInt32(0);
        return;
    }

    args.rval().setInt32(cursor->objsLeftInBatch());
}

void CursorInfo::Functions::readOnly::call(JSContext* cx, JS::CallArgs args) {
    ObjectWrapper(cx, args.thisv()).setBoolean(InternedString::_ro, true);

    args.rval().set(args.thisv());
}

void CursorInfo::Functions::close::call(JSContext* cx, JS::CallArgs args) {
    auto cursor = getCursor(args);

    if (cursor)
        cursor->kill();

    args.rval().setUndefined();
}

}  // namespace mozjs
}  // namespace bongo
