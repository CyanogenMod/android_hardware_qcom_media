/*
 * Copyright (C) 2009 The Android Open Source Project
 * Copyright (c) 2011, Code Aurora Forum. All rights reserved.
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

#define LOG_TAG "AudioPolicyManager"
//#define LOG_NDEBUG 0
#include <utils/Log.h>
#include "AudioPolicyManager.h"
#include <media/mediarecorder.h>
#include <fcntl.h>
#include <cutils/properties.h> // for property_get

namespace android_audio_legacy {



// ----------------------------------------------------------------------------
// AudioPolicyManager for msm7k platform
// Common audio policy manager code is implemented in AudioPolicyManagerBase class
// ----------------------------------------------------------------------------

// ---  class factory


extern "C" AudioPolicyInterface* createAudioPolicyManager(AudioPolicyClientInterface *clientInterface)
{
    return new AudioPolicyManager(clientInterface);
}

extern "C" void destroyAudioPolicyManager(AudioPolicyInterface *interface)
{
    delete interface;
}


void AudioPolicyManager::setPhoneState(int state)
{
    LOGE("setPhoneState() state %d", state);
    uint32_t newDevice = 0;
    if (state < 0 || state >= AudioSystem::NUM_MODES) {
        LOGW("setPhoneState() invalid state %d", state);
        return;
    }

    if (state == mPhoneState ) {
        LOGW("setPhoneState() setting same state %d", state);
        return;
    }

    // if leaving call state, handle special case of active streams
    // pertaining to sonification strategy see handleIncallSonification()
    if (isInCall()) {
        LOGV("setPhoneState() in call state management: new state is %d", state);
        for (int stream = 0; stream < AudioSystem::NUM_STREAM_TYPES; stream++) {
            AudioPolicyManagerBase::handleIncallSonification(stream, false, true);
        }
    }

    // store previous phone state for management of sonification strategy below
    int oldState = mPhoneState;
    mPhoneState = state;
    // force routing command to audio hardware when starting call
    // even if no device change is needed
    bool force = (mPhoneState == AudioSystem::MODE_IN_CALL);

    // are we entering or starting a call
    if (!isStateInCall(oldState) && isStateInCall(state)) {
        LOGV("  Entering call in setPhoneState()");
        // force routing command to audio hardware when starting a call
        // even if no device change is needed
        force = true;
    } else if (isStateInCall(oldState) && !isStateInCall(state)) {
        LOGV("  Exiting call in setPhoneState()");
        // force routing command to audio hardware when exiting a call
        // even if no device change is needed
        force = true;
    } else if (isStateInCall(state) && (state != oldState)) {
        LOGV("  Switching between telephony and VoIP in setPhoneState()");
        // force routing command to audio hardware when switching between telephony and VoIP
        // even if no device change is needed
        force = true;
    }

    // check for device and output changes triggered by new phone state
    newDevice = getNewDevice(mHardwareOutput, false);
#ifdef WITH_A2DP
    AudioPolicyManagerBase::checkOutputForAllStrategies();
    AudioPolicyManagerBase::checkA2dpSuspend();
#endif
    AudioPolicyManagerBase::updateDeviceForStrategy();

    AudioOutputDescriptor *hwOutputDesc = mOutputs.valueFor(mHardwareOutput);

    // force routing command to audio hardware when ending call
    // even if no device change is needed
    if (isStateInCall(oldState) && newDevice == 0) {
        newDevice = hwOutputDesc->device();
        force = true;
    }

    // when changing from ring tone to in call mode, mute the ringing tone
    // immediately and delay the route change to avoid sending the ring tone
    // tail into the earpiece or headset.
    int delayMs = 0;
    if (isStateInCall(state) && oldState == AudioSystem::MODE_RINGTONE) {
        // delay the device change command by twice the output latency to have some margin
        // and be sure that audio buffers not yet affected by the mute are out when
        // we actually apply the route change
        delayMs = hwOutputDesc->mLatency*2;
        setStreamMute(AudioSystem::RING, true, mHardwareOutput);
    }

    // change routing is necessary
    setOutputDevice(mHardwareOutput, newDevice, force, delayMs);

    // if entering in call state, handle special case of active streams
    // pertaining to sonification strategy see handleIncallSonification()
    if (isStateInCall(state)) {
        LOGV("setPhoneState() in call state management: new state is %d", state);
        // unmute the ringing tone after a sufficient delay if it was muted before
        // setting output device above
        if (oldState == AudioSystem::MODE_RINGTONE) {
            setStreamMute(AudioSystem::RING, false, mHardwareOutput, MUTE_TIME_MS);
        }
        for (int stream = 0; stream < AudioSystem::NUM_STREAM_TYPES; stream++) {
            AudioPolicyManagerBase::handleIncallSonification(stream, true, true);
        }
    }

    // Flag that ringtone volume must be limited to music volume until we exit MODE_RINGTONE
    if (state == AudioSystem::MODE_RINGTONE &&
        (hwOutputDesc->mRefCount[AudioSystem::MUSIC])) {
//        (systemTime() - mMusicStopTime) < seconds(SONIFICATION_HEADSET_MUSIC_DELAY))) {
        mLimitRingtoneVolume = true;
    } else {
        mLimitRingtoneVolume = false;
    }
}


uint32_t AudioPolicyManager::getDeviceForStrategy(routing_strategy strategy, bool fromCache)
{
    uint32_t device = 0;

    if (fromCache) {
        LOGV("getDeviceForStrategy() from cache strategy %d, device %x", strategy, mDeviceForStrategy[strategy]);
        return mDeviceForStrategy[strategy];
    }

    switch (strategy) {
    case STRATEGY_DTMF:
        if (mPhoneState != AudioSystem::MODE_IN_CALL) {
            // when off call, DTMF strategy follows the same rules as MEDIA strategy
            device = getDeviceForStrategy(STRATEGY_MEDIA, false);
            break;
        }
        // when in call, DTMF and PHONE strategies follow the same rules
        // FALL THROUGH

    case STRATEGY_PHONE:
        // for phone strategy, we first consider the forced use and then the available devices by order
        // of priority
        switch (mForceUse[AudioSystem::FOR_COMMUNICATION]) {
        case AudioSystem::FORCE_BT_SCO:
            if (mPhoneState != AudioSystem::MODE_IN_CALL || strategy != STRATEGY_DTMF) {
                device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_BLUETOOTH_SCO_CARKIT;
                if (device) break;
            }
            device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_BLUETOOTH_SCO_HEADSET;
            if (device) break;
            device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_BLUETOOTH_SCO;
            if (device) break;
            // if SCO device is requested but no SCO device is available, fall back to default case
            // FALL THROUGH

        default:    // FORCE_NONE
            device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_WIRED_HEADPHONE;
            if (device) break;
            device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_WIRED_HEADSET;
            if (device) break;
#ifdef WITH_A2DP
            // when not in a phone call, phone strategy should route STREAM_VOICE_CALL to A2DP
            if (mPhoneState != AudioSystem::MODE_IN_CALL) {
                device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_BLUETOOTH_A2DP;
                if (device) break;
                device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_BLUETOOTH_A2DP_HEADPHONES;
                if (device) break;
            }
#endif
            if (mPhoneState == AudioSystem::MODE_RINGTONE)
                device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_SPEAKER;
            if (device) break;

            device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_EARPIECE;
            if (device == 0) {
                LOGE("getDeviceForStrategy() earpiece device not found");
            }
            break;

        case AudioSystem::FORCE_SPEAKER:
            if (mPhoneState != AudioSystem::MODE_IN_CALL || strategy != STRATEGY_DTMF) {
                device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_BLUETOOTH_SCO_CARKIT;
                if (device) break;
            }
#ifdef WITH_A2DP
            // when not in a phone call, phone strategy should route STREAM_VOICE_CALL to
            // A2DP speaker when forcing to speaker output
            if (mPhoneState != AudioSystem::MODE_IN_CALL) {
                device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_BLUETOOTH_A2DP_SPEAKER;
                if (device) break;
            }
#endif
            device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_SPEAKER;
            if (device == 0) {
                LOGE("getDeviceForStrategy() speaker device not found");
            }
            break;
        }
    break;

    case STRATEGY_SONIFICATION:

        // If incall, just select the STRATEGY_PHONE device: The rest of the behavior is handled by
        // handleIncallSonification().
        if (mPhoneState == AudioSystem::MODE_IN_CALL) {
            device = getDeviceForStrategy(STRATEGY_PHONE, false);
            break;
        }
        // FALL THROUGH

    case STRATEGY_ENFORCED_AUDIBLE:
        // strategy STRATEGY_ENFORCED_AUDIBLE uses same routing policy as STRATEGY_SONIFICATION
        // except when in call where it doesn't default to STRATEGY_PHONE behavior
        device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_SPEAKER;
        if (device == 0) {
            LOGE("getDeviceForStrategy() speaker device not found");
        }
        // The second device used for sonification is the same as the device used by media strategy
        // FALL THROUGH
#ifdef FM_RADIO

        // for analog FM alerts should be played on the speaker only
        if(FM_ANALOG == getFMMode())
            break;
#endif
    case STRATEGY_MEDIA: {
        switch (mForceUse[AudioSystem::FOR_MEDIA]) {
        default:{
            uint32_t device2 = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_AUX_DIGITAL;
#ifdef WITH_A2DP
            if ((mA2dpOutput != 0) && !(FM_ANALOG == getFMMode())) {
                /* analog fm over a2dp not supported */
                if (strategy == STRATEGY_SONIFICATION && !a2dpUsedForSonification()) {
                    break;
                }
                if (device2 == 0) {
                    device2 = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_BLUETOOTH_A2DP;
                }
                if (device2 == 0) {
                    device2 = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_BLUETOOTH_A2DP_HEADPHONES;
                }
                if (device2 == 0) {
                    device2 = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_BLUETOOTH_A2DP_SPEAKER;
                }
            }
