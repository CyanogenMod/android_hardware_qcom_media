/*--------------------------------------------------------------------------
Copyright (c) 2010-2015, The Linux Foundation. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of The Linux Foundation nor
      the names of its contributors may be used to endorse or promote
      products derived from this software without specific prior written
      permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
--------------------------------------------------------------------------*/

#include <string.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <fcntl.h>
#include "video_encoder_device_v4l2.h"
#include "omx_video_encoder.h"
#include <media/msm_vidc.h>
#ifdef USE_ION
#include <linux/msm_ion.h>
#endif
#include <media/msm_media_info.h>
#include <cutils/properties.h>
#include <media/hardware/HardwareAPI.h>

#ifdef _ANDROID_
#include <media/hardware/HardwareAPI.h>
#include <gralloc_priv.h>
#endif

#define ALIGN(x, to_align) ((((unsigned long) x) + (to_align - 1)) & ~(to_align - 1))
#define EXTRADATA_IDX(__num_planes) ((__num_planes) ? (__num_planes) - 1 : 0)
#define MAXDPB 16
#define MIN(x,y) (((x) < (y)) ? (x) : (y))
#define MAX(x,y) (((x) > (y)) ? (x) : (y))
#define ROUND(__sz, __align) (((__sz) + ((__align>>1))) & (~(__align-1)))
#define MAX_PROFILE_PARAMS 6
#define MPEG4_SP_START 0
#define MPEG4_ASP_START (MPEG4_SP_START + 10)
#define H263_BP_START 0
#define H264_BP_START 0
#define H264_HP_START (H264_BP_START + 18)
#define H264_MP_START (H264_BP_START + 36)
#define HEVC_MAIN_START 0
#define HEVC_MAIN10_START (HEVC_MAIN_START + 13)
#define POLL_TIMEOUT 1000
#define MAX_SUPPORTED_SLICES_PER_FRAME 28 /* Max supported slices with 32 output buffers */

#define SZ_4K 0x1000
#define SZ_1M 0x100000

/* MPEG4 profile and level table*/
static const unsigned int mpeg4_profile_level_table[][MAX_PROFILE_PARAMS]= {
    /*max mb per frame, max mb per sec, max bitrate, level, profile, dpbmbs*/
    {99,1485,64000,OMX_VIDEO_MPEG4Level0,OMX_VIDEO_MPEG4ProfileSimple,0},
    {99,1485,64000,OMX_VIDEO_MPEG4Level1,OMX_VIDEO_MPEG4ProfileSimple,0},
    {396,5940,128000,OMX_VIDEO_MPEG4Level2,OMX_VIDEO_MPEG4ProfileSimple,0},
    {396,11880,384000,OMX_VIDEO_MPEG4Level3,OMX_VIDEO_MPEG4ProfileSimple,0},
    {1200,36000,4000000,OMX_VIDEO_MPEG4Level4a,OMX_VIDEO_MPEG4ProfileSimple,0},
    {1620,40500,8000000,OMX_VIDEO_MPEG4Level5,OMX_VIDEO_MPEG4ProfileSimple,0},
    {3600,108000,12000000,OMX_VIDEO_MPEG4Level5,OMX_VIDEO_MPEG4ProfileSimple,0},
    {32400,972000,20000000,OMX_VIDEO_MPEG4Level5,OMX_VIDEO_MPEG4ProfileSimple,0},
    {34560,1036800,20000000,OMX_VIDEO_MPEG4Level5,OMX_VIDEO_MPEG4ProfileSimple,0},
    /* Please update MPEG4_ASP_START accordingly, while adding new element */
    {0,0,0,0,0,0},

    {99,1485,128000,OMX_VIDEO_MPEG4Level0,OMX_VIDEO_MPEG4ProfileAdvancedSimple,0},
    {99,1485,128000,OMX_VIDEO_MPEG4Level1,OMX_VIDEO_MPEG4ProfileAdvancedSimple,0},
    {396,5940,384000,OMX_VIDEO_MPEG4Level2,OMX_VIDEO_MPEG4ProfileAdvancedSimple,0},
    {396,11880,768000,OMX_VIDEO_MPEG4Level3,OMX_VIDEO_MPEG4ProfileAdvancedSimple,0},
    {792,23760,3000000,OMX_VIDEO_MPEG4Level4,OMX_VIDEO_MPEG4ProfileAdvancedSimple,0},
    {1620,48600,8000000,OMX_VIDEO_MPEG4Level5,OMX_VIDEO_MPEG4ProfileAdvancedSimple,0},
    {32400,972000,20000000,OMX_VIDEO_MPEG4Level5,OMX_VIDEO_MPEG4ProfileAdvancedSimple,0},
    {34560,1036800,20000000,OMX_VIDEO_MPEG4Level5,OMX_VIDEO_MPEG4ProfileAdvancedSimple,0},
    {0,0,0,0,0,0},
};

/* H264 profile and level table*/
static const unsigned int h264_profile_level_table[][MAX_PROFILE_PARAMS]= {
    /*max mb per frame, max mb per sec, max bitrate, level, profile, dpbmbs*/
    {99,1485,64000,OMX_VIDEO_AVCLevel1,OMX_VIDEO_AVCProfileBaseline,396},
    {99,1485,128000,OMX_VIDEO_AVCLevel1b,OMX_VIDEO_AVCProfileBaseline,396},
    {396,3000,192000,OMX_VIDEO_AVCLevel11,OMX_VIDEO_AVCProfileBaseline,900},
    {396,6000,384000,OMX_VIDEO_AVCLevel12,OMX_VIDEO_AVCProfileBaseline,2376},
    {396,11880,768000,OMX_VIDEO_AVCLevel13,OMX_VIDEO_AVCProfileBaseline,2376},
    {396,11880,2000000,OMX_VIDEO_AVCLevel2,OMX_VIDEO_AVCProfileBaseline,2376},
    {792,19800,4000000,OMX_VIDEO_AVCLevel21,OMX_VIDEO_AVCProfileBaseline,4752},
    {1620,20250,4000000,OMX_VIDEO_AVCLevel22,OMX_VIDEO_AVCProfileBaseline,8100},
    {1620,40500,10000000,OMX_VIDEO_AVCLevel3,OMX_VIDEO_AVCProfileBaseline,8100},
    {3600,108000,14000000,OMX_VIDEO_AVCLevel31,OMX_VIDEO_AVCProfileBaseline,18000},
    {5120,216000,20000000,OMX_VIDEO_AVCLevel32,OMX_VIDEO_AVCProfileBaseline,20480},
    {8192,245760,20000000,OMX_VIDEO_AVCLevel4,OMX_VIDEO_AVCProfileBaseline,32768},
    {8192,245760,50000000,OMX_VIDEO_AVCLevel41,OMX_VIDEO_AVCProfileBaseline,32768},
    {8704,522240,50000000,OMX_VIDEO_AVCLevel42,OMX_VIDEO_AVCProfileBaseline,34816},
    {22080,589824,135000000,OMX_VIDEO_AVCLevel5,OMX_VIDEO_AVCProfileBaseline,110400},
    {36864,983040,240000000,OMX_VIDEO_AVCLevel51,OMX_VIDEO_AVCProfileBaseline,184320},
    {36864,2073600,240000000,OMX_VIDEO_AVCLevel52,OMX_VIDEO_AVCProfileBaseline,184320},
    /* Please update H264_HP_START accordingly, while adding new element */
    {0,0,0,0,0,0},

    {99,1485,64000,OMX_VIDEO_AVCLevel1,OMX_VIDEO_AVCProfileHigh,396},
    {99,1485,160000,OMX_VIDEO_AVCLevel1b,OMX_VIDEO_AVCProfileHigh,396},
    {396,3000,240000,OMX_VIDEO_AVCLevel11,OMX_VIDEO_AVCProfileHigh,900},
    {396,6000,480000,OMX_VIDEO_AVCLevel12,OMX_VIDEO_AVCProfileHigh,2376},
    {396,11880,960000,OMX_VIDEO_AVCLevel13,OMX_VIDEO_AVCProfileHigh,2376},
    {396,11880,2500000,OMX_VIDEO_AVCLevel2,OMX_VIDEO_AVCProfileHigh,2376},
    {792,19800,5000000,OMX_VIDEO_AVCLevel21,OMX_VIDEO_AVCProfileHigh,4752},
    {1620,20250,5000000,OMX_VIDEO_AVCLevel22,OMX_VIDEO_AVCProfileHigh,8100},
    {1620,40500,12500000,OMX_VIDEO_AVCLevel3,OMX_VIDEO_AVCProfileHigh,8100},
    {3600,108000,17500000,OMX_VIDEO_AVCLevel31,OMX_VIDEO_AVCProfileHigh,18000},
    {5120,216000,25000000,OMX_VIDEO_AVCLevel32,OMX_VIDEO_AVCProfileHigh,20480},
    {8192,245760,25000000,OMX_VIDEO_AVCLevel4,OMX_VIDEO_AVCProfileHigh,32768},
    {8192,245760,50000000,OMX_VIDEO_AVCLevel41,OMX_VIDEO_AVCProfileHigh,32768},
    {8704,522240,50000000,OMX_VIDEO_AVCLevel42,OMX_VIDEO_AVCProfileHigh,34816},
    {22080,589824,135000000,OMX_VIDEO_AVCLevel5,OMX_VIDEO_AVCProfileHigh,110400},
    {36864,983040,240000000,OMX_VIDEO_AVCLevel51,OMX_VIDEO_AVCProfileHigh,184320},
    {36864,2073600,240000000,OMX_VIDEO_AVCLevel52,OMX_VIDEO_AVCProfileHigh,184320},
    /* Please update H264_MP_START accordingly, while adding new element */
    {0,0,0,0,0,0},

    {99,1485,64000,OMX_VIDEO_AVCLevel1,OMX_VIDEO_AVCProfileMain,396},
    {99,1485,128000,OMX_VIDEO_AVCLevel1b,OMX_VIDEO_AVCProfileMain,396},
    {396,3000,192000,OMX_VIDEO_AVCLevel11,OMX_VIDEO_AVCProfileMain,900},
    {396,6000,384000,OMX_VIDEO_AVCLevel12,OMX_VIDEO_AVCProfileMain,2376},
    {396,11880,768000,OMX_VIDEO_AVCLevel13,OMX_VIDEO_AVCProfileMain,2376},
    {396,11880,2000000,OMX_VIDEO_AVCLevel2,OMX_VIDEO_AVCProfileMain,2376},
    {792,19800,4000000,OMX_VIDEO_AVCLevel21,OMX_VIDEO_AVCProfileMain,4752},
    {1620,20250,4000000,OMX_VIDEO_AVCLevel22,OMX_VIDEO_AVCProfileMain,8100},
    {1620,40500,10000000,OMX_VIDEO_AVCLevel3,OMX_VIDEO_AVCProfileMain,8100},
    {3600,108000,14000000,OMX_VIDEO_AVCLevel31,OMX_VIDEO_AVCProfileMain,18000},
    {5120,216000,20000000,OMX_VIDEO_AVCLevel32,OMX_VIDEO_AVCProfileMain,20480},
    {8192,245760,20000000,OMX_VIDEO_AVCLevel4,OMX_VIDEO_AVCProfileMain,32768},
    {8192,245760,50000000,OMX_VIDEO_AVCLevel41,OMX_VIDEO_AVCProfileMain,32768},
    {8704,522240,50000000,OMX_VIDEO_AVCLevel42,OMX_VIDEO_AVCProfileMain,34816},
    {22080,589824,135000000,OMX_VIDEO_AVCLevel5,OMX_VIDEO_AVCProfileMain,110400},
    {36864,983040,240000000,OMX_VIDEO_AVCLevel51,OMX_VIDEO_AVCProfileMain,184320},
    {36864,2073600,240000000,OMX_VIDEO_AVCLevel52,OMX_VIDEO_AVCProfileMain,184320},
    {0,0,0,0,0,0}

};

/* H263 profile and level table*/
static const unsigned int h263_profile_level_table[][MAX_PROFILE_PARAMS]= {
    /*max mb per frame, max mb per sec, max bitrate, level, profile, dpbmbs*/
    {99,1485,64000,OMX_VIDEO_H263Level10,OMX_VIDEO_H263ProfileBaseline,0},
    {396,5940,128000,OMX_VIDEO_H263Level20,OMX_VIDEO_H263ProfileBaseline,0},
    {396,11880,384000,OMX_VIDEO_H263Level30,OMX_VIDEO_H263ProfileBaseline,0},
    {396,11880,2048000,OMX_VIDEO_H263Level40,OMX_VIDEO_H263ProfileBaseline,0},
    {99,1485,128000,OMX_VIDEO_H263Level45,OMX_VIDEO_H263ProfileBaseline,0},
    {396,19800,4096000,OMX_VIDEO_H263Level50,OMX_VIDEO_H263ProfileBaseline,0},
    {810,40500,8192000,OMX_VIDEO_H263Level60,OMX_VIDEO_H263ProfileBaseline,0},
    {1620,81000,16384000,OMX_VIDEO_H263Level70,OMX_VIDEO_H263ProfileBaseline,0},
    {32400,972000,20000000,OMX_VIDEO_H263Level70,OMX_VIDEO_H263ProfileBaseline,0},
    {34560,1036800,20000000,OMX_VIDEO_H263Level70,OMX_VIDEO_H263ProfileBaseline,0},
    {0,0,0,0,0,0}
};

/* HEVC profile and level table*/
static const unsigned int hevc_profile_level_table[][MAX_PROFILE_PARAMS]= {
    /*max mb per frame, max mb per sec, max bitrate, level, profile*/
    {99,1485,128000,OMX_VIDEO_HEVCMainTierLevel1,OMX_VIDEO_HEVCProfileMain,0},
    {396,11880,1500000,OMX_VIDEO_HEVCMainTierLevel2,OMX_VIDEO_HEVCProfileMain,0},
    {900,27000,3000000,OMX_VIDEO_HEVCMainTierLevel21,OMX_VIDEO_HEVCProfileMain,0},
    {2025,60750,6000000,OMX_VIDEO_HEVCMainTierLevel3,OMX_VIDEO_HEVCProfileMain,0},
    {8640,259200,10000000,OMX_VIDEO_HEVCMainTierLevel31,OMX_VIDEO_HEVCProfileMain,0},
    {34560,1166400,12000000,OMX_VIDEO_HEVCMainTierLevel4,OMX_VIDEO_HEVCProfileMain,0},
    {138240,4147200,20000000,OMX_VIDEO_HEVCMainTierLevel41,OMX_VIDEO_HEVCProfileMain,0},
    {138240,8294400,25000000,OMX_VIDEO_HEVCMainTierLevel5,OMX_VIDEO_HEVCProfileMain,0},
    {138240,4147200,40000000,OMX_VIDEO_HEVCMainTierLevel51,OMX_VIDEO_HEVCProfileMain,0},
    {138240,4147200,50000000,OMX_VIDEO_HEVCHighTierLevel41,OMX_VIDEO_HEVCProfileMain,0},
    {138240,4147200,100000000,OMX_VIDEO_HEVCHighTierLevel5,OMX_VIDEO_HEVCProfileMain,0},
    {138240,4147200,160000000,OMX_VIDEO_HEVCHighTierLevel51,OMX_VIDEO_HEVCProfileMain,0},
    {138240,4147200,240000000,OMX_VIDEO_HEVCHighTierLevel52,OMX_VIDEO_HEVCProfileMain,0},
    /* Please update HEVC_MAIN_START accordingly, while adding new element */
    {0,0,0,0,0},

    {99,1485,128000,OMX_VIDEO_HEVCMainTierLevel1,OMX_VIDEO_HEVCProfileMain10,0},
    {396,11880,1500000,OMX_VIDEO_HEVCMainTierLevel2,OMX_VIDEO_HEVCProfileMain10,0},
    {900,27000,3000000,OMX_VIDEO_HEVCMainTierLevel21,OMX_VIDEO_HEVCProfileMain10,0},
    {2025,60750,6000000,OMX_VIDEO_HEVCMainTierLevel3,OMX_VIDEO_HEVCProfileMain10,0},
    {8640,259200,10000000,OMX_VIDEO_HEVCMainTierLevel31,OMX_VIDEO_HEVCProfileMain10,0},
    {34560,1166400,12000000,OMX_VIDEO_HEVCMainTierLevel4,OMX_VIDEO_HEVCProfileMain10,0},
    {138240,4147200,20000000,OMX_VIDEO_HEVCMainTierLevel41,OMX_VIDEO_HEVCProfileMain10,0},
    {138240,8294400,25000000,OMX_VIDEO_HEVCMainTierLevel5,OMX_VIDEO_HEVCProfileMain10,0},
    {138240,4147200,40000000,OMX_VIDEO_HEVCMainTierLevel51,OMX_VIDEO_HEVCProfileMain10,0},
    {138240,4147200,50000000,OMX_VIDEO_HEVCHighTierLevel41,OMX_VIDEO_HEVCProfileMain10,0},
    {138240,4147200,100000000,OMX_VIDEO_HEVCHighTierLevel5,OMX_VIDEO_HEVCProfileMain10,0},
    {138240,4147200,160000000,OMX_VIDEO_HEVCHighTierLevel51,OMX_VIDEO_HEVCProfileMain10,0},
    {0,0,0,0,0},
};


#define Log2(number, power)  { OMX_U32 temp = number; power = 0; while( (0 == (temp & 0x1)) &&  power < 16) { temp >>=0x1; power++; } }
#define Q16ToFraction(q,num,den) { OMX_U32 power; Log2(q,power);  num = q >> power; den = 0x1 << (16 - power); }

#define BUFFER_LOG_LOC "/data/misc/media"

//constructor
venc_dev::venc_dev(class omx_venc *venc_class)
{
    //nothing to do
    int i = 0;
    venc_handle = venc_class;
    etb = ebd = ftb = fbd = 0;
    m_poll_efd = -1;

    struct v4l2_control control;
    for (i = 0; i < MAX_PORT; i++)
        streaming[i] = false;

    stopped = 1;
    paused = false;
    async_thread_created = false;
    color_format = 0;
    hw_overload = false;
    mBatchSize = 0;
    pthread_mutex_init(&pause_resume_mlock, NULL);
    pthread_cond_init(&pause_resume_cond, NULL);
    memset(&input_extradata_info, 0, sizeof(input_extradata_info));
    memset(&output_extradata_info, 0, sizeof(output_extradata_info));
    memset(&idrperiod, 0, sizeof(idrperiod));
    memset(&multislice, 0, sizeof(multislice));
    memset (&slice_mode, 0 , sizeof(slice_mode));
    memset(&m_sVenc_cfg, 0, sizeof(m_sVenc_cfg));
    memset(&rate_ctrl, 0, sizeof(rate_ctrl));
    memset(&bitrate, 0, sizeof(bitrate));
    memset(&intra_period, 0, sizeof(intra_period));
    memset(&codec_profile, 0, sizeof(codec_profile));
    memset(&set_param, 0, sizeof(set_param));
    memset(&time_inc, 0, sizeof(time_inc));
    memset(&m_sInput_buff_property, 0, sizeof(m_sInput_buff_property));
    memset(&m_sOutput_buff_property, 0, sizeof(m_sOutput_buff_property));
    memset(&session_qp, 0, sizeof(session_qp));
    memset(&entropy, 0, sizeof(entropy));
    memset(&dbkfilter, 0, sizeof(dbkfilter));
    memset(&intra_refresh, 0, sizeof(intra_refresh));
    memset(&hec, 0, sizeof(hec));
    memset(&voptimecfg, 0, sizeof(voptimecfg));
    memset(&capability, 0, sizeof(capability));
    memset(&m_debug,0,sizeof(m_debug));
    memset(&hier_layers,0,sizeof(hier_layers));
    is_searchrange_set = false;
    enable_mv_narrow_searchrange = false;
    supported_rc_modes = RC_ALL;
    memset(&vqzip_sei_info, 0, sizeof(vqzip_sei_info));
    memset(&ltrinfo, 0, sizeof(ltrinfo));
    memset(&fd_list, 0, sizeof(fd_list));
    memset(&hybrid_hp, 0, sizeof(hybrid_hp));
    sess_priority.priority = 1;
    operating_rate = 0;

    char property_value[PROPERTY_VALUE_MAX] = {0};
    property_get("vidc.enc.log.in", property_value, "0");
    m_debug.in_buffer_log = atoi(property_value);

    property_get("vidc.enc.log.out", property_value, "0");
    m_debug.out_buffer_log = atoi(property_value);

    property_get("vidc.enc.log.extradata", property_value, "0");
    m_debug.extradata_log = atoi(property_value);

#ifdef _UBWC_
    property_get("debug.gralloc.gfx_ubwc_disable", property_value, "0");
    if(!(strncmp(property_value, "1", PROPERTY_VALUE_MAX)) ||
        !(strncmp(property_value, "true", PROPERTY_VALUE_MAX))) {
        is_gralloc_source_ubwc = 0;
    } else {
        is_gralloc_source_ubwc = 1;
    }
#else
    is_gralloc_source_ubwc = 0;
#endif

    snprintf(m_debug.log_loc, PROPERTY_VALUE_MAX,
             "%s", BUFFER_LOG_LOC);
}

venc_dev::~venc_dev()
{
    //nothing to do
}

void* venc_dev::async_venc_message_thread (void *input)
{
    struct venc_msg venc_msg;
    omx_video* omx_venc_base = NULL;
    omx_venc *omx = reinterpret_cast<omx_venc*>(input);
    omx_venc_base = reinterpret_cast<omx_video*>(input);
    OMX_BUFFERHEADERTYPE* omxhdr = NULL;

    prctl(PR_SET_NAME, (unsigned long)"VideoEncCallBackThread", 0, 0, 0);
    struct v4l2_plane plane[VIDEO_MAX_PLANES];
    struct pollfd pfds[2];
    struct v4l2_buffer v4l2_buf;
    struct v4l2_event dqevent;
    struct statistics stats;
    pfds[0].events = POLLIN | POLLRDNORM | POLLOUT | POLLWRNORM | POLLRDBAND | POLLPRI;
    pfds[1].events = POLLIN | POLLERR;
    pfds[0].fd = omx->handle->m_nDriver_fd;
    pfds[1].fd = omx->handle->m_poll_efd;
    int error_code = 0,rc=0;

    memset(&stats, 0, sizeof(statistics));
    memset(&v4l2_buf, 0, sizeof(v4l2_buf));

    while (1) {
        pthread_mutex_lock(&omx->handle->pause_resume_mlock);

        if (omx->handle->paused) {
            venc_msg.msgcode = VEN_MSG_PAUSE;
            venc_msg.statuscode = VEN_S_SUCCESS;

            if (omx->async_message_process(input, &venc_msg) < 0) {
                DEBUG_PRINT_ERROR("ERROR: Failed to process pause msg");
                pthread_mutex_unlock(&omx->handle->pause_resume_mlock);
                break;
            }

            /* Block here until the IL client resumes us again */
            pthread_cond_wait(&omx->handle->pause_resume_cond,
                    &omx->handle->pause_resume_mlock);

            venc_msg.msgcode = VEN_MSG_RESUME;
            venc_msg.statuscode = VEN_S_SUCCESS;

            if (omx->async_message_process(input, &venc_msg) < 0) {
                DEBUG_PRINT_ERROR("ERROR: Failed to process resume msg");
                pthread_mutex_unlock(&omx->handle->pause_resume_mlock);
                break;
            }
            memset(&stats, 0, sizeof(statistics));
        }

        pthread_mutex_unlock(&omx->handle->pause_resume_mlock);

        rc = poll(pfds, 2, POLL_TIMEOUT);

        if (!rc) {
            DEBUG_PRINT_HIGH("Poll timedout, pipeline stalled due to client/firmware ETB: %d, EBD: %d, FTB: %d, FBD: %d",
                    omx->handle->etb, omx->handle->ebd, omx->handle->ftb, omx->handle->fbd);
            continue;
        } else if (rc < 0) {
            DEBUG_PRINT_ERROR("Error while polling: %d", rc);
            break;
        }

        if ((pfds[1].revents & POLLIN) || (pfds[1].revents & POLLERR)) {
            DEBUG_PRINT_ERROR("async_venc_message_thread interrupted to be exited");
            break;
        }

        if ((pfds[0].revents & POLLIN) || (pfds[0].revents & POLLRDNORM)) {
            v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
            v4l2_buf.memory = V4L2_MEMORY_USERPTR;
            v4l2_buf.length = omx->handle->num_output_planes;
            v4l2_buf.m.planes = plane;

            while (!ioctl(pfds[0].fd, VIDIOC_DQBUF, &v4l2_buf)) {
                venc_msg.msgcode=VEN_MSG_OUTPUT_BUFFER_DONE;
                venc_msg.statuscode=VEN_S_SUCCESS;
                omxhdr=omx_venc_base->m_out_mem_ptr+v4l2_buf.index;
                venc_msg.buf.len= v4l2_buf.m.planes->bytesused;
                venc_msg.buf.offset = v4l2_buf.m.planes->data_offset;
                venc_msg.buf.flags = 0;
                venc_msg.buf.ptrbuffer = (OMX_U8 *)omx_venc_base->m_pOutput_pmem[v4l2_buf.index].buffer;
                venc_msg.buf.clientdata=(void*)omxhdr;
                venc_msg.buf.timestamp = (uint64_t) v4l2_buf.timestamp.tv_sec * (uint64_t) 1000000 + (uint64_t) v4l2_buf.timestamp.tv_usec;

                /* TODO: ideally report other types of frames as well
                 * for now it doesn't look like IL client cares about
                 * other types
                 */
                if (v4l2_buf.flags & V4L2_QCOM_BUF_FLAG_IDRFRAME)
                    venc_msg.buf.flags |= QOMX_VIDEO_PictureTypeIDR;

                if (v4l2_buf.flags & V4L2_BUF_FLAG_KEYFRAME)
                    venc_msg.buf.flags |= OMX_BUFFERFLAG_SYNCFRAME;

                if (v4l2_buf.flags & V4L2_QCOM_BUF_FLAG_CODECCONFIG)
                    venc_msg.buf.flags |= OMX_BUFFERFLAG_CODECCONFIG;

                if (v4l2_buf.flags & V4L2_QCOM_BUF_FLAG_EOS)
                    venc_msg.buf.flags |= OMX_BUFFERFLAG_EOS;

                if (omx->handle->num_output_planes > 1 && v4l2_buf.m.planes->bytesused)
                    venc_msg.buf.flags |= OMX_BUFFERFLAG_EXTRADATA;

                if (omxhdr->nFilledLen)
                    venc_msg.buf.flags |= OMX_BUFFERFLAG_ENDOFFRAME;

                omx->handle->fbd++;
                stats.bytes_generated += venc_msg.buf.len;

                if (omx->async_message_process(input,&venc_msg) < 0) {
                    DEBUG_PRINT_ERROR("ERROR: Wrong ioctl message");
                    break;
                }
            }
        }

        if ((pfds[0].revents & POLLOUT) || (pfds[0].revents & POLLWRNORM)) {
            v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
            v4l2_buf.memory = V4L2_MEMORY_USERPTR;
            v4l2_buf.m.planes = plane;
            v4l2_buf.length = omx->handle->num_input_planes;

            while (!ioctl(pfds[0].fd, VIDIOC_DQBUF, &v4l2_buf)) {
                venc_msg.msgcode=VEN_MSG_INPUT_BUFFER_DONE;
                venc_msg.statuscode=VEN_S_SUCCESS;

                if (omx->handle->mBatchSize) {
                    int bufIndex = omx->handle->mBatchInfo.retrieveBufferAt(v4l2_buf.index);
                    if (bufIndex < 0) {
                        DEBUG_PRINT_ERROR("Retrieved invalid buffer %d", v4l2_buf.index);
                        break;
                    }
                    if (omx->handle->mBatchInfo.isPending(bufIndex)) {
                        DEBUG_PRINT_LOW(" EBD for %d [v4l2-id=%d].. batch still pending",
                                bufIndex, v4l2_buf.index);
                        //do not return to client yet
                        break;
                    }
                    v4l2_buf.index = bufIndex;
                }
                if (omx_venc_base->mUseProxyColorFormat && !omx_venc_base->mUsesColorConversion)
                    omxhdr = &omx_venc_base->meta_buffer_hdr[v4l2_buf.index];
                else
                    omxhdr = &omx_venc_base->m_inp_mem_ptr[v4l2_buf.index];

                venc_msg.buf.clientdata=(void*)omxhdr;
                omx->handle->ebd++;

                DEBUG_PRINT_LOW("sending EBD %p [id=%d]", omxhdr, v4l2_buf.index);
                if (omx->async_message_process(input,&venc_msg) < 0) {
                    DEBUG_PRINT_ERROR("ERROR: Wrong ioctl message");
                    break;
                }
            }
        }

        if (pfds[0].revents & POLLPRI) {
            rc = ioctl(pfds[0].fd, VIDIOC_DQEVENT, &dqevent);

            if (dqevent.type == V4L2_EVENT_MSM_VIDC_FLUSH_DONE) {
                venc_msg.msgcode = VEN_MSG_FLUSH_INPUT_DONE;
                venc_msg.statuscode = VEN_S_SUCCESS;

                if (omx->async_message_process(input,&venc_msg) < 0) {
                    DEBUG_PRINT_ERROR("ERROR: Wrong ioctl message");
                    break;
                }

                venc_msg.msgcode = VEN_MSG_FLUSH_OUPUT_DONE;
                venc_msg.statuscode = VEN_S_SUCCESS;

                if (omx->async_message_process(input,&venc_msg) < 0) {
                    DEBUG_PRINT_ERROR("ERROR: Wrong ioctl message");
                    break;
                }
            } else if (dqevent.type == V4L2_EVENT_MSM_VIDC_HW_OVERLOAD) {
                DEBUG_PRINT_ERROR("HW Overload received");
                venc_msg.statuscode = VEN_S_EFAIL;
                venc_msg.msgcode = VEN_MSG_HW_OVERLOAD;

                if (omx->async_message_process(input,&venc_msg) < 0) {
                    DEBUG_PRINT_ERROR("ERROR: Wrong ioctl message");
                    break;
                }
            } else if (dqevent.type == V4L2_EVENT_MSM_VIDC_SYS_ERROR){
                DEBUG_PRINT_ERROR("ERROR: Encoder is in bad state");
                venc_msg.msgcode = VEN_MSG_INDICATION;
                venc_msg.statuscode=VEN_S_EFAIL;

                if (omx->async_message_process(input,&venc_msg) < 0) {
                    DEBUG_PRINT_ERROR("ERROR: Wrong ioctl message");
                    break;
                }
            }
        }

        /* calc avg. fps, bitrate */
        struct timeval tv;
        gettimeofday(&tv,NULL);
        OMX_U64 time_diff = (OMX_U32)((tv.tv_sec * 1000000 + tv.tv_usec) -
                (stats.prev_tv.tv_sec * 1000000 + stats.prev_tv.tv_usec));
        if (time_diff >= 5000000) {
            if (stats.prev_tv.tv_sec) {
                OMX_U32 num_fbd = omx->handle->fbd - stats.prev_fbd;
                float framerate = num_fbd * 1000000/(float)time_diff;
                OMX_U32 bitrate = (stats.bytes_generated * 8/num_fbd) * framerate;
                DEBUG_PRINT_HIGH("stats: avg. fps %0.2f, bitrate %d",
                    framerate, bitrate);
            }
            stats.prev_tv = tv;
            stats.bytes_generated = 0;
            stats.prev_fbd = omx->handle->fbd;
        }

    }

    DEBUG_PRINT_HIGH("omx_venc: Async Thread exit");
    return NULL;
}

static const int event_type[] = {
    V4L2_EVENT_MSM_VIDC_FLUSH_DONE,
    V4L2_EVENT_MSM_VIDC_SYS_ERROR
};

static OMX_ERRORTYPE subscribe_to_events(int fd)
{
    OMX_ERRORTYPE eRet = OMX_ErrorNone;
    struct v4l2_event_subscription sub;
    int array_sz = sizeof(event_type)/sizeof(int);
    int i,rc;
    memset(&sub, 0, sizeof(sub));

    if (fd < 0) {
       DEBUG_PRINT_ERROR("Invalid input: %d", fd);
        return OMX_ErrorBadParameter;
    }

    for (i = 0; i < array_sz; ++i) {
        memset(&sub, 0, sizeof(sub));
        sub.type = event_type[i];
        rc = ioctl(fd, VIDIOC_SUBSCRIBE_EVENT, &sub);

        if (rc) {
           DEBUG_PRINT_ERROR("Failed to subscribe event: 0x%x", sub.type);
            break;
        }
    }

    if (i < array_sz) {
        for (--i; i >=0 ; i--) {
            memset(&sub, 0, sizeof(sub));
            sub.type = event_type[i];
            rc = ioctl(fd, VIDIOC_UNSUBSCRIBE_EVENT, &sub);

            if (rc)
               DEBUG_PRINT_ERROR("Failed to unsubscribe event: 0x%x", sub.type);
        }

        eRet = OMX_ErrorNotImplemented;
    }

    return eRet;
}

int venc_dev::append_mbi_extradata(void *dst, struct msm_vidc_extradata_header* src)
{
    OMX_QCOM_EXTRADATA_MBINFO *mbi = (OMX_QCOM_EXTRADATA_MBINFO *)dst;

    if (!dst || !src)
        return 0;

    /* TODO: Once Venus 3XX target names are known, nFormat should 2 for those
     * targets, since the payload format will be different */
    mbi->nFormat = 1;
    mbi->nDataSize = src->data_size;
    memcpy(&mbi->data, &src->data, src->data_size);

    return mbi->nDataSize + sizeof(*mbi);
}

bool venc_dev::handle_input_extradata(void *buffer, int index, int fd)
{
    OMX_BUFFERHEADERTYPE *p_bufhdr = (OMX_BUFFERHEADERTYPE *) buffer;
    OMX_OTHER_EXTRADATATYPE *p_extra = NULL;
    unsigned int consumed_len = 0;
    int enable = 0, i = 0;
    int height = 0, width = 0;


    if (!input_extradata_info.uaddr) {
        DEBUG_PRINT_ERROR("Extradata buffers not allocated\n");
        return false;
    }

    /*
     * At this point encoder component doesn't know where the extradata is
     * located in YUV buffer. For all practical usecases, decoder appends
     * extradata after nFilledLen which is calcualted as 32 aligned height
     * and width * 3 / 2. Hence start looking for extradata from this point.
     */

    height = ALIGN(m_sVenc_cfg.input_height, 32);
    width = ALIGN(m_sVenc_cfg.input_width, 32);

    index = venc_get_index_from_fd(fd);

    unsigned char *pVirt;
    int size = VENUS_BUFFER_SIZE(COLOR_FMT_NV12, width, height);
    pVirt= (unsigned char *)mmap(NULL, size, PROT_READ|PROT_WRITE,MAP_SHARED, fd, 0);

    p_extra = (OMX_OTHER_EXTRADATATYPE *) ((unsigned long)(pVirt + ((width * height * 3) / 2) + 3)&(~3));
    char *p_extradata = input_extradata_info.uaddr + index * input_extradata_info.buffer_size;
    OMX_OTHER_EXTRADATATYPE *data = (struct OMX_OTHER_EXTRADATATYPE *)p_extradata;
    if (p_extra) {
        while ((consumed_len < input_extradata_info.buffer_size)
            && (p_extra->eType != (OMX_EXTRADATATYPE)MSM_VIDC_EXTRADATA_NONE)) {
            DEBUG_PRINT_LOW("Extradata Type = 0x%x", (OMX_QCOM_EXTRADATATYPE)p_extra->eType);
            switch ((OMX_QCOM_EXTRADATATYPE)p_extra->eType) {
            case OMX_ExtraDataFrameDimension:
            {
                struct msm_vidc_extradata_index *payload;
                OMX_QCOM_EXTRADATA_FRAMEDIMENSION *framedimension_format;
                data->nSize = (sizeof(OMX_OTHER_EXTRADATATYPE) + sizeof(struct msm_vidc_extradata_index) + 3)&(~3);
                data->nVersion.nVersion = OMX_SPEC_VERSION;
                data->nPortIndex = 0;
                data->eType = (OMX_EXTRADATATYPE)MSM_VIDC_EXTRADATA_INDEX;
                data->nDataSize = sizeof(struct msm_vidc_input_crop_payload);
                framedimension_format = (OMX_QCOM_EXTRADATA_FRAMEDIMENSION *)p_extra->data;
                payload = (struct msm_vidc_extradata_index *)(data->data);
                payload->type = (msm_vidc_extradata_type)MSM_VIDC_EXTRADATA_INPUT_CROP;
                payload->input_crop.left = framedimension_format->nDecWidth;
                payload->input_crop.top = framedimension_format->nDecHeight;
                payload->input_crop.width = framedimension_format->nActualWidth;
                payload->input_crop.height = framedimension_format->nActualHeight;
                DEBUG_PRINT_LOW("Height = %d Width = %d Actual Height = %d Actual Width = %d",
                    framedimension_format->nDecWidth, framedimension_format->nDecHeight,
                    framedimension_format->nActualWidth, framedimension_format->nActualHeight);
                data = (OMX_OTHER_EXTRADATATYPE *)((char *)data + data->nSize);
                break;
            }
            case OMX_ExtraDataQP:
            {
                OMX_QCOM_EXTRADATA_QP * qp_payload = NULL;
                struct msm_vidc_frame_qp_payload *payload;
                data->nSize = (sizeof(OMX_OTHER_EXTRADATATYPE) + sizeof(struct msm_vidc_frame_qp_payload) + 3)&(~3);
                data->nVersion.nVersion = OMX_SPEC_VERSION;
                data->nPortIndex = 0;
                data->eType = (OMX_EXTRADATATYPE)MSM_VIDC_EXTRADATA_FRAME_QP;
                data->nDataSize = sizeof(struct  msm_vidc_frame_qp_payload);
                qp_payload = (OMX_QCOM_EXTRADATA_QP *)p_extra->data;
                payload = (struct  msm_vidc_frame_qp_payload *)(data->data);
                payload->frame_qp = qp_payload->nQP;
                DEBUG_PRINT_LOW("FRame QP = %d", payload->frame_qp);
                data = (OMX_OTHER_EXTRADATATYPE *)((char *)data + data->nSize);
                break;
            }
            default:
                break;
            }
            consumed_len += p_extra->nSize;
            p_extra = (OMX_OTHER_EXTRADATATYPE *)((char *)p_extra + p_extra->nSize);
        }

        data->nSize = (sizeof(OMX_OTHER_EXTRADATATYPE) +  sizeof(struct VQZipStats) + 3)&(~3);
        data->nVersion.nVersion = OMX_SPEC_VERSION;
        data->nPortIndex = 0;
        data->eType = (OMX_EXTRADATATYPE)MSM_VIDC_EXTRADATA_YUVSTATS_INFO;
        data->nDataSize = sizeof(struct VQZipStats);
        vqzip.fill_stats_data((void*)pVirt, (void*) data->data);

        data = (OMX_OTHER_EXTRADATATYPE *)((char *)data + data->nSize);

        data->nSize = sizeof(OMX_OTHER_EXTRADATATYPE);
        data->nVersion.nVersion = OMX_SPEC_VERSION;
        data->eType = OMX_ExtraDataNone;
        data->nDataSize = 0;
        data->data[0] = 0;

    }
    munmap(pVirt, size);
    return true;
}

