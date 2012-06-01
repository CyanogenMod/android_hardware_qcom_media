/*
** Copyright 2008, The Android Open-Source Project
** Copyright (c) 2011, Code Aurora Forum. All rights reserved.
** Copyright (c) 2011, The CyanogenMod Project
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

#ifndef ANDROID_AUDIO_HARDWARE_H
#define ANDROID_AUDIO_HARDWARE_H

#include <stdint.h>
#include <sys/types.h>

#include <utils/threads.h>
#include <utils/SortedVector.h>

#include <hardware_legacy/AudioHardwareBase.h>

extern "C" {
#include <linux/msm_audio.h>
#include <linux/msm_audio_aac.h>
#ifdef WITH_QCOM_SPEECH
#include <linux/msm_audio_qcp.h>
#include <linux/msm_audio_amrnb.h>
#endif
}

namespace android_audio_legacy {
using android::SortedVector;
using android::Mutex;

// ----------------------------------------------------------------------------
// Kernel driver interface
//

#define SAMP_RATE_INDX_8000	0
#define SAMP_RATE_INDX_11025	1
#define SAMP_RATE_INDX_12000	2
#define SAMP_RATE_INDX_16000	3
#define SAMP_RATE_INDX_22050	4
#define SAMP_RATE_INDX_24000	5
#define SAMP_RATE_INDX_32000	6
#define SAMP_RATE_INDX_44100	7
#define SAMP_RATE_INDX_48000	8

#define EQ_MAX_BAND_NUM 12

#define ADRC_ENABLE  0x0001
#define ADRC_DISABLE 0x0000
#define EQ_ENABLE    0x0002
#define EQ_DISABLE   0x0000
#define RX_IIR_ENABLE   0x0004
#define RX_IIR_DISABLE  0x0000
#define MBADRC_ENABLE  0x0010
#define MBADRC_DISABLE 0x0000

/* HTC */
#define MOD_PLAY 1
#define MOD_REC  2
#define MOD_TX   3
#define MOD_RX   4

#define ACDB_ID_HAC_HANDSET_MIC           107
#define ACDB_ID_HAC_HANDSET_SPKR          207
#define ACDB_ID_EXT_MIC_REC               307
#define ACDB_ID_HEADSET_PLAYBACK          407
#define ACDB_ID_HEADSET_RINGTONE_PLAYBACK 408
#define ACDB_ID_INT_MIC_REC               507
#define ACDB_ID_CAMCORDER                 508
#define ACDB_ID_INT_MIC_VR                509
#define ACDB_ID_SPKR_PLAYBACK             607
#define ACDB_ID_ALT_SPKR_PLAYBACK         608

struct eq_filter_type {
    int16_t gain;
    uint16_t freq;
    uint16_t type;
    uint16_t qf;
};

struct eqalizer {
    uint16_t bands;
    uint16_t params[132];
};

struct rx_iir_filter {
    uint16_t num_bands;
    uint16_t iir_params[48];
};

struct msm_audio_config {
    uint32_t buffer_size;
    uint32_t buffer_count;
    uint32_t channel_count;
    uint32_t sample_rate;
    uint32_t codec_type;
    uint32_t unused[3];
};

struct msm_bt_endpoint {
    int tx;
    int rx;
    char name[64];
};

enum tty_modes {
    TTY_OFF = 0,
    TTY_VCO = 1,
    TTY_HCO = 2,
    TTY_FULL = 3
};

#define CODEC_TYPE_PCM 0
#define AUDIO_HW_NUM_OUT_BUF 2  // Number of buffers in audio driver for output
// TODO: determine actual audio DSP and hardware latency
#define AUDIO_HW_OUT_LATENCY_MS 0  // Additionnal latency introduced by audio DSP and hardware in ms

#define AUDIO_HW_IN_SAMPLERATE 8000                 // Default audio input sample rate
#define AUDIO_HW_IN_CHANNELS (AudioSystem::CHANNEL_IN_MONO) // Default audio input channel mask
#define AUDIO_HW_IN_BUFFERSIZE 2048                 // Default audio input buffer size
#define AUDIO_HW_IN_FORMAT (AudioSystem::PCM_16_BIT)  // Default audio input sample format

#define VOICE_VOLUME_MAX        100  /* Maximum voice volume */

struct msm_audio_stats {
    uint32_t out_bytes;
    uint32_t unused[3];
};