#endif
            if (device2 == 0) {
                device2 = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_WIRED_HEADPHONE;
            }
            if (device2 == 0) {
                device2 = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_WIRED_HEADSET;
            }
            if (device2 == 0) {
                device2 = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_SPEAKER;
            }
            if (device2 == 0) {
                device2 = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_EARPIECE;
            }

            // device is DEVICE_OUT_SPEAKER if we come from case STRATEGY_SONIFICATION or
            // STRATEGY_ENFORCED_AUDIBLE, 0 otherwise
            device |= device2;
        }
            break;
        case AudioSystem::FORCE_SPEAKER:
            device = mAvailableOutputDevices & AudioSystem::DEVICE_OUT_SPEAKER;
            break;
       }

#ifdef FM_RADIO
       if (mAvailableOutputDevices & AudioSystem::DEVICE_OUT_FM) {
           device |= AudioSystem::DEVICE_OUT_FM;
           if(FM_ANALOG == getFMMode()){
               if (device == (AudioSystem::DEVICE_OUT_SPEAKER | AudioSystem::DEVICE_OUT_WIRED_HEADSET | AudioSystem::DEVICE_OUT_FM))
                  device = AudioSystem::DEVICE_OUT_SPEAKER;
               else if(device & AudioSystem::DEVICE_OUT_WIRED_HEADSET)
                  device &= ~(device & AudioSystem::DEVICE_OUT_WIRED_HEADSET);
          }
       }