bool venc_dev::handle_output_extradata(void *buffer, int index)
{
    OMX_BUFFERHEADERTYPE *p_bufhdr = (OMX_BUFFERHEADERTYPE *) buffer;
    OMX_OTHER_EXTRADATATYPE *p_extra = NULL;

    if (!output_extradata_info.uaddr) {
        DEBUG_PRINT_ERROR("Extradata buffers not allocated\n");
        return false;
    }

    p_extra = (OMX_OTHER_EXTRADATATYPE *)ALIGN(p_bufhdr->pBuffer +
                p_bufhdr->nOffset + p_bufhdr->nFilledLen, 4);

    if (output_extradata_info.buffer_size >
            p_bufhdr->nAllocLen - ALIGN(p_bufhdr->nOffset + p_bufhdr->nFilledLen, 4)) {
        DEBUG_PRINT_ERROR("Insufficient buffer size for extradata");
        p_extra = NULL;
        return false;
    } else if (sizeof(msm_vidc_extradata_header) != sizeof(OMX_OTHER_EXTRADATATYPE)) {
        /* A lot of the code below assumes this condition, so error out if it's not met */
        DEBUG_PRINT_ERROR("Extradata ABI mismatch");
        return false;
    }

    struct msm_vidc_extradata_header *p_extradata = NULL;
    do {
        p_extradata = (struct msm_vidc_extradata_header *) (p_extradata ?
            ((char *)p_extradata) + p_extradata->size :
            output_extradata_info.uaddr + index * output_extradata_info.buffer_size);

        switch (p_extradata->type) {
            case MSM_VIDC_EXTRADATA_METADATA_MBI:
            {
                OMX_U32 payloadSize = append_mbi_extradata(&p_extra->data, p_extradata);
                p_extra->nSize = ALIGN(sizeof(OMX_OTHER_EXTRADATATYPE) + payloadSize, 4);
                p_extra->nVersion.nVersion = OMX_SPEC_VERSION;
                p_extra->nPortIndex = OMX_DirOutput;
                p_extra->eType = (OMX_EXTRADATATYPE)OMX_ExtraDataVideoEncoderMBInfo;
                p_extra->nDataSize = payloadSize;
                break;
            }
            case MSM_VIDC_EXTRADATA_METADATA_LTR:
            {
                *p_extra->data = *p_extradata->data;
                p_extra->nSize = ALIGN(sizeof(OMX_OTHER_EXTRADATATYPE) + p_extradata->data_size, 4);
                p_extra->nVersion.nVersion = OMX_SPEC_VERSION;
                p_extra->nPortIndex = OMX_DirOutput;
                p_extra->eType = (OMX_EXTRADATATYPE) OMX_ExtraDataVideoLTRInfo;
                p_extra->nDataSize = p_extradata->data_size;
                break;
            }
            case MSM_VIDC_EXTRADATA_NONE:
                p_extra->nSize = ALIGN(sizeof(OMX_OTHER_EXTRADATATYPE), 4);
                p_extra->nVersion.nVersion = OMX_SPEC_VERSION;
                p_extra->nPortIndex = OMX_DirOutput;
                p_extra->eType = OMX_ExtraDataNone;
                p_extra->nDataSize = 0;
                break;
            default:
                /* No idea what this stuff is, just skip over it */
                DEBUG_PRINT_HIGH("Found an unrecognised extradata (%x) ignoring it",
                        p_extradata->type);
                continue;
        }

        p_extra = (OMX_OTHER_EXTRADATATYPE *)(((char *)p_extra) + p_extra->nSize);
    } while (p_extradata->type != MSM_VIDC_EXTRADATA_NONE);

    /* Just for debugging: Traverse the list of extra datas  and spit it out onto log */
    p_extra = (OMX_OTHER_EXTRADATATYPE *)ALIGN(p_bufhdr->pBuffer +
                p_bufhdr->nOffset + p_bufhdr->nFilledLen, 4);
    while(p_extra->eType != OMX_ExtraDataNone)
    {
        DEBUG_PRINT_LOW("[%p/%u] found extradata type %x of size %u (%u) at %p",
                p_bufhdr->pBuffer, (unsigned int)p_bufhdr->nFilledLen, p_extra->eType,
                (unsigned int)p_extra->nSize, (unsigned int)p_extra->nDataSize, p_extra);

        p_extra = (OMX_OTHER_EXTRADATATYPE *) (((OMX_U8 *) p_extra) +
                p_extra->nSize);
    }

    return true;
}

int venc_dev::venc_set_format(int format)
{
    int rc = true;

    if (format)
        color_format = format;
    else {
        color_format = 0;
        rc = false;
    }

    return rc;
}

OMX_ERRORTYPE venc_dev::allocate_extradata(struct extradata_buffer_info *extradata_info)
{
    if (extradata_info->allocated) {
        DEBUG_PRINT_HIGH("2nd allocation return for port = %d",extradata_info->port_index);
        return OMX_ErrorNone;
    }

#ifdef USE_ION

    if (extradata_info->buffer_size) {
        if (extradata_info->ion.ion_alloc_data.handle) {
            munmap((void *)extradata_info->uaddr, extradata_info->size);
            close(extradata_info->ion.fd_ion_data.fd);
            venc_handle->free_ion_memory(&extradata_info->ion);
        }

        extradata_info->size = (extradata_info->size + 4095) & (~4095);

        extradata_info->ion.ion_device_fd = venc_handle->alloc_map_ion_memory(
                extradata_info->size,
                &extradata_info->ion.ion_alloc_data,
                &extradata_info->ion.fd_ion_data, 0);

        if (extradata_info->ion.ion_device_fd < 0) {
            DEBUG_PRINT_ERROR("Failed to alloc extradata memory\n");
            return OMX_ErrorInsufficientResources;
        }

        extradata_info->uaddr = (char *)mmap(NULL,
                extradata_info->size,
                PROT_READ|PROT_WRITE, MAP_SHARED,
                extradata_info->ion.fd_ion_data.fd , 0);

        if (extradata_info->uaddr == MAP_FAILED) {
            DEBUG_PRINT_ERROR("Failed to map extradata memory\n");
            close(extradata_info->ion.fd_ion_data.fd);
            venc_handle->free_ion_memory(&extradata_info->ion);
            return OMX_ErrorInsufficientResources;
        }
    }

#endif
    extradata_info->allocated = OMX_TRUE;
    return OMX_ErrorNone;
}

void venc_dev::free_extradata()
{
#ifdef USE_ION

    if (output_extradata_info.uaddr) {
        munmap((void *)output_extradata_info.uaddr, output_extradata_info.size);
        close(output_extradata_info.ion.fd_ion_data.fd);
        venc_handle->free_ion_memory(&output_extradata_info.ion);
    }

    memset(&output_extradata_info, 0, sizeof(output_extradata_info));
    output_extradata_info.ion.fd_ion_data.fd = -1;

    if (input_extradata_info.uaddr) {
        munmap((void *)input_extradata_info.uaddr, input_extradata_info.size);
        close(input_extradata_info.ion.fd_ion_data.fd);
        venc_handle->free_ion_memory(&input_extradata_info.ion);
    }

    memset(&input_extradata_info, 0, sizeof(input_extradata_info));
    input_extradata_info.ion.fd_ion_data.fd = -1;

#endif
}

bool venc_dev::venc_get_output_log_flag()
{
    return (m_debug.out_buffer_log == 1);
}

int venc_dev::venc_output_log_buffers(const char *buffer_addr, int buffer_len)
{
    if (!m_debug.outfile) {
        int size = 0;
        if(m_sVenc_cfg.codectype == V4L2_PIX_FMT_MPEG4) {
           size = snprintf(m_debug.outfile_name, PROPERTY_VALUE_MAX, "%s/output_enc_%lu_%lu_%p.m4v",
                           m_debug.log_loc, m_sVenc_cfg.input_width, m_sVenc_cfg.input_height, this);
        } else if(m_sVenc_cfg.codectype == V4L2_PIX_FMT_H264) {
           size = snprintf(m_debug.outfile_name, PROPERTY_VALUE_MAX, "%s/output_enc_%lu_%lu_%p.264",
                           m_debug.log_loc, m_sVenc_cfg.input_width, m_sVenc_cfg.input_height, this);
        } else if(m_sVenc_cfg.codectype == V4L2_PIX_FMT_HEVC) {
           size = snprintf(m_debug.outfile_name, PROPERTY_VALUE_MAX, "%s/output_enc_%ld_%ld_%p.265",
                           m_debug.log_loc, m_sVenc_cfg.input_width, m_sVenc_cfg.input_height, this);
        } else if(m_sVenc_cfg.codectype == V4L2_PIX_FMT_H263) {
           size = snprintf(m_debug.outfile_name, PROPERTY_VALUE_MAX, "%s/output_enc_%lu_%lu_%p.263",
                           m_debug.log_loc, m_sVenc_cfg.input_width, m_sVenc_cfg.input_height, this);
        } else if(m_sVenc_cfg.codectype == V4L2_PIX_FMT_VP8) {
           size = snprintf(m_debug.outfile_name, PROPERTY_VALUE_MAX, "%s/output_enc_%lu_%lu_%p.ivf",
                           m_debug.log_loc, m_sVenc_cfg.input_width, m_sVenc_cfg.input_height, this);
        }
        if ((size > PROPERTY_VALUE_MAX) && (size < 0)) {
             DEBUG_PRINT_ERROR("Failed to open output file: %s for logging size:%d",
                                m_debug.outfile_name, size);
        }
        m_debug.outfile = fopen(m_debug.outfile_name, "ab");
        if (!m_debug.outfile) {
            DEBUG_PRINT_ERROR("Failed to open output file: %s for logging errno:%d",
                               m_debug.outfile_name, errno);
            m_debug.outfile_name[0] = '\0';
            return -1;
        }
    }
    if (m_debug.outfile && buffer_len) {
        DEBUG_PRINT_LOW("%s buffer_len:%d", __func__, buffer_len);
        fwrite(buffer_addr, buffer_len, 1, m_debug.outfile);
    }
    return 0;
}

int venc_dev::venc_extradata_log_buffers(char *buffer_addr)
{
    if (!m_debug.extradatafile && m_debug.extradata_log) {
        int size = 0;
        if(m_sVenc_cfg.codectype == V4L2_PIX_FMT_MPEG4) {
           size = snprintf(m_debug.extradatafile_name, PROPERTY_VALUE_MAX, "%s/extradata_enc_%lu_%lu_%p.m4v",
                           m_debug.log_loc, m_sVenc_cfg.input_width, m_sVenc_cfg.input_height, this);
        } else if(m_sVenc_cfg.codectype == V4L2_PIX_FMT_H264) {
           size = snprintf(m_debug.extradatafile_name, PROPERTY_VALUE_MAX, "%s/extradata_enc_%lu_%lu_%p.264",
                           m_debug.log_loc, m_sVenc_cfg.input_width, m_sVenc_cfg.input_height, this);
        } else if(m_sVenc_cfg.codectype == V4L2_PIX_FMT_HEVC) {
           size = snprintf(m_debug.extradatafile_name, PROPERTY_VALUE_MAX, "%s/extradata_enc_%lu_%lu_%p.265",
                           m_debug.log_loc, m_sVenc_cfg.input_width, m_sVenc_cfg.input_height, this);
        } else if(m_sVenc_cfg.codectype == V4L2_PIX_FMT_H263) {
           size = snprintf(m_debug.extradatafile_name, PROPERTY_VALUE_MAX, "%s/extradata_enc_%lu_%lu_%p.263",
                           m_debug.log_loc, m_sVenc_cfg.input_width, m_sVenc_cfg.input_height, this);
        } else if(m_sVenc_cfg.codectype == V4L2_PIX_FMT_VP8) {
           size = snprintf(m_debug.extradatafile_name, PROPERTY_VALUE_MAX, "%s/extradata_enc_%lu_%lu_%p.ivf",
                           m_debug.log_loc, m_sVenc_cfg.input_width, m_sVenc_cfg.input_height, this);
        }
        if ((size > PROPERTY_VALUE_MAX) && (size < 0)) {
             DEBUG_PRINT_ERROR("Failed to open extradata file: %s for logging size:%d",
                                m_debug.extradatafile_name, size);
        }

        m_debug.extradatafile = fopen(m_debug.extradatafile_name, "ab");
        if (!m_debug.extradatafile) {
            DEBUG_PRINT_ERROR("Failed to open extradata file: %s for logging errno:%d",
                               m_debug.extradatafile_name, errno);
            m_debug.extradatafile_name[0] = '\0';
            return -1;
        }
    }

    if (m_debug.extradatafile) {
        OMX_OTHER_EXTRADATATYPE *p_extra = NULL;
        do {
            p_extra = (OMX_OTHER_EXTRADATATYPE *)(!p_extra ? buffer_addr :
                    ((char *)p_extra) + p_extra->nSize);
            fwrite(p_extra, p_extra->nSize, 1, m_debug.extradatafile);
        } while (p_extra->eType != OMX_ExtraDataNone);
    }
    return 0;
}

int venc_dev::venc_input_log_buffers(OMX_BUFFERHEADERTYPE *pbuffer, int fd, int plane_offset,
        unsigned long inputformat) {
    if (!m_debug.infile) {
        int size = snprintf(m_debug.infile_name, PROPERTY_VALUE_MAX, "%s/input_enc_%lu_%lu_%p.yuv",
                            m_debug.log_loc, m_sVenc_cfg.input_width, m_sVenc_cfg.input_height, this);
        if ((size > PROPERTY_VALUE_MAX) && (size < 0)) {
             DEBUG_PRINT_ERROR("Failed to open output file: %s for logging size:%d",
                                m_debug.infile_name, size);
        }
        m_debug.infile = fopen (m_debug.infile_name, "ab");
        if (!m_debug.infile) {
            DEBUG_PRINT_HIGH("Failed to open input file: %s for logging", m_debug.infile_name);
            m_debug.infile_name[0] = '\0';
            return -1;
        }
    }

    if (m_debug.infile && pbuffer && pbuffer->nFilledLen) {
        int stride, scanlines;
        int color_format;
        unsigned long i, msize;
        unsigned char *pvirt = NULL, *ptemp = NULL;
        unsigned char *temp = (unsigned char *)pbuffer->pBuffer;

        switch (inputformat) {
            case V4L2_PIX_FMT_NV12:
                color_format = COLOR_FMT_NV12;
                break;
            case V4L2_PIX_FMT_NV12_UBWC:
                color_format = COLOR_FMT_NV12_UBWC;
                break;
            case V4L2_PIX_FMT_RGB32:
                color_format = COLOR_FMT_RGBA8888;
                break;
            case V4L2_PIX_FMT_RGBA8888_UBWC:
                color_format = COLOR_FMT_RGBA8888_UBWC;
                break;
            default:
                color_format = COLOR_FMT_NV12;
                DEBUG_PRINT_LOW("Default format NV12 is set for logging [%d]", inputformat);
                break;
        }

        msize = VENUS_BUFFER_SIZE(color_format, m_sVenc_cfg.input_width, m_sVenc_cfg.input_height);
        const unsigned int extra_size = VENUS_EXTRADATA_SIZE(m_sVenc_cfg.input_width, m_sVenc_cfg.input_height);

        if (metadatamode == 1) {
            pvirt= (unsigned char *)mmap(NULL, msize, PROT_READ|PROT_WRITE,MAP_SHARED, fd, plane_offset);
            if (pvirt == MAP_FAILED) {
                DEBUG_PRINT_ERROR("%s mmap failed", __func__);
                return -1;
            }
            ptemp = pvirt;
        } else {
            ptemp = temp;
        }

        if (color_format == COLOR_FMT_NV12) {
            stride = VENUS_Y_STRIDE(color_format, m_sVenc_cfg.input_width);
            scanlines = VENUS_Y_SCANLINES(color_format, m_sVenc_cfg.input_height);

            for (i = 0; i < m_sVenc_cfg.input_height; i++) {
                fwrite(ptemp, m_sVenc_cfg.input_width, 1, m_debug.infile);
                ptemp += stride;
            }
            if (metadatamode == 1) {
                ptemp = pvirt + (stride * scanlines);
            } else {
                ptemp = (unsigned char *)pbuffer->pBuffer + (stride * scanlines);
            }
            for (i = 0; i < m_sVenc_cfg.input_height/2; i++) {
                fwrite(ptemp, m_sVenc_cfg.input_width, 1, m_debug.infile);
                ptemp += stride;
            }
        } else if (color_format == COLOR_FMT_RGBA8888) {
            stride = VENUS_RGB_STRIDE(color_format, m_sVenc_cfg.input_width);
            scanlines = VENUS_RGB_SCANLINES(color_format, m_sVenc_cfg.input_height);

            for (i = 0; i < m_sVenc_cfg.input_height; i++) {
                fwrite(ptemp, m_sVenc_cfg.input_width * 4, 1, m_debug.infile);
                ptemp += stride;
            }
        } else if (color_format == COLOR_FMT_NV12_UBWC || color_format == COLOR_FMT_RGBA8888_UBWC) {
            if (color_format == COLOR_FMT_NV12_UBWC) {
                msize -= 2 * extra_size;
            }
            fwrite(ptemp, msize, 1, m_debug.infile);
        }

        if (metadatamode == 1 && pvirt) {
            munmap(pvirt, msize);
        }
    }

    return 0;
}

bool venc_dev::venc_open(OMX_U32 codec)
{
    int r;
    unsigned int alignment = 0,buffer_size = 0, temp =0;
    struct v4l2_control control;
    OMX_STRING device_name = (OMX_STRING)"/dev/video33";
    char property_value[PROPERTY_VALUE_MAX] = {0};
    char platform_name[PROPERTY_VALUE_MAX] = {0};
    FILE *soc_file = NULL;
    char buffer[10];

    property_get("ro.board.platform", platform_name, "0");
    property_get("vidc.enc.narrow.searchrange", property_value, "0");
    enable_mv_narrow_searchrange = atoi(property_value);

    if (!strncmp(platform_name, "msm8610", 7)) {
        device_name = (OMX_STRING)"/dev/video/q6_enc";
        supported_rc_modes = (RC_ALL & ~RC_CBR_CFR);
    }
    m_nDriver_fd = open (device_name, O_RDWR);
    if ((int)m_nDriver_fd < 0) {
        DEBUG_PRINT_ERROR("ERROR: Omx_venc::Comp Init Returning failure");
        return false;
    }
    m_poll_efd = eventfd(0, 0);
    if (m_poll_efd < 0) {
        DEBUG_PRINT_ERROR("Failed to open event fd(%s)", strerror(errno));
        return false;
    }
    DEBUG_PRINT_LOW("m_nDriver_fd = %u", (unsigned int)m_nDriver_fd);

    // set the basic configuration of the video encoder driver
    m_sVenc_cfg.input_width = OMX_CORE_QCIF_WIDTH;
    m_sVenc_cfg.input_height= OMX_CORE_QCIF_HEIGHT;
    m_sVenc_cfg.dvs_width = OMX_CORE_QCIF_WIDTH;
    m_sVenc_cfg.dvs_height = OMX_CORE_QCIF_HEIGHT;
    m_sVenc_cfg.fps_num = 30;
    m_sVenc_cfg.fps_den = 1;
    m_sVenc_cfg.targetbitrate = 64000;
    m_sVenc_cfg.inputformat= V4L2_DEFAULT_OUTPUT_COLOR_FMT;

    m_codec = codec;

    if (codec == OMX_VIDEO_CodingMPEG4) {
        m_sVenc_cfg.codectype = V4L2_PIX_FMT_MPEG4;
        codec_profile.profile = V4L2_MPEG_VIDEO_MPEG4_PROFILE_SIMPLE;
        profile_level.level = V4L2_MPEG_VIDEO_MPEG4_LEVEL_2;
        session_qp_range.minqp = 1;
        session_qp_range.maxqp = 31;
    } else if (codec == OMX_VIDEO_CodingH263) {
        m_sVenc_cfg.codectype = V4L2_PIX_FMT_H263;
        codec_profile.profile = VEN_PROFILE_H263_BASELINE;
        profile_level.level = VEN_LEVEL_H263_20;
        session_qp_range.minqp = 1;
        session_qp_range.maxqp = 31;
    } else if (codec == OMX_VIDEO_CodingAVC) {
        m_sVenc_cfg.codectype = V4L2_PIX_FMT_H264;
        codec_profile.profile = V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE;
        profile_level.level = V4L2_MPEG_VIDEO_H264_LEVEL_1_0;
        session_qp_range.minqp = 1;
        session_qp_range.maxqp = 51;
    } else if (codec == OMX_VIDEO_CodingVP8) {
        m_sVenc_cfg.codectype = V4L2_PIX_FMT_VP8;
        codec_profile.profile = V4L2_MPEG_VIDC_VIDEO_VP8_UNUSED;
        profile_level.level = V4L2_MPEG_VIDC_VIDEO_VP8_VERSION_0;
        session_qp_range.minqp = 1;
        session_qp_range.maxqp = 128;
    } else if (codec == OMX_VIDEO_CodingHEVC) {
        m_sVenc_cfg.codectype = V4L2_PIX_FMT_HEVC;
        session_qp_range.minqp = 1;
        session_qp_range.maxqp = 51;
        codec_profile.profile = V4L2_MPEG_VIDC_VIDEO_HEVC_PROFILE_MAIN;
        profile_level.level = V4L2_MPEG_VIDC_VIDEO_HEVC_LEVEL_MAIN_TIER_LEVEL_1;
    }
    session_qp_values.minqp = session_qp_range.minqp;
    session_qp_values.maxqp = session_qp_range.maxqp;

    int ret;
    ret = subscribe_to_events(m_nDriver_fd);

    if (ret) {
        DEBUG_PRINT_ERROR("Subscribe Event Failed");
        return false;
    }

    struct v4l2_fmtdesc fdesc;
    struct v4l2_format fmt;
    struct v4l2_requestbuffers bufreq;
    struct v4l2_capability cap;

    ret = ioctl(m_nDriver_fd, VIDIOC_QUERYCAP, &cap);

    if (ret) {
        DEBUG_PRINT_ERROR("Failed to query capabilities");
    } else {
        DEBUG_PRINT_LOW("Capabilities: driver_name = %s, card = %s, bus_info = %s,"
                " version = %d, capabilities = %x", cap.driver, cap.card,
                cap.bus_info, cap.version, cap.capabilities);
    }

    ret=0;
    fdesc.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    fdesc.index=0;

    while (ioctl(m_nDriver_fd, VIDIOC_ENUM_FMT, &fdesc) == 0) {
        DEBUG_PRINT_LOW("fmt: description: %s, fmt: %x, flags = %x", fdesc.description,
                fdesc.pixelformat, fdesc.flags);
        fdesc.index++;
    }

    fdesc.type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    fdesc.index=0;

    while (ioctl(m_nDriver_fd, VIDIOC_ENUM_FMT, &fdesc) == 0) {
        DEBUG_PRINT_LOW("fmt: description: %s, fmt: %x, flags = %x", fdesc.description,
                fdesc.pixelformat, fdesc.flags);
        fdesc.index++;
    }

    is_thulium_v1 = false;
    soc_file= fopen("/sys/devices/soc0/soc_id", "r");
    if (soc_file) {
        fread(buffer, 1, 4, soc_file);
        fclose(soc_file);
        if (atoi(buffer) == 246) {
            soc_file = fopen("/sys/devices/soc0/revision", "r");
            if (soc_file) {
                fread(buffer, 1, 4, soc_file);
                fclose(soc_file);
                if (atoi(buffer) == 1) {
                    is_thulium_v1 = true;
                    DEBUG_PRINT_HIGH("is_thulium_v1 = TRUE");
                }
            }
        }
    }

    if (venc_handle->is_secure_session()) {
        m_sOutput_buff_property.alignment = SZ_1M;
        m_sInput_buff_property.alignment  = SZ_1M;
    } else {
        m_sOutput_buff_property.alignment = SZ_4K;
        m_sInput_buff_property.alignment  = SZ_4K;
    }
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    fmt.fmt.pix_mp.height = m_sVenc_cfg.dvs_height;
    fmt.fmt.pix_mp.width = m_sVenc_cfg.dvs_width;
    fmt.fmt.pix_mp.pixelformat = m_sVenc_cfg.codectype;

    /*TODO: Return values not handled properly in this function anywhere.
     * Need to handle those.*/
    ret = ioctl(m_nDriver_fd, VIDIOC_S_FMT, &fmt);

    if (ret) {
        DEBUG_PRINT_ERROR("Failed to set format on capture port");
        return false;
    }

    m_sOutput_buff_property.datasize=fmt.fmt.pix_mp.plane_fmt[0].sizeimage;

    fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    fmt.fmt.pix_mp.height = m_sVenc_cfg.input_height;
    fmt.fmt.pix_mp.width = m_sVenc_cfg.input_width;
    fmt.fmt.pix_mp.pixelformat = V4L2_DEFAULT_OUTPUT_COLOR_FMT;

    ret = ioctl(m_nDriver_fd, VIDIOC_S_FMT, &fmt);
    m_sInput_buff_property.datasize=fmt.fmt.pix_mp.plane_fmt[0].sizeimage;

    bufreq.memory = V4L2_MEMORY_USERPTR;
    bufreq.count = 2;

    bufreq.type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    ret = ioctl(m_nDriver_fd,VIDIOC_REQBUFS, &bufreq);
    m_sInput_buff_property.mincount = m_sInput_buff_property.actualcount = bufreq.count;

    bufreq.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    bufreq.count = 2;
    ret = ioctl(m_nDriver_fd,VIDIOC_REQBUFS, &bufreq);
    m_sOutput_buff_property.mincount = m_sOutput_buff_property.actualcount = bufreq.count;

    if(venc_handle->is_secure_session()) {
        control.id = V4L2_CID_MPEG_VIDC_VIDEO_SECURE;
        control.value = 1;
        DEBUG_PRINT_HIGH("ioctl: open secure device");
        ret=ioctl(m_nDriver_fd, VIDIOC_S_CTRL,&control);
        if (ret) {
            DEBUG_PRINT_ERROR("ioctl: open secure dev fail, rc %d", ret);
            return false;
        }
    }

    resume_in_stopped = 0;
    metadatamode = 0;

    control.id = V4L2_CID_MPEG_VIDEO_HEADER_MODE;
    control.value = V4L2_MPEG_VIDEO_HEADER_MODE_SEPARATE;

    DEBUG_PRINT_LOW("Calling IOCTL to disable seq_hdr in sync_frame id=%d, val=%d", control.id, control.value);

    if (ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control))
        DEBUG_PRINT_ERROR("Failed to set control");

    struct v4l2_frmsizeenum frmsize;

    //Get the hardware capabilities
    memset((void *)&frmsize,0,sizeof(frmsize));
    frmsize.index = 0;
    frmsize.pixel_format = m_sVenc_cfg.codectype;
    ret = ioctl(m_nDriver_fd, VIDIOC_ENUM_FRAMESIZES, &frmsize);

    if (ret || frmsize.type != V4L2_FRMSIZE_TYPE_STEPWISE) {
        DEBUG_PRINT_ERROR("Failed to get framesizes");
        return false;
    }

    if (frmsize.type == V4L2_FRMSIZE_TYPE_STEPWISE) {
        capability.min_width = frmsize.stepwise.min_width;
        capability.max_width = frmsize.stepwise.max_width;
        capability.min_height = frmsize.stepwise.min_height;
        capability.max_height = frmsize.stepwise.max_height;
    }

    //Initialize non-default parameters
    if (m_sVenc_cfg.codectype == V4L2_PIX_FMT_VP8) {
        control.id = V4L2_CID_MPEG_VIDC_VIDEO_NUM_P_FRAMES;
        control.value = 0x7fffffff;
        if (ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control))
            DEBUG_PRINT_ERROR("Failed to set V4L2_CID_MPEG_VIDC_VIDEO_NUM_P_FRAME\n");
    }

    property_get("vidc.debug.turbo", property_value, "0");
    if (atoi(property_value)) {
        DEBUG_PRINT_HIGH("Turbo mode debug property enabled");
        control.id = V4L2_CID_MPEG_VIDC_SET_PERF_LEVEL;
        control.value = V4L2_CID_MPEG_VIDC_PERF_LEVEL_TURBO;
        if (ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control)) {
            DEBUG_PRINT_ERROR("Failed to set turbo mode");
        }
    }

    input_extradata_info.port_index = OUTPUT_PORT;
    output_extradata_info.port_index = CAPTURE_PORT;
    return true;
}


static OMX_ERRORTYPE unsubscribe_to_events(int fd)
{
    OMX_ERRORTYPE eRet = OMX_ErrorNone;
    struct v4l2_event_subscription sub;
    int array_sz = sizeof(event_type)/sizeof(int);
    int i,rc;

    if (fd < 0) {
       DEBUG_PRINT_ERROR("Invalid input: %d", fd);
        return OMX_ErrorBadParameter;
    }

    for (i = 0; i < array_sz; ++i) {
        memset(&sub, 0, sizeof(sub));
        sub.type = event_type[i];
        rc = ioctl(fd, VIDIOC_UNSUBSCRIBE_EVENT, &sub);

        if (rc) {
           DEBUG_PRINT_ERROR("Failed to unsubscribe event: 0x%x", sub.type);
            break;
        }
    }

    return eRet;
}

void venc_dev::venc_close()
{
    DEBUG_PRINT_LOW("venc_close: fd = %u", (unsigned int)m_nDriver_fd);

    if ((int)m_nDriver_fd >= 0) {
        DEBUG_PRINT_HIGH("venc_close E");

        if(!eventfd_write(m_poll_efd, 1)) {
            if (async_thread_created)
                pthread_join(m_tid,NULL);
        }

        DEBUG_PRINT_HIGH("venc_close X");
        unsubscribe_to_events(m_nDriver_fd);
        close(m_poll_efd);
        close(m_nDriver_fd);
        m_nDriver_fd = -1;
    }

    if (m_debug.infile) {
        fclose(m_debug.infile);
        m_debug.infile = NULL;
    }

    if (m_debug.outfile) {
        fclose(m_debug.outfile);
        m_debug.outfile = NULL;
    }

    if (m_debug.extradatafile) {
        fclose(m_debug.extradatafile);
        m_debug.extradatafile = NULL;
    }
}

bool venc_dev::venc_set_buf_req(OMX_U32 *min_buff_count,
        OMX_U32 *actual_buff_count,
        OMX_U32 *buff_size,
        OMX_U32 port)
{
    (void)min_buff_count, (void)buff_size;
    unsigned long temp_count = 0;

    if (port == 0) {
        if (*actual_buff_count > m_sInput_buff_property.mincount) {
            temp_count = m_sInput_buff_property.actualcount;
            m_sInput_buff_property.actualcount = *actual_buff_count;
            DEBUG_PRINT_LOW("I/P Count set to %u", (unsigned int)*actual_buff_count);
        }
    } else {
        if (*actual_buff_count > m_sOutput_buff_property.mincount) {
            temp_count = m_sOutput_buff_property.actualcount;
            m_sOutput_buff_property.actualcount = *actual_buff_count;
            DEBUG_PRINT_LOW("O/P Count set to %u", (unsigned int)*actual_buff_count);
        }
    }

    return true;

}

bool venc_dev::venc_loaded_start()
{
    return true;
}

bool venc_dev::venc_loaded_stop()
{
    return true;
}

bool venc_dev::venc_loaded_start_done()
{
    return true;
}

bool venc_dev::venc_loaded_stop_done()
{
    return true;
}

bool venc_dev::venc_get_seq_hdr(void *buffer,
        unsigned buffer_size, unsigned *header_len)
{
    (void) buffer, (void) buffer_size, (void) header_len;
    return true;
}