#ifdef WITH_QCOM_SPEECH
/* AMR frame type definitions */
typedef enum {
  AMRSUP_SPEECH_GOOD,          /* Good speech frame              */
  AMRSUP_SPEECH_DEGRADED,      /* Speech degraded                */
  AMRSUP_ONSET,                /* onset                          */
  AMRSUP_SPEECH_BAD,           /* Corrupt speech frame (bad CRC) */
  AMRSUP_SID_FIRST,            /* First silence descriptor       */
  AMRSUP_SID_UPDATE,           /* Comfort noise frame            */
  AMRSUP_SID_BAD,              /* Corrupt SID frame (bad CRC)    */
  AMRSUP_NO_DATA,              /* Nothing to transmit            */
  AMRSUP_SPEECH_LOST,          /* Lost speech in downlink        */
  AMRSUP_FRAME_TYPE_MAX
} amrsup_frame_type;

/* AMR frame mode (frame rate) definitions */
typedef enum {
  AMRSUP_MODE_0475,    /* 4.75 kbit /s */
  AMRSUP_MODE_0515,    /* 5.15 kbit /s */
  AMRSUP_MODE_0590,    /* 5.90 kbit /s */
  AMRSUP_MODE_0670,    /* 6.70 kbit /s */
  AMRSUP_MODE_0740,    /* 7.40 kbit /s */
  AMRSUP_MODE_0795,    /* 7.95 kbit /s */
  AMRSUP_MODE_1020,    /* 10.2 kbit /s */
  AMRSUP_MODE_1220,    /* 12.2 kbit /s */
  AMRSUP_MODE_MAX
} amrsup_mode_type;

/* The AMR classes
*/
typedef enum  {
  AMRSUP_CLASS_A,
  AMRSUP_CLASS_B,
  AMRSUP_CLASS_C
} amrsup_class_type;

/* The maximum number of bits in each class */
#define AMRSUP_CLASS_A_MAX 81
#define AMRSUP_CLASS_B_MAX 405
#define AMRSUP_CLASS_C_MAX 60

/* The size of the buffer required to hold the vocoder frame */
#define AMRSUP_VOC_FRAME_BYTES  \
  ((AMRSUP_CLASS_A_MAX + AMRSUP_CLASS_B_MAX + AMRSUP_CLASS_C_MAX + 7) / 8)

/* Size of each AMR class to hold one frame of AMR data */
#define AMRSUP_CLASS_A_BYTES ((AMRSUP_CLASS_A_MAX + 7) / 8)
#define AMRSUP_CLASS_B_BYTES ((AMRSUP_CLASS_B_MAX + 7) / 8)
#define AMRSUP_CLASS_C_BYTES ((AMRSUP_CLASS_C_MAX + 7) / 8)


/* Number of bytes for an AMR IF2 frame */
#define AMRSUP_IF2_FRAME_BYTES 32

/* Frame types for 4-bit frame type as in 3GPP TS 26.101 v3.2.0, Sec.4.1.1 */
typedef enum {
  AMRSUP_FRAME_TYPE_INDEX_0475    = 0,    /* 4.75 kbit /s    */
  AMRSUP_FRAME_TYPE_INDEX_0515    = 1,    /* 5.15 kbit /s    */
  AMRSUP_FRAME_TYPE_INDEX_0590    = 2,    /* 5.90 kbit /s    */
  AMRSUP_FRAME_TYPE_INDEX_0670    = 3,    /* 6.70 kbit /s    */
  AMRSUP_FRAME_TYPE_INDEX_0740    = 4,    /* 7.40 kbit /s    */
  AMRSUP_FRAME_TYPE_INDEX_0795    = 5,    /* 7.95 kbit /s    */
  AMRSUP_FRAME_TYPE_INDEX_1020    = 6,    /* 10.2 kbit /s    */
  AMRSUP_FRAME_TYPE_INDEX_1220    = 7,    /* 12.2 kbit /s    */
  AMRSUP_FRAME_TYPE_INDEX_AMR_SID = 8,    /* SID frame       */
/* Frame types 9-11 are not supported */
  AMRSUP_FRAME_TYPE_INDEX_NO_DATA = 15,   /* No data         */
  AMRSUP_FRAME_TYPE_INDEX_MAX,
  AMRSUP_FRAME_TYPE_INDEX_UNDEF = AMRSUP_FRAME_TYPE_INDEX_MAX
} amrsup_frame_type_index_type;

#define AMRSUP_FRAME_TYPE_INDEX_MASK         0x0F /* All frame types */
#define AMRSUP_FRAME_TYPE_INDEX_SPEECH_MASK  0x07 /* Speech frame    */

