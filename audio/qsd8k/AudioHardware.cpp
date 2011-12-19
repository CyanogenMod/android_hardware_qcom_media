/*
** Copyright 2008, The Android Open-Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#include <math.h>

//#define LOG_NDEBUG 0
#define LOG_TAG "AudioHardwareQSD"
#include <utils/Log.h>
#include <utils/String8.h>
#include <hardware_legacy/power.h>

#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include <fcntl.h>

#include <cutils/properties.h> // for property_get for the voice recognition mode switch

// hardware specific functions

#include "AudioHardware.h"
#include <media/AudioRecord.h>
#include <media/mediarecorder.h>

extern "C" {
#include "msm_audio.h"
#include <linux/a1026.h>
#include <linux/tpa2018d1.h>
}

#define LOG_SND_RPC 0  // Set to 1 to log sound RPC's
#define TX_PATH (1)

static const uint32_t SND_DEVICE_CURRENT = 256;
static const uint32_t SND_DEVICE_HANDSET = 0;
static const uint32_t SND_DEVICE_SPEAKER = 1;
static const uint32_t SND_DEVICE_BT = 3;
static const uint32_t SND_DEVICE_CARKIT = 4;
static const uint32_t SND_DEVICE_BT_EC_OFF = 45;
static const uint32_t SND_DEVICE_HEADSET = 2;
static const uint32_t SND_DEVICE_HEADSET_AND_SPEAKER = 10;
static const uint32_t SND_DEVICE_FM_HEADSET = 9;
static const uint32_t SND_DEVICE_FM_SPEAKER = 11;
static const uint32_t SND_DEVICE_NO_MIC_HEADSET = 8;
static const uint32_t SND_DEVICE_TTY_FULL = 5;
static const uint32_t SND_DEVICE_TTY_VCO = 6;
static const uint32_t SND_DEVICE_TTY_HCO = 7;
static const uint32_t SND_DEVICE_HANDSET_BACK_MIC = 20;
static const uint32_t SND_DEVICE_SPEAKER_BACK_MIC = 21;
static const uint32_t SND_DEVICE_NO_MIC_HEADSET_BACK_MIC = 28;
static const uint32_t SND_DEVICE_HEADSET_AND_SPEAKER_BACK_MIC = 30;
namespace android_audio_legacy {
static int support_a1026 = 1;
static bool support_tpa2018d1 = true;
static int fd_a1026 = -1;
static int old_pathid = -1;
static int new_pathid = -1;
static int curr_out_device = -1;
static int curr_mic_device = -1;
static int voice_started = 0;
static int fd_fm_device = -1;
static int stream_volume = -300;
// use VR mode on inputs: 1 == VR mode enabled when selected, 0 = VR mode disabled when selected
static int vr_mode_enabled;
static bool vr_mode_change = false;
static int vr_uses_ns = 0;
static int alt_enable = 0;
static int hac_enable = 0;
// enable or disable 2-mic noise suppression in call on receiver mode
static int enable1026 = 1;
//FIXME add new settings in A1026 driver for an incall no ns mode, based on the current vr no ns
#define A1026_PATH_INCALL_NO_NS_RECEIVER A1026_PATH_VR_NO_NS_RECEIVER

int errCount = 0;
static void * acoustic;
const uint32_t AudioHardware::inputSamplingRates[] = {
        8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000
};

// ID string for audio wakelock
static const char kOutputWakelockStr[] = "AudioHardwareQSDOut";
static const char kInputWakelockStr[] = "AudioHardwareQSDIn";

// ----------------------------------------------------------------------------

AudioHardware::AudioHardware() :
    mA1026Init(false), mInit(false), mMicMute(true),
    mBluetoothNrec(true),
    mHACSetting(false),
    mBluetoothIdTx(0), mBluetoothIdRx(0),
    mOutput(0),
    mNoiseSuppressionState(A1026_NS_STATE_AUTO),
    mVoiceVolume(VOICE_VOLUME_MAX), mTTYMode(TTY_MODE_OFF)
{
    int (*snd_get_num)();
    int (*snd_get_bt_endpoint)(msm_bt_endpoint *);
    int (*set_acoustic_parameters)();
    int (*set_tpa2018d1_parameters)();

    struct msm_bt_endpoint *ept;

    doA1026_init();

    acoustic =:: dlopen("/system/lib/libhtc_acoustic.so", RTLD_NOW);
    if (acoustic == NULL ) {
        LOGD("Could not open libhtc_acoustic.so");
        /* this is not really an error on non-htc devices... */
        mNumBTEndpoints = 0;
        mInit = true;
        return;
    }
    set_acoustic_parameters = (int (*)(void))::dlsym(acoustic, "set_acoustic_parameters");
    if ((*set_acoustic_parameters) == 0 ) {
        LOGE("Could not open set_acoustic_parameters()");
        return;
    }

    set_tpa2018d1_parameters = (int (*)(void))::dlsym(acoustic, "set_tpa2018d1_parameters");
    if ((*set_tpa2018d1_parameters) == 0) {
        LOGD("set_tpa2018d1_parameters() not present");
        support_tpa2018d1 = false;
    }

    int rc = set_acoustic_parameters();
    if (rc < 0) {
        LOGD("Could not set acoustic parameters to share memory: %d", rc);
    }

    if (support_tpa2018d1) {
       rc = set_tpa2018d1_parameters();
       if (rc < 0) {
           support_tpa2018d1 = false;
           LOGD("speaker amplifier tpa2018 is not supported\n");
       }
    }

    snd_get_num = (int (*)(void))::dlsym(acoustic, "snd_get_num");
    if ((*snd_get_num) == 0 ) {
        LOGD("Could not open snd_get_num()");
    }

    mNumBTEndpoints = snd_get_num();
    LOGV("mNumBTEndpoints = %d", mNumBTEndpoints);
    mBTEndpoints = new msm_bt_endpoint[mNumBTEndpoints];
    mInit = true;
    LOGV("constructed %d SND endpoints)", mNumBTEndpoints);
    ept = mBTEndpoints;
    snd_get_bt_endpoint = (int (*)(msm_bt_endpoint *))::dlsym(acoustic, "snd_get_bt_endpoint");
    if ((*snd_get_bt_endpoint) == 0 ) {
        LOGE("Could not open snd_get_bt_endpoint()");
        return;
    }
    snd_get_bt_endpoint(mBTEndpoints);

    for (int i = 0; i < mNumBTEndpoints; i++) {
        LOGV("BT name %s (tx,rx)=(%d,%d)", mBTEndpoints[i].name, mBTEndpoints[i].tx, mBTEndpoints[i].rx);
    }

    // reset voice mode in case media_server crashed and restarted while in call
    int fd = open("/dev/msm_audio_ctl", O_RDWR);
    if (fd >= 0) {
        ioctl(fd, AUDIO_STOP_VOICE, NULL);
        close(fd);
    }

    vr_mode_change = false;
    vr_mode_enabled = 0;
    enable1026 = 1;
    char value[PROPERTY_VALUE_MAX];
    // Check the system property to enable or not the special recording modes
    property_get("media.a1026.enableA1026", value, "1");
    enable1026 = atoi(value);
    LOGV("Enable mode selection for A1026 is %d", enable1026);
    // Check the system property for which VR mode to use
    property_get("media.a1026.nsForVoiceRec", value, "0");
    vr_uses_ns = atoi(value);
    LOGV("Using Noise Suppression for Voice Rec is %d", vr_uses_ns);

    // Check the system property for enable or not the ALT function
    property_get("htc.audio.alt.enable", value, "0");
    alt_enable = atoi(value);
    LOGV("Enable ALT function: %d", alt_enable);

    // Check the system property for enable or not the HAC function
    property_get("htc.audio.hac.enable", value, "0");
    hac_enable = atoi(value);
    LOGV("Enable HAC function: %d", hac_enable);

    mInit = true;
}

AudioHardware::~AudioHardware()
{
    for (size_t index = 0; index < mInputs.size(); index++) {
        closeInputStream((AudioStreamIn*)mInputs[index]);
    }
    mInputs.clear();
    closeOutputStream((AudioStreamOut*)mOutput);
    mInit = false;
}

status_t AudioHardware::initCheck()
{
    return mInit ? NO_ERROR : NO_INIT;
}

AudioStreamOut* AudioHardware::openOutputStream(
        uint32_t devices, int *format, uint32_t *channels, uint32_t *sampleRate, status_t *status)
{
    { // scope for the lock
        android::Mutex::Autolock lock(mLock);

        AudioStreamOutMSM72xx* out;
        if (mOutput) {
            // only one output stream allowed
            out = mOutput;
        } else {
            // create new output stream
            out = new AudioStreamOutMSM72xx();
        }

        status_t lStatus = out->set(this, devices, format, channels, sampleRate);
        if (status) {
            *status = lStatus;
        }
        if (lStatus == NO_ERROR) {
            mOutput = out;
        } else {
            delete out;
        }
    }
    return mOutput;
}

void AudioHardware::closeOutputStream(AudioStreamOut* out) {
    android::Mutex::Autolock lock(mLock);
    if (mOutput == 0 || mOutput != out) {
        LOGW("Attempt to close invalid output stream");
    }
    else {
        delete mOutput;
        mOutput = 0;
    }
}

