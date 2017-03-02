// dumprestore_helpers.js

load('./jstests/multiVersion/libs/verify_collection_data.js');

// Given a "test spec" object, runs the specified test.
//
// The "test spec" object has the format:
//
// {
//     'serverSourceVersion' : "latest",
//     'serverDestVersion' : "2.4",
//     'bongoDumpVersion' : "2.2",
//     'bongoRestoreVersion' : "latest",
//     'dumpDir' : dumpDir,
//     'testDbpath' : testDbpath,
//     'dumpType' : "bongos",
//     'restoreType' : "bongod", // "bongos" also supported
//     'storageEngine': [ "mmapv1" ]
// }
//
// The first four fields are which versions of the various binaries to use in the test.
//
// The "dumpDir" field is the external directory to use as scratch space for database dumps.
//
// The "testDbpath" is the external directory to use as the server dbpath directory.
//
// For the "dumpType" and "restoreType" fields, the following values are supported:
//     - "bongod" - Do the dump or restore by connecting to a single bongod node
//     - "bongos" - Do the dump or restore by connecting to a sharded cluster
//
function multiVersionDumpRestoreTest(configObj) {
    // First sanity check the arguments in our configObj
    var requiredKeys = [
        'serverSourceVersion',
        'serverDestVersion',
        'bongoDumpVersion',
        'bongoRestoreVersion',
        'dumpDir',
        'testDbpath',
        'dumpType',
        'restoreType',
        'storageEngine'
    ];

    var i;

    for (i = 0; i < requiredKeys.length; i++) {
        assert(configObj.hasOwnProperty(requiredKeys[i]),
               "Missing required key: " + requiredKeys[i] + " in config object");
    }

    resetDbpath(configObj.dumpDir);
    resetDbpath(configObj.testDbpath);
    if (configObj.dumpType === "bongos") {
        var shardingTestConfig = {
            name: testBaseName + "_sharded_source",
            bongos: [{binVersion: configObj.serverSourceVersion}],
            shards: [{
                binVersion: configObj.serverSourceVersion,
                storageEngine: configObj.storageEngine
            }],
            config: [{binVersion: configObj.serverSourceVersion}]
        };
        var shardingTest = new ShardingTest(shardingTestConfig);
        var serverSource = shardingTest.s;
    } else {
        var serverSource = BongoRunner.runBongod({
            binVersion: configObj.serverSourceVersion,
            dbpath: configObj.testDbpath,
            storageEngine: configObj.storageEngine
        });
    }
    var sourceDB = serverSource.getDB(testBaseName);

    // Create generators to create collections with our seed data
    // Testing with both a capped collection and a normal collection
    var cappedCollGen = new CollectionDataGenerator({"capped": true});
    var collGen = new CollectionDataGenerator({"capped": false});

    // Create collections using the different generators
    var sourceCollCapped = createCollectionWithData(sourceDB, "cappedColl", cappedCollGen);
    var sourceColl = createCollectionWithData(sourceDB, "coll", collGen);

    // Record the current collection states for later validation
    var cappedCollValid = new CollectionDataValidator();
    cappedCollValid.recordCollectionData(sourceCollCapped);
    var collValid = new CollectionDataValidator();
    collValid.recordCollectionData(sourceColl);

    // Dump using the specified version of bongodump from the running bongod or bongos instance.
    if (configObj.dumpType === "bongod") {
        BongoRunner.runBongoTool("bongodump", {
            out: configObj.dumpDir,
            binVersion: configObj.bongoDumpVersion,
            host: serverSource.host,
            db: testBaseName
        });
        BongoRunner.stopBongod(serverSource.port);
    } else { /* "bongos" */
        BongoRunner.runBongoTool("bongodump", {
            out: configObj.dumpDir,
            binVersion: configObj.bongoDumpVersion,
            host: serverSource.host,
            db: testBaseName
        });
        shardingTest.stop();
    }

    // Restore using the specified version of bongorestore
    if (configObj.restoreType === "bongod") {
        var serverDest = BongoRunner.runBongod(
            {binVersion: configObj.serverDestVersion, storageEngine: configObj.storageEngine});

        BongoRunner.runBongoTool("bongorestore", {
            dir: configObj.dumpDir + "/" + testBaseName,
            binVersion: configObj.bongoRestoreVersion,
            host: serverDest.host,
            db: testBaseName
        });
    } else { /* "bongos" */
        var shardingTestConfig = {
            name: testBaseName + "_sharded_dest",
            bongos: [{binVersion: configObj.serverDestVersion}],
            shards:
                [{binVersion: configObj.serverDestVersion, storageEngine: configObj.storageEngine}],
            config: [{binVersion: configObj.serverDestVersion}]
        };
        var shardingTest = new ShardingTest(shardingTestConfig);
        serverDest = shardingTest.s;
        BongoRunner.runBongoTool("bongorestore", {
            dir: configObj.dumpDir + "/" + testBaseName,
            binVersion: configObj.bongoRestoreVersion,
            host: serverDest.host,
            db: testBaseName
        });
    }

    var destDB = serverDest.getDB(testBaseName);

    // Get references to our destinations collections
    // XXX: These are in the global scope (no "var"), but they need to be global to be in scope for
    // the "assert.soon" calls below.
    destColl = destDB.getCollection("coll");
    destCollCapped = destDB.getCollection("cappedColl");

    // Wait until we actually have data or timeout
    assert.soon("destColl.findOne()", "no data after sleep");
    assert.soon("destCollCapped.findOne()", "no data after sleep");

    let destDbVersion = destDB.version();

    // The bongorestore tool removes the "v" field when creating indexes from a dump. This allows
    // indexes to be built with the latest supported index version. We therefore remove the "v"
    // field when comparing whether the indexes we built are equivalent.
    const options = {indexSpecFieldsToSkip: ["v"]};

    // Validate that our collections were properly restored
    assert(collValid.validateCollectionData(destColl, destDbVersion, options));
    assert(cappedCollValid.validateCollectionData(destCollCapped, destDbVersion, options));

    if (configObj.restoreType === "bongos") {
        shardingTest.stop();
    } else {
        BongoRunner.stopBongod(serverDest.port);
    }
}