typedef enum {
  AMRSUP_CODEC_AMR_NB,
  AMRSUP_CODEC_AMR_WB,
  AMRSUP_CODEC_MAX
} amrsup_codec_type;

/* IF1-encoded frame info */
typedef struct {
  amrsup_frame_type_index_type frame_type_index;
  unsigned char fqi;    /* frame quality indicator: TRUE: good frame, FALSE: bad */
  amrsup_codec_type amr_type;   /* AMR-NB or AMR-WB */
} amrsup_if1_frame_info_type;

#define AUDFADEC_AMR_FRAME_TYPE_MASK     0x78
#define AUDFADEC_AMR_FRAME_TYPE_SHIFT    3
#define AUDFADEC_AMR_FRAME_QUALITY_MASK  0x04

#define AMR_CLASS_A_BITS_BAD   0

#define AMR_CLASS_A_BITS_SID  39

#define AMR_CLASS_A_BITS_122  81
#define AMR_CLASS_B_BITS_122 103
#define AMR_CLASS_C_BITS_122  60

typedef struct {
  int   len_a;
  unsigned short *class_a;
  int   len_b;
  unsigned short *class_b;
  int   len_c;
  unsigned short *class_c;
} amrsup_frame_order_type;

/* ======================== 12.2 kbps mode ========================== */
const unsigned short amrsup_bit_order_122_a[AMR_CLASS_A_BITS_122] = {
     0,   1,   2,   3,   4,   5,   6,   7,   8,   9,
    10,  11,  12,  13,  14,  23,  15,  16,  17,  18,
    19,  20,  21,  22,  24,  25,  26,  27,  28,  38,
   141,  39, 142,  40, 143,  41, 144,  42, 145,  43,
   146,  44, 147,  45, 148,  46, 149,  47,  97, 150,
   200,  48,  98, 151, 201,  49,  99, 152, 202,  86,
   136, 189, 239,  87, 137, 190, 240,  88, 138, 191,
   241,  91, 194,  92, 195,  93, 196,  94, 197,  95,
   198
};

const unsigned short amrsup_bit_order_122_b[AMR_CLASS_B_BITS_122] = {
   /**/  29,  30,  31,  32,  33,  34,  35,  50, 100,
   153, 203,  89, 139, 192, 242,  51, 101, 154, 204,
    55, 105, 158, 208,  90, 140, 193, 243,  59, 109,
   162, 212,  63, 113, 166, 216,  67, 117, 170, 220,
    36,  37,  54,  53,  52,  58,  57,  56,  62,  61,
    60,  66,  65,  64,  70,  69,  68, 104, 103, 102,
   108, 107, 106, 112, 111, 110, 116, 115, 114, 120,
   119, 118, 157, 156, 155, 161, 160, 159, 165, 164,
   163, 169, 168, 167, 173, 172, 171, 207, 206, 205,
   211, 210, 209, 215, 214, 213, 219, 218, 217, 223,
   222, 221,  73,  72
};

const unsigned short amrsup_bit_order_122_c[AMR_CLASS_C_BITS_122] = {
   /* ------------- */  71,  76,  75,  74,  79,  78,
    77,  82,  81,  80,  85,  84,  83, 123, 122, 121,
   126, 125, 124, 129, 128, 127, 132, 131, 130, 135,
   134, 133, 176, 175, 174, 179, 178, 177, 182, 181,
   180, 185, 184, 183, 188, 187, 186, 226, 225, 224,
   229, 228, 227, 232, 231, 230, 235, 234, 233, 238,
   237, 236,  96, 199
};


const amrsup_frame_order_type amrsup_122_framing = {
  AMR_CLASS_A_BITS_122,
  (unsigned short *) amrsup_bit_order_122_a,
  AMR_CLASS_B_BITS_122,
  (unsigned short *) amrsup_bit_order_122_b,
  AMR_CLASS_C_BITS_122,
  (unsigned short *) amrsup_bit_order_122_c
};
#endif

// ----------------------------------------------------------------------------

using android_audio_legacy::AudioHardwareBase;
using android_audio_legacy::AudioStreamOut;
using android_audio_legacy::AudioStreamIn;
using android_audio_legacy::AudioSystem;
using android_audio_legacy::AudioHardwareInterface;

class AudioHardware : public  AudioHardwareBase
{
    class AudioStreamOutMSM72xx;
#ifdef WITH_QCOM_LPA
    class AudioSessionOutMSM7xxx;
#endif
    class AudioStreamInMSM72xx;

public:
                        AudioHardware();
    virtual             ~AudioHardware();
    virtual status_t    initCheck();