#endif
// Do not play media stream if in call and the requested device would change the hardware
        // output routing
        if (mPhoneState == AudioSystem::MODE_IN_CALL &&
            !AudioSystem::isA2dpDevice((AudioSystem::audio_devices)device) &&
            device != getDeviceForStrategy(STRATEGY_PHONE)) {
            device = 0;
            LOGV("getDeviceForStrategy() incompatible media and phone devices");
        }
        } break;

    default:
        LOGW("getDeviceForStrategy() unknown strategy: %d", strategy);
        break;
    }

    LOGV("getDeviceForStrategy() strategy %d, device %x", strategy, device);
    return device;
}

status_t AudioPolicyManager::setDeviceConnectionState(AudioSystem::audio_devices device,
                                                      AudioSystem::device_connection_state state,
                                                      const char *device_address)
{

    LOGV("setDeviceConnectionState() device: %x, state %d, address %s", device, state, device_address);

    // connect/disconnect only 1 device at a time
    if (AudioSystem::popCount(device) != 1) return BAD_VALUE;

    if (strlen(device_address) >= MAX_DEVICE_ADDRESS_LEN) {
        LOGE("setDeviceConnectionState() invalid address: %s", device_address);
        return BAD_VALUE;
    }

    // handle output devices
    if (AudioSystem::isOutputDevice(device)) {

#ifndef WITH_A2DP
        if (AudioSystem::isA2dpDevice(device)) {
            LOGE("setDeviceConnectionState() invalid device: %x", device);
            return BAD_VALUE;
        }
#endif

        switch (state)
        {
        // handle output device connection
        case AudioSystem::DEVICE_STATE_AVAILABLE:
#ifdef FM_RADIO

            if(device == AudioSystem::DEVICE_OUT_FM){
                char value[PROPERTY_VALUE_MAX];
                fm_modes fmMode = FM_DIGITAL;

                if (property_get("hw.fm.isAnalog", value, NULL)
                && !strcasecmp(value, "true")){
                    fmMode = FM_ANALOG ;
                }

                LOGD("Current FM mode %d, New Fm Mode %d",getFMMode(),fmMode);

                if (fmMode == getFMMode()){
                    LOGE("FM is already connected in %d Mode",fmMode);
                    return INVALID_OPERATION;
                } else if (FM_NONE != getFMMode()){
                    LOGE("Rejctng dev conction:Anlg FM & Dgtl FM Mutuly xclusve");
                    return INVALID_OPERATION;
                }else{
                    setFmMode(fmMode);
                    LOGW("FM started in %d Mode",fmMode);
                }
            }
#endif
            if (mAvailableOutputDevices & device) {
                LOGW("setDeviceConnectionState() device already connected: %x", device);
                return INVALID_OPERATION;
            }
            LOGV("setDeviceConnectionState() connecting device %x", device);

            // register new device as available
            mAvailableOutputDevices |= device;

#ifdef WITH_A2DP
            // handle A2DP device connection
            if (AudioSystem::isA2dpDevice(device)) {
                status_t status = handleA2dpConnection(device, device_address);
                if (status != NO_ERROR) {
                    mAvailableOutputDevices &= ~device;
                    return status;
                }
            } else
#endif
            {
                if (AudioSystem::isBluetoothScoDevice(device)) {
                    LOGV("setDeviceConnectionState() BT SCO  device, address %s", device_address);
                    // keep track of SCO device address
                    mScoDeviceAddress = String8(device_address, MAX_DEVICE_ADDRESS_LEN);
                }
            }
            break;
        // handle output device disconnection
        case AudioSystem::DEVICE_STATE_UNAVAILABLE: {
#ifdef FM_RADIO

            if(device == AudioSystem::DEVICE_OUT_FM){
                uint32_t newDevice;
                fm_modes prevFmMode = getFMMode();

                LOGD("turning off Fm device in Mode %d",getFMMode());
                setFmMode(FM_NONE);
                newDevice = getDeviceForStrategy(STRATEGY_MEDIA, false);
                if((FM_ANALOG == prevFmMode) && ((newDevice & AudioSystem::DEVICE_OUT_BLUETOOTH_A2DP) ||
                   (newDevice & AudioSystem::DEVICE_OUT_BLUETOOTH_A2DP_HEADPHONES)||
                   (newDevice & AudioSystem::DEVICE_OUT_BLUETOOTH_A2DP_SPEAKER))) {
                    LOGW("setDeviceConnectionState() FM off, switch to Wired Headset");
                    setOutputDevice(mHardwareOutput, (AudioSystem::DEVICE_OUT_WIRED_HEADSET), true);
                }
            }
#endif
            if (!(mAvailableOutputDevices & device)) {
                LOGW("setDeviceConnectionState() device not connected: %x", device);
                return INVALID_OPERATION;
            }


            LOGV("setDeviceConnectionState() disconnecting device %x", device);
            // remove device from available output devices
            mAvailableOutputDevices &= ~device;

#ifdef WITH_A2DP
            // handle A2DP device disconnection
            if (AudioSystem::isA2dpDevice(device)) {
                status_t status = handleA2dpDisconnection(device, device_address);
                if (status != NO_ERROR) {
                    mAvailableOutputDevices |= device;
                    return status;
                }
            } else
#endif
            {
                if (AudioSystem::isBluetoothScoDevice(device)) {
                    mScoDeviceAddress = "";
                }
            }
            } break;

        default:
            LOGE("setDeviceConnectionState() invalid state: %x", state);
            return BAD_VALUE;
        }

        // request routing change if necessary
        uint32_t newDevice = getNewDevice(mHardwareOutput, false);
#ifdef FM_RADIO
        if (device == AudioSystem::DEVICE_OUT_FM) {
            if (state == AudioSystem::DEVICE_STATE_AVAILABLE) {
                mOutputs.valueFor(mHardwareOutput)->changeRefCount(AudioSystem::FM, 1);
            }
            else {
                mOutputs.valueFor(mHardwareOutput)->changeRefCount(AudioSystem::FM, -1);
            }
            if (newDevice == 0) {
                newDevice = getDeviceForStrategy(STRATEGY_MEDIA, false);
            }
        }
#endif
#ifdef WITH_A2DP
        AudioPolicyManagerBase::checkOutputForAllStrategies();
        // A2DP outputs must be closed after checkOutputForAllStrategies() is executed
        if (state == AudioSystem::DEVICE_STATE_UNAVAILABLE && AudioSystem::isA2dpDevice(device)) {
            closeA2dpOutputs();
        }
        checkA2dpSuspend();
#endif
        updateDeviceForStrategy();
        setOutputDevice(mHardwareOutput, newDevice);

        if (device == AudioSystem::DEVICE_OUT_WIRED_HEADSET) {
            device = AudioSystem::DEVICE_IN_WIRED_HEADSET;
        } else if (device == AudioSystem::DEVICE_OUT_BLUETOOTH_SCO ||
                   device == AudioSystem::DEVICE_OUT_BLUETOOTH_SCO_HEADSET ||
                   device == AudioSystem::DEVICE_OUT_BLUETOOTH_SCO_CARKIT) {
            device = AudioSystem::DEVICE_IN_BLUETOOTH_SCO_HEADSET;
        } else {
            return NO_ERROR;
        }
    }
    // handle input devices
    if (AudioSystem::isInputDevice(device)) {
        switch (state)
        {
        // handle input device connection
        case AudioSystem::DEVICE_STATE_AVAILABLE: {
            if (mAvailableInputDevices & device) {
                LOGW("setDeviceConnectionState() device already connected: %d", device);
                return INVALID_OPERATION;
            }
            mAvailableInputDevices |= device;
            }
            break;

        // handle input device disconnection
        case AudioSystem::DEVICE_STATE_UNAVAILABLE: {
            if (!(mAvailableInputDevices & device)) {
                LOGW("setDeviceConnectionState() device not connected: %d", device);
                return INVALID_OPERATION;
            }
            mAvailableInputDevices &= ~device;
            } break;

        default:
            LOGE("setDeviceConnectionState() invalid state: %x", state);
            return BAD_VALUE;
        }

        audio_io_handle_t activeInput = getActiveInput();
        if (activeInput != 0) {
            AudioInputDescriptor *inputDesc = mInputs.valueFor(activeInput);
            uint32_t newDevice = getDeviceForInputSource(inputDesc->mInputSource);
            LOGV("setDeviceConnectionState() changing device from %x to %x for input %d",
                inputDesc->mDevice, newDevice, activeInput);
            inputDesc->mDevice = newDevice;
            AudioParameter param = AudioParameter();
            param.addInt(String8(AudioParameter::keyRouting), (int)newDevice);
            LOGV("String to set param in setDeviceconnection %s\n", param.toString().string());
                mpClientInterface->setParameters(activeInput, param.toString());
        }

        return NO_ERROR;
    }

    LOGW("setDeviceConnectionState() invalid device: %x", device);
    return BAD_VALUE;
}

