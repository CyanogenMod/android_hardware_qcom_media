/*
** Copyright 2008, The Android Open-Source Project
** Copyright (c) 2010-2012, Code Aurora Forum. All rights reserved.
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
#define LOG_TAG "AudioHardwareMSM8660"
#include <utils/Log.h>
#include <utils/String8.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include <fcntl.h>
#include "AudioHardware.h"
#include <media/AudioSystem.h>
#include <cutils/properties.h>

#include <linux/android_pmem.h>
#ifdef QCOM_ACDB_ENABLED
#include <linux/msm_audio_acdb.h>
#endif
#include <linux/msm_audio_mvs.h>
#include <sys/mman.h>
#include "control.h"
#include "acdb.h"

#define VOICE_SESSION_NAME "Voice session"

// hardware specific functions

#define LOG_SND_RPC 0  // Set to 1 to log sound RPC's

#define DUALMIC_KEY "dualmic_enabled"
#define BTHEADSET_VGS "bt_headset_vgs"
#define ANC_KEY "anc_enabled"
#define TTY_MODE_KEY "tty_mode"
#define ECHO_SUPRESSION "ec_supported"


#define MVS_DEVICE "/dev/msm_mvs"

#ifdef QCOM_FM_ENABLED
#define FM_DEVICE  "/dev/msm_fm"
#define FM_A2DP_REC 1
#define FM_FILE_REC 2
#endif

namespace android_audio_legacy {

Mutex   mDeviceSwitchLock;
static int audpre_index, tx_iir_index;
static void * acoustic;
const uint32_t AudioHardware::inputSamplingRates[] = {
        8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000
};
static uint32_t INVALID_DEVICE                        = 65535;
static uint32_t SND_DEVICE_CURRENT                    = -1;
static uint32_t SND_DEVICE_HANDSET                    = 0;
static uint32_t SND_DEVICE_SPEAKER                    = 1;
static uint32_t SND_DEVICE_HEADSET                    = 2;
static uint32_t SND_DEVICE_FM_HANDSET                 = 3;
static uint32_t SND_DEVICE_FM_SPEAKER                 = 4;
static uint32_t SND_DEVICE_FM_HEADSET                 = 5;
static uint32_t SND_DEVICE_BT                         = 6;
static uint32_t SND_DEVICE_BT_EC_OFF                  = -1;
static uint32_t SND_DEVICE_HEADSET_AND_SPEAKER        = 7;
static uint32_t SND_DEVICE_NO_MIC_HEADSET             = 8;
static uint32_t SND_DEVICE_IN_S_SADC_OUT_HANDSET      = 9;
static uint32_t SND_DEVICE_IN_S_SADC_OUT_SPEAKER_PHONE= 10;
static uint32_t SND_DEVICE_TTY_HEADSET                = 11;
static uint32_t SND_DEVICE_TTY_HCO                    = 12;
static uint32_t SND_DEVICE_TTY_VCO                    = 13;
static uint32_t SND_DEVICE_TTY_FULL                   = 14;
static uint32_t SND_DEVICE_HDMI                       = 15;
static uint32_t SND_DEVICE_CARKIT                     = -1;
static uint32_t SND_DEVICE_ANC_HEADSET                = 16;
static uint32_t SND_DEVICE_NO_MIC_ANC_HEADSET         = 17;
static uint32_t SND_DEVICE_HEADPHONE_AND_SPEAKER      = 18;
static uint32_t SND_DEVICE_FM_TX                      = 19;
static uint32_t SND_DEVICE_FM_TX_AND_SPEAKER          = 20;
static uint32_t SND_DEVICE_SPEAKER_TX                 = 21;

static uint32_t DEVICE_HANDSET_RX            = 0; // handset_rx
static uint32_t DEVICE_HANDSET_TX            = 1;//handset_tx
static uint32_t DEVICE_SPEAKER_RX            = 2; //speaker_stereo_rx
static uint32_t DEVICE_SPEAKER_TX            = 3;//speaker_mono_tx
static uint32_t DEVICE_HEADSET_RX            = 4; //headset_stereo_rx
static uint32_t DEVICE_HEADSET_TX            = 5; //headset_mono_tx
static uint32_t DEVICE_FMRADIO_HANDSET_RX    = 6; //fmradio_handset_rx
static uint32_t DEVICE_FMRADIO_HEADSET_RX    = 7; //fmradio_headset_rx
static uint32_t DEVICE_FMRADIO_SPEAKER_RX    = 8; //fmradio_speaker_rx
static uint32_t DEVICE_DUALMIC_HANDSET_TX    = 9; //handset_dual_mic_endfire_tx
static uint32_t DEVICE_DUALMIC_SPEAKER_TX    = 10; //speaker_dual_mic_endfire_tx
static uint32_t DEVICE_TTY_HEADSET_MONO_RX   = 11; //tty_headset_mono_rx
static uint32_t DEVICE_TTY_HEADSET_MONO_TX   = 12; //tty_headset_mono_tx
static uint32_t DEVICE_SPEAKER_HEADSET_RX    = 13; //headset_stereo_speaker_stereo_rx
static uint32_t DEVICE_FMRADIO_STEREO_TX     = 14;
static uint32_t DEVICE_HDMI_STERO_RX         = 15; //hdmi_stereo_rx
static uint32_t DEVICE_ANC_HEADSET_STEREO_RX = 16; //ANC RX
static uint32_t DEVICE_BT_SCO_RX             = 17; //bt_sco_rx
static uint32_t DEVICE_BT_SCO_TX             = 18; //bt_sco_tx
static uint32_t DEVICE_FMRADIO_STEREO_RX     = 19;

static uint32_t FLUENCE_MODE_ENDFIRE   = 0;
static uint32_t FLUENCE_MODE_BROADSIDE = 1;
static int vr_enable = 0;

int dev_cnt = 0;
const char ** name = NULL;
int mixer_cnt = 0;
static uint32_t cur_tx = INVALID_DEVICE;
static uint32_t cur_rx = INVALID_DEVICE;
#ifdef QCOM_VOIP_ENABLED
int voip_session_id = 0;
int voip_session_mute = 0;
#endif
int voice_session_id = 0;
int voice_session_mute = 0;
static bool dualmic_enabled = false;
#ifdef QCOM_ANC_HEADSET_ENABLED
static bool anc_running = false;
static bool anc_setting = false;
// This flag is used for avoiding multiple init/deinit of ANC driver.
static bool anc_enabled = false;
#endif
bool vMicMute = false;

#ifdef QCOM_ACDB_ENABLED
static bool bInitACDB = false;
#endif

typedef struct routing_table
{
    unsigned short dec_id;
    int dev_id;
    int dev_id_tx;
    int stream_type;
    bool active;
    struct routing_table *next;
} Routing_table;
Routing_table* head;
Mutex       mRoutingTableLock;

typedef struct device_table
{
    int dev_id;
    int acdb_id;
    int class_id;
    int capability;
}Device_table;
Device_table* device_list;

enum STREAM_TYPES {
    PCM_PLAY=1,
    PCM_REC,
#ifdef QCOM_TUNNEL_LPA_ENABLED
    LPA_DECODE,
#endif
    VOICE_CALL,
#ifdef QCOM_VOIP_ENABLED
    VOIP_CALL,
#endif
#ifdef QCOM_FM_ENABLED
    FM_RADIO,
    FM_REC,
    FM_A2DP,
#endif
    INVALID_STREAM
};

typedef struct ComboDeviceType
{
    uint32_t DeviceId;
    STREAM_TYPES StreamType;
}CurrentComboDeviceStruct;
CurrentComboDeviceStruct CurrentComboDeviceData;
Mutex   mComboDeviceLock;

#ifdef QCOM_FM_ENABLED
enum FM_STATE {
    FM_INVALID=1,
    FM_OFF,
    FM_ON
};

FM_STATE fmState = FM_INVALID;
#endif

static uint32_t fmDevice = INVALID_DEVICE;

#define MAX_DEVICE_COUNT 30
#define DEV_ID(X) device_list[X].dev_id
#ifdef QCOM_ACDB_ENABLED
#define ACDB_ID(X) device_list[X].acdb_id
#endif
#define CAPABILITY(X) device_list[X].capability

void addToTable(int decoder_id,int device_id,int device_id_tx,int stream_type,bool active) {
    Routing_table* temp_ptr;
    ALOGD("addToTable stream %d",stream_type);
    Mutex::Autolock lock(mRoutingTableLock);
    temp_ptr = (Routing_table* ) malloc(sizeof(Routing_table));
    temp_ptr->next = NULL;
    temp_ptr->dec_id = decoder_id;
    temp_ptr->dev_id = device_id;
    temp_ptr->dev_id_tx = device_id_tx;
    temp_ptr->stream_type = stream_type;
    temp_ptr->active = active;
    //make sure Voice node is always on top.
    //For voice call device Switching, there a limitation
    //Routing must happen before disabling/Enabling device.
    if(head->next != NULL){
       if(head->next->stream_type == VOICE_CALL){
          temp_ptr->next = head->next->next;
          head->next->next = temp_ptr;
          return;
       }
    }
    //add new Node to head.
    temp_ptr->next =head->next;
    head->next = temp_ptr;
}

bool isStreamOn(int Stream_type) {
    Routing_table* temp_ptr;
    Mutex::Autolock lock(mRoutingTableLock);
    temp_ptr = head->next;
    while(temp_ptr!=NULL) {
        if(temp_ptr->stream_type == Stream_type)
                return true;
        temp_ptr=temp_ptr->next;
    }
    return false;
}

bool isStreamOnAndActive(int Stream_type) {
    Routing_table* temp_ptr;
    Mutex::Autolock lock(mRoutingTableLock);
    temp_ptr = head->next;
    while(temp_ptr!=NULL) {
        if(temp_ptr->stream_type == Stream_type) {
            if(temp_ptr->active == true) {
                return true;
            }
            else {
                return false;
            }
        }
        temp_ptr=temp_ptr->next;
    }
    return false;
}

bool isStreamOnAndInactive(int Stream_type) {
    Routing_table* temp_ptr;
    Mutex::Autolock lock(mRoutingTableLock);
    temp_ptr = head->next;
    while(temp_ptr!=NULL) {
        if(temp_ptr->stream_type == Stream_type) {
            if(temp_ptr->active == false) {
                return true;
            }
            else {
                return false;
            }
        }
        temp_ptr=temp_ptr->next;
    }
    return false;
}

Routing_table*  getNodeByStreamType(int Stream_type) {
    Routing_table* temp_ptr;
    Mutex::Autolock lock(mRoutingTableLock);
    temp_ptr = head->next;
    while(temp_ptr!=NULL) {
        if(temp_ptr->stream_type == Stream_type) {
            return temp_ptr;
        }
        temp_ptr=temp_ptr->next;
    }
    return NULL;
}

void modifyActiveStateOfStream(int Stream_type, bool Active) {
    Routing_table* temp_ptr;
    Mutex::Autolock lock(mRoutingTableLock);
    temp_ptr = head->next;
    while(temp_ptr!=NULL) {
        if(temp_ptr->stream_type == Stream_type) {
            temp_ptr->active = Active;
        }
        temp_ptr=temp_ptr->next;
    }
}

void modifyActiveDeviceOfStream(int Stream_type,int Device_id,int Device_id_tx) {
    Routing_table* temp_ptr;
    temp_ptr = head->next;
    while(temp_ptr!=NULL) {
        if(temp_ptr->stream_type == Stream_type) {
            temp_ptr->dev_id = Device_id;
            temp_ptr->dev_id_tx = Device_id_tx;
        }
        temp_ptr=temp_ptr->next;
    }
}

void printTable()
{
    Routing_table * temp_ptr;
    Mutex::Autolock lock(mRoutingTableLock);
    temp_ptr = head->next;
    while(temp_ptr!=NULL) {
        printf("%d %d %d %d %d\n",temp_ptr->dec_id,temp_ptr->dev_id,temp_ptr->dev_id_tx,temp_ptr->stream_type,temp_ptr->active);
        temp_ptr = temp_ptr->next;
    }
}

void deleteFromTable(int Stream_type) {
    Routing_table *temp_ptr,*temp1;
    ALOGD("deleteFromTable stream %d",Stream_type);
    Mutex::Autolock lock(mRoutingTableLock);
    temp_ptr = head;
    while(temp_ptr->next!=NULL) {
        if(temp_ptr->next->stream_type == Stream_type) {
            temp1 = temp_ptr->next;
            temp_ptr->next = temp_ptr->next->next;
            free(temp1);
            return;
        }
        temp_ptr=temp_ptr->next;
    }

}

bool isDeviceListEmpty() {
    if(head->next == NULL)
        return true;
    else
        return false;
}

#ifdef QCOM_ANC_HEADSET_ENABLED
//NEEDS to be called with device already enabled
#define ANC_ACDB_STEREO_FF_ID 26
int enableANC(int enable, uint32_t device)
{
    int rc;
    device = 16;
    ALOGD("%s: enable=%d, device=%d", __func__, enable, device);

    // If anc is already enabled/disabled, then don't initalize the driver again.
    if (enable == anc_enabled)
    {
        ALOGV("ANC driver is already in state %d. Not calling anc driver", enable);
        return -EPERM;
    }

    if (enable) {
#ifdef QCOM_ACDB_ENABLED
        rc = acdb_loader_send_anc_cal(ANC_ACDB_STEREO_FF_ID);
        if (rc) {
            ALOGE("Error processing ANC ACDB data\n");
            return rc;
        }
#endif
    }
    rc = msm_enable_anc(DEV_ID(device),enable);

    if ( rc == 0 )
    {
        ALOGV("msm_enable_anc was successful");
        anc_enabled = enable;
    } else
    {
        ALOGV("msm_enable_anc failed");
    }

    return rc;
}
#endif

#ifdef QCOM_ACDB_ENABLED
static void initACDB() {
    while(bInitACDB == false) {
        ALOGD("Calling acdb_loader_init_ACDB()");
        if(acdb_loader_init_ACDB() == 0){
            ALOGD("acdb_loader_init_ACDB() successful");
            bInitACDB = true;
        }
    }
}
#endif

int enableDevice(int device,short enable) {

    // prevent disabling of a device if it doesn't exist
    //Temporaray hack till speaker_tx device is mainlined
    if(DEV_ID(device) == INVALID_DEVICE) {
        return 0;
    }
#ifdef QCOM_ACDB_ENABLED
    if(bInitACDB == false) {
        initACDB();
    }
#endif
    ALOGE("value of device and enable is %d %d ALSA dev id:%d",device,enable,DEV_ID(device));
    if( msm_en_device(DEV_ID(device), enable)) {
        ALOGE("msm_en_device(%d,%d) failed errno = %d",DEV_ID(device),enable, errno);
        return -1;
    }
    return 0;
}

static status_t updateDeviceInfo(int rx_device,int tx_device) {
    bool isRxDeviceEnabled = false,isTxDeviceEnabled = false;
    Routing_table *temp_ptr,*temp_head;
    int tx_dev_prev = INVALID_DEVICE;
    temp_head = head;

    ALOGD("updateDeviceInfo: E");
    Mutex::Autolock lock(mDeviceSwitchLock);

    if(temp_head->next == NULL) {
        ALOGD("simple device switch");
        if(cur_rx!=INVALID_DEVICE)
            enableDevice(cur_rx,0);
        if(cur_tx != INVALID_DEVICE)
            enableDevice(cur_tx,0);
        cur_rx = rx_device;
        cur_tx = tx_device;
        if(cur_rx == DEVICE_ANC_HEADSET_STEREO_RX) {
            enableDevice(cur_rx,1);
            enableDevice(cur_tx,1);
        }
        return NO_ERROR;
    }

    Mutex::Autolock lock_1(mRoutingTableLock);

    while(temp_head->next != NULL) {
        temp_ptr = temp_head->next;
        switch(temp_ptr->stream_type) {
            case PCM_PLAY:
#ifdef QCOM_TUNNEL_LPA_ENABLED
            case LPA_DECODE:
#endif
#ifdef QCOM_FM_ENABLED
            case FM_RADIO:
            case FM_A2DP:
#endif
                if(rx_device == INVALID_DEVICE)
                    return -1;
                ALOGD("The node type is %d", temp_ptr->stream_type);
                ALOGV("rx_device = %d,temp_ptr->dev_id = %d",rx_device,temp_ptr->dev_id);
                if(rx_device != temp_ptr->dev_id) {
                    enableDevice(temp_ptr->dev_id,0);
                }
                if(msm_route_stream(PCM_PLAY,temp_ptr->dec_id,DEV_ID(temp_ptr->dev_id),0)) {
                     ALOGV("msm_route_stream(PCM_PLAY,%d,%d,0) failed",temp_ptr->dec_id,DEV_ID(temp_ptr->dev_id));
                }
                if(isRxDeviceEnabled == false) {
                    enableDevice(rx_device,1);
#ifdef QCOM_ACDB_ENABLED
                    acdb_loader_send_audio_cal(ACDB_ID(rx_device), CAPABILITY(rx_device));
#endif
                    isRxDeviceEnabled = true;
                }
                if(msm_route_stream(PCM_PLAY,temp_ptr->dec_id,DEV_ID(rx_device),1)) {
                    ALOGV("msm_route_stream(PCM_PLAY,%d,%d,1) failed",temp_ptr->dec_id,DEV_ID(rx_device));
                }
                modifyActiveDeviceOfStream(temp_ptr->stream_type,rx_device,INVALID_DEVICE);
                cur_tx = tx_device ;
                cur_rx = rx_device ;
                break;

            case PCM_REC:

                if(tx_device == INVALID_DEVICE)
                    return -1;

                // If dual  mic is enabled in QualComm settings then that takes preference.
                if ( dualmic_enabled && (DEV_ID(DEVICE_DUALMIC_SPEAKER_TX) != INVALID_DEVICE))
                {
                   tx_device = DEVICE_DUALMIC_SPEAKER_TX;
                }

                ALOGD("case PCM_REC");
                if(isTxDeviceEnabled == false) {
                    enableDevice(temp_ptr->dev_id,0);
                    enableDevice(tx_device,1);
#ifdef QCOM_ACDB_ENABLED
                    acdb_loader_send_audio_cal(ACDB_ID(tx_device), CAPABILITY(tx_device));
#endif
                    isTxDeviceEnabled = true;
                }
                if(msm_route_stream(PCM_REC,temp_ptr->dec_id,DEV_ID(temp_ptr->dev_id),0)) {
                    ALOGV("msm_route_stream(PCM_PLAY,%d,%d,0) failed",temp_ptr->dec_id,DEV_ID(temp_ptr->dev_id));
                }
                if(msm_route_stream(PCM_REC,temp_ptr->dec_id,DEV_ID(tx_device),1)) {
                    ALOGV("msm_route_stream(PCM_REC,%d,%d,1) failed",temp_ptr->dec_id,DEV_ID(tx_device));
                }
                modifyActiveDeviceOfStream(PCM_REC,tx_device,INVALID_DEVICE);
                tx_dev_prev = cur_tx;
                cur_tx = tx_device ;
                cur_rx = rx_device ;
                if((vMicMute == true) && (tx_dev_prev != cur_tx)) {
                    ALOGD("REC:device switch with mute enabled :tx_dev_prev %d cur_tx: %d",tx_dev_prev, cur_tx);
                    msm_device_mute(DEV_ID(cur_tx), true);
                }
                break;
            case VOICE_CALL:
#ifdef QCOM_VOIP_ENABLED
            case VOIP_CALL:
#endif
                if(rx_device == INVALID_DEVICE || tx_device == INVALID_DEVICE)
                    return -1;
                ALOGD("case VOICE_CALL\VOIP CALL %d",temp_ptr->stream_type);
#ifdef QCOM_ACDB_ENABLED
                acdb_loader_send_voice_cal(ACDB_ID(rx_device),ACDB_ID(tx_device));
#endif
                msm_route_voice(DEV_ID(rx_device),DEV_ID(tx_device),1);

                // Temporary work around for Speaker mode. The driver is not
                // supporting Speaker Rx and Handset Tx combo
                if(isRxDeviceEnabled == false) {
                    if (rx_device != temp_ptr->dev_id)
                    {
                        enableDevice(temp_ptr->dev_id,0);
                    }
                    isRxDeviceEnabled = true;
                }
                if(isTxDeviceEnabled == false) {
                    if (tx_device != temp_ptr->dev_id_tx)
                    {
                        enableDevice(temp_ptr->dev_id_tx,0);
                    }
                    isTxDeviceEnabled = true;
                }

                if (rx_device != temp_ptr->dev_id)
                {
                    enableDevice(rx_device,1);
                }

                if (tx_device != temp_ptr->dev_id_tx)
                {
                    enableDevice(tx_device,1);
                }

                cur_rx = rx_device;
                cur_tx = tx_device;
                modifyActiveDeviceOfStream(temp_ptr->stream_type,cur_rx,cur_tx);
                break;
            default:
                break;
        }
        temp_head = temp_head->next;
    }
    ALOGD("updateDeviceInfo: X");
    return NO_ERROR;
}

void freeMemory() {
    Routing_table *temp;
    while(head != NULL) {
        temp = head->next;
        free(head);
        head = temp;
    }
free(device_list);
}

//
// ----------------------------------------------------------------------------

AudioHardware::AudioHardware() :
    mInit(false), mMicMute(true), mBluetoothNrec(true), mBluetoothId(0),
    mOutput(0),mBluetoothVGS(false),
    mCurSndDevice(-1),
    mTtyMode(TTY_OFF), mFmFd(-1), mNumPcmRec(0)
#ifdef QCOM_VOIP_ENABLED
    , mVoipFd(-1), mNumVoipStreams(0), mDirectOutput(0)
#endif
{

    int control;
    int i = 0,index = 0;
#ifdef QCOM_ACDB_ENABLED
    int acdb_id = INVALID_ACDB_ID;
#endif
    int fluence_mode = FLUENCE_MODE_ENDFIRE;
    char value[128];

        head = (Routing_table* ) malloc(sizeof(Routing_table));
        head->next = NULL;

        ALOGD("msm_mixer_open: Opening the device");
        control = msm_mixer_open("/dev/snd/controlC0", 0);
        if(control< 0)
                ALOGE("ERROR opening the device");


        mixer_cnt = msm_mixer_count();
        ALOGD("msm_mixer_count:mixer_cnt =%d",mixer_cnt);

        dev_cnt = msm_get_device_count();
        ALOGV("got device_count %d",dev_cnt);
        if (dev_cnt <= 0) {
           ALOGE("NO devices registered\n");
           return;
        }

        //End any voice call if it exists. This is to ensure the next request
        //to voice call after a mediaserver crash or sub system restart
        //is not ignored by the voice driver.
        if (msm_end_voice() < 0)
            ALOGE("msm_end_voice() failed");

        if(msm_reset_all_device() < 0)
            ALOGE("msm_reset_all_device() failed");

        name = msm_get_device_list();
        device_list = (Device_table* )malloc(sizeof(Device_table)*MAX_DEVICE_COUNT);
        if(device_list == NULL) {
            ALOGE("malloc failed for device list");
            return;
        }
        property_get("persist.audio.fluence.mode",value,"0");
        if (!strcmp("broadside", value)) {
              fluence_mode = FLUENCE_MODE_BROADSIDE;
        }

    property_get("persist.audio.vr.enable",value,"Unknown");
    if (!strcmp("true", value))
        vr_enable = 1;

        for(i = 0;i<MAX_DEVICE_COUNT;i++)
            device_list[i].dev_id = INVALID_DEVICE;

        for(i = 0; i < dev_cnt;i++) {
            if(strcmp((char* )name[i],"handset_rx") == 0) {
                index = DEVICE_HANDSET_RX;
            }
            else if(strcmp((char* )name[i],"handset_tx") == 0) {
                index = DEVICE_HANDSET_TX;
            }
            else if((strcmp((char* )name[i],"speaker_stereo_rx") == 0) || (strcmp((char* )name[i],"speaker_rx") == 0)) {
                index = DEVICE_SPEAKER_RX;
            }
            else if((strcmp((char* )name[i],"speaker_mono_tx") == 0) || (strcmp((char* )name[i],"speaker_tx") == 0)) {
                index = DEVICE_SPEAKER_TX;
            }
            else if((strcmp((char* )name[i],"headset_stereo_rx") == 0) || (strcmp((char* )name[i],"headset_rx") == 0)) {
                index = DEVICE_HEADSET_RX;
            }
            else if((strcmp((char* )name[i],"headset_mono_tx") == 0) || (strcmp((char* )name[i],"headset_tx") == 0)) {
                index = DEVICE_HEADSET_TX;
            }
            else if(strcmp((char* )name[i],"fmradio_handset_rx") == 0) {
                index = DEVICE_FMRADIO_HANDSET_RX;
            }
            else if((strcmp((char* )name[i],"fmradio_headset_rx") == 0) || (strcmp((char* )name[i],"fm_radio_headset_rx") == 0)) {
                index = DEVICE_FMRADIO_HEADSET_RX;
            }
            else if((strcmp((char* )name[i],"fmradio_speaker_rx") == 0) || (strcmp((char* )name[i],"fm_radio_speaker_rx") == 0)) {
                index = DEVICE_FMRADIO_SPEAKER_RX;
            }
            else if((strcmp((char* )name[i],"handset_dual_mic_endfire_tx") == 0) || (strcmp((char* )name[i],"dualmic_speaker_ef_tx") == 0)) {
                if (fluence_mode == FLUENCE_MODE_ENDFIRE) {
                     index = DEVICE_DUALMIC_HANDSET_TX;
                } else {
                     ALOGV("Endfire handset found but user request for %d\n", fluence_mode);
                     continue;
                }
            }
            else if(strcmp((char* )name[i],"speaker_dual_mic_endfire_tx") == 0) {
                if (fluence_mode == FLUENCE_MODE_ENDFIRE) {
                     index = DEVICE_DUALMIC_SPEAKER_TX;
                } else {
                     ALOGV("Endfire speaker found but user request for %d\n", fluence_mode);
                     continue;
                }
            }
            else if(strcmp((char* )name[i],"handset_dual_mic_broadside_tx") == 0) {
                if (fluence_mode == FLUENCE_MODE_BROADSIDE) {
                     index = DEVICE_DUALMIC_HANDSET_TX;
                } else {
                     ALOGV("Broadside handset found but user request for %d\n", fluence_mode);
                     continue;
                }
            }
            else if(strcmp((char* )name[i],"speaker_dual_mic_broadside_tx") == 0) {
                if (fluence_mode == FLUENCE_MODE_BROADSIDE) {
                     index = DEVICE_DUALMIC_SPEAKER_TX;
                } else {
                     ALOGV("Broadside speaker found but user request for %d\n", fluence_mode);
                     continue;
                }
            }
            else if((strcmp((char* )name[i],"tty_headset_mono_rx") == 0) || (strcmp((char* )name[i],"tty_headset_rx") == 0)) {
                index = DEVICE_TTY_HEADSET_MONO_RX;
            }
            else if((strcmp((char* )name[i],"tty_headset_mono_tx") == 0) || (strcmp((char* )name[i],"tty_headset_tx") == 0)) {
                index = DEVICE_TTY_HEADSET_MONO_TX;
            }
            else if((strcmp((char* )name[i],"bt_sco_rx") == 0) || (strcmp((char* )name[i],"bt_sco_mono_rx") == 0)) {
                index = DEVICE_BT_SCO_RX;
            }
            else if((strcmp((char* )name[i],"bt_sco_tx") == 0) || (strcmp((char* )name[i],"bt_sco_mono_tx") == 0)) {
                index = DEVICE_BT_SCO_TX;
            }
            else if((strcmp((char*)name[i],"headset_stereo_speaker_stereo_rx") == 0) ||
                    (strcmp((char*)name[i],"headset_speaker_stereo_rx") == 0) || (strcmp((char*)name[i],"speaker_headset_rx") == 0)) {
                index = DEVICE_SPEAKER_HEADSET_RX;
            }
            else if((strcmp((char*)name[i],"fmradio_stereo_tx") == 0) || (strcmp((char*)name[i],"fm_radio_tx") == 0)) {
                index = DEVICE_FMRADIO_STEREO_TX;
            }
            else if((strcmp((char*)name[i],"hdmi_stereo_rx") == 0) || (strcmp((char*)name[i],"hdmi_rx") == 0)) {
                index = DEVICE_HDMI_STERO_RX;
            }
            //to check for correct name and ACDB number for ANC
            else if(strcmp((char*)name[i],"anc_headset_stereo_rx") == 0) {
                index = DEVICE_ANC_HEADSET_STEREO_RX;
            }
            else if(strcmp((char*)name[i],"fmradio_stereo_rx") == 0)
                index = DEVICE_FMRADIO_STEREO_RX;
            else
                continue;
            ALOGV("index = %d",index);

            device_list[index].dev_id = msm_get_device((char* )name[i]);
            if(device_list[index].dev_id >= 0) {
                    ALOGV("Found device: %s:index = %d,dev_id: %d",( char* )name[i], index,device_list[index].dev_id);
            }
#ifdef QCOM_ACDB_ENABLED
            acdb_mapper_get_acdb_id_from_dev_name((char* )name[i], &device_list[index].acdb_id);
            device_list[index].class_id = msm_get_device_class(device_list[index].dev_id);
            device_list[index].capability = msm_get_device_capability(device_list[index].dev_id);
            ALOGV("acdb ID = %d,class ID = %d,capablity = %d for device %d",device_list[index].acdb_id,
            device_list[index].class_id,device_list[index].capability,device_list[index].dev_id);
#endif
        }

        mInit = true;

        CurrentComboDeviceData.DeviceId = INVALID_DEVICE;
        CurrentComboDeviceData.StreamType = INVALID_STREAM;
}

AudioHardware::~AudioHardware()
{
    for (size_t index = 0; index < mInputs.size(); index++) {
        closeInputStream((AudioStreamIn*)mInputs[index]);
    }
    mInputs.clear();
#ifdef QCOM_VOIP_ENABLED
    mVoipInputs.clear();
#endif
    closeOutputStream((AudioStreamOut*)mOutput);
    if (acoustic) {
        ::dlclose(acoustic);
        acoustic = 0;
    }
    msm_mixer_close();
#ifdef QCOM_ACDB_ENABLED
    acdb_loader_deallocate_ACDB();
#endif
    freeMemory();

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
        status_t lStatus;
        Mutex::Autolock lock(mLock);
#ifdef QCOM_VOIP_ENABLED
        // only one output stream allowed
        if (mOutput && (devices != AudioSystem::DEVICE_OUT_DIRECTOUTPUT)) {
            if (status) {
                *status = INVALID_OPERATION;
            }
            ALOGE(" AudioHardware::openOutputStream Only one output stream allowed \n");
            return 0;
        }

        if(devices == AudioSystem::DEVICE_OUT_DIRECTOUTPUT) {
            // open direct output stream
            if(mDirectOutput == 0) {
               ALOGV(" AudioHardware::openOutputStream Direct output stream \n");
               AudioStreamOutDirect* out = new AudioStreamOutDirect();
               lStatus = out->set(this, devices, format, channels, sampleRate);
               if (status) {
                   *status = lStatus;
               }
               if (lStatus == NO_ERROR) {
                   mDirectOutput = out;
                   ALOGV(" \n set sucessful for AudioStreamOutDirect");
               } else {
                   ALOGE(" \n set Failed for AudioStreamOutDirect");
                   delete out;
               }
            }
            else {
                   ALOGE(" \n AudioHardware::AudioStreamOutDirect is already open");
            }
            return mDirectOutput;
        } else
#endif
        {
            ALOGV(" AudioHardware::openOutputStream AudioStreamOutMSM8x60 output stream \n");
            // only one output stream allowed
            if (mOutput) {
                if (status) {
                  *status = INVALID_OPERATION;
                }
                ALOGE(" AudioHardware::openOutputStream Only one output stream allowed \n");
                return 0;
            }

            // create new output stream
            AudioStreamOutMSM8x60* out = new AudioStreamOutMSM8x60();
            lStatus = out->set(this, devices, format, channels, sampleRate);
            if (status) {
                *status = lStatus;
            }
            if (lStatus == NO_ERROR) {
                mOutput = out;
            } else {
                delete out;
            }
            return mOutput;
        }
    }
}

#ifdef QCOM_TUNNEL_LPA_ENABLED
AudioStreamOut* AudioHardware::openOutputSession(
        uint32_t devices, int *format, status_t *status, int sessionId,uint32_t samplingRate, uint32_t channels)
{
    AudioSessionOutMSM8x60* out;
    { // scope for the lock
        Mutex::Autolock lock(mLock);

        // create new output stream
        out = new AudioSessionOutMSM8x60();
        status_t lStatus = out->set(this, devices, format, sessionId);
        if (status) {
            *status = lStatus;
        }
        if (lStatus != NO_ERROR) {
            delete out;
            out = NULL;
        }
    }
    return out;
}
#endif

void AudioHardware::closeOutputStream(AudioStreamOut* out) {
    Mutex::Autolock lock(mLock);
    if ((mOutput == 0
#ifdef QCOM_VOIP_ENABLED
        && mDirectOutput == 0
#endif
        ) || ((mOutput != out)
#ifdef QCOM_VOIP_ENABLED
         && (mDirectOutput != out)
#endif
       )) {
        ALOGW("Attempt to close invalid output stream");
    }
    else if (mOutput == out) {
        delete mOutput;
        mOutput = 0;
    }
#ifdef QCOM_VOIP_ENABLED
    else if (mDirectOutput == out) {
        ALOGV(" deleting  mDirectOutput \n");
        delete mDirectOutput;
        mDirectOutput = 0;
    }
#endif
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
#ifdef QCOM_VOIP_ENABLED
    if(devices == AudioSystem::DEVICE_IN_COMMUNICATION) {
        ALOGE("Create Audio stream Voip \n");
        AudioStreamInVoip* inVoip = new AudioStreamInVoip();
        status_t lStatus = NO_ERROR;
        lStatus =  inVoip->set(this, devices, format, channels, sampleRate, acoustic_flags);
        if (status) {
            *status = lStatus;
        }
        if (lStatus != NO_ERROR) {
            ALOGE(" Error creating voip input \n");
            mLock.unlock();
            delete inVoip;
            return 0;
        }
        mVoipInputs.add(inVoip);
        mLock.unlock();
        return inVoip;
    } else
#endif
      {
        AudioStreamInMSM8x60* in8x60 = new AudioStreamInMSM8x60();
        status_t lStatus = in8x60->set(this, devices, format, channels, sampleRate, acoustic_flags);
        if (status) {
            *status = lStatus;
        }
        if (lStatus != NO_ERROR) {
            ALOGE("Error creating Audio stream AudioStreamInMSM8x60 \n");
            mLock.unlock();
            delete in8x60;
            return 0;
        }
        mInputs.add(in8x60);
        mLock.unlock();
        return in8x60;
    }
}

void AudioHardware::closeInputStream(AudioStreamIn* in) {
    Mutex::Autolock lock(mLock);

    ssize_t index = -1;
    if((index = mInputs.indexOf((AudioStreamInMSM8x60 *)in)) >= 0) {
        ALOGV("closeInputStream AudioStreamInMSM8x60");
        mLock.unlock();
        delete mInputs[index];
        mLock.lock();
        mInputs.removeAt(index);
    }
#ifdef QCOM_VOIP_ENABLED
    else if ((index = mVoipInputs.indexOf((AudioStreamInVoip *)in)) >= 0) {
        ALOGV("closeInputStream mVoipInputs");
        mLock.unlock();
        delete mVoipInputs[index];
        mLock.lock();
        mInputs.removeAt(index);
    }
#endif
    else {
        ALOGE("Attempt to close invalid input stream");
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
    int session_id = 0;
    if (mMicMute != state) {
        mMicMute = state;
        ALOGD("setMicMute_nosync calling voice mute with the mMicMute %d", mMicMute);
        if(isStreamOnAndActive(VOICE_CALL)) {
             session_id = voice_session_id;
             voice_session_mute = mMicMute;
#ifdef QCOM_VOIP_ENABLED
        } else if (isStreamOnAndActive(VOIP_CALL)) {
            session_id = voip_session_id;
            voip_session_mute = mMicMute;
#endif
        } else {
            ALOGE(" unknown voice stream");
            return -1;
        }
        msm_set_voice_tx_mute_ext(mMicMute,session_id);
    }
    return NO_ERROR;
}

status_t AudioHardware::getMicMute(bool* state)
{
    int session_id = 0;
    if(isStreamOnAndActive(VOICE_CALL)) {
          session_id = voice_session_id;
          *state = mMicMute = voice_session_mute;
#ifdef QCOM_VOIP_ENABLED
    } else if (isStreamOnAndActive(VOIP_CALL)) {
           session_id = voip_session_id;
           *state = mMicMute = voip_session_mute;
#endif
    } else
         *state = mMicMute;
    return NO_ERROR;
}

status_t AudioHardware::setParameters(const String8& keyValuePairs)
{
    AudioParameter param = AudioParameter(keyValuePairs);
    int rc = 0;
    String8 value;
    String8 key;
    const char BT_NREC_KEY[] = "bt_headset_nrec";
    const char BT_NAME_KEY[] = "bt_headset_name";
    const char BT_NREC_VALUE_ON[] = "on";
#ifdef QCOM_FM_ENABLED
    const char FM_NAME_KEY[] = "FMRadioOn";
    const char FM_VALUE_HANDSET[] = "handset";
    const char FM_VALUE_SPEAKER[] = "speaker";
    const char FM_VALUE_HEADSET[] = "headset";
    const char FM_VALUE_FALSE[] = "false";
#endif

    ALOGV("setParameters() %s", keyValuePairs.string());

    if (keyValuePairs.length() == 0) return BAD_VALUE;

    key = String8(BT_NREC_KEY);
    if (param.get(key, value) == NO_ERROR) {
        if (value == BT_NREC_VALUE_ON) {
            mBluetoothNrec = true;
        } else {
            mBluetoothNrec = false;
            ALOGI("Turning noise reduction and echo cancellation off for BT "
                 "headset");
        }
    }
    key = String8(BTHEADSET_VGS);
    if (param.get(key, value) == NO_ERROR) {
        if (value == BT_NREC_VALUE_ON) {
            mBluetoothVGS = true;
        } else {
            mBluetoothVGS = false;
        }
    }
    key = String8(BT_NAME_KEY);
    if (param.get(key, value) == NO_ERROR) {
        doRouting(NULL);
    }

    key = String8(DUALMIC_KEY);
    if (param.get(key, value) == NO_ERROR) {
        if (value == "true") {
            dualmic_enabled = true;
            ALOGI("DualMic feature Enabled");
        } else {
            dualmic_enabled = false;
            ALOGI("DualMic feature Disabled");
        }
        doRouting(NULL);
    }
#ifdef QCOM_ANC_HEADSET_ENABLED
    key = String8(ANC_KEY);
    if (param.get(key, value) == NO_ERROR) {
        if (value == "true") {
          ALOGE("Enabling ANC setting in the setparameter\n");
          anc_setting= true;
        } else {
           ALOGE("Disabling ANC setting in the setparameter\n");
           anc_setting= false;
           //disabling ANC feature.
           enableANC(0,cur_rx);
           anc_running = false;
        }
     doRouting(NULL);
    }
#endif

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
        ALOGI("Changed TTY Mode=%s", value.string());
        if((mMode == AudioSystem::MODE_IN_CALL) &&
           (cur_rx == DEVICE_HEADSET_RX) &&
           (cur_tx == DEVICE_HEADSET_TX))
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
        value = String8(dualmic_enabled ? "true" : "false");
        param.add(key, value);
    }
#ifdef QCOM_FM_ENABLED
    key = String8("Fm-radio");
    if ( param.get(key,value) == NO_ERROR ) {
        if ( getNodeByStreamType(FM_RADIO) ) {
            param.addInt(String8("isFMON"), true );
        }
    }
#endif
    key = String8(BTHEADSET_VGS);
    if (param.get(key, value) == NO_ERROR) {
        if(mBluetoothVGS)
           param.addInt(String8("isVGS"), true);
    }

    key = String8(ECHO_SUPRESSION);
    if (param.get(key, value) == NO_ERROR) {
        value = String8("yes");
        param.add(key, value);
    }
    ALOGV("AudioHardware::getParameters() %s", param.toString().string());
    return param.toString();
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
    if (format != AudioSystem::PCM_16_BIT){
        ALOGW("getInputBufferSize bad format: %d", format);
        return 0;
    }
    if (channelCount < 1 || channelCount > 2) {
        ALOGW("getInputBufferSize bad channel count: %d", channelCount);
        return 0;
    }

    if (sampleRate == 8000) {
       return 320*channelCount;
    } else if (sampleRate == 16000){
       return 640*channelCount;
    } else {
        /*
            Return pcm record buffer size based on the sampling rate:
            If sampling rate >= 44.1 Khz, use 512 samples/channel pcm recording and
            If sampling rate < 44.1 Khz, use 256 samples/channel pcm recording
        */
       if(sampleRate >= 44100){
           return 1024*channelCount;
       } else {
           return 512*channelCount;
       }
    }
}

