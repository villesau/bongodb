/**
 *    Copyright (C) 2016 BongoDB Inc.
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
 *    must comply with the GNU Affero General Public License in all respects
 *    for all of the code used other than as permitted herein. If you modify
 *    file(s) with this exception, you may extend this exception to your
 *    version of the file(s), but you are not obligated to do so. If you do not
 *    wish to do so, delete this exception statement from your version. If you
 *    delete this exception statement from all source files in the program,
 *    then also delete it in the license file.
 */

#define BONGO_LOG_DEFAULT_COMPONENT ::bongo::logger::LogComponent::kStorage

#include "bongo/platform/basic.h"

#include <boost/filesystem/operations.hpp>
#include <boost/optional.hpp>
#include <fstream>
#include <iostream>
#include <limits>
#include <signal.h>
#include <string>

#include "bongo/base/checked_cast.h"
#include "bongo/base/init.h"
#include "bongo/base/initializer.h"
#include "bongo/base/status.h"
#include "bongo/client/replica_set_monitor.h"
#include "bongo/config.h"
#include "bongo/db/audit.h"
#include "bongo/db/auth/auth_index_d.h"
#include "bongo/db/auth/authorization_manager.h"
#include "bongo/db/auth/authorization_manager_global.h"
#include "bongo/db/catalog/collection.h"
#include "bongo/db/catalog/database.h"
#include "bongo/db/catalog/database_catalog_entry.h"
#include "bongo/db/catalog/database_holder.h"
#include "bongo/db/catalog/index_catalog.h"
#include "bongo/db/catalog/index_key_validate.h"
#include "bongo/db/client.h"
#include "bongo/db/clientcursor.h"
#include "bongo/db/commands/feature_compatibility_version.h"
#include "bongo/db/concurrency/d_concurrency.h"
#include "bongo/db/concurrency/lock_state.h"
#include "bongo/db/db_raii.h"
#include "bongo/db/dbdirectclient.h"
#include "bongo/db/dbhelpers.h"
#include "bongo/db/dbmessage.h"
#include "bongo/db/dbwebserver.h"
#include "bongo/db/diag_log.h"
#include "bongo/db/exec/working_set_common.h"
#include "bongo/db/ftdc/ftdc_bongod.h"
#include "bongo/db/index_names.h"
#include "bongo/db/index_rebuilder.h"
#include "bongo/db/initialize_server_global_state.h"
#include "bongo/db/initialize_snmp.h"
#include "bongo/db/introspect.h"
#include "bongo/db/json.h"
#include "bongo/db/log_process_details.h"
#include "bongo/db/logical_clock.h"
#include "bongo/db/bongod_options.h"
#include "bongo/db/op_observer_impl.h"
#include "bongo/db/operation_context.h"
#include "bongo/db/query/internal_plans.h"
#include "bongo/db/range_deleter_service.h"
#include "bongo/db/repair_database.h"
#include "bongo/db/repl/oplog.h"
#include "bongo/db/repl/repl_settings.h"
#include "bongo/db/repl/replication_coordinator_external_state_impl.h"
#include "bongo/db/repl/replication_coordinator_global.h"
#include "bongo/db/repl/replication_coordinator_impl.h"
#include "bongo/db/repl/storage_interface_impl.h"
#include "bongo/db/repl/topology_coordinator_impl.h"
#include "bongo/db/restapi.h"
#include "bongo/db/s/balancer/balancer.h"
#include "bongo/db/s/sharding_initialization_bongod.h"
#include "bongo/db/s/sharding_state.h"
#include "bongo/db/s/sharding_state_recovery.h"
#include "bongo/db/s/type_shard_identity.h"
#include "bongo/db/server_options.h"
#include "bongo/db/server_parameters.h"
#include "bongo/db/service_context.h"
#include "bongo/db/service_context_d.h"
#include "bongo/db/service_entry_point_bongod.h"
#include "bongo/db/startup_warnings_bongod.h"
#include "bongo/db/stats/counters.h"
#include "bongo/db/stats/snapshots.h"
#include "bongo/db/storage/mmap_v1/mmap_v1_options.h"
#include "bongo/db/storage/storage_engine.h"
#include "bongo/db/storage/storage_options.h"
#include "bongo/db/storage/wiredtiger/wiredtiger_customization_hooks.h"
#include "bongo/db/ttl.h"
#include "bongo/db/wire_version.h"
#include "bongo/executor/network_connection_hook.h"
#include "bongo/executor/network_interface_factory.h"
#include "bongo/platform/process_id.h"
#include "bongo/rpc/metadata/egress_metadata_hook_list.h"
#include "bongo/s/client/shard_registry.h"
#include "bongo/s/grid.h"
#include "bongo/s/sharding_initialization.h"
#include "bongo/scripting/dbdirectclient_factory.h"
#include "bongo/scripting/engine.h"
#include "bongo/stdx/future.h"
#include "bongo/stdx/memory.h"
#include "bongo/stdx/thread.h"
#include "bongo/transport/transport_layer_legacy.h"
#include "bongo/util/assert_util.h"
#include "bongo/util/cmdline_utils/censor_cmdline.h"
#include "bongo/util/concurrency/task.h"
#include "bongo/util/concurrency/thread_name.h"
#include "bongo/util/exception_filter_win32.h"
#include "bongo/util/exit.h"
#include "bongo/util/fail_point_service.h"
#include "bongo/util/fast_clock_source_factory.h"
#include "bongo/util/log.h"
#include "bongo/util/net/listen.h"
#include "bongo/util/net/ssl_manager.h"
#include "bongo/util/ntservice.h"
#include "bongo/util/options_parser/startup_options.h"
#include "bongo/util/quick_exit.h"
#include "bongo/util/ramlog.h"
#include "bongo/util/scopeguard.h"
#include "bongo/util/signal_handlers.h"
#include "bongo/util/stacktrace.h"
#include "bongo/util/startup_test.h"
#include "bongo/util/text.h"
#include "bongo/util/time_support.h"
#include "bongo/util/version.h"

