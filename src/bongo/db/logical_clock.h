/**
 *    Copyright (C) 2017 BongoDB, Inc.
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

#pragma once

#include "bongo/db/signed_logical_time.h"
#include "bongo/stdx/mutex.h"

namespace bongo {
class TimeProofService;
class ServiceContext;
class OperationContext;

/**
 * LogicalClock maintain the clusterTime for a clusterNode. Every cluster node in a replica set has
 * an instance of the LogicalClock installed as a ServiceContext decoration. LogicalClock owns the
 * TimeProofService that allows it to generate proofs to sign LogicalTime values and to validate the
 * proofs of SignedLogicalTime values.LogicalClock instance must be created before the instance
 * starts up.
 */
class LogicalClock {
public:
    // Decorate ServiceContext with LogicalClock instance.
    static LogicalClock* get(ServiceContext* service);
    static LogicalClock* get(OperationContext* ctx);
    static void set(ServiceContext* service, std::unique_ptr<LogicalClock> logicalClock);

    /**
     *  Creates an instance of LogicalClock. The TimeProofService must already be fully initialized.
     *  The validateProof indicates if the advanceClusterTime validates newTime. It should do so
     *  only when LogicalClock installed on bongos and the auth is on. When the auth is off we
     *  assume that the DBA uses other ways to validate authenticity of user messages.
     */
    LogicalClock(ServiceContext*, std::unique_ptr<TimeProofService>, bool validateProof);

    /**
     * The method sets clusterTime to the newTime if the newTime > _clusterTime and the newTime
     * passes the rate check and proof validation.
     * Returns an error if the newTime does not pass the rate check or proof validation,
     * OK otherwise.
     */
    Status advanceClusterTime(const SignedLogicalTime&);

    /**
     * Simliar to advaneClusterTime, but only does rate checking and not proof validation.
     */
    Status advanceClusterTimeFromTrustedSource(LogicalTime);

    /**
     * Returns the current clusterTime.
     */
    SignedLogicalTime getClusterTime();

    /**
     * Returns the next  clusterTime value and provides the guarantee that the next reserveTicks
     * call will return the value at least nTicks ticks in the future from the current clusterTime.
     */
    LogicalTime reserveTicks(uint64_t nTicks);

    /**
     * Resets _clusterTime to the signed time created from newTime. Should be used at the
     * initialization after reading the oplog. Must not be called on already initialized clock.
     */
    void initClusterTimeFromTrustedSource(LogicalTime newTime);

private:
    /**
     * Utility to create valid SignedLogicalTime from LogicalTime.
     */
    SignedLogicalTime _makeSignedLogicalTime(LogicalTime);

    ServiceContext* const _service;
    std::unique_ptr<TimeProofService> _timeProofService;

    // the mutex protects _clusterTime
    stdx::mutex _mutex;
    SignedLogicalTime _clusterTime;
    const bool _validateProof;
};

}  // namespace bongo