// Given an object with list values, returns a cursor that returns an object for every combination
// of selections of the values in the lists.
//
// Usage:
// var permutationCursor = getPermutationIterator({"a":[0,1], "b":[2,3]});
// while (permutationCursor.hasNext()) {
//    var permutation = permutationCursor.next();
//    printjson(permutation);
// }
//
// This will print:
//
// { "a" : 0, "b" : 2 }
// { "a" : 1, "b" : 2 }
// { "a" : 0, "b" : 3 }
// { "a" : 1, "b" : 3 }
function getPermutationIterator(permsObj) {
    function getAllPermutations(permsObj) {
        // Split our permutations object into "first" and "rest"
        var gotFirst = false;
        var firstKey;
        var firstValues;
        var restObj = {};
        for (var key in permsObj) {
            if (permsObj.hasOwnProperty(key)) {
                if (gotFirst) {
                    restObj[key] = permsObj[key];
                } else {
                    firstKey = key;
                    firstValues = permsObj[key];
                    gotFirst = true;
                }
            }
        }

        // Our base case is an empty object, which just has a single permutation, "{}"
        if (!gotFirst) {
            return [{}];
        }

        // Iterate the possibilities for "first" and for each one recursively get all the
        // permutations for "rest"
        var resultPermObjs = [];
        var i = 0;
        var j = 0;
        for (i = 0; i < firstValues.length; i++) {
            var subPermObjs = getAllPermutations(restObj);
            for (j = 0; j < subPermObjs.length; j++) {
                subPermObjs[j][firstKey] = firstValues[i];
                resultPermObjs.push(subPermObjs[j]);
            }
        }
        return resultPermObjs;
    }

    var allPermutations = getAllPermutations(permsObj);
    var currentPermutation = 0;

    return {
        "next": function() {
            return allPermutations[currentPermutation++];
        },
        "hasNext": function() {
            return currentPermutation < allPermutations.length;
        }
    };
}

// Given a "test spec" object, runs all test combinations.
//
// The "test spec" object has the format:
//
// {
//     'serverSourceVersion' : [ "latest", "2.4" ],
//     'serverDestVersion' :[ "latest", "2.4" ],
//     'bongoDumpVersion' :[ "latest", "2.4" ],
//     'bongoRestoreVersion' :[ "latest", "2.4" ],
//     'dumpDir' : [ dumpDir ],
//     'testDbpath' : [ testDbpath ],
//     'dumpType' : [ "bongod", "bongos" ],
//     'restoreType' : [ "bongod", "bongos" ],
//     'storageEngine': [ "mmapv1" ]
// }
//
// This function will run a test for each possible combination of the parameters.  See comments on
// "getPermutationIterator" above.
function runAllDumpRestoreTests(testCasePermutations) {
    var testCaseCursor = getPermutationIterator(testCasePermutations);
    while (testCaseCursor.hasNext()) {
        var testCase = testCaseCursor.next();
        print("Running multiversion bongodump bongorestore test:");
        printjson(testCase);
        multiVersionDumpRestoreTest(testCase);
    }
}