#if !defined(_WIN32)
#include <sys/file.h>
#endif

namespace bongo {

using std::unique_ptr;
using std::cout;
using std::cerr;
using std::endl;
using std::list;
using std::string;
using std::stringstream;
using std::vector;

using logger::LogComponent;

extern int diagLogging;

namespace {

const NamespaceString startupLogCollectionName("local.startup_log");
const NamespaceString kSystemReplSetCollection("local.system.replset");

#ifdef _WIN32
ntservice::NtServiceDefaultStrings defaultServiceStrings = {
    L"BongoDB", L"BongoDB", L"BongoDB Server"};
#endif

Timer startupSrandTimer;

void logStartup(OperationContext* txn) {
    BSONObjBuilder toLog;
    stringstream id;
    id << getHostNameCached() << "-" << jsTime().asInt64();
    toLog.append("_id", id.str());
    toLog.append("hostname", getHostNameCached());

    toLog.appendTimeT("startTime", time(0));
    toLog.append("startTimeLocal", dateToCtimeString(Date_t::now()));

    toLog.append("cmdLine", serverGlobalParams.parsedOpts);
    toLog.append("pid", ProcessId::getCurrent().asLongLong());


    BSONObjBuilder buildinfo(toLog.subobjStart("buildinfo"));
    VersionInfoInterface::instance().appendBuildInfo(&buildinfo);
    appendStorageEngineList(&buildinfo);
    buildinfo.doneFast();

    BSONObj o = toLog.obj();

    ScopedTransaction transaction(txn, MODE_X);
    Lock::GlobalWrite lk(txn->lockState());
    AutoGetOrCreateDb autoDb(txn, startupLogCollectionName.db(), bongo::MODE_X);
    Database* db = autoDb.getDb();
    Collection* collection = db->getCollection(startupLogCollectionName);
    WriteUnitOfWork wunit(txn);
    if (!collection) {
        BSONObj options = BSON("capped" << true << "size" << 10 * 1024 * 1024);
        bool shouldReplicateWrites = txn->writesAreReplicated();
        txn->setReplicatedWrites(false);
        ON_BLOCK_EXIT(&OperationContext::setReplicatedWrites, txn, shouldReplicateWrites);
        uassertStatusOK(userCreateNS(txn, db, startupLogCollectionName.ns(), options));
        collection = db->getCollection(startupLogCollectionName);
    }
    invariant(collection);

    OpDebug* const nullOpDebug = nullptr;
    uassertStatusOK(collection->insertDocument(txn, o, nullOpDebug, false));
    wunit.commit();
}

void checkForIdIndexes(OperationContext* txn, Database* db) {
    if (db->name() == "local") {
        // we do not need an _id index on anything in the local database
        return;
    }

    list<string> collections;
    db->getDatabaseCatalogEntry()->getCollectionNamespaces(&collections);

    // for each collection, ensure there is a $_id_ index
    for (list<string>::iterator i = collections.begin(); i != collections.end(); ++i) {
        const string& collectionName = *i;
        NamespaceString ns(collectionName);
        if (ns.isSystem())
            continue;

        Collection* coll = db->getCollection(collectionName);
        if (!coll)
            continue;

        if (coll->getIndexCatalog()->findIdIndex(txn))
            continue;

        log() << "WARNING: the collection '" << *i << "' lacks a unique index on _id."
              << " This index is needed for replication to function properly" << startupWarningsLog;
        log() << "\t To fix this, you need to create a unique index on _id."
              << " See http://dochub.bongodb.org/core/build-replica-set-indexes"
              << startupWarningsLog;
    }
}

/**
 * Checks if this server was started without --replset but has a config in local.system.replset
 * (meaning that this is probably a replica set member started in stand-alone mode).
 *
 * @returns the number of documents in local.system.replset or 0 if this was started with
 *          --replset.
 */
unsigned long long checkIfReplMissingFromCommandLine(OperationContext* txn) {
    if (!repl::getGlobalReplicationCoordinator()->getSettings().usingReplSets()) {
        DBDirectClient c(txn);
        return c.count(kSystemReplSetCollection.ns());
    }
    return 0;
}

/**
 * Check that the oplog is capped, and abort the process if it is not.
 * Caller must lock DB before calling this function.
 */
void checkForCappedOplog(OperationContext* txn, Database* db) {
    const NamespaceString oplogNss(repl::rsOplogName);
    invariant(txn->lockState()->isDbLockedForMode(oplogNss.db(), MODE_IS));
    Collection* oplogCollection = db->getCollection(oplogNss);
    if (oplogCollection && !oplogCollection->isCapped()) {
        severe() << "The oplog collection " << oplogNss
                 << " is not capped; a capped oplog is a requirement for replication to function.";
        fassertFailedNoTrace(40115);
    }
}

void repairDatabasesAndCheckVersion(OperationContext* txn) {
    LOG(1) << "enter repairDatabases (to check pdfile version #)";

    ScopedTransaction transaction(txn, MODE_X);
    Lock::GlobalWrite lk(txn->lockState());

    vector<string> dbNames;

    StorageEngine* storageEngine = txn->getServiceContext()->getGlobalStorageEngine();
    storageEngine->listDatabases(&dbNames);

    // Repair all databases first, so that we do not try to open them if they are in bad shape
    if (storageGlobalParams.repair) {
        invariant(!storageGlobalParams.readOnly);
        for (vector<string>::const_iterator i = dbNames.begin(); i != dbNames.end(); ++i) {
            const string dbName = *i;
            LOG(1) << "    Repairing database: " << dbName;

            fassert(18506, repairDatabase(txn, storageEngine, dbName));
        }
    }

    const repl::ReplSettings& replSettings = repl::getGlobalReplicationCoordinator()->getSettings();

    if (!storageGlobalParams.readOnly) {
        // We open the "local" database before calling checkIfReplMissingFromCommandLine() to ensure
        // the in-memory catalog entries for the 'kSystemReplSetCollection' collection have been
        // populated if the collection exists. If the "local" database didn't exist at this point
        // yet, then it will be created. If the bongod is running in a read-only mode, then it is
        // fine to not open the "local" database and populate the catalog entries because we won't
        // attempt to drop the temporary collections anyway.
        Lock::DBLock dbLock(txn->lockState(), kSystemReplSetCollection.db(), MODE_X);
        dbHolder().openDb(txn, kSystemReplSetCollection.db());
    }

    // On replica set members we only clear temp collections on DBs other than "local" during
    // promotion to primary. On pure slaves, they are only cleared when the oplog tells them
    // to. The local DB is special because it is not replicated.  See SERVER-10927 for more
    // details.
    const bool shouldClearNonLocalTmpCollections =
        !(checkIfReplMissingFromCommandLine(txn) || replSettings.usingReplSets() ||
          replSettings.isSlave());

    for (vector<string>::const_iterator i = dbNames.begin(); i != dbNames.end(); ++i) {
        const string dbName = *i;
        LOG(1) << "    Recovering database: " << dbName;

        Database* db = dbHolder().openDb(txn, dbName);
        invariant(db);

        // First thing after opening the database is to check for file compatibility,
        // otherwise we might crash if this is a deprecated format.
        auto status = db->getDatabaseCatalogEntry()->currentFilesCompatible(txn);
        if (!status.isOK()) {
            if (status.code() == ErrorCodes::CanRepairToDowngrade) {
                // Convert CanRepairToDowngrade statuses to MustUpgrade statuses to avoid logging a
                // potentially confusing and inaccurate message.
                //
                // TODO SERVER-24097: Log a message informing the user that they can start the
                // current version of bongod with --repair and then proceed with normal startup.
                status = {ErrorCodes::MustUpgrade, status.reason()};
            }
            severe() << "Unable to start bongod due to an incompatibility with the data files and"
                        " this version of bongod: "
                     << redact(status);
            severe() << "Please consult our documentation when trying to downgrade to a previous"
                        " major release";
            quickExit(EXIT_NEED_UPGRADE);
            return;
        }

        // Check if admin.system.version contains an invalid featureCompatibilityVersion.
        // If a valid featureCompatibilityVersion is present, cache it as a server parameter.
        if (dbName == "admin") {
            if (Collection* versionColl =
                    db->getCollection(FeatureCompatibilityVersion::kCollection)) {
                BSONObj featureCompatibilityVersion;
                if (Helpers::findOne(txn,
                                     versionColl,
                                     BSON("_id" << FeatureCompatibilityVersion::kParameterName),
                                     featureCompatibilityVersion)) {
                    auto version = FeatureCompatibilityVersion::parse(featureCompatibilityVersion);
                    if (!version.isOK()) {
                        severe() << version.getStatus();
                        fassertFailedNoTrace(40283);
                    }
                    serverGlobalParams.featureCompatibility.version.store(version.getValue());
                }
            }
        }

        // Major versions match, check indexes
        const string systemIndexes = db->name() + ".system.indexes";

        Collection* coll = db->getCollection(systemIndexes);
        unique_ptr<PlanExecutor> exec(
            InternalPlanner::collectionScan(txn, systemIndexes, coll, PlanExecutor::YIELD_MANUAL));

        BSONObj index;
        PlanExecutor::ExecState state;
        while (PlanExecutor::ADVANCED == (state = exec->getNext(&index, NULL))) {
            const BSONObj key = index.getObjectField("key");
            const string plugin = IndexNames::findPluginName(key);

            if (db->getDatabaseCatalogEntry()->isOlderThan24(txn)) {
                if (IndexNames::existedBefore24(plugin)) {
                    continue;
                }

                log() << "Index " << index << " claims to be of type '" << plugin << "', "
                      << "which is either invalid or did not exist before v2.4. "
                      << "See the upgrade section: "
                      << "http://dochub.bongodb.org/core/upgrade-2.4" << startupWarningsLog;
            }

            if (index["v"].isNumber() && index["v"].numberInt() == 0) {
                log() << "WARNING: The index: " << index << " was created with the deprecated"
                      << " v:0 format.  This format will not be supported in a future release."
                      << startupWarningsLog;
                log() << "\t To fix this, you need to rebuild this index."
                      << " For instructions, see http://dochub.bongodb.org/core/rebuild-v0-indexes"
                      << startupWarningsLog;
            }
        }

        // Non-yielding collection scans from InternalPlanner will never error.
        invariant(PlanExecutor::IS_EOF == state);

        if (replSettings.usingReplSets()) {
            // We only care about the _id index if we are in a replset
            checkForIdIndexes(txn, db);
            // Ensure oplog is capped (mmap does not guarantee order of inserts on noncapped
            // collections)
            if (db->name() == "local") {
                checkForCappedOplog(txn, db);
            }
        }

        if (!storageGlobalParams.readOnly &&
            (shouldClearNonLocalTmpCollections || dbName == "local")) {
            db->clearTmpCollections(txn);
        }
    }

    LOG(1) << "done repairDatabases";
}

void _initWireSpec() {
    WireSpec& spec = WireSpec::instance();

    spec.isInternalClient = true;
}

BONGO_FP_DECLARE(shutdownAtStartup);

ExitCode _initAndListen(int listenPort) {
    Client::initThread("initandlisten");

    _initWireSpec();
    auto globalServiceContext = getGlobalServiceContext();

    globalServiceContext->setFastClockSource(FastClockSourceFactory::create(Milliseconds(10)));
    globalServiceContext->setOpObserver(stdx::make_unique<OpObserverImpl>());

    DBDirectClientFactory::get(globalServiceContext)
        .registerImplementation([](OperationContext* txn) {
            return std::unique_ptr<DBClientBase>(new DBDirectClient(txn));
        });

    const repl::ReplSettings& replSettings = repl::getGlobalReplicationCoordinator()->getSettings();

    {
        ProcessId pid = ProcessId::getCurrent();
        LogstreamBuilder l = log(LogComponent::kControl);
        l << "BongoDB starting : pid=" << pid << " port=" << serverGlobalParams.port
          << " dbpath=" << storageGlobalParams.dbpath;
        if (replSettings.isMaster())
            l << " master=" << replSettings.isMaster();
        if (replSettings.isSlave())
            l << " slave=" << (int)replSettings.isSlave();

        const bool is32bit = sizeof(int*) == 4;
        l << (is32bit ? " 32" : " 64") << "-bit host=" << getHostNameCached() << endl;
    }

    DEV log(LogComponent::kControl) << "DEBUG build (which is slower)" << endl;

#if defined(_WIN32)
    VersionInfoInterface::instance().logTargetMinOS();
#endif

    logProcessDetails();

    checked_cast<ServiceContextBongoD*>(getGlobalServiceContext())->createLockFile();

    transport::TransportLayerLegacy::Options options;
    options.port = listenPort;
    options.ipList = serverGlobalParams.bind_ip;

    auto sep =
        stdx::make_unique<ServiceEntryPointBongod>(getGlobalServiceContext()->getTransportLayer());
    auto sepPtr = sep.get();

    getGlobalServiceContext()->setServiceEntryPoint(std::move(sep));

    // Create, start, and attach the TL
    auto transportLayer = stdx::make_unique<transport::TransportLayerLegacy>(options, sepPtr);
    auto res = transportLayer->setup();
    if (!res.isOK()) {
        error() << "Failed to set up listener: " << res;
        return EXIT_NET_ERROR;
    }

    std::shared_ptr<DbWebServer> dbWebServer;
    if (serverGlobalParams.isHttpInterfaceEnabled) {
        dbWebServer.reset(new DbWebServer(serverGlobalParams.bind_ip,
                                          serverGlobalParams.port + 1000,
                                          getGlobalServiceContext(),
                                          new RestAdminAccess()));
        if (!dbWebServer->setupSockets()) {
            error() << "Failed to set up sockets for HTTP interface during startup.";
            return EXIT_NET_ERROR;
        }
    }

    getGlobalServiceContext()->initializeGlobalStorageEngine();

#ifdef BONGO_CONFIG_WIREDTIGER_ENABLED
    if (WiredTigerCustomizationHooks::get(getGlobalServiceContext())->restartRequired()) {
        exitCleanly(EXIT_CLEAN);
    }
#endif

    // Warn if we detect configurations for multiple registered storage engines in
    // the same configuration file/environment.
    if (serverGlobalParams.parsedOpts.hasField("storage")) {
        BSONElement storageElement = serverGlobalParams.parsedOpts.getField("storage");
        invariant(storageElement.isABSONObj());
        BSONObj storageParamsObj = storageElement.Obj();
        BSONObjIterator i = storageParamsObj.begin();
        while (i.more()) {
            BSONElement e = i.next();
            // Ignore if field name under "storage" matches current storage engine.
            if (storageGlobalParams.engine == e.fieldName()) {
                continue;
            }

            // Warn if field name matches non-active registered storage engine.
            if (getGlobalServiceContext()->isRegisteredStorageEngine(e.fieldName())) {
                warning() << "Detected configuration for non-active storage engine "
                          << e.fieldName() << " when current storage engine is "
                          << storageGlobalParams.engine;
            }
        }
    }

    if (!getGlobalServiceContext()->getGlobalStorageEngine()->getSnapshotManager()) {
        if (moe::startupOptionsParsed.count("replication.enableMajorityReadConcern") &&
            moe::startupOptionsParsed["replication.enableMajorityReadConcern"].as<bool>()) {
            // Note: we are intentionally only erroring if the user explicitly requested that we
            // enable majority read concern. We do not error if the they are implicitly enabled for
            // CSRS because a required step in the upgrade procedure can involve an mmapv1 node in
            // the CSRS in the REMOVED state. This is handled by the TopologyCoordinator.
            invariant(replSettings.isMajorityReadConcernEnabled());
            severe() << "Majority read concern requires a storage engine that supports"
                     << " snapshots, such as wiredTiger. " << storageGlobalParams.engine
                     << " does not support snapshots.";
            exitCleanly(EXIT_BADOPTIONS);
        }
    }

    logBongodStartupWarnings(storageGlobalParams, serverGlobalParams);

    {
        stringstream ss;
        ss << endl;
        ss << "*********************************************************************" << endl;
        ss << " ERROR: dbpath (" << storageGlobalParams.dbpath << ") does not exist." << endl;
        ss << " Create this directory or give existing directory in --dbpath." << endl;
        ss << " See http://dochub.bongodb.org/core/startingandstoppingbongo" << endl;
        ss << "*********************************************************************" << endl;
        uassert(10296, ss.str().c_str(), boost::filesystem::exists(storageGlobalParams.dbpath));
    }

    {
        stringstream ss;
        ss << "repairpath (" << storageGlobalParams.repairpath << ") does not exist";
        uassert(12590, ss.str().c_str(), boost::filesystem::exists(storageGlobalParams.repairpath));
    }

    initializeSNMP();

    if (!storageGlobalParams.readOnly) {
        boost::filesystem::remove_all(storageGlobalParams.dbpath + "/_tmp/");
    }

    if (mmapv1GlobalOptions.journalOptions & MMAPV1Options::JournalRecoverOnly)
        return EXIT_NET_ERROR;

    if (bongodGlobalParams.scriptingEnabled) {
        ScriptEngine::setup();
    }

    auto startupOpCtx = getGlobalServiceContext()->makeOperationContext(&cc());

    repairDatabasesAndCheckVersion(startupOpCtx.get());

    if (storageGlobalParams.upgrade) {
        log() << "finished checking dbs";
        exitCleanly(EXIT_CLEAN);
    }

    uassertStatusOK(getGlobalAuthorizationManager()->initialize(startupOpCtx.get()));

    /* this is for security on certain platforms (nonce generation) */
    srand((unsigned)(curTimeMicros64() ^ startupSrandTimer.micros()));

    // The snapshot thread provides historical collection level and lock statistics for use
    // by the web interface. Only needed when HTTP is enabled.
    if (serverGlobalParams.isHttpInterfaceEnabled) {
        statsSnapshotThread.go();

        invariant(dbWebServer);
        stdx::thread web(stdx::bind(&webServerListenThread, dbWebServer));
        web.detach();
    }

    AuthorizationManager* globalAuthzManager = getGlobalAuthorizationManager();
    if (globalAuthzManager->shouldValidateAuthSchemaOnStartup()) {
        Status status = authindex::verifySystemIndexes(startupOpCtx.get());
        if (!status.isOK()) {
            log() << redact(status);
            exitCleanly(EXIT_NEED_UPGRADE);
        }

        // SERVER-14090: Verify that auth schema version is schemaVersion26Final.
        int foundSchemaVersion;
        status =
            globalAuthzManager->getAuthorizationVersion(startupOpCtx.get(), &foundSchemaVersion);
        if (!status.isOK()) {
            log() << "Auth schema version is incompatible: "
                  << "User and role management commands require auth data to have "
                  << "at least schema version " << AuthorizationManager::schemaVersion26Final
                  << " but startup could not verify schema version: " << status;
            exitCleanly(EXIT_NEED_UPGRADE);
        }
        if (foundSchemaVersion < AuthorizationManager::schemaVersion26Final) {
            log() << "Auth schema version is incompatible: "
                  << "User and role management commands require auth data to have "
                  << "at least schema version " << AuthorizationManager::schemaVersion26Final
                  << " but found " << foundSchemaVersion << ". In order to upgrade "
                  << "the auth schema, first downgrade BongoDB binaries to version "
                  << "2.6 and then run the authSchemaUpgrade command.";
            exitCleanly(EXIT_NEED_UPGRADE);
        }
    } else if (globalAuthzManager->isAuthEnabled()) {
        error() << "Auth must be disabled when starting without auth schema validation";
        exitCleanly(EXIT_BADOPTIONS);
    } else {
        // If authSchemaValidation is disabled and server is running without auth,
        // warn the user and continue startup without authSchema metadata checks.
        log() << startupWarningsLog;
        log() << "** WARNING: Startup auth schema validation checks are disabled for the "
                 "database."
              << startupWarningsLog;
        log() << "**          This mode should only be used to manually repair corrupted auth "
                 "data."
              << startupWarningsLog;
    }

    auto shardingInitialized =
        uassertStatusOK(ShardingState::get(startupOpCtx.get())
                            ->initializeShardingAwarenessIfNeeded(startupOpCtx.get()));
    if (shardingInitialized) {
        reloadShardRegistryUntilSuccess(startupOpCtx.get());
    }

    if (!storageGlobalParams.readOnly) {
        logStartup(startupOpCtx.get());

        startFTDC();

        getDeleter()->startWorkers();

        restartInProgressIndexesFromLastShutdown(startupOpCtx.get());

        if (serverGlobalParams.clusterRole == ClusterRole::ShardServer) {
            // Note: For replica sets, ShardingStateRecovery happens on transition to primary.
            if (!repl::getGlobalReplicationCoordinator()->isReplEnabled()) {
                uassertStatusOK(ShardingStateRecovery::recover(startupOpCtx.get()));
            }
        } else if (serverGlobalParams.clusterRole == ClusterRole::ConfigServer) {
            uassertStatusOK(
                initializeGlobalShardingStateForBongod(startupOpCtx.get(),
                                                       ConnectionString::forLocal(),
                                                       kDistLockProcessIdForConfigServer));
            Balancer::create(startupOpCtx->getServiceContext());
        }

        repl::getGlobalReplicationCoordinator()->startup(startupOpCtx.get());

        const unsigned long long missingRepl =
            checkIfReplMissingFromCommandLine(startupOpCtx.get());
        if (missingRepl) {
            log() << startupWarningsLog;
            log() << "** WARNING: bongod started without --replSet yet " << missingRepl
                  << " documents are present in local.system.replset" << startupWarningsLog;
            log() << "**          Restart with --replSet unless you are doing maintenance and "
                  << " no other clients are connected." << startupWarningsLog;
            log() << "**          The TTL collection monitor will not start because of this."
                  << startupWarningsLog;
            log() << "**         ";
            log() << " For more info see http://dochub.bongodb.org/core/ttlcollections";
            log() << startupWarningsLog;
        } else {
            startTTLBackgroundJob();
        }

        if (!replSettings.usingReplSets() && !replSettings.isSlave() &&
            storageGlobalParams.engine != "devnull") {
            ScopedTransaction transaction(startupOpCtx.get(), MODE_X);
            Lock::GlobalWrite lk(startupOpCtx.get()->lockState());
            FeatureCompatibilityVersion::setIfCleanStartup(
                startupOpCtx.get(), repl::StorageInterface::get(getGlobalServiceContext()));
        }

        if (replSettings.usingReplSets() || (!replSettings.isMaster() && replSettings.isSlave()) ||
            !internalValidateFeaturesAsMaster) {
            serverGlobalParams.featureCompatibility.validateFeaturesAsMaster.store(false);
        }
    }

    startClientCursorMonitor();

    PeriodicTask::startRunningPeriodicTasks();

    // MessageServer::run will return when exit code closes its socket and we don't need the
    // operation context anymore
    startupOpCtx.reset();

    auto start = getGlobalServiceContext()->addAndStartTransportLayer(std::move(transportLayer));
    if (!start.isOK()) {
        error() << "Failed to start the listener: " << start.toString();
        return EXIT_NET_ERROR;
    }

#ifndef _WIN32
    bongo::signalForkSuccess();
#else
    if (ntservice::shouldStartService()) {
        ntservice::reportStatus(SERVICE_RUNNING);
        log() << "Service running";
    }
#endif

    if (BONGO_FAIL_POINT(shutdownAtStartup)) {
        log() << "starting clean exit via failpoint";
        exitCleanly(EXIT_CLEAN);
    }
    return waitForShutdown();
}

ExitCode initAndListen(int listenPort) {
    try {
        return _initAndListen(listenPort);
    } catch (DBException& e) {
        log() << "exception in initAndListen: " << e.toString() << ", terminating";
        return EXIT_UNCAUGHT;
    } catch (std::exception& e) {
        log() << "exception in initAndListen std::exception: " << e.what() << ", terminating";
        return EXIT_UNCAUGHT;
    } catch (int& n) {
        log() << "exception in initAndListen int: " << n << ", terminating";
        return EXIT_UNCAUGHT;
    } catch (...) {
        log() << "exception in initAndListen, terminating";
        return EXIT_UNCAUGHT;
    }
}

}  // namespace

#if defined(_WIN32)
ExitCode initService() {
    return initAndListen(serverGlobalParams.port);
}
#endif

}  // namespace bongo

