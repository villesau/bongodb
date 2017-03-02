/**
 * Tests that the following bongod command line options are incompatible with --queryableBackupMode:
 *   --replSet
 *   --configsvr
 *   --upgrade
 *   --repair
 *   --profile
 */

// Check that starting bongod with both --queryableBackupMode and --replSet fails.
(function() {
    "use strict";

    var name = "queryable_backup_mode_repl_set";
    var dbdir = BongoRunner.dataPath + name + "/";

    resetDbpath(dbdir);

    // Insert dummy document to ensure startup failure isn't due to lack of storage metadata file.
    var conn = BongoRunner.runBongod({dbpath: dbdir, noCleanData: true});
    assert.neq(null, conn, "bongod was unable to start up");

    var coll = conn.getCollection('test.foo');
    coll.insertOne({a: 1});
    BongoRunner.stopBongod(conn);

    conn = BongoRunner.runBongod(
        {dbpath: dbdir, noCleanData: true, queryableBackupMode: '', replSet: 'bar'});

    assert.eq(
        null,
        conn,
        "bongod should fail to start when both --queryableBackupMode and --replSet are provided");

    conn = BongoRunner.runBongod(
        {dbpath: dbdir, noCleanData: true, queryableBackupMode: '', configsvr: ''});

    assert.eq(
        null,
        conn,
        "bongod should fail to start when both --queryableBackupMode and --configsvr are provided");

    conn = BongoRunner.runBongod(
        {dbpath: dbdir, noCleanData: true, queryableBackupMode: '', upgrade: ''});

    assert.eq(
        null,
        conn,
        "bongod should fail to start when both --queryableBackupMode and --upgrade are provided");

    conn = BongoRunner.runBongod(
        {dbpath: dbdir, noCleanData: true, queryableBackupMode: '', repair: ''});

    assert.eq(
        null,
        conn,
        "bongod should fail to start when both --queryableBackupMode and --repair are provided");

    conn = BongoRunner.runBongod(
        {dbpath: dbdir, noCleanData: true, queryableBackupMode: '', profile: 1});

    assert.eq(
        null,
        conn,
        "bongod should fail to start when both --queryableBackupMode and --profile are provided");
})();