bool venc_dev::venc_get_buf_req(OMX_U32 *min_buff_count,
        OMX_U32 *actual_buff_count,
        OMX_U32 *buff_size,
        OMX_U32 port)
{
    struct v4l2_format fmt;
    struct v4l2_requestbuffers bufreq;
    unsigned int buf_size = 0, extra_data_size = 0, client_extra_data_size = 0;
    int ret;
    int extra_idx = 0;

    if (port == 0) {
        fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        fmt.fmt.pix_mp.height = m_sVenc_cfg.input_height;
        fmt.fmt.pix_mp.width = m_sVenc_cfg.input_width;
        fmt.fmt.pix_mp.pixelformat = m_sVenc_cfg.inputformat;
        ret = ioctl(m_nDriver_fd, VIDIOC_G_FMT, &fmt);
        m_sInput_buff_property.datasize=fmt.fmt.pix_mp.plane_fmt[0].sizeimage;
        bufreq.memory = V4L2_MEMORY_USERPTR;

        if (*actual_buff_count)
            bufreq.count = *actual_buff_count;
        else
            bufreq.count = 2;

        // Increase buffer-header count for metadata-mode on input port
        // to improve buffering and reduce bottlenecks in clients
        if (metadatamode && (bufreq.count < 9)) {
            DEBUG_PRINT_LOW("FW returned buffer count = %d , overwriting with 9",
                bufreq.count);
            bufreq.count = 9;
        }
        if (m_sVenc_cfg.input_height * m_sVenc_cfg.input_width >= 3840*2160) {
            DEBUG_PRINT_LOW("Increasing buffer count = %d to 11", bufreq.count);
            bufreq.count = 11;
        }

        int actualCount = bufreq.count;
        // Request MAX_V4L2_BUFS from V4L2 in batch mode.
        // Keep the original count for the client
        if (metadatamode && mBatchSize) {
            bufreq.count = MAX_V4L2_BUFS;
        }

        bufreq.type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        ret = ioctl(m_nDriver_fd,VIDIOC_REQBUFS, &bufreq);

        if (ret) {
            DEBUG_PRINT_ERROR("VIDIOC_REQBUFS OUTPUT_MPLANE Failed");
            return false;
        }
        fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        fmt.fmt.pix_mp.height = m_sVenc_cfg.input_height;
        fmt.fmt.pix_mp.width = m_sVenc_cfg.input_width;
        fmt.fmt.pix_mp.pixelformat = m_sVenc_cfg.inputformat;
        ret = ioctl(m_nDriver_fd, VIDIOC_G_FMT, &fmt);
        m_sInput_buff_property.datasize=fmt.fmt.pix_mp.plane_fmt[0].sizeimage;

        m_sInput_buff_property.mincount = m_sInput_buff_property.actualcount = actualCount;
        *min_buff_count = m_sInput_buff_property.mincount;
        *actual_buff_count = m_sInput_buff_property.actualcount;
#ifdef USE_ION
        // For ION memory allocations of the allocated buffer size
        // must be 4k aligned, hence aligning the input buffer
        // size to 4k.
        m_sInput_buff_property.datasize = ALIGN(m_sInput_buff_property.datasize, SZ_4K);
#endif
        *buff_size = m_sInput_buff_property.datasize;
        num_input_planes = fmt.fmt.pix_mp.num_planes;
        extra_idx = EXTRADATA_IDX(num_input_planes);

        if (extra_idx && (extra_idx < VIDEO_MAX_PLANES)) {
            extra_data_size =  fmt.fmt.pix_mp.plane_fmt[extra_idx].sizeimage;
        } else if (extra_idx >= VIDEO_MAX_PLANES) {
            DEBUG_PRINT_ERROR("Extradata index is more than allowed: %d\n", extra_idx);
            return OMX_ErrorBadParameter;
        }
        input_extradata_info.buffer_size = extra_data_size;
        input_extradata_info.count = m_sInput_buff_property.actualcount + 1;
        input_extradata_info.size = input_extradata_info.buffer_size * input_extradata_info.count;

    } else {
        unsigned int extra_idx = 0;
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        fmt.fmt.pix_mp.height = m_sVenc_cfg.dvs_height;
        fmt.fmt.pix_mp.width = m_sVenc_cfg.dvs_width;
        fmt.fmt.pix_mp.pixelformat = m_sVenc_cfg.codectype;

        ret = ioctl(m_nDriver_fd, VIDIOC_S_FMT, &fmt);
        m_sOutput_buff_property.datasize=fmt.fmt.pix_mp.plane_fmt[0].sizeimage;
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        fmt.fmt.pix_mp.height = m_sVenc_cfg.dvs_height;
        fmt.fmt.pix_mp.width = m_sVenc_cfg.dvs_width;
        fmt.fmt.pix_mp.pixelformat = m_sVenc_cfg.codectype;

        ret = ioctl(m_nDriver_fd, VIDIOC_G_FMT, &fmt);
        m_sOutput_buff_property.datasize=fmt.fmt.pix_mp.plane_fmt[0].sizeimage;
        bufreq.memory = V4L2_MEMORY_USERPTR;

        if (mBatchSize) {
            // If we're in batch mode, we'd like to end up in a situation where
            // driver is able to own mBatchSize buffers and we'd also own atleast
            // mBatchSize buffers
            bufreq.count = MAX(*actual_buff_count, mBatchSize) + mBatchSize;
        } else if (*actual_buff_count) {
            bufreq.count = *actual_buff_count;
        } else {
            bufreq.count = 2;
        }

        bufreq.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        ret = ioctl(m_nDriver_fd,VIDIOC_REQBUFS, &bufreq);

        if (ret) {
            DEBUG_PRINT_ERROR("VIDIOC_REQBUFS CAPTURE_MPLANE Failed");
            return false;
        }

        m_sOutput_buff_property.mincount = m_sOutput_buff_property.actualcount = bufreq.count;
        *min_buff_count = m_sOutput_buff_property.mincount;
        *actual_buff_count = m_sOutput_buff_property.actualcount;
        *buff_size = m_sOutput_buff_property.datasize;
        num_output_planes = fmt.fmt.pix_mp.num_planes;
        extra_idx = EXTRADATA_IDX(num_output_planes);

        if (extra_idx && (extra_idx < VIDEO_MAX_PLANES)) {
            extra_data_size =  fmt.fmt.pix_mp.plane_fmt[extra_idx].sizeimage;
        } else if (extra_idx >= VIDEO_MAX_PLANES) {
            DEBUG_PRINT_ERROR("Extradata index is more than allowed: %d", extra_idx);
            return OMX_ErrorBadParameter;
        }

        output_extradata_info.buffer_size = extra_data_size;
        output_extradata_info.count = m_sOutput_buff_property.actualcount;
        output_extradata_info.size = output_extradata_info.buffer_size * output_extradata_info.count;
    }

    return true;
}

bool venc_dev::venc_set_param(void *paramData, OMX_INDEXTYPE index)
{
    DEBUG_PRINT_LOW("venc_set_param:: venc-720p");
    struct v4l2_format fmt;
    struct v4l2_requestbuffers bufreq;
    int ret;

    switch ((int)index) {
        case OMX_IndexParamPortDefinition:
            {
                OMX_PARAM_PORTDEFINITIONTYPE *portDefn;
                portDefn = (OMX_PARAM_PORTDEFINITIONTYPE *) paramData;
                DEBUG_PRINT_LOW("venc_set_param: OMX_IndexParamPortDefinition");

                if (portDefn->nPortIndex == PORT_INDEX_IN) {
                    if (!venc_set_encode_framerate(portDefn->format.video.xFramerate, 0)) {
                        return false;
                    }

                    if (!venc_set_color_format(portDefn->format.video.eColorFormat)) {
                        return false;
                    }
                    if (enable_mv_narrow_searchrange &&
                        (m_sVenc_cfg.input_width * m_sVenc_cfg.input_height) >=
                        (OMX_CORE_1080P_WIDTH * OMX_CORE_1080P_HEIGHT)) {
                        if (venc_set_searchrange() == false) {
                            DEBUG_PRINT_ERROR("ERROR: Failed to set search range");
                        }
                    }
                    if (m_sVenc_cfg.input_height != portDefn->format.video.nFrameHeight ||
                            m_sVenc_cfg.input_width != portDefn->format.video.nFrameWidth) {
                        DEBUG_PRINT_LOW("Basic parameter has changed");
                        m_sVenc_cfg.input_height = portDefn->format.video.nFrameHeight;
                        m_sVenc_cfg.input_width = portDefn->format.video.nFrameWidth;
                        fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
                        fmt.fmt.pix_mp.height = m_sVenc_cfg.input_height;
                        fmt.fmt.pix_mp.width = m_sVenc_cfg.input_width;
                        fmt.fmt.pix_mp.pixelformat = m_sVenc_cfg.inputformat;

                        if (ioctl(m_nDriver_fd, VIDIOC_S_FMT, &fmt)) {
                            DEBUG_PRINT_ERROR("VIDIOC_S_FMT OUTPUT_MPLANE Failed");
                            hw_overload = errno == EBUSY;
                            return false;
                        }

                        m_sInput_buff_property.datasize=fmt.fmt.pix_mp.plane_fmt[0].sizeimage;
                        bufreq.memory = V4L2_MEMORY_USERPTR;
                        bufreq.count = portDefn->nBufferCountActual;
                        bufreq.type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;

                        if (ioctl(m_nDriver_fd,VIDIOC_REQBUFS, &bufreq)) {
                            DEBUG_PRINT_ERROR("VIDIOC_REQBUFS OUTPUT_MPLANE Failed");
                            return false;
                        }

                        if (bufreq.count == portDefn->nBufferCountActual)
                            m_sInput_buff_property.mincount = m_sInput_buff_property.actualcount = bufreq.count;

                        if (portDefn->nBufferCountActual >= m_sInput_buff_property.mincount)
                            m_sInput_buff_property.actualcount = portDefn->nBufferCountActual;
                        if (num_input_planes > 1)
                            input_extradata_info.count = m_sInput_buff_property.actualcount + 1;

                    }

                    DEBUG_PRINT_LOW("input: actual: %u, min: %u, count_req: %u",
                            (unsigned int)portDefn->nBufferCountActual, (unsigned int)m_sInput_buff_property.mincount, bufreq.count);
                } else if (portDefn->nPortIndex == PORT_INDEX_OUT) {
                    m_sVenc_cfg.dvs_height = portDefn->format.video.nFrameHeight;
                    m_sVenc_cfg.dvs_width = portDefn->format.video.nFrameWidth;
                    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
                    fmt.fmt.pix_mp.height = m_sVenc_cfg.dvs_height;
                    fmt.fmt.pix_mp.width = m_sVenc_cfg.dvs_width;
                    fmt.fmt.pix_mp.pixelformat = m_sVenc_cfg.codectype;

                    if (ioctl(m_nDriver_fd, VIDIOC_S_FMT, &fmt)) {
                        DEBUG_PRINT_ERROR("VIDIOC_S_FMT CAPTURE_MPLANE Failed");
                        hw_overload = errno == EBUSY;
                        return false;
                    }

                    m_sOutput_buff_property.datasize = fmt.fmt.pix_mp.plane_fmt[0].sizeimage;

                    if (!venc_set_target_bitrate(portDefn->format.video.nBitrate, 0)) {
                        return false;
                    }

                        m_sOutput_buff_property.actualcount = portDefn->nBufferCountActual;
                        bufreq.memory = V4L2_MEMORY_USERPTR;
                        bufreq.count = portDefn->nBufferCountActual;
                        bufreq.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

                        if (ioctl(m_nDriver_fd,VIDIOC_REQBUFS, &bufreq)) {
                            DEBUG_PRINT_ERROR("ERROR: Request for setting o/p buffer count failed: requested: %u, current: %u",
                                    (unsigned int)portDefn->nBufferCountActual, (unsigned int)m_sOutput_buff_property.actualcount);
                            return false;
                        }

                        if (bufreq.count == portDefn->nBufferCountActual)
                            m_sOutput_buff_property.mincount = m_sOutput_buff_property.actualcount = bufreq.count;

                        if (portDefn->nBufferCountActual >= m_sOutput_buff_property.mincount)
                            m_sOutput_buff_property.actualcount = portDefn->nBufferCountActual;

                        if (num_output_planes > 1)
                            output_extradata_info.count = m_sOutput_buff_property.actualcount;

                    DEBUG_PRINT_LOW("Output: actual: %u, min: %u, count_req: %u",
                            (unsigned int)portDefn->nBufferCountActual, (unsigned int)m_sOutput_buff_property.mincount, bufreq.count);
                } else {
                    DEBUG_PRINT_ERROR("ERROR: Invalid Port Index for OMX_IndexParamPortDefinition");
                }

                break;
            }
        case OMX_IndexParamVideoPortFormat:
            {
                OMX_VIDEO_PARAM_PORTFORMATTYPE *portFmt;
                portFmt =(OMX_VIDEO_PARAM_PORTFORMATTYPE *)paramData;
                DEBUG_PRINT_LOW("venc_set_param: OMX_IndexParamVideoPortFormat");

                if (portFmt->nPortIndex == (OMX_U32) PORT_INDEX_IN) {
                    if (!venc_set_color_format(portFmt->eColorFormat)) {
                        return false;
                    }
                } else if (portFmt->nPortIndex == (OMX_U32) PORT_INDEX_OUT) {
                    if (!venc_set_encode_framerate(portFmt->xFramerate, 0)) {
                        return false;
                    }
                } else {
                    DEBUG_PRINT_ERROR("ERROR: Invalid Port Index for OMX_IndexParamVideoPortFormat");
                }

                break;
            }
        case OMX_IndexParamVideoBitrate:
            {
                OMX_VIDEO_PARAM_BITRATETYPE* pParam;
                pParam = (OMX_VIDEO_PARAM_BITRATETYPE*)paramData;
                DEBUG_PRINT_LOW("venc_set_param: OMX_IndexParamVideoBitrate");

                if (pParam->nPortIndex == (OMX_U32) PORT_INDEX_OUT) {
                    if (!venc_set_target_bitrate(pParam->nTargetBitrate, 0)) {
                        DEBUG_PRINT_ERROR("ERROR: Target Bit Rate setting failed");
                        return false;
                    }

                    if (!venc_set_ratectrl_cfg(pParam->eControlRate)) {
                        DEBUG_PRINT_ERROR("ERROR: Rate Control setting failed");
                        return false;
                    }
                } else {
                    DEBUG_PRINT_ERROR("ERROR: Invalid Port Index for OMX_IndexParamVideoBitrate");
                }

                break;
            }
        case OMX_IndexParamVideoMpeg4:
            {
                OMX_VIDEO_PARAM_MPEG4TYPE* pParam;
                OMX_U32 bFrames = 0;

                pParam = (OMX_VIDEO_PARAM_MPEG4TYPE*)paramData;
                DEBUG_PRINT_LOW("venc_set_param: OMX_IndexParamVideoMpeg4");

                if (pParam->nPortIndex == (OMX_U32) PORT_INDEX_OUT) {
                    if (!venc_set_voptiming_cfg(pParam->nTimeIncRes)) {
                        DEBUG_PRINT_ERROR("ERROR: Request for setting vop_timing failed");
                        return false;
                    }

                    m_profile_set = false;
                    m_level_set = false;
                    rc_off_level = (int)pParam->eLevel;
                    if (!venc_set_profile_level (pParam->eProfile, pParam->eLevel)) {
                        DEBUG_PRINT_ERROR("ERROR: Unsuccessful in updating Profile and level");
                        return false;
                    } else {
                        if (pParam->eProfile == OMX_VIDEO_MPEG4ProfileAdvancedSimple) {
                            if (pParam->nBFrames) {
                                bFrames = pParam->nBFrames;
                            }
                        } else {
                            if (pParam->nBFrames) {
                                DEBUG_PRINT_ERROR("Warning: B frames not supported");
                                bFrames = 0;
                            }
                        }
                    }

                    if (!venc_set_intra_period (pParam->nPFrames,bFrames)) {
                        DEBUG_PRINT_ERROR("ERROR: Request for setting intra period failed");
                        return false;
                    }

                    if (!venc_set_multislice_cfg(OMX_IndexParamVideoMpeg4,pParam->nSliceHeaderSpacing)) {
                        DEBUG_PRINT_ERROR("ERROR: Unsuccessful in updating slice_config");
                        return false;
                    }
                } else {
                    DEBUG_PRINT_ERROR("ERROR: Invalid Port Index for OMX_IndexParamVideoMpeg4");
                }

                break;
            }
        case OMX_IndexParamVideoH263:
            {
                OMX_VIDEO_PARAM_H263TYPE* pParam = (OMX_VIDEO_PARAM_H263TYPE*)paramData;
                DEBUG_PRINT_LOW("venc_set_param: OMX_IndexParamVideoH263");
                OMX_U32 bFrames = 0;

                if (pParam->nPortIndex == (OMX_U32) PORT_INDEX_OUT) {
                    m_profile_set = false;
                    m_level_set = false;
                    rc_off_level = (int)pParam->eLevel;
                    if (!venc_set_profile_level (pParam->eProfile, pParam->eLevel)) {
                        DEBUG_PRINT_ERROR("ERROR: Unsuccessful in updating Profile and level");
                        return false;
                    }

                    if (pParam->nBFrames)
                        DEBUG_PRINT_ERROR("WARNING: B frame not supported for H.263");

                    if (venc_set_intra_period (pParam->nPFrames, bFrames) == false) {
                        DEBUG_PRINT_ERROR("ERROR: Request for setting intra period failed");
                        return false;
                    }
                } else {
                    DEBUG_PRINT_ERROR("ERROR: Invalid Port Index for OMX_IndexParamVideoH263");
                }

                break;
            }
        case OMX_IndexParamVideoAvc:
            {
                DEBUG_PRINT_LOW("venc_set_param:OMX_IndexParamVideoAvc");
                OMX_VIDEO_PARAM_AVCTYPE* pParam = (OMX_VIDEO_PARAM_AVCTYPE*)paramData;
                OMX_U32 bFrames = 0;

                if (pParam->nPortIndex == (OMX_U32) PORT_INDEX_OUT) {
                    DEBUG_PRINT_LOW("pParam->eProfile :%d ,pParam->eLevel %d",
                            pParam->eProfile,pParam->eLevel);

                    m_profile_set = false;
                    m_level_set = false;
                    rc_off_level = (int)pParam->eLevel;
                    if (!venc_set_profile_level (pParam->eProfile,pParam->eLevel)) {
                        DEBUG_PRINT_ERROR("ERROR: Unsuccessful in updating Profile and level %d, %d",
                                pParam->eProfile, pParam->eLevel);
                        return false;
                    } else {
                        if ((pParam->eProfile != OMX_VIDEO_AVCProfileBaseline) &&
                            (pParam->eProfile != (OMX_VIDEO_AVCPROFILETYPE) QOMX_VIDEO_AVCProfileConstrainedBaseline)) {
                            if (pParam->nBFrames) {
                                bFrames = pParam->nBFrames;
                            }
                        } else {
                            if (pParam->nBFrames) {
                                DEBUG_PRINT_ERROR("Warning: B frames not supported");
                                bFrames = 0;
                            }
                        }
                    }

                    if (!venc_set_intra_period (pParam->nPFrames, bFrames)) {
                        DEBUG_PRINT_ERROR("ERROR: Request for setting intra period failed");
                        return false;
                    }

                    if (!venc_set_entropy_config (pParam->bEntropyCodingCABAC, pParam->nCabacInitIdc)) {
                        DEBUG_PRINT_ERROR("ERROR: Request for setting Entropy failed");
                        return false;
                    }

                    if (!venc_set_inloop_filter (pParam->eLoopFilterMode)) {
                        DEBUG_PRINT_ERROR("ERROR: Request for setting Inloop filter failed");
                        return false;
                    }

                    if (!venc_set_multislice_cfg(OMX_IndexParamVideoAvc, pParam->nSliceHeaderSpacing)) {
                        DEBUG_PRINT_ERROR("WARNING: Unsuccessful in updating slice_config");
                        return false;
                    }
                } else {
                    DEBUG_PRINT_ERROR("ERROR: Invalid Port Index for OMX_IndexParamVideoAvc");
                }

                //TBD, lot of other variables to be updated, yet to decide
                break;
            }
        case (OMX_INDEXTYPE)OMX_IndexParamVideoVp8:
            {
                DEBUG_PRINT_LOW("venc_set_param:OMX_IndexParamVideoVp8");
                OMX_VIDEO_PARAM_VP8TYPE* pParam = (OMX_VIDEO_PARAM_VP8TYPE*)paramData;
                rc_off_level = (int)pParam->eLevel;
                if (!venc_set_profile_level (pParam->eProfile, pParam->eLevel)) {
                    DEBUG_PRINT_ERROR("ERROR: Unsuccessful in updating Profile and level %d, %d",
                                        pParam->eProfile, pParam->eLevel);
                    return false;
                }
                if(venc_set_vpx_error_resilience(pParam->bErrorResilientMode) == false) {
                    DEBUG_PRINT_ERROR("ERROR: Failed to set vpx error resilience");
                    return false;
                 }
                if(!venc_set_ltrmode(1, 1)) {
                   DEBUG_PRINT_ERROR("ERROR: Failed to enable ltrmode");
                   return false;
                }

                 // For VP8, hier-p and ltr are mutually exclusive features in firmware
                 // Disable hier-p if ltr is enabled.
                 if (m_codec == OMX_VIDEO_CodingVP8) {
                     DEBUG_PRINT_LOW("Disable Hier-P as LTR is being set");
                     if (!venc_set_hier_layers(QOMX_HIERARCHICALCODING_P, 0)) {
                        DEBUG_PRINT_ERROR("Disabling Hier P count failed");
                     }
                 }

                break;
            }
            case (OMX_INDEXTYPE)OMX_IndexParamVideoHevc:
            {
                DEBUG_PRINT_LOW("venc_set_param:OMX_IndexParamVideoHevc");
                OMX_VIDEO_PARAM_HEVCTYPE* pParam = (OMX_VIDEO_PARAM_HEVCTYPE*)paramData;
                rc_off_level = (int)pParam->eLevel;
                if (!venc_set_profile_level (pParam->eProfile, pParam->eLevel)) {
                    DEBUG_PRINT_ERROR("ERROR: Unsuccessful in updating Profile and level %d, %d",
                                        pParam->eProfile, pParam->eLevel);
                    return false;
                }
                if (!venc_set_inloop_filter(OMX_VIDEO_AVCLoopFilterEnable))
                    DEBUG_PRINT_HIGH("WARN: Request for setting Inloop filter failed for HEVC encoder");

                if (is_thulium_v1 && !venc_set_intra_period (0, 0)) {
                    DEBUG_PRINT_ERROR("ERROR: Request for setting intra period failed");
                    return false;
                }
                break;
            }
        case OMX_IndexParamVideoIntraRefresh:
            {
                DEBUG_PRINT_LOW("venc_set_param:OMX_IndexParamVideoIntraRefresh");
                OMX_VIDEO_PARAM_INTRAREFRESHTYPE *intra_refresh =
                    (OMX_VIDEO_PARAM_INTRAREFRESHTYPE *)paramData;

                if (intra_refresh->nPortIndex == (OMX_U32) PORT_INDEX_OUT) {
                    if (venc_set_intra_refresh(intra_refresh->eRefreshMode, intra_refresh->nCirMBs) == false) {
                        DEBUG_PRINT_ERROR("ERROR: Setting Intra refresh failed");
                        return false;
                    }
                } else {
                    DEBUG_PRINT_ERROR("ERROR: Invalid Port Index for OMX_IndexParamVideoIntraRefresh");
                }

                break;
            }
        case OMX_IndexParamVideoErrorCorrection:
            {
                DEBUG_PRINT_LOW("venc_set_param:OMX_IndexParamVideoErrorCorrection");
                OMX_VIDEO_PARAM_ERRORCORRECTIONTYPE *error_resilience =
                    (OMX_VIDEO_PARAM_ERRORCORRECTIONTYPE *)paramData;

                if (error_resilience->nPortIndex == (OMX_U32) PORT_INDEX_OUT) {
                    if (venc_set_error_resilience(error_resilience) == false) {
                        DEBUG_PRINT_ERROR("ERROR: Setting Intra refresh failed");
                        return false;
                    }
                } else {
                    DEBUG_PRINT_ERROR("ERROR: Invalid Port Index for OMX_IndexParamVideoErrorCorrection");
                }

                break;
            }
        case OMX_IndexParamVideoProfileLevelCurrent:
            {
                DEBUG_PRINT_LOW("venc_set_param:OMX_IndexParamVideoProfileLevelCurrent");
                OMX_VIDEO_PARAM_PROFILELEVELTYPE *profile_level =
                    (OMX_VIDEO_PARAM_PROFILELEVELTYPE *)paramData;

                if (profile_level->nPortIndex == (OMX_U32) PORT_INDEX_OUT) {
                    m_profile_set = false;
                    m_level_set = false;
                    rc_off_level = (int)profile_level->eLevel;
                    if (!venc_set_profile_level (profile_level->eProfile,
                                profile_level->eLevel)) {
                        DEBUG_PRINT_ERROR("WARNING: Unsuccessful in updating Profile and level");
                        return false;
                    }
                } else {
                    DEBUG_PRINT_ERROR("ERROR: Invalid Port Index for OMX_IndexParamVideoProfileLevelCurrent");
                }

                break;
            }
        case OMX_IndexParamVideoQuantization:
            {
                DEBUG_PRINT_LOW("venc_set_param:OMX_IndexParamVideoQuantization");
                OMX_VIDEO_PARAM_QUANTIZATIONTYPE *session_qp =
                    (OMX_VIDEO_PARAM_QUANTIZATIONTYPE *)paramData;
                if (session_qp->nPortIndex == (OMX_U32) PORT_INDEX_OUT) {
                    if (venc_set_session_qp (session_qp->nQpI,
                                session_qp->nQpP,
                                session_qp->nQpB) == false) {
                        DEBUG_PRINT_ERROR("ERROR: Setting Session QP failed");
                        return false;
                    }
                } else {
                    DEBUG_PRINT_ERROR("ERROR: Invalid Port Index for OMX_IndexParamVideoQuantization");
                }

                break;
            }
        case QOMX_IndexParamVideoInitialQp:
            {
                QOMX_EXTNINDEX_VIDEO_INITIALQP * initqp =
                    (QOMX_EXTNINDEX_VIDEO_INITIALQP *)paramData;
                 if (initqp->bEnableInitQp) {
                    DEBUG_PRINT_LOW("Enable initial QP: %d", (int)initqp->bEnableInitQp);
                    if(venc_enable_initial_qp(initqp) == false) {
                       DEBUG_PRINT_ERROR("ERROR: Failed to enable initial QP");
                       return OMX_ErrorUnsupportedSetting;
                     }
                 } else
                    DEBUG_PRINT_ERROR("ERROR: setting QOMX_IndexParamVideoEnableInitialQp");
                break;
            }
        case OMX_QcomIndexParamVideoQPRange:
            {
                DEBUG_PRINT_LOW("venc_set_param:OMX_QcomIndexParamVideoQPRange");
                OMX_QCOM_VIDEO_PARAM_QPRANGETYPE *session_qp_range =
                    (OMX_QCOM_VIDEO_PARAM_QPRANGETYPE *)paramData;

                if(session_qp_range->nPortIndex == (OMX_U32)PORT_INDEX_OUT) {
                    if(venc_set_session_qp_range (session_qp_range->minQP,
                                session_qp_range->maxQP) == false) {
                        DEBUG_PRINT_ERROR("ERROR: Setting QP Range[%u %u] failed",
                            (unsigned int)session_qp_range->minQP, (unsigned int)session_qp_range->maxQP);
                        return false;
                    } else {
                        session_qp_values.minqp = session_qp_range->minQP;
                        session_qp_values.maxqp = session_qp_range->maxQP;
                    }
                } else {
                    DEBUG_PRINT_ERROR("ERROR: Invalid Port Index for OMX_QcomIndexParamVideoQPRange");
                }

                break;
            }
        case OMX_QcomIndexEnableSliceDeliveryMode:
            {
                QOMX_EXTNINDEX_PARAMTYPE* pParam =
                    (QOMX_EXTNINDEX_PARAMTYPE*)paramData;

                if (pParam->nPortIndex == PORT_INDEX_OUT) {
                    if (venc_set_slice_delivery_mode(pParam->bEnable) == false) {
                        DEBUG_PRINT_ERROR("Setting slice delivery mode failed");
                        return OMX_ErrorUnsupportedSetting;
                    }
                } else {
                    DEBUG_PRINT_ERROR("OMX_QcomIndexEnableSliceDeliveryMode "
                            "called on wrong port(%u)", (unsigned int)pParam->nPortIndex);
                    return OMX_ErrorBadPortIndex;
                }

                break;
            }
        case OMX_ExtraDataFrameDimension:
            {
                DEBUG_PRINT_LOW("venc_set_param: OMX_ExtraDataFrameDimension");
                OMX_BOOL extra_data = *(OMX_BOOL *)(paramData);

                if (venc_set_extradata(OMX_ExtraDataFrameDimension, extra_data) == false) {
                    DEBUG_PRINT_ERROR("ERROR: Setting OMX_ExtraDataFrameDimension failed");
                    return false;
                }

                extradata = true;
                break;
            }
        case OMX_ExtraDataVideoEncoderSliceInfo:
            {
                DEBUG_PRINT_LOW("venc_set_param: OMX_ExtraDataVideoEncoderSliceInfo");
                OMX_BOOL extra_data = *(OMX_BOOL *)(paramData);

                if (venc_set_extradata(OMX_ExtraDataVideoEncoderSliceInfo, extra_data) == false) {
                    DEBUG_PRINT_ERROR("ERROR: Setting OMX_ExtraDataVideoEncoderSliceInfo failed");
                    return false;
                }

                extradata = true;
                break;
            }
        case OMX_ExtraDataVideoEncoderMBInfo:
            {
                DEBUG_PRINT_LOW("venc_set_param: OMX_ExtraDataVideoEncoderMBInfo");
                OMX_BOOL extra_data =  *(OMX_BOOL *)(paramData);

                if (venc_set_extradata(OMX_ExtraDataVideoEncoderMBInfo, extra_data) == false) {
                    DEBUG_PRINT_ERROR("ERROR: Setting OMX_ExtraDataVideoEncoderMBInfo failed");
                    return false;
                }

                extradata = true;
                break;
            }
        case OMX_QcomIndexParamSequenceHeaderWithIDR:
            {
                PrependSPSPPSToIDRFramesParams * pParam =
                    (PrependSPSPPSToIDRFramesParams *)paramData;

                DEBUG_PRINT_LOW("set inband sps/pps: %d", pParam->bEnable);
                if(venc_set_inband_video_header(pParam->bEnable) == false) {
                    DEBUG_PRINT_ERROR("ERROR: set inband sps/pps failed");
                    return OMX_ErrorUnsupportedSetting;
                }

                break;
            }
        case OMX_QcomIndexParamH264AUDelimiter:
            {
                OMX_QCOM_VIDEO_CONFIG_H264_AUD * pParam =
                    (OMX_QCOM_VIDEO_CONFIG_H264_AUD *)paramData;

                DEBUG_PRINT_LOW("set AU delimiters: %d", pParam->bEnable);
                if(venc_set_au_delimiter(pParam->bEnable) == false) {
                    DEBUG_PRINT_ERROR("ERROR: set H264 AU delimiter failed");
                    return OMX_ErrorUnsupportedSetting;
                }

                break;
            }
        case OMX_QcomIndexParamMBIStatisticsMode:
            {
                OMX_QOMX_VIDEO_MBI_STATISTICS * pParam =
                    (OMX_QOMX_VIDEO_MBI_STATISTICS *)paramData;

                DEBUG_PRINT_LOW("set MBI Dump mode: %d", pParam->eMBIStatisticsType);
                if(venc_set_mbi_statistics_mode(pParam->eMBIStatisticsType) == false) {
                    DEBUG_PRINT_ERROR("ERROR: set MBI Statistics mode failed");
                    return OMX_ErrorUnsupportedSetting;
                }

                break;
            }

        case OMX_QcomIndexConfigH264EntropyCodingCabac:
            {
                QOMX_VIDEO_H264ENTROPYCODINGTYPE * pParam =
                    (QOMX_VIDEO_H264ENTROPYCODINGTYPE *)paramData;

                DEBUG_PRINT_LOW("set Entropy info : %d", pParam->bCabac);
                if(venc_set_entropy_config (pParam->bCabac, 0) == false) {
                    DEBUG_PRINT_ERROR("ERROR: set Entropy failed");
                    return OMX_ErrorUnsupportedSetting;
                }

                break;
            }

         case OMX_QcomIndexHierarchicalStructure:
           {
               QOMX_VIDEO_HIERARCHICALLAYERS* pParam =
                   (QOMX_VIDEO_HIERARCHICALLAYERS*)paramData;

                if (pParam->nPortIndex == PORT_INDEX_OUT) {
                    if (!venc_set_hier_layers(pParam->eHierarchicalCodingType, pParam->nNumLayers)) {
                        DEBUG_PRINT_ERROR("Setting Hier P count failed");
                        return false;
                    }
                } else {
                    DEBUG_PRINT_ERROR("OMX_QcomIndexHierarchicalStructure called on wrong port(%d)", (int)pParam->nPortIndex);
                    return false;
                }

                // For VP8, hier-p and ltr are mutually exclusive features in firmware
                // Disable ltr if hier-p is enabled.
                if (m_codec == OMX_VIDEO_CodingVP8) {
                    DEBUG_PRINT_LOW("Disable LTR as HIER-P is being set");
                    if(!venc_set_ltrmode(0, 1)) {
                         DEBUG_PRINT_ERROR("ERROR: Failed to disable ltrmode");
                     }
                }
                break;
           }
        case OMX_QcomIndexParamPerfLevel:
            {
                OMX_QCOM_VIDEO_PARAM_PERF_LEVEL *pParam =
                        (OMX_QCOM_VIDEO_PARAM_PERF_LEVEL *)paramData;
                DEBUG_PRINT_LOW("Set perf level: %d", pParam->ePerfLevel);
                if (!venc_set_perf_level(pParam->ePerfLevel)) {
                    DEBUG_PRINT_ERROR("ERROR: Failed to set perf level to %d", pParam->ePerfLevel);
                    return false;
                } else {
                    performance_level.perflevel = (unsigned int) pParam->ePerfLevel;
                }
                break;
            }
        case OMX_QcomIndexParamH264VUITimingInfo:
            {
                OMX_QCOM_VIDEO_PARAM_VUI_TIMING_INFO *pParam =
                        (OMX_QCOM_VIDEO_PARAM_VUI_TIMING_INFO *)paramData;
                DEBUG_PRINT_LOW("Set VUI timing info: %d", pParam->bEnable);
                if(venc_set_vui_timing_info(pParam->bEnable) == false) {
                    DEBUG_PRINT_ERROR("ERROR: Failed to set vui timing info to %d", pParam->bEnable);
                    return false;
                } else {
                    vui_timing_info.enabled = (unsigned int) pParam->bEnable;
                }
                break;
            }
        case OMX_QTIIndexParamVQZIPSEIType:
            {
                OMX_QTI_VIDEO_PARAM_VQZIP_SEI_TYPE*pParam =
                        (OMX_QTI_VIDEO_PARAM_VQZIP_SEI_TYPE *)paramData;
                DEBUG_PRINT_LOW("Enable VQZIP SEI: %d", pParam->bEnable);
                if(venc_set_vqzip_sei_type(pParam->bEnable) == false) {
                    DEBUG_PRINT_ERROR("ERROR: Failed to set VQZIP SEI type %d", pParam->bEnable);
                    return false;
                }
                break;
            }
        case OMX_QcomIndexParamPeakBitrate:
            {
                OMX_QCOM_VIDEO_PARAM_PEAK_BITRATE *pParam =
                        (OMX_QCOM_VIDEO_PARAM_PEAK_BITRATE *)paramData;
                DEBUG_PRINT_LOW("Set peak bitrate: %u", (unsigned int)pParam->nPeakBitrate);
                if(venc_set_peak_bitrate(pParam->nPeakBitrate) == false) {
                    DEBUG_PRINT_ERROR("ERROR: Failed to set peak bitrate to %u", (unsigned int)pParam->nPeakBitrate);
                    return false;
                } else {
                    peak_bitrate.peakbitrate = (unsigned int) pParam->nPeakBitrate;
                }
                break;
            }
       case OMX_QcomIndexParamSetMVSearchrange:
            {
               DEBUG_PRINT_LOW("venc_set_config: OMX_QcomIndexParamSetMVSearchrange");
               is_searchrange_set = true;
               if (!venc_set_searchrange()) {
                   DEBUG_PRINT_ERROR("ERROR: Failed to set search range");
                   return false;
               }
            }
            break;
        case OMX_QcomIndexParamVideoLTRCount:
            {
                DEBUG_PRINT_LOW("venc_set_param: OMX_QcomIndexParamVideoLTRCount");
                OMX_QCOM_VIDEO_PARAM_LTRCOUNT_TYPE* pParam =
                        (OMX_QCOM_VIDEO_PARAM_LTRCOUNT_TYPE*)paramData;
                if (pParam->nCount > 0) {
                    if (venc_set_ltrmode(1, pParam->nCount) == false) {
                        DEBUG_PRINT_ERROR("ERROR: Enable LTR mode failed");
                        return false;
                    }
                } else {
                    if (venc_set_ltrmode(0, 0) == false) {
                        DEBUG_PRINT_ERROR("ERROR: Disable LTR mode failed");
                        return false;
                    }
                }
                break;
            }
        case OMX_QcomIndexParamVideoHybridHierpMode:
            {
                if (!venc_set_hybrid_hierp((QOMX_EXTNINDEX_VIDEO_HYBRID_HP_MODE*)paramData)) {
                     DEBUG_PRINT_ERROR("Setting hybrid Hier-P mode failed");
                     return false;
                }
                break;
            }
        case OMX_QcomIndexParamBatchSize:
            {
                OMX_PARAM_U32TYPE* pParam =
                    (OMX_PARAM_U32TYPE*)paramData;

                if (pParam->nPortIndex == PORT_INDEX_OUT) {
                    DEBUG_PRINT_ERROR("For the moment, client-driven batching not supported"
                            " on output port");
                    return OMX_ErrorUnsupportedSetting;
                }

                if (!venc_set_batch_size(pParam->nU32)) {
                     DEBUG_PRINT_ERROR("Failed setting batch size as %d", pParam->nU32);
                     return OMX_ErrorUnsupportedSetting;
                }
                break;
            }
        case OMX_QcomIndexParamVencAspectRatio:
            {
                if (!venc_set_aspectratio(paramData)) {
                    DEBUG_PRINT_ERROR("ERROR: Setting OMX_QcomIndexParamVencAspectRatio failed");
                    return OMX_ErrorUnsupportedSetting;
                }
                break;
            }
        case OMX_IndexParamVideoSliceFMO:
        default:
            DEBUG_PRINT_ERROR("ERROR: Unsupported parameter in venc_set_param: %u",
                    index);
            break;
            //case
    }

    return true;
}

bool venc_dev::venc_check_valid_config()
{
   if (streaming[OUTPUT_PORT] && streaming[CAPTURE_PORT] &&
       ((m_sVenc_cfg.codectype == V4L2_PIX_FMT_H264 && hier_layers.hier_mode == HIER_P_HYBRID) ||
       (m_sVenc_cfg.codectype == V4L2_PIX_FMT_HEVC && hier_layers.hier_mode == HIER_P))) {
        DEBUG_PRINT_ERROR("venc_set_config not allowed run time for following usecases");
        DEBUG_PRINT_ERROR("For H264 : When Hybrid Hier P enabled");
        DEBUG_PRINT_ERROR("For H265 : When Hier P enabled");
        return false;
    }
   return true;
}