static status_t set_volume_rpc(uint32_t device,
                               uint32_t method,
                               uint32_t volume)
{
    ALOGV("set_volume_rpc(%d, %d, %d)\n", device, method, volume);

    if (device == -1UL) return NO_ERROR;
     return NO_ERROR;
}

status_t AudioHardware::setVoiceVolume(float v)
{
    int session_id = 0;
    if (v < 0.0) {
        ALOGW("setVoiceVolume(%f) under 0.0, assuming 0.0\n", v);
        v = 0.0;
    } else if (v > 1.0) {
        ALOGW("setVoiceVolume(%f) over 1.0, assuming 1.0\n", v);
        v = 1.0;
    }

    if(isStreamOnAndActive(VOICE_CALL)) {
        session_id = voice_session_id;
    }
#ifdef QCOM_VOIP_ENABLED
    else if (isStreamOnAndActive(VOIP_CALL)) {
        session_id = voip_session_id;
    }
#endif
    else {
        ALOGE(" unknown stream ");
        return -1;
    }
    int vol = lrint(v * 100.0);
    // Voice volume levels from android are mapped to driver volume levels as follows.
    // 0 -> 5, 20 -> 4, 40 ->3, 60 -> 2, 80 -> 1, 100 -> 0
    vol = 5 - (vol/20);
    ALOGD("setVoiceVolume(%f)\n", v);
    ALOGI("Setting in-call volume to %d (available range is 5(MIN VOLUME)  to 0(MAX VOLUME)\n", vol);

    if(msm_set_voice_rx_vol_ext(vol,session_id)) {
        ALOGE("msm_set_voice_rx_vol(%d) failed errno = %d",vol,errno);
        return -1;
    }
    ALOGV("msm_set_voice_rx_vol(%d) succeeded session_id %d",vol,session_id);
    return NO_ERROR;
}