void AudioPolicyManager::setForceUse(AudioSystem::force_use usage, AudioSystem::forced_config config)
{
    LOGV("setForceUse() usage %d, config %d, mPhoneState %d", usage, config, mPhoneState);

    bool forceVolumeReeval = false;
    switch(usage) {
    case AudioSystem::FOR_COMMUNICATION:
        if (config != AudioSystem::FORCE_SPEAKER && config != AudioSystem::FORCE_BT_SCO &&
            config != AudioSystem::FORCE_NONE) {
            LOGW("setForceUse() invalid config %d for FOR_COMMUNICATION", config);
            return;
        }
        mForceUse[usage] = config;
        break;
    case AudioSystem::FOR_MEDIA:
        if (config != AudioSystem::FORCE_HEADPHONES && config != AudioSystem::FORCE_BT_A2DP &&
            config != AudioSystem::FORCE_WIRED_ACCESSORY && config != AudioSystem::FORCE_NONE &&
            config != AudioSystem::FORCE_SPEAKER) {
            LOGW("setForceUse() invalid config %d for FOR_MEDIA", config);
            return;
        }
        mForceUse[usage] = config;
        {
            uint32_t device = getDeviceForStrategy(STRATEGY_MEDIA);
            setOutputDevice(mHardwareOutput, device);
        }
        break;
    case AudioSystem::FOR_RECORD:
        if (config != AudioSystem::FORCE_BT_SCO && config != AudioSystem::FORCE_WIRED_ACCESSORY &&
            config != AudioSystem::FORCE_NONE) {
            LOGW("setForceUse() invalid config %d for FOR_RECORD", config);
            return;
        }
        mForceUse[usage] = config;
        break;
    case AudioSystem::FOR_DOCK:
        if (config != AudioSystem::FORCE_NONE && config != AudioSystem::FORCE_BT_CAR_DOCK &&
            config != AudioSystem::FORCE_BT_DESK_DOCK && config != AudioSystem::FORCE_WIRED_ACCESSORY) {
            LOGW("setForceUse() invalid config %d for FOR_DOCK", config);
        }
        forceVolumeReeval = true;
        mForceUse[usage] = config;
        break;
    default:
        LOGW("setForceUse() invalid usage %d", usage);
        break;
    }

    // check for device and output changes triggered by new phone state
    uint32_t newDevice = getNewDevice(mHardwareOutput, false);
#ifdef WITH_A2DP
    checkOutputForAllStrategies();
    checkA2dpSuspend();
#endif
    updateDeviceForStrategy();
    setOutputDevice(mHardwareOutput, newDevice);
    if (forceVolumeReeval) {
        applyStreamVolumes(mHardwareOutput, newDevice);
    }

    audio_io_handle_t activeInput = getActiveInput();
    if (activeInput != 0) {
        AudioInputDescriptor *inputDesc = mInputs.valueFor(activeInput);
        newDevice = getDeviceForInputSource(inputDesc->mInputSource);
        if (newDevice != inputDesc->mDevice) {
            LOGV("setForceUse() changing device from %x to %x for input %d",
                    inputDesc->mDevice, newDevice, activeInput);
            inputDesc->mDevice = newDevice;
            AudioParameter param = AudioParameter();
            param.addInt(String8(AudioParameter::keyRouting), (int)newDevice);
            mpClientInterface->setParameters(activeInput, param.toString());
        }
    }

}

