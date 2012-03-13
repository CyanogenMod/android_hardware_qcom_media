/*
** Copyright 2008, The Android Open-Source Project
** Copyright (c) 2012, Code Aurora Forum. All rights reserved.
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

#define LOG_TAG "AudioHardwareMSM72XX"
#include <utils/Log.h>
#include <utils/String8.h>

#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <errno.h>

#include <cutils/properties.h> // for property_get

// hardware specific functions

#include "AudioHardware.h"
//#include <media/AudioRecord.h>

#define LOG_SND_RPC 0  // Set to 1 to log sound RPC's

#undef COMBO_DEVICE_SUPPORTED // Headset speaker combo device not supported on this target
#define DUALMIC_KEY "dualmic_enabled"
#define TTY_MODE_KEY "tty_mode"

namespace android_audio_legacy {

static int audpre_index, tx_iir_index;
static void * acoustic;
const uint32_t AudioHardware::inputSamplingRates[] = {
        8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000
};

static int get_audpp_filter(void);
static int msm72xx_enable_postproc(bool state);
static int msm72xx_enable_preproc(bool state);

// Post processing paramters
static struct rx_iir_filter iir_cfg[3];
static struct adrc_filter adrc_cfg[3];
static struct mbadrc_filter mbadrc_cfg[3];
eqalizer eqalizer[3];
static uint16_t adrc_flag[3];
static uint16_t mbadrc_flag[3];
static uint16_t eq_flag[3];
static uint16_t rx_iir_flag[3];
static uint16_t agc_flag[3];
static uint16_t ns_flag[3];
static uint16_t txiir_flag[3];
static bool audpp_filter_inited = false;
static bool adrc_filter_exists[3];
static bool mbadrc_filter_exists[3];
static int post_proc_feature_mask = 0;
static bool playback_in_progress = false;

//Pre processing parameters
static struct tx_iir tx_iir_cfg[9];
static struct ns ns_cfg[9];
static struct tx_agc tx_agc_cfg[9];
static int enable_preproc_mask[9];

static int snd_device = -1;

#define PCM_OUT_DEVICE "/dev/msm_pcm_out"
#define PCM_IN_DEVICE "/dev/msm_pcm_in"
#define PCM_CTL_DEVICE "/dev/msm_pcm_ctl"
#define PREPROC_CTL_DEVICE "/dev/msm_preproc_ctl"
#define VOICE_MEMO_DEVICE "/dev/msm_voicememo"

static uint32_t SND_DEVICE_CURRENT=-1;
static uint32_t SND_DEVICE_HANDSET=-1;
static uint32_t SND_DEVICE_SPEAKER=-1;
static uint32_t SND_DEVICE_BT=-1;
static uint32_t SND_DEVICE_BT_EC_OFF=-1;
static uint32_t SND_DEVICE_HEADSET=-1;
static uint32_t SND_DEVICE_HEADSET_AND_SPEAKER=-1;
static uint32_t SND_DEVICE_IN_S_SADC_OUT_HANDSET=-1;
static uint32_t SND_DEVICE_IN_S_SADC_OUT_SPEAKER_PHONE=-1;
static uint32_t SND_DEVICE_TTY_HEADSET=-1;
static uint32_t SND_DEVICE_TTY_HCO=-1;
static uint32_t SND_DEVICE_TTY_VCO=-1;
static uint32_t SND_DEVICE_CARKIT=-1;
static uint32_t SND_DEVICE_FM_SPEAKER=-1;
static uint32_t SND_DEVICE_FM_HEADSET=-1;
static uint32_t SND_DEVICE_NO_MIC_HEADSET=-1;
// ----------------------------------------------------------------------------

AudioHardware::AudioHardware() :
    mInit(false), mMicMute(true), mBluetoothNrec(true), mBluetoothId(0),
    mOutput(0), mSndEndpoints(NULL), mCurSndDevice(-1), mDualMicEnabled(false)
{
   if (get_audpp_filter() == 0) {
           audpp_filter_inited = true;
   }

    m7xsnddriverfd = open("/dev/msm_snd", O_RDWR);
    if (m7xsnddriverfd >= 0) {
        int rc = ioctl(m7xsnddriverfd, SND_GET_NUM_ENDPOINTS, &mNumSndEndpoints);
        if (rc >= 0) {
            mSndEndpoints = new msm_snd_endpoint[mNumSndEndpoints];
            mInit = true;
            LOGV("constructed (%d SND endpoints)", rc);
            struct msm_snd_endpoint *ept = mSndEndpoints;
            for (int cnt = 0; cnt < mNumSndEndpoints; cnt++, ept++) {
                ept->id = cnt;
                ioctl(m7xsnddriverfd, SND_GET_ENDPOINT, ept);
                LOGV("cnt = %d ept->name = %s ept->id = %d\n", cnt, ept->name, ept->id);
#define CHECK_FOR(desc) if (!strcmp(ept->name, #desc)) SND_DEVICE_##desc = ept->id;
                CHECK_FOR(CURRENT);
                CHECK_FOR(HANDSET);
                CHECK_FOR(SPEAKER);
                CHECK_FOR(BT);
                CHECK_FOR(BT_EC_OFF);
                CHECK_FOR(HEADSET);
                CHECK_FOR(HEADSET_AND_SPEAKER);
                CHECK_FOR(IN_S_SADC_OUT_HANDSET);
                CHECK_FOR(IN_S_SADC_OUT_SPEAKER_PHONE);
                CHECK_FOR(TTY_HEADSET);
                CHECK_FOR(TTY_HCO);
                CHECK_FOR(TTY_VCO);
#undef CHECK_FOR
            }
        }
        else LOGE("Could not retrieve number of MSM SND endpoints.");

        int AUTO_VOLUME_ENABLED = 1; // setting enabled as default

        static const char *const path = "/system/etc/AutoVolumeControl.txt";
        int txtfd;
        struct stat st;
        char *read_buf;

        txtfd = open(path, O_RDONLY);
        if (txtfd < 0) {
            LOGE("failed to open AUTO_VOLUME_CONTROL %s: %s (%d)",
                  path, strerror(errno), errno);
        }
        else {
            if (fstat(txtfd, &st) < 0) {
                LOGE("failed to stat %s: %s (%d)",
                      path, strerror(errno), errno);
                close(txtfd);
            }

            read_buf = (char *) mmap(0, st.st_size,
                        PROT_READ | PROT_WRITE,
                        MAP_PRIVATE,
                        txtfd, 0);

            if (read_buf == MAP_FAILED) {
                LOGE("failed to mmap parameters file: %s (%d)",
                      strerror(errno), errno);
                close(txtfd);
            }

            if(read_buf[0] =='0')
               AUTO_VOLUME_ENABLED = 0;

            munmap(read_buf, st.st_size);
            close(txtfd);
        }

        ioctl(m7xsnddriverfd, SND_AVC_CTL, &AUTO_VOLUME_ENABLED);
        ioctl(m7xsnddriverfd, SND_AGC_CTL, &AUTO_VOLUME_ENABLED);
    }
	else LOGE("Could not open MSM SND driver.");
}

AudioHardware::~AudioHardware()
{
    for (size_t index = 0; index < mInputs.size(); index++) {
        closeInputStream((AudioStreamIn*)mInputs[index]);
    }
    mInputs.clear();
    closeOutputStream((AudioStreamOut*)mOutput);
    delete [] mSndEndpoints;
    if (acoustic) {
        ::dlclose(acoustic);
        acoustic = 0;
    }
    if (m7xsnddriverfd > 0)
    {
      close(m7xsnddriverfd);
      m7xsnddriverfd = -1;
    }
    for (int index = 0; index < 9; index++) {
        enable_preproc_mask[index] = 0;
    }
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
        Mutex::Autolock lock(mLock);

        // only one output stream allowed
        if (mOutput) {
            if (status) {
                *status = INVALID_OPERATION;
            }
            return 0;
        }

        // create new output stream
        AudioStreamOutMSM72xx* out = new AudioStreamOutMSM72xx();
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
    Mutex::Autolock lock(mLock);
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

    if ( (mMode == AudioSystem::MODE_IN_CALL) &&
         (getInputSampleRate(*sampleRate) > AUDIO_HW_IN_SAMPLERATE) &&
         (*format == AUDIO_HW_IN_FORMAT) )
    {
        LOGE("PCM recording, in a voice call, with sample rate more than 8K not supported \
                re-configure with 8K and try software re-sampler ");
        *status = BAD_VALUE;
        *sampleRate = AUDIO_HW_IN_SAMPLERATE;
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
    Mutex::Autolock lock(mLock);

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
    status_t status = AudioHardwareBase::setMode(mode);
    if (status == NO_ERROR) {
        // make sure that doAudioRouteOrMute() is called by doRouting()
        // even if the new device selected is the same as current one.
        clearCurDevice();
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

status_t AudioHardware::setMicMute(bool state)
{
    Mutex::Autolock lock(mLock);
    return setMicMute_nosync(state);
}

// always call with mutex held
status_t AudioHardware::setMicMute_nosync(bool state)
{
    if (mMicMute != state) {
        mMicMute = state;
        return doAudioRouteOrMute(SND_DEVICE_CURRENT);
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
    const char BT_NREC_VALUE_ON[] = "on";


    LOGV("setParameters() %s", keyValuePairs.string());

    if (keyValuePairs.length() == 0) return BAD_VALUE;

    key = String8(BT_NREC_KEY);
    if (param.get(key, value) == NO_ERROR) {
        if (value == BT_NREC_VALUE_ON) {
            mBluetoothNrec = true;
        } else {
            mBluetoothNrec = false;
            LOGI("Turning noise reduction and echo cancellation off for BT "
                 "headset");
        }
    }
    key = String8(BT_NAME_KEY);
    if (param.get(key, value) == NO_ERROR) {
        mBluetoothId = 0;
        for (int i = 0; i < mNumSndEndpoints; i++) {
            if (!strcasecmp(value.string(), mSndEndpoints[i].name)) {
                mBluetoothId = mSndEndpoints[i].id;
                LOGI("Using custom acoustic parameters for %s", value.string());
                break;
            }
        }
        if (mBluetoothId == 0) {
            LOGI("Using default acoustic parameters "
                 "(%s not in acoustic database)", value.string());
            doRouting(NULL);
        }
    }

    key = String8(DUALMIC_KEY);
    if (param.get(key, value) == NO_ERROR) {
        if (value == "true") {
            mDualMicEnabled = true;
            LOGI("DualMike feature Enabled");
        } else {
            mDualMicEnabled = false;
            LOGI("DualMike feature Disabled");
        }
        doRouting(NULL);
    }

    key = String8(TTY_MODE_KEY);
    if (param.get(key, value) == NO_ERROR) {
        if (value == "full") {
            mTtyMode = TTY_FULL;
        } else if (value == "hco") {
            mTtyMode = TTY_HCO;
        } else if (value == "vco") {
            mTtyMode = TTY_VCO;
        } else {
            mTtyMode = TTY_OFF;
        }
        if(mMode != AudioSystem::MODE_IN_CALL){
           return NO_ERROR;
        }
        doRouting(NULL);
    }

    return NO_ERROR;
}

String8 AudioHardware::getParameters(const String8& keys)
{
    AudioParameter param = AudioParameter(keys);
    String8 value;

    String8 key = String8(DUALMIC_KEY);

    if (param.get(key, value) == NO_ERROR) {
        value = String8(mDualMicEnabled ? "true" : "false");
        param.add(key, value);
    }

    LOGV("AudioHardware::getParameters() %s", param.toString().string());
    return param.toString();
}

int check_and_set_audpp_parameters(char *buf, int size)
{
    char *p, *ps;
    static const char *const seps = ",";
    int table_num;
    int i, j;
    int device_id = 0;
    int samp_index = 0;
    eq_filter_type eq[12];
    int fd;
    void *audioeq;
    void *(*eq_cal)(int32_t, int32_t, int32_t, uint16_t, int32_t, int32_t *, int32_t *, uint16_t *);
    uint16_t numerator[6];
    uint16_t denominator[4];
    uint16_t shift[2];

    if ((buf[0] == 'A') && ((buf[1] == '1') || (buf[1] == '2') || (buf[1] == '3'))) {
        /* IIR filter */
        if(buf[1] == '1') device_id=0;
        if(buf[1] == '2') device_id=1;
        if(buf[1] == '3') device_id=2;
        if (!(p = strtok(buf, ",")))
            goto token_err;

        /* Table header */
        table_num = strtol(p + 1, &ps, 10);
        if (!(p = strtok(NULL, seps)))
            goto token_err;
        /* Table description */
        if (!(p = strtok(NULL, seps)))
            goto token_err;

        for (i = 0; i < 48; i++) {
            iir_cfg[device_id].iir_params[i] = (uint16_t)strtol(p, &ps, 16);
            if (!(p = strtok(NULL, seps)))
                goto token_err;
        }
        rx_iir_flag[device_id] = (uint16_t)strtol(p, &ps, 16);
        if (!(p = strtok(NULL, seps)))
            goto token_err;
        iir_cfg[device_id].num_bands = (uint16_t)strtol(p, &ps, 16);

    } else if ((buf[0] == 'B') && ((buf[1] == '1') || (buf[1] == '2') || (buf[1] == '3'))) {
        /* This is the ADRC record we are looking for.  Tokenize it */
        if(buf[1] == '1') device_id=0;
        if(buf[1] == '2') device_id=1;
        if(buf[1] == '3') device_id=2;
        adrc_filter_exists[device_id] = true;
        if (!(p = strtok(buf, ",")))
            goto token_err;

        /* Table header */
        table_num = strtol(p + 1, &ps, 10);
        if (!(p = strtok(NULL, seps)))
            goto token_err;

        /* Table description */
        if (!(p = strtok(NULL, seps)))
            goto token_err;
        adrc_flag[device_id] = (uint16_t)strtol(p, &ps, 16);

        if (!(p = strtok(NULL, seps)))
            goto token_err;
        adrc_cfg[device_id].adrc_params[0] = (uint16_t)strtol(p, &ps, 16);

        if (!(p = strtok(NULL, seps)))
            goto token_err;
        adrc_cfg[device_id].adrc_params[1] = (uint16_t)strtol(p, &ps, 16);

        if (!(p = strtok(NULL, seps)))
            goto token_err;
        adrc_cfg[device_id].adrc_params[2] = (uint16_t)strtol(p, &ps, 16);

        if (!(p = strtok(NULL, seps)))
            goto token_err;
        adrc_cfg[device_id].adrc_params[3] = (uint16_t)strtol(p, &ps, 16);

        if (!(p = strtok(NULL, seps)))
            goto token_err;
        adrc_cfg[device_id].adrc_params[4] = (uint16_t)strtol(p, &ps, 16);

        if (!(p = strtok(NULL, seps)))
            goto token_err;
        adrc_cfg[device_id].adrc_params[5] = (uint16_t)strtol(p, &ps, 16);

        if (!(p = strtok(NULL, seps)))
            goto token_err;
        adrc_cfg[device_id].adrc_params[6] = (uint16_t)strtol(p, &ps, 16);

        if (!(p = strtok(NULL, seps)))
            goto token_err;
        adrc_cfg[device_id].adrc_params[7] = (uint16_t)strtol(p, &ps, 16);

        if (!(p = strtok(NULL, seps)))
            goto token_err;

    } else if (buf[0] == 'C' && ((buf[1] == '1') || (buf[1] == '2') || (buf[1] == '3'))) {
        /* This is the EQ record we are looking for.  Tokenize it */
        if(buf[1] == '1') device_id=0;
        if(buf[1] == '2') device_id=1;
        if(buf[1] == '3') device_id=2;
        if (!(p = strtok(buf, ",")))
            goto token_err;

        /* Table header */
        table_num = strtol(p + 1, &ps, 10);
        if (!(p = strtok(NULL, seps)))
            goto token_err;
        /* Table description */
        if (!(p = strtok(NULL, seps)))
            goto token_err;

        eq_flag[device_id] = (uint16_t)strtol(p, &ps, 16);
        if (!(p = strtok(NULL, seps)))
            goto token_err;
        LOGI("EQ flag = %02x.", eq_flag[device_id]);

        audioeq = ::dlopen("/system/lib/libaudioeq.so", RTLD_NOW);
        if (audioeq == NULL) {
            LOGE("audioeq library open failure");
            return -1;
        }
        eq_cal = (void *(*) (int32_t, int32_t, int32_t, uint16_t, int32_t, int32_t *, int32_t *, uint16_t *))::dlsym(audioeq, "audioeq_calccoefs");
        memset(&eqalizer[device_id], 0, sizeof(eqalizer));
        /* Temp add the bands here */
        eqalizer[device_id].bands = 8;
        for (i = 0; i < eqalizer[device_id].bands; i++) {

            eq[i].gain = (uint16_t)strtol(p, &ps, 16);

            if (!(p = strtok(NULL, seps)))
                goto token_err;
            eq[i].freq = (uint16_t)strtol(p, &ps, 16);

            if (!(p = strtok(NULL, seps)))
                goto token_err;
            eq[i].type = (uint16_t)strtol(p, &ps, 16);

            if (!(p = strtok(NULL, seps)))
                goto token_err;
            eq[i].qf = (uint16_t)strtol(p, &ps, 16);

            if (!(p = strtok(NULL, seps)))
                goto token_err;

            eq_cal(eq[i].gain, eq[i].freq, 48000, eq[i].type, eq[i].qf, (int32_t*)numerator, (int32_t *)denominator, shift);
            for (j = 0; j < 6; j++) {
                eqalizer[device_id].params[ ( i * 6) + j] = numerator[j];
            }
            for (j = 0; j < 4; j++) {
                eqalizer[device_id].params[(eqalizer[device_id].bands * 6) + (i * 4) + j] = denominator[j];
            }
            eqalizer[device_id].params[(eqalizer[device_id].bands * 10) + i] = shift[0];
        }
        ::dlclose(audioeq);

    } else if ((buf[0] == 'D') && ((buf[1] == '1') || (buf[1] == '2') || (buf[1] == '3'))) {
     /* This is the MB_ADRC record we are looking for.  Tokenize it */
        if(buf[1] == '1') device_id=0;
        if(buf[1] == '2') device_id=1;
        if(buf[1] == '3') device_id=2;
        mbadrc_filter_exists[device_id] = true;
        if (!(p = strtok(buf, ",")))
            goto token_err;
          /* Table header */
        table_num = strtol(p + 1, &ps, 10);
        if (!(p = strtok(NULL, seps)))
            goto token_err;

        /* Table description */
        if (!(p = strtok(NULL, seps)))
            goto token_err;
        mbadrc_cfg[device_id].num_bands = (uint16_t)strtol(p, &ps, 16);

        if (!(p = strtok(NULL, seps)))
            goto token_err;
        mbadrc_cfg[device_id].down_samp_level = (uint16_t)strtol(p, &ps, 16);

        if (!(p = strtok(NULL, seps)))
            goto token_err;
        mbadrc_cfg[device_id].adrc_delay = (uint16_t)strtol(p, &ps, 16);

        if (!(p = strtok(NULL, seps)))
            goto token_err;
        mbadrc_cfg[device_id].ext_buf_size = (uint16_t)strtol(p, &ps, 16);
        int ext_buf_count = mbadrc_cfg[device_id].ext_buf_size / 2;

        if (!(p = strtok(NULL, seps)))
            goto token_err;
        mbadrc_cfg[device_id].ext_partition = (uint16_t)strtol(p, &ps, 16);

        if (!(p = strtok(NULL, seps)))
            goto token_err;
        mbadrc_cfg[device_id].ext_buf_msw = (uint16_t)strtol(p, &ps, 16);

        if (!(p = strtok(NULL, seps)))
            goto token_err;
        mbadrc_cfg[device_id].ext_buf_lsw = (uint16_t)strtol(p, &ps, 16);

        for(i = 0;i < mbadrc_cfg[device_id].num_bands; i++) {
            for(j = 0; j < 10; j++) {
                if (!(p = strtok(NULL, seps)))
                    goto token_err;
                mbadrc_cfg[device_id].adrc_band[i].adrc_band_params[j] = (uint16_t)strtol(p, &ps, 16);
            }
        }

        for(i = 0;i < mbadrc_cfg[device_id].ext_buf_size/2; i++) {
            if (!(p = strtok(NULL, seps)))
                goto token_err;
            mbadrc_cfg[device_id].ext_buf.buff[i] = (uint16_t)strtol(p, &ps, 16);
        }
        if (!(p = strtok(NULL, seps)))
            goto token_err;

        mbadrc_flag[device_id] = (uint16_t)strtol(p, &ps, 16);
        LOGV("MBADRC flag = %02x.", mbadrc_flag[device_id]);
    }else if ((buf[0] == 'E') || (buf[0] == 'F') || (buf[0] == 'G')){
     //Pre-Processing Features TX_IIR,NS,AGC
        switch (buf[1]) {
                case '1':
                        samp_index = 0;
                        break;
                case '2':
                        samp_index = 1;
                        break;
                case '3':
                        samp_index = 2;
                        break;
                case '4':
                        samp_index = 3;
                        break;
                case '5':
                        samp_index = 4;
                        break;
                case '6':
                        samp_index = 5;
                        break;
                case '7':
                        samp_index = 6;
                        break;
                case '8':
                        samp_index = 7;
                        break;
                case '9':
                        samp_index = 8;
                        break;
                default:
                        return -EINVAL;
                        break;
        }

        if (buf[0] == 'E')  {
        /* TX_IIR filter */
        if (!(p = strtok(buf, ","))){
            goto token_err;}

        /* Table header */
        table_num = strtol(p + 1, &ps, 10);
        if (!(p = strtok(NULL, seps))){
            goto token_err;}
        /* Table description */
        if (!(p = strtok(NULL, seps))){
            goto token_err;}

        for (i = 0; i < 48; i++) {
            j = (i >= 40)? i : ((i % 2)? (i - 1) : (i + 1));
            tx_iir_cfg[samp_index].iir_params[j] = (uint16_t)strtol(p, &ps, 16);
            if (!(p = strtok(NULL, seps))){
                goto token_err;}
        }

        tx_iir_cfg[samp_index].active_flag = (uint16_t)strtol(p, &ps, 16);
        if (!(p = strtok(NULL, seps))){
            goto token_err;}

        txiir_flag[device_id] = (uint16_t)strtol(p, &ps, 16);

        if (!(p = strtok(NULL, seps)))
            goto token_err;
        tx_iir_cfg[samp_index].num_bands = (uint16_t)strtol(p, &ps, 16);

        tx_iir_cfg[samp_index].cmd_id = 0;

        LOGV("TX IIR flag = %02x.", txiir_flag[device_id]);
        if (txiir_flag[device_id] != 0)
             enable_preproc_mask[samp_index] |= TX_IIR_ENABLE;
        } else if(buf[0] == 'F')  {
        /* AGC filter */
        if (!(p = strtok(buf, ",")))
            goto token_err;

        /* Table header */
        table_num = strtol(p + 1, &ps, 10);
        if (!(p = strtok(NULL, seps)))
            goto token_err;
        /* Table description */
        if (!(p = strtok(NULL, seps)))
            goto token_err;

        tx_agc_cfg[samp_index].cmd_id = (uint16_t)strtol(p, &ps, 16);
        if (!(p = strtok(NULL, seps)))
            goto token_err;

        tx_agc_cfg[samp_index].tx_agc_param_mask = (uint16_t)strtol(p, &ps, 16);
        if (!(p = strtok(NULL, seps)))
            goto token_err;

        tx_agc_cfg[samp_index].tx_agc_enable_flag = (uint16_t)strtol(p, &ps, 16);
        if (!(p = strtok(NULL, seps)))
            goto token_err;

        tx_agc_cfg[samp_index].static_gain = (uint16_t)strtol(p, &ps, 16);
        if (!(p = strtok(NULL, seps)))
            goto token_err;

        tx_agc_cfg[samp_index].adaptive_gain_flag = (uint16_t)strtol(p, &ps, 16);
        if (!(p = strtok(NULL, seps)))
            goto token_err;

        for (i = 0; i < 19; i++) {
            tx_agc_cfg[samp_index].agc_params[i] = (uint16_t)strtol(p, &ps, 16);
            if (!(p = strtok(NULL, seps)))
                goto token_err;
            }

        agc_flag[device_id] = (uint16_t)strtol(p, &ps, 16);
        LOGV("AGC flag = %02x.", agc_flag[device_id]);
        if (agc_flag[device_id] != 0)
            enable_preproc_mask[samp_index] |= AGC_ENABLE;
        } else if ((buf[0] == 'G')) {
        /* This is the NS record we are looking for.  Tokenize it */
        if (!(p = strtok(buf, ",")))
            goto token_err;

        /* Table header */
        table_num = strtol(p + 1, &ps, 10);
        if (!(p = strtok(NULL, seps)))
            goto token_err;

        /* Table description */
        if (!(p = strtok(NULL, seps)))
            goto token_err;
        ns_cfg[samp_index].cmd_id = (uint16_t)strtol(p, &ps, 16);

        if (!(p = strtok(NULL, seps)))
            goto token_err;
        ns_cfg[samp_index].ec_mode_new = (uint16_t)strtol(p, &ps, 16);

        if (!(p = strtok(NULL, seps)))
            goto token_err;
        ns_cfg[samp_index].dens_gamma_n = (uint16_t)strtol(p, &ps, 16);

        if (!(p = strtok(NULL, seps)))
            goto token_err;
        ns_cfg[samp_index].dens_nfe_block_size = (uint16_t)strtol(p, &ps, 16);

        if (!(p = strtok(NULL, seps)))
            goto token_err;
        ns_cfg[samp_index].dens_limit_ns = (uint16_t)strtol(p, &ps, 16);

        if (!(p = strtok(NULL, seps)))
            goto token_err;
        ns_cfg[samp_index].dens_limit_ns_d = (uint16_t)strtol(p, &ps, 16);

        if (!(p = strtok(NULL, seps)))
            goto token_err;
        ns_cfg[samp_index].wb_gamma_e = (uint16_t)strtol(p, &ps, 16);

        if (!(p = strtok(NULL, seps)))
            goto token_err;
        ns_cfg[samp_index].wb_gamma_n = (uint16_t)strtol(p, &ps, 16);

        if (!(p = strtok(NULL, seps)))
            goto token_err;
        ns_flag[device_id] = (uint16_t)strtol(p, &ps, 16);

        LOGV("NS flag = %02x.", ns_flag[device_id]);
        if (ns_flag[device_id] != 0)
            enable_preproc_mask[samp_index] |= NS_ENABLE;
        }
    }
    return 0;

