/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "../Macros.h"

#include "KeyMouseInputMapper.h"
#include "TouchCursorInputMapperCommon.h"
#include <cutils/properties.h>

namespace android {

// --- Static Definitions ---

KeyMouseInputMapper::KeyMouseInputMapper(InputDeviceContext& deviceContext,
                                     const InputReaderConfiguration& readerConfig)
      : InputMapper(deviceContext, readerConfig),
        mLastEventTime(std::numeric_limits<nsecs_t>::min()) {}

KeyMouseInputMapper::~KeyMouseInputMapper() {
}

uint32_t KeyMouseInputMapper::getSources() const{
    return AINPUT_SOURCE_MOUSE;
}

void KeyMouseInputMapper::populateDeviceInfo(InputDeviceInfo& info) {
    InputMapper::populateDeviceInfo(info);
}

void KeyMouseInputMapper::dump(std::string& dump) {

}

std::list<NotifyArgs> KeyMouseInputMapper::reconfigure(nsecs_t when,
                                                     const InputReaderConfiguration& readerConfig,
                                                     ConfigurationChanges changes) {

    std::list<NotifyArgs> out = InputMapper::reconfigure(when, readerConfig, changes);
    mSource=AINPUT_SOURCE_MOUSE;
    mXPrecision = 1.0f;
    mYPrecision = 1.0f;
    mPointerController = getContext()->getPointerController(getDeviceId());
    mDisplayWidth = 0;
    mDisplayHeight = 0;
    return out;
}

std::list<NotifyArgs> KeyMouseInputMapper::reset(nsecs_t when) {
    mButtonState = 0;
    mDownTime = 0;
    mLastEventTime = std::numeric_limits<nsecs_t>::min();
    mCursorButtonAccumulator.reset(getDeviceContext());

    return InputMapper::reset(when);
}

std::list<NotifyArgs> KeyMouseInputMapper::process(const RawEvent* rawEvent) {

	std::list<NotifyArgs> out;
        mCursorButtonAccumulator.process(rawEvent);

        if (rawEvent->type == EV_KEY && ((rawEvent->code== 28)||(rawEvent->code== 232))) {
                mdeltax = 0;
                mdeltay = 0;
                const auto [eventTime, readTime] =
                   applyBluetoothTimestampSmoothening(getDeviceContext().getDeviceIdentifier(),
                                                   rawEvent->when, rawEvent->readTime,
                                                   mLastEventTime);
            out += sync(eventTime, readTime);
            mLastEventTime = eventTime;
        }
	return out;
}

std::list<NotifyArgs> KeyMouseInputMapper::sync(nsecs_t when, nsecs_t readTime) {
    std::list<NotifyArgs> out;
    /*if (!mDisplayId) {
        // Ignore events when there is no target display configured.
        return out;
    }*/

    int32_t lastButtonState = mButtonState;
    int32_t currentButtonState = mCursorButtonAccumulator.getButtonState();
    mButtonState = currentButtonState;
    char mKeyLock[PROPERTY_VALUE_MAX] = "";
    memset(mKeyLock,0,5);
    property_get("sys.KeyMouse.mKeyMouseState", mKeyLock, "off");

    bool wasDown = isPointerDown(lastButtonState);
    bool down = isPointerDown(currentButtonState);
    bool downChanged;
    if (!wasDown && down) {
        mDownTime = when;
        downChanged = true;
    } else if (wasDown && !down) {
        downChanged = true;
    } else {
        downChanged = false;
    }
    nsecs_t downTime = mDownTime;
    if(strcmp(mKeyLock, "off") == 0) {
        return out;
    }
    bool buttonsChanged = currentButtonState != lastButtonState;
    int32_t buttonsPressed = currentButtonState & ~lastButtonState;
    int32_t buttonsReleased = lastButtonState & ~currentButtonState;

    float deltaX = mdeltax;
    float deltaY = mdeltay;


    // Move the pointer.
    PointerProperties pointerProperties;
    pointerProperties.clear();
    pointerProperties.id = 0;
    pointerProperties.toolType = ToolType::MOUSE;

    PointerCoords pointerCoords;
    pointerCoords.clear();

    //paint the pointer of mouse here
    uint32_t policyFlags = 0;
    policyFlags |= POLICY_FLAG_WAKE;

    int32_t displayId = ADISPLAY_ID_DEFAULT;
    float xCursorPosition = AMOTION_EVENT_INVALID_CURSOR_POSITION;
    float yCursorPosition = AMOTION_EVENT_INVALID_CURSOR_POSITION;

    if (mPointerController != NULL) {
        mPointerController->setPresentation(PointerControllerInterface::Presentation::POINTER);
        mPointerController->move(deltaX,deltaY);
        mPointerController->unfade(PointerControllerInterface::Transition::IMMEDIATE);

	std::tie(xCursorPosition, yCursorPosition) = mPointerController->getPosition();
        pointerCoords.setAxisValue(AMOTION_EVENT_AXIS_X, xCursorPosition);
        pointerCoords.setAxisValue(AMOTION_EVENT_AXIS_Y, yCursorPosition);
        mDisplayId = mPointerController->getDisplayId();//ADISPLAY_ID_DEFAULT;
    }
    // Moving an external trackball or mouse should wake the device.
    // We don't do this for internal cursor devices to prevent them from waking up
    // the device in your pocket.
    // TODO: Use the input device configuration to control this behavior more finely.

    // Synthesize key down from buttons if needed.
    out +=  synthesizeButtonKeys(getContext(), AKEY_EVENT_ACTION_DOWN, when, readTime, getDeviceId(),
                         mSource, displayId, policyFlags, lastButtonState, currentButtonState);

    // Send motion event.
    if (downChanged || buttonsChanged) {
        int32_t metaState = getContext()->getGlobalMetaState();
        int32_t buttonState = lastButtonState;
        int32_t motionEventAction;
        if (downChanged) {
            motionEventAction = down ? AMOTION_EVENT_ACTION_DOWN : AMOTION_EVENT_ACTION_UP;
        } else if (down || (mSource != AINPUT_SOURCE_MOUSE)) {
            motionEventAction = AMOTION_EVENT_ACTION_MOVE;
        } else {
            motionEventAction = AMOTION_EVENT_ACTION_HOVER_MOVE;
        }
        if (buttonsReleased) {
            BitSet32 released(buttonsReleased);
            while (!released.isEmpty()) {
                int32_t actionButton = BitSet32::valueForBit(released.clearFirstMarkedBit());
                buttonState &= ~actionButton;
                out.push_back(NotifyMotionArgs(getContext()->getNextId(), when, readTime,
                                               getDeviceId(), mSource, *mDisplayId, policyFlags,
                                               AMOTION_EVENT_ACTION_BUTTON_RELEASE, actionButton, 0,
                                               metaState, buttonState, MotionClassification::NONE,
                                               AMOTION_EVENT_EDGE_FLAG_NONE, 1, &pointerProperties,
                                               &pointerCoords, mXPrecision, mYPrecision,
                                               xCursorPosition, yCursorPosition, downTime,
                                               /* videoFrames */ {}));
            }
        }

        out.push_back(NotifyMotionArgs(getContext()->getNextId(), when, readTime, getDeviceId(),
                                       mSource, *mDisplayId, policyFlags, motionEventAction, 0, 0,
                                       metaState, currentButtonState, MotionClassification::NONE,
                                       AMOTION_EVENT_EDGE_FLAG_NONE, 1, &pointerProperties,
                                       &pointerCoords, mXPrecision, mYPrecision, xCursorPosition,
                                       yCursorPosition, downTime,
                                       /* videoFrames */ {}));

        if (buttonsPressed) {
            BitSet32 pressed(buttonsPressed);
            while (!pressed.isEmpty()) {
                int32_t actionButton = BitSet32::valueForBit(pressed.clearFirstMarkedBit());
                buttonState |= actionButton;
                out.push_back(NotifyMotionArgs(getContext()->getNextId(), when, readTime,
                                               getDeviceId(), mSource, *mDisplayId, policyFlags,
                                               AMOTION_EVENT_ACTION_BUTTON_PRESS, actionButton, 0,
                                               metaState, buttonState, MotionClassification::NONE,
                                               AMOTION_EVENT_EDGE_FLAG_NONE, 1, &pointerProperties,
                                               &pointerCoords, mXPrecision, mYPrecision,
                                               xCursorPosition, yCursorPosition, downTime,
                                               /* videoFrames */ {}));
            }
        }

        ALOG_ASSERT(buttonState == currentButtonState);

        // Send hover move after UP to tell the application that the mouse is hovering now.
        if (motionEventAction == AMOTION_EVENT_ACTION_UP
                && (mSource == AINPUT_SOURCE_MOUSE)) {
            if (buttonsChanged) {
                mPointerController->setPresentation(
                        PointerControllerInterface::Presentation::POINTER);


                mPointerController->unfade(PointerControllerInterface::Transition::IMMEDIATE);
            }

            if (false) {
                // Rotate the cursor position that is in PointerController's rotated coordinate
                // space to InputReader's un-rotated coordinate space.
                //rotatePoint(mOrientation, xCursorPosition /*byRef*/, yCursorPosition /*byRef*/,
                 //           mDisplayWidth, mDisplayHeight);
            }
            out.push_back(NotifyMotionArgs(getContext()->getNextId(), when, readTime, getDeviceId(),
                                           mSource, *mDisplayId, policyFlags,
                                           AMOTION_EVENT_ACTION_HOVER_MOVE, 0, 0, metaState,
                                           currentButtonState, MotionClassification::NONE,
                                           AMOTION_EVENT_EDGE_FLAG_NONE, 1, &pointerProperties,
                                           &pointerCoords, mXPrecision, mYPrecision,
                                           xCursorPosition, yCursorPosition, downTime,
                                           /* videoFrames */ {}));
        }

    }
    // Synthesize key up from buttons if needed.
    out += synthesizeButtonKeys(getContext(), AKEY_EVENT_ACTION_UP, when, readTime, getDeviceId(), mSource,
                         displayId, policyFlags, lastButtonState, currentButtonState);
    return out;
}

int32_t KeyMouseInputMapper::getScanCodeState(uint32_t sourceMask, int32_t scanCode) {
    if (scanCode >= BTN_MOUSE && scanCode < BTN_JOYSTICK) {
        return getDeviceContext().getScanCodeState(scanCode);
    } else {
        return AKEY_STATE_UNKNOWN;
    }
}

void KeyMouseInputMapper::fadePointer() {

}

} // namespace android