    virtual status_t    setVoiceVolume(float volume);
    virtual status_t    setMasterVolume(float volume);
#ifdef FM_RADIO
    virtual status_t    setFmVolume(float volume);
#endif
    virtual status_t    setMode(int mode);

    // mic mute
    virtual status_t    setMicMute(bool state);
    virtual status_t    getMicMute(bool* state);

    virtual status_t    setParameters(const String8& keyValuePairs);
    virtual String8     getParameters(const String8& keys);

    // create I/O streams
    virtual AudioStreamOut* openOutputStream(
                                uint32_t devices,
                                int *format=0,
                                uint32_t *channels=0,
                                uint32_t *sampleRate=0,
                                status_t *status=0);

#ifdef WITH_QCOM_LPA
    virtual AudioStreamOut* openOutputSession(
                                uint32_t devices,
                                int *format=0,
                                status_t *status=0,
                                int sessionId=-1);
#endif

    virtual AudioStreamIn* openInputStream(
                                uint32_t devices,
                                int *format,
                                uint32_t *channels,
                                uint32_t *sampleRate,
                                status_t *status,
                                AudioSystem::audio_in_acoustics acoustics);

    virtual    void        closeOutputStream(AudioStreamOut* out);
    virtual    void        closeInputStream(AudioStreamIn* in);

    virtual size_t getInputBufferSize(uint32_t sampleRate, int format, int channelCount);
               void        clearCurDevice() { mCurSndDevice = -1; }

protected:
    virtual status_t    dump(int fd, const Vector<String16>& args);

private:

    status_t    doAudioRouteOrMuteHTC(uint32_t device);
    status_t    doAudioRouteOrMute(uint32_t device);
    status_t    setMicMute_nosync(bool state);
    status_t    checkMicMute();
    status_t    dumpInternals(int fd, const Vector<String16>& args);
    uint32_t    getInputSampleRate(uint32_t sampleRate);
    bool        checkOutputStandby();
    status_t    get_mMode();
    status_t    set_mRecordState(bool onoff);
    status_t    get_mRecordState();
    status_t    get_snd_dev();
    status_t    doRouting(AudioStreamInMSM72xx *input);
    uint32_t    getACDB(int mode, uint32_t device);
    status_t    do_aic3254_control(uint32_t device);
    bool        isAic3254Device(uint32_t device);
    status_t    aic3254_config(uint32_t device);
    int         aic3254_ioctl(int cmd, const int argc);
    void        aic3254_powerdown();
    int         aic3254_set_volume(int volume);
#ifdef FM_RADIO
    status_t    enableFM(int sndDevice);
    status_t enableComboDevice(uint32_t sndDevice, bool enableOrDisable);
    status_t    disableFM();
#endif
    AudioStreamInMSM72xx*   getActiveInput_l();
    FILE *fp;

    class AudioStreamOutMSM72xx : public AudioStreamOut {
    public:
                            AudioStreamOutMSM72xx();
        virtual             ~AudioStreamOutMSM72xx();
                status_t    set(AudioHardware* mHardware,
                                uint32_t devices,
                                int *pFormat,
                                uint32_t *pChannels,
                                uint32_t *pRate);
        virtual uint32_t    sampleRate() const { return 44100; }
        // must be 32-bit aligned - driver only seems to like 4800
        virtual size_t      bufferSize() const { return 4800; }
        virtual uint32_t    channels() const { return AudioSystem::CHANNEL_OUT_STEREO; }
        virtual int         format() const { return AudioSystem::PCM_16_BIT; }
        virtual uint32_t    latency() const { return (1000*AUDIO_HW_NUM_OUT_BUF*(bufferSize()/frameSize()))/sampleRate()+AUDIO_HW_OUT_LATENCY_MS; }
        virtual status_t    setVolume(float left, float right) { return INVALID_OPERATION; }
        virtual ssize_t     write(const void* buffer, size_t bytes);
        virtual status_t    standby();
        virtual status_t    dump(int fd, const Vector<String16>& args);
                bool        checkStandby();
        virtual status_t    setParameters(const String8& keyValuePairs);
        virtual String8     getParameters(const String8& keys);
                uint32_t    devices() { return mDevices; }
        virtual status_t    getRenderPosition(uint32_t *dspFrames);

