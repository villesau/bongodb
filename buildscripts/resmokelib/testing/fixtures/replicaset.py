"""
Replica set fixture for executing JSTests against.
"""

from __future__ import absolute_import

import os.path
import time

import pybongo

from . import interface
from . import standalone
from ... import config
from ... import logging
from ... import utils


class ReplicaSetFixture(interface.ReplFixture):
    """
    Fixture which provides JSTests with a replica set to run against.
    """

    # Error response codes copied from bongo/base/error_codes.err.
    _ALREADY_INITIALIZED = 23
    _NODE_NOT_FOUND = 74

    def __init__(self,
                 logger,
                 job_num,
                 bongod_executable=None,
                 bongod_options=None,
                 dbpath_prefix=None,
                 preserve_dbpath=False,
                 num_nodes=2,
                 start_initial_sync_node=False,
                 write_concern_majority_journal_default=None,
                 auth_options=None,
                 replset_config_options=None,
                 voting_secondaries=True):

        interface.ReplFixture.__init__(self, logger, job_num)

        self.bongod_executable = bongod_executable
        self.bongod_options = utils.default_if_none(bongod_options, {})
        self.preserve_dbpath = preserve_dbpath
        self.num_nodes = num_nodes
        self.start_initial_sync_node = start_initial_sync_node
        self.write_concern_majority_journal_default = write_concern_majority_journal_default
        self.auth_options = auth_options
        self.replset_config_options = utils.default_if_none(replset_config_options, {})
        self.voting_secondaries = voting_secondaries

        # The dbpath in bongod_options is used as the dbpath prefix for replica set members and
        # takes precedence over other settings. The ShardedClusterFixture uses this parameter to
        # create replica sets and assign their dbpath structure explicitly.
        if "dbpath" in self.bongod_options:
            self._dbpath_prefix = self.bongod_options.pop("dbpath")
        else:
            # Command line options override the YAML configuration.
            dbpath_prefix = utils.default_if_none(config.DBPATH_PREFIX, dbpath_prefix)
            dbpath_prefix = utils.default_if_none(dbpath_prefix, config.DEFAULT_DBPATH_PREFIX)
            self._dbpath_prefix = os.path.join(dbpath_prefix,
                                               "job%d" % (self.job_num),
                                               config.FIXTURE_SUBDIR)

        self.nodes = []
        self.replset_name = None
        self.initial_sync_node = None
        self.initial_sync_node_idx = -1

    def setup(self):
        self.replset_name = self.bongod_options.get("replSet", "rs")

        if not self.nodes:
            for i in xrange(self.num_nodes):
                node = self._new_bongod(i, self.replset_name)
                self.nodes.append(node)

        for node in self.nodes:
            node.setup()

        if self.start_initial_sync_node:
            if not self.initial_sync_node:
                self.initial_sync_node_idx = len(self.nodes)
                self.initial_sync_node = self._new_bongod(self.initial_sync_node_idx,
                                                          self.replset_name)
            self.initial_sync_node.setup()
            self.initial_sync_node.await_ready()

        self.port = self.get_primary().port

        # Call await_ready() on each of the nodes here because we want to start the election as
        # soon as possible.
        for node in self.nodes:
            node.await_ready()

        # Initiate the replica set.
        members = []
        for (i, node) in enumerate(self.nodes):
            member_info = {"_id": i, "host": node.get_connection_string()}
            if i > 0:
                member_info["priority"] = 0
                if i >= 7 or not self.voting_secondaries:
                    # Only 7 nodes in a replica set can vote, so the other members must still be
                    # non-voting when this fixture is configured to have voting secondaries.
                    member_info["votes"] = 0
            members.append(member_info)
        if self.initial_sync_node:
            members.append({"_id": self.initial_sync_node_idx,
                            "host": self.initial_sync_node.get_connection_string(),
                            "priority": 0,
                            "hidden": 1,
                            "votes": 0})

        initiate_cmd_obj = {"replSetInitiate": {"_id": self.replset_name, "members": members}}

        client = utils.new_bongo_client(port=self.port)
        if self.auth_options is not None:
            auth_db = client[self.auth_options["authenticationDatabase"]]
            auth_db.authenticate(self.auth_options["username"],
                                 password=self.auth_options["password"],
                                 mechanism=self.auth_options["authenticationMechanism"])

        if self.write_concern_majority_journal_default is not None:
            initiate_cmd_obj["replSetInitiate"]["writeConcernMajorityJournalDefault"] = self.write_concern_majority_journal_default
        else:
            serverStatus = client.admin.command({"serverStatus": 1})
            cmdLineOpts = client.admin.command({"getCmdLineOpts": 1})
            if not (serverStatus["storageEngine"]["persistent"] and
                    cmdLineOpts["parsed"].get("storage", {}).get("journal", {}).get("enabled", True)):
                initiate_cmd_obj["replSetInitiate"]["writeConcernMajorityJournalDefault"] = False

        if self.replset_config_options.get("configsvr", False):
            initiate_cmd_obj["replSetInitiate"]["configsvr"] = True
        if self.replset_config_options.get("settings"):
            replset_settings = self.replset_config_options["settings"]
            initiate_cmd_obj["replSetInitiate"]["settings"] = replset_settings

        self.logger.info("Issuing replSetInitiate command...%s", initiate_cmd_obj)

        # replSetInitiate and replSetReconfig commands can fail with a NodeNotFound error
        # if a heartbeat times out during the quorum check. We retry three times to reduce
        # the chance of failing this way.
        num_initiate_attempts = 3
        for attempt in range(1, num_initiate_attempts + 1):
            try:
                client.admin.command(initiate_cmd_obj)
                break
            except pybongo.errors.OperationFailure as err:
                # Ignore errors from the "replSetInitiate" command when the replica set has already
                # been initiated.
                if err.code == ReplicaSetFixture._ALREADY_INITIALIZED:
                    return

                # Retry on NodeNotFound errors from the "replSetInitiate" command.
                if err.code != ReplicaSetFixture._NODE_NOT_FOUND:
                    raise

                msg = "replSetInitiate failed attempt {0} of {1} with error: {2}".format(
                    attempt, num_initiate_attempts, err)
                self.logger.error(msg)
                if attempt == num_initiate_attempts:
                    raise
                time.sleep(5)  # Wait a little bit before trying again.

    def await_ready(self):
        # Wait for the primary to be elected.
        client = utils.new_bongo_client(port=self.port)
        while True:
            is_master = client.admin.command("isMaster")["ismaster"]
            if is_master:
                break
            self.logger.info("Waiting for primary on port %d to be elected.", self.port)
            time.sleep(0.1)  # Wait a little bit before trying again.

        secondaries = self.get_secondaries()
        if self.initial_sync_node:
            secondaries.append(self.initial_sync_node)

        # Wait for the secondaries to become available.
        for secondary in secondaries:
            client = utils.new_bongo_client(port=secondary.port,
                                            read_preference=pybongo.ReadPreference.SECONDARY)
            while True:
                is_secondary = client.admin.command("isMaster")["secondary"]
                if is_secondary:
                    break
                self.logger.info("Waiting for secondary on port %d to become available.",
                                 secondary.port)
                time.sleep(0.1)  # Wait a little bit before trying again.

    def teardown(self):
        running_at_start = self.is_running()
        success = True  # Still a success even if nothing is running.

        if not running_at_start:
            self.logger.info("Replica set was expected to be running in teardown(), but wasn't.")
        else:
            self.logger.info("Stopping all members of the replica set...")

        if self.initial_sync_node:
            success = self.initial_sync_node.teardown() and success

        # Terminate the secondaries first to reduce noise in the logs.
        for node in reversed(self.nodes):
            success = node.teardown() and success

        if running_at_start:
            self.logger.info("Successfully stopped all members of the replica set.")

        return success

    def is_running(self):
        running = all(node.is_running() for node in self.nodes)

        if self.initial_sync_node:
            running = self.initial_sync_node.is_running() or running

        return running

    def get_primary(self):
        # The primary is always the first element of the 'nodes' list because all other members of
        # the replica set are configured with priority=0.
        return self.nodes[0]

    def get_secondaries(self):
        return self.nodes[1:]

    def get_initial_sync_node(self):
        return self.initial_sync_node

    def _new_bongod(self, index, replset_name):
        """
        Returns a standalone.BongoDFixture configured to be used as a
        replica-set member of 'replset_name'.
        """

        bongod_logger = self._get_logger_for_bongod(index)
        bongod_options = self.bongod_options.copy()
        bongod_options["replSet"] = replset_name
        bongod_options["dbpath"] = os.path.join(self._dbpath_prefix, "node%d" % (index))

        return standalone.BongoDFixture(bongod_logger,
                                        self.job_num,
                                        bongod_executable=self.bongod_executable,
                                        bongod_options=bongod_options,
                                        preserve_dbpath=self.preserve_dbpath)

    def _get_logger_for_bongod(self, index):
        """
        Returns a new logging.Logger instance for use as the primary, secondary, or initial
        sync member of a replica-set.
        """

        if index == 0:
            logger_name = "%s:primary" % (self.logger.name)
        elif index == self.initial_sync_node_idx:
            logger_name = "%s:initsync" % (self.logger.name)
        else:
            suffix = str(index - 1) if self.num_nodes > 2 else ""
            logger_name = "%s:secondary%s" % (self.logger.name, suffix)

        return logging.loggers.new_logger(logger_name, parent=self.logger)

    def get_connection_string(self):
        if self.replset_name is None:
            raise ValueError("Must call setup() before calling get_connection_string()")

        conn_strs = [node.get_connection_string() for node in self.nodes]
        if self.initial_sync_node:
            conn_strs.append(self.initial_sync_node.get_connection_string())
        return self.replset_name + "/" + ",".join(conn_strs)