uint32_t AudioPolicyManager::getDeviceForInputSource(int inputSource)
{
    uint32_t device;

    switch(inputSource) {
    case AUDIO_SOURCE_DEFAULT:
    case AUDIO_SOURCE_MIC:
    case AUDIO_SOURCE_VOICE_RECOGNITION:
    case AUDIO_SOURCE_VOICE_COMMUNICATION:
        if (mForceUse[AudioSystem::FOR_RECORD] == AudioSystem::FORCE_BT_SCO &&
            mAvailableInputDevices & AudioSystem::DEVICE_IN_BLUETOOTH_SCO_HEADSET) {
            device = AudioSystem::DEVICE_IN_BLUETOOTH_SCO_HEADSET;
        } else if (mAvailableInputDevices & AudioSystem::DEVICE_IN_WIRED_HEADSET) {
            device = AudioSystem::DEVICE_IN_WIRED_HEADSET;
        } else {
            device = AudioSystem::DEVICE_IN_BUILTIN_MIC;
        }
        break;
    case AUDIO_SOURCE_CAMCORDER:
        if (hasBackMicrophone()) {
            device = AudioSystem::DEVICE_IN_BACK_MIC;
        } else {
            device = AudioSystem::DEVICE_IN_BUILTIN_MIC;
        }
        break;
    case AUDIO_SOURCE_VOICE_UPLINK:
    case AUDIO_SOURCE_VOICE_DOWNLINK:
    case AUDIO_SOURCE_VOICE_CALL:
        device = AudioSystem::DEVICE_IN_VOICE_CALL;
        break;
#ifdef FM_RADIO
    case AUDIO_SOURCE_FM_RX:
        device = AudioSystem::DEVICE_IN_FM_RX;
        break;
    case AUDIO_SOURCE_FM_RX_A2DP:
        device = AudioSystem::DEVICE_IN_FM_RX_A2DP;
        break;
#endif

    default:
        LOGW("getInput() invalid input source %d", inputSource);
        device = 0;
        break;
    }
    LOGV("getDeviceForInputSource()input source %d, device %08x", inputSource, device);
    return device;
}