token_err:
    LOGE("malformatted pcm control buffer");
    return -EINVAL;
}

static int get_audpp_filter(void)
{
    struct stat st;
    char *read_buf;
    char *next_str, *current_str;
    int csvfd;

    LOGI("get_audpp_filter");
    static const char *const path =
        "/system/etc/AudioFilter.csv";
    csvfd = open(path, O_RDONLY);
    if (csvfd < 0) {
        /* failed to open normal acoustic file ... */
        LOGE("failed to open AUDIO_NORMAL_FILTER %s: %s (%d).",
             path, strerror(errno), errno);
        return -1;
    } else LOGI("open %s success.", path);

    if (fstat(csvfd, &st) < 0) {
        LOGE("failed to stat %s: %s (%d).",
             path, strerror(errno), errno);
        close(csvfd);
        return -1;
    }

    read_buf = (char *) mmap(0, st.st_size,
                    PROT_READ | PROT_WRITE,
                    MAP_PRIVATE,
                    csvfd, 0);

    if (read_buf == MAP_FAILED) {
        LOGE("failed to mmap parameters file: %s (%d)",
             strerror(errno), errno);
        close(csvfd);
        return -1;
    }

    current_str = read_buf;

    while (*current_str != (char)EOF)  {
        int len;
        next_str = strchr(current_str, '\n');
        if (!next_str)
           break;
        len = next_str - current_str;
        *next_str++ = '\0';
        if (check_and_set_audpp_parameters(current_str, len)) {
            LOGI("failed to set audpp parameters, exiting.");
            munmap(read_buf, st.st_size);
            close(csvfd);
            return -1;
        }
        current_str = next_str;
    }

    munmap(read_buf, st.st_size);
    close(csvfd);
    return 0;
}