#ifdef QCOM_FM_ENABLED
status_t AudioHardware::setFmVolume(float v)
{
    int vol = android::AudioSystem::logToLinear( (v?(v + 0.005):v) );
    if ( vol > 100 ) {
        vol = 100;
    }
    else if ( vol < 0 ) {
        vol = 0;
    }
    ALOGV("setFmVolume(%f)\n", v);
    ALOGV("Setting FM volume to %d (available range is 0 to 100)\n", vol);
    Routing_table* temp = NULL;
    temp = getNodeByStreamType(FM_RADIO);
    if(temp == NULL){
        ALOGV("No Active FM stream is running");
        return NO_ERROR;
    }
    if(msm_set_volume(temp->dec_id, vol)) {
        ALOGE("msm_set_volume(%d) failed for FM errno = %d",vol,errno);
        return -1;
    }
    ALOGV("msm_set_volume(%d) for FM succeeded",vol);
    return NO_ERROR;
}
#endif

status_t AudioHardware::setMasterVolume(float v)
{
    Mutex::Autolock lock(mLock);
    int vol = ceil(v * 7.0);
    ALOGI("Set master volume to %d.\n", vol);

    set_volume_rpc(SND_DEVICE_HANDSET, SND_METHOD_VOICE, vol);
    set_volume_rpc(SND_DEVICE_SPEAKER, SND_METHOD_VOICE, vol);
    set_volume_rpc(SND_DEVICE_BT,      SND_METHOD_VOICE, vol);
    set_volume_rpc(SND_DEVICE_HEADSET, SND_METHOD_VOICE, vol);
    //TBD - does HDMI require this handling

    // We return an error code here to let the audioflinger do in-software
    // volume on top of the maximum volume that we set through the SND API.
    // return error - software mixer will handle it
    return -1;
}