status_t AudioPolicyManager::startOutput(audio_io_handle_t output,
                                             AudioSystem::stream_type stream,
                                             int session)
{
    LOGI("startOutput() output %d, stream %d, session %d", output, stream, session);
    ssize_t index = mOutputs.indexOfKey(output);
    if (index < 0) {
        LOGW("startOutput() unknow output %d", output);
        return BAD_VALUE;
    }

    AudioOutputDescriptor *outputDesc = mOutputs.valueAt(index);
    routing_strategy strategy = getStrategy((AudioSystem::stream_type)stream);

#ifdef WITH_A2DP
    if (mA2dpOutput != 0  && !a2dpUsedForSonification() &&
            (strategy == STRATEGY_SONIFICATION || strategy == STRATEGY_ENFORCED_AUDIBLE)) {
        setStrategyMute(STRATEGY_MEDIA, true, mA2dpOutput);
    }
#endif

    // incremenent usage count for this stream on the requested output:
    // NOTE that the usage count is the same for duplicated output and hardware output which is
    // necassary for a correct control of hardware output routing by startOutput() and stopOutput()
    outputDesc->changeRefCount(stream, 1);

    uint32_t NewDevice = AudioPolicyManagerBase::getNewDevice(output);
#ifdef FM_RADIO
    if((stream == AudioSystem::SYSTEM) && (FM_ANALOG == getFMMode())
    && (NewDevice == AudioSystem::DEVICE_OUT_FM)){
        NewDevice |= AudioSystem::DEVICE_OUT_WIRED_HEADSET;
        LOGE("Selecting AnlgFM + CODEC device %x",NewDevice);
    }
#endif
    setOutputDevice(output, NewDevice, true);

    // handle special case for sonification while in call
    if (isInCall()) {
        AudioPolicyManagerBase::handleIncallSonification(stream, true, false);
    }

    // apply volume rules for current stream and device if necessary
    checkAndSetVolume(stream, mStreams[stream].mIndexCur, output, outputDesc->device());

    return NO_ERROR;
}

status_t AudioPolicyManager::stopInput(audio_io_handle_t input)
{
    LOGV("stopInput() input %d", input);
    uint32_t newDevice = NULL;
    ssize_t index = mInputs.indexOfKey(input);
    if (index < 0) {
        LOGW("stopInput() unknow input %d", input);
        return BAD_VALUE;
    }
    AudioInputDescriptor *inputDesc = mInputs.valueAt(index);

    if (inputDesc->mRefCount == 0) {
        LOGW("stopInput() input %d already stopped", input);
        return INVALID_OPERATION;
    } else {
        AudioParameter param = AudioParameter();
        param.addInt(String8(AudioParameter::keyRouting), 0);
        LOGV("stopInput string to setParam %s\n",  param.toString().string());
        mpClientInterface->setParameters(input, param.toString());
        inputDesc->mRefCount = 0;

        newDevice = AudioPolicyManagerBase::getNewDevice(mHardwareOutput);
        param.addInt(String8(AudioParameter::keyRouting), (int)newDevice);
        mpClientInterface->setParameters(mHardwareOutput, param.toString());
    }
    return NO_ERROR;
}