bool venc_dev::venc_set_config(void *configData, OMX_INDEXTYPE index)
{

    DEBUG_PRINT_LOW("Inside venc_set_config");

    if(!venc_check_valid_config()) {
        DEBUG_PRINT_ERROR("venc_set_config not allowed for this configuration");
        return false;
    }

    switch ((int)index) {
        case OMX_IndexConfigVideoBitrate:
            {
                OMX_VIDEO_CONFIG_BITRATETYPE *bit_rate = (OMX_VIDEO_CONFIG_BITRATETYPE *)
                    configData;
                DEBUG_PRINT_LOW("venc_set_config: OMX_IndexConfigVideoBitrate");

                if (bit_rate->nPortIndex == (OMX_U32)PORT_INDEX_OUT) {
                    if (venc_set_target_bitrate(bit_rate->nEncodeBitrate, 1) == false) {
                        DEBUG_PRINT_ERROR("ERROR: Setting Target Bit rate failed");
                        return false;
                    }
                } else {
                    DEBUG_PRINT_ERROR("ERROR: Invalid Port Index for OMX_IndexConfigVideoBitrate");
                }

                break;
            }
        case OMX_IndexConfigVideoFramerate:
            {
                OMX_CONFIG_FRAMERATETYPE *frame_rate = (OMX_CONFIG_FRAMERATETYPE *)
                    configData;
                DEBUG_PRINT_LOW("venc_set_config: OMX_IndexConfigVideoFramerate");

                if (frame_rate->nPortIndex == (OMX_U32)PORT_INDEX_OUT) {
                    if (venc_set_encode_framerate(frame_rate->xEncodeFramerate, 1) == false) {
                        DEBUG_PRINT_ERROR("ERROR: Setting Encode Framerate failed");
                        return false;
                    }
                } else {
                    DEBUG_PRINT_ERROR("ERROR: Invalid Port Index for OMX_IndexConfigVideoFramerate");
                }

                break;
            }
        case QOMX_IndexConfigVideoIntraperiod:
            {
                DEBUG_PRINT_LOW("venc_set_param:QOMX_IndexConfigVideoIntraperiod");
                QOMX_VIDEO_INTRAPERIODTYPE *intraperiod =
                    (QOMX_VIDEO_INTRAPERIODTYPE *)configData;

                if (intraperiod->nPortIndex == (OMX_U32) PORT_INDEX_OUT) {
                    if (venc_set_intra_period(intraperiod->nPFrames, intraperiod->nBFrames) == false) {
                        DEBUG_PRINT_ERROR("ERROR: Request for setting intra period failed");
                        return false;
                    }
                }

                break;
            }
        case OMX_IndexConfigVideoIntraVOPRefresh:
            {
                OMX_CONFIG_INTRAREFRESHVOPTYPE *intra_vop_refresh = (OMX_CONFIG_INTRAREFRESHVOPTYPE *)
                    configData;
                DEBUG_PRINT_LOW("venc_set_config: OMX_IndexConfigVideoIntraVOPRefresh");

                if (intra_vop_refresh->nPortIndex == (OMX_U32)PORT_INDEX_OUT) {
                    if (venc_set_intra_vop_refresh(intra_vop_refresh->IntraRefreshVOP) == false) {
                        DEBUG_PRINT_ERROR("ERROR: Setting Encode Framerate failed");
                        return false;
                    }
                } else {
                    DEBUG_PRINT_ERROR("ERROR: Invalid Port Index for OMX_IndexConfigVideoFramerate");
                }

                break;
            }
        case OMX_IndexConfigCommonRotate:
            {
                OMX_CONFIG_ROTATIONTYPE *config_rotation =
                    reinterpret_cast<OMX_CONFIG_ROTATIONTYPE*>(configData);
                OMX_U32 nFrameWidth;
                if (!config_rotation) {
                   return false;
                }
                if (true == deinterlace_enabled) {
                    DEBUG_PRINT_ERROR("ERROR: Rotation is not supported with deinterlacing");
                    return false;
                }
                DEBUG_PRINT_HIGH("venc_set_config: updating the new Dims");
                nFrameWidth = m_sVenc_cfg.dvs_width;
                m_sVenc_cfg.dvs_width  = m_sVenc_cfg.dvs_height;
                m_sVenc_cfg.dvs_height = nFrameWidth;

                if(venc_set_vpe_rotation(config_rotation->nRotation) == false) {
                    DEBUG_PRINT_ERROR("ERROR: Dimension Change for Rotation failed");
                    return false;
                }

                break;
            }
        case OMX_IndexConfigVideoAVCIntraPeriod:
            {
                OMX_VIDEO_CONFIG_AVCINTRAPERIOD *avc_iperiod = (OMX_VIDEO_CONFIG_AVCINTRAPERIOD*) configData;
                DEBUG_PRINT_LOW("venc_set_param: OMX_IndexConfigVideoAVCIntraPeriod");

                if (venc_set_idr_period(avc_iperiod->nPFrames, avc_iperiod->nIDRPeriod)
                        == false) {
                    DEBUG_PRINT_ERROR("ERROR: Setting "
                            "OMX_IndexConfigVideoAVCIntraPeriod failed");
                    return false;
                }
                break;
            }
        case OMX_IndexConfigCommonDeinterlace:
            {
                OMX_VIDEO_CONFIG_DEINTERLACE *deinterlace = (OMX_VIDEO_CONFIG_DEINTERLACE *) configData;
                DEBUG_PRINT_LOW("venc_set_config: OMX_IndexConfigCommonDeinterlace");
                if(deinterlace->nPortIndex == (OMX_U32)PORT_INDEX_OUT) {
                    if (m_sVenc_cfg.dvs_width == m_sVenc_cfg.input_height &&
                        m_sVenc_cfg.dvs_height == m_sVenc_cfg.input_width)
                    {
                        DEBUG_PRINT_ERROR("ERROR: Deinterlace not supported with rotation");
                        return false;
                    }
                    if(venc_set_deinterlace(deinterlace->nEnable) == false) {
                        DEBUG_PRINT_ERROR("ERROR: Setting Deinterlace failed");
                        return false;
                    }
                } else {
                DEBUG_PRINT_ERROR("ERROR: Invalid Port Index for OMX_IndexConfigCommonDeinterlace");
                }
                break;
            }
        case OMX_IndexConfigVideoVp8ReferenceFrame:
            {
                OMX_VIDEO_VP8REFERENCEFRAMETYPE* vp8refframe = (OMX_VIDEO_VP8REFERENCEFRAMETYPE*) configData;
                DEBUG_PRINT_LOW("venc_set_config: OMX_IndexConfigVideoVp8ReferenceFrame");
                if ((vp8refframe->nPortIndex == (OMX_U32)PORT_INDEX_IN) &&
                        (vp8refframe->bUseGoldenFrame)) {
                    if(venc_set_useltr(0x1) == false) {
                        DEBUG_PRINT_ERROR("ERROR: use goldenframe failed");
                        return false;
                    }
                } else if((vp8refframe->nPortIndex == (OMX_U32)PORT_INDEX_IN) &&
                        (vp8refframe->bGoldenFrameRefresh)) {
                    if(venc_set_markltr(0x1) == false) {
                        DEBUG_PRINT_ERROR("ERROR: Setting goldenframe failed");
                        return false;
                    }
                } else {
                    DEBUG_PRINT_ERROR("ERROR: Invalid Port Index for OMX_IndexConfigVideoVp8ReferenceFrame");
                }
                break;
            }
        case OMX_QcomIndexConfigVideoLTRUse:
            {
                OMX_QCOM_VIDEO_CONFIG_LTRUSE_TYPE* pParam = (OMX_QCOM_VIDEO_CONFIG_LTRUSE_TYPE*)configData;
                DEBUG_PRINT_LOW("venc_set_config: OMX_QcomIndexConfigVideoLTRUse");
                if (pParam->nPortIndex == (OMX_U32)PORT_INDEX_IN) {
                    if (venc_set_useltr(pParam->nID) == false) {
                        DEBUG_PRINT_ERROR("ERROR: Use LTR failed");
                        return false;
                    }
                } else {
                    DEBUG_PRINT_ERROR("ERROR: Invalid Port Index for OMX_QcomIndexConfigVideoLTRUse");
                }
                break;
            }
        case OMX_QcomIndexConfigVideoLTRMark:
            {
                OMX_QCOM_VIDEO_CONFIG_LTRMARK_TYPE* pParam = (OMX_QCOM_VIDEO_CONFIG_LTRMARK_TYPE*)configData;
                DEBUG_PRINT_LOW("venc_set_config: OMX_QcomIndexConfigVideoLTRMark");
                if (pParam->nPortIndex == (OMX_U32)PORT_INDEX_IN) {
                    if (venc_set_markltr(pParam->nID) == false) {
                        DEBUG_PRINT_ERROR("ERROR: Mark LTR failed");
                        return false;
                    }
                }  else {
                    DEBUG_PRINT_ERROR("ERROR: Invalid Port Index for OMX_QcomIndexConfigVideoLTRMark");
                }
                break;
            }
        case OMX_QcomIndexConfigPerfLevel:
            {
                OMX_QCOM_VIDEO_CONFIG_PERF_LEVEL *perf =
                        (OMX_QCOM_VIDEO_CONFIG_PERF_LEVEL *)configData;
                DEBUG_PRINT_LOW("Set perf level: %d", perf->ePerfLevel);
                if (!venc_set_perf_level(perf->ePerfLevel)) {
                    DEBUG_PRINT_ERROR("ERROR: Failed to set perf level to %d", perf->ePerfLevel);
                    return false;
                } else {
                    performance_level.perflevel = (unsigned int) perf->ePerfLevel;
                }
                break;
            }
        case OMX_QcomIndexConfigVideoVencPerfMode:
            {
                QOMX_EXTNINDEX_VIDEO_PERFMODE *pParam = (QOMX_EXTNINDEX_VIDEO_PERFMODE *) configData;
                DEBUG_PRINT_LOW("venc_set_config: OMX_QcomIndexConfigVideoVencPerfMode");
                if (venc_set_perf_mode(pParam->nPerfMode) == false) {
                    DEBUG_PRINT_ERROR("Failed to set V4L2_CID_MPEG_VIDC_VIDEO_PERF_MODE");
                    return false;
                }
                break;
            }
        case OMX_QcomIndexConfigMaxHierPLayers:
            {
                QOMX_EXTNINDEX_VIDEO_MAX_HIER_P_LAYERS *pParam =
                    (QOMX_EXTNINDEX_VIDEO_MAX_HIER_P_LAYERS *) configData;
                DEBUG_PRINT_LOW("venc_set_config: OMX_QcomIndexConfigMaxHierPLayers");
                if (venc_set_max_hierp(pParam->nMaxHierLayers) == false) {
                    DEBUG_PRINT_ERROR("Failed to set OMX_QcomIndexConfigMaxHierPLayers");
                    return false;
                }
                break;
            }
        case OMX_QcomIndexConfigBaseLayerId:
            {
                OMX_SKYPE_VIDEO_CONFIG_BASELAYERPID* pParam =
                    (OMX_SKYPE_VIDEO_CONFIG_BASELAYERPID*) configData;
                if (venc_set_baselayerid(pParam->nPID) == false) {
                    DEBUG_PRINT_ERROR("Failed to set OMX_QcomIndexConfigBaseLayerId failed");
                    return OMX_ErrorUnsupportedSetting;
                }
                break;
            }
        case OMX_QcomIndexConfigQp:
            {
                OMX_SKYPE_VIDEO_CONFIG_QP* pParam =
                    (OMX_SKYPE_VIDEO_CONFIG_QP*) configData;
                if (venc_set_qp(pParam->nQP) == false) {
                    DEBUG_PRINT_ERROR("Failed to set OMX_QcomIndexConfigQp failed");
                    return OMX_ErrorUnsupportedSetting;
                }
                break;
            }
        case OMX_IndexConfigPriority:
            {
                OMX_PARAM_U32TYPE *priority = (OMX_PARAM_U32TYPE *)configData;
                DEBUG_PRINT_LOW("Set_config: priority %d",priority->nU32);
                if (!venc_set_priority(priority->nU32)) {
                    DEBUG_PRINT_ERROR("Failed to set priority");
                    return false;
                }
                break;
            }
        case OMX_IndexConfigOperatingRate:
            {
                OMX_PARAM_U32TYPE *rate = (OMX_PARAM_U32TYPE *)configData;
                DEBUG_PRINT_LOW("Set_config: operating rate %d", rate->nU32);
                if (!venc_set_operatingrate(rate->nU32)) {
                    DEBUG_PRINT_ERROR("Failed to set operating rate");
                    return false;
                }
                break;
            }
        default:
            DEBUG_PRINT_ERROR("Unsupported config index = %u", index);
            break;
    }

    return true;
}

unsigned venc_dev::venc_stop( void)
{
    struct venc_msg venc_msg;
    struct v4l2_requestbuffers bufreq;
    int rc = 0, ret = 0;

    if (!stopped) {
        enum v4l2_buf_type cap_type;

        if (streaming[OUTPUT_PORT]) {
            cap_type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
            rc = ioctl(m_nDriver_fd, VIDIOC_STREAMOFF, &cap_type);

            if (rc) {
                DEBUG_PRINT_ERROR("Failed to call streamoff on driver: capability: %d, %d",
                        cap_type, rc);
            } else
                streaming[OUTPUT_PORT] = false;

            DEBUG_PRINT_LOW("Releasing registered buffers from driver on o/p port");
            bufreq.memory = V4L2_MEMORY_USERPTR;
            bufreq.count = 0;
            bufreq.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
            ret = ioctl(m_nDriver_fd, VIDIOC_REQBUFS, &bufreq);

            if (ret) {
                DEBUG_PRINT_ERROR("ERROR: VIDIOC_REQBUFS OUTPUT MPLANE Failed");
                return false;
            }
        }

        if (!rc && streaming[CAPTURE_PORT]) {
            cap_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
            rc = ioctl(m_nDriver_fd, VIDIOC_STREAMOFF, &cap_type);

            if (rc) {
                DEBUG_PRINT_ERROR("Failed to call streamoff on driver: capability: %d, %d",
                        cap_type, rc);
            } else
                streaming[CAPTURE_PORT] = false;

            DEBUG_PRINT_LOW("Releasing registered buffers from driver on capture port");
            bufreq.memory = V4L2_MEMORY_USERPTR;
            bufreq.count = 0;
            bufreq.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
            ret = ioctl(m_nDriver_fd, VIDIOC_REQBUFS, &bufreq);

            if (ret) {
                DEBUG_PRINT_ERROR("ERROR: VIDIOC_REQBUFS CAPTURE MPLANE Failed");
                return false;
            }
        }

        if (!rc && !ret) {
            venc_stop_done();
            stopped = 1;
            /*set flag to re-configure when started again*/
            resume_in_stopped = 1;

        }
    }

    return rc;
}

unsigned venc_dev::venc_pause(void)
{
    pthread_mutex_lock(&pause_resume_mlock);
    paused = true;
    pthread_mutex_unlock(&pause_resume_mlock);
    return 0;
}

unsigned venc_dev::venc_resume(void)
{
    pthread_mutex_lock(&pause_resume_mlock);
    paused = false;
    pthread_mutex_unlock(&pause_resume_mlock);

    return pthread_cond_signal(&pause_resume_cond);
}

unsigned venc_dev::venc_start_done(void)
{
    struct venc_msg venc_msg;
    venc_msg.msgcode = VEN_MSG_START;
    venc_msg.statuscode = VEN_S_SUCCESS;
    venc_handle->async_message_process(venc_handle,&venc_msg);
    return 0;
}

unsigned venc_dev::venc_stop_done(void)
{
    struct venc_msg venc_msg;
    free_extradata();
    venc_msg.msgcode=VEN_MSG_STOP;
    venc_msg.statuscode=VEN_S_SUCCESS;
    venc_handle->async_message_process(venc_handle,&venc_msg);
    return 0;
}

unsigned venc_dev::venc_set_message_thread_id(pthread_t tid)
{
    async_thread_created = true;
    m_tid=tid;
    return 0;
}

void venc_dev::venc_set_vqzip_defaults()
{
    struct v4l2_control control;
    int rc = 0;

    control.id = V4L2_CID_MPEG_VIDC_VIDEO_RATE_CONTROL;
    control.value = V4L2_CID_MPEG_VIDC_VIDEO_RATE_CONTROL_OFF;
    rc = ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control);
    if (rc)
        DEBUG_PRINT_ERROR("Failed to set Rate Control OFF for VQZIP");
    control.id = V4L2_CID_MPEG_VIDC_VIDEO_NUM_P_FRAMES;
    control.value = INT_MAX;

    rc = ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control);
    if (rc)
        DEBUG_PRINT_ERROR("Failed to set P frame period for VQZIP");

    control.id = V4L2_CID_MPEG_VIDC_VIDEO_NUM_B_FRAMES;
    control.value = 0;

    rc = ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control);
    if (rc)
        DEBUG_PRINT_ERROR("Failed to set B frame period for VQZIP");

}


unsigned venc_dev::venc_start(void)
{
    enum v4l2_buf_type buf_type;
    int ret, r;
    struct v4l2_control control;

    memset(&control, 0, sizeof(control));

    DEBUG_PRINT_HIGH("%s(): Check Profile/Level set in driver before start",
            __func__);
    m_level_set = false;

    if (!venc_set_profile_level(0, 0)) {
        DEBUG_PRINT_ERROR("ERROR: %s(): Driver Profile/Level is NOT SET",
                __func__);
    } else {
        DEBUG_PRINT_HIGH("%s(): Driver Profile[%lu]/Level[%lu] successfully SET",
                __func__, codec_profile.profile, profile_level.level);
    }

    if (vqzip_sei_info.enabled)
        venc_set_vqzip_defaults();

    venc_config_print();

    if(resume_in_stopped){
        /*set buffercount when restarted*/
        venc_reconfig_reqbufs();
        resume_in_stopped = 0;
    }

    /* Check if slice_delivery mode is enabled & max slices is sufficient for encoding complete frame */
    if (slice_mode.enable && multislice.mslice_size &&
            (m_sVenc_cfg.dvs_width *  m_sVenc_cfg.dvs_height)/(256 * multislice.mslice_size) >= MAX_SUPPORTED_SLICES_PER_FRAME) {
        DEBUG_PRINT_ERROR("slice_mode: %lu, max slices (%lu) should be less than (%d)", slice_mode.enable,
                (m_sVenc_cfg.dvs_width *  m_sVenc_cfg.dvs_height)/(256 * multislice.mslice_size),
                MAX_SUPPORTED_SLICES_PER_FRAME);
        return 1;
    }

    buf_type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    DEBUG_PRINT_LOW("send_command_proxy(): Idle-->Executing");
    ret=ioctl(m_nDriver_fd, VIDIOC_STREAMON,&buf_type);

    if (ret)
        return 1;

    streaming[CAPTURE_PORT] = true;

    control.id = V4L2_CID_MPEG_VIDC_VIDEO_REQUEST_SEQ_HEADER;
    control.value = 1;
    ret = ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control);
    if (ret) {
        DEBUG_PRINT_ERROR("failed to request seq header");
        return 1;
    }


    stopped = 0;
    return 0;
}

inline const char* hiermode_string(int val)
{
    switch(val)
    {
    case HIER_NONE:
        return "No Hier";
    case HIER_P:
        return "Hier-P";
    case HIER_B:
        return "Hier-B";
    case HIER_P_HYBRID:
        return "Hybrid Hier-P";
    default:
        return "No hier";
    }
}

inline const char* bitrate_type_string(int val)
{
    switch(val)
    {
    case V4L2_CID_MPEG_VIDC_VIDEO_VENC_BITRATE_DISABLE:
        return "CUMULATIVE";
    case V4L2_CID_MPEG_VIDC_VIDEO_VENC_BITRATE_ENABLE:
        return "LAYER WISE";
    default:
        return "Unknown Bitrate Type";
    }
}

void venc_dev::venc_config_print()
{

    DEBUG_PRINT_HIGH("ENC_CONFIG: Codec: %ld, Profile %ld, level : %ld",
            m_sVenc_cfg.codectype, codec_profile.profile, profile_level.level);

    DEBUG_PRINT_HIGH("ENC_CONFIG: Input Width: %ld, Height:%ld, Fps: %ld",
            m_sVenc_cfg.input_width, m_sVenc_cfg.input_height,
            m_sVenc_cfg.fps_num/m_sVenc_cfg.fps_den);

    DEBUG_PRINT_HIGH("ENC_CONFIG: Output Width: %ld, Height:%ld, Fps: %ld",
            m_sVenc_cfg.dvs_width, m_sVenc_cfg.dvs_height,
            m_sVenc_cfg.fps_num/m_sVenc_cfg.fps_den);

    DEBUG_PRINT_HIGH("ENC_CONFIG: Bitrate: %ld, RC: %ld, P - Frames : %ld, B - Frames = %ld",
            bitrate.target_bitrate, rate_ctrl.rcmode, intra_period.num_pframes, intra_period.num_bframes);

    DEBUG_PRINT_HIGH("ENC_CONFIG: qpI: %ld, qpP: %ld, qpb: %ld",
            session_qp.iframeqp, session_qp.pframeqp, session_qp.bframeqp);

    DEBUG_PRINT_HIGH("ENC_CONFIG: Init_qpI: %ld, Init_qpP: %ld, Init_qpb: %ld",
            init_qp.iframeqp, init_qp.pframeqp, init_qp.bframeqp);

    DEBUG_PRINT_HIGH("ENC_CONFIG: minQP: %lu, maxQP: %lu",
            session_qp_values.minqp, session_qp_values.maxqp);

    DEBUG_PRINT_HIGH("ENC_CONFIG: VOP_Resolution: %ld, Slice-Mode: %ld, Slize_Size: %ld",
            voptimecfg.voptime_resolution, multislice.mslice_mode,
            multislice.mslice_size);

    DEBUG_PRINT_HIGH("ENC_CONFIG: EntropyMode: %d, CabacModel: %ld",
            entropy.longentropysel, entropy.cabacmodel);

    DEBUG_PRINT_HIGH("ENC_CONFIG: DB-Mode: %ld, alpha: %ld, Beta: %ld",
            dbkfilter.db_mode, dbkfilter.slicealpha_offset,
            dbkfilter.slicebeta_offset);

    DEBUG_PRINT_HIGH("ENC_CONFIG: IntraMB/Frame: %ld, HEC: %ld, IDR Period: %ld",
            intra_refresh.mbcount, hec.header_extension, idrperiod.idrperiod);

    DEBUG_PRINT_HIGH("ENC_CONFIG: LTR Enabled: %d, Count: %d",
            ltrinfo.enabled, ltrinfo.count);

    DEBUG_PRINT_HIGH("ENC_CONFIG: Hier layers: %d, Hier Mode: %s VPX_ErrorResilience: %d",
            hier_layers.numlayers, hiermode_string(hier_layers.hier_mode), vpx_err_resilience.enable);

    DEBUG_PRINT_HIGH("ENC_CONFIG: Hybrid_HP PARAMS: Layers: %d, Frame Interval : %d, MinQP: %d, Max_QP: %d",
            hybrid_hp.nHpLayers, hybrid_hp.nKeyFrameInterval, hybrid_hp.nMinQuantizer, hybrid_hp.nMaxQuantizer);

    DEBUG_PRINT_HIGH("ENC_CONFIG: Hybrid_HP PARAMS: Layer0: %d, Layer1: %d, Later2: %d, Layer3: %d, Layer4: %d, Layer5: %d",
            hybrid_hp.nTemporalLayerBitrateRatio[0], hybrid_hp.nTemporalLayerBitrateRatio[1],
            hybrid_hp.nTemporalLayerBitrateRatio[2], hybrid_hp.nTemporalLayerBitrateRatio[3],
            hybrid_hp.nTemporalLayerBitrateRatio[4], hybrid_hp.nTemporalLayerBitrateRatio[5]);

    DEBUG_PRINT_HIGH("ENC_CONFIG: Performace level: %d", performance_level.perflevel);

    DEBUG_PRINT_HIGH("ENC_CONFIG: VUI timing info enabled: %d", vui_timing_info.enabled);

    DEBUG_PRINT_HIGH("ENC_CONFIG: Peak bitrate: %d", peak_bitrate.peakbitrate);

    DEBUG_PRINT_HIGH("ENC_CONFIG: Session Priority: %u", sess_priority.priority);

    DEBUG_PRINT_HIGH("ENC_CONFIG: Operating Rate: %u", operating_rate);
}

bool venc_dev::venc_reconfig_reqbufs()
{
    struct v4l2_requestbuffers bufreq;

    bufreq.memory = V4L2_MEMORY_USERPTR;
    bufreq.count = m_sInput_buff_property.actualcount;
    bufreq.type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    if(ioctl(m_nDriver_fd,VIDIOC_REQBUFS, &bufreq)) {
            DEBUG_PRINT_ERROR("VIDIOC_REQBUFS OUTPUT_MPLANE Failed when resume");
            return false;
    }

    bufreq.memory = V4L2_MEMORY_USERPTR;
    bufreq.count = m_sOutput_buff_property.actualcount;
    bufreq.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if(ioctl(m_nDriver_fd,VIDIOC_REQBUFS, &bufreq))
    {
            DEBUG_PRINT_ERROR("ERROR: Request for setting o/p buffer count failed when resume");
            return false;
    }
    return true;
}

unsigned venc_dev::venc_flush( unsigned port)
{
    struct v4l2_encoder_cmd enc;
    DEBUG_PRINT_LOW("in %s", __func__);

    enc.cmd = V4L2_ENC_QCOM_CMD_FLUSH;
    enc.flags = V4L2_QCOM_CMD_FLUSH_OUTPUT | V4L2_QCOM_CMD_FLUSH_CAPTURE;

    if (ioctl(m_nDriver_fd, VIDIOC_ENCODER_CMD, &enc)) {
        DEBUG_PRINT_ERROR("Flush Port (%d) Failed ", port);
        return -1;
    }

    return 0;

}

//allocating I/P memory from pmem and register with the device


bool venc_dev::venc_use_buf(void *buf_addr, unsigned port,unsigned index)
{

    struct pmem *pmem_tmp;
    struct v4l2_buffer buf;
    struct v4l2_plane plane[VIDEO_MAX_PLANES];
    int rc = 0;
    unsigned int extra_idx;
    int extradata_index = 0;

    pmem_tmp = (struct pmem *)buf_addr;
    DEBUG_PRINT_LOW("venc_use_buf:: pmem_tmp = %p", pmem_tmp);

    if (port == PORT_INDEX_OUT) {
        extra_idx = EXTRADATA_IDX(num_output_planes);

        if ((num_output_planes > 1) && (extra_idx)) {
            rc = allocate_extradata(&output_extradata_info);

            if (rc)
                DEBUG_PRINT_ERROR("Failed to allocate extradata: %d", rc);
        }

        buf.index = index;
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_USERPTR;
        plane[0].length = pmem_tmp->size;
        plane[0].m.userptr = (unsigned long)pmem_tmp->buffer;
        plane[0].reserved[0] = pmem_tmp->fd;
        plane[0].reserved[1] = 0;
        plane[0].data_offset = pmem_tmp->offset;
        buf.m.planes = plane;
        buf.length = num_output_planes;

        if (extra_idx && (extra_idx < VIDEO_MAX_PLANES)) {
            plane[extra_idx].length = output_extradata_info.buffer_size;
            plane[extra_idx].m.userptr = (unsigned long) (output_extradata_info.uaddr + index * output_extradata_info.buffer_size);
#ifdef USE_ION
            plane[extra_idx].reserved[0] = output_extradata_info.ion.fd_ion_data.fd;
#endif
            plane[extra_idx].reserved[1] = output_extradata_info.buffer_size * index;
            plane[extra_idx].data_offset = 0;
        } else if  (extra_idx >= VIDEO_MAX_PLANES) {
            DEBUG_PRINT_ERROR("Extradata index is more than allowed: %d", extra_idx);
            return OMX_ErrorBadParameter;
        }

        rc = ioctl(m_nDriver_fd, VIDIOC_PREPARE_BUF, &buf);

        if (rc)
            DEBUG_PRINT_LOW("VIDIOC_PREPARE_BUF Failed");
    } else if (port == PORT_INDEX_IN) {
            DEBUG_PRINT_LOW("No need to call VIDIOC_PREPARE_BUF on input port");
    } else {
        DEBUG_PRINT_ERROR("ERROR: venc_use_buf:Invalid Port Index ");
        return false;
    }

    return true;
}

bool venc_dev::venc_free_buf(void *buf_addr, unsigned port)
{
    struct pmem *pmem_tmp;
    struct venc_bufferpayload dev_buffer;

    memset(&dev_buffer, 0, sizeof(dev_buffer));
    pmem_tmp = (struct pmem *)buf_addr;

    if (port == PORT_INDEX_IN) {
        dev_buffer.pbuffer = (OMX_U8 *)pmem_tmp->buffer;
        dev_buffer.fd  = pmem_tmp->fd;
        dev_buffer.maped_size = pmem_tmp->size;
        dev_buffer.sz = pmem_tmp->size;
        dev_buffer.offset = pmem_tmp->offset;
        DEBUG_PRINT_LOW("venc_free_buf:pbuffer = %p,fd = %x, offset = %d, maped_size = %d", \
                dev_buffer.pbuffer, \
                dev_buffer.fd, \
                dev_buffer.offset, \
                dev_buffer.maped_size);

    } else if (port == PORT_INDEX_OUT) {
        dev_buffer.pbuffer = (OMX_U8 *)pmem_tmp->buffer;
        dev_buffer.fd  = pmem_tmp->fd;
        dev_buffer.sz = pmem_tmp->size;
        dev_buffer.maped_size = pmem_tmp->size;
        dev_buffer.offset = pmem_tmp->offset;

        DEBUG_PRINT_LOW("venc_free_buf:pbuffer = %p,fd = %x, offset = %d, maped_size = %d", \
                dev_buffer.pbuffer, \
                dev_buffer.fd, \
                dev_buffer.offset, \
                dev_buffer.maped_size);
    } else {
        DEBUG_PRINT_ERROR("ERROR: venc_free_buf:Invalid Port Index ");
        return false;
    }

    return true;
}

bool venc_dev::venc_color_align(OMX_BUFFERHEADERTYPE *buffer,
        OMX_U32 width, OMX_U32 height)
{
    OMX_U32 y_stride = VENUS_Y_STRIDE(COLOR_FMT_NV12, width),
            y_scanlines = VENUS_Y_SCANLINES(COLOR_FMT_NV12, height),
            uv_stride = VENUS_UV_STRIDE(COLOR_FMT_NV12, width),
            uv_scanlines = VENUS_UV_SCANLINES(COLOR_FMT_NV12, height),
            src_chroma_offset = width * height;

    if (buffer->nAllocLen >= VENUS_BUFFER_SIZE(COLOR_FMT_NV12, width, height)) {
        OMX_U8* src_buf = buffer->pBuffer, *dst_buf = buffer->pBuffer;
        //Do chroma first, so that we can convert it in-place
        src_buf += width * height;
        dst_buf += y_stride * y_scanlines;
        for (int line = height / 2 - 1; line >= 0; --line) {
            memmove(dst_buf + line * uv_stride,
                    src_buf + line * width,
                    width);
        }

        dst_buf = src_buf = buffer->pBuffer;
        //Copy the Y next
        for (int line = height - 1; line > 0; --line) {
            memmove(dst_buf + line * y_stride,
                    src_buf + line * width,
                    width);
        }
    } else {
        DEBUG_PRINT_ERROR("Failed to align Chroma. from %u to %u : \
                Insufficient bufferLen=%u v/s Required=%u",
                (unsigned int)(width*height), (unsigned int)src_chroma_offset, (unsigned int)buffer->nAllocLen,
                VENUS_BUFFER_SIZE(COLOR_FMT_NV12, width, height));
        return false;
    }

    return true;
}

bool venc_dev::venc_get_performance_level(OMX_U32 *perflevel)
{
    if (!perflevel) {
        DEBUG_PRINT_ERROR("Null pointer error");
        return false;
    } else {
        *perflevel = performance_level.perflevel;
        return true;
    }
}

bool venc_dev::venc_get_vui_timing_info(OMX_U32 *enabled)
{
    if (!enabled) {
        DEBUG_PRINT_ERROR("Null pointer error");
        return false;
    } else {
        *enabled = vui_timing_info.enabled;
        return true;
    }
}

bool venc_dev::venc_get_vqzip_sei_info(OMX_U32 *enabled)
{
    if (!enabled) {
        DEBUG_PRINT_ERROR("Null pointer error");
        return false;
    } else {
        *enabled = vqzip_sei_info.enabled;
        return true;
    }
}

bool venc_dev::venc_get_peak_bitrate(OMX_U32 *peakbitrate)
{
    if (!peakbitrate) {
        DEBUG_PRINT_ERROR("Null pointer error");
        return false;
    } else {
        *peakbitrate = peak_bitrate.peakbitrate;
        return true;
    }
}

bool venc_dev::venc_get_batch_size(OMX_U32 *size)
{
    if (!size) {
        DEBUG_PRINT_ERROR("Null pointer error");
        return false;
    } else {
        *size = mBatchSize;
        return true;
    }
}