    private:
                AudioHardware* mHardware;
                int         mFd;
                int         mStartCount;
                int         mRetryCount;
                bool        mStandby;
                uint32_t    mDevices;
    };

#ifdef WITH_QCOM_LPA
    class AudioSessionOutMSM7xxx : public AudioStreamOut {
    public:
                            AudioSessionOutMSM7xxx();
        virtual             ~AudioSessionOutMSM7xxx();
                status_t    set(AudioHardware* mHardware,
                                uint32_t devices,
                                int *pFormat,
                                int32_t sessionId);
        virtual uint32_t    sampleRate() const { return 44100; }
        // must be 32-bit aligned - driver only seems to like 4800
        virtual size_t      bufferSize() const { return 4800; }
        virtual uint32_t    channels() const { return AudioSystem::CHANNEL_OUT_STEREO; }
        virtual int         format() const { return AudioSystem::MP3; }
        virtual uint32_t    latency() const { return 0; }
        virtual status_t    setVolume(float left, float right);
        virtual ssize_t     write(const void* buffer, size_t bytes) {return 0;};
        virtual status_t    standby();
        virtual status_t    dump(int fd, const Vector<String16>& args) {return 0;};
                bool        checkStandby();
        virtual status_t    setParameters(const String8& keyValuePairs);
        virtual String8     getParameters(const String8& keys);
                uint32_t    devices() { return mDevices; }
        virtual status_t    getRenderPosition(uint32_t *dspFrames);

    private:
                AudioHardware* mHardware;
                int         mStartCount;
                int         mRetryCount;
                bool        mStandby;
                uint32_t    mDevices;
                int         mSessionId;
    };
#endif

    class AudioStreamInMSM72xx : public AudioStreamIn {
    public:
        enum input_state {
            AUDIO_INPUT_CLOSED,
            AUDIO_INPUT_OPENED,
            AUDIO_INPUT_STARTED
        };

                            AudioStreamInMSM72xx();
        virtual             ~AudioStreamInMSM72xx();
                status_t    set(AudioHardware* mHardware,
                                uint32_t devices,
                                int *pFormat,
                                uint32_t *pChannels,
                                uint32_t *pRate,
                                AudioSystem::audio_in_acoustics acoustics);
        virtual size_t      bufferSize() const { return mBufferSize; }
        virtual uint32_t    channels() const { return mChannels; }
        virtual int         format() const { return mFormat; }
        virtual uint32_t    sampleRate() const { return mSampleRate; }
        virtual status_t    setGain(float gain) { return INVALID_OPERATION; }
        virtual ssize_t     read(void* buffer, ssize_t bytes);
        virtual status_t    dump(int fd, const Vector<String16>& args);
        virtual status_t    standby();
        virtual status_t    setParameters(const String8& keyValuePairs);
        virtual String8     getParameters(const String8& keys);
        virtual unsigned int  getInputFramesLost() const { return 0; }
                uint32_t    devices() { return mDevices; }
                int         state() const { return mState; }
        virtual status_t    addAudioEffect(effect_interface_s**) { return 0;}
        virtual status_t    removeAudioEffect(effect_interface_s**) { return 0;}

    private:
                AudioHardware* mHardware;
                int         mFd;
                int         mState;
                int         mRetryCount;
                int         mFormat;
                uint32_t    mChannels;
                uint32_t    mSampleRate;
                size_t      mBufferSize;
                AudioSystem::audio_in_acoustics mAcoustics;
                uint32_t    mDevices;
                bool        mFirstread;
                uint32_t	mFmRec;
    };

            static const uint32_t inputSamplingRates[];
            bool        mInit;
            bool        mMicMute;
            int         mFmFd;
            bool        mBluetoothNrec;
            bool        mBluetoothVGS;
            uint32_t    mBluetoothId;
            bool        mHACSetting;
            uint32_t    mBluetoothIdTx;
            uint32_t    mBluetoothIdRx;
            AudioStreamOutMSM72xx*  mOutput;
            SortedVector <AudioStreamInMSM72xx*>   mInputs;
            msm_bt_endpoint *mBTEndpoints;
            int         mNumBTEndpoints;
            int mCurSndDevice;
            int m7xsnddriverfd;
            float       mVoiceVolume;
            int         mTtyMode;
            int         mNoiseSuppressionState;
            bool        mDualMicEnabled;
            bool        mRecordState;
            char        mCurDspProfile[22];
            bool        mEffectEnabled;
            char        mActiveAP[10];
            char        mEffect[10];

     friend class AudioStreamInMSM72xx;
            Mutex       mLock;
};

// ----------------------------------------------------------------------------

}; // namespace android

#endif // ANDROID_AUDIO_HARDWARE_MSM72XX_H
