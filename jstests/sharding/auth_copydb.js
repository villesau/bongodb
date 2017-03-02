// Tests the copydb command on bongos with auth
(function() {
    'use strict';

    var st = new ShardingTest({shards: 1, bongos: 1, other: {keyFile: 'jstests/libs/key1'}});
    var bongos = st.s0;
    var destAdminDB = bongos.getDB('admin');
    var destTestDB = bongos.getDB('test');

    var sourceBongodConn = BongoRunner.runBongod({});
    var sourceTestDB = sourceBongodConn.getDB('test');

    sourceTestDB.foo.insert({a: 1});

    destAdminDB.createUser({
        user: 'admin',
        pwd: 'password',
        roles: jsTest.adminUserRoles
    });  // Turns on access control enforcement

    jsTestLog("Running copydb that should fail");
    var res = destAdminDB.runCommand(
        {copydb: 1, fromhost: sourceBongodConn.host, fromdb: 'test', todb: 'test'});
    printjson(res);
    assert.commandFailed(res);

    destAdminDB.auth('admin', 'password');
    assert.eq(0, destTestDB.foo.count());  // Be extra sure the copydb didn't secretly succeed.

    jsTestLog("Running copydb that should succeed");
    res = destAdminDB.runCommand(
        {copydb: 1, fromhost: sourceBongodConn.host, fromdb: 'test', todb: 'test'});
    printjson(res);
    assert.commandWorked(res);

    assert.eq(1, destTestDB.foo.count());
    assert.eq(1, destTestDB.foo.findOne().a);

    st.stop();
})();