bool venc_dev::venc_empty_buf(void *buffer, void *pmem_data_buf, unsigned index, unsigned fd)
{
    struct pmem *temp_buffer;
    struct v4l2_buffer buf;
    struct v4l2_requestbuffers bufreq;
    struct v4l2_plane plane[VIDEO_MAX_PLANES];
    int rc = 0, extra_idx;
    struct OMX_BUFFERHEADERTYPE *bufhdr;
    encoder_media_buffer_type * meta_buf = NULL;
    temp_buffer = (struct pmem *)buffer;

    memset (&buf, 0, sizeof(buf));
    memset (&plane, 0, sizeof(plane));

    if (buffer == NULL) {
        DEBUG_PRINT_ERROR("ERROR: venc_etb: buffer is NULL");
        return false;
    }

    bufhdr = (OMX_BUFFERHEADERTYPE *)buffer;
    bufreq.memory = V4L2_MEMORY_USERPTR;
    bufreq.count = m_sInput_buff_property.actualcount;
    bufreq.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;

    DEBUG_PRINT_LOW("Input buffer length %u", (unsigned int)bufhdr->nFilledLen);

    if (pmem_data_buf) {
        DEBUG_PRINT_LOW("\n Internal PMEM addr for i/p Heap UseBuf: %p", pmem_data_buf);
        plane[0].m.userptr = (unsigned long)pmem_data_buf;
        plane[0].data_offset = bufhdr->nOffset;
        plane[0].length = bufhdr->nAllocLen;
        plane[0].bytesused = bufhdr->nFilledLen;
    } else {
        // --------------------------------------------------------------------------------------
        // [Usage]             [metadatamode] [Type]        [color_format] [Where is buffer info]
        // ---------------------------------------------------------------------------------------
        // Camera-2              1            CameraSource   0              meta-handle
        // Camera-3              1            GrallocSource  0              gralloc-private-handle
        // surface encode (RBG)  1            GrallocSource  1              bufhdr (color-converted)
        // CPU (Eg: MediaCodec)  0            --             0              bufhdr
        // ---------------------------------------------------------------------------------------
        if (metadatamode) {
            plane[0].m.userptr = (unsigned long)bufhdr->pBuffer;
            meta_buf = (encoder_media_buffer_type *)bufhdr->pBuffer;

            if (!meta_buf) {
                //empty EOS buffer
                if (!bufhdr->nFilledLen && (bufhdr->nFlags & OMX_BUFFERFLAG_EOS)) {
                    plane[0].data_offset = bufhdr->nOffset;
                    plane[0].length = bufhdr->nAllocLen;
                    plane[0].bytesused = bufhdr->nFilledLen;
                    DEBUG_PRINT_LOW("venc_empty_buf: empty EOS buffer");
                } else {
                    return false;
                }
            } else if (!color_format) {

                if (meta_buf->buffer_type == kMetadataBufferTypeCameraSource) {
                    native_handle_t *hnd = (native_handle_t*)meta_buf->meta_handle;
                    if (!hnd) {
                        DEBUG_PRINT_ERROR("ERROR: venc_etb: handle is NULL");
                        return false;
                    }

                    if (!streaming[OUTPUT_PORT] && !(m_sVenc_cfg.inputformat == V4L2_PIX_FMT_RGB32 ||
                        m_sVenc_cfg.inputformat == V4L2_PIX_FMT_RGBA8888_UBWC)) {
                        int usage = 0;
                        struct v4l2_format fmt;
                        fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
                        m_sVenc_cfg.inputformat = V4L2_PIX_FMT_NV12;
                        fmt.fmt.pix_mp.height = m_sVenc_cfg.input_height;
                        fmt.fmt.pix_mp.width = m_sVenc_cfg.input_width;
                        if (!mBatchSize && hnd->numFds + hnd->numInts > 3) {
                            usage = hnd->data[3];
                        } else if (mBatchSize) {
                            usage = BatchInfo::getColorFormatAt(hnd, 0);
                        }
                        if (usage & private_handle_t::PRIV_FLAGS_UBWC_ALIGNED) {
                            m_sVenc_cfg.inputformat = V4L2_PIX_FMT_NV12_UBWC;
                        }
                        if (usage & private_handle_t::PRIV_FLAGS_ITU_R_709) {
                            buf.flags = V4L2_MSM_BUF_FLAG_YUV_601_709_CLAMP;
                        }
                        fmt.fmt.pix_mp.pixelformat = m_sVenc_cfg.inputformat;
                        if (ioctl(m_nDriver_fd, VIDIOC_S_FMT, &fmt)) {
                            DEBUG_PRINT_ERROR("Failed setting color format in Camerasource %x", m_sVenc_cfg.inputformat);
                            return false;
                        }
                        if(ioctl(m_nDriver_fd,VIDIOC_REQBUFS, &bufreq)) {
                            DEBUG_PRINT_ERROR("VIDIOC_REQBUFS OUTPUT_MPLANE Failed");
                            return false;
                        }
                    }

                    // Setting batch mode is sticky. We do not expect camera to change
                    // between batch and normal modes at runtime.
                    if (mBatchSize) {
                        if ((unsigned)hnd->numFds != mBatchSize) {
                            DEBUG_PRINT_ERROR("Don't support dynamic batch sizes (changed from %d->%d)",
                                    mBatchSize, hnd->numFds);
                            return false;
                        }

                        return venc_empty_batch ((OMX_BUFFERHEADERTYPE*)buffer, index);
                    }

                    if (hnd->numFds + hnd->numInts > 2) {
                        plane[0].data_offset = hnd->data[1];
                        plane[0].length = hnd->data[2];
                        plane[0].bytesused = hnd->data[2];
                    }
                    DEBUG_PRINT_LOW("venc_empty_buf: camera buf: fd = %d filled %d of %d flag 0x%x format 0x%x",
                            fd, plane[0].bytesused, plane[0].length, buf.flags, m_sVenc_cfg.inputformat);
                } else if (meta_buf->buffer_type == kMetadataBufferTypeGrallocSource) {
                    private_handle_t *handle = (private_handle_t *)meta_buf->meta_handle;
                    if (!streaming[OUTPUT_PORT]) {
                        struct v4l2_format fmt;
                        fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
                        if (handle->format == HAL_PIXEL_FORMAT_NV12_ENCODEABLE) {
                            if ((handle->flags & private_handle_t::PRIV_FLAGS_UBWC_ALIGNED) &&
                                 is_gralloc_source_ubwc) {
                                 m_sVenc_cfg.inputformat = V4L2_PIX_FMT_NV12_UBWC;
                            } else {
                                m_sVenc_cfg.inputformat = V4L2_PIX_FMT_NV12;
                            }
                        } else if (handle->format == HAL_PIXEL_FORMAT_RGBA_8888) {
                            if ((handle->flags & private_handle_t::PRIV_FLAGS_UBWC_ALIGNED) &&
                                 is_gralloc_source_ubwc) {
                                 m_sVenc_cfg.inputformat = V4L2_PIX_FMT_RGBA8888_UBWC;
                            } else {
                                m_sVenc_cfg.inputformat = V4L2_PIX_FMT_RGB32;
                            }
                        } else if (  handle->format == QOMX_COLOR_FORMATYUV420PackedSemiPlanar32m) {
                                m_sVenc_cfg.inputformat = V4L2_PIX_FMT_NV12;
                        }
                        fmt.fmt.pix_mp.pixelformat = m_sVenc_cfg.inputformat;
                        fmt.fmt.pix_mp.height = m_sVenc_cfg.input_height;
                        fmt.fmt.pix_mp.width = m_sVenc_cfg.input_width;
                        if (ioctl(m_nDriver_fd, VIDIOC_S_FMT, &fmt)) {
                            DEBUG_PRINT_ERROR("Failed setting color format in Grallocsource %x", m_sVenc_cfg.inputformat);
                            return false;
                        }
                        if(ioctl(m_nDriver_fd,VIDIOC_REQBUFS, &bufreq)) {
                            DEBUG_PRINT_ERROR("VIDIOC_REQBUFS OUTPUT_MPLANE Failed");
                            return false;
                        }
                    }

                    fd = handle->fd;
                    plane[0].data_offset = 0;
                    plane[0].length = handle->size;
                    plane[0].bytesused = handle->size;
                    DEBUG_PRINT_LOW("venc_empty_buf: Opaque camera buf: fd = %d "
                                ": filled %d of %d format 0x%x", fd, plane[0].bytesused, plane[0].length, m_sVenc_cfg.inputformat);
                }
            } else {
                plane[0].m.userptr = (unsigned long) bufhdr->pBuffer;
                plane[0].data_offset = bufhdr->nOffset;
                plane[0].length = bufhdr->nAllocLen;
                plane[0].bytesused = bufhdr->nFilledLen;
                DEBUG_PRINT_LOW("venc_empty_buf: Opaque non-camera buf: fd = %d filled %d of %d",
                        fd, plane[0].bytesused, plane[0].length);
            }
        } else {
            plane[0].m.userptr = (unsigned long) bufhdr->pBuffer;
            plane[0].data_offset = bufhdr->nOffset;
            plane[0].length = bufhdr->nAllocLen;
            plane[0].bytesused = bufhdr->nFilledLen;
            DEBUG_PRINT_LOW("venc_empty_buf: non-camera buf: fd = %d filled %d of %d",
                    fd, plane[0].bytesused, plane[0].length);
        }
    }

    extra_idx = EXTRADATA_IDX(num_input_planes);

    if (extra_idx && (extra_idx < VIDEO_MAX_PLANES)) {
        int extradata_index = venc_get_index_from_fd(fd);
        if (extradata_index < 0 ) {
                DEBUG_PRINT_ERROR("Extradata index calculation went wrong for fd = %d", fd);
                return OMX_ErrorBadParameter;
            }

        plane[extra_idx].bytesused = 0;
        plane[extra_idx].length = input_extradata_info.buffer_size;
        plane[extra_idx].m.userptr = (unsigned long) (input_extradata_info.uaddr + extradata_index * input_extradata_info.buffer_size);
#ifdef USE_ION
        plane[extra_idx].reserved[0] = input_extradata_info.ion.fd_ion_data.fd;
#endif
        plane[extra_idx].reserved[1] = input_extradata_info.buffer_size * extradata_index;
        plane[extra_idx].data_offset = 0;
    } else if (extra_idx >= VIDEO_MAX_PLANES) {
        DEBUG_PRINT_ERROR("Extradata index higher than expected: %d\n", extra_idx);
        return false;
    }

    buf.index = index;
    buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    buf.memory = V4L2_MEMORY_USERPTR;
    plane[0].reserved[0] = fd;
    plane[0].reserved[1] = 0;
    buf.m.planes = plane;
    buf.length = num_input_planes;

    if (bufhdr->nFlags & OMX_BUFFERFLAG_EOS)
        buf.flags |= V4L2_QCOM_BUF_FLAG_EOS;

    buf.timestamp.tv_sec = bufhdr->nTimeStamp / 1000000;
    buf.timestamp.tv_usec = (bufhdr->nTimeStamp % 1000000);
    rc = ioctl(m_nDriver_fd, VIDIOC_QBUF, &buf);

    if (rc) {
        DEBUG_PRINT_ERROR("Failed to qbuf (etb) to driver");
        return false;
    }

    etb++;

    if (!streaming[OUTPUT_PORT]) {
        enum v4l2_buf_type buf_type;
        buf_type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        int ret;
        ret = ioctl(m_nDriver_fd, VIDIOC_STREAMON, &buf_type);

        if (ret) {
            DEBUG_PRINT_ERROR("Failed to call streamon");
            if (errno == EBUSY) {
                hw_overload = true;
            }
            return false;
        } else {
            streaming[OUTPUT_PORT] = true;
        }
    }
    if (m_debug.in_buffer_log) {
        venc_input_log_buffers(bufhdr, fd, plane[0].data_offset, m_sVenc_cfg.inputformat);
    }

    return true;
}

bool venc_dev::venc_empty_batch(OMX_BUFFERHEADERTYPE *bufhdr, unsigned index)
{
    struct v4l2_buffer buf;
    struct v4l2_plane plane;
    int rc = 0;
    struct v4l2_control control;
    encoder_media_buffer_type * meta_buf = NULL;
    native_handle_t *hnd = NULL;

    if (bufhdr == NULL) {
        DEBUG_PRINT_ERROR("ERROR: %s: buffer is NULL", __func__);
        return false;
    }

    bool status = true;
    if (metadatamode) {
        plane.m.userptr = (unsigned long)bufhdr->pBuffer;
        meta_buf = (encoder_media_buffer_type *)bufhdr->pBuffer;

        if (!color_format) {
            if (meta_buf->buffer_type == kMetadataBufferTypeCameraSource) {
                hnd = (native_handle_t*)meta_buf->meta_handle;
                if (!hnd) {
                    DEBUG_PRINT_ERROR("venc_empty_batch: invalid handle !");
                    return false;
                } else if (hnd->numFds > kMaxBuffersInBatch) {
                    DEBUG_PRINT_ERROR("venc_empty_batch: Too many buffers (%d) in batch. "
                            "Max = %d", hnd->numFds, kMaxBuffersInBatch);
                    status = false;
                }
                DEBUG_PRINT_LOW("venc_empty_batch: Batch of %d bufs", hnd->numFds);
            } else {
                DEBUG_PRINT_ERROR("Batch supported for CameraSource buffers only !");
                status = false;
            }
        } else {
            DEBUG_PRINT_ERROR("Batch supported for Camera buffers only !");
            status = false;
        }
    } else {
        DEBUG_PRINT_ERROR("Batch supported for metabuffer mode only !");
        status = false;
    }

    if (status) {
        OMX_TICKS bufTimeStamp = 0ll;
        int numBufs = hnd->numFds;
        int v4l2Ids[kMaxBuffersInBatch] = {-1};
        for (int i = 0; i < numBufs; ++i) {
            v4l2Ids[i] = mBatchInfo.registerBuffer(index);
            if (v4l2Ids[i] < 0) {
                DEBUG_PRINT_ERROR("Failed to register buffer");
                // TODO: cleanup the map and purge all slots of current index
                status = false;
                break;
            }
        }
        for (int i = 0; i < numBufs; ++i) {
            int v4l2Id = v4l2Ids[i];

            memset(&buf, 0, sizeof(buf));
            memset(&plane, 0, sizeof(plane));

            DEBUG_PRINT_LOW("Batch: registering %d as %d", index, v4l2Id);
            buf.index = (unsigned)v4l2Id;
            buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
            buf.memory = V4L2_MEMORY_USERPTR;
            plane.reserved[0] = BatchInfo::getFdAt(hnd, i);
            plane.reserved[1] = 0;
            plane.data_offset = BatchInfo::getOffsetAt(hnd, i);
            plane.m.userptr = (unsigned long)meta_buf;
            plane.length = plane.bytesused = BatchInfo::getSizeAt(hnd, i);
            buf.m.planes = &plane;
            buf.length = 1;

            if (bufhdr->nFlags & OMX_BUFFERFLAG_EOS)
                buf.flags |= V4L2_QCOM_BUF_FLAG_EOS;
            if (i != numBufs - 1) {
                buf.flags |= V4L2_MSM_BUF_FLAG_DEFER;
                DEBUG_PRINT_LOW("for buffer %d (etb #%d) in batch of %d, marking as defer",
                        i, etb + 1, numBufs);
            }


            // timestamp differences from camera are in nano-seconds
            bufTimeStamp = bufhdr->nTimeStamp + BatchInfo::getTimeStampAt(hnd, i) / 1000;

            DEBUG_PRINT_LOW(" Q Batch [%d of %d] : buf=%x fd=%d len=%d TS=%lld",
                i, numBufs, bufhdr, plane.reserved[0], plane.length, bufTimeStamp);
            buf.timestamp.tv_sec = bufTimeStamp / 1000000;
            buf.timestamp.tv_usec = (bufTimeStamp % 1000000);

            rc = ioctl(m_nDriver_fd, VIDIOC_QBUF, &buf);
            if (rc) {
                DEBUG_PRINT_ERROR("%s: Failed to qbuf (etb) to driver", __func__);
                return false;
            }

            etb++;
        }
    }

    if (status && !streaming[OUTPUT_PORT]) {
        enum v4l2_buf_type buf_type;
        buf_type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        int ret;
        ret = ioctl(m_nDriver_fd, VIDIOC_STREAMON, &buf_type);
        if (ret) {
            DEBUG_PRINT_ERROR("Failed to call streamon");
            if (errno == EBUSY) {
                hw_overload = true;
            }
            status = false;
        } else {
            streaming[OUTPUT_PORT] = true;
        }
    }

    return status;
}

bool venc_dev::venc_fill_buf(void *buffer, void *pmem_data_buf,unsigned index,unsigned fd)
{
    struct pmem *temp_buffer = NULL;
    struct venc_buffer  frameinfo;
    struct v4l2_buffer buf;
    struct v4l2_plane plane[VIDEO_MAX_PLANES];
    int rc = 0;
    unsigned int extra_idx;
    struct OMX_BUFFERHEADERTYPE *bufhdr;

    if (buffer == NULL)
        return false;

    bufhdr = (OMX_BUFFERHEADERTYPE *)buffer;

    if (pmem_data_buf) {
        DEBUG_PRINT_LOW("Internal PMEM addr for o/p Heap UseBuf: %p", pmem_data_buf);
        plane[0].m.userptr = (unsigned long)pmem_data_buf;
    } else {
        DEBUG_PRINT_LOW("Shared PMEM addr for o/p PMEM UseBuf/AllocateBuf: %p", bufhdr->pBuffer);
        plane[0].m.userptr = (unsigned long)bufhdr->pBuffer;
    }

    memset(&buf, 0, sizeof(buf));
    memset(&plane, 0, sizeof(plane));

    buf.index = index;
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buf.memory = V4L2_MEMORY_USERPTR;
    plane[0].length = bufhdr->nAllocLen;
    plane[0].bytesused = bufhdr->nFilledLen;
    plane[0].reserved[0] = fd;
    plane[0].reserved[1] = 0;
    plane[0].data_offset = bufhdr->nOffset;
    buf.m.planes = plane;
    buf.length = num_output_planes;
    buf.flags = 0;

    if (mBatchSize) {
        // Should always mark first buffer as DEFER, since 0 % anything is 0, just offset by 1
        // This results in the first batch being of size mBatchSize + 1, but thats good because
        // we need an extra FTB for the codec specific data.

        if (!ftb || ftb % mBatchSize) {
            buf.flags |= V4L2_MSM_BUF_FLAG_DEFER;
            DEBUG_PRINT_LOW("for ftb buffer %d marking as defer", ftb + 1);
        }
    }

    extra_idx = EXTRADATA_IDX(num_output_planes);

    if (extra_idx && (extra_idx < VIDEO_MAX_PLANES)) {
        plane[extra_idx].bytesused = 0;
        plane[extra_idx].length = output_extradata_info.buffer_size;
        plane[extra_idx].m.userptr = (unsigned long) (output_extradata_info.uaddr + index * output_extradata_info.buffer_size);
#ifdef USE_ION
        plane[extra_idx].reserved[0] = output_extradata_info.ion.fd_ion_data.fd;
#endif
        plane[extra_idx].reserved[1] = output_extradata_info.buffer_size * index;
        plane[extra_idx].data_offset = 0;
    } else if (extra_idx >= VIDEO_MAX_PLANES) {
        DEBUG_PRINT_ERROR("Extradata index higher than expected: %d", extra_idx);
        return false;
    }

    rc = ioctl(m_nDriver_fd, VIDIOC_QBUF, &buf);

    if (rc) {
        DEBUG_PRINT_ERROR("Failed to qbuf (ftb) to driver");
        return false;
    }

    ftb++;
    return true;
}

bool venc_dev::venc_set_inband_video_header(OMX_BOOL enable)
{
    struct v4l2_control control;

    control.id = V4L2_CID_MPEG_VIDEO_HEADER_MODE;
    if(enable) {
        control.value = V4L2_MPEG_VIDEO_HEADER_MODE_JOINED_WITH_I_FRAME;
    } else {
        control.value = V4L2_MPEG_VIDEO_HEADER_MODE_SEPARATE;
    }

    DEBUG_PRINT_HIGH("Set inband sps/pps: %d", enable);
    if(ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control) < 0) {
        DEBUG_PRINT_ERROR("Request for inband sps/pps failed");
        return false;
    }
    return true;
}

bool venc_dev::venc_set_au_delimiter(OMX_BOOL enable)
{
    struct v4l2_control control;

    control.id = V4L2_CID_MPEG_VIDC_VIDEO_H264_AU_DELIMITER;
    if(enable) {
        control.value = V4L2_MPEG_VIDC_VIDEO_H264_AU_DELIMITER_ENABLED;
    } else {
        control.value = V4L2_MPEG_VIDC_VIDEO_H264_AU_DELIMITER_DISABLED;
    }

    DEBUG_PRINT_HIGH("Set au delimiter: %d", enable);
    if(ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control) < 0) {
        DEBUG_PRINT_ERROR("Request to set AU delimiter failed");
        return false;
    }
    return true;
}

bool venc_dev::venc_set_mbi_statistics_mode(OMX_U32 mode)
{
    struct v4l2_control control;

    control.id = V4L2_CID_MPEG_VIDC_VIDEO_MBI_STATISTICS_MODE;
    control.value = mode;

    DEBUG_PRINT_HIGH("Set MBI dumping mode: %d", mode);
    if(ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control) < 0) {
        DEBUG_PRINT_ERROR("Setting MBI mode failed");
        return false;
    }
    return true;
}

int venc_dev::venc_get_index_from_fd(OMX_U32 fd)
{
    unsigned int i = 0;
    for (;i < 64; i++) {
        if (fd_list[i] == fd) {
            DEBUG_PRINT_HIGH("FD is present at index = %d", i);
            return i;
        }
    }
    for (i = 0;i < 64; i++)
        if (fd_list[i] == 0) {
            DEBUG_PRINT_HIGH("FD added at index = %d", i);
            fd_list[i] = fd;
            return i;
    }
    return -EINVAL;
}

bool venc_dev::venc_set_vqzip_sei_type(OMX_BOOL enable)
{
    struct v4l2_control sei_control, yuvstats_control;

    DEBUG_PRINT_HIGH("Set VQZIP SEI: %d", enable);
    sei_control.id = V4L2_CID_MPEG_VIDC_VIDEO_VQZIP_SEI;
    yuvstats_control.id = V4L2_CID_MPEG_VIDC_VIDEO_EXTRADATA;

    if (ioctl(m_nDriver_fd, VIDIOC_G_CTRL, &yuvstats_control) < 0) {
        DEBUG_PRINT_HIGH("Non-Fatal: Request to set VQZIP failed");
    }

    if(enable) {
        sei_control.value = V4L2_CID_MPEG_VIDC_VIDEO_VQZIP_SEI_ENABLE;
        yuvstats_control.value |= V4L2_MPEG_VIDC_EXTRADATA_YUV_STATS;
    } else {
        sei_control.value = V4L2_CID_MPEG_VIDC_VIDEO_VQZIP_SEI_DISABLE;
        yuvstats_control.value &= ~V4L2_MPEG_VIDC_EXTRADATA_YUV_STATS;
    }

    if (ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &sei_control) < 0) {
        DEBUG_PRINT_HIGH("Non-Fatal: Request to set SEI failed");
    }

    if (ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &yuvstats_control) < 0) {
        DEBUG_PRINT_HIGH("Non-Fatal: Request to set YUVSTATS failed");
    }
    vqzip.pConfig.nWidth = ALIGN(m_sVenc_cfg.input_width, 16);
    vqzip.pConfig.nHeight = ALIGN(m_sVenc_cfg.input_height, 16);
    vqzip.init();
    vqzip_sei_info.enabled = true;

    return true;
}

bool venc_dev::venc_validate_hybridhp_params(OMX_U32 layers, OMX_U32 bFrames, OMX_U32 count, int mode)
{
    // Check for layers in Hier-p/hier-B with Hier-P-Hybrid
    if (layers && (mode == HIER_P || mode == HIER_B) && hier_layers.hier_mode == HIER_P_HYBRID)
        return false;

    // Check for bframes with Hier-P-Hybrid
    if (bFrames && hier_layers.hier_mode == HIER_P_HYBRID)
        return false;

    // Check for Hier-P-Hybrid with bframes/LTR/hier-p/Hier-B
    if (layers && mode == HIER_P_HYBRID && (intra_period.num_bframes || hier_layers.hier_mode == HIER_P ||
           hier_layers.hier_mode == HIER_B || ltrinfo.count))
        return false;

    // Check for LTR with Hier-P-Hybrid
    if (count && hier_layers.hier_mode == HIER_P_HYBRID)
        return false;

    return true;
}

bool venc_dev::venc_set_hier_layers(QOMX_VIDEO_HIERARCHICALCODINGTYPE type,
                                    OMX_U32 num_layers)
{
    struct v4l2_control control;

    if (!venc_validate_hybridhp_params(num_layers, 0, 0, (int)type)){
        DEBUG_PRINT_ERROR("Invalid settings, Hier-pLayers enabled with HybridHP");
        return false;
    }

    if (type == QOMX_HIERARCHICALCODING_P) {
        // Reduce layer count by 1 before sending to driver. This avoids
        // driver doing the same in multiple places.
        control.id = V4L2_CID_MPEG_VIDC_VIDEO_HIER_P_NUM_LAYERS;
        control.value = num_layers - 1;
        DEBUG_PRINT_HIGH("Set Hier P num layers: %u", (unsigned int)num_layers);
        if (ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control)) {
            DEBUG_PRINT_ERROR("Request to set Hier P num layers failed");
            return false;
        }
        if (m_sVenc_cfg.codectype == V4L2_PIX_FMT_H264) {
            DEBUG_PRINT_LOW("Set H264_SVC_NAL");
            control.id = V4L2_CID_MPEG_VIDC_VIDEO_H264_NAL_SVC;
            control.value = V4L2_CID_MPEG_VIDC_VIDEO_H264_NAL_SVC_ENABLED;
            if (ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control)) {
                DEBUG_PRINT_ERROR("Failed to enable SVC_NAL");
                return false;
            }
        }
        hier_layers.hier_mode = HIER_P;
    } else if (type == QOMX_HIERARCHICALCODING_B) {
        if (m_sVenc_cfg.codectype != V4L2_PIX_FMT_HEVC) {
            DEBUG_PRINT_ERROR("Failed : Hier B layers supported only for HEVC encode");
            return false;
        }
        control.id = V4L2_CID_MPEG_VIDC_VIDEO_HIER_B_NUM_LAYERS;
        control.value = num_layers - 1;
        DEBUG_PRINT_INFO("Set Hier B num layers: %u", (unsigned int)num_layers);
        if (ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control)) {
            DEBUG_PRINT_ERROR("Request to set Hier P num layers failed");
            return false;
        }
        hier_layers.hier_mode = HIER_B;
    } else {
        DEBUG_PRINT_ERROR("Request to set hier num layers failed for type: %d", type);
        return false;
    }
    hier_layers.numlayers = num_layers;
    return true;
}

bool venc_dev::venc_set_extradata(OMX_U32 extra_data, OMX_BOOL enable)
{
    struct v4l2_control control;

    DEBUG_PRINT_HIGH("venc_set_extradata:: %x", (int) extra_data);

    if (enable == OMX_FALSE) {
        /* No easy way to turn off extradata to the driver
         * at the moment */
        return false;
    }

    control.id = V4L2_CID_MPEG_VIDC_VIDEO_EXTRADATA;
    switch (extra_data) {
        case OMX_ExtraDataVideoEncoderSliceInfo:
            control.value = V4L2_MPEG_VIDC_EXTRADATA_MULTISLICE_INFO;
            break;
        case OMX_ExtraDataVideoEncoderMBInfo:
            control.value = V4L2_MPEG_VIDC_EXTRADATA_METADATA_MBI;
            break;
        case OMX_ExtraDataFrameDimension:
            control.value = V4L2_MPEG_VIDC_EXTRADATA_INPUT_CROP;
            break;
        default:
            DEBUG_PRINT_ERROR("Unrecognized extradata index 0x%x", (unsigned int)extra_data);
            return false;
    }

    if (ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control)) {
        DEBUG_PRINT_ERROR("ERROR: Request for setting extradata (%x) failed %d",
                (unsigned int)extra_data, errno);
        return false;
    }

    return true;
}

bool venc_dev::venc_set_slice_delivery_mode(OMX_U32 enable)
{
    struct v4l2_control control;

    if (enable) {
        control.id = V4L2_CID_MPEG_VIDEO_MULTI_SLICE_DELIVERY_MODE;
        control.value = 1;
        DEBUG_PRINT_LOW("Set slice_delivery_mode: %d", control.value);

        if (multislice.mslice_mode == V4L2_MPEG_VIDEO_MULTI_SICE_MODE_MAX_MB && m_sVenc_cfg.codectype == V4L2_PIX_FMT_H264) {
            if (ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control)) {
                DEBUG_PRINT_ERROR("Request for setting slice delivery mode failed");
                return false;
            } else {
                DEBUG_PRINT_LOW("Successfully set Slice delivery mode id: %d, value=%d", control.id, control.value);
                slice_mode.enable = 1;
            }
        } else {
            DEBUG_PRINT_ERROR("Failed to set slice delivery mode, slice_mode [%lu] "
                    "is not MB BASED or [%lu] is not H264 codec ", multislice.mslice_mode,
                    m_sVenc_cfg.codectype);
        }
    } else {
        DEBUG_PRINT_ERROR("Slice_DELIVERY_MODE not enabled");
    }

    return true;
}

bool venc_dev::venc_enable_initial_qp(QOMX_EXTNINDEX_VIDEO_INITIALQP* initqp)
{
    int rc;
    struct v4l2_control control;
    struct v4l2_ext_control ctrl[4];
    struct v4l2_ext_controls controls;

    ctrl[0].id = V4L2_CID_MPEG_VIDC_VIDEO_I_FRAME_QP;
    ctrl[0].value = initqp->nQpI;
    ctrl[1].id = V4L2_CID_MPEG_VIDC_VIDEO_P_FRAME_QP;
    ctrl[1].value = initqp->nQpP;
    ctrl[2].id = V4L2_CID_MPEG_VIDC_VIDEO_B_FRAME_QP;
    ctrl[2].value = initqp->nQpB;
    ctrl[3].id = V4L2_CID_MPEG_VIDC_VIDEO_ENABLE_INITIAL_QP;
    ctrl[3].value = initqp->bEnableInitQp;

    controls.count = 4;
    controls.ctrl_class = V4L2_CTRL_CLASS_MPEG;
    controls.controls = ctrl;

    DEBUG_PRINT_LOW("Calling IOCTL set control for id=%x val=%d, id=%x val=%d, id=%x val=%d, id=%x val=%d",
                    controls.controls[0].id, controls.controls[0].value,
                    controls.controls[1].id, controls.controls[1].value,
                    controls.controls[2].id, controls.controls[2].value,
                    controls.controls[3].id, controls.controls[3].value);

    rc = ioctl(m_nDriver_fd, VIDIOC_S_EXT_CTRLS, &controls);
    if (rc) {
        DEBUG_PRINT_ERROR("Failed to set session_qp %d", rc);
        return false;
    }

    init_qp.iframeqp = initqp->nQpI;
    init_qp.pframeqp = initqp->nQpP;
    init_qp.bframeqp = initqp->nQpB;
    init_qp.enableinitqp = initqp->bEnableInitQp;

    DEBUG_PRINT_LOW("Success IOCTL set control for id=%x val=%d, id=%x val=%d, id=%x val=%d, id=%x val=%d",
                    controls.controls[0].id, controls.controls[0].value,
                    controls.controls[1].id, controls.controls[1].value,
                    controls.controls[2].id, controls.controls[2].value,
                    controls.controls[3].id, controls.controls[3].value);
    return true;
}

bool venc_dev::venc_set_session_qp(OMX_U32 i_frame_qp, OMX_U32 p_frame_qp,OMX_U32 b_frame_qp)
{
    int rc;
    struct v4l2_control control;

    control.id = V4L2_CID_MPEG_VIDEO_H264_I_FRAME_QP;
    control.value = i_frame_qp;

    DEBUG_PRINT_LOW("Calling IOCTL set control for id=%d, val=%d", control.id, control.value);
    rc = ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control);

    if (rc) {
        DEBUG_PRINT_ERROR("Failed to set control");
        return false;
    }

    DEBUG_PRINT_LOW("Success IOCTL set control for id=%d, value=%d", control.id, control.value);
    session_qp.iframeqp = control.value;

    control.id = V4L2_CID_MPEG_VIDEO_H264_P_FRAME_QP;
    control.value = p_frame_qp;

    DEBUG_PRINT_LOW("Calling IOCTL set control for id=%d, val=%d", control.id, control.value);
    rc = ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control);

    if (rc) {
        DEBUG_PRINT_ERROR("Failed to set control");
        return false;
    }

    DEBUG_PRINT_LOW("Success IOCTL set control for id=%d, value=%d", control.id, control.value);

    session_qp.pframeqp = control.value;

    if ((codec_profile.profile == V4L2_MPEG_VIDEO_H264_PROFILE_MAIN) ||
            (codec_profile.profile == V4L2_MPEG_VIDEO_H264_PROFILE_HIGH)) {

        control.id = V4L2_CID_MPEG_VIDEO_H264_B_FRAME_QP;
        control.value = b_frame_qp;

        DEBUG_PRINT_LOW("Calling IOCTL set control for id=%d, val=%d", control.id, control.value);
        rc = ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control);

        if (rc) {
            DEBUG_PRINT_ERROR("Failed to set control");
            return false;
        }

        DEBUG_PRINT_LOW("Success IOCTL set control for id=%d, value=%d", control.id, control.value);

        session_qp.bframeqp = control.value;
    }

    return true;
}

bool venc_dev::venc_set_session_qp_range(OMX_U32 min_qp, OMX_U32 max_qp)
{
    int rc;
    struct v4l2_control control;

    if ((min_qp >= session_qp_range.minqp) && (max_qp <= session_qp_range.maxqp)) {

        if (m_sVenc_cfg.codectype == V4L2_PIX_FMT_VP8)
            control.id = V4L2_CID_MPEG_VIDC_VIDEO_VP8_MIN_QP;
        else
            control.id = V4L2_CID_MPEG_VIDEO_H264_MIN_QP;
        control.value = min_qp;

        DEBUG_PRINT_LOW("Calling IOCTL set MIN_QP control id=%d, val=%d",
                control.id, control.value);
        rc = ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control);
        if (rc) {
            DEBUG_PRINT_ERROR("Failed to set control");
            return false;
        }

        if (m_sVenc_cfg.codectype == V4L2_PIX_FMT_VP8)
            control.id = V4L2_CID_MPEG_VIDC_VIDEO_VP8_MAX_QP;
        else
            control.id = V4L2_CID_MPEG_VIDEO_H264_MAX_QP;
        control.value = max_qp;

        DEBUG_PRINT_LOW("Calling IOCTL set MAX_QP control id=%d, val=%d",
                control.id, control.value);
        rc = ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control);
        if (rc) {
            DEBUG_PRINT_ERROR("Failed to set control");
            return false;
        }
    } else {
        DEBUG_PRINT_ERROR("Wrong qp values[%u %u], allowed range[%u %u]",
            (unsigned int)min_qp, (unsigned int)max_qp, (unsigned int)session_qp_range.minqp, (unsigned int)session_qp_range.maxqp);
    }

    return true;
}