AudioStreamIn* AudioHardware::openInputStream(
        uint32_t devices, int *format, uint32_t *channels, uint32_t *sampleRate, status_t *status,
        AudioSystem::audio_in_acoustics acoustic_flags)
{
    // check for valid input source
    if (!AudioSystem::isInputDevice((AudioSystem::audio_devices)devices)) {
        return 0;
    }

    mLock.lock();

    AudioStreamInMSM72xx* in = new AudioStreamInMSM72xx();
    status_t lStatus = in->set(this, devices, format, channels, sampleRate, acoustic_flags);
    if (status) {
        *status = lStatus;
    }
    if (lStatus != NO_ERROR) {
        mLock.unlock();
        delete in;
        return 0;
    }

    mInputs.add(in);
    mLock.unlock();

    return in;
}

void AudioHardware::closeInputStream(AudioStreamIn* in) {
    android::Mutex::Autolock lock(mLock);

    ssize_t index = mInputs.indexOf((AudioStreamInMSM72xx *)in);
    if (index < 0) {
        LOGW("Attempt to close invalid input stream");
    } else {
        mLock.unlock();
        delete mInputs[index];
        mLock.lock();
        mInputs.removeAt(index);
    }
}

status_t AudioHardware::setMode(int mode)
{
    // VR mode is never used in a call and must be cleared when entering the IN_CALL mode
    if (mode == AudioSystem::MODE_IN_CALL) {
        vr_mode_enabled = 0;
    }

    if (support_tpa2018d1)
        do_tpa2018_control(mode);

    int prevMode = mMode;
    status_t status = AudioHardwareBase::setMode(mode);
    if (status == NO_ERROR) {
        // make sure that doAudioRouteOrMute() is called by doRouting()
        // when entering or exiting in call mode even if the new device
        // selected is the same as current one.
        if (((prevMode != AudioSystem::MODE_IN_CALL) && (mMode == AudioSystem::MODE_IN_CALL)) ||
            ((prevMode == AudioSystem::MODE_IN_CALL) && (mMode != AudioSystem::MODE_IN_CALL))) {
            clearCurDevice();
        }
    }

    return status;
}

bool AudioHardware::checkOutputStandby()
{
    if (mOutput)
        if (!mOutput->checkStandby())
            return false;

    return true;
}
static status_t set_mic_mute(bool _mute)
{
    uint32_t mute = _mute;
    int fd = -1;
    fd = open("/dev/msm_audio_ctl", O_RDWR);
    if (fd < 0) {
        LOGE("Cannot open msm_audio_ctl device\n");
        return -1;
    }
    LOGD("Setting mic mute to %d\n", mute);
    if (ioctl(fd, AUDIO_SET_MUTE, &mute)) {
        LOGE("Cannot set mic mute on current device\n");
        close(fd);
        return -1;
    }
    close(fd);
    return NO_ERROR;
}

status_t AudioHardware::setMicMute(bool state)
{
    android::Mutex::Autolock lock(mLock);
    return setMicMute_nosync(state);
}

// always call with mutex held
status_t AudioHardware::setMicMute_nosync(bool state)
{
    if (mMicMute != state) {
        mMicMute = state;
        return set_mic_mute(mMicMute); //always set current TX device
    }
    return NO_ERROR;
}

status_t AudioHardware::getMicMute(bool* state)
{
    *state = mMicMute;
    return NO_ERROR;
}

status_t AudioHardware::setParameters(const String8& keyValuePairs)
{
    AudioParameter param = AudioParameter(keyValuePairs);
    String8 value;
    String8 key;
    const char BT_NREC_KEY[] = "bt_headset_nrec";
    const char BT_NAME_KEY[] = "bt_headset_name";
    const char HAC_KEY[] = "HACSetting";
    const char BT_NREC_VALUE_ON[] = "on";
    const char HAC_VALUE_ON[] = "ON";


    LOGV("setParameters() %s", keyValuePairs.string());

    if (keyValuePairs.length() == 0) return BAD_VALUE;

    if(hac_enable) {
        key = String8(HAC_KEY);
        if (param.get(key, value) == NO_ERROR) {
            if (value == HAC_VALUE_ON) {
                mHACSetting = true;
                LOGD("Enable HAC");
            } else {
                mHACSetting = false;
                LOGD("Disable HAC");
            }
        }
    }

    key = String8(BT_NREC_KEY);
    if (param.get(key, value) == NO_ERROR) {
        if (value == BT_NREC_VALUE_ON) {
            mBluetoothNrec = true;
        } else {
            mBluetoothNrec = false;
            LOGD("Turning noise reduction and echo cancellation off for BT "
                 "headset");
        }
    }
    key = String8(BT_NAME_KEY);
    if (param.get(key, value) == NO_ERROR) {
        mBluetoothIdTx = 0;
        mBluetoothIdRx = 0;
        for (int i = 0; i < mNumBTEndpoints; i++) {
            if (!strcasecmp(value.string(), mBTEndpoints[i].name)) {
                mBluetoothIdTx = mBTEndpoints[i].tx;
                mBluetoothIdRx = mBTEndpoints[i].rx;
                LOGD("Using custom acoustic parameters for %s", value.string());
                break;
            }
        }
        if (mBluetoothIdTx == 0) {
            LOGD("Using default acoustic parameters "
                 "(%s not in acoustic database)", value.string());
        }
        doRouting();
    }
    key = String8("noise_suppression");
    if (param.get(key, value) == NO_ERROR) {
        if (support_a1026 == 1) {
            int noiseSuppressionState;
            if (value == "off") {
                noiseSuppressionState = A1026_NS_STATE_OFF;
            } else if (value == "auto") {
                noiseSuppressionState = A1026_NS_STATE_AUTO;
            } else if (value == "far_talk") {
                noiseSuppressionState = A1026_NS_STATE_FT;
            } else if (value == "close_talk") {
                noiseSuppressionState = A1026_NS_STATE_CT;
            } else {
                return BAD_VALUE;
            }

            if (noiseSuppressionState != mNoiseSuppressionState) {
                if (!mA1026Init) {
                    LOGW("Audience A1026 not initialized.\n");
                    return INVALID_OPERATION;
                }

                mA1026Lock.lock();
                if (fd_a1026 < 0) {
                    fd_a1026 = open("/dev/audience_a1026", O_RDWR);
                    if (fd_a1026 < 0) {
                        LOGE("Cannot open audience_a1026 device (%d)\n", fd_a1026);
                        mA1026Lock.unlock();
                        return -1;
                    }
                }
                LOGV("Setting noise suppression %s", value.string());

                int rc = ioctl(fd_a1026, A1026_SET_NS_STATE, &noiseSuppressionState);
                if (!rc) {
                    mNoiseSuppressionState = noiseSuppressionState;
                } else {
                    LOGE("Failed to set noise suppression %s", value.string());
                }
                close(fd_a1026);
                fd_a1026 = -1;
                mA1026Lock.unlock();
            }
        } else {
            return INVALID_OPERATION;
        }
     }

    key = String8("tty_mode");
    if (param.get(key, value) == NO_ERROR) {
        int ttyMode;
        if (value == "tty_off") {
            ttyMode = TTY_MODE_OFF;
        } else if (value == "tty_full") {
            ttyMode = TTY_MODE_FULL;
        } else if (value == "tty_vco") {
            ttyMode = TTY_MODE_VCO;
        } else if (value == "tty_hco") {
            ttyMode = TTY_MODE_HCO;
        } else {
            return BAD_VALUE;
        }

        if (ttyMode != mTTYMode) {
            LOGV("new tty mode %d", ttyMode);
            mTTYMode = ttyMode;
            doRouting();
        }
     }

    return NO_ERROR;
}

String8 AudioHardware::getParameters(const String8& keys)
{
    AudioParameter request = AudioParameter(keys);
    AudioParameter reply = AudioParameter();
    String8 value;
    String8 key;

    LOGV("getParameters() %s", keys.string());

    key = "noise_suppression";
    if (request.get(key, value) == NO_ERROR) {
        switch(mNoiseSuppressionState) {
        case A1026_NS_STATE_OFF:
            value = "off";
            break;
        case A1026_NS_STATE_AUTO:
            value = "auto";
            break;
        case A1026_NS_STATE_FT:
            value = "far_talk";
            break;
        case A1026_NS_STATE_CT:
            value = "close_talk";
            break;
        }
        reply.add(key, value);
    }

    return reply.toString();
}


static unsigned calculate_audpre_table_index(unsigned index)
{
    switch (index) {
        case 48000:    return SAMP_RATE_INDX_48000;
        case 44100:    return SAMP_RATE_INDX_44100;
        case 32000:    return SAMP_RATE_INDX_32000;
        case 24000:    return SAMP_RATE_INDX_24000;
        case 22050:    return SAMP_RATE_INDX_22050;
        case 16000:    return SAMP_RATE_INDX_16000;
        case 12000:    return SAMP_RATE_INDX_12000;
        case 11025:    return SAMP_RATE_INDX_11025;
        case 8000:    return SAMP_RATE_INDX_8000;
        default:     return -1;
    }
}