using namespace bongo;

static int bongoDbMain(int argc, char* argv[], char** envp);

#if defined(_WIN32)
// In Windows, wmain() is an alternate entry point for main(), and receives the same parameters
// as main() but encoded in Windows Unicode (UTF-16); "wide" 16-bit wchar_t characters.  The
// WindowsCommandLine object converts these wide character strings to a UTF-8 coded equivalent
// and makes them available through the argv() and envp() members.  This enables bongoDbMain()
// to process UTF-8 encoded arguments and environment variables without regard to platform.
int wmain(int argc, wchar_t* argvW[], wchar_t* envpW[]) {
    WindowsCommandLine wcl(argc, argvW, envpW);
    int exitCode = bongoDbMain(argc, wcl.argv(), wcl.envp());
    quickExit(exitCode);
}
#else
int main(int argc, char* argv[], char** envp) {
    int exitCode = bongoDbMain(argc, argv, envp);
    quickExit(exitCode);
}
#endif

namespace {
BONGO_INITIALIZER_GENERAL(ForkServer, ("EndStartupOptionHandling"), ("default"))
(InitializerContext* context) {
    bongo::forkServerOrDie();
    return Status::OK();
}
}  // namespace

/*
 * This function should contain the startup "actions" that we take based on the startup config.  It
 * is intended to separate the actions from "storage" and "validation" of our startup configuration.
 */