bool venc_dev::venc_set_profile_level(OMX_U32 eProfile,OMX_U32 eLevel)
{
    struct venc_profile requested_profile = {0};
    struct ven_profilelevel requested_level = {0};
    unsigned long mb_per_frame = 0;
    DEBUG_PRINT_LOW("venc_set_profile_level:: eProfile = %u, Level = %u",
            (unsigned int)eProfile, (unsigned int)eLevel);
    mb_per_frame = ((m_sVenc_cfg.dvs_height + 15) >> 4)*
        ((m_sVenc_cfg.dvs_width + 15) >> 4);

    if ((eProfile == 0) && (eLevel == 0) && m_profile_set && m_level_set) {
        DEBUG_PRINT_LOW("Profile/Level setting complete before venc_start");
        return true;
    }

    if (vqzip_sei_info.enabled) {
        DEBUG_PRINT_HIGH("VQZIP is enabled. Profile and Level set by client. Skipping validation");
        return true;
    }

    DEBUG_PRINT_LOW("Validating Profile/Level from table");

    if (!venc_validate_profile_level(&eProfile, &eLevel)) {
        DEBUG_PRINT_LOW("ERROR: Profile/Level validation failed");
        return false;
    }

    if (m_sVenc_cfg.codectype == V4L2_PIX_FMT_MPEG4) {
        DEBUG_PRINT_LOW("eProfile = %u, OMX_VIDEO_MPEG4ProfileSimple = %d and "
                "OMX_VIDEO_MPEG4ProfileAdvancedSimple = %d", (unsigned int)eProfile,
                OMX_VIDEO_MPEG4ProfileSimple, OMX_VIDEO_MPEG4ProfileAdvancedSimple);

        if (eProfile == OMX_VIDEO_MPEG4ProfileSimple) {
            requested_profile.profile = V4L2_MPEG_VIDEO_MPEG4_PROFILE_SIMPLE;
        } else if (eProfile == OMX_VIDEO_MPEG4ProfileAdvancedSimple) {
            requested_profile.profile = V4L2_MPEG_VIDEO_MPEG4_PROFILE_ADVANCED_SIMPLE;
        } else {
            DEBUG_PRINT_LOW("ERROR: Unsupported MPEG4 profile = %u",
                    (unsigned int)eProfile);
            return false;
        }

        DEBUG_PRINT_LOW("eLevel = %u, OMX_VIDEO_MPEG4Level0 = %d, OMX_VIDEO_MPEG4Level1 = %d,"
                "OMX_VIDEO_MPEG4Level2 = %d, OMX_VIDEO_MPEG4Level3 = %d, OMX_VIDEO_MPEG4Level4 = %d,"
                "OMX_VIDEO_MPEG4Level5 = %d", (unsigned int)eLevel, OMX_VIDEO_MPEG4Level0, OMX_VIDEO_MPEG4Level1,
                OMX_VIDEO_MPEG4Level2, OMX_VIDEO_MPEG4Level3, OMX_VIDEO_MPEG4Level4, OMX_VIDEO_MPEG4Level5);

        if (mb_per_frame >= 3600) {
            if (requested_profile.profile == V4L2_MPEG_VIDEO_MPEG4_PROFILE_ADVANCED_SIMPLE)
                requested_level.level = V4L2_MPEG_VIDEO_MPEG4_LEVEL_5;

            if (requested_profile.profile == V4L2_MPEG_VIDEO_MPEG4_PROFILE_SIMPLE)
                requested_level.level = V4L2_MPEG_VIDEO_MPEG4_LEVEL_5;
        } else {
            switch (eLevel) {
                case OMX_VIDEO_MPEG4Level0:
                    requested_level.level = V4L2_MPEG_VIDEO_MPEG4_LEVEL_0;
                    break;
                case OMX_VIDEO_MPEG4Level0b:
                    requested_level.level = V4L2_MPEG_VIDEO_MPEG4_LEVEL_0B;
                    break;
                case OMX_VIDEO_MPEG4Level1:
                    requested_level.level = V4L2_MPEG_VIDEO_MPEG4_LEVEL_1;
                    break;
                case OMX_VIDEO_MPEG4Level2:
                    requested_level.level = V4L2_MPEG_VIDEO_MPEG4_LEVEL_2;
                    break;
                case OMX_VIDEO_MPEG4Level3:
                    requested_level.level = V4L2_MPEG_VIDEO_MPEG4_LEVEL_3;
                    break;
                case OMX_VIDEO_MPEG4Level4a:
                    requested_level.level = V4L2_MPEG_VIDEO_MPEG4_LEVEL_4;
                    break;
                case OMX_VIDEO_MPEG4Level5:
                    requested_level.level = V4L2_MPEG_VIDEO_MPEG4_LEVEL_5;
                    break;
                default:
                    return false;
                    // TODO update corresponding levels for MPEG4_LEVEL_3b,MPEG4_LEVEL_6
                    break;
            }
        }
    } else if (m_sVenc_cfg.codectype == V4L2_PIX_FMT_H263) {

        switch (eProfile) {
            case OMX_VIDEO_H263ProfileBaseline:
                requested_profile.profile = V4L2_MPEG_VIDC_VIDEO_H263_PROFILE_BASELINE;
                break;
            case OMX_VIDEO_H263ProfileH320Coding:
                requested_profile.profile = V4L2_MPEG_VIDC_VIDEO_H263_PROFILE_H320CODING;
                break;
            case OMX_VIDEO_H263ProfileBackwardCompatible:
                requested_profile.profile = V4L2_MPEG_VIDC_VIDEO_H263_PROFILE_BACKWARDCOMPATIBLE;
                break;
            case OMX_VIDEO_H263ProfileISWV2:
                requested_profile.profile = V4L2_MPEG_VIDC_VIDEO_H263_PROFILE_ISWV2;
                break;
            case OMX_VIDEO_H263ProfileISWV3:
                requested_profile.profile = V4L2_MPEG_VIDC_VIDEO_H263_PROFILE_ISWV3;
                break;
            case OMX_VIDEO_H263ProfileHighCompression:
                requested_profile.profile = V4L2_MPEG_VIDC_VIDEO_H263_PROFILE_HIGHCOMPRESSION;
                break;
            case OMX_VIDEO_H263ProfileInternet:
                requested_profile.profile = V4L2_MPEG_VIDC_VIDEO_H263_PROFILE_INTERNET;
                break;
            case OMX_VIDEO_H263ProfileInterlace:
                requested_profile.profile = V4L2_MPEG_VIDC_VIDEO_H263_PROFILE_INTERLACE;
                break;
            case OMX_VIDEO_H263ProfileHighLatency:
                requested_profile.profile = V4L2_MPEG_VIDC_VIDEO_H263_PROFILE_HIGHLATENCY;
                break;
            default:
                DEBUG_PRINT_LOW("ERROR: Unsupported H.263 profile = %lu",
                        requested_profile.profile);
                return false;
        }

        //profile level
        switch (eLevel) {
            case OMX_VIDEO_H263Level10:
                requested_level.level = V4L2_MPEG_VIDC_VIDEO_H263_LEVEL_1_0;
                break;
            case OMX_VIDEO_H263Level20:
                requested_level.level = V4L2_MPEG_VIDC_VIDEO_H263_LEVEL_2_0;
                break;
            case OMX_VIDEO_H263Level30:
                requested_level.level = V4L2_MPEG_VIDC_VIDEO_H263_LEVEL_3_0;
                break;
            case OMX_VIDEO_H263Level40:
                requested_level.level = V4L2_MPEG_VIDC_VIDEO_H263_LEVEL_4_0;
                break;
            case OMX_VIDEO_H263Level45:
                requested_level.level = V4L2_MPEG_VIDC_VIDEO_H263_LEVEL_4_5;
                break;
            case OMX_VIDEO_H263Level50:
                requested_level.level = V4L2_MPEG_VIDC_VIDEO_H263_LEVEL_5_0;
                break;
            case OMX_VIDEO_H263Level60:
                requested_level.level = V4L2_MPEG_VIDC_VIDEO_H263_LEVEL_6_0;
                break;
            case OMX_VIDEO_H263Level70:
                requested_level.level = V4L2_MPEG_VIDC_VIDEO_H263_LEVEL_7_0;
                break;
            default:
                return false;
                break;
        }
    } else if (m_sVenc_cfg.codectype == V4L2_PIX_FMT_H264) {
        if (eProfile == OMX_VIDEO_AVCProfileBaseline) {
            requested_profile.profile = V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE;
        } else if(eProfile == QOMX_VIDEO_AVCProfileConstrainedBaseline) {
            requested_profile.profile = V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_BASELINE;
        } else if(eProfile == QOMX_VIDEO_AVCProfileConstrainedHigh) {
            requested_profile.profile = V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_HIGH;
        } else if (eProfile == OMX_VIDEO_AVCProfileMain) {
            requested_profile.profile = V4L2_MPEG_VIDEO_H264_PROFILE_MAIN;
        } else if (eProfile == OMX_VIDEO_AVCProfileExtended) {
            requested_profile.profile = V4L2_MPEG_VIDEO_H264_PROFILE_EXTENDED;
        } else if (eProfile == OMX_VIDEO_AVCProfileHigh) {
            requested_profile.profile = V4L2_MPEG_VIDEO_H264_PROFILE_HIGH;
        } else if (eProfile == OMX_VIDEO_AVCProfileHigh10) {
            requested_profile.profile = V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_10;
        } else if (eProfile == OMX_VIDEO_AVCProfileHigh422) {
            requested_profile.profile = V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_422;
        } else if (eProfile == OMX_VIDEO_AVCProfileHigh444) {
            requested_profile.profile = V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_444_PREDICTIVE;
        } else {
            DEBUG_PRINT_LOW("ERROR: Unsupported H.264 profile = %lu",
                    requested_profile.profile);
            return false;
        }

        //profile level
        switch (eLevel) {
            case OMX_VIDEO_AVCLevel1:
                requested_level.level = V4L2_MPEG_VIDEO_H264_LEVEL_1_0;
                break;
            case OMX_VIDEO_AVCLevel1b:
                requested_level.level = V4L2_MPEG_VIDEO_H264_LEVEL_1B;
                break;
            case OMX_VIDEO_AVCLevel11:
                requested_level.level = V4L2_MPEG_VIDEO_H264_LEVEL_1_1;
                break;
            case OMX_VIDEO_AVCLevel12:
                requested_level.level = V4L2_MPEG_VIDEO_H264_LEVEL_1_2;
                break;
            case OMX_VIDEO_AVCLevel13:
                requested_level.level = V4L2_MPEG_VIDEO_H264_LEVEL_1_3;
                break;
            case OMX_VIDEO_AVCLevel2:
                requested_level.level = V4L2_MPEG_VIDEO_H264_LEVEL_2_0;
                break;
            case OMX_VIDEO_AVCLevel21:
                requested_level.level = V4L2_MPEG_VIDEO_H264_LEVEL_2_1;
                break;
            case OMX_VIDEO_AVCLevel22:
                requested_level.level = V4L2_MPEG_VIDEO_H264_LEVEL_2_2;
                break;
            case OMX_VIDEO_AVCLevel3:
                requested_level.level = V4L2_MPEG_VIDEO_H264_LEVEL_3_0;
                break;
            case OMX_VIDEO_AVCLevel31:
                requested_level.level = V4L2_MPEG_VIDEO_H264_LEVEL_3_1;
                break;
            case OMX_VIDEO_AVCLevel32:
                requested_level.level = V4L2_MPEG_VIDEO_H264_LEVEL_3_2;
                break;
            case OMX_VIDEO_AVCLevel4:
                requested_level.level = V4L2_MPEG_VIDEO_H264_LEVEL_4_0;
                break;
            case OMX_VIDEO_AVCLevel41:
                requested_level.level = V4L2_MPEG_VIDEO_H264_LEVEL_4_1;
                break;
            case OMX_VIDEO_AVCLevel42:
                requested_level.level = V4L2_MPEG_VIDEO_H264_LEVEL_4_2;
                break;
            case OMX_VIDEO_AVCLevel5:
                requested_level.level = V4L2_MPEG_VIDEO_H264_LEVEL_5_0;
                break;
            case OMX_VIDEO_AVCLevel51:
                requested_level.level = V4L2_MPEG_VIDEO_H264_LEVEL_5_1;
                break;
            case OMX_VIDEO_AVCLevel52:
                requested_level.level = V4L2_MPEG_VIDEO_H264_LEVEL_5_2;
                break;
            case OMX_VIDEO_AVCLevelMax:
                requested_level.level = V4L2_MPEG_VIDEO_H264_LEVEL_5_2;
                break;
            default :
                DEBUG_PRINT_ERROR("ERROR: Unsupported H.264 level= %lu",
                        requested_level.level);
                return false;
                break;
        }
    } else if (m_sVenc_cfg.codectype == V4L2_PIX_FMT_VP8) {
        if (!(eProfile == OMX_VIDEO_VP8ProfileMain)) {
            DEBUG_PRINT_ERROR("ERROR: Unsupported VP8 profile = %u",
                        (unsigned int)eProfile);
            return false;
        }
        requested_profile.profile = V4L2_MPEG_VIDC_VIDEO_VP8_UNUSED;
        m_profile_set = true;
        switch(eLevel) {
            case OMX_VIDEO_VP8Level_Version0:
                requested_level.level = V4L2_MPEG_VIDC_VIDEO_VP8_VERSION_0;
                break;
            case OMX_VIDEO_VP8Level_Version1:
                requested_level.level = V4L2_MPEG_VIDC_VIDEO_VP8_VERSION_1;
                break;
            default:
                DEBUG_PRINT_ERROR("ERROR: Unsupported VP8 level= %u",
                            (unsigned int)eLevel);
                return false;
                break;
        }
    }  else if (m_sVenc_cfg.codectype == V4L2_PIX_FMT_HEVC) {
        if (eProfile == OMX_VIDEO_HEVCProfileMain) {
            requested_profile.profile = V4L2_MPEG_VIDC_VIDEO_HEVC_PROFILE_MAIN;
        } else if(eProfile == OMX_VIDEO_HEVCProfileMain10) {
            requested_profile.profile = V4L2_MPEG_VIDC_VIDEO_HEVC_PROFILE_MAIN10;
        } else {
            DEBUG_PRINT_ERROR("ERROR: Unsupported HEVC profile = %lu",
                    requested_profile.profile);
            return false;
        }

        //profile level
        switch (eLevel) {
            case OMX_VIDEO_HEVCMainTierLevel1:
                requested_level.level = V4L2_MPEG_VIDC_VIDEO_HEVC_LEVEL_MAIN_TIER_LEVEL_1;
                break;
            case OMX_VIDEO_HEVCHighTierLevel1:
                requested_level.level = V4L2_MPEG_VIDC_VIDEO_HEVC_LEVEL_HIGH_TIER_LEVEL_1;
                break;
            case OMX_VIDEO_HEVCMainTierLevel2:
                requested_level.level = V4L2_MPEG_VIDC_VIDEO_HEVC_LEVEL_MAIN_TIER_LEVEL_2;
                break;
            case OMX_VIDEO_HEVCHighTierLevel2:
                requested_level.level = V4L2_MPEG_VIDC_VIDEO_HEVC_LEVEL_HIGH_TIER_LEVEL_2;
                break;
            case OMX_VIDEO_HEVCMainTierLevel21:
                requested_level.level = V4L2_MPEG_VIDC_VIDEO_HEVC_LEVEL_MAIN_TIER_LEVEL_2_1;
                break;
            case OMX_VIDEO_HEVCHighTierLevel21:
                requested_level.level = V4L2_MPEG_VIDC_VIDEO_HEVC_LEVEL_HIGH_TIER_LEVEL_2_1;
                break;
            case OMX_VIDEO_HEVCMainTierLevel3:
                requested_level.level = V4L2_MPEG_VIDC_VIDEO_HEVC_LEVEL_MAIN_TIER_LEVEL_3;
                break;
            case OMX_VIDEO_HEVCHighTierLevel3:
                requested_level.level = V4L2_MPEG_VIDC_VIDEO_HEVC_LEVEL_HIGH_TIER_LEVEL_3;
                break;
            case OMX_VIDEO_HEVCMainTierLevel31:
                requested_level.level = V4L2_MPEG_VIDC_VIDEO_HEVC_LEVEL_MAIN_TIER_LEVEL_3_1;
                break;
            case OMX_VIDEO_HEVCHighTierLevel31:
                requested_level.level = V4L2_MPEG_VIDC_VIDEO_HEVC_LEVEL_HIGH_TIER_LEVEL_3_1;
                break;
            case OMX_VIDEO_HEVCMainTierLevel4:
                requested_level.level = V4L2_MPEG_VIDC_VIDEO_HEVC_LEVEL_MAIN_TIER_LEVEL_4;
                break;
            case OMX_VIDEO_HEVCHighTierLevel4:
                requested_level.level = V4L2_MPEG_VIDC_VIDEO_HEVC_LEVEL_HIGH_TIER_LEVEL_4;
                break;
            case OMX_VIDEO_HEVCMainTierLevel41:
                requested_level.level = V4L2_MPEG_VIDC_VIDEO_HEVC_LEVEL_MAIN_TIER_LEVEL_4_1;
                break;
            case OMX_VIDEO_HEVCHighTierLevel41:
                requested_level.level = V4L2_MPEG_VIDC_VIDEO_HEVC_LEVEL_HIGH_TIER_LEVEL_4_1;
                break;
            case OMX_VIDEO_HEVCMainTierLevel5:
                requested_level.level = V4L2_MPEG_VIDC_VIDEO_HEVC_LEVEL_MAIN_TIER_LEVEL_5;
                break;
            case OMX_VIDEO_HEVCHighTierLevel5:
                requested_level.level = V4L2_MPEG_VIDC_VIDEO_HEVC_LEVEL_HIGH_TIER_LEVEL_5;
                break;
            case OMX_VIDEO_HEVCMainTierLevel51:
                requested_level.level = V4L2_MPEG_VIDC_VIDEO_HEVC_LEVEL_MAIN_TIER_LEVEL_5_1;
                break;
            case OMX_VIDEO_HEVCHighTierLevel51:
                requested_level.level = V4L2_MPEG_VIDC_VIDEO_HEVC_LEVEL_HIGH_TIER_LEVEL_5_1;
                break;
            case OMX_VIDEO_HEVCMainTierLevel52:
                requested_level.level = V4L2_MPEG_VIDC_VIDEO_HEVC_LEVEL_MAIN_TIER_LEVEL_5_2;
                break;
            case OMX_VIDEO_HEVCHighTierLevel52:
                requested_level.level = V4L2_MPEG_VIDC_VIDEO_HEVC_LEVEL_HIGH_TIER_LEVEL_5_2;
                break;
            case OMX_VIDEO_HEVCMainTierLevel6:
                requested_level.level = V4L2_MPEG_VIDC_VIDEO_HEVC_LEVEL_MAIN_TIER_LEVEL_6;
                break;
            case OMX_VIDEO_HEVCHighTierLevel6:
                requested_level.level = V4L2_MPEG_VIDC_VIDEO_HEVC_LEVEL_HIGH_TIER_LEVEL_6;
                break;
            case OMX_VIDEO_HEVCMainTierLevel61:
                requested_level.level = V4L2_MPEG_VIDC_VIDEO_HEVC_LEVEL_MAIN_TIER_LEVEL_6_1;
                break;
            case OMX_VIDEO_HEVCHighTierLevel61:
                requested_level.level = V4L2_MPEG_VIDC_VIDEO_HEVC_LEVEL_HIGH_TIER_LEVEL_6_1;
                break;
            default :
                DEBUG_PRINT_ERROR("ERROR: Unsupported HEVC level= %lu",
                        requested_level.level);
                return false;
        }
    }

    if (!m_profile_set) {
        int rc;
        struct v4l2_control control;

        if (m_sVenc_cfg.codectype == V4L2_PIX_FMT_H264) {
            control.id = V4L2_CID_MPEG_VIDEO_H264_PROFILE;
        } else if (m_sVenc_cfg.codectype == V4L2_PIX_FMT_MPEG4) {
            control.id = V4L2_CID_MPEG_VIDEO_MPEG4_PROFILE;
        } else if (m_sVenc_cfg.codectype == V4L2_PIX_FMT_H263) {
            control.id = V4L2_CID_MPEG_VIDC_VIDEO_H263_PROFILE;
        } else if (m_sVenc_cfg.codectype == V4L2_PIX_FMT_HEVC) {
            control.id = V4L2_CID_MPEG_VIDC_VIDEO_HEVC_PROFILE;
        } else {
            DEBUG_PRINT_ERROR("Wrong CODEC");
            return false;
        }

        control.value = requested_profile.profile;

        DEBUG_PRINT_LOW("Calling IOCTL set control for id=%d, val=%d", control.id, control.value);
        rc = ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control);

        if (rc) {
            DEBUG_PRINT_ERROR("Failed to set control");
            return false;
        }

        DEBUG_PRINT_LOW("Success IOCTL set control for id=%d, value=%d", control.id, control.value);

        codec_profile.profile = control.value;
        m_profile_set = true;
    }

    if (!m_level_set) {
        int rc;
        struct v4l2_control control;

        if (m_sVenc_cfg.codectype == V4L2_PIX_FMT_H264) {
            control.id = V4L2_CID_MPEG_VIDEO_H264_LEVEL;
        } else if (m_sVenc_cfg.codectype == V4L2_PIX_FMT_MPEG4) {
            control.id = V4L2_CID_MPEG_VIDEO_MPEG4_LEVEL;
        } else if (m_sVenc_cfg.codectype == V4L2_PIX_FMT_H263) {
            control.id = V4L2_CID_MPEG_VIDC_VIDEO_H263_LEVEL;
        } else if (m_sVenc_cfg.codectype == V4L2_PIX_FMT_VP8) {
            control.id = V4L2_CID_MPEG_VIDC_VIDEO_VP8_PROFILE_LEVEL;
        } else if (m_sVenc_cfg.codectype == V4L2_PIX_FMT_HEVC) {
            control.id = V4L2_CID_MPEG_VIDC_VIDEO_HEVC_TIER_LEVEL;
        } else {
            DEBUG_PRINT_ERROR("Wrong CODEC");
            return false;
        }

        control.value = requested_level.level;

        DEBUG_PRINT_LOW("Calling IOCTL set control for id=%d, val=%d", control.id, control.value);
        rc = ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control);

        if (rc) {
            DEBUG_PRINT_ERROR("Failed to set control");
            return false;
        }

        DEBUG_PRINT_LOW("Success IOCTL set control for id=%d, value=%d", control.id, control.value);

        profile_level.level = control.value;
        m_level_set = true;
    }

    return true;
}

bool venc_dev::venc_set_voptiming_cfg( OMX_U32 TimeIncRes)
{

    struct venc_voptimingcfg vop_timing_cfg;

    DEBUG_PRINT_LOW("venc_set_voptiming_cfg: TimeRes = %u",
            (unsigned int)TimeIncRes);

    vop_timing_cfg.voptime_resolution = TimeIncRes;

    voptimecfg.voptime_resolution = vop_timing_cfg.voptime_resolution;
    return true;
}

bool venc_dev::venc_set_intra_period(OMX_U32 nPFrames, OMX_U32 nBFrames)
{

    DEBUG_PRINT_LOW("venc_set_intra_period: nPFrames = %u, nBFrames: %u", (unsigned int)nPFrames, (unsigned int)nBFrames);
    int rc;
    struct v4l2_control control;
    int pframe = 0, bframe = 0;

    if ((codec_profile.profile != V4L2_MPEG_VIDEO_MPEG4_PROFILE_ADVANCED_SIMPLE) &&
            (codec_profile.profile != V4L2_MPEG_VIDEO_H264_PROFILE_MAIN) &&
            (codec_profile.profile != V4L2_MPEG_VIDC_VIDEO_HEVC_PROFILE_MAIN) &&
            (codec_profile.profile != V4L2_MPEG_VIDC_VIDEO_HEVC_PROFILE_MAIN10) &&
            (codec_profile.profile != V4L2_MPEG_VIDEO_H264_PROFILE_HIGH)) {
        nBFrames=0;
    }

    if (!venc_validate_hybridhp_params(0, nBFrames, 0, 0) && !is_thulium_v1) {
        DEBUG_PRINT_ERROR("Invalid settings, bframes cannot be enabled with HybridHP");
        return false;
    }

    intra_period.num_pframes = nPFrames;
    intra_period.num_bframes = nBFrames;

    if (!venc_calibrate_gop() && !is_thulium_v1)
    {
        DEBUG_PRINT_ERROR("Invalid settings, Hybrid HP enabled with LTR OR Hier-pLayers OR bframes");
        return false;
    }

    control.id = V4L2_CID_MPEG_VIDC_VIDEO_NUM_P_FRAMES;
    control.value = intra_period.num_pframes;
    rc = ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control);

    if (rc) {
        DEBUG_PRINT_ERROR("Failed to set control");
        return false;
    }

    DEBUG_PRINT_LOW("Success IOCTL set control for id=%d, value=%d", control.id, control.value);

    control.id = V4L2_CID_MPEG_VIDC_VIDEO_NUM_B_FRAMES;
    control.value = intra_period.num_bframes;
    DEBUG_PRINT_LOW("Calling IOCTL set control for id=%d, val=%d", control.id, control.value);
    rc = ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control);

    if (rc) {
        DEBUG_PRINT_ERROR("Failed to set control");
        return false;
    }

    DEBUG_PRINT_LOW("Success IOCTL set control for id=%d, value=%lu", control.id, intra_period.num_bframes);

    if (m_sVenc_cfg.codectype == V4L2_PIX_FMT_H264 ||
        m_sVenc_cfg.codectype == V4L2_PIX_FMT_HEVC) {
        control.id = V4L2_CID_MPEG_VIDC_VIDEO_IDR_PERIOD;
        control.value = 1;

        rc = ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control);

        if (rc) {
            DEBUG_PRINT_ERROR("Failed to set control");
            return false;
        }
        idrperiod.idrperiod = 1;
    }
    return true;
}

bool venc_dev::venc_set_idr_period(OMX_U32 nPFrames, OMX_U32 nIDRPeriod)
{
    int rc = 0;
    struct v4l2_control control;
    DEBUG_PRINT_LOW("venc_set_idr_period: nPFrames = %u, nIDRPeriod: %u",
            (unsigned int)nPFrames, (unsigned int)nIDRPeriod);

    if (m_sVenc_cfg.codectype != V4L2_PIX_FMT_H264) {
        DEBUG_PRINT_ERROR("ERROR: IDR period valid for H264 only!!");
        return false;
    }

    if (venc_set_intra_period (nPFrames, intra_period.num_bframes) == false) {
        DEBUG_PRINT_ERROR("ERROR: Request for setting intra period failed");
        return false;
    }

    if (!intra_period.num_bframes)
        intra_period.num_pframes = nPFrames;
    control.id = V4L2_CID_MPEG_VIDC_VIDEO_IDR_PERIOD;
    control.value = nIDRPeriod;

    rc = ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control);

    if (rc) {
        DEBUG_PRINT_ERROR("Failed to set control");
        return false;
    }

    idrperiod.idrperiod = nIDRPeriod;
    return true;
}

bool venc_dev::venc_set_entropy_config(OMX_BOOL enable, OMX_U32 i_cabac_level)
{
    int rc = 0;
    struct v4l2_control control;

    DEBUG_PRINT_LOW("venc_set_entropy_config: CABAC = %u level: %u", enable, (unsigned int)i_cabac_level);

    if (enable && (codec_profile.profile != V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE) &&
            (codec_profile.profile != V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_BASELINE)) {

        control.value = V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CABAC;
        control.id = V4L2_CID_MPEG_VIDEO_H264_ENTROPY_MODE;

        DEBUG_PRINT_LOW("Calling IOCTL set control for id=%d, val=%d", control.id, control.value);
        rc = ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control);

        if (rc) {
            DEBUG_PRINT_ERROR("Failed to set control");
            return false;
        }

        DEBUG_PRINT_LOW("Success IOCTL set control for id=%d, value=%d", control.id, control.value);
        entropy.longentropysel = control.value;

        if (i_cabac_level == 0) {
            control.value = V4L2_CID_MPEG_VIDC_VIDEO_H264_CABAC_MODEL_0;
        } else if (i_cabac_level == 1) {
            control.value = V4L2_CID_MPEG_VIDC_VIDEO_H264_CABAC_MODEL_1;
        } else if (i_cabac_level == 2) {
            control.value = V4L2_CID_MPEG_VIDC_VIDEO_H264_CABAC_MODEL_2;
        }

        control.id = V4L2_CID_MPEG_VIDC_VIDEO_H264_CABAC_MODEL;
        //control.value = entropy_cfg.cabacmodel;
        DEBUG_PRINT_LOW("Calling IOCTL set control for id=%d, val=%d", control.id, control.value);
        rc = ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control);

        if (rc) {
            DEBUG_PRINT_ERROR("Failed to set control");
            return false;
        }

        DEBUG_PRINT_LOW("Success IOCTL set control for id=%d, value=%d", control.id, control.value);
        entropy.cabacmodel=control.value;
    } else if (!enable) {
        control.value =  V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CAVLC;
        control.id = V4L2_CID_MPEG_VIDEO_H264_ENTROPY_MODE;
        DEBUG_PRINT_LOW("Calling IOCTL set control for id=%d, val=%d", control.id, control.value);
        rc = ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control);

        if (rc) {
            DEBUG_PRINT_ERROR("Failed to set control");
            return false;
        }

        DEBUG_PRINT_LOW("Success IOCTL set control for id=%d, value=%d", control.id, control.value);
        entropy.longentropysel=control.value;
    } else {
        DEBUG_PRINT_ERROR("Invalid Entropy mode for Baseline Profile");
        return false;
    }

    return true;
}

bool venc_dev::venc_set_multislice_cfg(OMX_INDEXTYPE Codec, OMX_U32 nSlicesize) // MB
{
    int rc;
    struct v4l2_control control;
    bool status = true;

    if ((Codec != OMX_IndexParamVideoH263)  && (nSlicesize)) {
        control.value =  V4L2_MPEG_VIDEO_MULTI_SICE_MODE_MAX_MB;
    } else {
        control.value =  V4L2_MPEG_VIDEO_MULTI_SLICE_MODE_SINGLE;
    }

    control.id = V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MODE;
    DEBUG_PRINT_LOW("Calling IOCTL set control for id=%d, val=%d", control.id, control.value);
    rc = ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control);

    if (rc) {
        DEBUG_PRINT_ERROR("Failed to set control");
        return false;
    }

    DEBUG_PRINT_LOW("Success IOCTL set control for id=%d, value=%d", control.id, control.value);
    multislice.mslice_mode=control.value;

    if (multislice.mslice_mode!=V4L2_MPEG_VIDEO_MULTI_SLICE_MODE_SINGLE) {

        control.id = V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MAX_MB;
        control.value = nSlicesize;
        DEBUG_PRINT_LOW("Calling SLICE_MB IOCTL set control for id=%d, val=%d", control.id, control.value);
        rc = ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control);

        if (rc) {
            DEBUG_PRINT_ERROR("Failed to set control");
            return false;
        }

        DEBUG_PRINT_LOW("Success IOCTL set control for id=%d, value=%d", control.id, control.value);
        multislice.mslice_size=control.value;

    }

    return status;
}

bool venc_dev::venc_set_intra_refresh(OMX_VIDEO_INTRAREFRESHTYPE ir_mode, OMX_U32 irMBs)
{
    bool status = true;
    int rc;
    struct v4l2_control control_mode,control_mbs;
    control_mode.id = V4L2_CID_MPEG_VIDC_VIDEO_INTRA_REFRESH_MODE;

    // There is no disabled mode.  Disabled mode is indicated by a 0 count.
    if (irMBs == 0 || ir_mode == OMX_VIDEO_IntraRefreshMax) {
        control_mode.value = V4L2_CID_MPEG_VIDC_VIDEO_INTRA_REFRESH_NONE;
        return status;
    } else if ((ir_mode == OMX_VIDEO_IntraRefreshCyclic) &&
            (irMBs < ((m_sVenc_cfg.dvs_width * m_sVenc_cfg.dvs_height)>>8))) {
        control_mode.value = V4L2_CID_MPEG_VIDC_VIDEO_INTRA_REFRESH_CYCLIC;
        control_mbs.id=V4L2_CID_MPEG_VIDC_VIDEO_CIR_MBS;
        control_mbs.value=irMBs;
    } else if ((ir_mode == OMX_VIDEO_IntraRefreshAdaptive) &&
            (irMBs < ((m_sVenc_cfg.dvs_width * m_sVenc_cfg.dvs_height)>>8))) {
        control_mode.value = V4L2_CID_MPEG_VIDC_VIDEO_INTRA_REFRESH_ADAPTIVE;
        control_mbs.id=V4L2_CID_MPEG_VIDC_VIDEO_AIR_MBS;
        control_mbs.value=irMBs;
    } else if ((ir_mode == OMX_VIDEO_IntraRefreshBoth) &&
            (irMBs < ((m_sVenc_cfg.dvs_width * m_sVenc_cfg.dvs_height)>>8))) {
        control_mode.value = V4L2_CID_MPEG_VIDC_VIDEO_INTRA_REFRESH_CYCLIC_ADAPTIVE;
    } else if ((ir_mode == OMX_VIDEO_IntraRefreshRandom) &&
            (irMBs < ((m_sVenc_cfg.dvs_width * m_sVenc_cfg.dvs_height)>>8))) {
        control_mode.value = V4L2_CID_MPEG_VIDC_VIDEO_INTRA_REFRESH_RANDOM;
        control_mbs.id = V4L2_CID_MPEG_VIDC_VIDEO_AIR_MBS;
        control_mbs.value = irMBs;
    } else {
        DEBUG_PRINT_ERROR("ERROR: Invalid IntraRefresh Parameters:"
                "mb count: %u, mb mode:%d", (unsigned int)irMBs, ir_mode);
        return false;
    }

    DEBUG_PRINT_LOW("Calling IOCTL set control for id=%u, val=%d", control_mode.id, control_mode.value);
    rc = ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control_mode);

    if (rc) {
        DEBUG_PRINT_ERROR("Failed to set control");
        return false;
    }

    DEBUG_PRINT_LOW("Success IOCTL set control for id=%d, value=%d", control_mode.id, control_mode.value);

    DEBUG_PRINT_LOW("Calling IOCTL set control for id=%d, val=%d", control_mbs.id, control_mbs.value);
    rc = ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control_mbs);

    if (rc) {
        DEBUG_PRINT_ERROR("Failed to set control");
        return false;
    }

    DEBUG_PRINT_LOW("Success IOCTL set control for id=%d, value=%d", control_mbs.id, control_mbs.value);

    intra_refresh.irmode = control_mode.value;
    intra_refresh.mbcount = control_mbs.value;

    return status;
}

bool venc_dev::venc_set_error_resilience(OMX_VIDEO_PARAM_ERRORCORRECTIONTYPE* error_resilience)
{
    bool status = true;
    struct venc_headerextension hec_cfg;
    struct venc_multiclicecfg multislice_cfg;
    int rc;
    OMX_U32 resynchMarkerSpacingBytes = 0;
    struct v4l2_control control;

    memset(&control, 0, sizeof(control));

    if (m_sVenc_cfg.codectype == V4L2_PIX_FMT_MPEG4) {
        if (error_resilience->bEnableHEC) {
            hec_cfg.header_extension = 1;
        } else {
            hec_cfg.header_extension = 0;
        }

        hec.header_extension = error_resilience->bEnableHEC;
    }

    if (error_resilience->bEnableRVLC) {
        DEBUG_PRINT_ERROR("RVLC is not Supported");
        return false;
    }

    if (( m_sVenc_cfg.codectype != V4L2_PIX_FMT_H263) &&
            (error_resilience->bEnableDataPartitioning)) {
        DEBUG_PRINT_ERROR("DataPartioning are not Supported for MPEG4/H264");
        return false;
    }

    if (error_resilience->nResynchMarkerSpacing) {
        resynchMarkerSpacingBytes = error_resilience->nResynchMarkerSpacing;
        resynchMarkerSpacingBytes = ALIGN(resynchMarkerSpacingBytes, 8) >> 3;
    }
    if (( m_sVenc_cfg.codectype != V4L2_PIX_FMT_H263) &&
            (error_resilience->nResynchMarkerSpacing)) {
        multislice_cfg.mslice_mode = VEN_MSLICE_CNT_BYTE;
        multislice_cfg.mslice_size = resynchMarkerSpacingBytes;
        control.id = V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MODE;
        control.value = V4L2_MPEG_VIDEO_MULTI_SICE_MODE_MAX_BYTES;
    } else if (m_sVenc_cfg.codectype == V4L2_PIX_FMT_H263 &&
            error_resilience->bEnableDataPartitioning) {
        multislice_cfg.mslice_mode = VEN_MSLICE_GOB;
        multislice_cfg.mslice_size = resynchMarkerSpacingBytes;
        control.id = V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MODE;
        control.value = V4L2_MPEG_VIDEO_MULTI_SLICE_GOB;
    } else {
        multislice_cfg.mslice_mode = VEN_MSLICE_OFF;
        multislice_cfg.mslice_size = 0;
        control.id = V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MODE;
        control.value =  V4L2_MPEG_VIDEO_MULTI_SLICE_MODE_SINGLE;
    }

    DEBUG_PRINT_LOW("%s(): mode = %lu, size = %lu", __func__,
            multislice_cfg.mslice_mode, multislice_cfg.mslice_size);
    DEBUG_PRINT_ERROR("Calling IOCTL set control for id=%x, val=%d", control.id, control.value);
    rc = ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control);

    if (rc) {
       DEBUG_PRINT_ERROR("Failed to set Slice mode control");
        return false;
    }

    DEBUG_PRINT_ERROR("Success IOCTL set control for id=%x, value=%d", control.id, control.value);
    multislice.mslice_mode=control.value;

    control.id = V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MAX_BYTES;
    control.value = resynchMarkerSpacingBytes;
    DEBUG_PRINT_ERROR("Calling IOCTL set control for id=%x, val=%d", control.id, control.value);

    rc = ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control);

    if (rc) {
       DEBUG_PRINT_ERROR("Failed to set MAX MB control");
        return false;
    }

    DEBUG_PRINT_ERROR("Success IOCTL set control for id=%x, value=%d", control.id, control.value);
    multislice.mslice_mode = multislice_cfg.mslice_mode;
    multislice.mslice_size = multislice_cfg.mslice_size;
    return status;
}

bool venc_dev::venc_set_inloop_filter(OMX_VIDEO_AVCLOOPFILTERTYPE loopfilter)
{
    int rc;
    struct v4l2_control control;
    control.id=V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_MODE;

    if (loopfilter == OMX_VIDEO_AVCLoopFilterEnable) {
        control.value=V4L2_MPEG_VIDEO_H264_LOOP_FILTER_MODE_ENABLED;
    } else if (loopfilter == OMX_VIDEO_AVCLoopFilterDisable) {
        control.value=V4L2_MPEG_VIDEO_H264_LOOP_FILTER_MODE_DISABLED;
    } else if (loopfilter == OMX_VIDEO_AVCLoopFilterDisableSliceBoundary) {
        control.value=V4L2_MPEG_VIDEO_H264_LOOP_FILTER_MODE_DISABLED_AT_SLICE_BOUNDARY;
    }

    DEBUG_PRINT_LOW("Calling IOCTL set control for id=%d, val=%d", control.id, control.value);
    rc = ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control);

    if (rc) {
        return false;
    }

    DEBUG_PRINT_LOW("Success IOCTL set control for id=%d, value=%d", control.id, control.value);

    dbkfilter.db_mode=control.value;

    control.id=V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_ALPHA;
    control.value=0;

    DEBUG_PRINT_LOW("Calling IOCTL set control for id=%d, val=%d", control.id, control.value);
    rc = ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control);

    if (rc) {
        return false;
    }

    DEBUG_PRINT_LOW("Success IOCTL set control for id=%d, value=%d", control.id, control.value);
    control.id=V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_BETA;
    control.value=0;
    DEBUG_PRINT_LOW("Calling IOCTL set control for id=%d, val=%d", control.id, control.value);
    rc = ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control);

    if (rc) {
        return false;
    }

    DEBUG_PRINT_LOW("Success IOCTL set control for id=%d, value=%d", control.id, control.value);


    dbkfilter.slicealpha_offset = dbkfilter.slicebeta_offset = 0;
    return true;
}

bool venc_dev::venc_set_target_bitrate(OMX_U32 nTargetBitrate, OMX_U32 config)
{
    DEBUG_PRINT_LOW("venc_set_target_bitrate: bitrate = %u",
            (unsigned int)nTargetBitrate);
    struct v4l2_control control;
    int rc = 0;
    control.id = V4L2_CID_MPEG_VIDEO_BITRATE;
    control.value = nTargetBitrate;

    DEBUG_PRINT_LOW("Calling IOCTL set control for id=%d, val=%d", control.id, control.value);
    rc = ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control);

    if (rc) {
        DEBUG_PRINT_ERROR("Failed to set control");
        return false;
    }

    DEBUG_PRINT_LOW("Success IOCTL set control for id=%d, value=%d", control.id, control.value);


    m_sVenc_cfg.targetbitrate = control.value;
    bitrate.target_bitrate = control.value;

    if (!config) {
        m_level_set = false;

        if (venc_set_profile_level(0, 0)) {
            DEBUG_PRINT_HIGH("Calling set level (Bitrate) with %lu",profile_level.level);
        }
    }

    return true;
}