static int msm72xx_enable_postproc(bool state)
{
    int fd;
    int device_id=0;

    if (!audpp_filter_inited)
    {
        LOGE("Parsing error in AudioFilter.csv.");
        return -EINVAL;
    }
    if(snd_device < 0) {
        LOGE("Enabling/Disabling post proc features for device: %d", snd_device);
        return -EINVAL;
    }

    if(snd_device == SND_DEVICE_SPEAKER)
    {
        device_id = 0;
        LOGI("set device to SND_DEVICE_SPEAKER device_id=0");
    }
    if(snd_device == SND_DEVICE_HANDSET)
    {
        device_id = 1;
        LOGI("set device to SND_DEVICE_HANDSET device_id=1");
    }
    if(snd_device == SND_DEVICE_HEADSET)
    {
        device_id = 2;
        LOGI("set device to SND_DEVICE_HEADSET device_id=2");
    }

    fd = open(PCM_CTL_DEVICE, O_RDWR);
    if (fd < 0) {
        LOGE("Cannot open PCM Ctl device");
        return -EPERM;
    }

    if(mbadrc_filter_exists[device_id] && state)
    {
        LOGV("MBADRC Enabled");
        post_proc_feature_mask &= ADRC_DISABLE;
        if ((mbadrc_flag[device_id] == 0) && (post_proc_feature_mask & MBADRC_ENABLE))
        {
            LOGV("MBADRC Disable");
            post_proc_feature_mask &= MBADRC_DISABLE;
        }
        else if(post_proc_feature_mask & MBADRC_ENABLE)
        {
            LOGV("MBADRC Enabled %d", post_proc_feature_mask);

            if (ioctl(fd, AUDIO_SET_MBADRC, &mbadrc_cfg[device_id]) < 0)
            {
                LOGE("set mbadrc filter error");
            }
        }
    }
    else if (adrc_filter_exists[device_id] && state)
    {
        post_proc_feature_mask &= MBADRC_DISABLE;
        LOGV("ADRC Enabled %d", post_proc_feature_mask);

        if (adrc_flag[device_id] == 0 && (post_proc_feature_mask & ADRC_ENABLE))
            post_proc_feature_mask &= ADRC_DISABLE;
        else if(post_proc_feature_mask & ADRC_ENABLE)
        {
            LOGI("ADRC Filter ADRC FLAG = %02x.", adrc_flag[device_id]);
            LOGI("ADRC Filter COMP THRESHOLD = %02x.", adrc_cfg[device_id].adrc_params[0]);
            LOGI("ADRC Filter COMP SLOPE = %02x.", adrc_cfg[device_id].adrc_params[1]);
            LOGI("ADRC Filter COMP RMS TIME = %02x.", adrc_cfg[device_id].adrc_params[2]);
            LOGI("ADRC Filter COMP ATTACK[0] = %02x.", adrc_cfg[device_id].adrc_params[3]);
            LOGI("ADRC Filter COMP ATTACK[1] = %02x.", adrc_cfg[device_id].adrc_params[4]);
            LOGI("ADRC Filter COMP RELEASE[0] = %02x.", adrc_cfg[device_id].adrc_params[5]);
            LOGI("ADRC Filter COMP RELEASE[1] = %02x.", adrc_cfg[device_id].adrc_params[6]);
            LOGI("ADRC Filter COMP DELAY = %02x.", adrc_cfg[device_id].adrc_params[7]);
            if (ioctl(fd, AUDIO_SET_ADRC, &adrc_cfg[device_id]) < 0)
            {
                LOGE("set adrc filter error.");
            }
        }
    }
    else
    {
        LOGV("MBADRC and ADRC Disabled");
        post_proc_feature_mask &= (MBADRC_DISABLE | ADRC_DISABLE);
    }

    if (eq_flag[device_id] == 0 && (post_proc_feature_mask & EQ_ENABLE))
        post_proc_feature_mask &= EQ_DISABLE;
    else if ((post_proc_feature_mask & EQ_ENABLE) && state)
    {
        LOGI("Setting EQ Filter");
        if (ioctl(fd, AUDIO_SET_EQ, &eqalizer[device_id]) < 0) {
            LOGE("set Equalizer error.");
        }
    }

    if (rx_iir_flag[device_id] == 0 && (post_proc_feature_mask & RX_IIR_ENABLE))
        post_proc_feature_mask &= RX_IIR_DISABLE;
    else if ((post_proc_feature_mask & RX_IIR_ENABLE)&& state)
    {
        LOGI("IIR Filter FLAG = %02x.", rx_iir_flag[device_id]);
        LOGI("IIR NUMBER OF BANDS = %02x.", iir_cfg[device_id].num_bands);
        LOGI("IIR Filter N1 = %02x.", iir_cfg[device_id].iir_params[0]);
        LOGI("IIR Filter N2 = %02x.",  iir_cfg[device_id].iir_params[1]);
        LOGI("IIR Filter N3 = %02x.",  iir_cfg[device_id].iir_params[2]);
        LOGI("IIR Filter N4 = %02x.",  iir_cfg[device_id].iir_params[3]);
        LOGI("IIR FILTER M1 = %02x.",  iir_cfg[device_id].iir_params[24]);
        LOGI("IIR FILTER M2 = %02x.", iir_cfg[device_id].iir_params[25]);
        LOGI("IIR FILTER M3 = %02x.",  iir_cfg[device_id].iir_params[26]);
        LOGI("IIR FILTER M4 = %02x.",  iir_cfg[device_id].iir_params[27]);
        LOGI("IIR FILTER M16 = %02x.",  iir_cfg[device_id].iir_params[39]);
        LOGI("IIR FILTER SF1 = %02x.",  iir_cfg[device_id].iir_params[40]);
         if (ioctl(fd, AUDIO_SET_RX_IIR, &iir_cfg[device_id]) < 0)
        {
            LOGE("set rx iir filter error.");
        }
    }

    if(state){
        LOGI("Enabling post proc features with mask 0x%04x", post_proc_feature_mask);
        if (ioctl(fd, AUDIO_ENABLE_AUDPP, &post_proc_feature_mask) < 0) {
            LOGE("enable audpp error");
            close(fd);
            return -EPERM;
        }
    } else{
        int disable_mask = 0;

        if(post_proc_feature_mask & MBADRC_ENABLE) disable_mask &= MBADRC_DISABLE;
        if(post_proc_feature_mask & ADRC_ENABLE) disable_mask &= ADRC_DISABLE;
        if(post_proc_feature_mask & EQ_ENABLE) disable_mask &= EQ_DISABLE;
        if(post_proc_feature_mask & RX_IIR_ENABLE) disable_mask &= RX_IIR_DISABLE;

        LOGI("disabling post proc features with mask 0x%04x", disable_mask);
        if (ioctl(fd, AUDIO_ENABLE_AUDPP, &disable_mask) < 0) {
            LOGE("enable audpp error");
            close(fd);
            return -EPERM;
        }
   }

   close(fd);
   return 0;
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
size_t AudioHardware::getInputBufferSize(uint32_t sampleRate, int format, int channelCount)
{
    if ( (format != AudioSystem::PCM_16_BIT) &&
         (format != AudioSystem::AAC)){
        LOGW("getInputBufferSize bad format: 0x%x", format);
        return 0;
    }
    if (channelCount < 1 || channelCount > 2) {
        LOGW("getInputBufferSize bad channel count: %d", channelCount);
        return 0;
    }

   if (format == AudioSystem::AAC)
       return 2048; 
    else
       return 2048*channelCount;
}

static status_t set_volume_rpc(uint32_t device,
                               uint32_t method,
                               uint32_t volume,
                               int m7xsnddriverfd)
{
#if LOG_SND_RPC
    LOGD("rpc_snd_set_volume(%d, %d, %d)\n", device, method, volume);
#endif

    if (device == -1UL) return NO_ERROR;

    if (m7xsnddriverfd < 0) {
        LOGE("Can not open snd device");
        return -EPERM;
    }
    /* rpc_snd_set_volume(
     *     device,            # Any hardware device enum, including
     *                        # SND_DEVICE_CURRENT
     *     method,            # must be SND_METHOD_VOICE to do anything useful
     *     volume,            # integer volume level, in range [0,5].
     *                        # note that 0 is audible (not quite muted)
     *  )
     * rpc_snd_set_volume only works for in-call sound volume.
     */
     struct msm_snd_volume_config args;
     args.device = device;
     args.method = method;
     args.volume = volume;

     if (ioctl(m7xsnddriverfd, SND_SET_VOLUME, &args) < 0) {
         LOGE("snd_set_volume error.");
         return -EIO;
     }
     return NO_ERROR;
}

status_t AudioHardware::setVoiceVolume(float v)
{
    if (v < 0.0) {
        LOGW("setVoiceVolume(%f) under 0.0, assuming 0.0\n", v);
        v = 0.0;
    } else if (v > 1.0) {
        LOGW("setVoiceVolume(%f) over 1.0, assuming 1.0\n", v);
        v = 1.0;
    }

    int vol = lrint(v * 7.0);
    LOGD("setVoiceVolume(%f)\n", v);
    LOGI("Setting in-call volume to %d (available range is 0 to 7)\n", vol);

    if ((mCurSndDevice != -1) && ((mCurSndDevice == SND_DEVICE_TTY_HEADSET) || (mCurSndDevice == SND_DEVICE_TTY_VCO)))
    {
        vol = 1;
        LOGI("For TTY device in FULL or VCO mode, the volume level is set to: %d \n", vol);
    }

    Mutex::Autolock lock(mLock);
    set_volume_rpc(SND_DEVICE_CURRENT, SND_METHOD_VOICE, vol, m7xsnddriverfd);
    return NO_ERROR;
}

status_t AudioHardware::setMasterVolume(float v)
{
    Mutex::Autolock lock(mLock);
    int vol = ceil(v * 7.0);
    LOGI("Set master volume to %d.\n", vol);
    set_volume_rpc(SND_DEVICE_HANDSET, SND_METHOD_VOICE, vol, m7xsnddriverfd);
    set_volume_rpc(SND_DEVICE_SPEAKER, SND_METHOD_VOICE, vol, m7xsnddriverfd);
    set_volume_rpc(SND_DEVICE_BT,      SND_METHOD_VOICE, vol, m7xsnddriverfd);
    set_volume_rpc(SND_DEVICE_HEADSET, SND_METHOD_VOICE, vol, m7xsnddriverfd);
    set_volume_rpc(SND_DEVICE_IN_S_SADC_OUT_HANDSET, SND_METHOD_VOICE, vol, m7xsnddriverfd);
    set_volume_rpc(SND_DEVICE_IN_S_SADC_OUT_SPEAKER_PHONE, SND_METHOD_VOICE, vol, m7xsnddriverfd);
    set_volume_rpc(SND_DEVICE_TTY_HEADSET, SND_METHOD_VOICE, 1, m7xsnddriverfd);
    set_volume_rpc(SND_DEVICE_TTY_VCO, SND_METHOD_VOICE, 1, m7xsnddriverfd);
    // We return an error code here to let the audioflinger do in-software
    // volume on top of the maximum volume that we set through the SND API.
    // return error - software mixer will handle it
    return -1;
}

static status_t do_route_audio_rpc(uint32_t device,
                                   bool ear_mute, bool mic_mute, int m7xsnddriverfd)
{
    if (device == -1UL)
        return NO_ERROR;

#if LOG_SND_RPC
    LOGD("rpc_snd_set_device(%d, %d, %d)\n", device, ear_mute, mic_mute);
#endif

    if (m7xsnddriverfd < 0) {
        LOGE("Can not open snd device");
        return -EPERM;
    }
    // RPC call to switch audio path
    /* rpc_snd_set_device(
     *     device,            # Hardware device enum to use
     *     ear_mute,          # Set mute for outgoing voice audio
     *                        # this should only be unmuted when in-call
     *     mic_mute,          # Set mute for incoming voice audio
     *                        # this should only be unmuted when in-call or
     *                        # recording.
     *  )
     */
    struct msm_snd_device_config args;
    args.device = device;
    args.ear_mute = ear_mute ? SND_MUTE_MUTED : SND_MUTE_UNMUTED;
    if((device != SND_DEVICE_CURRENT) && (!mic_mute)) {
       //Explicitly mute the mic to release DSP resources
        args.mic_mute = SND_MUTE_MUTED;
        if (ioctl(m7xsnddriverfd, SND_SET_DEVICE, &args) < 0) {
            LOGE("snd_set_device error.");
            return -EIO;
        }
    }
    args.mic_mute = mic_mute ? SND_MUTE_MUTED : SND_MUTE_UNMUTED;

    if (ioctl(m7xsnddriverfd, SND_SET_DEVICE, &args) < 0) {
        LOGE("snd_set_device error.");
        return -EIO;
    }

    return NO_ERROR;
}

// always call with mutex held
status_t AudioHardware::doAudioRouteOrMute(uint32_t device)
{
#if 0
    if (device == (uint32_t)SND_DEVICE_BT || device == (uint32_t)SND_DEVICE_CARKIT) {
        if (mBluetoothId) {
            device = mBluetoothId;
        } else if (!mBluetoothNrec) {
            device = SND_DEVICE_BT_EC_OFF;
        }
    }
#endif
    LOGV("doAudioRouteOrMute() device %x, mMode %d, mMicMute %d", device, mMode, mMicMute);
    return do_route_audio_rpc(device,
                              mMode != AudioSystem::MODE_IN_CALL, mMicMute, m7xsnddriverfd);
}

status_t AudioHardware::doRouting(AudioStreamInMSM72xx *input)
{
    /* currently this code doesn't work without the htc libacoustic */

    Mutex::Autolock lock(mLock);
    uint32_t outputDevices = mOutput->devices();
    status_t ret = NO_ERROR;
    int new_snd_device = -1;
    int new_post_proc_feature_mask = 0;

    //int (*msm72xx_enable_audpp)(int);
    //msm72xx_enable_audpp = (int (*)(int))::dlsym(acoustic, "msm72xx_enable_audpp");

    if (input != NULL) {
        uint32_t inputDevice = input->devices();
        LOGI("do input routing device %x\n", inputDevice);
        // ignore routing device information when we start a recording in voice
        // call
        // Recording will happen through currently active tx device
        if(inputDevice == AudioSystem::DEVICE_IN_VOICE_CALL)
            return NO_ERROR;
        if (inputDevice != 0) {
            if (inputDevice & AudioSystem::DEVICE_IN_BLUETOOTH_SCO_HEADSET) {
                LOGI("Routing audio to Bluetooth PCM\n");
                new_snd_device = SND_DEVICE_BT;
            } else if (inputDevice & AudioSystem::DEVICE_IN_WIRED_HEADSET) {
                    LOGI("Routing audio to Wired Headset\n");
                    new_snd_device = SND_DEVICE_HEADSET;
            } else {
                if (outputDevices & AudioSystem::DEVICE_OUT_SPEAKER) {
                    LOGI("Routing audio to Speakerphone\n");
                    new_snd_device = SND_DEVICE_SPEAKER;
                    new_post_proc_feature_mask = (ADRC_ENABLE | EQ_ENABLE | RX_IIR_ENABLE | MBADRC_ENABLE);
                } else {
                    LOGI("Routing audio to Handset\n");
                    new_snd_device = SND_DEVICE_HANDSET;
                }
            }
        }
    }

    // if inputDevice == 0, restore output routing
    if (new_snd_device == -1) {
        if (outputDevices & (outputDevices - 1)) {
            if ((outputDevices & AudioSystem::DEVICE_OUT_SPEAKER) == 0) {
                LOGW("Hardware does not support requested route combination (%#X),"
                     " picking closest possible route...", outputDevices);
            }
        }

        if ((mTtyMode != TTY_OFF) && (mMode == AudioSystem::MODE_IN_CALL) &&
                (outputDevices & AudioSystem::DEVICE_OUT_WIRED_HEADSET)) {
            if (mTtyMode == TTY_FULL) {
                LOGI("Routing audio to TTY FULL Mode\n");
                new_snd_device = SND_DEVICE_TTY_HEADSET;
            } else if (mTtyMode == TTY_VCO) {
                LOGI("Routing audio to TTY VCO Mode\n");
                new_snd_device = SND_DEVICE_TTY_VCO;
            } else if (mTtyMode == TTY_HCO) {
                LOGI("Routing audio to TTY HCO Mode\n");
                new_snd_device = SND_DEVICE_TTY_HCO;
            }
        } else if (outputDevices &
                   (AudioSystem::DEVICE_OUT_BLUETOOTH_SCO | AudioSystem::DEVICE_OUT_BLUETOOTH_SCO_HEADSET)) {
            LOGI("Routing audio to Bluetooth PCM\n");
            new_snd_device = SND_DEVICE_BT;
        } else if (outputDevices & AudioSystem::DEVICE_OUT_BLUETOOTH_SCO_CARKIT) {
            LOGI("Routing audio to Bluetooth PCM\n");
            new_snd_device = SND_DEVICE_CARKIT;
#ifdef COMBO_DEVICE_SUPPORTED
        } else if ((outputDevices & AudioSystem::DEVICE_OUT_WIRED_HEADSET) &&
                   (outputDevices & AudioSystem::DEVICE_OUT_SPEAKER)) {
            LOGI("Routing audio to Wired Headset and Speaker\n");
            new_snd_device = SND_DEVICE_HEADSET_AND_SPEAKER;
            new_post_proc_feature_mask = (ADRC_ENABLE | EQ_ENABLE | RX_IIR_ENABLE | MBADRC_ENABLE);
        } else if (outputDevices & AudioSystem::DEVICE_OUT_WIRED_HEADPHONE) {
            if (outputDevices & AudioSystem::DEVICE_OUT_SPEAKER) {
                LOGI("Routing audio to No microphone Wired Headset and Speaker (%d,%x)\n", mMode, outputDevices);
                new_snd_device = SND_DEVICE_HEADSET_AND_SPEAKER;
                new_post_proc_feature_mask = (ADRC_ENABLE | EQ_ENABLE | RX_IIR_ENABLE | MBADRC_ENABLE);
            } else {
                LOGI("Routing audio to No microphone Wired Headset (%d,%x)\n", mMode, outputDevices);
                new_snd_device = SND_DEVICE_NO_MIC_HEADSET;
            }
#endif
        } else if (outputDevices & AudioSystem::DEVICE_OUT_WIRED_HEADSET) {
            LOGI("Routing audio to Wired Headset\n");
            new_snd_device = SND_DEVICE_HEADSET;
            new_post_proc_feature_mask = (ADRC_ENABLE | EQ_ENABLE | RX_IIR_ENABLE | MBADRC_ENABLE);
        } else if (outputDevices & AudioSystem::DEVICE_OUT_SPEAKER) {
            LOGI("Routing audio to Speakerphone\n");
            new_snd_device = SND_DEVICE_SPEAKER;
            new_post_proc_feature_mask = (ADRC_ENABLE | EQ_ENABLE | RX_IIR_ENABLE | MBADRC_ENABLE);
        } else {
            LOGI("Routing audio to Handset\n");
            new_snd_device = SND_DEVICE_HANDSET;
            new_post_proc_feature_mask = (ADRC_ENABLE | EQ_ENABLE | RX_IIR_ENABLE | MBADRC_ENABLE);
        }
    }

    if (mDualMicEnabled && mMode == AudioSystem::MODE_IN_CALL) {
        if (new_snd_device == SND_DEVICE_HANDSET) {
            LOGI("Routing audio to handset with DualMike enabled\n");
            new_snd_device = SND_DEVICE_IN_S_SADC_OUT_HANDSET;
        } else if (new_snd_device == SND_DEVICE_SPEAKER) {
            LOGI("Routing audio to speakerphone with DualMike enabled\n");
            new_snd_device = SND_DEVICE_IN_S_SADC_OUT_SPEAKER_PHONE;
        }
    }

    if (new_snd_device != -1 && new_snd_device != mCurSndDevice) {
        ret = doAudioRouteOrMute(new_snd_device);

       //disable post proc first for previous session
       if(playback_in_progress)
           msm72xx_enable_postproc(false);

       //enable post proc for new device
       snd_device = new_snd_device;
       post_proc_feature_mask = new_post_proc_feature_mask;

       if(playback_in_progress)
           msm72xx_enable_postproc(true);

       mCurSndDevice = new_snd_device;
    }

    return ret;
}

status_t AudioHardware::checkMicMute()
{
    Mutex::Autolock lock(mLock);
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
    snprintf(buffer, SIZE, "\tmBluetoothId: %d\n", mBluetoothId);
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
        if (mInputs[i]->state() > AudioStreamInMSM72xx::AUDIO_INPUT_CLOSED) {
            return mInputs[i];
        }
    }

    return NULL;
}
// ----------------------------------------------------------------------------

