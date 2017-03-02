/**
 * Tests that logged users will not show up in the log.
 *
 * @param bongo {Bongo} connection object.
 */
var doTest = function(bongo, callSetParam) {
    var TEST_USER = 'foo';
    var TEST_PWD = 'bar';
    var testDB = bongo.getDB('test');

    testDB.createUser({user: TEST_USER, pwd: TEST_PWD, roles: jsTest.basicUserRoles});
    testDB.auth(TEST_USER, TEST_PWD);

    testDB.runCommand({dbStats: 1});

    var log = testDB.adminCommand({getLog: 'global'});
    log.log.forEach(function(line) {
        assert.eq(-1, line.indexOf('user: foo@'), 'user logged: ' + line);
    });

    // logUserIds should not be settable
    var res = testDB.runCommand({setParameter: 1, logUserIds: 1});
    assert(!res.ok);

    testDB.runCommand({dbStats: 1});

    log = testDB.adminCommand({getLog: 'global'});
    log.log.forEach(function(line) {
        assert.eq(-1, line.indexOf('user: foo@'), 'user logged: ' + line);
    });
};

var bongo = BongoRunner.runBongod({verbose: 5});
doTest(bongo);
BongoRunner.stopBongod(bongo.port);

var st = new ShardingTest({shards: 1, verbose: 5});
doTest(st.s);
st.stop();
