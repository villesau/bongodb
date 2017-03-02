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

#define BONGO_LOG_DEFAULT_COMPONENT ::bongo::logger::LogComponent::kDefault

#include "bongo/platform/basic.h"

#include "bongo/db/logical_clock.h"

#include "bongo/base/status.h"
#include "bongo/db/operation_context.h"
#include "bongo/db/service_context.h"
#include "bongo/db/time_proof_service.h"
#include "bongo/util/log.h"

namespace bongo {

namespace {
const auto getLogicalClock = ServiceContext::declareDecoration<std::unique_ptr<LogicalClock>>();
}

LogicalClock* LogicalClock::get(ServiceContext* service) {
    return getLogicalClock(service).get();
}

LogicalClock* LogicalClock::get(OperationContext* ctx) {
    return get(ctx->getClient()->getServiceContext());
}

void LogicalClock::set(ServiceContext* service, std::unique_ptr<LogicalClock> clockArg) {
    auto& clock = getLogicalClock(service);
    clock = std::move(clockArg);
}

LogicalClock::LogicalClock(ServiceContext* service,
                           std::unique_ptr<TimeProofService> tps,
                           bool validateProof)
    : _service(service), _timeProofService(std::move(tps)), _validateProof(validateProof) {}

SignedLogicalTime LogicalClock::getClusterTime() {
    stdx::lock_guard<stdx::mutex> lock(_mutex);
    return _clusterTime;
}

SignedLogicalTime LogicalClock::_makeSignedLogicalTime(LogicalTime logicalTime) {
    return SignedLogicalTime(logicalTime, _timeProofService->getProof(logicalTime));
}

Status LogicalClock::advanceClusterTime(const SignedLogicalTime& newTime) {
    if (_validateProof) {
        invariant(_timeProofService);
        auto res = _timeProofService->checkProof(newTime.getTime(), newTime.getProof());
        if (res != Status::OK()) {
            return res;
        }
    }

    stdx::lock_guard<stdx::mutex> lock(_mutex);
    // TODO: rate check per SERVER-27721
    if (newTime.getTime() > _clusterTime.getTime()) {
        _clusterTime = newTime;
    }

    return Status::OK();
}

Status LogicalClock::advanceClusterTimeFromTrustedSource(LogicalTime newTime) {
    stdx::lock_guard<stdx::mutex> lock(_mutex);
    // TODO: rate check per SERVER-27721
    if (newTime > _clusterTime.getTime()) {
        _clusterTime = _makeSignedLogicalTime(newTime);
    }

    return Status::OK();
}

LogicalTime LogicalClock::reserveTicks(uint64_t ticks) {

    invariant(ticks > 0);

    stdx::lock_guard<stdx::mutex> lock(_mutex);

    const unsigned wallClockSecs =
        durationCount<Seconds>(_service->getFastClockSource()->now().toDurationSinceEpoch());
    unsigned currentSecs = _clusterTime.getTime().asTimestamp().getSecs();
    LogicalTime clusterTimestamp = _clusterTime.getTime();

    if (BONGO_unlikely(currentSecs < wallClockSecs)) {
        clusterTimestamp = LogicalTime(Timestamp(wallClockSecs, 1));
    } else {
        clusterTimestamp.addTicks(1);
    }
    auto currentTime = clusterTimestamp;
    clusterTimestamp.addTicks(ticks - 1);

    // Fail if time is not moving forward for 2**31 ticks
    if (BONGO_unlikely(clusterTimestamp.asTimestamp().getSecs() > wallClockSecs) &&
        clusterTimestamp.asTimestamp().getInc() >= 1U << 31) {
        bongo::severe() << "clock skew detected, prev: " << wallClockSecs
                        << " now: " << clusterTimestamp.asTimestamp().getSecs();
        fassertFailed(17449);
    }

    _clusterTime = _makeSignedLogicalTime(clusterTimestamp);
    return currentTime;
}

void LogicalClock::initClusterTimeFromTrustedSource(LogicalTime newTime) {
    invariant(_clusterTime.getTime() == LogicalTime::kUninitialized);
    _clusterTime = _makeSignedLogicalTime(newTime);
}

}  // namespace bongo