AudioHardware::AudioStreamOutMSM72xx::AudioStreamOutMSM72xx() :
    mHardware(0), mFd(-1), mStartCount(0), mRetryCount(0), mStandby(true), mDevices(0)
{
}

status_t AudioHardware::AudioStreamOutMSM72xx::set(
        AudioHardware* hw, uint32_t devices, int *pFormat, uint32_t *pChannels, uint32_t *pRate)
{
    int lFormat = pFormat ? *pFormat : 0;
    uint32_t lChannels = pChannels ? *pChannels : 0;
    uint32_t lRate = pRate ? *pRate : 0;

    mHardware = hw;

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

    mDevices = devices;

    return NO_ERROR;
}

AudioHardware::AudioStreamOutMSM72xx::~AudioStreamOutMSM72xx()
{
    if (mFd >= 0) close(mFd);
}

ssize_t AudioHardware::AudioStreamOutMSM72xx::write(const void* buffer, size_t bytes)
{
    // LOGD("AudioStreamOutMSM72xx::write(%p, %u)", buffer, bytes);
    status_t status = NO_INIT;
    size_t count = bytes;
    const uint8_t* p = static_cast<const uint8_t*>(buffer);

    if (mStandby) {

        // open driver
        LOGV("open driver");
        status = ::open("/dev/msm_pcm_out", O_RDWR);
        if (status < 0) {
            LOGE("Cannot open /dev/msm_pcm_out errno: %d", errno);
            goto Error;
        }
        mFd = status;

        // configuration
        LOGV("get config");
        struct msm_audio_config config;
        status = ioctl(mFd, AUDIO_GET_CONFIG, &config);
        if (status < 0) {
            LOGE("Cannot read config");
            goto Error;
        }

        LOGV("set config");
        config.channel_count = AudioSystem::popCount(channels());
        config.sample_rate = sampleRate();
        config.buffer_size = bufferSize();
        config.buffer_count = AUDIO_HW_NUM_OUT_BUF;
        config.type = CODEC_TYPE_PCM;
        status = ioctl(mFd, AUDIO_SET_CONFIG, &config);
        if (status < 0) {
            LOGE("Cannot set config");
            goto Error;
        }

        LOGV("buffer_size: %u", config.buffer_size);
        LOGV("buffer_count: %u", config.buffer_count);
        LOGV("channel_count: %u", config.channel_count);
        LOGV("sample_rate: %u", config.sample_rate);

        // fill 2 buffers before AUDIO_START
        mStartCount = AUDIO_HW_NUM_OUT_BUF;
        mStandby = false;
    }

    while (count) {
        ssize_t written = ::write(mFd, p, count);
        if (written >= 0) {
            count -= written;
            p += written;
        } else {
            if (errno != EAGAIN) return written;
            mRetryCount++;
            LOGW("EAGAIN - retry");
        }
    }

    // start audio after we fill 2 buffers
    if (mStartCount) {
        if (--mStartCount == 0) {
            ioctl(mFd, AUDIO_START, 0);
            playback_in_progress = true;
            //enable post processing
            msm72xx_enable_postproc(true);
        }
    }
    return bytes;

Error:
    if (mFd >= 0) {
        ::close(mFd);
        mFd = -1;
    }
    // Simulate audio output timing in case of error
    usleep(bytes * 1000000 / frameSize() / sampleRate());

    return status;
}