size_t AudioHardware::getBufferSize(uint32_t sampleRate, int channelCount)
{
    size_t bufSize;

    if (sampleRate < 11025) {
        bufSize = 256;
    } else if (sampleRate < 22050) {
        bufSize = 512;
    } else if (sampleRate < 32000) {
        bufSize = 768;
    } else if (sampleRate < 44100) {
        bufSize = 1024;
    } else {
        bufSize = 1536;
    }

    return bufSize*channelCount;
}


size_t AudioHardware::getInputBufferSize(uint32_t sampleRate, int format, int channelCount)
{
    if (format != AudioSystem::PCM_16_BIT) {
        LOGW("getInputBufferSize bad format: %d", format);
        return 0;
    }
    if (channelCount < 1 || channelCount > 2) {
        LOGW("getInputBufferSize bad channel count: %d", channelCount);
        return 0;
    }
    if (sampleRate < 8000 || sampleRate > 48000) {
        LOGW("getInputBufferSize bad sample rate: %d", sampleRate);
        return 0;
    }

    return getBufferSize(sampleRate, channelCount);
}

static status_t set_volume_rpc(uint32_t volume)
{
    int fd = -1;
    fd = open("/dev/msm_audio_ctl", O_RDWR);
    if (fd < 0) {
        LOGE("Cannot open msm_audio_ctl device\n");
        return -1;
    }
    volume *= 20; //percentage
    LOGD("Setting in-call volume to %d\n", volume);
    if (ioctl(fd, AUDIO_SET_VOLUME, &volume)) {
        LOGW("Cannot set volume on current device\n");
    }
    close(fd);
    return NO_ERROR;
}

status_t AudioHardware::setVoiceVolume(float v)
{
    if (v < 0.0) {
        LOGW("setVoiceVolume(%f) under 0.0, assuming 0.0", v);
        v = 0.0;
    } else if (v > 1.0) {
        LOGW("setVoiceVolume(%f) over 1.0, assuming 1.0", v);
        v = 1.0;
    }

    int vol = lrint(v * VOICE_VOLUME_MAX);

    android::Mutex::Autolock lock(mLock);
    if (mHACSetting && hac_enable && mCurSndDevice == (int) SND_DEVICE_HANDSET) {
        LOGD("HAC enable: Setting in-call volume to maximum.\n");
        set_volume_rpc(VOICE_VOLUME_MAX);
    } else {
        LOGI("voice volume %d (range is 0 to %d)", vol, VOICE_VOLUME_MAX);
        set_volume_rpc(vol); //always set current device
    }
    mVoiceVolume = vol;
    return NO_ERROR;
}

status_t AudioHardware::setMasterVolume(float v)
{
    LOGI("Set master volume to %f", v);
    // We return an error code here to let the audioflinger do in-software
    // volume on top of the maximum volume that we set through the SND API.
    // return error - software mixer will handle it
    return -1;
}

static status_t do_route_audio_dev_ctrl(uint32_t device, bool inCall, uint32_t rx_acdb_id, uint32_t tx_acdb_id)
{
    uint32_t out_device = 0, mic_device = 0;
    uint32_t path[2];
    int fd = 0;

    if (device == SND_DEVICE_CURRENT)
        goto Incall;

    // hack -- kernel needs to put these in include file
    LOGD("Switching audio device to ");
    if (device == SND_DEVICE_HANDSET) {
           out_device = HANDSET_SPKR;
           mic_device = HANDSET_MIC;
           LOGD("Handset");
    } else if ((device  == SND_DEVICE_BT) || (device == SND_DEVICE_BT_EC_OFF)) {
           out_device = BT_SCO_SPKR;
           mic_device = BT_SCO_MIC;
           LOGD("BT Headset");
    } else if (device == SND_DEVICE_SPEAKER ||
               device == SND_DEVICE_SPEAKER_BACK_MIC) {
           out_device = SPKR_PHONE_MONO;
           mic_device = SPKR_PHONE_MIC;
           LOGD("Speakerphone");
    } else if (device == SND_DEVICE_HEADSET) {
           out_device = HEADSET_SPKR_STEREO;
           mic_device = HEADSET_MIC;
           LOGD("Stereo Headset");
    } else if (device == SND_DEVICE_HEADSET_AND_SPEAKER) {
           out_device = SPKR_PHONE_HEADSET_STEREO;
           mic_device = HEADSET_MIC;
           LOGD("Stereo Headset + Speaker");
    } else if (device == SND_DEVICE_HEADSET_AND_SPEAKER_BACK_MIC) {
           out_device = SPKR_PHONE_HEADSET_STEREO;
           mic_device = SPKR_PHONE_MIC;
           LOGD("Stereo Headset + Speaker and back mic");
    } else if (device == SND_DEVICE_NO_MIC_HEADSET) {
           out_device = HEADSET_SPKR_STEREO;
           mic_device = HANDSET_MIC;
           LOGD("No microphone Wired Headset");
    } else if (device == SND_DEVICE_NO_MIC_HEADSET_BACK_MIC) {
           out_device = HEADSET_SPKR_STEREO;
           mic_device = SPKR_PHONE_MIC;
           LOGD("No microphone Wired Headset and back mic");
    } else if (device == SND_DEVICE_HANDSET_BACK_MIC) {
           out_device = HANDSET_SPKR;
           mic_device = SPKR_PHONE_MIC;
           LOGD("Handset and back mic");
    } else if (device == SND_DEVICE_FM_HEADSET) {
           out_device = FM_HEADSET;
           mic_device = HEADSET_MIC;
           LOGD("Stereo FM headset");
    } else if (device == SND_DEVICE_FM_SPEAKER) {
           out_device = FM_SPKR;
           mic_device = HEADSET_MIC;
           LOGD("Stereo FM speaker");
    } else if (device == SND_DEVICE_CARKIT) {
           out_device = BT_SCO_SPKR;
           mic_device = BT_SCO_MIC;
           LOGD("Carkit");
    } else if (device == SND_DEVICE_TTY_FULL) {
        out_device = TTY_HEADSET_SPKR;
        mic_device = TTY_HEADSET_MIC;
        LOGD("TTY FULL headset");
    } else if (device == SND_DEVICE_TTY_VCO) {
        out_device = TTY_HEADSET_SPKR;
        mic_device = SPKR_PHONE_MIC;
        LOGD("TTY VCO headset");
    } else if (device == SND_DEVICE_TTY_HCO) {
        out_device = SPKR_PHONE_MONO;
        mic_device = TTY_HEADSET_MIC;
        LOGD("TTY HCO headset");
    } else {
        LOGE("unknown device %d", device);
        return -1;
    }

#if 0 //Add for FM support
    if (out_device == FM_HEADSET ||
        out_device == FM_SPKR) {
        if (fd_fm_device < 0) {
            fd_fm_device = open("/dev/msm_htc_fm", O_RDWR);
            if (fd_fm_device < 0) {
                LOGE("Cannot open msm_htc_fm device");
                return -1;
            }
            LOGD("Opened msm_htc_fm for FM radio");
        }
    } else if (fd_fm_device >= 0) {
        close(fd_fm_device);
        fd_fm_device = -1;
        LOGD("Closed msm_htc_fm after FM radio");
    }
#endif

    fd = open("/dev/msm_audio_ctl", O_RDWR);
    if (fd < 0)        {
       LOGE("Cannot open msm_audio_ctl");
       return -1;
    }
    path[0] = out_device;
    path[1] = rx_acdb_id;
    if (ioctl(fd, AUDIO_SWITCH_DEVICE, &path)) {
       LOGE("Cannot switch audio device");
       close(fd);
       return -1;
    }
    path[0] = mic_device;
    path[1] = tx_acdb_id;
    if (ioctl(fd, AUDIO_SWITCH_DEVICE, &path)) {
       LOGE("Cannot switch mic device");
       close(fd);
       return -1;
    }
    curr_out_device = out_device;
    curr_mic_device = mic_device;

Incall:
    if (inCall == true && !voice_started) {
        if (fd < 0) {
            fd = open("/dev/msm_audio_ctl", O_RDWR);

            if (fd < 0) {
                LOGE("Cannot open msm_audio_ctl");
                return -1;
            }
        }
        path[0] = rx_acdb_id;
        path[1] = tx_acdb_id;
        if (ioctl(fd, AUDIO_START_VOICE, &path)) {
            LOGE("Cannot start voice");
            close(fd);
            return -1;
        }
        LOGD("Voice Started!!");
        voice_started = 1;
    }
    else if (inCall == false && voice_started) {
        if (fd < 0) {
            fd = open("/dev/msm_audio_ctl", O_RDWR);

            if (fd < 0) {
                LOGE("Cannot open msm_audio_ctl");
                return -1;
            }
        }
        if (ioctl(fd, AUDIO_STOP_VOICE, NULL)) {
               LOGE("Cannot stop voice");
               close(fd);
               return -1;
        }
        LOGD("Voice Stopped!!");
        voice_started = 0;
    }

    close(fd);
    return NO_ERROR;
}