static status_t do_route_audio_rpc(uint32_t device,
                                   int mode, bool mic_mute)
{
    if(device == -1)
        return 0;

    int new_rx_device = INVALID_DEVICE,new_tx_device = INVALID_DEVICE,fm_device = INVALID_DEVICE;
    Routing_table* temp = NULL;
    ALOGV("do_route_audio_rpc(%d, %d, %d)", device, mode, mic_mute);

    if(device == SND_DEVICE_HANDSET) {
        new_rx_device = DEVICE_HANDSET_RX;
        new_tx_device = DEVICE_HANDSET_TX;
        ALOGV("In HANDSET");
    }
    else if(device == SND_DEVICE_SPEAKER) {
        new_rx_device = DEVICE_SPEAKER_RX;
        new_tx_device = DEVICE_SPEAKER_TX;
        ALOGV("In SPEAKER");
    }
    else if(device == SND_DEVICE_HEADSET) {
        new_rx_device = DEVICE_HEADSET_RX;
        new_tx_device = DEVICE_HEADSET_TX;
        ALOGV("In HEADSET");
    }
    else if(device == SND_DEVICE_NO_MIC_HEADSET) {
        new_rx_device = DEVICE_HEADSET_RX;
        new_tx_device = DEVICE_HANDSET_TX;
        ALOGV("In NO MIC HEADSET");
    }
#ifdef QCOM_FM_ENABLED
    else if (device == SND_DEVICE_FM_HANDSET) {
        fm_device = DEVICE_FMRADIO_HANDSET_RX;
        ALOGV("In FM HANDSET");
    }
    else if(device == SND_DEVICE_FM_SPEAKER) {
        fm_device = DEVICE_FMRADIO_SPEAKER_RX;
        ALOGV("In FM SPEAKER");
    }
    else if(device == SND_DEVICE_FM_HEADSET) {
        fm_device = DEVICE_FMRADIO_HEADSET_RX;
        ALOGV("In FM HEADSET");
    }
#endif
    else if(device == SND_DEVICE_IN_S_SADC_OUT_HANDSET) {
        new_rx_device = DEVICE_HANDSET_RX;
        new_tx_device = DEVICE_DUALMIC_HANDSET_TX;
        ALOGV("In DUALMIC_HANDSET");
        if(DEV_ID(new_tx_device) == INVALID_DEVICE) {
            new_tx_device = DEVICE_HANDSET_TX;
            ALOGV("Falling back to HANDSET_RX AND HANDSET_TX as no DUALMIC_HANDSET_TX support found");
        }
    }
    else if(device == SND_DEVICE_IN_S_SADC_OUT_SPEAKER_PHONE) {
        new_rx_device = DEVICE_SPEAKER_RX;
        new_tx_device = DEVICE_DUALMIC_SPEAKER_TX;
        ALOGV("In DUALMIC_SPEAKER");
        if(DEV_ID(new_tx_device) == INVALID_DEVICE) {
            new_tx_device = DEVICE_SPEAKER_TX;
            ALOGV("Falling back to SPEAKER_RX AND SPEAKER_TX as no DUALMIC_SPEAKER_TX support found");
        }
    }
    else if(device == SND_DEVICE_TTY_FULL) {
        new_rx_device = DEVICE_TTY_HEADSET_MONO_RX;
        new_tx_device = DEVICE_TTY_HEADSET_MONO_TX;
        ALOGV("In TTY_FULL");
    }
    else if(device == SND_DEVICE_TTY_VCO) {
        new_rx_device = DEVICE_TTY_HEADSET_MONO_RX;
        new_tx_device = DEVICE_HANDSET_TX;
        ALOGV("In TTY_VCO");
    }
    else if(device == SND_DEVICE_TTY_HCO) {
        new_rx_device = DEVICE_HANDSET_RX;
        new_tx_device = DEVICE_TTY_HEADSET_MONO_TX;
        ALOGV("In TTY_HCO");
    }
    else if(device == SND_DEVICE_BT) {
        new_rx_device = DEVICE_BT_SCO_RX;
        new_tx_device = DEVICE_BT_SCO_TX;
        ALOGV("In BT_HCO");
    }
    else if(device == SND_DEVICE_HEADSET_AND_SPEAKER) {
        new_rx_device = DEVICE_SPEAKER_HEADSET_RX;
        new_tx_device = DEVICE_HEADSET_TX;
        ALOGV("In DEVICE_SPEAKER_HEADSET_RX and DEVICE_HEADSET_TX");
        if(DEV_ID(new_rx_device) == INVALID_DEVICE) {
             new_rx_device = DEVICE_HEADSET_RX;
             ALOGV("Falling back to HEADSET_RX AND HEADSET_TX as no combo device support found");
        }
    }
    else if(device == SND_DEVICE_HEADPHONE_AND_SPEAKER) {
        new_rx_device = DEVICE_SPEAKER_HEADSET_RX;
        new_tx_device = DEVICE_HANDSET_TX;
        ALOGV("In DEVICE_SPEAKER_HEADSET_RX and DEVICE_HANDSET_TX");
        if(DEV_ID(new_rx_device) == INVALID_DEVICE) {
             new_rx_device = DEVICE_HEADSET_RX;
             ALOGV("Falling back to HEADSET_RX AND HANDSET_TX as no combo device support found");
        }
    }
    else if (device == SND_DEVICE_HDMI) {
        new_rx_device = DEVICE_HDMI_STERO_RX;
        new_tx_device = cur_tx;
        ALOGI("In DEVICE_HDMI_STERO_RX and cur_tx");
    }
#ifdef QCOM_ANC_HEADSET_ENABLED
    else if(device == SND_DEVICE_ANC_HEADSET) {
        new_rx_device = DEVICE_ANC_HEADSET_STEREO_RX;
        new_tx_device = DEVICE_HEADSET_TX;
        ALOGI("In ANC HEADSET");
    }
    else if(device == SND_DEVICE_NO_MIC_ANC_HEADSET) {
        new_rx_device = DEVICE_ANC_HEADSET_STEREO_RX;
        new_tx_device = DEVICE_HANDSET_TX;
        ALOGI("In ANC HEADPhone");
    }
#endif
#ifdef QCOM_FM_ENABLED
    else if(device == SND_DEVICE_FM_TX){
        new_rx_device = DEVICE_FMRADIO_STEREO_RX;
        ALOGI("In DEVICE_FMRADIO_STEREO_RX and cur_tx");
    }
#endif
    else if(device == SND_DEVICE_SPEAKER_TX) {
        new_rx_device = cur_rx;
        new_tx_device = DEVICE_SPEAKER_TX;
        ALOGI("In SPEAKER_TX cur_rx = %d\n", cur_rx);
    }

    if(new_rx_device != INVALID_DEVICE)
        ALOGD("new_rx = %d", DEV_ID(new_rx_device));
    if(new_tx_device != INVALID_DEVICE)
        ALOGD("new_tx = %d", DEV_ID(new_tx_device));

    if ((mode == AudioSystem::MODE_IN_CALL) && !isStreamOn(VOICE_CALL)) {
        ALOGV("Going to enable RX/TX device for voice stream");
            // Routing Voice
            if ( (new_rx_device != INVALID_DEVICE) && (new_tx_device != INVALID_DEVICE))
            {
#ifdef QCOM_ACDB_ENABLED
                initACDB();
                acdb_loader_send_voice_cal(ACDB_ID(new_rx_device),ACDB_ID(new_tx_device));
#endif
                ALOGD("Starting voice on Rx %d and Tx %d device", DEV_ID(new_rx_device), DEV_ID(new_tx_device));
                msm_route_voice(DEV_ID(new_rx_device),DEV_ID(new_tx_device), 1);
            }
            else
            {
                return -1;
            }

            if(cur_rx != INVALID_DEVICE && (enableDevice(cur_rx,0) == -1))
                    return -1;

            if(cur_tx != INVALID_DEVICE&&(enableDevice(cur_tx,0) == -1))
                    return -1;

           //Enable RX device
           if(new_rx_device !=INVALID_DEVICE && (enableDevice(new_rx_device,1) == -1))
               return -1;
            //Enable TX device
           if(new_tx_device !=INVALID_DEVICE && (enableDevice(new_tx_device,1) == -1))
               return -1;
           voice_session_id = msm_get_voc_session(VOICE_SESSION_NAME);
           if(voice_session_id <=0) {
                ALOGE("voice session invalid");
                return 0;
           }
           msm_start_voice_ext(voice_session_id);
           msm_set_voice_tx_mute_ext(voice_session_mute,voice_session_id);

           if(!isDeviceListEmpty())
               updateDeviceInfo(new_rx_device,new_tx_device);
            cur_rx = new_rx_device;
            cur_tx = new_tx_device;
            addToTable(0,cur_rx,cur_tx,VOICE_CALL,true);
    }
    else if ((mode == AudioSystem::MODE_NORMAL) && isStreamOnAndActive(VOICE_CALL)) {
        ALOGV("Going to disable RX/TX device during end of voice call");
        temp = getNodeByStreamType(VOICE_CALL);
        if(temp == NULL)
            return 0;

        // Ending voice call
        ALOGD("Ending Voice call");
        msm_end_voice_ext(voice_session_id);
        voice_session_id = 0;
        voice_session_mute = 0;

        if((temp->dev_id != INVALID_DEVICE && temp->dev_id_tx != INVALID_DEVICE)
#ifdef QCOM_VOIP_ENABLED
        && (!isStreamOn(VOIP_CALL))
#endif
        ) {
           enableDevice(temp->dev_id,0);
           enableDevice(temp->dev_id_tx,0);
        }
        deleteFromTable(VOICE_CALL);
        updateDeviceInfo(new_rx_device,new_tx_device);
        if(new_rx_device != INVALID_DEVICE && new_tx_device != INVALID_DEVICE) {
            cur_rx = new_rx_device;
            cur_tx = new_tx_device;
        }
    }
    else {
        ALOGD("updateDeviceInfo() called for default case");
        updateDeviceInfo(new_rx_device,new_tx_device);
    }
    return NO_ERROR;
}

// always call with mutex held
status_t AudioHardware::doAudioRouteOrMute(uint32_t device)
{
// BT acoustics is not supported. This might be used by OEMs. Hence commenting
// the code and not removing it.
#if 0
    if (device == (uint32_t)SND_DEVICE_BT || device == (uint32_t)SND_DEVICE_CARKIT) {
        if (mBluetoothId) {
            device = mBluetoothId;
        } else if (!mBluetoothNrec) {
            device = SND_DEVICE_BT_EC_OFF;
        }
    }
#endif

    ALOGV("doAudioRouteOrMute() device %x, mMode %d, mMicMute %d", device, mMode, mMicMute);
    return do_route_audio_rpc(device, mMode, mMicMute);
}

status_t AudioHardware::doRouting(AudioStreamInMSM8x60 *input)
{
    Mutex::Autolock lock(mLock);
    uint32_t outputDevices = mOutput->devices();
    status_t ret = NO_ERROR;
    int audProcess = (ADRC_DISABLE | EQ_DISABLE | RX_IIR_DISABLE);
    int sndDevice = -1;



    if (input != NULL) {
        uint32_t inputDevice = input->devices();
        ALOGI("do input routing device %x\n", inputDevice);
        // ignore routing device information when we start a recording in voice
        // call
        // Recording will happen through currently active tx device
        if((inputDevice == AudioSystem::DEVICE_IN_VOICE_CALL)
#ifdef QCOM_FM_ENABLED
           || (inputDevice == AudioSystem::DEVICE_IN_FM_RX)
           || (inputDevice == AudioSystem::DEVICE_IN_FM_RX_A2DP)
#endif
        )
            return NO_ERROR;
        if (inputDevice != 0) {
            if (inputDevice & AudioSystem::DEVICE_IN_BLUETOOTH_SCO_HEADSET) {
                ALOGI("Routing audio to Bluetooth PCM\n");
                sndDevice = SND_DEVICE_BT;
            } else if (inputDevice & AudioSystem::DEVICE_IN_WIRED_HEADSET) {
                if ((outputDevices & AudioSystem::DEVICE_OUT_WIRED_HEADSET) &&
                    (outputDevices & AudioSystem::DEVICE_OUT_SPEAKER)) {
                    ALOGI("Routing audio to Wired Headset and Speaker\n");
                    sndDevice = SND_DEVICE_HEADSET_AND_SPEAKER;
                    audProcess = (ADRC_ENABLE | EQ_ENABLE | RX_IIR_ENABLE | MBADRC_ENABLE);
                } else {
                    ALOGI("Routing audio to Wired Headset\n");
                    sndDevice = SND_DEVICE_HEADSET;
                }
            }
#ifdef QCOM_ANC_HEADSET_ENABLED
            else if (inputDevice & AudioSystem::DEVICE_IN_ANC_HEADSET) {
                    ALOGI("Routing audio to ANC Headset\n");
                    sndDevice = SND_DEVICE_ANC_HEADSET;
                }
#endif
            else if (isStreamOnAndActive(PCM_PLAY)
#ifdef QCOM_TUNNEL_LPA_ENABLED
                     || isStreamOnAndActive(LPA_DECODE)
#endif
              ) {
                if (outputDevices & AudioSystem::DEVICE_OUT_SPEAKER) {
                    ALOGI("Routing audio to Speakerphone\n");
                    sndDevice = SND_DEVICE_SPEAKER;
                } else if (outputDevices & AudioSystem::DEVICE_OUT_WIRED_HEADPHONE) {
                    ALOGI("Routing audio to Speakerphone\n");
                    sndDevice = SND_DEVICE_NO_MIC_HEADSET;
                }
#ifdef QCOM_FM_ENABLED
                 else if (outputDevices & AudioSystem::DEVICE_OUT_FM_TX) {
                    ALOGE("Routing audio_rx to Speaker\n");
                    sndDevice = SND_DEVICE_SPEAKER_TX;
                }
#endif
                 else {
                    ALOGI("Routing audio to Handset\n");
                    sndDevice = SND_DEVICE_HANDSET;
                }
            } else {
                ALOGI("Routing audio to Handset\n");
                sndDevice = SND_DEVICE_HANDSET;
            }
        }
        // if inputDevice == 0, restore output routing
    }

    if (sndDevice == -1) {
        if (outputDevices & (outputDevices - 1)) {
            if ((outputDevices & AudioSystem::DEVICE_OUT_SPEAKER) == 0) {
                ALOGW("Hardware does not support requested route combination (%#X),"
                     " picking closest possible route...", outputDevices);
            }
        }
        if ((mTtyMode != TTY_OFF) && (mMode == AudioSystem::MODE_IN_CALL) &&
                ((outputDevices & AudioSystem::DEVICE_OUT_WIRED_HEADSET)
#ifdef QCOM_ANC_HEADSET_ENABLED
                 ||(outputDevices & AudioSystem::DEVICE_OUT_ANC_HEADSET)
#endif
            )) {
            if (mTtyMode == TTY_FULL) {
                ALOGI("Routing audio to TTY FULL Mode\n");
                sndDevice = SND_DEVICE_TTY_FULL;
            } else if (mTtyMode == TTY_VCO) {
                ALOGI("Routing audio to TTY VCO Mode\n");
                sndDevice = SND_DEVICE_TTY_VCO;
            } else if (mTtyMode == TTY_HCO) {
                ALOGI("Routing audio to TTY HCO Mode\n");
                sndDevice = SND_DEVICE_TTY_HCO;
            }
        } else if (outputDevices &
                   (AudioSystem::DEVICE_OUT_BLUETOOTH_SCO | AudioSystem::DEVICE_OUT_BLUETOOTH_SCO_HEADSET)) {
            ALOGI("Routing audio to Bluetooth PCM\n");
            sndDevice = SND_DEVICE_BT;
        } else if (outputDevices & AudioSystem::DEVICE_OUT_BLUETOOTH_SCO_CARKIT) {
            ALOGI("Routing audio to Bluetooth PCM\n");
            sndDevice = SND_DEVICE_CARKIT;
        } else if (outputDevices & AudioSystem::DEVICE_OUT_AUX_DIGITAL) {
            ALOGI("Routing audio to HDMI\n");
            sndDevice = SND_DEVICE_HDMI;
        } else if ((outputDevices & AudioSystem::DEVICE_OUT_WIRED_HEADSET) &&
                   (outputDevices & AudioSystem::DEVICE_OUT_SPEAKER)) {
            ALOGI("Routing audio to Wired Headset and Speaker\n");
            sndDevice = SND_DEVICE_HEADSET_AND_SPEAKER;
            audProcess = (ADRC_ENABLE | EQ_ENABLE | RX_IIR_ENABLE | MBADRC_ENABLE);
        } else 
#ifdef QCOM_FM_ENABLED
          if ((outputDevices & AudioSystem::DEVICE_OUT_FM_TX) &&
                   (outputDevices & AudioSystem::DEVICE_OUT_SPEAKER)) {
            ALOGI("Routing audio to FM Tx and Speaker\n");
            sndDevice = SND_DEVICE_FM_TX_AND_SPEAKER;
            enableComboDevice(sndDevice,1);
            audProcess = (ADRC_ENABLE | EQ_ENABLE | RX_IIR_ENABLE | MBADRC_ENABLE);
        }  else
#endif
          if (outputDevices & AudioSystem::DEVICE_OUT_WIRED_HEADPHONE) {
            if (outputDevices & AudioSystem::DEVICE_OUT_SPEAKER) {
                ALOGI("Routing audio to No microphone Wired Headset and Speaker (%d,%x)\n", mMode, outputDevices);
                sndDevice = SND_DEVICE_HEADPHONE_AND_SPEAKER;
                audProcess = (ADRC_ENABLE | EQ_ENABLE | RX_IIR_ENABLE | MBADRC_ENABLE);
            } else {
                ALOGI("Routing audio to No microphone Wired Headset (%d,%x)\n", mMode, outputDevices);
                sndDevice = SND_DEVICE_NO_MIC_HEADSET;
                audProcess = (ADRC_ENABLE | EQ_ENABLE | RX_IIR_ENABLE | MBADRC_ENABLE);
            }
        }
#ifdef QCOM_ANC_HEADSET_ENABLED
             else if (outputDevices & AudioSystem::DEVICE_OUT_ANC_HEADPHONE) {
                ALOGI("Routing audio to No microphone ANC Headset (%d,%x)\n", mMode, outputDevices);
                sndDevice = SND_DEVICE_NO_MIC_ANC_HEADSET;
                audProcess = (ADRC_ENABLE | EQ_ENABLE | RX_IIR_ENABLE | MBADRC_ENABLE);
        }
#endif
         else if (outputDevices & AudioSystem::DEVICE_OUT_WIRED_HEADSET) {
             ALOGI("Routing audio to Wired Headset\n");
             sndDevice = SND_DEVICE_HEADSET;
             audProcess = (ADRC_ENABLE | EQ_ENABLE | RX_IIR_ENABLE | MBADRC_ENABLE);
        }
#ifdef QCOM_ANC_HEADSET_ENABLED
          else if (outputDevices & AudioSystem::DEVICE_OUT_ANC_HEADSET) {
            ALOGI("Routing audio to ANC Headset\n");
            sndDevice = SND_DEVICE_ANC_HEADSET;
            audProcess = (ADRC_ENABLE | EQ_ENABLE | RX_IIR_ENABLE | MBADRC_ENABLE);
        }
#endif
          else if (outputDevices & AudioSystem::DEVICE_OUT_SPEAKER) {
            ALOGI("Routing audio to Speakerphone\n");
            sndDevice = SND_DEVICE_SPEAKER;
            audProcess = (ADRC_ENABLE | EQ_ENABLE | RX_IIR_ENABLE | MBADRC_ENABLE);
        } else
#ifdef QCOM_FM_ENABLED
         if (outputDevices & AudioSystem::DEVICE_OUT_FM_TX){
            ALOGI("Routing audio to FM Tx Device\n");
            sndDevice = SND_DEVICE_FM_TX;
            audProcess = (ADRC_ENABLE | EQ_ENABLE | RX_IIR_ENABLE | MBADRC_ENABLE);
        } else
#endif
          if(outputDevices & AudioSystem::DEVICE_OUT_EARPIECE){
            ALOGI("Routing audio to Handset\n");
            sndDevice = SND_DEVICE_HANDSET;
            audProcess = (ADRC_ENABLE | EQ_ENABLE | RX_IIR_ENABLE | MBADRC_ENABLE);
        }
    }

    if (dualmic_enabled) {
        if (sndDevice == SND_DEVICE_HANDSET) {
            ALOGI("Routing audio to handset with DualMike enabled\n");
            sndDevice = SND_DEVICE_IN_S_SADC_OUT_HANDSET;
        } else if (sndDevice == SND_DEVICE_SPEAKER) {
            ALOGI("Routing audio to speakerphone with DualMike enabled\n");
            sndDevice = SND_DEVICE_IN_S_SADC_OUT_SPEAKER_PHONE;
        }
    }
#ifdef QCOM_FM_ENABLED
    if ((outputDevices & AudioSystem::DEVICE_OUT_FM) && (mFmFd == -1)){
        enableFM(sndDevice);
    }
    if ((mFmFd != -1) && !(outputDevices & AudioSystem::DEVICE_OUT_FM)){
        disableFM();
    }

    if ((CurrentComboDeviceData.DeviceId == INVALID_DEVICE) &&
        (sndDevice == SND_DEVICE_FM_TX_AND_SPEAKER )){
        /* speaker rx is already enabled change snd device to the fm tx
         * device and let the flow take the regular route to
         * updatedeviceinfo().
         */
        Mutex::Autolock lock_1(mComboDeviceLock);

        CurrentComboDeviceData.DeviceId = SND_DEVICE_FM_TX_AND_SPEAKER;
        sndDevice = DEVICE_FMRADIO_STEREO_RX;
    }
    else
#endif
    if(CurrentComboDeviceData.DeviceId != INVALID_DEVICE){
        /* time to disable the combo device */
        enableComboDevice(CurrentComboDeviceData.DeviceId,0);
        Mutex::Autolock lock_2(mComboDeviceLock);
        CurrentComboDeviceData.DeviceId = INVALID_DEVICE;
        CurrentComboDeviceData.StreamType = INVALID_STREAM;
    }

    if (sndDevice != -1 && sndDevice != mCurSndDevice) {
        ret = doAudioRouteOrMute(sndDevice);
        mCurSndDevice = sndDevice;
    }
#ifdef QCOM_ANC_HEADSET_ENABLED
    //check if ANC setting is ON
    if (anc_setting == true
                && (sndDevice == SND_DEVICE_ANC_HEADSET
                || sndDevice ==SND_DEVICE_NO_MIC_ANC_HEADSET)) {
        enableANC(1,sndDevice);
        anc_running = true;
    } else {
        //disconnection case
        anc_running = false;
    }
#endif
    return ret;
}