status_t AudioHardware::AudioStreamOutMSM72xx::standby()
{
    status_t status = NO_ERROR;
    if (!mStandby && mFd >= 0) {
        //disable post processing
        msm72xx_enable_postproc(false);
        playback_in_progress = false;
        ::close(mFd);
        mFd = -1;
    }
    mStandby = true;
    return status;
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
        status = mHardware->doRouting(NULL);
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
    mHardware(0), mFd(-1), mState(AUDIO_INPUT_CLOSED), mRetryCount(0),
    mFormat(AUDIO_HW_IN_FORMAT), mChannels(AUDIO_HW_IN_CHANNELS),
    mSampleRate(AUDIO_HW_IN_SAMPLERATE), mBufferSize(AUDIO_HW_IN_BUFFERSIZE),
    mAcoustics((AudioSystem::audio_in_acoustics)0), mDevices(0)
{
}

status_t AudioHardware::AudioStreamInMSM72xx::set(
        AudioHardware* hw, uint32_t devices, int *pFormat, uint32_t *pChannels, uint32_t *pRate,
        AudioSystem::audio_in_acoustics acoustic_flags)
{
    if ((pFormat == 0) ||
        ((*pFormat != AUDIO_HW_IN_FORMAT) &&
         (*pFormat != AudioSystem::AAC)))
    {
        *pFormat = AUDIO_HW_IN_FORMAT;
        LOGE("audio format bad value");
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

    if (pChannels == 0 || (*pChannels & (AudioSystem::CHANNEL_IN_MONO | AudioSystem::CHANNEL_IN_STEREO)) == 0)
    {
        *pChannels = AUDIO_HW_IN_CHANNELS;
        return BAD_VALUE;
    }

    mHardware = hw;

    LOGV("AudioStreamInMSM72xx::set(%d, %d, %u)", *pFormat, *pChannels, *pRate);
    if (mFd >= 0) {
        LOGE("Audio record already open");
        return -EPERM;
    }

    struct msm_audio_config config;
    struct msm_audio_voicememo_config gcfg;
    memset(&gcfg,0,sizeof(gcfg));
    status_t status = 0;
    if(*pFormat == AUDIO_HW_IN_FORMAT)
    {
    // open audio input device
        status = ::open(PCM_IN_DEVICE, O_RDWR);
        if (status < 0) {
            LOGE("Cannot open %s errno: %d", PCM_IN_DEVICE, errno);
            goto Error;
        }
        mFd = status;

        // configuration
        status = ioctl(mFd, AUDIO_GET_CONFIG, &config);
        if (status < 0) {
            LOGE("Cannot read config");
           goto Error;
        }

    LOGV("set config");
    config.channel_count = AudioSystem::popCount(*pChannels);
    config.sample_rate = *pRate;
    config.buffer_size = bufferSize();
    config.buffer_count = 2;
        config.type = CODEC_TYPE_PCM;
    status = ioctl(mFd, AUDIO_SET_CONFIG, &config);
    if (status < 0) {
        LOGE("Cannot set config");
        if (ioctl(mFd, AUDIO_GET_CONFIG, &config) == 0) {
            if (config.channel_count == 1) {
                *pChannels = AudioSystem::CHANNEL_IN_MONO;
            } else {
                *pChannels = AudioSystem::CHANNEL_IN_STEREO;
            }
            *pRate = config.sample_rate;
        }
        goto Error;
    }

    LOGV("confirm config");
    status = ioctl(mFd, AUDIO_GET_CONFIG, &config);
    if (status < 0) {
        LOGE("Cannot read config");
        goto Error;
    }
    LOGV("buffer_size: %u", config.buffer_size);
    LOGV("buffer_count: %u", config.buffer_count);
    LOGV("channel_count: %u", config.channel_count);
    LOGV("sample_rate: %u", config.sample_rate);

    mDevices = devices;
    mFormat = AUDIO_HW_IN_FORMAT;
    mChannels = *pChannels;
    mSampleRate = config.sample_rate;
    mBufferSize = config.buffer_size;
    }
    else if(*pFormat == AudioSystem::AAC) {
      // open AAC input device
               status = ::open(PCM_IN_DEVICE, O_RDWR);
               if (status < 0) {
                     LOGE("Cannot open AAC input  device for read");
                     goto Error;
               }
               mFd = status;

      /* Config param */
               if(ioctl(mFd, AUDIO_GET_CONFIG, &config))
               {
                     LOGE(" Error getting buf config param AUDIO_GET_CONFIG \n");
                     goto  Error;
               }

      LOGV("The Config buffer size is %d", config.buffer_size);
      LOGV("The Config buffer count is %d", config.buffer_count);
      LOGV("The Config Channel count is %d", config.channel_count);
      LOGV("The Config Sample rate is %d", config.sample_rate);

      mDevices = devices;
      mChannels = *pChannels;
      mSampleRate = *pRate;
      mBufferSize = 2048;
      mFormat = *pFormat;

      config.channel_count = AudioSystem::popCount(*pChannels);
      config.sample_rate = *pRate;
      config.type = 1; // Configuring PCM_IN_DEVICE to AAC format

      if (ioctl(mFd, AUDIO_SET_CONFIG, &config)) {
             LOGE(" Error in setting config of msm_pcm_in device \n");
                   goto Error;
        }
    }

    //mHardware->setMicMute_nosync(false);
    mState = AUDIO_INPUT_OPENED;

    //if (!acoustic)
    //    return NO_ERROR;

    audpre_index = calculate_audpre_table_index(mSampleRate);
    if(audpre_index < 0) {
        LOGE("wrong sampling rate");
        status = -EINVAL;
        goto Error;
    }
    return NO_ERROR;

Error:
    if (mFd >= 0) {
        ::close(mFd);
        mFd = -1;
    }
    return status;
}

static int msm72xx_enable_preproc(bool state)
{
    uint16_t mask = 0x0000;

    if (audpp_filter_inited)
    {
        int fd;

        fd = open(PREPROC_CTL_DEVICE, O_RDWR);
        if (fd < 0) {
             LOGE("Cannot open PreProc Ctl device");
             return -EPERM;
        }

        if (enable_preproc_mask[audpre_index] & AGC_ENABLE) {
            /* Setting AGC Params */
            LOGI("AGC Filter Param1= %02x.", tx_agc_cfg[audpre_index].cmd_id);
            LOGI("AGC Filter Param2= %02x.", tx_agc_cfg[audpre_index].tx_agc_param_mask);
            LOGI("AGC Filter Param3= %02x.", tx_agc_cfg[audpre_index].tx_agc_enable_flag);
            LOGI("AGC Filter Param4= %02x.", tx_agc_cfg[audpre_index].static_gain);
            LOGI("AGC Filter Param5= %02x.", tx_agc_cfg[audpre_index].adaptive_gain_flag);
            LOGI("AGC Filter Param6= %02x.", tx_agc_cfg[audpre_index].agc_params[0]);
            LOGI("AGC Filter Param7= %02x.", tx_agc_cfg[audpre_index].agc_params[18]);
            if ((enable_preproc_mask[audpre_index] & AGC_ENABLE) &&
                (ioctl(fd, AUDIO_SET_AGC, &tx_agc_cfg[audpre_index]) < 0))
            {
                LOGE("set AGC filter error.");
            }
        }

        if (enable_preproc_mask[audpre_index] & NS_ENABLE) {
            /* Setting NS Params */
            LOGI("NS Filter Param1= %02x.", ns_cfg[audpre_index].cmd_id);
            LOGI("NS Filter Param2= %02x.", ns_cfg[audpre_index].ec_mode_new);
            LOGI("NS Filter Param3= %02x.", ns_cfg[audpre_index].dens_gamma_n);
            LOGI("NS Filter Param4= %02x.", ns_cfg[audpre_index].dens_nfe_block_size);
            LOGI("NS Filter Param5= %02x.", ns_cfg[audpre_index].dens_limit_ns);
            LOGI("NS Filter Param6= %02x.", ns_cfg[audpre_index].dens_limit_ns_d);
            LOGI("NS Filter Param7= %02x.", ns_cfg[audpre_index].wb_gamma_e);
            LOGI("NS Filter Param8= %02x.", ns_cfg[audpre_index].wb_gamma_n);
            if ((enable_preproc_mask[audpre_index] & NS_ENABLE) &&
                (ioctl(fd, AUDIO_SET_NS, &ns_cfg[audpre_index]) < 0))
            {
                LOGE("set NS filter error.");
            }
        }

        if (enable_preproc_mask[audpre_index] & TX_IIR_ENABLE) {
            /* Setting TX_IIR Params */
            LOGI("TX_IIR Filter Param1= %02x.", tx_iir_cfg[audpre_index].cmd_id);
            LOGI("TX_IIR Filter Param2= %02x.", tx_iir_cfg[audpre_index].active_flag);
            LOGI("TX_IIR Filter Param3= %02x.", tx_iir_cfg[audpre_index].num_bands);
            LOGI("TX_IIR Filter Param4= %02x.", tx_iir_cfg[audpre_index].iir_params[0]);
            LOGI("TX_IIR Filter Param5= %02x.", tx_iir_cfg[audpre_index].iir_params[1]);
            LOGI("TX_IIR Filter Param6 %02x.", tx_iir_cfg[audpre_index].iir_params[47]);
            if ((enable_preproc_mask[audpre_index] & TX_IIR_ENABLE) &&
                (ioctl(fd, AUDIO_SET_TX_IIR, &tx_iir_cfg[audpre_index]) < 0))
            {
               LOGE("set TX IIR filter error.");
            }
        }

        if (state == true) {
            /*Setting AUDPRE_ENABLE*/
            if (ioctl(fd, AUDIO_ENABLE_AUDPRE, &enable_preproc_mask[audpre_index]) < 0) {
                LOGE("set AUDPRE_ENABLE error.");
            }
        } else {
            /*Setting AUDPRE_ENABLE*/
            if (ioctl(fd, AUDIO_ENABLE_AUDPRE, &mask) < 0) {
                LOGE("set AUDPRE_ENABLE error.");
            }
        }
        close(fd);
    }

    return NO_ERROR;
}

AudioHardware::AudioStreamInMSM72xx::~AudioStreamInMSM72xx()
{
    LOGV("AudioStreamInMSM72xx destructor");
    standby();
}

ssize_t AudioHardware::AudioStreamInMSM72xx::read( void* buffer, ssize_t bytes)
{
    LOGV("AudioStreamInMSM72xx::read(%p, %ld)", buffer, bytes);
    if (!mHardware) return -1;

    size_t count = bytes;
    size_t  aac_framesize= bytes;
    uint8_t* p = static_cast<uint8_t*>(buffer);
    uint32_t* recogPtr = (uint32_t *)p;
    uint16_t* frameCountPtr;
    uint16_t* frameSizePtr;

    if (mState < AUDIO_INPUT_OPENED) {
        AudioHardware *hw = mHardware;
        hw->mLock.lock();
        status_t status = set(hw, mDevices, &mFormat, &mChannels, &mSampleRate, mAcoustics);
        hw->mLock.unlock();
        if (status != NO_ERROR) {
            return -1;
        }
        mFirstread = false;
    }

    if (mState < AUDIO_INPUT_STARTED) {
        mState = AUDIO_INPUT_STARTED;
        // force routing to input device
        mHardware->clearCurDevice();
        mHardware->doRouting(this);
        if (ioctl(mFd, AUDIO_START, 0)) {
            LOGE("Error starting record");
            standby();
            return -1;
        }
        msm72xx_enable_preproc(true);
    }

    // Resetting the bytes value, to return the appropriate read value
    bytes = 0;
    if (mFormat == AudioSystem::AAC)
    {
        *((uint32_t*)recogPtr) = 0x51434F4D ;// ('Q','C','O', 'M') Number to identify format as AAC by higher layers
        recogPtr++;
        frameCountPtr = (uint16_t*)recogPtr;
        *frameCountPtr = 0;
        p += 3*sizeof(uint16_t);
        count -= 3*sizeof(uint16_t);
    }
    while (count > 0) {

        if (mFormat == AudioSystem::AAC) {
            frameSizePtr = (uint16_t *)p;
            p += sizeof(uint16_t);
            if(!(count > 2)) break;
            count -= sizeof(uint16_t);
        }

        ssize_t bytesRead = ::read(mFd, p, count);
        if (bytesRead > 0) {
            LOGV("Number of Bytes read = %d", bytesRead);
            count -= bytesRead;
            p += bytesRead;
            bytes += bytesRead;
            LOGV("Total Number of Bytes read = %d", bytes);

            if (mFormat == AudioSystem::AAC){
                *frameSizePtr =  bytesRead;
                (*frameCountPtr)++;
            }

            if(!mFirstread)
            {
               mFirstread = true;
               break;
            }

        }
        else if(bytesRead == 0)
        {
         LOGI("Bytes Read = %d ,Buffer no longer sufficient",bytesRead);
         break;
        } else {
            if (errno != EAGAIN) return bytesRead;
            mRetryCount++;
            LOGW("EAGAIN - retrying");
        }
    }
    if (mFormat == AudioSystem::AAC)
         return aac_framesize;

    return bytes;
}

status_t AudioHardware::AudioStreamInMSM72xx::standby()
{
    if (mState > AUDIO_INPUT_CLOSED) {
        msm72xx_enable_preproc(false);
        if (mFd >= 0) {
            ::close(mFd);
            mFd = -1;
        }
        mState = AUDIO_INPUT_CLOSED;
    }
    if (!mHardware) return -1;
    // restore output routing if necessary
    mHardware->clearCurDevice();
    mHardware->doRouting(this);
    return NO_ERROR;
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
    snprintf(buffer, SIZE, "\tmState: %d\n", mState);
    result.append(buffer);
    snprintf(buffer, SIZE, "\tmRetryCount: %d\n", mRetryCount);
    result.append(buffer);
    ::write(fd, result.string(), result.size());
    return NO_ERROR;
}

status_t AudioHardware::AudioStreamInMSM72xx::setParameters(const String8& keyValuePairs)
{
    AudioParameter param = AudioParameter(keyValuePairs);
    String8 key = String8(AudioParameter::keyRouting);
    status_t status = NO_ERROR;
    int device;
    LOGV("AudioStreamInMSM72xx::setParameters() %s", keyValuePairs.string());

    if (param.getInt(key, device) == NO_ERROR) {
        LOGV("set input routing %x", device);
        if (device & (device - 1)) {
            status = BAD_VALUE;
        } else {
            mDevices = device;
            status = mHardware->doRouting(this);
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