bool venc_dev::venc_set_encode_framerate(OMX_U32 encode_framerate, OMX_U32 config)
{
    struct v4l2_streamparm parm;
    int rc = 0;
    struct venc_framerate frame_rate_cfg;
    Q16ToFraction(encode_framerate,frame_rate_cfg.fps_numerator,frame_rate_cfg.fps_denominator);
    parm.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    parm.parm.output.timeperframe.numerator = frame_rate_cfg.fps_denominator;
    parm.parm.output.timeperframe.denominator = frame_rate_cfg.fps_numerator;

    if (frame_rate_cfg.fps_numerator > 0)
        rc = ioctl(m_nDriver_fd, VIDIOC_S_PARM, &parm);

    if (rc) {
        DEBUG_PRINT_ERROR("ERROR: Request for setting framerate failed");
        return false;
    }

    m_sVenc_cfg.fps_den = frame_rate_cfg.fps_denominator;
    m_sVenc_cfg.fps_num = frame_rate_cfg.fps_numerator;

    if (!config) {
        m_level_set = false;

        if (venc_set_profile_level(0, 0)) {
            DEBUG_PRINT_HIGH("Calling set level (Framerate) with %lu",profile_level.level);
        }
    }

    return true;
}

bool venc_dev::venc_set_color_format(OMX_COLOR_FORMATTYPE color_format)
{
    struct v4l2_format fmt;
    DEBUG_PRINT_LOW("venc_set_color_format: color_format = %u ", color_format);

    switch ((int)color_format) {
        case OMX_COLOR_FormatYUV420SemiPlanar:
        case QOMX_COLOR_FORMATYUV420PackedSemiPlanar32m:
            m_sVenc_cfg.inputformat = V4L2_PIX_FMT_NV12;
            break;
        case QOMX_COLOR_FormatYVU420SemiPlanar:
            m_sVenc_cfg.inputformat = V4L2_PIX_FMT_NV21;
            break;
        case QOMX_COLOR_FORMATYUV420PackedSemiPlanar32mCompressed:
            m_sVenc_cfg.inputformat = V4L2_PIX_FMT_NV12_UBWC;
            break;
        case QOMX_COLOR_Format32bitRGBA8888:
            m_sVenc_cfg.inputformat = V4L2_PIX_FMT_RGB32;
            break;
        case QOMX_COLOR_Format32bitRGBA8888Compressed:
            m_sVenc_cfg.inputformat = V4L2_PIX_FMT_RGBA8888_UBWC;
            break;
        default:
            DEBUG_PRINT_HIGH("WARNING: Unsupported Color format [%d]", color_format);
            m_sVenc_cfg.inputformat = V4L2_DEFAULT_OUTPUT_COLOR_FMT;
            DEBUG_PRINT_HIGH("Default color format NV12 UBWC is set");
            break;
    }

    fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    fmt.fmt.pix_mp.pixelformat = m_sVenc_cfg.inputformat;
    fmt.fmt.pix_mp.height = m_sVenc_cfg.input_height;
    fmt.fmt.pix_mp.width = m_sVenc_cfg.input_width;

    if (ioctl(m_nDriver_fd, VIDIOC_S_FMT, &fmt)) {
        DEBUG_PRINT_ERROR("Failed setting color format %x", color_format);
        return false;
    }

    return true;
}

bool venc_dev::venc_set_intra_vop_refresh(OMX_BOOL intra_vop_refresh)
{
    DEBUG_PRINT_LOW("venc_set_intra_vop_refresh: intra_vop = %uc", intra_vop_refresh);

    if (intra_vop_refresh == OMX_TRUE) {
        struct v4l2_control control;
        int rc;
        control.id = V4L2_CID_MPEG_VIDC_VIDEO_REQUEST_IFRAME;
        control.value = 1;
       DEBUG_PRINT_ERROR("Calling IOCTL set control for id=%x, val=%d", control.id, control.value);
        rc = ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control);

        if (rc) {
           DEBUG_PRINT_ERROR("Failed to set Intra Frame Request control");
            return false;
        }

       DEBUG_PRINT_ERROR("Success IOCTL set control for id=%x, value=%d", control.id, control.value);
    } else {
        DEBUG_PRINT_ERROR("ERROR: VOP Refresh is False, no effect");
    }

    return true;
}

bool venc_dev::venc_set_deinterlace(OMX_U32 enable)
{
    DEBUG_PRINT_LOW("venc_set_deinterlace: enable = %u", (unsigned int)enable);
    struct v4l2_control control;
    int rc;
    control.id = V4L2_CID_MPEG_VIDC_VIDEO_DEINTERLACE;
    if (enable)
        control.value = V4L2_CID_MPEG_VIDC_VIDEO_DEINTERLACE_ENABLED;
    else
        control.value = V4L2_CID_MPEG_VIDC_VIDEO_DEINTERLACE_ENABLED;

    DEBUG_PRINT_LOW("Calling IOCTL set control for id=%x, val=%d", control.id, control.value);
    rc = ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control);
    if (rc) {
        DEBUG_PRINT_ERROR("Failed to set Deinterlcing control");
        return false;
    }
    DEBUG_PRINT_LOW("Success IOCTL set control for id=%x, value=%d", control.id, control.value);
    deinterlace_enabled = true;
    return true;
}

bool venc_dev::venc_calibrate_gop()
{
    int ratio, sub_gop_size, gop_size, nPframes, nBframes, nLayers;
    int num_sub_gops_in_a_gop;
    nPframes = intra_period.num_pframes;
    nBframes = intra_period.num_bframes;
    nLayers = hier_layers.numlayers;

    if (!nPframes && nLayers) {
        DEBUG_PRINT_ERROR("nPframes should be non-zero when nLayers are present\n");
        return false;
    }

    if (nLayers > 1) { /*Multi-layer encoding*/
        sub_gop_size = 1 << (nLayers - 1);
        /* Actual GOP definition is nPframes + nBframes + 1 but for the sake of
         * below calculations we are ignoring +1 . Ignoring +1 in below
         * calculations is not a mistake but intentional.
         */
        gop_size = MAX(sub_gop_size, ROUND(nPframes + nBframes, sub_gop_size));
        num_sub_gops_in_a_gop = gop_size/sub_gop_size;
        if (nBframes) { /*Hier-B case*/
        /*
            * Frame Type--> I  B  B  B  P  B  B  B  P  I  B  B  P ...
            * Layer -->     0  2  1  2  0  2  1  2  0  0  2  1  2 ...
            * nPframes = 2, nBframes = 6, nLayers = 3
            *
            * Intention is to keep the intraperiod as close as possible to what is desired
            * by the client while adjusting nPframes and nBframes to meet other constraints.
            * eg1: Input by client: nPframes =  9, nBframes = 14, nLayers = 2
            *    Output of this fn: nPframes = 12, nBframes = 12, nLayers = 2
            *
            * eg2: Input by client: nPframes = 9, nBframes = 4, nLayers = 2
            *    Output of this fn: nPframes = 7, nBframes = 7, nLayers = 2
            */
            nPframes = num_sub_gops_in_a_gop;
            nBframes = gop_size - nPframes;
        } else { /*Hier-P case*/
            /*
            * Frame Type--> I  P  P  P  P  P  P  P  I  P  P  P  P ...
            * Layer-->      0  2  1  2  0  2  1  2  0  2  1  2  0 ...
            * nPframes =  7, nBframes = 0, nLayers = 3
            *
            * Intention is to keep the intraperiod as close as possible to what is desired
            * by the client while adjusting nPframes and nBframes to meet other constraints.
            * eg1: Input by client: nPframes = 9, nBframes = 0, nLayers = 3
            *    Output of this fn: nPframes = 7, nBframes = 0, nLayers = 3
            *
            * eg2: Input by client: nPframes = 10, nBframes = 0, nLayers = 3
            *     Output of this fn:nPframes = 12, nBframes = 0, nLayers = 3
            */
            nPframes = gop_size - 1;
        }
    } else { /*Single-layer encoding*/
        if (nBframes) {
            /* I  P  B  B  B  P  B  B  B   P   B   B   B   I   P   B   B...
            *  1  2  3  4  5  6  7  8  9  10  11  12  13  14  15  16  17...
            * nPframes = 3, nBframes = 9, nLayers = 0
            *
            * ratio is rounded,
            * eg1: nPframes = 9, nBframes = 11 => ratio = 1
            * eg2: nPframes = 9, nBframes = 16 => ratio = 2
            */
            ratio = MAX(1, MIN((nBframes + (nPframes >> 1))/nPframes, 3));
            nBframes = ratio * nPframes;
        }
    }
    DEBUG_PRINT_LOW("P/B Frames changed from: %ld/%ld to %d/%d",
        intra_period.num_pframes, intra_period.num_bframes, nPframes, nBframes);
    intra_period.num_pframes = nPframes;
    intra_period.num_bframes = nBframes;
    hier_layers.numlayers = nLayers;
    return true;
}

bool venc_dev::venc_set_bitrate_type(OMX_U32 type)
{
    struct v4l2_control control;
    int rc = 0;
    control.id = V4L2_CID_MPEG_VIDC_VIDEO_VENC_BITRATE_TYPE;
    control.value = type;
    DEBUG_PRINT_LOW("Set Bitrate type to %s for %d \n", bitrate_type_string(type), type);
    rc = ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control);
    if (rc) {
        DEBUG_PRINT_ERROR("Request to set Bitrate type to %s failed",
            bitrate_type_string(type));
        return false;
    }
    return true;
}

bool venc_dev::venc_set_layer_bitrates(QOMX_EXTNINDEX_VIDEO_HYBRID_HP_MODE* hpmode)
{
    DEBUG_PRINT_LOW("venc_set_layer_bitrates");
    struct v4l2_ext_control ctrl[MAX_HYB_HIERP_LAYERS];
    struct v4l2_ext_controls controls;
    int rc = 0;
    OMX_U32 i;

    if (!venc_set_bitrate_type(V4L2_CID_MPEG_VIDC_VIDEO_VENC_BITRATE_ENABLE)) {
        DEBUG_PRINT_ERROR("Failed to set layerwise bitrate type %d", rc);
        return false;
    }

    for (i = 0; i < hpmode->nHpLayers; i++) {
        if (!hpmode->nTemporalLayerBitrateRatio[i]) {
            DEBUG_PRINT_ERROR("invalid bitrate settings for layer %d\n", i);
            return false;
        } else {
            ctrl[i].id = V4L2_CID_MPEG_VIDC_VENC_PARAM_LAYER_BITRATE;
            ctrl[i].value = hpmode->nTemporalLayerBitrateRatio[i];
            hybrid_hp.nTemporalLayerBitrateRatio[i] =  hpmode->nTemporalLayerBitrateRatio[i];
        }
    }
    controls.count = hpmode->nHpLayers;
    controls.ctrl_class = V4L2_CTRL_CLASS_MPEG;
    controls.controls = ctrl;

    rc = ioctl(m_nDriver_fd, VIDIOC_S_EXT_CTRLS, &controls);
    if (rc) {
        DEBUG_PRINT_ERROR("Failed to set layerwise bitrate %d", rc);
        return false;
    }

    hybrid_hp.nHpLayers = hpmode->nHpLayers;

    DEBUG_PRINT_LOW("Success in setting Layer wise bitrate: %d, %d, %d, %d, %d, %d",
        hpmode->nTemporalLayerBitrateRatio[0],hpmode->nTemporalLayerBitrateRatio[1],
        hpmode->nTemporalLayerBitrateRatio[2],hpmode->nTemporalLayerBitrateRatio[3],
        hpmode->nTemporalLayerBitrateRatio[4],hpmode->nTemporalLayerBitrateRatio[5]);

    return true;
}

bool venc_dev::venc_set_hybrid_hierp(QOMX_EXTNINDEX_VIDEO_HYBRID_HP_MODE* hhp)
{
    DEBUG_PRINT_LOW("venc_set_hybrid_hierp layers");
    struct v4l2_control control;
    int rc;

    if (!venc_validate_hybridhp_params(hhp->nHpLayers, 0, 0, (int) HIER_P_HYBRID)) {
        DEBUG_PRINT_ERROR("Invalid settings, Hybrid HP enabled with LTR OR Hier-pLayers OR bframes");
        return false;
    }

    if (!hhp->nHpLayers || hhp->nHpLayers > MAX_HYB_HIERP_LAYERS) {
        DEBUG_PRINT_ERROR("Invalid numbers of layers set: %d (max supported is 6)", hhp->nHpLayers);
        return false;
    }
    if (!venc_set_intra_period(hhp->nKeyFrameInterval, 0)) {
       DEBUG_PRINT_ERROR("Failed to set Intraperiod: %d", hhp->nKeyFrameInterval);
       return false;
    }

    hier_layers.numlayers = hhp->nHpLayers;
    if (m_sVenc_cfg.codectype == V4L2_PIX_FMT_H264) {
        hier_layers.hier_mode = HIER_P_HYBRID;
    } else if (m_sVenc_cfg.codectype == V4L2_PIX_FMT_HEVC) {
        hier_layers.hier_mode = HIER_P;
    }
    if (venc_calibrate_gop()) {
     // Update the driver with the new nPframes and nBframes
        control.id = V4L2_CID_MPEG_VIDC_VIDEO_NUM_P_FRAMES;
        control.value = intra_period.num_pframes;
        rc = ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control);
        if (rc) {
            DEBUG_PRINT_ERROR("Failed to set control");
            return false;
        }

        control.id = V4L2_CID_MPEG_VIDC_VIDEO_NUM_B_FRAMES;
        control.value = intra_period.num_bframes;
        rc = ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control);
        if (rc) {
            DEBUG_PRINT_ERROR("Failed to set control");
            return false;
        }
        DEBUG_PRINT_LOW("Updated nPframes (%ld) and nBframes (%ld)",
                         intra_period.num_pframes, intra_period.num_bframes);
    } else {
        DEBUG_PRINT_ERROR("Invalid settings, Hybrid HP enabled with LTR OR Hier-pLayers OR bframes");
        return false;
    }
    if (m_sVenc_cfg.codectype == V4L2_PIX_FMT_H264) {
        control.id = V4L2_CID_MPEG_VIDC_VIDEO_HYBRID_HIERP_MODE;
    } else if (m_sVenc_cfg.codectype == V4L2_PIX_FMT_HEVC) {
        control.id = V4L2_CID_MPEG_VIDC_VIDEO_HIER_P_NUM_LAYERS;
    }
    control.value = hhp->nHpLayers - 1;

    DEBUG_PRINT_LOW("Calling IOCTL set control for id=%x, val=%d",
                    control.id, control.value);

    rc = ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control);
    if (rc) {
        DEBUG_PRINT_ERROR("Failed to set hybrid hierp/hierp %d", rc);
        return false;
    }

    DEBUG_PRINT_LOW("SUCCESS IOCTL set control for id=%x, val=%d",
                    control.id, control.value);
    if (m_sVenc_cfg.codectype == V4L2_PIX_FMT_H264) {
        control.id = V4L2_CID_MPEG_VIDC_VIDEO_H264_NAL_SVC;
        control.value = V4L2_CID_MPEG_VIDC_VIDEO_H264_NAL_SVC_ENABLED;
        if (ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control)) {
            DEBUG_PRINT_ERROR("Failed to enable SVC_NAL");
            return false;
        }
    } else if (m_sVenc_cfg.codectype == V4L2_PIX_FMT_HEVC) {
        control.id = V4L2_CID_MPEG_VIDC_VIDEO_MAX_HIERP_LAYERS;
        control.value = hhp->nHpLayers - 1;
        if (ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control)) {
            DEBUG_PRINT_ERROR("Failed to enable SVC_NAL");
            return false;
        }
    } else {
        DEBUG_PRINT_ERROR("Failed : Unsupported codec for Hybrid Hier P : %d", m_sVenc_cfg.codectype);
        return false;
    }

    if(venc_set_session_qp_range (hhp->nMinQuantizer,
                hhp->nMaxQuantizer) == false) {
        DEBUG_PRINT_ERROR("ERROR: Setting QP Range for hybridHP [%u %u] failed",
            (unsigned int)hhp->nMinQuantizer, (unsigned int)hhp->nMaxQuantizer);
        return false;
    } else {
        session_qp_values.minqp = hhp->nMinQuantizer;
        session_qp_values.maxqp = hhp->nMaxQuantizer;
    }

    if (!venc_set_layer_bitrates(hhp)) {
       DEBUG_PRINT_ERROR("Failed to set Layer wise bitrate: %d, %d, %d, %d, %d, %d",
            hhp->nTemporalLayerBitrateRatio[0],hhp->nTemporalLayerBitrateRatio[1],
            hhp->nTemporalLayerBitrateRatio[2],hhp->nTemporalLayerBitrateRatio[3],
            hhp->nTemporalLayerBitrateRatio[4],hhp->nTemporalLayerBitrateRatio[5]);
       return false;
    }
    // Set this or else the layer0 bitrate will be overwritten by
    // default value in component
    m_sVenc_cfg.targetbitrate  = bitrate.target_bitrate = hhp->nTemporalLayerBitrateRatio[0];
    hybrid_hp.nHpLayers = hhp->nHpLayers;
    hybrid_hp.nKeyFrameInterval = hhp->nKeyFrameInterval;
    hybrid_hp.nMaxQuantizer = hhp->nMaxQuantizer;
    hybrid_hp.nMinQuantizer = hhp->nMinQuantizer;
    return true;
}

bool venc_dev::venc_set_ltrmode(OMX_U32 enable, OMX_U32 count)
{
    DEBUG_PRINT_LOW("venc_set_ltrmode: enable = %u", (unsigned int)enable);
    struct v4l2_ext_control ctrl[2];
    struct v4l2_ext_controls controls;
    int rc;

    if (!venc_validate_hybridhp_params(0, 0, count, 0)) {
        DEBUG_PRINT_ERROR("Invalid settings, LTR enabled with HybridHP");
        return false;
    }

    ctrl[0].id = V4L2_CID_MPEG_VIDC_VIDEO_LTRMODE;
    if (enable)
        ctrl[0].value = V4L2_MPEG_VIDC_VIDEO_LTR_MODE_MANUAL;
    else
        ctrl[0].value = V4L2_MPEG_VIDC_VIDEO_LTR_MODE_DISABLE;

    ctrl[1].id = V4L2_CID_MPEG_VIDC_VIDEO_LTRCOUNT;
    if (enable && count > 0)
        ctrl[1].value = count;
    else if (enable)
        ctrl[1].value = 1;
    else
        ctrl[1].value = 0;

    controls.count = 2;
    controls.ctrl_class = V4L2_CTRL_CLASS_MPEG;
    controls.controls = ctrl;

    DEBUG_PRINT_LOW("Calling IOCTL set control for id=%x, val=%d id=%x, val=%d",
                    controls.controls[0].id, controls.controls[0].value,
                    controls.controls[1].id, controls.controls[1].value);

    rc = ioctl(m_nDriver_fd, VIDIOC_S_EXT_CTRLS, &controls);
    if (rc) {
        DEBUG_PRINT_ERROR("Failed to set ltrmode %d", rc);
        return false;
    }
    ltrinfo.enabled = enable;
    ltrinfo.count = count;

    DEBUG_PRINT_LOW("Success IOCTL set control for id=%x, val=%d id=%x, val=%d",
                    controls.controls[0].id, controls.controls[0].value,
                    controls.controls[1].id, controls.controls[1].value);

    if (!venc_set_profile_level(0, 0)) {
        DEBUG_PRINT_ERROR("ERROR: %s(): Driver Profile/Level is NOT SET",
                __func__);
    } else {
        DEBUG_PRINT_HIGH("%s(): Driver Profile[%lu]/Level[%lu] successfully SET",
                __func__, codec_profile.profile, profile_level.level);
    }

    return true;
}

bool venc_dev::venc_set_useltr(OMX_U32 frameIdx)
{
    DEBUG_PRINT_LOW("venc_use_goldenframe");
    int rc = true;
    struct v4l2_control control;

    control.id = V4L2_CID_MPEG_VIDC_VIDEO_USELTRFRAME;
    control.value = frameIdx;

    rc = ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control);
    if (rc) {
        DEBUG_PRINT_ERROR("Failed to set use_ltr %d", rc);
        return false;
    }

    DEBUG_PRINT_LOW("Success IOCTL set control for id=%x, val=%d",
                    control.id, control.value);
    return true;
}

bool venc_dev::venc_set_markltr(OMX_U32 frameIdx)
{
    DEBUG_PRINT_LOW("venc_set_goldenframe");
    int rc = true;
    struct v4l2_control control;

    control.id = V4L2_CID_MPEG_VIDC_VIDEO_MARKLTRFRAME;
    control.value = frameIdx;

    rc = ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control);
    if (rc) {
        DEBUG_PRINT_ERROR("Failed to set ltrmode %d", rc);
        return false;
    }

    DEBUG_PRINT_LOW("Success IOCTL set control for id=%x, val=%d",
                    control.id, control.value);
    return true;
}

bool venc_dev::venc_set_vpe_rotation(OMX_S32 rotation_angle)
{
    DEBUG_PRINT_LOW("venc_set_vpe_rotation: rotation angle = %d", (int)rotation_angle);
    struct v4l2_control control;
    int rc;
    struct v4l2_format fmt;
    struct v4l2_requestbuffers bufreq;

    control.id = V4L2_CID_MPEG_VIDC_VIDEO_ROTATION;
    if (rotation_angle == 0)
        control.value = V4L2_CID_MPEG_VIDC_VIDEO_ROTATION_NONE;
    else if (rotation_angle == 90)
        control.value = V4L2_CID_MPEG_VIDC_VIDEO_ROTATION_90;
    else if (rotation_angle == 180)
        control.value = V4L2_CID_MPEG_VIDC_VIDEO_ROTATION_180;
    else if (rotation_angle == 270)
        control.value = V4L2_CID_MPEG_VIDC_VIDEO_ROTATION_270;
    else {
        DEBUG_PRINT_ERROR("Failed to find valid rotation angle");
        return false;
    }

    DEBUG_PRINT_LOW("Calling IOCTL set control for id=%x, val=%d", control.id, control.value);
    rc = ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control);
    if (rc) {
        DEBUG_PRINT_HIGH("Failed to set VPE Rotation control");
        return false;
    }
    DEBUG_PRINT_LOW("Success IOCTL set control for id=%x, value=%d", control.id, control.value);

    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    fmt.fmt.pix_mp.height = m_sVenc_cfg.dvs_height;
    fmt.fmt.pix_mp.width = m_sVenc_cfg.dvs_width;
    fmt.fmt.pix_mp.pixelformat = m_sVenc_cfg.codectype;
    if (ioctl(m_nDriver_fd, VIDIOC_S_FMT, &fmt)) {
        DEBUG_PRINT_ERROR("Failed to set format on capture port");
        return false;
    }

    m_sOutput_buff_property.datasize = fmt.fmt.pix_mp.plane_fmt[0].sizeimage;
    bufreq.memory = V4L2_MEMORY_USERPTR;
    bufreq.count = m_sOutput_buff_property.actualcount;
    bufreq.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (ioctl(m_nDriver_fd,VIDIOC_REQBUFS, &bufreq)) {
        DEBUG_PRINT_ERROR("ERROR: Request for o/p buffer count failed for rotation");
            return false;
    }
    if (bufreq.count >= m_sOutput_buff_property.mincount)
        m_sOutput_buff_property.actualcount = m_sOutput_buff_property.mincount = bufreq.count;

    return true;
}

bool venc_dev::venc_set_searchrange()
{
    DEBUG_PRINT_LOW("venc_set_searchrange");
    struct v4l2_control control;
    struct v4l2_ext_control ctrl[6];
    struct v4l2_ext_controls controls;
    int rc;

    if (m_sVenc_cfg.codectype == V4L2_PIX_FMT_MPEG4) {
        ctrl[0].id = V4L2_CID_MPEG_VIDC_VIDEO_IFRAME_X_RANGE;
        ctrl[0].value = 16;
        ctrl[1].id = V4L2_CID_MPEG_VIDC_VIDEO_IFRAME_Y_RANGE;
        ctrl[1].value = 4;
        ctrl[2].id = V4L2_CID_MPEG_VIDC_VIDEO_PFRAME_X_RANGE;
        ctrl[2].value = 16;
        ctrl[3].id = V4L2_CID_MPEG_VIDC_VIDEO_PFRAME_Y_RANGE;
        ctrl[3].value = 4;
        ctrl[4].id = V4L2_CID_MPEG_VIDC_VIDEO_BFRAME_X_RANGE;
        ctrl[4].value = 12;
        ctrl[5].id = V4L2_CID_MPEG_VIDC_VIDEO_BFRAME_Y_RANGE;
        ctrl[5].value = 4;
    } else if ((m_sVenc_cfg.codectype == V4L2_PIX_FMT_H264) ||
               (m_sVenc_cfg.codectype == V4L2_PIX_FMT_VP8)) {
        ctrl[0].id = V4L2_CID_MPEG_VIDC_VIDEO_IFRAME_X_RANGE;
        ctrl[0].value = 16;
        ctrl[1].id = V4L2_CID_MPEG_VIDC_VIDEO_IFRAME_Y_RANGE;
        ctrl[1].value = 4;
        ctrl[2].id = V4L2_CID_MPEG_VIDC_VIDEO_PFRAME_X_RANGE;
        ctrl[2].value = 16;
        ctrl[3].id = V4L2_CID_MPEG_VIDC_VIDEO_PFRAME_Y_RANGE;
        ctrl[3].value = 4;
        ctrl[4].id = V4L2_CID_MPEG_VIDC_VIDEO_BFRAME_X_RANGE;
        ctrl[4].value = 12;
        ctrl[5].id = V4L2_CID_MPEG_VIDC_VIDEO_BFRAME_Y_RANGE;
        ctrl[5].value = 4;
    } else if (m_sVenc_cfg.codectype == V4L2_PIX_FMT_H263) {
        ctrl[0].id = V4L2_CID_MPEG_VIDC_VIDEO_IFRAME_X_RANGE;
        ctrl[0].value = 4;
        ctrl[1].id = V4L2_CID_MPEG_VIDC_VIDEO_IFRAME_Y_RANGE;
        ctrl[1].value = 4;
        ctrl[2].id = V4L2_CID_MPEG_VIDC_VIDEO_PFRAME_X_RANGE;
        ctrl[2].value = 4;
        ctrl[3].id = V4L2_CID_MPEG_VIDC_VIDEO_PFRAME_Y_RANGE;
        ctrl[3].value = 4;
        ctrl[4].id = V4L2_CID_MPEG_VIDC_VIDEO_BFRAME_X_RANGE;
        ctrl[4].value = 4;
        ctrl[5].id = V4L2_CID_MPEG_VIDC_VIDEO_BFRAME_Y_RANGE;
        ctrl[5].value = 4;
    } else {
        DEBUG_PRINT_ERROR("Invalid codec type");
        return false;
    }
    controls.count = 6;
    controls.ctrl_class = V4L2_CTRL_CLASS_MPEG;
    controls.controls = ctrl;

    DEBUG_PRINT_LOW(" Calling IOCTL set control for"
        "id=%x, val=%d id=%x, val=%d"
        "id=%x, val=%d id=%x, val=%d"
        "id=%x, val=%d id=%x, val=%d",
        controls.controls[0].id, controls.controls[0].value,
        controls.controls[1].id, controls.controls[1].value,
        controls.controls[2].id, controls.controls[2].value,
        controls.controls[3].id, controls.controls[3].value,
        controls.controls[4].id, controls.controls[4].value,
        controls.controls[5].id, controls.controls[5].value);

    rc = ioctl(m_nDriver_fd, VIDIOC_S_EXT_CTRLS, &controls);
    if (rc) {
        DEBUG_PRINT_ERROR("Failed to set search range %d", rc);
        return false;
    }
    return true;
}

bool venc_dev::venc_set_ratectrl_cfg(OMX_VIDEO_CONTROLRATETYPE eControlRate)
{
    bool status = true;
    struct v4l2_control control;
    int rc = 0;
    control.id = V4L2_CID_MPEG_VIDC_VIDEO_RATE_CONTROL;

    switch (eControlRate) {
        case OMX_Video_ControlRateDisable:
            control.value = V4L2_CID_MPEG_VIDC_VIDEO_RATE_CONTROL_OFF;
            break;
        case OMX_Video_ControlRateVariableSkipFrames:
            (supported_rc_modes & RC_VBR_VFR) ?
                control.value = V4L2_CID_MPEG_VIDC_VIDEO_RATE_CONTROL_VBR_VFR :
                status = false;
            break;
        case OMX_Video_ControlRateVariable:
            (supported_rc_modes & RC_VBR_CFR) ?
                control.value = V4L2_CID_MPEG_VIDC_VIDEO_RATE_CONTROL_VBR_CFR :
                status = false;
            break;
        case OMX_Video_ControlRateConstantSkipFrames:
            (supported_rc_modes & RC_CBR_VFR) ?
                control.value = V4L2_CID_MPEG_VIDC_VIDEO_RATE_CONTROL_CBR_VFR :
                status = false;
            break;
        case OMX_Video_ControlRateConstant:
            (supported_rc_modes & RC_CBR_CFR) ?
                control.value = V4L2_CID_MPEG_VIDC_VIDEO_RATE_CONTROL_CBR_CFR :
                status = false;
            break;
        default:
            status = false;
            break;
    }

    if (status) {

        DEBUG_PRINT_LOW("Calling IOCTL set control for id=%d, val=%d", control.id, control.value);
        rc = ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control);

        if (rc) {
            DEBUG_PRINT_ERROR("Failed to set control");
            return false;
        }

        DEBUG_PRINT_LOW("Success IOCTL set control for id=%d, value=%d", control.id, control.value);

        rate_ctrl.rcmode = control.value;
    }

    return status;
}

bool venc_dev::venc_set_perf_level(QOMX_VIDEO_PERF_LEVEL ePerfLevel)
{
    bool status = true;
    struct v4l2_control control;
    int rc = 0;
    control.id = V4L2_CID_MPEG_VIDC_SET_PERF_LEVEL;

    switch (ePerfLevel) {
    case OMX_QCOM_PerfLevelNominal:
        control.value = V4L2_CID_MPEG_VIDC_PERF_LEVEL_NOMINAL;
        break;
    case OMX_QCOM_PerfLevelTurbo:
        control.value = V4L2_CID_MPEG_VIDC_PERF_LEVEL_TURBO;
        break;
    default:
        status = false;
        break;
    }

    if (status) {
        DEBUG_PRINT_LOW("Calling IOCTL set control for id=%d, val=%d", control.id, control.value);
        rc = ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control);

        if (rc) {
            DEBUG_PRINT_ERROR("Failed to set control for id=%d, val=%d", control.id, control.value);
            return false;
        }

        DEBUG_PRINT_LOW("Success IOCTL set control for id=%d, value=%d", control.id, control.value);
    }
    return status;
}

bool venc_dev::venc_set_perf_mode(OMX_U32 mode)
{
    struct v4l2_control control;
    if (mode && mode <= V4L2_MPEG_VIDC_VIDEO_PERF_POWER_SAVE) {
        control.id = V4L2_CID_MPEG_VIDC_VIDEO_PERF_MODE;
        control.value = mode;
        DEBUG_PRINT_LOW("Going to set V4L2_CID_MPEG_VIDC_VIDEO_PERF_MODE");
        if (ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control)) {
            DEBUG_PRINT_ERROR("Failed to set V4L2_CID_MPEG_VIDC_VIDEO_PERF_MODE");
            return false;
        }
        return true;
    } else {
        DEBUG_PRINT_ERROR("Invalid mode set for V4L2_CID_MPEG_VIDC_VIDEO_PERF_MODE: %d", mode);
        return false;
    }
}

bool venc_dev::venc_set_qp(OMX_U32 nQp)
{
    struct v4l2_control control;
    if (nQp) {
        control.id = V4L2_CID_MPEG_VIDC_VIDEO_CONFIG_QP;
        control.value = nQp;
        DEBUG_PRINT_LOW("Going to set V4L2_CID_MPEG_VIDC_VIDEO_CONFIG_QP");
        if (ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control)) {
            DEBUG_PRINT_ERROR("Failed to set V4L2_CID_MPEG_VIDC_VIDEO_CONFIG_QP");
            return false;
        }
    } else {
        DEBUG_PRINT_ERROR("Invalid qp set for V4L2_CID_MPEG_VIDC_VIDEO_CONFIG_QP: %d", nQp);
        return false;
    }
    return true;
}

bool venc_dev::venc_set_aspectratio(void *nSar)
{
    int rc;
    struct v4l2_control control;
    struct v4l2_ext_control ctrl[2];
    struct v4l2_ext_controls controls;
    QOMX_EXTNINDEX_VIDEO_VENC_SAR *sar;

    sar = (QOMX_EXTNINDEX_VIDEO_VENC_SAR *) nSar;

    ctrl[0].id = V4L2_CID_MPEG_VIDC_VENC_PARAM_SAR_WIDTH;
    ctrl[0].value = sar->nSARWidth;
    ctrl[1].id = V4L2_CID_MPEG_VIDC_VENC_PARAM_SAR_HEIGHT;
    ctrl[1].value = sar->nSARHeight;

    controls.count = 2;
    controls.ctrl_class = V4L2_CTRL_CLASS_MPEG;
    controls.controls = ctrl;

    DEBUG_PRINT_LOW("Calling IOCTL set control for id=%x val=%d, id=%x val=%d",
                    controls.controls[0].id, controls.controls[0].value,
                    controls.controls[1].id, controls.controls[1].value);

    rc = ioctl(m_nDriver_fd, VIDIOC_S_EXT_CTRLS, &controls);
    if (rc) {
        DEBUG_PRINT_ERROR("Failed to set SAR %d", rc);
        return false;
    }

    DEBUG_PRINT_LOW("Success IOCTL set control for id=%x val=%d, id=%x val=%d",
                    controls.controls[0].id, controls.controls[0].value,
                    controls.controls[1].id, controls.controls[1].value);
    return true;
}

bool venc_dev::venc_set_max_hierp(OMX_U32 hierp_layers)
{
    struct v4l2_control control;
    if (hierp_layers && (hier_layers.hier_mode == HIER_P) &&
            (hierp_layers <= hier_layers.numlayers)) {
        control.id = V4L2_CID_MPEG_VIDC_VIDEO_MAX_HIERP_LAYERS;
        control.value = hierp_layers;
        DEBUG_PRINT_LOW("Going to set V4L2_CID_MPEG_VIDC_VIDEO_MAX_HIERP_LAYERS");
        if (ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control)) {
            DEBUG_PRINT_ERROR("Failed to set MAX_HIERP_LAYERS");
            return false;
        }
        return true;
    } else {
        DEBUG_PRINT_ERROR("Invalid layers set for MAX_HIERP_LAYERS: %d",
                hierp_layers);
        return false;
    }
}

bool venc_dev::venc_set_baselayerid(OMX_U32 baseid)
{
    struct v4l2_control control;
    if (hier_layers.hier_mode == HIER_P) {
        control.id = V4L2_CID_MPEG_VIDC_VIDEO_BASELAYER_ID;
        control.value = baseid;
        DEBUG_PRINT_LOW("Going to set V4L2_CID_MPEG_VIDC_VIDEO_BASELAYER_ID");
        if (ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control)) {
            DEBUG_PRINT_ERROR("Failed to set V4L2_CID_MPEG_VIDC_VIDEO_BASELAYER_ID");
            return false;
        }
        return true;
    } else {
        DEBUG_PRINT_ERROR("Invalid mode set for V4L2_CID_MPEG_VIDC_VIDEO_BASELAYER_ID: %d",
                hier_layers.hier_mode);
        return false;
    }
}

bool venc_dev::venc_set_vui_timing_info(OMX_BOOL enable)
{
    struct v4l2_control control;
    int rc = 0;
    control.id = V4L2_CID_MPEG_VIDC_VIDEO_H264_VUI_TIMING_INFO;

    if (enable)
        control.value = V4L2_MPEG_VIDC_VIDEO_H264_VUI_TIMING_INFO_ENABLED;
    else
        control.value = V4L2_MPEG_VIDC_VIDEO_H264_VUI_TIMING_INFO_DISABLED;

    DEBUG_PRINT_LOW("Calling IOCTL set control for id=%x, val=%d", control.id, control.value);
    rc = ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control);
    if (rc) {
        DEBUG_PRINT_ERROR("Failed to set VUI timing info control");
        return false;
    }
    DEBUG_PRINT_LOW("Success IOCTL set control for id=%x, value=%d", control.id, control.value);
    return true;
}

bool venc_dev::venc_set_peak_bitrate(OMX_U32 nPeakBitrate)
{
    struct v4l2_control control;
    int rc = 0;
    control.id = V4L2_CID_MPEG_VIDEO_BITRATE_PEAK;
    control.value = nPeakBitrate;

    DEBUG_PRINT_LOW("venc_set_peak_bitrate: bitrate = %u", (unsigned int)nPeakBitrate);

    DEBUG_PRINT_LOW("Calling IOCTL set control for id=%d, val=%d", control.id, control.value);
    rc = ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control);

    if (rc) {
        DEBUG_PRINT_ERROR("Failed to set peak bitrate control");
        return false;
    }

    DEBUG_PRINT_LOW("Success IOCTL set control for id=%d, value=%d", control.id, control.value);

    return true;
}