status_t AudioPolicyManager::stopOutput(audio_io_handle_t output,
                                            AudioSystem::stream_type stream,
                                            int session)
{
    LOGV("stopOutput() output %d, stream %d, session %d", output, stream, session);
    ssize_t index = mOutputs.indexOfKey(output);
    if (index < 0) {
        LOGW("stopOutput() unknow output %d", output);
        return BAD_VALUE;
    }

    AudioOutputDescriptor *outputDesc = mOutputs.valueAt(index);
    routing_strategy strategy = AudioPolicyManagerBase::getStrategy((AudioSystem::stream_type)stream);

    // handle special case for sonification while in call
    if (isInCall()) {
        AudioPolicyManagerBase::handleIncallSonification(stream, false, false);
    }

    if (outputDesc->mRefCount[stream] > 0) {
        // decrement usage count of this stream on the output
        outputDesc->changeRefCount(stream, -1);
        // store time at which the last music track was stopped - see computeVolume()
#if 0 //TODO
    if (stream == AudioSystem::MUSIC) {
            mMusicStopTime = systemTime();
        }
#endif
        uint32_t newDevice = AudioPolicyManagerBase::getNewDevice(mHardwareOutput, false);


#ifdef FM_RADIO
        if(FM_ANALOG == getFMMode())
            setOutputDevice(output, newDevice,true);
        else
#endif
		setOutputDevice(output, newDevice);

#ifdef WITH_A2DP
        if (mA2dpOutput != 0 && !a2dpUsedForSonification() &&
                (strategy == STRATEGY_SONIFICATION || strategy == STRATEGY_ENFORCED_AUDIBLE)) {
            setStrategyMute(STRATEGY_MEDIA, false, mA2dpOutput, mOutputs.valueFor(mHardwareOutput)->mLatency*2);
        }
#endif
        if (output != mHardwareOutput) {
            setOutputDevice(mHardwareOutput, AudioPolicyManagerBase::getNewDevice(mHardwareOutput), true);
        }
        return NO_ERROR;
    } else {
        LOGW("stopOutput() refcount is already 0 for output %d", output);
        return INVALID_OPERATION;
    }
}


void AudioPolicyManager::setOutputDevice(audio_io_handle_t output, uint32_t device, bool force, int delayMs)
{
    LOGV("setOutputDevice() output %d device %x delayMs %d", output, device, delayMs);
    AudioOutputDescriptor *outputDesc = mOutputs.valueFor(output);


    if (outputDesc->isDuplicated()) {
        setOutputDevice(outputDesc->mOutput1->mId, device, force, delayMs);
        setOutputDevice(outputDesc->mOutput2->mId, device, force, delayMs);
        return;
    }
#ifdef WITH_A2DP
    // filter devices according to output selected
    if (output == mA2dpOutput) {
        device &= AudioSystem::DEVICE_OUT_ALL_A2DP;
    } else {
        device &= ~AudioSystem::DEVICE_OUT_ALL_A2DP;
    }
#endif

    uint32_t prevDevice = (uint32_t)outputDesc->device();
    // Do not change the routing if:
    //  - the requestede device is 0
    //  - the requested device is the same as current device and force is not specified.
    // Doing this check here allows the caller to call setOutputDevice() without conditions
    if ((device == 0 || device == prevDevice) && !force) {
        LOGV("setOutputDevice() setting same device %x or null device for output %d", device, output);
        return;
    }

    outputDesc->mDevice = device;
    // mute media streams if both speaker and headset are selected
    if (device == (AudioSystem::DEVICE_OUT_SPEAKER | AudioSystem::DEVICE_OUT_WIRED_HEADSET) ||
        device == (AudioSystem::DEVICE_OUT_SPEAKER | AudioSystem::DEVICE_OUT_WIRED_HEADPHONE)
#ifdef FM_RADIO
        device == (AudioSystem::DEVICE_OUT_SPEAKER | AudioSystem::DEVICE_OUT_WIRED_HEADSET | AudioSystem::DEVICE_OUT_FM) ||
        device == (AudioSystem::DEVICE_OUT_SPEAKER | AudioSystem::DEVICE_OUT_WIRED_HEADSET | AudioSystem::DEVICE_OUT_FM) ||
        device == (AudioSystem::DEVICE_OUT_SPEAKER | AudioSystem::DEVICE_OUT_FM_TX)
#endif
	){
        setStrategyMute(STRATEGY_MEDIA, true, output);
        // wait for the PCM output buffers to empty before proceeding with the rest of the command
        usleep(outputDesc->mLatency*2*1000);
    }

    // do the routing
    AudioParameter param = AudioParameter();
    param.addInt(String8(AudioParameter::keyRouting), (int)device);
    mpClientInterface->setParameters(mHardwareOutput, param.toString(), delayMs);
    // update stream volumes according to new device
    applyStreamVolumes(output, device, delayMs);

    // if changing from a combined headset + speaker route, unmute media streams
    if (prevDevice == (AudioSystem::DEVICE_OUT_SPEAKER | AudioSystem::DEVICE_OUT_WIRED_HEADSET) ||
        prevDevice == (AudioSystem::DEVICE_OUT_SPEAKER | AudioSystem::DEVICE_OUT_WIRED_HEADPHONE)
#ifdef FM_RADIO
        prevDevice == (AudioSystem::DEVICE_OUT_SPEAKER | AudioSystem::DEVICE_OUT_WIRED_HEADSET | AudioSystem::DEVICE_OUT_FM) ||
        prevDevice == (AudioSystem::DEVICE_OUT_SPEAKER | AudioSystem::DEVICE_OUT_WIRED_HEADSET | AudioSystem::DEVICE_OUT_FM) ||
        prevDevice == (AudioSystem::DEVICE_OUT_SPEAKER | AudioSystem::DEVICE_OUT_FM_TX)
#endif
	){
        setStrategyMute(STRATEGY_MEDIA, false, output, delayMs);
    }
}

