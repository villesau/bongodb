/**
 * Test that queries can be routed to the right secondaries that are caught
 * up with the primary.
 */
(function() {

    var rsOpts = {nodes: 2};
    var st = new ShardingTest({bongos: 2, shards: {rs0: rsOpts, rs1: rsOpts}});

    st.s0.adminCommand({enableSharding: 'test'});

    st.ensurePrimaryShard('test', st.shard0.shardName);
    st.s0.adminCommand({shardCollection: 'test.user', key: {x: 1}});
    st.s0.adminCommand({split: 'test.user', middle: {x: 0}});

    st.s1.setReadPref('secondary');
    var testDB = st.s1.getDB('test');
    // This establishes the shard version Bongos #1's view.
    testDB.user.insert({x: 1});

    // Bongos #0 bumps up the version without Bongos #1 knowledge.
    // Note: moveChunk has implicit { w: 2 } write concern.
    st.s0.adminCommand(
        {moveChunk: 'test.user', find: {x: 0}, to: st.shard1.shardName, _waitForDelete: true});

    // Clear all the connections to make sure that Bongos #1 will attempt to establish
    // the shard version.
    assert.commandWorked(testDB.adminCommand({connPoolSync: 1}));

    // Bongos #1 performs a query to the secondary.
    var res = testDB.runReadCommand({count: 'user', query: {x: 1}});
    assert(res.ok);
    assert.eq(1, res.n, tojson(res));

    st.stop();
})();