static void startupConfigActions(const std::vector<std::string>& args) {
    // The "command" option is deprecated.  For backward compatibility, still support the "run"
    // and "dbppath" command.  The "run" command is the same as just running bongod, so just
    // falls through.
    if (moe::startupOptionsParsed.count("command")) {
        vector<string> command = moe::startupOptionsParsed["command"].as<vector<string>>();

        if (command[0].compare("dbpath") == 0) {
            cout << storageGlobalParams.dbpath << endl;
            quickExit(EXIT_SUCCESS);
        }

        if (command[0].compare("run") != 0) {
            cout << "Invalid command: " << command[0] << endl;
            printBongodHelp(moe::startupOptions);
            quickExit(EXIT_FAILURE);
        }

        if (command.size() > 1) {
            cout << "Too many parameters to 'run' command" << endl;
            printBongodHelp(moe::startupOptions);
            quickExit(EXIT_FAILURE);
        }
    }

#ifdef _WIN32
    ntservice::configureService(initService,
                                moe::startupOptionsParsed,
                                defaultServiceStrings,
                                std::vector<std::string>(),
                                args);
#endif  // _WIN32

#ifdef __linux__
    if (moe::startupOptionsParsed.count("shutdown") &&
        moe::startupOptionsParsed["shutdown"].as<bool>() == true) {
        bool failed = false;

        string name =
            (boost::filesystem::path(storageGlobalParams.dbpath) / "bongod.lock").string();
        if (!boost::filesystem::exists(name) || boost::filesystem::file_size(name) == 0)
            failed = true;

        pid_t pid;
        string procPath;
        if (!failed) {
            try {
                std::ifstream f(name.c_str());
                f >> pid;
                procPath = (str::stream() << "/proc/" << pid);
                if (!boost::filesystem::exists(procPath))
                    failed = true;
            } catch (const std::exception& e) {
                cerr << "Error reading pid from lock file [" << name << "]: " << e.what() << endl;
                failed = true;
            }
        }

        if (failed) {
            std::cerr << "There doesn't seem to be a server running with dbpath: "
                      << storageGlobalParams.dbpath << std::endl;
            quickExit(EXIT_FAILURE);
        }

        cout << "killing process with pid: " << pid << endl;
        int ret = kill(pid, SIGTERM);
        if (ret) {
            int e = errno;
            cerr << "failed to kill process: " << errnoWithDescription(e) << endl;
            quickExit(EXIT_FAILURE);
        }

        while (boost::filesystem::exists(procPath)) {
            sleepsecs(1);
        }

        quickExit(EXIT_SUCCESS);
    }
#endif
}