status_t AudioPolicyManager::checkAndSetVolume(int stream, int index, audio_io_handle_t output, uint32_t device, int delayMs, bool force)
{

    // do not change actual stream volume if the stream is muted
    if (mOutputs.valueFor(output)->mMuteCount[stream] != 0) {
        LOGV("checkAndSetVolume() stream %d muted count %d", stream, mOutputs.valueFor(output)->mMuteCount[stream]);
        return NO_ERROR;
    }

    // do not change in call volume if bluetooth is connected and vice versa
    if ((stream == AudioSystem::VOICE_CALL && mForceUse[AudioSystem::FOR_COMMUNICATION] == AudioSystem::FORCE_BT_SCO) ||
        (stream == AudioSystem::BLUETOOTH_SCO && mForceUse[AudioSystem::FOR_COMMUNICATION] != AudioSystem::FORCE_BT_SCO)) {
        LOGV("checkAndSetVolume() cannot set stream %d volume with force use = %d for comm",
             stream, mForceUse[AudioSystem::FOR_COMMUNICATION]);
        return INVALID_OPERATION;
    }

    float volume = computeVolume(stream, index, output, device);
    // We actually change the volume if:
    // - the float value returned by computeVolume() changed
    // - the force flag is set
    if (volume != mOutputs.valueFor(output)->mCurVolume[stream] ||
        (stream == AudioSystem::VOICE_CALL) ||
#ifdef FM_RADIO
	(stream == AudioSystem::FM) ||
#endif
	force) {
        mOutputs.valueFor(output)->mCurVolume[stream] = volume;
        LOGV("setStreamVolume() for output %d stream %d, volume %f, delay %d", output, stream, volume, delayMs);
        if (stream == AudioSystem::VOICE_CALL ||
            stream == AudioSystem::DTMF ||
            stream == AudioSystem::BLUETOOTH_SCO) {
            // offset value to reflect actual hardware volume that never reaches 0
            // 1% corresponds roughly to first step in VOICE_CALL stream volume setting (see AudioService.java)
            volume = 0.01 + 0.99 * volume;
        }
        mpClientInterface->setStreamVolume((AudioSystem::stream_type)stream, volume, output, delayMs);
    }

    if (stream == AudioSystem::VOICE_CALL ||
        stream == AudioSystem::BLUETOOTH_SCO) {
        float voiceVolume;
        // Force voice volume to max for bluetooth SCO as volume is managed by the headset
        if (stream == AudioSystem::VOICE_CALL) {
            voiceVolume = (float)index/(float)mStreams[stream].mIndexMax;
        } else {
            String8 key ("bt_headset_vgs");
            mpClientInterface->getParameters(output,key);
            AudioParameter result(mpClientInterface->getParameters(0,key));
            int value;
            if(result.getInt(String8("isVGS"),value) == NO_ERROR){
               LOGD("BT-SCO Voice Volume %f",(float)index/(float)mStreams[stream].mIndexMax);
               voiceVolume = 1.0;
             } else {
               voiceVolume = (float)index/(float)mStreams[stream].mIndexMax;
             }
        }
        if ((voiceVolume >= 0 && output == mHardwareOutput)
#ifdef FM_RADIO
&&
            (!(mAvailableOutputDevices & AudioSystem::DEVICE_OUT_FM))
#endif
        ) {
            mpClientInterface->setVoiceVolume(voiceVolume, delayMs);
            mLastVoiceVolume = voiceVolume;
        }
      }
#ifdef FM_RADIO
    else if ((stream == AudioSystem::FM) && (mAvailableOutputDevices & AudioSystem::DEVICE_OUT_FM)) {
        float fmVolume = -1.0;
        fmVolume = (float)index/(float)mStreams[stream].mIndexMax;
        if (fmVolume >= 0 && output == mHardwareOutput) {
            mpClientInterface->setFmVolume(fmVolume, delayMs);
        }
   }
#endif
    return NO_ERROR;

}
}; // namespace android