// always call with mutex held
status_t AudioHardware::doAudioRouteOrMute(uint32_t device)
{
    uint32_t rx_acdb_id = 0;
    uint32_t tx_acdb_id = 0;

    if (support_a1026 == 1)
            doAudience_A1026_Control(mMode, mRecordState, device);

    if (device == (uint32_t)SND_DEVICE_BT) {
        if (!mBluetoothNrec) {
            device = SND_DEVICE_BT_EC_OFF;
        }
    }


    if (device == (int) SND_DEVICE_BT) {
        if (mBluetoothIdTx != 0) {
            rx_acdb_id = mBluetoothIdRx;
            tx_acdb_id = mBluetoothIdTx;
        } else {
            /* use default BT entry defined in AudioBTID.csv */
            rx_acdb_id = mBTEndpoints[0].rx;
            tx_acdb_id = mBTEndpoints[0].tx;
            LOGD("Update ACDB ID to default BT setting\n");
        }
    }  else if (device == (int) SND_DEVICE_CARKIT
                || device == (int) SND_DEVICE_BT_EC_OFF) {
        if (mBluetoothIdTx != 0) {
            rx_acdb_id = mBluetoothIdRx;
            tx_acdb_id = mBluetoothIdTx;
        } else {
            /* use default carkit entry defined in AudioBTID.csv */
            rx_acdb_id = mBTEndpoints[1].rx;
            tx_acdb_id = mBTEndpoints[1].tx;
            LOGD("Update ACDB ID to default carkit setting");
        }
    } else if (mMode == AudioSystem::MODE_IN_CALL
               && hac_enable && mHACSetting &&
               device == (int) SND_DEVICE_HANDSET) {
        LOGD("Update acdb id to hac profile.");
        rx_acdb_id = ACDB_ID_HAC_HANDSET_SPKR;
        tx_acdb_id = ACDB_ID_HAC_HANDSET_MIC;
    } else {
        if (!checkOutputStandby() || mMode != AudioSystem::MODE_IN_CALL)
            rx_acdb_id = getACDB(MOD_PLAY, device);
        if (mRecordState)
            tx_acdb_id = getACDB(MOD_REC, device);
    }
    LOGV("doAudioRouteOrMute: rx acdb %d, tx acdb %d\n", rx_acdb_id, tx_acdb_id);

    return do_route_audio_dev_ctrl(device, mMode == AudioSystem::MODE_IN_CALL, rx_acdb_id, tx_acdb_id);
}

status_t AudioHardware::get_mMode(void)
{
    return mMode;
}

status_t AudioHardware::get_mRoutes(void)
{
    return mRoutes[mMode];
}

status_t AudioHardware::set_mRecordState(bool onoff)
{
    mRecordState = onoff;
    return 0;
}

status_t AudioHardware::get_batt_temp(int *batt_temp)
{
    int fd, len;
    const char *fn =
            "/sys/devices/platform/ds2784-battery/power_supply/battery/temp";
    char get_batt_temp[6] = { 0 };

    if ((fd = open(fn, O_RDONLY)) < 0) {
        LOGE("%s: cannot open %s: %s\n", __FUNCTION__, fn, strerror(errno));
        return UNKNOWN_ERROR;
    }

    if ((len = read(fd, get_batt_temp, sizeof(get_batt_temp))) <= 1) {
        LOGE("read battery temp fail: %s\n", strerror(errno));
        close(fd);
        return BAD_VALUE;
    }

    *batt_temp = strtol(get_batt_temp, NULL, 10);
    close(fd);
    return NO_ERROR;
}

/*
 * Note: upon exiting doA1026_init(), fd_a1026 will be -1
 */
status_t AudioHardware::doA1026_init(void)
{
    struct a1026img fwimg;
    char char_tmp = 0;
    unsigned char local_vpimg_buf[A1026_MAX_FW_SIZE], *ptr = local_vpimg_buf;
    int rc = 0, fw_fd = -1;
    ssize_t nr;
    size_t remaining;
    struct stat fw_stat;

    static const char *const fn = "/system/etc/vpimg";
    static const char *const path = "/dev/audience_a1026";

    if (fd_a1026 < 0)
        fd_a1026 = open(path, O_RDWR | O_NONBLOCK, 0);

    if (fd_a1026 < 0) {
        LOGE("Cannot open %s %d\n", path, fd_a1026);
        support_a1026 = 0;
        goto open_drv_err;
    }

    fw_fd = open(fn, O_RDONLY);
    if (fw_fd < 0) {
        LOGE("Fail to open %s\n", fn);
        goto ld_img_error;
    } else {
        LOGD("open %s success\n", fn);
    }

    rc = fstat(fw_fd, &fw_stat);
    if (rc < 0) {
        LOGE("Cannot stat file %s: %s\n", fn, strerror(errno));
        goto ld_img_error;
    }

    remaining = (int)fw_stat.st_size;

    LOGD("Firmware %s size %d\n", fn, remaining);

    if (remaining > sizeof(local_vpimg_buf)) {
        LOGE("File %s size %d exceeds internal limit %d\n",
             fn, remaining, sizeof(local_vpimg_buf));
        goto ld_img_error;
    }

    while (remaining) {
        nr = read(fw_fd, ptr, remaining);
        if (nr < 0) {
            LOGE("Error reading firmware: %s\n", strerror(errno));
            goto ld_img_error;
        }
        else if (!nr) {
            if (remaining)
                LOGW("EOF reading firmware %s while %d bytes remain\n",
                     fn, remaining);
            break;
        }
        remaining -= nr;
        ptr += nr;
    }

    close (fw_fd);
    fw_fd = -1;

    fwimg.buf = local_vpimg_buf;
    fwimg.img_size = (int)(fw_stat.st_size - remaining);
    LOGD("Total %d bytes put to user space buffer.\n", fwimg.img_size);

    rc = ioctl(fd_a1026, A1026_BOOTUP_INIT, &fwimg);
    if (!rc) {
        LOGD("audience_a1026 init OK\n");
        mA1026Init = 1;
    } else
        LOGE("audience_a1026 init failed\n");

ld_img_error:
    if (fw_fd >= 0)
        close(fw_fd);
    close(fd_a1026);
open_drv_err:
    fd_a1026 = -1;
    return rc;
}

status_t AudioHardware::get_snd_dev(void)
{
    android::Mutex::Autolock lock(mLock);
    return mCurSndDevice;
}

uint32_t AudioHardware::getACDB(int mode, int device)
{
    uint32_t acdb_id = 0;
    int batt_temp = 0;
    if (mMode == AudioSystem::MODE_IN_CALL) {
        LOGD("skip update ACDB due to in-call");
        return 0;
    }

    if (mode == MOD_PLAY) {
        switch (device) {
            case SND_DEVICE_HEADSET:
            case SND_DEVICE_NO_MIC_HEADSET:
            case SND_DEVICE_NO_MIC_HEADSET_BACK_MIC:
            case SND_DEVICE_FM_HEADSET:
                acdb_id = ACDB_ID_HEADSET_PLAYBACK;
                break;
            case SND_DEVICE_SPEAKER:
            case SND_DEVICE_FM_SPEAKER:
            case SND_DEVICE_SPEAKER_BACK_MIC:
                acdb_id = ACDB_ID_SPKR_PLAYBACK;
                if(alt_enable) {
                    LOGD("Enable ALT for speaker\n");
                    if (get_batt_temp(&batt_temp) == NO_ERROR) {
                        if (batt_temp < 50)
                            acdb_id = ACDB_ID_ALT_SPKR_PLAYBACK;
                        LOGD("ALT batt temp = %d\n", batt_temp);
                    }
                }
                break;
            case SND_DEVICE_HEADSET_AND_SPEAKER:
            case SND_DEVICE_HEADSET_AND_SPEAKER_BACK_MIC:
                acdb_id = ACDB_ID_HEADSET_RINGTONE_PLAYBACK;
                break;
            default:
                break;
        }
    } else if (mode == MOD_REC) {
        switch (device) {
            case SND_DEVICE_HEADSET:
            case SND_DEVICE_FM_HEADSET:
            case SND_DEVICE_FM_SPEAKER:
            case SND_DEVICE_HEADSET_AND_SPEAKER:
                acdb_id = ACDB_ID_EXT_MIC_REC;
                break;
            case SND_DEVICE_HANDSET:
            case SND_DEVICE_NO_MIC_HEADSET:
            case SND_DEVICE_SPEAKER:
                if (vr_mode_enabled == 0) {
                    acdb_id = ACDB_ID_INT_MIC_REC;
                } else {
                    acdb_id = ACDB_ID_INT_MIC_VR;
                }
                break;
            case SND_DEVICE_SPEAKER_BACK_MIC:
            case SND_DEVICE_NO_MIC_HEADSET_BACK_MIC:
            case SND_DEVICE_HANDSET_BACK_MIC:
            case SND_DEVICE_HEADSET_AND_SPEAKER_BACK_MIC:
                acdb_id = ACDB_ID_CAMCORDER;
                break;
            default:
                break;
        }
    }
    LOGV("getACDB, return ID %d\n", acdb_id);
    return acdb_id;
}