status_t AudioHardware::enableComboDevice(uint32_t sndDevice, bool enableOrDisable)
{
    ALOGD("enableComboDevice %u",enableOrDisable);
    status_t status = NO_ERROR;
#ifdef QCOM_TUNNEL_LPA_ENABLED
    Routing_table *LpaNode = getNodeByStreamType(LPA_DECODE);
#endif
    Routing_table *PcmNode = getNodeByStreamType(PCM_PLAY);

    if(enableDevice(DEVICE_SPEAKER_RX, enableOrDisable)) {
         ALOGE("enableDevice failed for device %d", DEVICE_SPEAKER_RX);
         return -1;
    }
#ifdef QCOM_FM_ENABLED
    if(SND_DEVICE_FM_TX_AND_SPEAKER == sndDevice){

        if(getNodeByStreamType(VOICE_CALL) || getNodeByStreamType(FM_RADIO) ||
           getNodeByStreamType(FM_A2DP)){
            ALOGE("voicecall/FM radio active bailing out");
            return NO_ERROR;
        }

        if(!LpaNode && !PcmNode) {
            ALOGE("No active playback session active bailing out ");
            cur_rx = DEVICE_FMRADIO_STEREO_RX;
            return NO_ERROR;
        }

        Mutex::Autolock lock_1(mComboDeviceLock);

        Routing_table* temp = NULL;

        if (enableOrDisable == 1) {
            if(CurrentComboDeviceData.StreamType == INVALID_STREAM){
                if (PcmNode){
                    temp = PcmNode;
                    CurrentComboDeviceData.StreamType = PCM_PLAY;
                    ALOGD("PCM_PLAY session Active ");
                }else if(LpaNode){
                    temp = LpaNode;
                    CurrentComboDeviceData.StreamType = LPA_DECODE;
                    ALOGD("LPA_DECODE session Active ");
                } else {
                    ALOGE("no PLAYback session Active ");
                    return -1;
                }
            }else
                temp = getNodeByStreamType(CurrentComboDeviceData.StreamType);

            if(temp == NULL){
                ALOGE("speaker de-route not possible");
                return -1;
            }

            ALOGD("combo:msm_route_stream(%d,%d,1)",temp->dec_id,
                DEV_ID(DEVICE_SPEAKER_RX));
            if(msm_route_stream(PCM_PLAY, temp->dec_id, DEV_ID(DEVICE_SPEAKER_RX),
                1)) {
                ALOGE("msm_route_stream failed");
                return -1;
            }

        }else if(enableOrDisable == 0) {
            temp = getNodeByStreamType(CurrentComboDeviceData.StreamType);


            if(temp == NULL){
                ALOGE("speaker de-route not possible");
                return -1;
            }

            ALOGD("combo:de-route msm_route_stream(%d,%d,0)",temp->dec_id,
                DEV_ID(DEVICE_SPEAKER_RX));
            if(msm_route_stream(PCM_PLAY, temp->dec_id,
                DEV_ID(DEVICE_SPEAKER_RX), 0)) {
                ALOGE("msm_route_stream failed");
                return -1;
            }
        }

    }
#endif

    return status;
}
#ifdef QCOM_FM_ENABLED
status_t AudioHardware::enableFM(int sndDevice)
{
    ALOGD("enableFM");
    status_t status = NO_INIT;
    unsigned short session_id = INVALID_DEVICE;
    status = ::open(FM_DEVICE, O_RDWR);
    if (status < 0) {
           ALOGE("Cannot open FM_DEVICE errno: %d", errno);
           goto Error;
    }
    mFmFd = status;
    if(ioctl(mFmFd, AUDIO_GET_SESSION_ID, &session_id)) {
           ALOGE("AUDIO_GET_SESSION_ID failed*********");
           goto Error;
    }

    if(enableDevice(DEVICE_FMRADIO_STEREO_TX, 1)) {
           ALOGE("enableDevice failed for device %d", DEVICE_FMRADIO_STEREO_TX);
           goto Error;
    }
#ifdef QCOM_ACDB_ENABLED
    acdb_loader_send_audio_cal(ACDB_ID(DEVICE_FMRADIO_STEREO_TX),
    CAPABILITY(DEVICE_FMRADIO_STEREO_TX));
#endif
    if(msm_route_stream(PCM_PLAY, session_id, DEV_ID(DEVICE_FMRADIO_STEREO_TX), 1)) {
           ALOGE("msm_route_stream failed");
           goto Error;
    }
    addToTable(session_id,cur_rx,INVALID_DEVICE,FM_RADIO,true);
    if(sndDevice == mCurSndDevice || mCurSndDevice == -1) {
        enableDevice(cur_rx, 1);
#ifdef QCOM_ACDB_ENABLED
        acdb_loader_send_audio_cal(ACDB_ID(cur_rx), CAPABILITY(cur_rx));
#endif
        msm_route_stream(PCM_PLAY,session_id,DEV_ID(cur_rx),1);
    }
    status = ioctl(mFmFd, AUDIO_START, 0);
    if (status < 0) {
            ALOGE("Cannot do AUDIO_START");
            goto Error;
    }
    return NO_ERROR;
    Error:
    if (mFmFd >= 0) {
        ::close(mFmFd);
        mFmFd = -1;
    }
    return NO_ERROR;
}

status_t AudioHardware::disableFM()
{
    ALOGD("disableFM");
    Routing_table* temp = NULL;
    temp = getNodeByStreamType(FM_RADIO);
    if(temp == NULL)
        return 0;
    if (mFmFd >= 0) {
            ::close(mFmFd);
            mFmFd = -1;
    }
    if(msm_route_stream(PCM_PLAY, temp->dec_id, DEV_ID(DEVICE_FMRADIO_STEREO_TX), 0)) {
           ALOGE("msm_route_stream failed");
           return 0;
    }
    if(!getNodeByStreamType(FM_A2DP)) {
       if(enableDevice(DEVICE_FMRADIO_STEREO_TX, 0)) {
          ALOGE("Device disable failed for device %d", DEVICE_FMRADIO_STEREO_TX);
       }
    }
    deleteFromTable(FM_RADIO);
    if(!getNodeByStreamType(VOICE_CALL) && !getNodeByStreamType(LPA_DECODE)
        && !getNodeByStreamType(PCM_PLAY) && !getNodeByStreamType(VOIP_CALL)) {
        if(enableDevice(cur_rx, 0)) {
            ALOGV("Disable device[%d] failed errno = %d",DEV_ID(cur_rx),errno);
            return 0;
        }
    }
    return NO_ERROR;
}
#endif

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


// ----------------------------------------------------------------------------

AudioHardware::AudioSessionOutMSM8x60::AudioSessionOutMSM8x60() :
    mHardware(0), mStartCount(0), mRetryCount(0), mStandby(true), mDevices(0), mSessionId(-1)
{
    ALOGD("AudioSessionOutMSM8x60:: Constructor");
}

status_t AudioHardware::AudioSessionOutMSM8x60::set(
        AudioHardware* hw, uint32_t devices, int *pFormat, int32_t sessionId)
{
    int lFormat = pFormat ? *pFormat : 0;

    mHardware = hw;
    mDevices = devices;

    if(sessionId >= 0) {
        Mutex::Autolock lock(mDeviceSwitchLock);
#ifdef QCOM_TUNNEL_LPA_ENABLED
        if(getNodeByStreamType(LPA_DECODE) != NULL) {
            ALOGE("Not allowed, There is alread an LPA Node existing");
            return -1;
        }
        ALOGD("AudioSessionOutMSM8x60::set() Adding LPA_DECODE Node to Table");
        addToTable(sessionId,cur_rx,INVALID_DEVICE,LPA_DECODE,true);
#endif
        ALOGV("enableDevice(cur_rx = %d, dev_id = %d)",cur_rx,DEV_ID(cur_rx));
        if(enableDevice(cur_rx, 1)) {
            ALOGE("enableDevice failed for device cur_rx %d", cur_rx);
            return -1;
        }

        ALOGV("msm_route_stream(PCM_PLAY,%d,%d,0)",sessionId,DEV_ID(cur_rx));
#ifdef QCOM_ACDB_ENABLED
        acdb_loader_send_audio_cal(ACDB_ID(cur_rx), CAPABILITY(cur_rx));
#endif
        if(msm_route_stream(PCM_PLAY,sessionId,DEV_ID(cur_rx),1)) {
            ALOGE("msm_route_stream(PCM_PLAY,%d,%d,1) failed",sessionId,DEV_ID(cur_rx));
            return -1;
        }

        Mutex::Autolock lock_1(mComboDeviceLock);
#ifdef QCOM_FM_ENABLED
        if(CurrentComboDeviceData.DeviceId == SND_DEVICE_FM_TX_AND_SPEAKER){
            ALOGD("Routing LPA steam to speaker for combo device");
            ALOGD("combo:msm_route_stream(LPA_DECODE,session id:%d,dev id:%d,1)",sessionId,
                DEV_ID(DEVICE_SPEAKER_RX));
                /* music session is already routed to speaker in
                 * enableComboDevice(), but at this point it can
                 * be said that it was done with incorrect session id,
                 * so re-routing with correct session id here.
                 */
            if(msm_route_stream(PCM_PLAY, sessionId, DEV_ID(DEVICE_SPEAKER_RX),
               1)) {
                ALOGE("msm_route_stream failed");
                return -1;
            }
            CurrentComboDeviceData.StreamType = LPA_DECODE;
        }
#endif

        mSessionId = sessionId;
    }
    return NO_ERROR;
}

AudioHardware::AudioSessionOutMSM8x60::~AudioSessionOutMSM8x60()
{
}

status_t AudioHardware::AudioSessionOutMSM8x60::standby()
{
    Routing_table* temp = NULL;
    ALOGD("AudioSessionOutMSM8x60::standby()");
    status_t status = NO_ERROR;
#ifdef QCOM_TUNNEL_LPA_ENABLED
    temp = getNodeByStreamType(LPA_DECODE);

    if(temp == NULL)
        return NO_ERROR;

    ALOGD("Deroute lpa playback stream");
    if(msm_route_stream(PCM_PLAY, temp->dec_id,DEV_ID(temp->dev_id), 0)) {
        ALOGE("could not set stream routing\n");
        deleteFromTable(LPA_DECODE);
        return -1;
    }
    deleteFromTable(LPA_DECODE);
#endif
    if (!getNodeByStreamType(VOICE_CALL) && !getNodeByStreamType(PCM_PLAY)
#ifdef QCOM_FM_ENABLED
        && !getNodeByStreamType(FM_RADIO)
#endif
#ifdef QCOM_VOIP_ENABLED
        && !getNodeByStreamType(VOIP_CALL)
#endif
       ) {
#ifdef QCOM_ANC_HEADSET_ENABLED
        if (anc_running == false) {
#endif
            if (enableDevice(cur_rx, 0)) {
                ALOGE("Disabling device failed for cur_rx %d", cur_rx);
                return 0;
            }
#ifdef QCOM_ANC_HEADSET_ENABLED
        }
#endif
    }

    mStandby = true;
    return status;
}

bool AudioHardware::AudioSessionOutMSM8x60::checkStandby()
{
    return mStandby;
}


status_t AudioHardware::AudioSessionOutMSM8x60::setParameters(const String8& keyValuePairs)
{
    AudioParameter param = AudioParameter(keyValuePairs);
    String8 key = String8(AudioParameter::keyRouting);
    status_t status = NO_ERROR;
    int device;
    ALOGV("AudioSessionOutMSM8x60::setParameters() %s", keyValuePairs.string());

    if (param.getInt(key, device) == NO_ERROR) {
        mDevices = device;
        ALOGV("set output routing %x", mDevices);
        status = mHardware->doRouting(NULL);
        param.remove(key);
    }

    if (param.size()) {
        status = BAD_VALUE;
    }
    return status;
}

String8 AudioHardware::AudioSessionOutMSM8x60::getParameters(const String8& keys)
{
    AudioParameter param = AudioParameter(keys);
    String8 value;
    String8 key = String8(AudioParameter::keyRouting);

    if (param.get(key, value) == NO_ERROR) {
        ALOGV("get routing %x", mDevices);
        param.addInt(key, (int)mDevices);
    }

    ALOGV("AudioSessionOutMSM8x60::getParameters() %s", param.toString().string());
    return param.toString();
}

status_t AudioHardware::AudioSessionOutMSM8x60::getRenderPosition(uint32_t *dspFrames)
{
    //TODO: enable when supported by driver
    return INVALID_OPERATION;
}

status_t AudioHardware::AudioSessionOutMSM8x60::setVolume(float left, float right)
{
    float v = (left + right) / 2;
    if (v < 0.0) {
        ALOGW("AudioSessionOutMSM8x60::setVolume(%f) under 0.0, assuming 0.0\n", v);
        v = 0.0;
    } else if (v > 1.0) {
        ALOGW("AudioSessionOutMSM8x60::setVolume(%f) over 1.0, assuming 1.0\n", v);
        v = 1.0;
    }

    // Ensure to convert the log volume back to linear for LPA
    float vol = v * 100;
    ALOGV("AudioSessionOutMSM8x60::setVolume(%f)\n", v);
    ALOGV("Setting session volume to %f (available range is 0 to 100)\n", vol);

    if(msm_set_volume(mSessionId, vol)) {
        ALOGE("msm_set_volume(%d %f) failed errno = %d",mSessionId, vol,errno);
        return -1;
    }
    ALOGV("msm_set_volume(%f) succeeded",vol);
    return NO_ERROR;
}

// ----------------------------------------------------------------------------

AudioHardware::AudioStreamOutMSM8x60::AudioStreamOutMSM8x60() :
    mHardware(0), mFd(-1), mStartCount(0), mRetryCount(0), mStandby(true), mDevices(0)
{
}

