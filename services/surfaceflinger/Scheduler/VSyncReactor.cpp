/*
 * Copyright 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define ATRACE_TAG ATRACE_TAG_GRAPHICS
#undef LOG_TAG
#define LOG_TAG "VSyncReactor"
//#define LOG_NDEBUG 0
#include "VSyncReactor.h"
#include <cutils/properties.h>
#include <log/log.h>
#include <utils/Trace.h>
#include "../TracedOrdinal.h"
#include "TimeKeeper.h"
#include "VSyncDispatch.h"
#include "VSyncTracker.h"

namespace android::scheduler {
using base::StringAppendF;

Clock::~Clock() = default;
nsecs_t SystemClock::now() const {
    return systemTime(SYSTEM_TIME_MONOTONIC);
}

class PredictedVsyncTracer {
public:
    PredictedVsyncTracer(VSyncDispatch& dispatch)
          : mRegistration(dispatch, std::bind(&PredictedVsyncTracer::callback, this),
                          "PredictedVsyncTracer") {
        scheduleRegistration();
    }

private:
    TracedOrdinal<bool> mParity = {"VSYNC-predicted", 0};
    VSyncCallbackRegistration mRegistration;

    void scheduleRegistration() { mRegistration.schedule({0, 0, 0}); }

    void callback() {
        mParity = !mParity;
        scheduleRegistration();
    }
};

VSyncReactor::VSyncReactor(std::unique_ptr<Clock> clock, VSyncDispatch& dispatch,
                           VSyncTracker& tracker, size_t pendingFenceLimit,
                           bool supportKernelIdleTimer)
      : mClock(std::move(clock)),
        mTracker(tracker),
        mDispatch(dispatch),
        mPendingLimit(pendingFenceLimit),
        mPredictedVsyncTracer(property_get_bool("debug.sf.show_predicted_vsync", false)
                                      ? std::make_unique<PredictedVsyncTracer>(mDispatch)
                                      : nullptr),
        mSupportKernelIdleTimer(supportKernelIdleTimer) {}

VSyncReactor::~VSyncReactor() = default;

bool VSyncReactor::addPresentFence(const std::shared_ptr<FenceTime>& fence) {
    if (!fence) {
        return false;
    }

    nsecs_t const signalTime = fence->getCachedSignalTime();
    if (signalTime == Fence::SIGNAL_TIME_INVALID) {
        return true;
    }

    std::lock_guard lock(mMutex);
    if (mExternalIgnoreFences || mInternalIgnoreFences) {
        return true;
    }

    bool timestampAccepted = true;
    for (auto it = mUnfiredFences.begin(); it != mUnfiredFences.end();) {
        auto const time = (*it)->getCachedSignalTime();
        if (time == Fence::SIGNAL_TIME_PENDING) {
            it++;
        } else if (time == Fence::SIGNAL_TIME_INVALID) {
            it = mUnfiredFences.erase(it);
        } else {
            timestampAccepted &= mTracker.addVsyncTimestamp(time);

            it = mUnfiredFences.erase(it);
        }
    }

    if (signalTime == Fence::SIGNAL_TIME_PENDING) {
        if (mPendingLimit == mUnfiredFences.size()) {
            mUnfiredFences.erase(mUnfiredFences.begin());
        }
        mUnfiredFences.push_back(fence);
    } else {
        timestampAccepted &= mTracker.addVsyncTimestamp(signalTime);
    }

    if (!timestampAccepted) {
        mMoreSamplesNeeded = true;
        setIgnorePresentFencesInternal(true);
        mPeriodConfirmationInProgress = true;
    }

    return mMoreSamplesNeeded;
}

void VSyncReactor::setIgnorePresentFences(bool ignoration) {
    std::lock_guard lock(mMutex);
    mExternalIgnoreFences = ignoration;
    updateIgnorePresentFencesInternal();
}

void VSyncReactor::setIgnorePresentFencesInternal(bool ignoration) {
    mInternalIgnoreFences = ignoration;
    updateIgnorePresentFencesInternal();
}

void VSyncReactor::updateIgnorePresentFencesInternal() {
    if (mExternalIgnoreFences || mInternalIgnoreFences) {
        mUnfiredFences.clear();
    }
}

nsecs_t VSyncReactor::computeNextRefresh(int periodOffset, nsecs_t now) const {
    auto const currentPeriod = periodOffset ? mTracker.currentPeriod() : 0;
    return mTracker.nextAnticipatedVSyncTimeFrom(now + periodOffset * currentPeriod);
}

nsecs_t VSyncReactor::expectedPresentTime(nsecs_t now) {
    return mTracker.nextAnticipatedVSyncTimeFrom(now);
}

void VSyncReactor::startPeriodTransition(nsecs_t newPeriod) {
    ATRACE_CALL();
    mPeriodConfirmationInProgress = true;
    mPeriodTransitioningTo = newPeriod;
    mMoreSamplesNeeded = true;
    setIgnorePresentFencesInternal(true);
}

void VSyncReactor::endPeriodTransition() {
    ATRACE_CALL();
    mPeriodTransitioningTo.reset();
    mPeriodConfirmationInProgress = false;
    mLastHwVsync.reset();
}

void VSyncReactor::setPeriod(nsecs_t period) {
    ATRACE_INT64("VSR-setPeriod", period);
    std::lock_guard lk(mMutex);
    mLastHwVsync.reset();

    if (!mSupportKernelIdleTimer && period == getPeriod()) {
        endPeriodTransition();
        setIgnorePresentFencesInternal(false);
        mMoreSamplesNeeded = false;
    } else {
        startPeriodTransition(period);
    }
}

nsecs_t VSyncReactor::getPeriod() {
    return mTracker.currentPeriod();
}

void VSyncReactor::beginResync() {
    mTracker.resetModel();
}

bool VSyncReactor::periodConfirmed(nsecs_t vsync_timestamp, std::optional<nsecs_t> HwcVsyncPeriod) {
    if (!mPeriodConfirmationInProgress) {
        return false;
    }

    if (!mLastHwVsync && !HwcVsyncPeriod) {
        return false;
    }

    const bool periodIsChanging =
            mPeriodTransitioningTo && (*mPeriodTransitioningTo != getPeriod());
    if (mSupportKernelIdleTimer && !periodIsChanging) {
        // Clear out the Composer-provided period and use the allowance logic below
        HwcVsyncPeriod = {};
    }

    auto const period = mPeriodTransitioningTo ? *mPeriodTransitioningTo : getPeriod();
    static constexpr int allowancePercent = 10;
    static constexpr std::ratio<allowancePercent, 100> allowancePercentRatio;
    auto const allowance = period * allowancePercentRatio.num / allowancePercentRatio.den;
    if (HwcVsyncPeriod) {
        return std::abs(*HwcVsyncPeriod - period) < allowance;
    }

    auto const distance = vsync_timestamp - *mLastHwVsync;
    return std::abs(distance - period) < allowance;
}

bool VSyncReactor::addResyncSample(nsecs_t timestamp, std::optional<nsecs_t> hwcVsyncPeriod,
                                   bool* periodFlushed) {
    assert(periodFlushed);

    std::lock_guard lock(mMutex);
    if (periodConfirmed(timestamp, hwcVsyncPeriod)) {
        ATRACE_NAME("VSR: period confirmed");
        if (mPeriodTransitioningTo) {
            mTracker.setPeriod(*mPeriodTransitioningTo);
            *periodFlushed = true;
        }

        if (mLastHwVsync) {
            mTracker.addVsyncTimestamp(*mLastHwVsync);
        }
        mTracker.addVsyncTimestamp(timestamp);

        endPeriodTransition();
        mMoreSamplesNeeded = mTracker.needsMoreSamples();
    } else if (mPeriodConfirmationInProgress) {
        ATRACE_NAME("VSR: still confirming period");
        mLastHwVsync = timestamp;
        mMoreSamplesNeeded = true;
        *periodFlushed = false;
    } else {
        ATRACE_NAME("VSR: adding sample");
        *periodFlushed = false;
        mTracker.addVsyncTimestamp(timestamp);
        mMoreSamplesNeeded = mTracker.needsMoreSamples();
    }

    if (!mMoreSamplesNeeded) {
        setIgnorePresentFencesInternal(false);
    }
    return mMoreSamplesNeeded;
}

void VSyncReactor::dump(std::string& result) const {
    std::lock_guard lock(mMutex);
    StringAppendF(&result, "VsyncReactor in use\n");
    StringAppendF(&result, "Has %zu unfired fences\n", mUnfiredFences.size());
    StringAppendF(&result, "mInternalIgnoreFences=%d mExternalIgnoreFences=%d\n",
                  mInternalIgnoreFences, mExternalIgnoreFences);
    StringAppendF(&result, "mMoreSamplesNeeded=%d mPeriodConfirmationInProgress=%d\n",
                  mMoreSamplesNeeded, mPeriodConfirmationInProgress);
    if (mPeriodTransitioningTo) {
        StringAppendF(&result, "mPeriodTransitioningTo=%" PRId64 "\n", *mPeriodTransitioningTo);
    } else {
        StringAppendF(&result, "mPeriodTransitioningTo=nullptr\n");
    }

    if (mLastHwVsync) {
        StringAppendF(&result, "Last HW vsync was %.2fms ago\n",
                      (mClock->now() - *mLastHwVsync) / 1e6f);
    } else {
        StringAppendF(&result, "No Last HW vsync\n");
    }

    StringAppendF(&result, "VSyncTracker:\n");
    mTracker.dump(result);
    StringAppendF(&result, "VSyncDispatch:\n");
    mDispatch.dump(result);
}

} // namespace android::scheduler