status_t AudioHardware::do_tpa2018_control(int mode)
{
    if (curr_out_device == HANDSET_SPKR ||
        curr_out_device == SPKR_PHONE_MONO ||
        curr_out_device == HEADSET_SPKR_STEREO ||
        curr_out_device == SPKR_PHONE_HEADSET_STEREO ||
        curr_out_device == FM_SPKR) {

	int fd, rc;
        int retry = 3;

        switch (mode) {
        case AudioSystem::MODE_NORMAL:
            mode = TPA2018_MODE_PLAYBACK;
            break;
        case AudioSystem::MODE_RINGTONE:
            mode = TPA2018_MODE_RINGTONE;
            break;
        case AudioSystem::MODE_IN_CALL:
            mode = TPA2018_MODE_VOICE_CALL;
            break;
        default:
            return 0;
        }

        fd = open("/dev/tpa2018d1", O_RDWR);
        if (fd < 0) {
            LOGE("can't open /dev/tpa2018d1 %d", fd);
            return -1;
        }

        do {
            rc = ioctl(fd, TPA2018_SET_MODE, &mode);
            if (!rc)
                break;
        } while (--retry);

        if (rc < 0) {
            LOGE("ioctl TPA2018_SET_MODE failed: %s", strerror(errno));
        } else
            LOGD("Update TPA2018_SET_MODE to mode %d success", mode);

        close(fd);
    }
    return 0;
}

status_t AudioHardware::doAudience_A1026_Control(int Mode, bool Record, uint32_t Routes)
{
    int rc = 0;
    int retry = 4;

    if (!mA1026Init) {
        LOGW("Audience A1026 not initialized.\n");
        return NO_INIT;
    }

    mA1026Lock.lock();
    if (fd_a1026 < 0) {
        fd_a1026 = open("/dev/audience_a1026", O_RDWR);
        if (fd_a1026 < 0) {
            LOGE("Cannot open audience_a1026 device (%d)\n", fd_a1026);
            mA1026Lock.unlock();
            return -1;
        }
    }

    if ((Mode < AudioSystem::MODE_CURRENT) || (Mode >= AudioSystem::NUM_MODES)) {
        LOGW("Illegal value: doAudience_A1026_Control(%d, %u, %u)", Mode, Record, Routes);
        mA1026Lock.unlock();
        return BAD_VALUE;
    }

    if (Mode == AudioSystem::MODE_IN_CALL) {
        if (Record == 1) {
	    switch (Routes) {
	        case SND_DEVICE_HANDSET:
	        case SND_DEVICE_NO_MIC_HEADSET:
	            //TODO: what do we do for camcorder when in call?
	        case SND_DEVICE_NO_MIC_HEADSET_BACK_MIC:
	        case SND_DEVICE_HANDSET_BACK_MIC:
	        case SND_DEVICE_TTY_VCO:
	            if (enable1026) {
                    new_pathid = A1026_PATH_INCALL_RECEIVER;
                    LOGV("A1026 control: new path is A1026_PATH_INCALL_RECEIVER");
	            } else {
	                new_pathid = A1026_PATH_INCALL_NO_NS_RECEIVER;
	                LOGV("A1026 control: new path is A1026_PATH_INCALL_NO_NS_RECEIVER");
	            }
	            break;
	        case SND_DEVICE_HEADSET:
	        case SND_DEVICE_HEADSET_AND_SPEAKER:
	        case SND_DEVICE_FM_HEADSET:
	        case SND_DEVICE_FM_SPEAKER:
	        case SND_DEVICE_HEADSET_AND_SPEAKER_BACK_MIC:
	            new_pathid = A1026_PATH_INCALL_HEADSET;
	            LOGV("A1026 control: new path is A1026_PATH_INCALL_HEADSET");
	            break;
	        case SND_DEVICE_SPEAKER:
	            //TODO: what do we do for camcorder when in call?
	        case SND_DEVICE_SPEAKER_BACK_MIC:
	            new_pathid = A1026_PATH_INCALL_SPEAKER;
	            LOGV("A1026 control: new path is A1026_PATH_INCALL_SPEAKER");
	            break;
	        case SND_DEVICE_BT:
	        case SND_DEVICE_BT_EC_OFF:
	        case SND_DEVICE_CARKIT:
	            new_pathid = A1026_PATH_INCALL_BT;
	            LOGV("A1026 control: new path is A1026_PATH_INCALL_BT");
	            break;
	        case SND_DEVICE_TTY_HCO:
	        case SND_DEVICE_TTY_FULL:
	            new_pathid = A1026_PATH_INCALL_TTY;
	            LOGV("A1026 control: new path is A1026_PATH_INCALL_TTY");
	            break;
	        default:
	            break;
            }
       } else {
           switch (Routes) {
	        case SND_DEVICE_HANDSET:
	        case SND_DEVICE_NO_MIC_HEADSET:
	        case SND_DEVICE_TTY_VCO:
	            if (enable1026) {
	                new_pathid = A1026_PATH_INCALL_RECEIVER; /* NS CT mode, Dual MIC */
                    LOGV("A1026 control: new path is A1026_PATH_INCALL_RECEIVER");
	            } else {
	                new_pathid = A1026_PATH_INCALL_NO_NS_RECEIVER;
	                LOGV("A1026 control: new path is A1026_PATH_INCALL_NO_NS_RECEIVER");
	            }
	            break;
	        case SND_DEVICE_HEADSET:
	        case SND_DEVICE_HEADSET_AND_SPEAKER:
	        case SND_DEVICE_FM_HEADSET:
	        case SND_DEVICE_FM_SPEAKER:
	            new_pathid = A1026_PATH_INCALL_HEADSET; /* NS disable, Headset MIC */
	            LOGV("A1026 control: new path is A1026_PATH_INCALL_HEADSET");
	            break;
	        case SND_DEVICE_SPEAKER:
	            new_pathid = A1026_PATH_INCALL_SPEAKER; /* NS FT mode, Main MIC */
	            LOGV("A1026 control: new path is A1026_PATH_INCALL_SPEAKER");
	            break;
	        case SND_DEVICE_BT:
	        case SND_DEVICE_BT_EC_OFF:
	        case SND_DEVICE_CARKIT:
	            new_pathid = A1026_PATH_INCALL_BT; /* QCOM NS, BT MIC */
	            LOGV("A1026 control: new path is A1026_PATH_INCALL_BT");
	            break;
	        case SND_DEVICE_TTY_HCO:
	        case SND_DEVICE_TTY_FULL:
	            new_pathid = A1026_PATH_INCALL_TTY;
	            LOGV("A1026 control: new path is A1026_PATH_INCALL_TTY");
	            break;
	        default:
	            break;
            }
       }
    } else if (Record == 1) {
        switch (Routes) {
        case SND_DEVICE_SPEAKER:
            // default output is speaker, recording from phone mic, user RECEIVER configuration
        case SND_DEVICE_HANDSET:
        case SND_DEVICE_NO_MIC_HEADSET:
	        if (vr_mode_enabled) {
	            if (vr_uses_ns) {
	                new_pathid = A1026_PATH_VR_NS_RECEIVER;
	                LOGV("A1026 control: new path is A1026_PATH_VR_NS_RECEIVER");
	            } else {
	                new_pathid = A1026_PATH_VR_NO_NS_RECEIVER;
	                LOGV("A1026 control: new path is A1026_PATH_VR_NO_NS_RECEIVER");
	            }
	        } else {
	            new_pathid = A1026_PATH_RECORD_RECEIVER; /* INT-MIC Recording: NS disable, Main MIC */
                LOGV("A1026 control: new path is A1026_PATH_RECORD_RECEIVER");
	        }
	        break;
        case SND_DEVICE_HEADSET:
        case SND_DEVICE_HEADSET_AND_SPEAKER:
        case SND_DEVICE_FM_HEADSET:
        case SND_DEVICE_FM_SPEAKER:
	        if (vr_mode_enabled) {
	            if (vr_uses_ns) {
	                new_pathid = A1026_PATH_VR_NS_HEADSET;
	                LOGV("A1026 control: new path is A1026_PATH_VR_NS_HEADSET");
	            } else {
	                new_pathid = A1026_PATH_VR_NO_NS_HEADSET;
	                LOGV("A1026 control: new path is A1026_PATH_VR_NO_NS_HEADSET");
	            }
	        } else {
	            new_pathid = A1026_PATH_RECORD_HEADSET; /* EXT-MIC Recording: NS disable, Headset MIC */
	            LOGV("A1026 control: new path is A1026_PATH_RECORD_HEADSET");
	        }
	        break;
        case SND_DEVICE_SPEAKER_BACK_MIC:
        case SND_DEVICE_NO_MIC_HEADSET_BACK_MIC:
        case SND_DEVICE_HANDSET_BACK_MIC:
        case SND_DEVICE_HEADSET_AND_SPEAKER_BACK_MIC:
	        new_pathid = A1026_PATH_CAMCORDER; /* CAM-Coder: NS FT mode, Back MIC */
	        LOGV("A1026 control: new path is A1026_PATH_CAMCORDER");
	        break;
        case SND_DEVICE_BT:
        case SND_DEVICE_BT_EC_OFF:
        case SND_DEVICE_CARKIT:
	        if (vr_mode_enabled) {
	            if (vr_uses_ns) {
	                new_pathid = A1026_PATH_VR_NS_BT;
	                LOGV("A1026 control: new path is A1026_PATH_VR_NS_BT");
	            } else {
	                new_pathid = A1026_PATH_VR_NO_NS_BT;
	                LOGV("A1026 control: new path is A1026_PATH_VR_NO_NS_BT");
	            }
	        } else {
	            new_pathid = A1026_PATH_RECORD_BT; /* BT MIC */
	            LOGV("A1026 control: new path is A1026_PATH_RECORD_BT");
	        }
	        break;
        default:
	        break;
        }
    }
    else {
        switch (Routes) {
        case SND_DEVICE_BT:
        case SND_DEVICE_BT_EC_OFF:
        case SND_DEVICE_CARKIT:
            new_pathid = A1026_PATH_RECORD_BT; /* BT MIC */
            LOGV("A1026 control: new path is A1026_PATH_RECORD_BT");
            break;
        default:
            new_pathid = A1026_PATH_SUSPEND;
            break;
        }
    }

    if (old_pathid != new_pathid) {
        //LOGI("A1026: do ioctl(A1026_SET_CONFIG) to %d\n", new_pathid);
        do {
            rc = ioctl(fd_a1026, A1026_SET_CONFIG, &new_pathid);
            if (!rc) {
                old_pathid = new_pathid;
                break;
            }
        } while (--retry);

        if (rc < 0) {
            LOGW("A1026 do hard reset to recover from error!\n");
            rc = doA1026_init(); /* A1026 needs to do hard reset! */
            if (!rc) {
                /* after doA1026_init(), fd_a1026 is -1*/
                fd_a1026 = open("/dev/audience_a1026", O_RDWR);
                if (fd_a1026 < 0) {
                    LOGE("A1026 Fatal Error: unable to open A1026 after hard reset\n");
                } else {
                    rc = ioctl(fd_a1026, A1026_SET_CONFIG, &new_pathid);
                    if (!rc) {
                        old_pathid = new_pathid;
                    } else {
                        LOGE("A1026 Fatal Error: unable to A1026_SET_CONFIG after hard reset\n");
                    }
                }
            } else
                LOGE("A1026 Fatal Error: Re-init A1026 Failed\n");
        }
    }

    if (fd_a1026 >= 0) {
        close(fd_a1026);
    }
    fd_a1026 = -1;
    mA1026Lock.unlock();

    return rc;
}


