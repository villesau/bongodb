/**
 *    Copyright (C) 2016 BongoDB, Inc.
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

#include "bongo/db/pipeline/aggregation_request.h"

#include "bongo/bson/bsonobj.h"
#include "bongo/bson/bsonobjbuilder.h"
#include "bongo/bson/json.h"
#include "bongo/db/catalog/document_validation.h"
#include "bongo/db/namespace_string.h"
#include "bongo/db/pipeline/document.h"
#include "bongo/db/pipeline/document_value_test_util.h"
#include "bongo/db/pipeline/value.h"
#include "bongo/unittest/unittest.h"
#include "bongo/util/assert_util.h"

namespace bongo {
namespace {

const Document kDefaultCursorOptionDocument{
    {AggregationRequest::kBatchSizeName, AggregationRequest::kDefaultBatchSize}};

//
// Parsing
//

TEST(AggregationRequestTest, ShouldParseAllKnownOptions) {
    NamespaceString nss("a.collection");
    const BSONObj inputBson = fromjson(
        "{pipeline: [{$match: {a: 'abc'}}], explain: true, allowDiskUse: true, fromRouter: true, "
        "bypassDocumentValidation: true, collation: {locale: 'en_US'}, cursor: {batchSize: 10}, "
        "hint: {a: 1}}");
    auto request = unittest::assertGet(AggregationRequest::parseFromBSON(nss, inputBson));
    ASSERT_TRUE(request.isExplain());
    ASSERT_TRUE(request.shouldAllowDiskUse());
    ASSERT_TRUE(request.isFromRouter());
    ASSERT_TRUE(request.shouldBypassDocumentValidation());
    ASSERT_EQ(request.getBatchSize(), 10);
    ASSERT_BSONOBJ_EQ(request.getHint(), BSON("a" << 1));
    ASSERT_BSONOBJ_EQ(request.getCollation(),
                      BSON("locale"
                           << "en_US"));
}

//
// Serialization
//

TEST(AggregationRequestTest, ShouldOnlySerializeRequiredFieldsIfNoOptionalFieldsAreSpecified) {
    NamespaceString nss("a.collection");
    AggregationRequest request(nss, {});

    auto expectedSerialization =
        Document{{AggregationRequest::kCommandName, nss.coll()},
                 {AggregationRequest::kPipelineName, Value(std::vector<Value>{})},
                 {AggregationRequest::kCursorName, Value(kDefaultCursorOptionDocument)}};
    ASSERT_DOCUMENT_EQ(request.serializeToCommandObj(), expectedSerialization);
}

TEST(AggregationRequestTest, ShouldNotSerializeOptionalValuesIfEquivalentToDefault) {
    NamespaceString nss("a.collection");
    AggregationRequest request(nss, {});
    request.setExplain(false);
    request.setAllowDiskUse(false);
    request.setFromRouter(false);
    request.setBypassDocumentValidation(false);
    request.setCollation(BSONObj());
    request.setHint(BSONObj());

    auto expectedSerialization =
        Document{{AggregationRequest::kCommandName, nss.coll()},
                 {AggregationRequest::kPipelineName, Value(std::vector<Value>{})},
                 {AggregationRequest::kCursorName, Value(kDefaultCursorOptionDocument)}};
    ASSERT_DOCUMENT_EQ(request.serializeToCommandObj(), expectedSerialization);
}

TEST(AggregationRequestTest, ShouldSerializeOptionalValuesIfSet) {
    NamespaceString nss("a.collection");
    AggregationRequest request(nss, {});
    request.setExplain(true);
    request.setAllowDiskUse(true);
    request.setFromRouter(true);
    request.setBypassDocumentValidation(true);
    request.setBatchSize(10);  // batchSize not serialzed when explain is true.
    const auto hintObj = BSON("a" << 1);
    request.setHint(hintObj);
    const auto collationObj = BSON("locale"
                                   << "en_US");
    request.setCollation(collationObj);

    auto expectedSerialization =
        Document{{AggregationRequest::kCommandName, nss.coll()},
                 {AggregationRequest::kPipelineName, Value(std::vector<Value>{})},
                 {AggregationRequest::kExplainName, true},
                 {AggregationRequest::kAllowDiskUseName, true},
                 {AggregationRequest::kFromRouterName, true},
                 {bypassDocumentValidationCommandOption(), true},
                 {AggregationRequest::kCollationName, collationObj},
                 {AggregationRequest::kHintName, hintObj}};
    ASSERT_DOCUMENT_EQ(request.serializeToCommandObj(), expectedSerialization);
}

TEST(AggregationRequestTest, ShouldSerializeBatchSizeIfSetAndExplainFalse) {
    NamespaceString nss("a.collection");
    AggregationRequest request(nss, {});
    request.setBatchSize(10);

    auto expectedSerialization =
        Document{{AggregationRequest::kCommandName, nss.coll()},
                 {AggregationRequest::kPipelineName, Value(std::vector<Value>{})},
                 {AggregationRequest::kCursorName,
                  Value(Document({{AggregationRequest::kBatchSizeName, 10}}))}};
    ASSERT_DOCUMENT_EQ(request.serializeToCommandObj(), expectedSerialization);
}

TEST(AggregationRequestTest, ShouldSetBatchSizeToDefaultOnEmptyCursorObject) {
    NamespaceString nss("a.collection");
    const BSONObj inputBson = fromjson("{pipeline: [{$match: {a: 'abc'}}], cursor: {}}");
    auto request = AggregationRequest::parseFromBSON(nss, inputBson);
    ASSERT_OK(request.getStatus());
    ASSERT_EQ(request.getValue().getBatchSize(), AggregationRequest::kDefaultBatchSize);
}

TEST(AggregationRequestTest, ShouldAcceptHintAsString) {
    NamespaceString nss("a.collection");
    const BSONObj inputBson =
        fromjson("{pipeline: [{$match: {a: 'abc'}}], hint: 'a_1', cursor: {}}");
    auto request = AggregationRequest::parseFromBSON(nss, inputBson);
    ASSERT_OK(request.getStatus());
    ASSERT_BSONOBJ_EQ(request.getValue().getHint(),
                      BSON("$hint"
                           << "a_1"));
}

//
// Error cases.
//

TEST(AggregationRequestTest, ShouldRejectNonArrayPipeline) {
    NamespaceString nss("a.collection");
    const BSONObj inputBson = fromjson("{pipeline: {}, cursor: {}}");
    ASSERT_NOT_OK(AggregationRequest::parseFromBSON(nss, inputBson).getStatus());
}

TEST(AggregationRequestTest, ShouldRejectPipelineArrayIfAnElementIsNotAnObject) {
    NamespaceString nss("a.collection");
    BSONObj inputBson = fromjson("{pipeline: [4], cursor: {}}");
    ASSERT_NOT_OK(AggregationRequest::parseFromBSON(nss, inputBson).getStatus());

    inputBson = fromjson("{pipeline: [{$match: {a: 'abc'}}, 4], cursor: {}}");
    ASSERT_NOT_OK(AggregationRequest::parseFromBSON(nss, inputBson).getStatus());
}

TEST(AggregationRequestTest, ShouldRejectNonObjectCollation) {
    NamespaceString nss("a.collection");
    const BSONObj inputBson =
        fromjson("{pipeline: [{$match: {a: 'abc'}}], cursor: {}, collation: 1}");
    ASSERT_NOT_OK(
        AggregationRequest::parseFromBSON(NamespaceString("a.collection"), inputBson).getStatus());
}

TEST(AggregationRequestTest, ShouldRejectNonStringNonObjectHint) {
    NamespaceString nss("a.collection");
    const BSONObj inputBson = fromjson("{pipeline: [{$match: {a: 'abc'}}], cursor: {}, hint: 1}");
    ASSERT_NOT_OK(
        AggregationRequest::parseFromBSON(NamespaceString("a.collection"), inputBson).getStatus());
}

TEST(AggregationRequestTest, ShouldRejectHintAsArray) {
    NamespaceString nss("a.collection");
    const BSONObj inputBson =
        fromjson("{pipeline: [{$match: {a: 'abc'}}], cursor: {}, hint: []}]}");
    ASSERT_NOT_OK(
        AggregationRequest::parseFromBSON(NamespaceString("a.collection"), inputBson).getStatus());
}

TEST(AggregationRequestTest, ShouldRejectNonBoolExplain) {
    NamespaceString nss("a.collection");
    const BSONObj inputBson =
        fromjson("{pipeline: [{$match: {a: 'abc'}}], cursor: {}, explain: 1}");
    ASSERT_NOT_OK(AggregationRequest::parseFromBSON(nss, inputBson).getStatus());
}

TEST(AggregationRequestTest, ShouldRejectNonBoolFromRouter) {
    NamespaceString nss("a.collection");
    const BSONObj inputBson =
        fromjson("{pipeline: [{$match: {a: 'abc'}}], cursor: {}, fromRouter: 1}");
    ASSERT_NOT_OK(AggregationRequest::parseFromBSON(nss, inputBson).getStatus());
}

TEST(AggregationRequestTest, ShouldRejectNonBoolAllowDiskUse) {
    NamespaceString nss("a.collection");
    const BSONObj inputBson =
        fromjson("{pipeline: [{$match: {a: 'abc'}}], cursor: {}, allowDiskUse: 1}");
    ASSERT_NOT_OK(AggregationRequest::parseFromBSON(nss, inputBson).getStatus());
}

TEST(AggregationRequestTest, ShouldRejectNoCursorNoExplain) {
    NamespaceString nss("a.collection");
    const BSONObj inputBson = fromjson("{pipeline: [{$match: {a: 'abc'}}]}");
    ASSERT_NOT_OK(AggregationRequest::parseFromBSON(nss, inputBson).getStatus());
}

//
// Ignore fields parsed elsewhere.
//

TEST(AggregationRequestTest, ShouldIgnoreFieldsPrefixedWithDollar) {
    NamespaceString nss("a.collection");
    const BSONObj inputBson =
        fromjson("{pipeline: [{$match: {a: 'abc'}}], cursor: {}, $unknown: 1}");
    ASSERT_OK(AggregationRequest::parseFromBSON(nss, inputBson).getStatus());
}

TEST(AggregationRequestTest, ShouldIgnoreWriteConcernOption) {
    NamespaceString nss("a.collection");
    const BSONObj inputBson =
        fromjson("{pipeline: [{$match: {a: 'abc'}}], cursor: {}, writeConcern: 'invalid'}");
    ASSERT_OK(AggregationRequest::parseFromBSON(nss, inputBson).getStatus());
}

TEST(AggregationRequestTest, ShouldIgnoreMaxTimeMsOption) {
    NamespaceString nss("a.collection");
    const BSONObj inputBson =
        fromjson("{pipeline: [{$match: {a: 'abc'}}], cursor: {}, maxTimeMS: 'invalid'}");
    ASSERT_OK(AggregationRequest::parseFromBSON(nss, inputBson).getStatus());
}

TEST(AggregationRequestTest, ShouldIgnoreReadConcernOption) {
    NamespaceString nss("a.collection");
    const BSONObj inputBson =
        fromjson("{pipeline: [{$match: {a: 'abc'}}], cursor: {}, readConcern: 'invalid'}");
    ASSERT_OK(AggregationRequest::parseFromBSON(nss, inputBson).getStatus());
}

}  // namespace
}  // namespace bongo