bool venc_dev::venc_set_vpx_error_resilience(OMX_BOOL enable)
{
    struct v4l2_control control;
    int rc = 0;
    control.id = V4L2_CID_MPEG_VIDC_VIDEO_VPX_ERROR_RESILIENCE;

    if (enable)
        control.value = 1;
    else
        control.value = 0;

    DEBUG_PRINT_LOW("venc_set_vpx_error_resilience: %d", control.value);

    DEBUG_PRINT_LOW("Calling IOCTL set control for id=%d, val=%d", control.id, control.value);

    rc = ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control);
    if (rc) {
        DEBUG_PRINT_ERROR("Failed to set VPX Error Resilience");
        return false;
    }
    vpx_err_resilience.enable = 1;
    DEBUG_PRINT_LOW("Success IOCTL set control for id=%d, value=%d", control.id, control.value);
    return true;
}

bool venc_dev::venc_set_priority(OMX_U32 priority) {
    struct v4l2_control control;

    control.id = V4L2_CID_MPEG_VIDC_VIDEO_PRIORITY;
    if (priority == 0)
        control.value = V4L2_MPEG_VIDC_VIDEO_PRIORITY_REALTIME_ENABLE;
    else
        control.value = V4L2_MPEG_VIDC_VIDEO_PRIORITY_REALTIME_DISABLE;

    if (ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control)) {
        DEBUG_PRINT_ERROR("Failed to set V4L2_MPEG_VIDC_VIDEO_PRIORITY_REALTIME_%s",
                priority == 0 ? "ENABLE" : "DISABLE");
        return false;
    }
    return true;
}

bool venc_dev::venc_set_operatingrate(OMX_U32 rate) {
    struct v4l2_control control;

    control.id = V4L2_CID_MPEG_VIDC_VIDEO_OPERATING_RATE;
    control.value = rate;

    DEBUG_PRINT_LOW("venc_set_operating_rate: %d fps", rate >> 16);
    DEBUG_PRINT_LOW("Calling IOCTL set control for id=%d, val=%d", control.id, control.value);

    if(ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control)) {
        hw_overload = errno == EBUSY;
        DEBUG_PRINT_ERROR("Failed to set operating rate %d fps (%s)",
                rate >> 16, hw_overload ? "HW overload" : strerror(errno));
        return false;
    }
    operating_rate = rate;
    DEBUG_PRINT_LOW("Operating Rate Set = %d fps",  rate >> 16);
    return true;
}

bool venc_dev::venc_get_profile_level(OMX_U32 *eProfile,OMX_U32 *eLevel)
{
    bool status = true;

    if (eProfile == NULL || eLevel == NULL) {
        return false;
    }

    if (m_sVenc_cfg.codectype == V4L2_PIX_FMT_MPEG4) {
        switch (codec_profile.profile) {
            case V4L2_MPEG_VIDEO_MPEG4_PROFILE_SIMPLE:
                *eProfile = OMX_VIDEO_MPEG4ProfileSimple;
                break;
            case V4L2_MPEG_VIDEO_MPEG4_PROFILE_ADVANCED_SIMPLE:
                *eProfile = OMX_VIDEO_MPEG4ProfileAdvancedSimple;
                break;
            default:
                *eProfile = OMX_VIDEO_MPEG4ProfileMax;
                status = false;
                break;
        }

        if (!status) {
            return status;
        }

        //profile level
        switch (profile_level.level) {
            case V4L2_MPEG_VIDEO_MPEG4_LEVEL_0:
                *eLevel = OMX_VIDEO_MPEG4Level0;
                break;
            case V4L2_MPEG_VIDEO_MPEG4_LEVEL_0B:
                *eLevel = OMX_VIDEO_MPEG4Level0b;
                break;
            case V4L2_MPEG_VIDEO_MPEG4_LEVEL_1:
                *eLevel = OMX_VIDEO_MPEG4Level1;
                break;
            case V4L2_MPEG_VIDEO_MPEG4_LEVEL_2:
                *eLevel = OMX_VIDEO_MPEG4Level2;
                break;
            case V4L2_MPEG_VIDEO_MPEG4_LEVEL_3:
                *eLevel = OMX_VIDEO_MPEG4Level3;
                break;
            case V4L2_MPEG_VIDEO_MPEG4_LEVEL_4:
                *eLevel = OMX_VIDEO_MPEG4Level4;
                break;
            case V4L2_MPEG_VIDEO_MPEG4_LEVEL_5:
                *eLevel = OMX_VIDEO_MPEG4Level5;
                break;
            default:
                *eLevel = OMX_VIDEO_MPEG4LevelMax;
                status =  false;
                break;
        }
    } else if (m_sVenc_cfg.codectype == V4L2_PIX_FMT_H263) {
        if (codec_profile.profile == VEN_PROFILE_H263_BASELINE) {
            *eProfile = OMX_VIDEO_H263ProfileBaseline;
        } else {
            *eProfile = OMX_VIDEO_H263ProfileMax;
            return false;
        }

        switch (profile_level.level) {
            case VEN_LEVEL_H263_10:
                *eLevel = OMX_VIDEO_H263Level10;
                break;
            case VEN_LEVEL_H263_20:
                *eLevel = OMX_VIDEO_H263Level20;
                break;
            case VEN_LEVEL_H263_30:
                *eLevel = OMX_VIDEO_H263Level30;
                break;
            case VEN_LEVEL_H263_40:
                *eLevel = OMX_VIDEO_H263Level40;
                break;
            case VEN_LEVEL_H263_45:
                *eLevel = OMX_VIDEO_H263Level45;
                break;
            case VEN_LEVEL_H263_50:
                *eLevel = OMX_VIDEO_H263Level50;
                break;
            case VEN_LEVEL_H263_60:
                *eLevel = OMX_VIDEO_H263Level60;
                break;
            case VEN_LEVEL_H263_70:
                *eLevel = OMX_VIDEO_H263Level70;
                break;
            default:
                *eLevel = OMX_VIDEO_H263LevelMax;
                status = false;
                break;
        }
    } else if (m_sVenc_cfg.codectype == V4L2_PIX_FMT_H264) {
        switch (codec_profile.profile) {
            case V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE:
                *eProfile = OMX_VIDEO_AVCProfileBaseline;
                break;
            case V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_BASELINE:
                *eProfile = QOMX_VIDEO_AVCProfileConstrainedBaseline;
                break;
            case V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_HIGH:
                *eProfile = QOMX_VIDEO_AVCProfileConstrainedHigh;
                break;
            case V4L2_MPEG_VIDEO_H264_PROFILE_MAIN:
                *eProfile = OMX_VIDEO_AVCProfileMain;
                break;
            case V4L2_MPEG_VIDEO_H264_PROFILE_HIGH:
                *eProfile = OMX_VIDEO_AVCProfileHigh;
                break;
            case V4L2_MPEG_VIDEO_H264_PROFILE_EXTENDED:
                *eProfile = OMX_VIDEO_AVCProfileExtended;
                break;
            case V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_10:
                *eProfile = OMX_VIDEO_AVCProfileHigh10;
                break;
            case V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_422:
                *eProfile = OMX_VIDEO_AVCProfileHigh422;
                break;
            case V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_444_PREDICTIVE:
                *eProfile = OMX_VIDEO_AVCProfileHigh444;
                break;
            default:
                *eProfile = OMX_VIDEO_AVCProfileMax;
                status = false;
                break;
        }

        if (!status) {
            return status;
        }

        switch (profile_level.level) {
            case V4L2_MPEG_VIDEO_H264_LEVEL_1_0:
                *eLevel = OMX_VIDEO_AVCLevel1;
                break;
            case V4L2_MPEG_VIDEO_H264_LEVEL_1B:
                *eLevel = OMX_VIDEO_AVCLevel1b;
                break;
            case V4L2_MPEG_VIDEO_H264_LEVEL_1_1:
                *eLevel = OMX_VIDEO_AVCLevel11;
                break;
            case V4L2_MPEG_VIDEO_H264_LEVEL_1_2:
                *eLevel = OMX_VIDEO_AVCLevel12;
                break;
            case V4L2_MPEG_VIDEO_H264_LEVEL_1_3:
                *eLevel = OMX_VIDEO_AVCLevel13;
                break;
            case V4L2_MPEG_VIDEO_H264_LEVEL_2_0:
                *eLevel = OMX_VIDEO_AVCLevel2;
                break;
            case V4L2_MPEG_VIDEO_H264_LEVEL_2_1:
                *eLevel = OMX_VIDEO_AVCLevel21;
                break;
            case V4L2_MPEG_VIDEO_H264_LEVEL_2_2:
                *eLevel = OMX_VIDEO_AVCLevel22;
                break;
            case V4L2_MPEG_VIDEO_H264_LEVEL_3_0:
                *eLevel = OMX_VIDEO_AVCLevel3;
                break;
            case V4L2_MPEG_VIDEO_H264_LEVEL_3_1:
                *eLevel = OMX_VIDEO_AVCLevel31;
                break;
            case V4L2_MPEG_VIDEO_H264_LEVEL_3_2:
                *eLevel = OMX_VIDEO_AVCLevel32;
                break;
            case V4L2_MPEG_VIDEO_H264_LEVEL_4_0:
                *eLevel = OMX_VIDEO_AVCLevel4;
                break;
            case V4L2_MPEG_VIDEO_H264_LEVEL_4_1:
                *eLevel = OMX_VIDEO_AVCLevel41;
                break;
            case V4L2_MPEG_VIDEO_H264_LEVEL_4_2:
                *eLevel = OMX_VIDEO_AVCLevel42;
                break;
            case V4L2_MPEG_VIDEO_H264_LEVEL_5_0:
                *eLevel = OMX_VIDEO_AVCLevel5;
                break;
            case V4L2_MPEG_VIDEO_H264_LEVEL_5_1:
                *eLevel = OMX_VIDEO_AVCLevel51;
                break;
            case V4L2_MPEG_VIDEO_H264_LEVEL_5_2:
                *eLevel = OMX_VIDEO_AVCLevel52;
                break;
            default :
                *eLevel = OMX_VIDEO_AVCLevelMax;
                status = false;
                break;
        }
    }
    else if (m_sVenc_cfg.codectype == V4L2_PIX_FMT_VP8) {
        switch (codec_profile.profile) {
            case V4L2_MPEG_VIDC_VIDEO_VP8_UNUSED:
                *eProfile = OMX_VIDEO_VP8ProfileMain;
                break;
            default:
                *eProfile = OMX_VIDEO_VP8ProfileMax;
                status = false;
                break;
        }
        if (!status) {
            return status;
        }

        switch (profile_level.level) {
            case V4L2_MPEG_VIDC_VIDEO_VP8_VERSION_0:
                *eLevel = OMX_VIDEO_VP8Level_Version0;
                break;
            case V4L2_MPEG_VIDC_VIDEO_VP8_VERSION_1:
                *eLevel = OMX_VIDEO_VP8Level_Version1;
                break;
            default:
                *eLevel = OMX_VIDEO_VP8LevelMax;
                status = false;
                break;
        }
    } else if (m_sVenc_cfg.codectype == V4L2_PIX_FMT_HEVC) {
        switch (codec_profile.profile) {
            case V4L2_MPEG_VIDC_VIDEO_HEVC_PROFILE_MAIN:
                *eProfile = OMX_VIDEO_HEVCProfileMain;
                break;
            case V4L2_MPEG_VIDC_VIDEO_HEVC_PROFILE_MAIN10:
                *eProfile = OMX_VIDEO_HEVCProfileMain10;
                break;
            default:
                *eProfile = OMX_VIDEO_HEVCProfileMax;
                status = false;
                break;
        }
        if (!status) {
            return status;
        }

        switch (profile_level.level) {
            case V4L2_MPEG_VIDC_VIDEO_HEVC_LEVEL_MAIN_TIER_LEVEL_1:
                *eLevel = OMX_VIDEO_HEVCMainTierLevel1;
                break;
            case V4L2_MPEG_VIDC_VIDEO_HEVC_LEVEL_HIGH_TIER_LEVEL_1:
                *eLevel = OMX_VIDEO_HEVCHighTierLevel1;
                break;
            case V4L2_MPEG_VIDC_VIDEO_HEVC_LEVEL_MAIN_TIER_LEVEL_2:
                *eLevel = OMX_VIDEO_HEVCMainTierLevel2;
                break;
            case V4L2_MPEG_VIDC_VIDEO_HEVC_LEVEL_HIGH_TIER_LEVEL_2:
                *eLevel = OMX_VIDEO_HEVCHighTierLevel2;
                break;
            case V4L2_MPEG_VIDC_VIDEO_HEVC_LEVEL_MAIN_TIER_LEVEL_2_1:
                *eLevel = OMX_VIDEO_HEVCMainTierLevel21;
                break;
            case V4L2_MPEG_VIDC_VIDEO_HEVC_LEVEL_HIGH_TIER_LEVEL_2_1:
                *eLevel = OMX_VIDEO_HEVCHighTierLevel21;
                break;
            case V4L2_MPEG_VIDC_VIDEO_HEVC_LEVEL_MAIN_TIER_LEVEL_3:
                *eLevel = OMX_VIDEO_HEVCMainTierLevel3;
                break;
            case V4L2_MPEG_VIDC_VIDEO_HEVC_LEVEL_HIGH_TIER_LEVEL_3:
                *eLevel = OMX_VIDEO_HEVCHighTierLevel3;
                break;
            case V4L2_MPEG_VIDC_VIDEO_HEVC_LEVEL_MAIN_TIER_LEVEL_3_1:
                *eLevel = OMX_VIDEO_HEVCMainTierLevel31;
                break;
            case V4L2_MPEG_VIDC_VIDEO_HEVC_LEVEL_HIGH_TIER_LEVEL_3_1:
                *eLevel = OMX_VIDEO_HEVCHighTierLevel31;
                break;
            case V4L2_MPEG_VIDC_VIDEO_HEVC_LEVEL_MAIN_TIER_LEVEL_4:
                *eLevel = OMX_VIDEO_HEVCMainTierLevel4;
                break;
            case V4L2_MPEG_VIDC_VIDEO_HEVC_LEVEL_HIGH_TIER_LEVEL_4:
                *eLevel = OMX_VIDEO_HEVCHighTierLevel4;
                break;
            case V4L2_MPEG_VIDC_VIDEO_HEVC_LEVEL_MAIN_TIER_LEVEL_4_1:
                *eLevel = OMX_VIDEO_HEVCMainTierLevel41;
                break;
            case V4L2_MPEG_VIDC_VIDEO_HEVC_LEVEL_HIGH_TIER_LEVEL_4_1:
                *eLevel = OMX_VIDEO_HEVCHighTierLevel41;
                break;
            case V4L2_MPEG_VIDC_VIDEO_HEVC_LEVEL_MAIN_TIER_LEVEL_5:
                *eLevel = OMX_VIDEO_HEVCMainTierLevel5;
                break;
            case V4L2_MPEG_VIDC_VIDEO_HEVC_LEVEL_HIGH_TIER_LEVEL_5:
                *eLevel = OMX_VIDEO_HEVCHighTierLevel5;
                break;
            case V4L2_MPEG_VIDC_VIDEO_HEVC_LEVEL_MAIN_TIER_LEVEL_5_1:
                *eLevel = OMX_VIDEO_HEVCMainTierLevel51;
                break;
            case V4L2_MPEG_VIDC_VIDEO_HEVC_LEVEL_HIGH_TIER_LEVEL_5_1:
                *eLevel = OMX_VIDEO_HEVCHighTierLevel51;
                break;
            case V4L2_MPEG_VIDC_VIDEO_HEVC_LEVEL_MAIN_TIER_LEVEL_5_2:
                *eLevel = OMX_VIDEO_HEVCMainTierLevel52;
                break;
            case V4L2_MPEG_VIDC_VIDEO_HEVC_LEVEL_HIGH_TIER_LEVEL_5_2:
                *eLevel = OMX_VIDEO_HEVCHighTierLevel52;
                break;
            case V4L2_MPEG_VIDC_VIDEO_HEVC_LEVEL_MAIN_TIER_LEVEL_6:
                *eLevel = OMX_VIDEO_HEVCMainTierLevel6;
                break;
            case V4L2_MPEG_VIDC_VIDEO_HEVC_LEVEL_HIGH_TIER_LEVEL_6:
                *eLevel = OMX_VIDEO_HEVCHighTierLevel6;
                break;
            case V4L2_MPEG_VIDC_VIDEO_HEVC_LEVEL_MAIN_TIER_LEVEL_6_1:
                *eLevel = OMX_VIDEO_HEVCMainTierLevel61;
                break;
            case V4L2_MPEG_VIDC_VIDEO_HEVC_LEVEL_HIGH_TIER_LEVEL_6_1:
                *eLevel = OMX_VIDEO_HEVCHighTierLevel61;
                break;
            case V4L2_MPEG_VIDC_VIDEO_HEVC_LEVEL_MAIN_TIER_LEVEL_6_2:
                *eLevel = OMX_VIDEO_HEVCMainTierLevel62;
                break;
            default:
                *eLevel = OMX_VIDEO_HEVCLevelMax;
                status = false;
                break;
        }
    }

    return status;
}

bool venc_dev::venc_validate_profile_level(OMX_U32 *eProfile, OMX_U32 *eLevel)
{
    OMX_U32 new_profile = 0, new_level = 0;
    unsigned const int *profile_tbl = NULL;
    OMX_U32 mb_per_frame, mb_per_sec;
    bool profile_level_found = false;

    DEBUG_PRINT_LOW("Init profile table for respective codec");

    //validate the ht,width,fps,bitrate and set the appropriate profile and level
    if (m_sVenc_cfg.codectype == V4L2_PIX_FMT_MPEG4) {
        if (*eProfile == 0) {
            if (!m_profile_set) {
                *eProfile = OMX_VIDEO_MPEG4ProfileSimple;
            } else {
                switch (codec_profile.profile) {
                    case V4L2_MPEG_VIDEO_MPEG4_PROFILE_SIMPLE:
                        *eProfile = OMX_VIDEO_MPEG4ProfileAdvancedSimple;
                        break;
                    case V4L2_MPEG_VIDEO_MPEG4_PROFILE_ADVANCED_SIMPLE:
                        *eProfile = OMX_VIDEO_MPEG4ProfileSimple;
                        break;
                    default:
                        DEBUG_PRINT_LOW("%s(): Unknown Error", __func__);
                        return false;
                }
            }
        }

        if (*eLevel == 0 && !m_level_set) {
            *eLevel = OMX_VIDEO_MPEG4LevelMax;
        }

        if (*eProfile == OMX_VIDEO_MPEG4ProfileSimple) {
            profile_tbl = (unsigned int const *)mpeg4_profile_level_table;
        } else if (*eProfile == OMX_VIDEO_MPEG4ProfileAdvancedSimple) {
            profile_tbl = (unsigned int const *)
                (&mpeg4_profile_level_table[MPEG4_ASP_START]);
        } else {
            DEBUG_PRINT_LOW("Unsupported MPEG4 profile type %u", (unsigned int)*eProfile);
            return false;
        }
    } else if (m_sVenc_cfg.codectype == V4L2_PIX_FMT_H264) {
        if (*eProfile == 0) {
            if (!m_profile_set) {
                *eProfile = OMX_VIDEO_AVCProfileBaseline;
            } else {
                switch (codec_profile.profile) {
                    case V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE:
                        *eProfile = OMX_VIDEO_AVCProfileBaseline;
                        break;
                    case V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_BASELINE:
                        *eProfile = QOMX_VIDEO_AVCProfileConstrainedBaseline;
                        break;
                    case V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_HIGH:
                         *eProfile = QOMX_VIDEO_AVCProfileConstrainedHigh;
                        break;
                    case V4L2_MPEG_VIDEO_H264_PROFILE_MAIN:
                        *eProfile = OMX_VIDEO_AVCProfileMain;
                        break;
                    case V4L2_MPEG_VIDEO_H264_PROFILE_EXTENDED:
                        *eProfile = OMX_VIDEO_AVCProfileExtended;
                        break;
                    case V4L2_MPEG_VIDEO_H264_PROFILE_HIGH:
                        *eProfile = OMX_VIDEO_AVCProfileHigh;
                        break;
                    case V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_10:
                        *eProfile = OMX_VIDEO_AVCProfileHigh10;
                        break;
                    case V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_422:
                        *eProfile = OMX_VIDEO_AVCProfileHigh422;
                        break;
                    case V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_444_PREDICTIVE:
                        *eProfile = OMX_VIDEO_AVCProfileHigh444;
                        break;
                    default:
                        DEBUG_PRINT_LOW("%s(): Unknown Error", __func__);
                        return false;
                }
            }
        }

        if (*eLevel == 0 && !m_level_set) {
            *eLevel = OMX_VIDEO_AVCLevelMax;
        }

        if ((*eProfile == OMX_VIDEO_AVCProfileBaseline) ||
            (*eProfile == QOMX_VIDEO_AVCProfileConstrainedBaseline)) {
            profile_tbl = (unsigned int const *)h264_profile_level_table;
        } else if ((*eProfile == OMX_VIDEO_AVCProfileHigh) ||
            (*eProfile == QOMX_VIDEO_AVCProfileConstrainedHigh)) {
            profile_tbl = (unsigned int const *)
                (&h264_profile_level_table[H264_HP_START]);
        } else if (*eProfile == OMX_VIDEO_AVCProfileMain) {
            profile_tbl = (unsigned int const *)
                (&h264_profile_level_table[H264_MP_START]);
        } else {
            DEBUG_PRINT_LOW("Unsupported AVC profile type %u", (unsigned int)*eProfile);
            return false;
        }
    } else if (m_sVenc_cfg.codectype == V4L2_PIX_FMT_H263) {
        if (*eProfile == 0) {
            if (!m_profile_set) {
                *eProfile = OMX_VIDEO_H263ProfileBaseline;
            } else {
                switch (codec_profile.profile) {
                    case VEN_PROFILE_H263_BASELINE:
                        *eProfile = OMX_VIDEO_H263ProfileBaseline;
                        break;
                    default:
                        DEBUG_PRINT_LOW("%s(): Unknown Error", __func__);
                        return false;
                }
            }
        }

        if (*eLevel == 0 && !m_level_set) {
            *eLevel = OMX_VIDEO_H263LevelMax;
        }

        if (*eProfile == OMX_VIDEO_H263ProfileBaseline) {
            profile_tbl = (unsigned int const *)h263_profile_level_table;
        } else {
            DEBUG_PRINT_LOW("Unsupported H.263 profile type %u", (unsigned int)*eProfile);
            return false;
        }
    } else if (m_sVenc_cfg.codectype == V4L2_PIX_FMT_VP8) {
        if (*eProfile == 0) {
            *eProfile = OMX_VIDEO_VP8ProfileMain;
        } else {
            switch (codec_profile.profile) {
                case V4L2_MPEG_VIDC_VIDEO_VP8_UNUSED:
                    *eProfile = OMX_VIDEO_VP8ProfileMain;
                    break;
                default:
                    DEBUG_PRINT_ERROR("%s(): Unknown VP8 profile", __func__);
                    return false;
            }
        }
        if (*eLevel == 0) {
            switch (profile_level.level) {
                case V4L2_MPEG_VIDC_VIDEO_VP8_VERSION_0:
                    *eLevel = OMX_VIDEO_VP8Level_Version0;
                    break;
                case V4L2_MPEG_VIDC_VIDEO_VP8_VERSION_1:
                    *eLevel = OMX_VIDEO_VP8Level_Version1;
                    break;
                default:
                    DEBUG_PRINT_ERROR("%s(): Unknown VP8 level", __func__);
                    return false;
            }
        }
        return true;
    } else if (m_sVenc_cfg.codectype == V4L2_PIX_FMT_HEVC) {
        if (*eProfile == 0) {
            if (!m_profile_set) {
                *eProfile = OMX_VIDEO_HEVCProfileMain;
            } else {
                switch (codec_profile.profile) {
                    case V4L2_MPEG_VIDC_VIDEO_HEVC_PROFILE_MAIN:
                        *eProfile = OMX_VIDEO_HEVCProfileMain;
                        break;
                    case V4L2_MPEG_VIDC_VIDEO_HEVC_PROFILE_MAIN10:
                        *eProfile = OMX_VIDEO_HEVCProfileMain10;
                        break;
                    default:
                        DEBUG_PRINT_ERROR("%s(): Unknown Error", __func__);
                        return false;
                }
            }
        }

        if (*eLevel == 0 && !m_level_set) {
            *eLevel = OMX_VIDEO_HEVCLevelMax;
        }

        if (*eProfile == OMX_VIDEO_HEVCProfileMain) {
            profile_tbl = (unsigned int const *)hevc_profile_level_table;
        } else if (*eProfile == OMX_VIDEO_HEVCProfileMain10) {
            profile_tbl = (unsigned int const *)
                (&hevc_profile_level_table[HEVC_MAIN10_START]);
        } else {
            DEBUG_PRINT_ERROR("Unsupported HEVC profile type %u", (unsigned int)*eProfile);
            return false;
        }
    } else {
        DEBUG_PRINT_ERROR("Invalid codec type");
        return false;
    }

    mb_per_frame = ((m_sVenc_cfg.dvs_height + 15) >> 4)*
        ((m_sVenc_cfg.dvs_width + 15)>> 4);

    if ((mb_per_frame >= 3600) && (m_sVenc_cfg.codectype == (unsigned long) V4L2_PIX_FMT_MPEG4)) {
        if (codec_profile.profile == (unsigned long) V4L2_MPEG_VIDEO_MPEG4_PROFILE_ADVANCED_SIMPLE)
            profile_level.level = V4L2_MPEG_VIDEO_MPEG4_LEVEL_5;

        if (codec_profile.profile == (unsigned long) V4L2_MPEG_VIDEO_MPEG4_PROFILE_SIMPLE)
            profile_level.level = V4L2_MPEG_VIDEO_MPEG4_LEVEL_5;

        {
            new_level = profile_level.level;
            new_profile = codec_profile.profile;
            return true;
        }
    }

    if (rate_ctrl.rcmode == V4L2_CID_MPEG_VIDC_VIDEO_RATE_CONTROL_OFF) {
        *eLevel = rc_off_level; //No level calculation for RC_OFF
        profile_level_found = true;
        return true;
    }

    mb_per_sec = mb_per_frame * m_sVenc_cfg.fps_num / m_sVenc_cfg.fps_den;

    bool h264, ltr, hlayers;
    unsigned int hybridp = 0, maxDpb = profile_tbl[5] / mb_per_frame;
    h264 = m_sVenc_cfg.codectype == V4L2_PIX_FMT_H264;
    ltr = ltrinfo.enabled && ((ltrinfo.count + 2) <= MIN((unsigned int) (profile_tbl[5] / mb_per_frame), MAXDPB));
    hlayers = hier_layers.numlayers && hier_layers.hier_mode == HIER_P &&
     ((intra_period.num_bframes + ltrinfo.count + hier_layers.numlayers + 1) <= (unsigned int) (profile_tbl[5] / profile_tbl[0]));

    /*  Hybrid HP reference buffers:
        layers = 1, 2 need 1 reference buffer
        layers = 3, 4 need 2 reference buffers
        layers = 5, 6 need 3 reference buffers
    */

    if(hier_layers.hier_mode == HIER_P_HYBRID)
        hybridp = MIN(MAX(maxDpb, ((hier_layers.numlayers + 1) / 2)), 16);

    do {
        if (mb_per_frame <= (unsigned int)profile_tbl[0]) {
            if (mb_per_sec <= (unsigned int)profile_tbl[1]) {
                if (m_sVenc_cfg.targetbitrate <= (unsigned int)profile_tbl[2]) {
                    if (h264 && (ltr || hlayers || hybridp)) {
                        // Update profile and level to adapt to the LTR and Hier-p/Hybrid-HP settings
                        new_level = (int)profile_tbl[3];
                        new_profile = (int)profile_tbl[4];
                        profile_level_found = true;
                        DEBUG_PRINT_LOW("Appropriate profile/level for LTR count: %u OR Hier-p: %u is %u/%u, maxDPB: %u",
                                        ltrinfo.count, hier_layers.numlayers, (int)new_profile, (int)new_level,
                                        MIN((unsigned int) (profile_tbl[5] / mb_per_frame), MAXDPB));
                        break;
                    } else {
                        new_level = (int)profile_tbl[3];
                        new_profile = (int)profile_tbl[4];
                        profile_level_found = true;
                        DEBUG_PRINT_LOW("Appropriate profile/level found %u/%u", (int) new_profile, (int) new_level);
                        break;
                    }
                }
            }
        }
        profile_tbl = profile_tbl + MAX_PROFILE_PARAMS;
    } while (profile_tbl[0] != 0);

    if (profile_level_found != true) {
        DEBUG_PRINT_LOW("ERROR: Unsupported profile/level");
        return false;
    }

    if ((*eLevel == OMX_VIDEO_MPEG4LevelMax) || (*eLevel == OMX_VIDEO_AVCLevelMax)
            || (*eLevel == OMX_VIDEO_H263LevelMax) || (*eLevel == OMX_VIDEO_VP8ProfileMax)
            || (*eLevel == OMX_VIDEO_HEVCLevelMax)) {
        *eLevel = new_level;
    }

    DEBUG_PRINT_LOW("%s: Returning with eProfile = %u"
            "Level = %u", __func__, (unsigned int)*eProfile, (unsigned int)*eLevel);

    return true;
}
#ifdef _ANDROID_ICS_
bool venc_dev::venc_set_meta_mode(bool mode)
{
    metadatamode = mode;
    return true;
}
#endif

bool venc_dev::venc_is_video_session_supported(unsigned long width,
        unsigned long height)
{
    if ((width * height < capability.min_width *  capability.min_height) ||
            (width * height > capability.max_width *  capability.max_height)) {
        DEBUG_PRINT_ERROR(
                "Unsupported video resolution WxH = (%lu)x(%lu) supported range = min (%d)x(%d) - max (%d)x(%d)",
                width, height, capability.min_width, capability.min_height,
                capability.max_width, capability.max_height);
        return false;
    }

    DEBUG_PRINT_LOW("video session supported");
    return true;
}

bool venc_dev::venc_set_batch_size(OMX_U32 batchSize)
{
    struct v4l2_control control;
    int ret;

    control.id = V4L2_CID_VIDC_QBUF_MODE;
    control.value = batchSize ? V4L2_VIDC_QBUF_BATCHED : V4L2_VIDC_QBUF_STANDARD;

    ret = ioctl(m_nDriver_fd, VIDIOC_S_CTRL, &control);
    if (ret) {
        DEBUG_PRINT_ERROR("Failed to set batching mode: %d", ret);
        return false;
    }

    mBatchSize = batchSize;
    DEBUG_PRINT_HIGH("Using batch size of %d", mBatchSize);
    return true;
}

venc_dev::BatchInfo::BatchInfo()
    : mNumPending(0) {
    pthread_mutex_init(&mLock, NULL);
    for (int i = 0; i < kMaxBufs; ++i) {
        mBufMap[i] = kBufIDFree;
    }
}

int venc_dev::BatchInfo::registerBuffer(int bufferId) {
    pthread_mutex_lock(&mLock);
    int availId = 0;
    for( ; availId < kMaxBufs && mBufMap[availId] != kBufIDFree; ++availId);
    if (availId >= kMaxBufs) {
        DEBUG_PRINT_ERROR("Failed to find free entry !");
        pthread_mutex_unlock(&mLock);
        return -1;
    }
    mBufMap[availId] = bufferId;
    mNumPending++;
    pthread_mutex_unlock(&mLock);
    return availId;
}

int venc_dev::BatchInfo::retrieveBufferAt(int v4l2Id) {
    pthread_mutex_lock(&mLock);
    if (v4l2Id >= kMaxBufs || v4l2Id < 0) {
        DEBUG_PRINT_ERROR("Batch: invalid index %d", v4l2Id);
        pthread_mutex_unlock(&mLock);
        return -1;
    }
    if (mBufMap[v4l2Id] == kBufIDFree) {
        DEBUG_PRINT_ERROR("Batch: buffer @ %d was not registered !", v4l2Id);
        pthread_mutex_unlock(&mLock);
        return -1;
    }
    int bufferId = mBufMap[v4l2Id];
    mBufMap[v4l2Id] = kBufIDFree;
    mNumPending--;
    pthread_mutex_unlock(&mLock);
    return bufferId;
}

bool venc_dev::BatchInfo::isPending(int bufferId) {
    pthread_mutex_lock(&mLock);
    int existsId = 0;
    for(; existsId < kMaxBufs && mBufMap[existsId] != bufferId; ++existsId);
    pthread_mutex_unlock(&mLock);
    return existsId < kMaxBufs;
}

int venc_dev::BatchInfo::getFdAt(native_handle_t *hnd, int index) {
    int fd = hnd && index < hnd->numFds ? hnd->data[index] : -1;
    return fd;
}

int venc_dev::BatchInfo::getOffsetAt(native_handle_t *hnd, int index) {
    int off = hnd && index < hnd->numInts ? hnd->data[hnd->numFds + index] : -1;
    return off;
}

int venc_dev::BatchInfo::getSizeAt(native_handle_t *hnd, int index) {
    int size = hnd && (index + hnd->numFds) < hnd->numInts ?
            hnd->data[2*hnd->numFds + index] : -1;
    return size;
}

int venc_dev::BatchInfo::getColorFormatAt(native_handle_t *hnd, int index) {
    int usage = hnd && (index + 2*hnd->numFds) < hnd->numInts ?
            hnd->data[3*hnd->numFds + index] : 0;
    return usage;
}

int venc_dev::BatchInfo::getTimeStampAt(native_handle_t *hnd, int index) {
    int size = hnd && (index + 3*hnd->numFds) < hnd->numInts ?
            hnd->data[4*hnd->numFds + index] : -1;
    return size;
}

venc_dev::venc_dev_vqzip::venc_dev_vqzip()
{
    mLibHandle = NULL;
    pthread_mutex_init(&lock, NULL);
}

bool venc_dev::venc_dev_vqzip::init()
{
    bool status = true;
    if (mLibHandle) {
        DEBUG_PRINT_ERROR("VQZIP init called twice");
        status = false;
    }
    if (status) {
        mLibHandle = dlopen("libvqzip.so", RTLD_NOW);
        if (mLibHandle) {
            mVQZIPInit = (vqzip_init_t)
                dlsym(mLibHandle,"VQZipInit");
            mVQZIPDeInit = (vqzip_deinit_t)
                dlsym(mLibHandle,"VQZipDeInit");
            mVQZIPComputeStats = (vqzip_compute_stats_t)
                dlsym(mLibHandle,"VQZipComputeStats");
            if (!mVQZIPInit || !mVQZIPDeInit || !mVQZIPComputeStats)
                status = false;
        } else {
            DEBUG_PRINT_ERROR("FATAL ERROR: could not dlopen libvqzip.so: %s", dlerror());
            status = false;
        }
        if (status) {
            mVQZIPHandle = mVQZIPInit();
        }
    }
    if (!status && mLibHandle) {
        dlclose(mLibHandle);
        mLibHandle = NULL;
        mVQZIPHandle = NULL;
        mVQZIPInit = NULL;
        mVQZIPDeInit = NULL;
        mVQZIPComputeStats = NULL;
    }
    return status;
}

int venc_dev::venc_dev_vqzip::fill_stats_data(void* pBuf, void* extraData)
{
    VQZipStatus result;
    VQZipStats *pStats = (VQZipStats *)extraData;
    pConfig.pSEIPayload = NULL;
    unsigned long size;

    if (!pBuf || !pStats || !mVQZIPHandle) {
        DEBUG_PRINT_ERROR("Invalid data passed to stats function");
    }
    result = mVQZIPComputeStats(mVQZIPHandle, (void* )pBuf, &pConfig, pStats);
    return (result < 0);
}

void venc_dev::venc_dev_vqzip::deinit()
{
    if (mLibHandle) {
        pthread_mutex_lock(&lock);
        dlclose(mLibHandle);
        mVQZIPDeInit(mVQZIPHandle);
        mLibHandle = NULL;
        mVQZIPHandle = NULL;
        mVQZIPInit = NULL;
        mVQZIPDeInit = NULL;
        mVQZIPComputeStats = NULL;
        pthread_mutex_unlock(&lock);
    }
}

venc_dev::venc_dev_vqzip::~venc_dev_vqzip()
{
    DEBUG_PRINT_HIGH("Destroy C2D instance");
    if (mLibHandle) {
        dlclose(mLibHandle);
    }
    mLibHandle = NULL;
    pthread_mutex_destroy(&lock);
}