status_t AudioHardware::doRouting()
{
    android::Mutex::Autolock lock(mLock);
    uint32_t outputDevices = mOutput->devices();
    status_t ret = NO_ERROR;
    AudioStreamInMSM72xx *input = getActiveInput_l();
    uint32_t inputDevice = (input == NULL) ? 0 : input->devices();
    int sndDevice = -1;

    if (mMode == AudioSystem::MODE_IN_CALL && mTTYMode != TTY_MODE_OFF) {
        if ((outputDevices & AudioSystem::DEVICE_OUT_WIRED_HEADSET) ||
            (outputDevices & AudioSystem::DEVICE_OUT_WIRED_HEADPHONE)) {
            switch (mTTYMode) {
            case TTY_MODE_FULL:
                sndDevice = SND_DEVICE_TTY_FULL;
                break;
            case TTY_MODE_VCO:
                sndDevice = SND_DEVICE_TTY_VCO;
                break;
            case TTY_MODE_HCO:
                sndDevice = SND_DEVICE_TTY_HCO;
                break;
            }
        }
    }

    if (sndDevice == -1 && inputDevice != 0) {
        LOGI("do input routing device %x\n", inputDevice);
        if (inputDevice & AudioSystem::DEVICE_IN_BLUETOOTH_SCO_HEADSET) {
            LOGI("Routing audio to Bluetooth PCM\n");
            sndDevice = SND_DEVICE_BT;
        } else if (inputDevice & AudioSystem::DEVICE_OUT_BLUETOOTH_SCO_CARKIT) {
            LOGI("Routing audio to Bluetooth car kit\n");
            sndDevice = SND_DEVICE_CARKIT;
        } else if (inputDevice & AudioSystem::DEVICE_IN_WIRED_HEADSET) {
            if ((outputDevices & AudioSystem::DEVICE_OUT_WIRED_HEADSET) &&
                    (outputDevices & AudioSystem::DEVICE_OUT_SPEAKER)) {
                        LOGI("Routing audio to Wired Headset and Speaker\n");
                        sndDevice = SND_DEVICE_HEADSET_AND_SPEAKER;
            } else {
                LOGI("Routing audio to Wired Headset\n");
                sndDevice = SND_DEVICE_HEADSET;
            }
        } else if (inputDevice & AudioSystem::DEVICE_IN_BACK_MIC) {
            if (outputDevices & (AudioSystem:: DEVICE_OUT_WIRED_HEADSET) &&
                   (outputDevices & AudioSystem:: DEVICE_OUT_SPEAKER)) {
                LOGI("Routing audio to Wired Headset and Speaker with back mic\n");
                sndDevice = SND_DEVICE_HEADSET_AND_SPEAKER_BACK_MIC;
            } else if (outputDevices & AudioSystem::DEVICE_OUT_SPEAKER) {
                LOGI("Routing audio to Speakerphone with back mic\n");
                sndDevice = SND_DEVICE_SPEAKER_BACK_MIC;
            } else if (outputDevices == AudioSystem::DEVICE_OUT_EARPIECE) {
                LOGI("Routing audio to Handset with back mic\n");
                sndDevice = SND_DEVICE_HANDSET_BACK_MIC;
            } else {
                LOGI("Routing audio to Headset with back mic\n");
                sndDevice = SND_DEVICE_NO_MIC_HEADSET_BACK_MIC;
            }
        } else {
            if (outputDevices & AudioSystem::DEVICE_OUT_SPEAKER) {
                LOGI("Routing audio to Speakerphone\n");
                sndDevice = SND_DEVICE_SPEAKER;
            } else if (outputDevices == AudioSystem::DEVICE_OUT_WIRED_HEADPHONE) {
                LOGI("Routing audio to Speakerphone\n");
                sndDevice = SND_DEVICE_NO_MIC_HEADSET;
            } else {
                LOGI("Routing audio to Handset\n");
                sndDevice = SND_DEVICE_HANDSET;
            }
        }
    }
    // if inputDevice == 0, restore output routing

    if (sndDevice == -1) {
        if (outputDevices & (outputDevices - 1)) {
            if ((outputDevices & AudioSystem::DEVICE_OUT_SPEAKER) == 0) {
                LOGW("Hardware does not support requested route combination (%#X),"
                        " picking closest possible route...", outputDevices);
            }
        }

        if (outputDevices &
                (AudioSystem::DEVICE_OUT_BLUETOOTH_SCO | AudioSystem::DEVICE_OUT_BLUETOOTH_SCO_HEADSET)) {
                    LOGI("Routing audio to Bluetooth PCM\n");
                    sndDevice = SND_DEVICE_BT;
        } else if (outputDevices & AudioSystem::DEVICE_OUT_BLUETOOTH_SCO_CARKIT) {
            LOGI("Routing audio to Bluetooth PCM\n");
            sndDevice = SND_DEVICE_CARKIT;
        } else if ((outputDevices & AudioSystem::DEVICE_OUT_WIRED_HEADSET) &&
                (outputDevices & AudioSystem::DEVICE_OUT_SPEAKER)) {
                    LOGI("Routing audio to Wired Headset and Speaker\n");
                    sndDevice = SND_DEVICE_HEADSET_AND_SPEAKER;
        } else if (outputDevices & AudioSystem::DEVICE_OUT_WIRED_HEADPHONE) {
            if (outputDevices & AudioSystem::DEVICE_OUT_SPEAKER) {
                LOGI("Routing audio to No microphone Wired Headset and Speaker (%d,%x)\n", mMode, outputDevices);
                sndDevice = SND_DEVICE_HEADSET_AND_SPEAKER;
            } else {
                LOGI("Routing audio to No microphone Wired Headset (%d,%x)\n", mMode, outputDevices);
                sndDevice = SND_DEVICE_NO_MIC_HEADSET;
            }
        } else if (outputDevices & AudioSystem::DEVICE_OUT_WIRED_HEADSET) {
            LOGI("Routing audio to Wired Headset\n");
            sndDevice = SND_DEVICE_HEADSET;
        } else if (outputDevices & AudioSystem::DEVICE_OUT_SPEAKER) {
            LOGI("Routing audio to Speakerphone\n");
            sndDevice = SND_DEVICE_SPEAKER;
        } else {
            LOGI("Routing audio to Handset\n");
            sndDevice = SND_DEVICE_HANDSET;
        }
    }

    if ((vr_mode_change) || (sndDevice != -1 && sndDevice != mCurSndDevice)) {
        ret = doAudioRouteOrMute(sndDevice);
        mCurSndDevice = sndDevice;
        if (mMode == AudioSystem::MODE_IN_CALL) {
            if (mHACSetting && hac_enable && mCurSndDevice == (int) SND_DEVICE_HANDSET) {
                LOGD("HAC enable: Setting in-call volume to maximum.\n");
                set_volume_rpc(VOICE_VOLUME_MAX);
            } else {
                set_volume_rpc(mVoiceVolume);
            }
        }
    }

    return ret;
}

status_t AudioHardware::checkMicMute()
{
    android::Mutex::Autolock lock(mLock);
    if (mMode != AudioSystem::MODE_IN_CALL) {
        setMicMute_nosync(true);
    }

    return NO_ERROR;
}