BONGO_INITIALIZER_WITH_PREREQUISITES(CreateReplicationManager,
                                     ("SetGlobalEnvironment", "SSLManager", "default"))
(InitializerContext* context) {
    auto serviceContext = getGlobalServiceContext();
    repl::StorageInterface::set(serviceContext, stdx::make_unique<repl::StorageInterfaceImpl>());
    auto storageInterface = repl::StorageInterface::get(serviceContext);

    repl::TopologyCoordinatorImpl::Options topoCoordOptions;
    topoCoordOptions.maxSyncSourceLagSecs = Seconds(repl::maxSyncSourceLagSecs);
    topoCoordOptions.clusterRole = serverGlobalParams.clusterRole;

    std::array<std::uint8_t, 20> tempKey = {};
    TimeProofService::Key key(std::move(tempKey));
    auto timeProofService = stdx::make_unique<TimeProofService>(std::move(key));
    auto logicalClock =
        stdx::make_unique<LogicalClock>(serviceContext, std::move(timeProofService), false);
    LogicalClock::set(serviceContext, std::move(logicalClock));

    auto hookList = stdx::make_unique<rpc::EgressMetadataHookList>();
    // TODO SERVER-27750: add LogicalTimeMetadataHook

    auto replCoord = stdx::make_unique<repl::ReplicationCoordinatorImpl>(
        serviceContext,
        getGlobalReplSettings(),
        stdx::make_unique<repl::ReplicationCoordinatorExternalStateImpl>(storageInterface),
        executor::makeNetworkInterface(
            "NetworkInterfaceASIO-Replication", nullptr, std::move(hookList)),
        stdx::make_unique<repl::TopologyCoordinatorImpl>(topoCoordOptions),
        storageInterface,
        static_cast<int64_t>(curTimeMillis64()));
    repl::ReplicationCoordinator::set(serviceContext, std::move(replCoord));
    repl::setOplogCollectionName();
    return Status::OK();
}