status_t AudioHardware::AudioStreamOutMSM8x60::set(
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

AudioHardware::AudioStreamOutMSM8x60::~AudioStreamOutMSM8x60()
{
    if (mFd >= 0) close(mFd);
}

ssize_t AudioHardware::AudioStreamOutMSM8x60::write(const void* buffer, size_t bytes)
{
    //ALOGE("AudioStreamOutMSM8x60::write(%p, %u)", buffer, bytes);
    status_t status = NO_INIT;
    size_t count = bytes;
    const uint8_t* p = static_cast<const uint8_t*>(buffer);
    unsigned short dec_id = INVALID_DEVICE;

    if (mStandby) {

        // open driver
        ALOGV("open driver");
        status = ::open("/dev/msm_pcm_out", O_WRONLY/*O_RDWR*/);
        if (status < 0) {
            ALOGE("Cannot open /dev/msm_pcm_out errno: %d", errno);
            goto Error;
        }
        mFd = status;

        // configuration
        ALOGV("get config");
        struct msm_audio_config config;
        status = ioctl(mFd, AUDIO_GET_CONFIG, &config);
        if (status < 0) {
            ALOGE("Cannot read config");
            goto Error;
        }

        ALOGV("set config");
        config.channel_count = AudioSystem::popCount(channels());
        config.sample_rate = sampleRate();
        config.buffer_size = bufferSize();
        config.buffer_count = AUDIO_HW_NUM_OUT_BUF;
        config.codec_type = CODEC_TYPE_PCM;
        status = ioctl(mFd, AUDIO_SET_CONFIG, &config);
        if (status < 0) {
            ALOGE("Cannot set config");
            goto Error;
        }

        ALOGV("buffer_size: %u", config.buffer_size);
        ALOGV("buffer_count: %u", config.buffer_count);
        ALOGV("channel_count: %u", config.channel_count);
        ALOGV("sample_rate: %u", config.sample_rate);

        // fill 2 buffers before AUDIO_START
        mStartCount = AUDIO_HW_NUM_OUT_BUF;
        mStandby = false;
    }

    while (count) {
        ssize_t written = ::write(mFd, p, count);
        if (written > 0) {
            count -= written;
            p += written;
        } else {
            if (errno != EAGAIN) return written;
            mRetryCount++;
            ALOGW("EAGAIN - retry");
        }
    }

    // start audio after we fill 2 buffers
    if (mStartCount) {
        if (--mStartCount == 0) {
            if(ioctl(mFd, AUDIO_GET_SESSION_ID, &dec_id)) {
                ALOGE("AUDIO_GET_SESSION_ID failed*********");
                return 0;
            }
            ALOGE("write(): dec_id = %d cur_rx = %d\n",dec_id,cur_rx);
            if(cur_rx == INVALID_DEVICE) {
                //return 0; //temporary fix until team upmerges code to froyo tip
                cur_rx = 0;
                cur_tx = 1;
            }

            Mutex::Autolock lock(mDeviceSwitchLock);

            ALOGE("cur_rx for pcm playback = %d",cur_rx);
            if(enableDevice(cur_rx, 1)) {
                ALOGE("enableDevice failed for device cur_rx %d", cur_rx);
                return 0;
            }
#ifdef QCOM_ACDB_ENABLED
            acdb_loader_send_audio_cal(ACDB_ID(cur_rx), CAPABILITY(cur_rx));
#endif
            if(msm_route_stream(PCM_PLAY, dec_id, DEV_ID(cur_rx), 1)) {
                ALOGE("msm_route_stream failed");
                return 0;
            }
            Mutex::Autolock lock_1(mComboDeviceLock);
#ifdef QCOM_FM_ENABLED
            if(CurrentComboDeviceData.DeviceId == SND_DEVICE_FM_TX_AND_SPEAKER){
                Routing_table *LpaNode = getNodeByStreamType(LPA_DECODE);
                /* This de-routes the LPA being routed on to speaker, which is done in
                  * enablecombo()
                  */
                if(LpaNode != NULL) {
                    ALOGD("combo:de-route:msm_route_stream(%d,%d,0)",LpaNode ->dec_id,
                        DEV_ID(DEVICE_SPEAKER_RX));
                    if(msm_route_stream(PCM_PLAY, LpaNode ->dec_id,
                        DEV_ID(DEVICE_SPEAKER_RX), 0)) {
                            ALOGE("msm_route_stream failed");
                            return -1;
                    }
                }
                ALOGD("Routing PCM stream to speaker for combo device");
                ALOGD("combo:msm_route_stream(PCM_PLAY,session id:%d,dev id:%d,1)",dec_id,
                    DEV_ID(DEVICE_SPEAKER_RX));
                if(msm_route_stream(PCM_PLAY, dec_id, DEV_ID(DEVICE_SPEAKER_RX),
                    1)) {
                    ALOGE("msm_route_stream failed");
                    return -1;
                }
                CurrentComboDeviceData.StreamType = PCM_PLAY;
            }
#endif
            addToTable(dec_id,cur_rx,INVALID_DEVICE,PCM_PLAY,true);
            ioctl(mFd, AUDIO_START, 0);
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

status_t AudioHardware::AudioStreamOutMSM8x60::standby()
{
    Routing_table* temp = NULL;
    ALOGD("AudioStreamOutMSM8x60::standby()");
    status_t status = NO_ERROR;

    temp = getNodeByStreamType(PCM_PLAY);

    if(temp == NULL)
        return NO_ERROR;

    ALOGD("Deroute pcm stream");
    if(msm_route_stream(PCM_PLAY, temp->dec_id,DEV_ID(temp->dev_id), 0)) {
        ALOGE("could not set stream routing\n");
        deleteFromTable(PCM_PLAY);
        return -1;
    }
    deleteFromTable(PCM_PLAY);
    if(!getNodeByStreamType(VOICE_CALL)
#ifdef QCOM_TUNNEL_LPA_ENABLED
       && !getNodeByStreamType(LPA_DECODE)
#endif
#ifdef QCOM_FM_ENABLED
       && !getNodeByStreamType(FM_RADIO)
#endif
#ifdef QCOM_VOIP_ENABLED
       && !getNodeByStreamType(VOIP_CALL)
#endif
     ) {
#ifdef QCOM_ANC_HEADSET_ENABLED
    //in case if ANC don't disable cur device.
      if (anc_running == false){
#endif
        if(enableDevice(cur_rx, 0)) {
            ALOGE("Disabling device failed for cur_rx %d", cur_rx);
            return 0;
        }
#ifdef QCOM_ANC_HEADSET_ENABLED
      }
#endif
    }

    if (!mStandby && mFd >= 0) {
        ::close(mFd);
        mFd = -1;
    }

    mStandby = true;
    return status;
}

status_t AudioHardware::AudioStreamOutMSM8x60::dump(int fd, const Vector<String16>& args)
{
    const size_t SIZE = 256;
    char buffer[SIZE];
    String8 result;
    result.append("AudioStreamOutMSM8x60::dump\n");
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

bool AudioHardware::AudioStreamOutMSM8x60::checkStandby()
{
    return mStandby;
}


status_t AudioHardware::AudioStreamOutMSM8x60::setParameters(const String8& keyValuePairs)
{
    AudioParameter param = AudioParameter(keyValuePairs);
    String8 key = String8(AudioParameter::keyRouting);
    status_t status = NO_ERROR;
    int device;
    ALOGV("AudioStreamOutMSM8x60::setParameters() %s", keyValuePairs.string());

    if (param.getInt(key, device) == NO_ERROR) {
        mDevices = device;
        ALOGV("set output routing %x", mDevices);
        status = mHardware->doRouting(NULL);
        param.remove(key);
    }

    if (param.size()) {
        status = BAD_VALUE;
    }
    return status;
}

String8 AudioHardware::AudioStreamOutMSM8x60::getParameters(const String8& keys)
{
    AudioParameter param = AudioParameter(keys);
    String8 value;
    String8 key = String8(AudioParameter::keyRouting);

    if (param.get(key, value) == NO_ERROR) {
        ALOGV("get routing %x", mDevices);
        param.addInt(key, (int)mDevices);
    }

    ALOGV("AudioStreamOutMSM8x60::getParameters() %s", param.toString().string());
    return param.toString();
}

status_t AudioHardware::AudioStreamOutMSM8x60::getRenderPosition(uint32_t *dspFrames)
{
    //TODO: enable when supported by driver
    return INVALID_OPERATION;
}

#ifdef QCOM_VOIP_ENABLED
// ----------------------------------------------------------------------------
// Audio Stream from DirectOutput thread
// ----------------------------------------------------------------------------

AudioHardware::AudioStreamOutDirect::AudioStreamOutDirect() :
    mHardware(0), mFd(-1), mStartCount(0), mRetryCount(0), mStandby(true), mDevices(0),mChannels(AudioSystem::CHANNEL_OUT_MONO),
    mSampleRate(AUDIO_HW_VOIP_SAMPLERATE_8K), mBufferSize(AUDIO_HW_VOIP_BUFFERSIZE_8K)
{
}

status_t AudioHardware::AudioStreamOutDirect::set(
        AudioHardware* hw, uint32_t devices, int *pFormat, uint32_t *pChannels, uint32_t *pRate)
{
    int lFormat = pFormat ? *pFormat : 0;
    uint32_t lChannels = pChannels ? *pChannels : 0;
    uint32_t lRate = pRate ? *pRate : 0;

    ALOGV("set1  lFormat = %d lChannels= %u lRate = %u\n", lFormat, lChannels, lRate );
    mHardware = hw;

    // fix up defaults
    if (lFormat == 0) lFormat = format();
    if (lChannels == 0) lChannels = channels();
    if (lRate == 0) lRate = sampleRate();

    // check values
    mFormat =  lFormat;
    mChannels = lChannels;
    mSampleRate = lRate;
    if(mSampleRate == AUDIO_HW_VOIP_SAMPLERATE_8K) {
        mBufferSize = AUDIO_HW_VOIP_BUFFERSIZE_8K;
    } else if(mSampleRate == AUDIO_HW_VOIP_SAMPLERATE_16K) {
        mBufferSize = AUDIO_HW_VOIP_BUFFERSIZE_16K;
    } else {
        ALOGE("  AudioStreamOutDirect::set return bad values\n");
        return BAD_VALUE;
    }

    mDevices = devices;

    if((voip_session_id <= 0)) {
        voip_session_id = msm_get_voc_session(VOIP_SESSION_NAME);
    }
    mHardware->mNumVoipStreams++;
    return NO_ERROR;
}

AudioHardware::AudioStreamOutDirect::~AudioStreamOutDirect()
{
    ALOGV("AudioStreamOutDirect destructor");
    standby();
    if (mHardware->mNumVoipStreams)
        mHardware->mNumVoipStreams--;
}

ssize_t AudioHardware::AudioStreamOutDirect::write(const void* buffer, size_t bytes)
{
    status_t status = NO_INIT;
    size_t count = bytes;
    const uint8_t* p = static_cast<const uint8_t*>(buffer);
    unsigned short dec_id = INVALID_DEVICE;

    if (mStandby) {
        if(mHardware->mVoipFd >= 0) {
                mFd = mHardware->mVoipFd;
        }
        else {
            // open driver
            ALOGV("open mvs driver");
            status = ::open(MVS_DEVICE, /*O_WRONLY*/ O_RDWR);
            if (status < 0) {
                ALOGE("Cannot open %s errno: %d",MVS_DEVICE, errno);
                goto Error;
            }
            mFd = status;
            mHardware->mVoipFd = mFd;
            // configuration
            ALOGV("get mvs config");
            struct msm_audio_mvs_config mvs_config;
            status = ioctl(mFd, AUDIO_GET_MVS_CONFIG, &mvs_config);
            if (status < 0) {
               ALOGE("Cannot read mvs config");
               goto Error;
            }

            ALOGV("set mvs config");
            int samplerate = sampleRate();
            if(samplerate == AUDIO_HW_VOIP_SAMPLERATE_8K) {
                 mvs_config.mvs_mode = MVS_MODE_PCM;
            } else if(samplerate == AUDIO_HW_VOIP_SAMPLERATE_16K) {
                 mvs_config.mvs_mode = MVS_MODE_PCM_WB;
            } else {
                 ALOGE("unsupported samplerate for voip");
                 goto Error;
            }
            status = ioctl(mFd, AUDIO_SET_MVS_CONFIG, &mvs_config);
            if (status < 0) {
                ALOGE("Cannot set mvs config");
                goto Error;
            }

            ALOGV("start mvs config");
            status = ioctl(mFd, AUDIO_START, 0);
            if (status < 0) {
                ALOGE("Cannot start mvs driver");
                goto Error;
            }

            // fill 2 buffers before AUDIO_START
            mStartCount = AUDIO_HW_NUM_OUT_BUF;
            mStandby = false;

            Mutex::Autolock lock(mDeviceSwitchLock);
            //Routing Voip
            if ((cur_rx != INVALID_DEVICE) && (cur_tx != INVALID_DEVICE))
            {
                ALOGV("Starting voip call on Rx %d and Tx %d device", DEV_ID(cur_rx), DEV_ID(cur_tx));
                msm_route_voice(DEV_ID(cur_rx),DEV_ID(cur_tx), 1);
            }
            else {
               return -1;
            }

            //Enable RX device
            if(cur_rx != INVALID_DEVICE && (enableDevice(cur_rx,1) == -1))
            {
               return -1;
            }

            //Enable TX device
            if(cur_tx != INVALID_DEVICE&&(enableDevice(cur_tx,1) == -1))
            {
               return -1;
            }
#ifdef QCOM_ACDB_ENABLED
            // voip calibration
            acdb_loader_send_voice_cal(ACDB_ID(cur_rx),ACDB_ID(cur_tx));
#endif
            // start Voip call
            ALOGD("Starting voip call and UnMuting the call");
            msm_start_voice_ext(voip_session_id);
            msm_set_voice_tx_mute_ext(voip_session_mute,voip_session_id);
            addToTable(0,cur_rx,cur_tx,VOIP_CALL,true);
        }
    }
    struct msm_audio_mvs_frame audio_mvs_frame;
    audio_mvs_frame.frame_type = 0;
    while (count) {
        audio_mvs_frame.len = count;
        memcpy(&audio_mvs_frame.voc_pkt, p, count);
        size_t written = ::write(mFd, &audio_mvs_frame, sizeof(audio_mvs_frame));
        ALOGV(" mvs bytes count = %d written : %d \n", count, written);
        if (written == 0) {
            count -= audio_mvs_frame.len;
            p += audio_mvs_frame.len;
        } else {
            if (errno != EAGAIN) return written;
            mRetryCount++;
            ALOGW("EAGAIN - retry");
        }
    }

    // start audio after we fill 2 buffers
    if (mStartCount) {
        if (--mStartCount == 0) {
            if(ioctl(mFd, AUDIO_GET_SESSION_ID, &dec_id)) {
                ALOGE("AUDIO_GET_SESSION_ID failed*********");
                return 0;
            }
            ALOGD("write(): dec_id = %d cur_rx = %d\n",dec_id,cur_rx);
            if(cur_rx == INVALID_DEVICE) {
                //return 0; //temporary fix until team upmerges code to froyo tip
                cur_rx = 0;
                cur_tx = 1;
            }
        }
    }
    bytes = audio_mvs_frame.len;
    return bytes;

Error:
ALOGE("  write Error \n");
    if (mFd >= 0) {
        ::close(mFd);
        mFd = -1;
        mHardware->mVoipFd = -1;
    }
    // Simulate audio output timing in case of error
    usleep(bytes * 1000000 / frameSize() / sampleRate());

    return status;
}

status_t AudioHardware::AudioStreamOutDirect::standby()
{
    Routing_table* temp = NULL;
    ALOGD("AudioStreamOutDirect::standby()");
    status_t status = NO_ERROR;
    int ret = 0;

    ALOGV(" AudioStreamOutDirect::standby mHardware->mNumVoipStreams = %d mFd = %d\n", mHardware->mNumVoipStreams, mFd);
    if (mFd >= 0 && (mHardware->mNumVoipStreams == 1)) {
        ret = msm_end_voice_ext(voip_session_id);
        if (ret < 0)
                ALOGE("Error %d ending voip\n", ret);

        temp = getNodeByStreamType(VOIP_CALL);
        if(temp == NULL)
        {
            ALOGE("VOIP: Going to disable RX/TX return 0");
            return 0;
        }

        if((temp->dev_id != INVALID_DEVICE && temp->dev_id_tx != INVALID_DEVICE)&& (!isStreamOn(VOICE_CALL))) {
            ALOGE(" Streamout: disable VOIP rx tx ");
           enableDevice(temp->dev_id,0);
           enableDevice(temp->dev_id_tx,0);
        }
        deleteFromTable(VOIP_CALL);
       if (mFd >= 0) {
           ret = ioctl(mFd, AUDIO_STOP, NULL);
           ALOGE("MVS stop returned %d \n", ret);
           ::close(mFd);
           mFd = mHardware->mVoipFd = -1;
           ALOGV("driver closed");
           voip_session_id = 0;
           voip_session_mute = 0;
       }
   }
    mStandby = true;
    return status;
}

status_t AudioHardware::AudioStreamOutDirect::dump(int fd, const Vector<String16>& args)
{
    const size_t SIZE = 256;
    char buffer[SIZE];
    String8 result;
    result.append("AudioStreamOutDirect::dump\n");
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

bool AudioHardware::AudioStreamOutDirect::checkStandby()
{
    return mStandby;
}


status_t AudioHardware::AudioStreamOutDirect::setParameters(const String8& keyValuePairs)
{
    AudioParameter param = AudioParameter(keyValuePairs);
    String8 key = String8(AudioParameter::keyRouting);
    status_t status = NO_ERROR;
    int device;
    ALOGV("AudioStreamOutDirect::setParameters() %s", keyValuePairs.string());

    if (param.getInt(key, device) == NO_ERROR) {
        mDevices = device;
        ALOGV("set output routing %x", mDevices);
        status = mHardware->doRouting(NULL);
        param.remove(key);
    }

    if (param.size()) {
        status = BAD_VALUE;
    }
    return status;
}

String8 AudioHardware::AudioStreamOutDirect::getParameters(const String8& keys)
{
    AudioParameter param = AudioParameter(keys);
    String8 value;
    String8 key = String8(AudioParameter::keyRouting);

    if (param.get(key, value) == NO_ERROR) {
        ALOGV("get routing %x", mDevices);
        param.addInt(key, (int)mDevices);
    }

    ALOGV("AudioStreamOutDirect::getParameters() %s", param.toString().string());
    return param.toString();
}

status_t AudioHardware::AudioStreamOutDirect::getRenderPosition(uint32_t *dspFrames)
{
    //TODO: enable when supported by driver
    return INVALID_OPERATION;
}

// End AudioStreamOutDirect
//.----------------------------------------------------------------------------
#endif

//.----------------------------------------------------------------------------
AudioHardware::AudioStreamInMSM8x60::AudioStreamInMSM8x60() :
    mHardware(0), mFd(-1), mState(AUDIO_INPUT_CLOSED), mRetryCount(0),
    mFormat(AUDIO_HW_IN_FORMAT), mChannels(AUDIO_HW_IN_CHANNELS),
    mSampleRate(AUDIO_HW_IN_SAMPLERATE), mBufferSize(AUDIO_HW_IN_BUFFERSIZE),
    mAcoustics((AudioSystem::audio_in_acoustics)0), mDevices(0)
{
}

status_t AudioHardware::AudioStreamInMSM8x60::set(
        AudioHardware* hw, uint32_t devices, int *pFormat, uint32_t *pChannels, uint32_t *pRate,
        AudioSystem::audio_in_acoustics acoustic_flags)
{
    if ((pFormat == 0) || (*pFormat != AUDIO_HW_IN_FORMAT))
    {
        *pFormat = AUDIO_HW_IN_FORMAT;
        return BAD_VALUE;
    }

    if (pRate == 0) {
        return BAD_VALUE;
    }
    uint32_t rate = hw->getInputSampleRate(*pRate);
    if (rate != *pRate) {
        *pRate = rate;
        ALOGE(" sample rate does not match\n");
        return BAD_VALUE;
    }

    if (pChannels == 0 || (*pChannels & (AudioSystem::CHANNEL_IN_MONO | AudioSystem::CHANNEL_IN_STEREO)) == 0) {
        *pChannels = AUDIO_HW_IN_CHANNELS;
        ALOGE(" Channel count does not match\n");
        return BAD_VALUE;
    }

    mHardware = hw;

    ALOGV("AudioStreamInMSM8x60::set(%d, %d, %u)", *pFormat, *pChannels, *pRate);
    if (mFd >= 0) {
        ALOGE("Audio record already open");
        return -EPERM;
    }
    status_t status =0;
    struct msm_voicerec_mode voc_rec_cfg;
#ifdef QCOM_FM_ENABLED
    if(devices == AudioSystem::DEVICE_IN_FM_RX_A2DP) {
        status = ::open("/dev/msm_pcm_in", O_RDONLY);
        if (status < 0) {
            ALOGE("Cannot open /dev/msm_pcm_in errno: %d", errno);
            goto Error;
        }
        mFd = status;
        // configuration
        ALOGV("get config");
        struct msm_audio_config config;
        status = ioctl(mFd, AUDIO_GET_CONFIG, &config);
        if (status < 0) {
            ALOGE("Cannot read config");
            goto Error;
        }

        ALOGV("set config");
        config.channel_count = AudioSystem::popCount(*pChannels);
        config.sample_rate = *pRate;
        config.buffer_size = bufferSize();
        config.buffer_count = 2;
        config.codec_type = CODEC_TYPE_PCM;
        status = ioctl(mFd, AUDIO_SET_CONFIG, &config);
        if (status < 0) {
            ALOGE("Cannot set config");
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

        ALOGV("confirm config");
        status = ioctl(mFd, AUDIO_GET_CONFIG, &config);
        if (status < 0) {
            ALOGE("Cannot read config");
            goto Error;
        }
        ALOGV("buffer_size: %u", config.buffer_size);
        ALOGV("buffer_count: %u", config.buffer_count);
        ALOGV("channel_count: %u", config.channel_count);
        ALOGV("sample_rate: %u", config.sample_rate);

        mDevices = devices;
        mFormat = AUDIO_HW_IN_FORMAT;
        mChannels = *pChannels;
        mSampleRate = config.sample_rate;
        mBufferSize = config.buffer_size;
    } else
#endif
      if (*pFormat == AUDIO_HW_IN_FORMAT) {
        if (mHardware->mNumPcmRec > 0) {
            /* Only one PCM recording is allowed at a time */
            ALOGE("Multiple PCM recordings is not allowed");
            status = -1;
            goto Error;
        }
        // open audio input device
        status = ::open("/dev/msm_pcm_in", O_RDWR);
        if (status < 0) {
            ALOGE("Cannot open /dev/msm_pcm_in errno: %d", errno);
            goto Error;
        }
        mHardware->mNumPcmRec ++;
        mFd = status;

        // configuration
        ALOGV("get config");
        struct msm_audio_config config;
        status = ioctl(mFd, AUDIO_GET_CONFIG, &config);
        if (status < 0) {
            ALOGE("Cannot read config");
            goto Error;
        }

        ALOGV("set config");
        config.channel_count = AudioSystem::popCount((*pChannels) &
                              (AudioSystem::CHANNEL_IN_STEREO|
                               AudioSystem::CHANNEL_IN_MONO));

        config.sample_rate = *pRate;

        mBufferSize = mHardware->getInputBufferSize(config.sample_rate,
                                                    AUDIO_HW_IN_FORMAT,
                                                    config.channel_count);
        config.buffer_size = bufferSize();
        // Make buffers to be allocated in driver equal to the number of buffers
        // that AudioFlinger allocates (Shared memory)
        config.buffer_count = 4;
        config.codec_type = CODEC_TYPE_PCM;
        status = ioctl(mFd, AUDIO_SET_CONFIG, &config);
        if (status < 0) {
            ALOGE("Cannot set config");
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

        ALOGV("confirm config");
        status = ioctl(mFd, AUDIO_GET_CONFIG, &config);
        if (status < 0) {
            ALOGE("Cannot read config");
            goto Error;
        }
        ALOGV("buffer_size: %u", config.buffer_size);
        ALOGV("buffer_count: %u", config.buffer_count);
        ALOGV("channel_count: %u", config.channel_count);
        ALOGV("sample_rate: %u", config.sample_rate);

        mDevices = devices;
        mFormat = AUDIO_HW_IN_FORMAT;
        mChannels = *pChannels;
        mSampleRate = config.sample_rate;
        mBufferSize = config.buffer_size;

        if (mDevices == AudioSystem::DEVICE_IN_VOICE_CALL) {
            if ((mChannels & AudioSystem::CHANNEL_IN_VOICE_DNLINK) &&
                (mChannels & AudioSystem::CHANNEL_IN_VOICE_UPLINK)) {
                ALOGV("Recording Source: Voice Call Both Uplink and Downlink");
                voc_rec_cfg.rec_mode = VOC_REC_BOTH;
            } else if (mChannels & AudioSystem::CHANNEL_IN_VOICE_DNLINK) {
                ALOGV("Recording Source: Voice Call DownLink");
                voc_rec_cfg.rec_mode = VOC_REC_DOWNLINK;
            } else if (mChannels & AudioSystem::CHANNEL_IN_VOICE_UPLINK) {
                ALOGV("Recording Source: Voice Call UpLink");
                voc_rec_cfg.rec_mode = VOC_REC_UPLINK;
            }
            if (ioctl(mFd, AUDIO_SET_INCALL, &voc_rec_cfg)) {
                ALOGE("Error: AUDIO_SET_INCALL failed\n");
                goto  Error;
            }
        }

    if(vr_enable && dualmic_enabled) {
        int audpre_mask = 0;
        audpre_mask = FLUENCE_ENABLE;

            ALOGV("enable fluence");
            if (ioctl(mFd, AUDIO_ENABLE_AUDPRE, &audpre_mask)) {
                ALOGV("cannot write audio config");
                goto Error;
            }
        }
    }
    //mHardware->setMicMute_nosync(false);
    mState = AUDIO_INPUT_OPENED;

    if (!acoustic)
        return NO_ERROR;

    audpre_index = calculate_audpre_table_index(mSampleRate);
    tx_iir_index = (audpre_index * 2) + (hw->checkOutputStandby() ? 0 : 1);
    ALOGD("audpre_index = %d, tx_iir_index = %d\n", audpre_index, tx_iir_index);

    /**
     * If audio-preprocessing failed, we should not block record.
     */
    int (*msm8x60_set_audpre_params)(int, int);
    msm8x60_set_audpre_params = (int (*)(int, int))::dlsym(acoustic, "msm8x60_set_audpre_params");
    status = msm8x60_set_audpre_params(audpre_index, tx_iir_index);
    if (status < 0)
        ALOGE("Cannot set audpre parameters");

    int (*msm8x60_enable_audpre)(int, int, int);
    msm8x60_enable_audpre = (int (*)(int, int, int))::dlsym(acoustic, "msm8x60_enable_audpre");
    mAcoustics = acoustic_flags;
    status = msm8x60_enable_audpre((int)acoustic_flags, audpre_index, tx_iir_index);
    if (status < 0)
        ALOGE("Cannot enable audpre");

    return NO_ERROR;

Error:
    if (mFd >= 0) {
        ::close(mFd);
        mFd = -1;
    }
    return status;
}

AudioHardware::AudioStreamInMSM8x60::~AudioStreamInMSM8x60()
{
    ALOGV("AudioStreamInMSM8x60 destructor");
    standby();
}

ssize_t AudioHardware::AudioStreamInMSM8x60::read( void* buffer, ssize_t bytes)
{
    unsigned short dec_id = INVALID_DEVICE;
    ALOGV("AudioStreamInMSM8x60::read(%p, %ld)", buffer, bytes);
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
        if (status != NO_ERROR) {
            hw->mLock.unlock();
            return -1;
        }
#ifdef QCOM_FM_ENABLED
        if((mDevices == AudioSystem::DEVICE_IN_FM_RX) || (mDevices == AudioSystem::DEVICE_IN_FM_RX_A2DP) ){
            if(ioctl(mFd, AUDIO_GET_SESSION_ID, &dec_id)) {
                ALOGE("AUDIO_GET_SESSION_ID failed*********");
                hw->mLock.unlock();
                return -1;
            }

            if(enableDevice(DEVICE_FMRADIO_STEREO_TX, 1)) {
                ALOGE("enableDevice DEVICE_FMRADIO_STEREO_TX failed");
                hw->mLock.unlock();
                return -1;
             }
#ifdef QCOM_ACDB_ENABLED
            acdb_loader_send_audio_cal(ACDB_ID(DEVICE_FMRADIO_STEREO_TX),
            CAPABILITY(DEVICE_FMRADIO_STEREO_TX));
#endif
            ALOGV("route FM");
            if(msm_route_stream(PCM_REC, dec_id, DEV_ID(DEVICE_FMRADIO_STEREO_TX), 1)) {
                ALOGE("msm_route_stream failed");
                hw->mLock.unlock();
                return -1;
            }

            //addToTable(dec_id,cur_tx,INVALID_DEVICE,PCM_REC,true);
            mFirstread = false;
            if (mDevices == AudioSystem::DEVICE_IN_FM_RX_A2DP) {
                addToTable(dec_id,cur_tx,INVALID_DEVICE,FM_A2DP,true);
                mFmRec = FM_A2DP_REC;
            } else {
                addToTable(dec_id,cur_tx,INVALID_DEVICE,FM_REC,true);
                mFmRec = FM_FILE_REC;
            }
            hw->mLock.unlock();
        } else
#endif
        {
            hw->mLock.unlock();
            if(ioctl(mFd, AUDIO_GET_SESSION_ID, &dec_id)) {
                ALOGE("AUDIO_GET_SESSION_ID failed*********");
                return -1;
            }

            Mutex::Autolock lock(mDeviceSwitchLock);

            if (!(mChannels & AudioSystem::CHANNEL_IN_VOICE_DNLINK ||
                  mChannels & AudioSystem::CHANNEL_IN_VOICE_UPLINK)) {
                ALOGV("dec_id = %d,cur_tx= %d",dec_id,cur_tx);
                 if(cur_tx == INVALID_DEVICE)
                     cur_tx = DEVICE_HANDSET_TX;
                 if(enableDevice(cur_tx, 1)) {
                     ALOGE("enableDevice failed for device cur_rx %d",cur_rx);
                     return -1;
                 }
#ifdef QCOM_ACDB_ENABLED
                 acdb_loader_send_audio_cal(ACDB_ID(cur_tx), CAPABILITY(cur_tx));
#endif
                 if(msm_route_stream(PCM_REC, dec_id, DEV_ID(cur_tx), 1)) {
                    ALOGE("msm_route_stream failed");
                    return -1;
                 }
                 addToTable(dec_id,cur_tx,INVALID_DEVICE,PCM_REC,true);
            }
            mFirstread = false;
        }
    }


    if (mState < AUDIO_INPUT_STARTED) {
        if (!(mChannels & AudioSystem::CHANNEL_IN_VOICE_DNLINK ||
            mChannels & AudioSystem::CHANNEL_IN_VOICE_UPLINK)) {
#ifdef QCOM_FM_ENABLED
            // force routing to input device
            // for FM recording, no need to reconfigure afe loopback path
            if (mFmRec != FM_FILE_REC) {
                mHardware->clearCurDevice();
                mHardware->doRouting(this);
            }
#endif
        }
        if (ioctl(mFd, AUDIO_START, 0)) {
            ALOGE("Error starting record");
            standby();
            return -1;
        }
        mState = AUDIO_INPUT_STARTED;
    }

    bytes = 0;
    if(mFormat == AUDIO_HW_IN_FORMAT)
    {
        if(count < mBufferSize) {
            ALOGE("read:: read size requested is less than min input buffer size");
            return 0;
        }
        while (count >= mBufferSize) {
            ssize_t bytesRead = ::read(mFd, buffer, count);
            if (bytesRead >= 0) {
                count -= bytesRead;
                p += bytesRead;
                bytes += bytesRead;
                if(!mFirstread)
                {
                   mFirstread = true;
                   break;
                }
            } else {
                if (errno != EAGAIN) return bytesRead;
                mRetryCount++;
                ALOGW("EAGAIN - retrying");
            }
        }
    }

    return bytes;
}

status_t AudioHardware::AudioStreamInMSM8x60::standby()
{
    bool isDriverClosed = false;
    ALOGD("AudioStreamInMSM8x60::standby()");
    Routing_table* temp = NULL;
    if (!mHardware) return -1;
    if (mState > AUDIO_INPUT_CLOSED) {
        if (mFd >= 0) {
            ::close(mFd);
            mFd = -1;
            ALOGV("driver closed");
            isDriverClosed = true;
            if(mHardware->mNumPcmRec && mFormat == AUDIO_HW_IN_FORMAT) {
                mHardware->mNumPcmRec --;
            }
        }
        mState = AUDIO_INPUT_CLOSED;
    }
#ifdef QCOM_FM_ENABLED
       if (mFmRec == FM_A2DP_REC) {
        //A2DP Recording
        temp = getNodeByStreamType(FM_A2DP);
        if(temp == NULL)
            return NO_ERROR;
        if(msm_route_stream(PCM_PLAY, temp->dec_id, DEV_ID(DEVICE_FMRADIO_STEREO_TX), 0)) {
           ALOGE("msm_route_stream failed");
           return 0;
        }
        deleteFromTable(FM_A2DP);
        if(enableDevice(DEVICE_FMRADIO_STEREO_TX, 0)) {
            ALOGE("enableDevice failed for device cur_rx %d", cur_rx);
        }
    }
    if (mFmRec == FM_FILE_REC) {
        //FM Recording
        temp = getNodeByStreamType(FM_REC);
        if(temp == NULL)
            return NO_ERROR;
        if(msm_route_stream(PCM_PLAY, temp->dec_id, DEV_ID(DEVICE_FMRADIO_STEREO_TX), 0)) {
           ALOGE("msm_route_stream failed");
           return 0;
        }
        deleteFromTable(FM_REC);
    }
#endif
    temp = getNodeByStreamType(PCM_REC);
    if(temp == NULL)
        return NO_ERROR;
    if(isDriverClosed){
        ALOGV("Deroute pcm stream");
        if (!(mChannels & AudioSystem::CHANNEL_IN_VOICE_DNLINK ||
            mChannels & AudioSystem::CHANNEL_IN_VOICE_UPLINK)) {
            if(msm_route_stream(PCM_REC, temp->dec_id,DEV_ID(temp->dev_id), 0)) {
               ALOGE("could not set stream routing\n");
               deleteFromTable(PCM_REC);
               return -1;
            }
        }
        ALOGV("Disable device");
        deleteFromTable(PCM_REC);
        if(!getNodeByStreamType(VOICE_CALL)
#ifdef QCOM_VOIP_ENABLED
         && !getNodeByStreamType(VOIP_CALL)
#endif
         ) {
            if(enableDevice(cur_tx, 0)) {
                ALOGE("Disabling device failed for cur_tx %d", cur_tx);
                return 0;
            }
        }
    } //isDriverClosed condition
    // restore output routing if necessary
    mHardware->clearCurDevice();
    mHardware->doRouting(this);
    return NO_ERROR;
}

status_t AudioHardware::AudioStreamInMSM8x60::dump(int fd, const Vector<String16>& args)
{
    const size_t SIZE = 256;
    char buffer[SIZE];
    String8 result;
    result.append("AudioStreamInMSM8x60::dump\n");
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

status_t AudioHardware::AudioStreamInMSM8x60::setParameters(const String8& keyValuePairs)
{
    AudioParameter param = AudioParameter(keyValuePairs);
    String8 key = String8(AudioParameter::keyRouting);
    status_t status = NO_ERROR;
    int device;
    ALOGV("AudioStreamInMSM8x60::setParameters() %s", keyValuePairs.string());

    if (param.getInt(key, device) == NO_ERROR) {
        ALOGV("set input routing %x", device);
        if (device & (device - 1)) {
            status = BAD_VALUE;
        } else if (device) {
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

String8 AudioHardware::AudioStreamInMSM8x60::getParameters(const String8& keys)
{
    AudioParameter param = AudioParameter(keys);
    String8 value;
    String8 key = String8(AudioParameter::keyRouting);

    if (param.get(key, value) == NO_ERROR) {
        ALOGV("get routing %x", mDevices);
        param.addInt(key, (int)mDevices);
    }

    ALOGV("AudioStreamInMSM8x60::getParameters() %s", param.toString().string());
    return param.toString();
}

// getActiveInput_l() must be called with mLock held
AudioHardware::AudioStreamInMSM8x60 *AudioHardware::getActiveInput_l()
{
    for (size_t i = 0; i < mInputs.size(); i++) {
        // return first input found not being in standby mode
        // as only one input can be in this state
        if (mInputs[i]->state() > AudioStreamInMSM8x60::AUDIO_INPUT_CLOSED) {
            return mInputs[i];
        }
    }

    return NULL;
}

#ifdef QCOM_VOIP_ENABLED
// ----------------------------------------------------------------------------
//  VOIP stream class
//.----------------------------------------------------------------------------
AudioHardware::AudioStreamInVoip::AudioStreamInVoip() :
    mHardware(0), mFd(-1), mState(AUDIO_INPUT_CLOSED), mRetryCount(0),
    mFormat(AUDIO_HW_IN_FORMAT), mChannels(AUDIO_HW_IN_CHANNELS),
    mSampleRate(AUDIO_HW_VOIP_SAMPLERATE_8K), mBufferSize(AUDIO_HW_VOIP_BUFFERSIZE_8K),
    mAcoustics((AudioSystem::audio_in_acoustics)0), mDevices(0)
{
}

status_t AudioHardware::AudioStreamInVoip::set(
        AudioHardware* hw, uint32_t devices, int *pFormat, uint32_t *pChannels, uint32_t *pRate,
        AudioSystem::audio_in_acoustics acoustic_flags)
{
    ALOGV(" AudioHardware::AudioStreamInVoip::set devices = %u format = %d pChannels = %u Rate = %u \n",
         devices, *pFormat, *pChannels, *pRate);
    ALOGV("AudioStreamInVoip cur_rx %d, cur_tx %d" ,cur_rx,cur_tx);
    if ((pFormat == 0) || (*pFormat != AUDIO_HW_IN_FORMAT))
    {
        *pFormat = AUDIO_HW_IN_FORMAT;
        return BAD_VALUE;
    }

    if (pRate == 0) {
        return BAD_VALUE;
    }
    uint32_t rate = hw->getInputSampleRate(*pRate);
    if (rate != *pRate) {
        *pRate = rate;
        ALOGE(" sample rate does not match\n");
        return BAD_VALUE;
    }

    if (pChannels == 0 || (*pChannels & (AudioSystem::CHANNEL_IN_MONO)) == 0) {
        *pChannels = AUDIO_HW_IN_CHANNELS;
        ALOGE(" Channle count does not match\n");
        return BAD_VALUE;
    }

    mHardware = hw;

    ALOGV("AudioStreamInVoip::set(%d, %d, %u)", *pFormat, *pChannels, *pRate);
    if (mFd >= 0) {
        ALOGE("Audio record already open");
        return -EPERM;
    }

        status_t status = NO_INIT;
        // open driver
        ALOGV("Check if driver is open");
        if(mHardware->mVoipFd >= 0) {
            mFd = mHardware->mVoipFd;
            // Increment voip stream count
            mHardware->mNumVoipStreams++;
            ALOGV("MVS driver is already opened, mHardware->mNumVoipStreams = %d \n", mHardware->mNumVoipStreams);
        }
        else {
            ALOGE("open mvs driver");
            status = ::open(MVS_DEVICE, /*O_WRONLY*/ O_RDWR);
            if (status < 0) {
                ALOGE("Cannot open %s errno: %d",MVS_DEVICE, errno);
                goto Error;
            }
            mFd = status;
            ALOGE("VOPIstreamin : Save the fd \n");
            mHardware->mVoipFd = mFd;
            // Increment voip stream count
            mHardware->mNumVoipStreams++;
            ALOGV(" input stream set mHardware->mNumVoipStreams = %d \n", mHardware->mNumVoipStreams);

            // configuration
            ALOGV("get mvs config");
            struct msm_audio_mvs_config mvs_config;
            status = ioctl(mFd, AUDIO_GET_MVS_CONFIG, &mvs_config);
            if (status < 0) {
               ALOGE("Cannot read mvs config");
               goto Error;
            }

            ALOGV("set mvs config");
            int samplerate = *pRate;
            if(samplerate == AUDIO_HW_VOIP_SAMPLERATE_8K) {
                 mvs_config.mvs_mode = MVS_MODE_PCM;
            } else if(samplerate == AUDIO_HW_VOIP_SAMPLERATE_16K) {
                 mvs_config.mvs_mode = MVS_MODE_PCM_WB;
            } else {
                 ALOGE("unsupported samplerate for voip");
                 goto Error;
            }
            status = ioctl(mFd, AUDIO_SET_MVS_CONFIG, &mvs_config);
            if (status < 0) {
                ALOGE("Cannot set mvs config");
                goto Error;
            }

            ALOGV("start mvs");
            status = ioctl(mFd, AUDIO_START, 0);
            if (status < 0) {
                ALOGE("Cannot start mvs driver");
                goto Error;
            }

            ALOGV("Going to enable RX/TX device for voice stream");

            Mutex::Autolock lock(mDeviceSwitchLock);
            // Routing Voip
           if ( (cur_rx != INVALID_DEVICE) && (cur_tx != INVALID_DEVICE))
           {
               ALOGV("Starting voip on Rx %d and Tx %d device", DEV_ID(cur_rx), DEV_ID(cur_tx));
               msm_route_voice(DEV_ID(cur_rx),DEV_ID(cur_tx), 1);
           }
           else
           {
               return -1;
           }

           if(cur_rx != INVALID_DEVICE && (enableDevice(cur_rx,1) == -1))
           {
               ALOGE(" Enable device for cur_rx failed \n");
               return -1;
           }

           if(cur_tx != INVALID_DEVICE&&(enableDevice(cur_tx,1) == -1))
           {
               ALOGE(" Enable device for cur_tx failed \n");
               return -1;
           }
#ifdef QCOM_ACDB_ENABLED
           // voice calibration
           acdb_loader_send_voice_cal(ACDB_ID(cur_rx),ACDB_ID(cur_tx));
#endif
           // start Voice call
           if(voip_session_id <= 0) {
                voip_session_id = msm_get_voc_session(VOIP_SESSION_NAME);
           }
           ALOGD("Starting voip call and UnMuting the call");
           msm_start_voice_ext(voip_session_id);
           msm_set_voice_tx_mute_ext(voip_session_mute,voip_session_id);
           addToTable(0,cur_rx,cur_tx,VOIP_CALL,true);
    }
    mFormat =  *pFormat;
    mChannels = *pChannels;
    mSampleRate = *pRate;
    if(mSampleRate == 8000)
       mBufferSize = 320;
    else if(mSampleRate == 16000)
       mBufferSize = 640;
    else
    {
       ALOGE(" unsupported sample rate");
       return -1;
    }

	 ALOGV(" AudioHardware::AudioStreamInVoip::set after configuring devices = %u format = %d pChannels = %u Rate = %u \n",
         devices, mFormat, mChannels, mSampleRate);

    ALOGV(" Set state  AUDIO_INPUT_OPENED\n");
    mState = AUDIO_INPUT_OPENED;

    if (!acoustic)
        return NO_ERROR;

     return NO_ERROR;

Error:
    if (mFd >= 0) {
        ::close(mFd);
        mFd = -1;
        mHardware->mVoipFd = -1;
    }
    ALOGE("Error : ret status \n");
    return status;
}

AudioHardware::AudioStreamInVoip::~AudioStreamInVoip()
{
    ALOGV("AudioStreamInVoip destructor");
    standby();
    if (mHardware->mNumVoipStreams)
        mHardware->mNumVoipStreams--;
}

ssize_t AudioHardware::AudioStreamInVoip::read( void* buffer, ssize_t bytes)
{
    unsigned short dec_id = INVALID_DEVICE;
    ALOGV("AudioStreamInVoip::read(%p, %ld)", buffer, bytes);
    if (!mHardware) return -1;

    size_t count = bytes;
    size_t totalBytesRead = 0;
    size_t  aac_framesize= bytes;
    uint8_t* p = static_cast<uint8_t*>(buffer);
    uint32_t* recogPtr = (uint32_t *)p;
    uint16_t* frameCountPtr;
    uint16_t* frameSizePtr;
    if (mState < AUDIO_INPUT_OPENED) {
       ALOGE(" reopen the device \n");
        AudioHardware *hw = mHardware;
        hw->mLock.lock();
        status_t status = set(hw, mDevices, &mFormat, &mChannels, &mSampleRate, mAcoustics);
        if (status != NO_ERROR) {
            hw->mLock.unlock();
            return -1;
        }
        hw->mLock.unlock();
        mState = AUDIO_INPUT_STARTED;
        bytes = 0;
  } else {
      ALOGE("AudioStreamInVoip::read : device is already open \n");
  }


  if(mFormat == AUDIO_HW_IN_FORMAT)
  {
      if(count < mBufferSize) {
          ALOGE("read:: read size requested is less than min input buffer size");
          return 0;
      }

      struct msm_audio_mvs_frame audio_mvs_frame;
      audio_mvs_frame.frame_type = 0;
      while (count >= mBufferSize) {
          audio_mvs_frame.len = count;
          ALOGV("Calling read count = %u mBufferSize = %u\n",count, mBufferSize );
          int bytesRead = ::read(mFd, &audio_mvs_frame, sizeof(audio_mvs_frame));
          ALOGV("read_bytes = %d \n", bytesRead);
          if (bytesRead > 0) {
              memcpy(buffer,&audio_mvs_frame.voc_pkt, count);
              count -= audio_mvs_frame.len;
              p += audio_mvs_frame.len;
              totalBytesRead += audio_mvs_frame.len;
              if(!mFirstread) {
                  mFirstread = true;
                  break;
              }
              } else {
                  ALOGE("retry read count = %d buffersize = %d\n", count, mBufferSize);
                  if (errno != EAGAIN) return bytesRead;
                  mRetryCount++;
                  ALOGW("EAGAIN - retrying");
              }
      }
  }
  return totalBytesRead;
}

status_t AudioHardware::AudioStreamInVoip::standby()
{
    bool isDriverClosed = false;
    Routing_table* temp = NULL;
    if (!mHardware) return -1;
    ALOGV(" AudioStreamInVoip::standby = %d \n", mHardware->mNumVoipStreams);
    if (mState > AUDIO_INPUT_CLOSED && (mHardware->mNumVoipStreams == 1)) {
         ALOGE(" closing mvs driver\n");
         //Mute and disable the device.
         int ret = 0;
         ret = msm_end_voice_ext(voip_session_id);
         if (ret < 0)
                 ALOGE("Error %d ending voice\n", ret);
        temp = getNodeByStreamType(VOIP_CALL);
        if(temp == NULL)
        {
            ALOGE("VOIPin: Going to disable RX/TX return 0");
            return 0;
        }

        if((temp->dev_id != INVALID_DEVICE && temp->dev_id_tx != INVALID_DEVICE)) {
           if(!getNodeByStreamType(VOICE_CALL) && !getNodeByStreamType(LPA_DECODE)
              && !getNodeByStreamType(PCM_PLAY) && !getNodeByStreamType(FM_RADIO)) {
               if (anc_running == false) {
                   enableDevice(temp->dev_id, 0);
                   ALOGV("Voipin: disable voip rx");
               }
            }
            if(!getNodeByStreamType(VOICE_CALL) && !getNodeByStreamType(PCM_REC)) {
                 enableDevice(temp->dev_id_tx,0);
                 ALOGD("VOIPin: disable voip tx");
            }
        }
        deleteFromTable(VOIP_CALL);
         if (mFd >= 0) {
            ret = ioctl(mFd, AUDIO_STOP, NULL);
            ALOGE("MVS stop returned %d \n", ret);
            ::close(mFd);
            mFd = mHardware->mVoipFd = -1;
            ALOGV("driver closed");
            isDriverClosed = true;
            voip_session_id = 0;
            voip_session_mute = 0;
        }
        mState = AUDIO_INPUT_CLOSED;
    }
    return NO_ERROR;
}

status_t AudioHardware::AudioStreamInVoip::dump(int fd, const Vector<String16>& args)
{
    const size_t SIZE = 256;
    char buffer[SIZE];
    String8 result;
    result.append("AudioStreamInVoip::dump\n");
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

status_t AudioHardware::AudioStreamInVoip::setParameters(const String8& keyValuePairs)
{
    AudioParameter param = AudioParameter(keyValuePairs);
    String8 key = String8(AudioParameter::keyRouting);
    status_t status = NO_ERROR;
    int device;
    ALOGV("AudioStreamInVoip::setParameters() %s", keyValuePairs.string());

    if (param.getInt(key, device) == NO_ERROR) {
        ALOGV("set input routing %x", device);
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

String8 AudioHardware::AudioStreamInVoip::getParameters(const String8& keys)
{
    AudioParameter param = AudioParameter(keys);
    String8 value;
    String8 key = String8(AudioParameter::keyRouting);

    if (param.get(key, value) == NO_ERROR) {
        ALOGV("get routing %x", mDevices);
        param.addInt(key, (int)mDevices);
    }

    ALOGV("AudioStreamInVoip::getParameters() %s", param.toString().string());
    return param.toString();
}

// getActiveInput_l() must be called with mLock held
AudioHardware::AudioStreamInVoip*AudioHardware::getActiveVoipInput_l()
{
    for (size_t i = 0; i < mVoipInputs.size(); i++) {
        // return first input found not being in standby mode
        // as only one input can be in this state
        if (mVoipInputs[i]->state() > AudioStreamInVoip::AUDIO_INPUT_CLOSED) {
            return mVoipInputs[i];
        }
    }

    return NULL;
}
// ---------------------------------------------------------------------------
//  VOIP stream class end
#endif

extern "C" AudioHardwareInterface* createAudioHardware(void) {
    return new AudioHardware();
}

}; // namespace android_audio_legacy