status_t AudioHardware::dumpInternals(int fd, const Vector<String16>& args)
{
    const size_t SIZE = 256;
    char buffer[SIZE];
    String8 result;
    result.append("AudioHardware::dumpInternals\n");
    snprintf(buffer, SIZE, "\tmInit: %s\n", mInit? "true": "false");
    result.append(buffer);
    snprintf(buffer, SIZE, "\tmMicMute: %s\n", mMicMute? "true": "false");
    result.append(buffer);
    snprintf(buffer, SIZE, "\tmBluetoothNrec: %s\n", mBluetoothNrec? "true": "false");
    result.append(buffer);
    snprintf(buffer, SIZE, "\tmBluetoothIdtx: %d\n", mBluetoothIdTx);
    result.append(buffer);
    snprintf(buffer, SIZE, "\tmBluetoothIdrx: %d\n", mBluetoothIdRx);
    result.append(buffer);
    ::write(fd, result.string(), result.size());
    return NO_ERROR;
}

status_t AudioHardware::dump(int fd, const Vector<String16>& args)
{
    dumpInternals(fd, args);
    for (size_t index = 0; index < mInputs.size(); index++) {
        mInputs[index]->dump(fd, args);
    }

    if (mOutput) {
        mOutput->dump(fd, args);
    }
    return NO_ERROR;
}

uint32_t AudioHardware::getInputSampleRate(uint32_t sampleRate)
{
    uint32_t i;
    uint32_t prevDelta;
    uint32_t delta;

    for (i = 0, prevDelta = 0xFFFFFFFF; i < sizeof(inputSamplingRates)/sizeof(uint32_t); i++, prevDelta = delta) {
        delta = abs(sampleRate - inputSamplingRates[i]);
        if (delta > prevDelta) break;
    }
    // i is always > 0 here
    return inputSamplingRates[i-1];
}

// getActiveInput_l() must be called with mLock held
AudioHardware::AudioStreamInMSM72xx *AudioHardware::getActiveInput_l()
{
    for (size_t i = 0; i < mInputs.size(); i++) {
        // return first input found not being in standby mode
        // as only one input can be in this state
        if (!mInputs[i]->checkStandby()) {
            return mInputs[i];
        }
    }

    return NULL;
}
// ----------------------------------------------------------------------------

AudioHardware::AudioStreamOutMSM72xx::AudioStreamOutMSM72xx() :
    mHardware(0), mFd(-1), mStartCount(0), mRetryCount(0), mStandby(true),
    mDevices(0), mChannels(AUDIO_HW_OUT_CHANNELS), mSampleRate(AUDIO_HW_OUT_SAMPLERATE),
    mBufferSize(AUDIO_HW_OUT_BUFSZ)
{
}

status_t AudioHardware::AudioStreamOutMSM72xx::set(
        AudioHardware* hw, uint32_t devices, int *pFormat, uint32_t *pChannels, uint32_t *pRate)
{
    int lFormat = pFormat ? *pFormat : 0;
    uint32_t lChannels = pChannels ? *pChannels : 0;
    uint32_t lRate = pRate ? *pRate : 0;

    mHardware = hw;
    mDevices = devices;

    // fix up defaults
    if (lFormat == 0) lFormat = format();
    if (lChannels == 0) lChannels = channels();
    if (lRate == 0) lRate = sampleRate();

    // check values
    if ((lFormat != format()) ||
        (lChannels != channels()) ||
        (lRate != sampleRate())) {
        if (pFormat) *pFormat = format();
        if (pChannels) *pChannels = channels();
        if (pRate) *pRate = sampleRate();
        return BAD_VALUE;
    }

    if (pFormat) *pFormat = lFormat;
    if (pChannels) *pChannels = lChannels;
    if (pRate) *pRate = lRate;

    mChannels = lChannels;
    mSampleRate = lRate;
    mBufferSize = hw->getBufferSize(lRate, AudioSystem::popCount(lChannels));

    return NO_ERROR;
}

AudioHardware::AudioStreamOutMSM72xx::~AudioStreamOutMSM72xx()
{
    standby();
}

ssize_t AudioHardware::AudioStreamOutMSM72xx::write(const void* buffer, size_t bytes)
{
    // LOGD("AudioStreamOutMSM72xx::write(%p, %u)", buffer, bytes);
    status_t status = NO_INIT;
    size_t count = bytes;
    const uint8_t* p = static_cast<const uint8_t*>(buffer);

    if (mStandby) {

        LOGV("acquire output wakelock");
        acquire_wake_lock(PARTIAL_WAKE_LOCK, kOutputWakelockStr);

        // open driver
        LOGV("open pcm_out driver");
        status = ::open("/dev/msm_pcm_out", O_RDWR);
        if (status < 0) {
            if (errCount++ < 10) {
                LOGE("Cannot open /dev/msm_pcm_out errno: %d", errno);
            }
            release_wake_lock(kOutputWakelockStr);
            goto Error;
        }
        mFd = status;
        mStandby = false;

        // configuration
        LOGV("get config");
        struct msm_audio_config config;
        status = ioctl(mFd, AUDIO_GET_CONFIG, &config);
        if (status < 0) {
            LOGE("Cannot read pcm_out config");
            goto Error;
        }

        LOGV("set pcm_out config");
        config.channel_count = AudioSystem::popCount(channels());
        config.sample_rate = mSampleRate;
        config.buffer_size = mBufferSize;
        config.buffer_count = AUDIO_HW_NUM_OUT_BUF;
        config.codec_type = CODEC_TYPE_PCM;
        status = ioctl(mFd, AUDIO_SET_CONFIG, &config);
        if (status < 0) {
            LOGE("Cannot set config");
            goto Error;
        }

        LOGV("buffer_size: %u", config.buffer_size);
        LOGV("buffer_count: %u", config.buffer_count);
        LOGV("channel_count: %u", config.channel_count);
        LOGV("sample_rate: %u", config.sample_rate);

        uint32_t acdb_id = mHardware->getACDB(MOD_PLAY, mHardware->get_snd_dev());
        status = ioctl(mFd, AUDIO_START, &acdb_id);
        if (status < 0) {
            LOGE("Cannot start pcm playback");
            goto Error;
        }

        status = ioctl(mFd, AUDIO_SET_VOLUME, &stream_volume);
        if (status < 0) {
            LOGE("Cannot start pcm playback");
            goto Error;
        }
    }

    while (count) {
        ssize_t written = ::write(mFd, p, count);
        if (written >= 0) {
            count -= written;
            p += written;
        } else {
            if (errno != EAGAIN) {
                status = written;
                goto Error;
            }
            mRetryCount++;
            LOGD("EAGAIN - retry");
        }
    }

    return bytes;

Error:

    standby();

    // Simulate audio output timing in case of error
    usleep((((bytes * 1000) / frameSize()) * 1000) / sampleRate());
    return status;
}

status_t AudioHardware::AudioStreamOutMSM72xx::standby()
{
    if (!mStandby) {
        LOGD("AudioHardware pcm playback is going to standby.");
        if (mFd >= 0) {
            ::close(mFd);
            mFd = -1;
        }
        LOGV("release output wakelock");
        release_wake_lock(kOutputWakelockStr);
        mStandby = true;
    }
    return NO_ERROR;
}

status_t AudioHardware::AudioStreamOutMSM72xx::dump(int fd, const Vector<String16>& args)
{
    const size_t SIZE = 256;
    char buffer[SIZE];
    String8 result;
    result.append("AudioStreamOutMSM72xx::dump\n");
    snprintf(buffer, SIZE, "\tsample rate: %d\n", sampleRate());
    result.append(buffer);
    snprintf(buffer, SIZE, "\tbuffer size: %d\n", bufferSize());
    result.append(buffer);
    snprintf(buffer, SIZE, "\tchannels: %d\n", channels());
    result.append(buffer);
    snprintf(buffer, SIZE, "\tformat: %d\n", format());
    result.append(buffer);
    snprintf(buffer, SIZE, "\tmHardware: %p\n", mHardware);
    result.append(buffer);
    snprintf(buffer, SIZE, "\tmFd: %d\n", mFd);
    result.append(buffer);
    snprintf(buffer, SIZE, "\tmStartCount: %d\n", mStartCount);
    result.append(buffer);
    snprintf(buffer, SIZE, "\tmRetryCount: %d\n", mRetryCount);
    result.append(buffer);
    snprintf(buffer, SIZE, "\tmStandby: %s\n", mStandby? "true": "false");
    result.append(buffer);
    ::write(fd, result.string(), result.size());
    return NO_ERROR;
}


bool AudioHardware::AudioStreamOutMSM72xx::checkStandby()
{
    return mStandby;
}


status_t AudioHardware::AudioStreamOutMSM72xx::setParameters(const String8& keyValuePairs)
{
    AudioParameter param = AudioParameter(keyValuePairs);
    String8 key = String8(AudioParameter::keyRouting);
    status_t status = NO_ERROR;
    int device;
    LOGV("AudioStreamOutMSM72xx::setParameters() %s", keyValuePairs.string());

    if (param.getInt(key, device) == NO_ERROR) {
        mDevices = device;
        LOGV("set output routing %x", mDevices);
        status = mHardware->doRouting();
        param.remove(key);
    }

    if (param.size()) {
        status = BAD_VALUE;
    }
    return status;
}