#ifdef BONGO_CONFIG_SSL
BONGO_INITIALIZER_GENERAL(setSSLManagerType, BONGO_NO_PREREQUISITES, ("SSLManager"))
(InitializerContext* context) {
    isSSLServer = true;
    return Status::OK();
}
#endif

#if !defined(__has_feature)
#define __has_feature(x) 0
#endif

// NOTE: This function may be called at any time after
// registerShutdownTask is called below. It must not depend on the
// prior execution of bongo initializers or the existence of threads.
static void shutdownTask() {
    auto serviceContext = getGlobalServiceContext();

    Client::initThreadIfNotAlready();
    Client& client = cc();

    ServiceContext::UniqueOperationContext uniqueTxn;
    OperationContext* txn = client.getOperationContext();
    if (!txn && serviceContext->getGlobalStorageEngine()) {
        uniqueTxn = client.makeOperationContext();
        txn = uniqueTxn.get();
    }

    log(LogComponent::kNetwork) << "shutdown: going to close listening sockets..." << endl;
    ListeningSockets::get()->closeAll();

    log(LogComponent::kNetwork) << "shutdown: going to flush diaglog..." << endl;
    _diaglog.flush();

    if (txn) {
        // This can wait a long time while we drain the secondary's apply queue, especially if it is
        // building an index.
        repl::ReplicationCoordinator::get(txn)->shutdown(txn);
    }

    if (serviceContext)
        serviceContext->setKillAllOperations();

    ReplicaSetMonitor::shutdown();
    if (auto sr = grid.shardRegistry()) {  // TODO: race: sr is a naked pointer
        sr->shutdown();
    }
#if __has_feature(address_sanitizer)
    auto sep = static_cast<ServiceEntryPointBongod*>(serviceContext->getServiceEntryPoint());

    if (sep) {
        // When running under address sanitizer, we get false positive leaks due to disorder around
        // the lifecycle of a connection and request. When we are running under ASAN, we try a lot
        // harder to dry up the server from active connections before going on to really shut down.

        log(LogComponent::kNetwork)
            << "shutdown: going to close all sockets because ASAN is active...";
        getGlobalServiceContext()->getTransportLayer()->shutdown();

        // Close all sockets in a detached thread, and then wait for the number of active
        // connections to reach zero. Give the detached background thread a 10 second deadline. If
        // we haven't closed drained all active operations within that deadline, just keep going
        // with shutdown: the OS will do it for us when the process terminates.

        stdx::packaged_task<void()> dryOutTask([sep] {
            // There isn't currently a way to wait on the TicketHolder to have all its tickets back,
            // unfortunately. So, busy wait in this detached thread.
            while (true) {
                const auto runningWorkers = sep->getNumberOfActiveWorkerThreads();

                if (runningWorkers == 0) {
                    log(LogComponent::kNetwork) << "shutdown: no running workers found...";
                    break;
                }
                log(LogComponent::kNetwork) << "shutdown: still waiting on " << runningWorkers
                                            << " active workers to drain... ";
                bongo::sleepFor(Milliseconds(250));
            }
        });

        auto dryNotification = dryOutTask.get_future();
        stdx::thread(std::move(dryOutTask)).detach();
        if (dryNotification.wait_for(Seconds(10).toSystemDuration()) !=
            stdx::future_status::ready) {
            log(LogComponent::kNetwork) << "shutdown: exhausted grace period for"
                                        << " active workers to drain; continuing with shutdown... ";
        }
    }

#endif

    // Shutdown Full-Time Data Capture
    stopFTDC();

    if (txn) {
        ShardingState::get(txn)->shutDown(txn);
    }

    // We should always be able to acquire the global lock at shutdown.
    //
    // TODO: This call chain uses the locker directly, because we do not want to start an
    // operation context, which also instantiates a recovery unit. Also, using the
    // lockGlobalBegin/lockGlobalComplete sequence, we avoid taking the flush lock. This will
    // all go away if we start acquiring the global/flush lock as part of ScopedTransaction.
    //
    // For a Windows service, dbexit does not call exit(), so we must leak the lock outside
    // of this function to prevent any operations from running that need a lock.
    //
    DefaultLockerImpl* globalLocker = new DefaultLockerImpl();
    LockResult result = globalLocker->lockGlobalBegin(MODE_X);
    if (result == LOCK_WAITING) {
        result = globalLocker->lockGlobalComplete(UINT_MAX);
    }

    invariant(LOCK_OK == result);

    // Global storage engine may not be started in all cases before we exit

    if (serviceContext && serviceContext->getGlobalStorageEngine()) {
        serviceContext->shutdownGlobalStorageEngineCleanly();
    }

    // We drop the scope cache because leak sanitizer can't see across the
    // thread we use for proxying MozJS requests. Dropping the cache cleans up
    // the memory and makes leak sanitizer happy.
    ScriptEngine::dropScopeCache();

    log(LogComponent::kControl) << "now exiting" << endl;

    audit::logShutdown(&cc());
}

static int bongoDbMain(int argc, char* argv[], char** envp) {
    registerShutdownTask(shutdownTask);

    setupSignalHandlers();

    srand(static_cast<unsigned>(curTimeMicros64()));

    Status status = bongo::runGlobalInitializers(argc, argv, envp);
    if (!status.isOK()) {
        severe(LogComponent::kControl) << "Failed global initialization: " << status;
        quickExit(EXIT_FAILURE);
    }

    startupConfigActions(std::vector<std::string>(argv, argv + argc));
    cmdline_utils::censorArgvArray(argc, argv);

    if (!initializeServerGlobalState())
        quickExit(EXIT_FAILURE);

    // Per SERVER-7434, startSignalProcessingThread() must run after any forks
    // (initializeServerGlobalState()) and before creation of any other threads.
    startSignalProcessingThread();

#if defined(_WIN32)
    if (ntservice::shouldStartService()) {
        ntservice::startService();
        // exits directly and so never reaches here either.
    }
#endif

    StartupTest::runTests();
    ExitCode exitCode = initAndListen(serverGlobalParams.port);
    exitCleanly(exitCode);
    return 0;
}