String8 AudioHardware::AudioStreamOutMSM72xx::getParameters(const String8& keys)
{
    AudioParameter param = AudioParameter(keys);
    String8 value;
    String8 key = String8(AudioParameter::keyRouting);

    if (param.get(key, value) == NO_ERROR) {
        LOGV("get routing %x", mDevices);
        param.addInt(key, (int)mDevices);
    }

    LOGV("AudioStreamOutMSM72xx::getParameters() %s", param.toString().string());
    return param.toString();
}

status_t AudioHardware::AudioStreamOutMSM72xx::getRenderPosition(uint32_t *dspFrames)
{
    //TODO: enable when supported by driver
    return INVALID_OPERATION;
}

// ----------------------------------------------------------------------------

AudioHardware::AudioStreamInMSM72xx::AudioStreamInMSM72xx() :
    mHardware(0), mFd(-1), mStandby(true), mRetryCount(0),
    mFormat(AUDIO_HW_IN_FORMAT), mChannels(AUDIO_HW_IN_CHANNELS),
    mSampleRate(AUDIO_HW_IN_SAMPLERATE), mBufferSize(AUDIO_HW_IN_BUFSZ),
    mAcoustics((AudioSystem::audio_in_acoustics)0), mDevices(0)
{
}

status_t AudioHardware::AudioStreamInMSM72xx::set(
        AudioHardware* hw, uint32_t devices, int *pFormat, uint32_t *pChannels, uint32_t *pRate,
        AudioSystem::audio_in_acoustics acoustic_flags)
{
    if (pFormat == 0 || *pFormat != AUDIO_HW_IN_FORMAT) {
        *pFormat = AUDIO_HW_IN_FORMAT;
        return BAD_VALUE;
    }
    if (pRate == 0) {
        return BAD_VALUE;
    }
    uint32_t rate = hw->getInputSampleRate(*pRate);
    if (rate != *pRate) {
        *pRate = rate;
        return BAD_VALUE;
    }

    if (pChannels == 0 || (*pChannels != AudioSystem::CHANNEL_IN_MONO &&
        *pChannels != AudioSystem::CHANNEL_IN_STEREO)) {
        *pChannels = AUDIO_HW_IN_CHANNELS;
        return BAD_VALUE;
    }

    mHardware = hw;

    LOGV("AudioStreamInMSM72xx::set(%d, %d, %u)", *pFormat, *pChannels, *pRate);
    if (mFd >= 0) {
        LOGE("Audio record already open");
        return -EPERM;
    }

    mBufferSize = hw->getBufferSize(*pRate, AudioSystem::popCount(*pChannels));
    mDevices = devices;
    mFormat = AUDIO_HW_IN_FORMAT;
    mChannels = *pChannels;
    mSampleRate = *pRate;

    return NO_ERROR;

}

AudioHardware::AudioStreamInMSM72xx::~AudioStreamInMSM72xx()
{
    LOGV("AudioStreamInMSM72xx destructor");
    standby();
}

ssize_t AudioHardware::AudioStreamInMSM72xx::read( void* buffer, ssize_t bytes)
{
//    LOGV("AudioStreamInMSM72xx::read(%p, %ld)", buffer, bytes);
    if (!mHardware) return -1;

    size_t count = bytes;
    uint8_t* p = static_cast<uint8_t*>(buffer);
    status_t status = NO_ERROR;

    if (mStandby) {
        {   // scope for the lock
            android::Mutex::Autolock lock(mHardware->mLock);
            LOGV("acquire input wakelock");
            acquire_wake_lock(PARTIAL_WAKE_LOCK, kInputWakelockStr);
            // open audio input device
            status = ::open("/dev/msm_pcm_in", O_RDWR);
            if (status < 0) {
                LOGE("Cannot open /dev/msm_pcm_in errno: %d", errno);
                LOGV("release input wakelock");
                release_wake_lock(kInputWakelockStr);
                goto Error;
            }
            mFd = status;
            mStandby = false;

            // configuration
            LOGV("get config");
            struct msm_audio_config config;
            status = ioctl(mFd, AUDIO_GET_CONFIG, &config);
            if (status < 0) {
                LOGE("Cannot read config");
                goto Error;
            }

            LOGV("set config");
            config.channel_count = AudioSystem::popCount(mChannels);
            config.sample_rate = mSampleRate;
            config.buffer_size = mBufferSize;
            config.buffer_count = 2;
            config.codec_type = CODEC_TYPE_PCM;
            status = ioctl(mFd, AUDIO_SET_CONFIG, &config);
            if (status < 0) {
                LOGE("Cannot set config");
                goto Error;
            }

            LOGV("buffer_size: %u", config.buffer_size);
            LOGV("buffer_count: %u", config.buffer_count);
            LOGV("channel_count: %u", config.channel_count);
            LOGV("sample_rate: %u", config.sample_rate);
        }

        mHardware->set_mRecordState(1);
        // make sure a1026 config is re-applied even is input device is not changed
        mHardware->clearCurDevice();
        mHardware->doRouting();

        uint32_t acdb_id = mHardware->getACDB(MOD_REC, mHardware->get_snd_dev());
        if (ioctl(mFd, AUDIO_START, &acdb_id)) {
            LOGE("Error starting record");
            goto Error;
        }
    }

    while (count) {
        ssize_t bytesRead = ::read(mFd, buffer, count);
        if (bytesRead >= 0) {
            count -= bytesRead;
            p += bytesRead;
        } else {
            if (errno != EAGAIN) {
                status = bytesRead;
                goto Error;
            }
            mRetryCount++;
            LOGD("EAGAIN - retrying");
        }
    }
    return bytes;

Error:
    standby();

    // Simulate audio input timing in case of error
    usleep((((bytes * 1000) / frameSize()) * 1000) / sampleRate());

    return status;
}

status_t AudioHardware::AudioStreamInMSM72xx::standby()
{
    if (!mStandby) {
        LOGD("AudioHardware PCM record is going to standby.");
        if (mFd >= 0) {
            ::close(mFd);
            mFd = -1;
        }
        LOGV("release input wakelock");
        release_wake_lock(kInputWakelockStr);

        mStandby = true;

        if (!mHardware) return -1;

        mHardware->set_mRecordState(0);
        // make sure a1026 config is re-applied even is input device is not changed
        mHardware->clearCurDevice();
        mHardware->doRouting();
    }

    return NO_ERROR;
}

bool AudioHardware::AudioStreamInMSM72xx::checkStandby()
{
    return mStandby;
}

status_t AudioHardware::AudioStreamInMSM72xx::dump(int fd, const Vector<String16>& args)
{
    const size_t SIZE = 256;
    char buffer[SIZE];
    String8 result;
    result.append("AudioStreamInMSM72xx::dump\n");
    snprintf(buffer, SIZE, "\tsample rate: %d\n", sampleRate());
    result.append(buffer);
    snprintf(buffer, SIZE, "\tbuffer size: %d\n", bufferSize());
    result.append(buffer);
    snprintf(buffer, SIZE, "\tchannels: %d\n", channels());
    result.append(buffer);
    snprintf(buffer, SIZE, "\tformat: %d\n", format());
    result.append(buffer);
    snprintf(buffer, SIZE, "\tmHardware: %p\n", mHardware);
    result.append(buffer);
    snprintf(buffer, SIZE, "\tmFd count: %d\n", mFd);
    result.append(buffer);
    snprintf(buffer, SIZE, "\tmStandby: %d\n", mStandby);
    result.append(buffer);
    snprintf(buffer, SIZE, "\tmRetryCount: %d\n", mRetryCount);
    result.append(buffer);
    ::write(fd, result.string(), result.size());
    return NO_ERROR;
}

status_t AudioHardware::AudioStreamInMSM72xx::setParameters(const String8& keyValuePairs)
{
    AudioParameter param = AudioParameter(keyValuePairs);
    status_t status = NO_ERROR;
    int device;
    String8 key = String8(AudioParameter::keyInputSource);
    int source;
    LOGV("AudioStreamInMSM72xx::setParameters() %s", keyValuePairs.string());

    // reading input source for voice recognition mode parameter
    if (param.getInt(key, source) == NO_ERROR) {
        LOGV("set input source %d", source);
        int uses_vr = (source == AUDIO_SOURCE_VOICE_RECOGNITION);
        vr_mode_change = (vr_mode_enabled != uses_vr);
        vr_mode_enabled = uses_vr;
        param.remove(key);
    }

    // reading routing parameter
    key = String8(AudioParameter::keyRouting);
    if (param.getInt(key, device) == NO_ERROR) {
        LOGV("set input routing %x", device);
        if (device & (device - 1)) {
            status = BAD_VALUE;
        } else {
            mDevices = device;
            status = mHardware->doRouting();
        }
        param.remove(key);
    }

    if (param.size()) {
        status = BAD_VALUE;
    }
    return status;
}

String8 AudioHardware::AudioStreamInMSM72xx::getParameters(const String8& keys)
{
    AudioParameter param = AudioParameter(keys);
    String8 value;
    String8 key = String8(AudioParameter::keyRouting);

    if (param.get(key, value) == NO_ERROR) {
        LOGV("get routing %x", mDevices);
        param.addInt(key, (int)mDevices);
    }

    LOGV("AudioStreamInMSM72xx::getParameters() %s", param.toString().string());
    return param.toString();
}

// ----------------------------------------------------------------------------

extern "C" AudioHardwareInterface* createAudioHardware(void) {
    return new AudioHardware();
}

}; // namespace android
