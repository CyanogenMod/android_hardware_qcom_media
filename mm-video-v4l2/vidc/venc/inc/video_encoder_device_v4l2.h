/*--------------------------------------------------------------------------
Copyright (c) 2012-2015, The Linux Foundation. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.
    * Neither the name of The Linux Foundation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
--------------------------------------------------------------------------*/
#ifndef __OMX_VENC_DEV__
#define __OMX_VENC_DEV__

#include "OMX_Types.h"
#include "OMX_Core.h"
#include "OMX_VideoExt.h"
#include "OMX_QCOMExtns.h"
#include "qc_omx_component.h"
#include "omx_video_common.h"
#include "omx_video_base.h"
#include "omx_video_encoder.h"
#include <linux/videodev2.h>
#include <poll.h>

#define TIMEOUT 5*60*1000
#define BIT(num) (1 << (num))
#define MAX_HYB_HIERP_LAYERS 6
#define MAX_v4L2_INPUT_BUFS (64) //VB2_MAX_FRAME

enum hier_type {
    HIER_NONE = 0x0,
    HIER_P = 0x1,
    HIER_B = 0x2,
    HIER_P_HYBRID = 0x3,
};

struct msm_venc_switch {
    unsigned char    status;
};

struct msm_venc_allocatorproperty {
    unsigned long     mincount;
    unsigned long     actualcount;
    unsigned long     datasize;
    unsigned long     suffixsize;
    unsigned long     alignment;
    unsigned long     bufpoolid;
};

struct msm_venc_basecfg {
    unsigned long    input_width;
    unsigned long    input_height;
    unsigned long    dvs_width;
    unsigned long    dvs_height;
    unsigned long    codectype;
    unsigned long    fps_num;
    unsigned long    fps_den;
    unsigned long    targetbitrate;
    unsigned long    inputformat;
};

struct msm_venc_profile {
    unsigned long    profile;
};
struct msm_venc_profilelevel {
    unsigned long    level;
};

struct msm_venc_sessionqp {
    unsigned long    iframeqp;
    unsigned long    pframeqp;
    unsigned long    bframeqp;
};

struct msm_venc_initqp {
    unsigned long    iframeqp;
    unsigned long    pframeqp;
    unsigned long    bframeqp;
    unsigned long    enableinitqp;
};

struct msm_venc_qprange {
    unsigned long    maxqp;
    unsigned long    minqp;
};
struct msm_venc_intraperiod {
    unsigned long    num_pframes;
    unsigned long    num_bframes;
};
struct msm_venc_seqheader {
    unsigned char *hdrbufptr;
    unsigned long    bufsize;
    unsigned long    hdrlen;
};

struct msm_venc_capability {
    unsigned long    codec_types;
    unsigned long    maxframe_width;
    unsigned long    maxframe_height;
    unsigned long    maxtarget_bitrate;
    unsigned long    maxframe_rate;
    unsigned long    input_formats;
    unsigned char    dvs;
};

struct msm_venc_entropycfg {
    unsigned longentropysel;
    unsigned long    cabacmodel;
};

struct msm_venc_dbcfg {
    unsigned long    db_mode;
    unsigned long    slicealpha_offset;
    unsigned long    slicebeta_offset;
};

struct msm_venc_intrarefresh {
    unsigned long    irmode;
    unsigned long    mbcount;
};

struct msm_venc_multiclicecfg {
    unsigned long    mslice_mode;
    unsigned long    mslice_size;
};

struct msm_venc_bufferflush {
    unsigned long    flush_mode;
};

struct msm_venc_ratectrlcfg {
    unsigned long    rcmode;
};

struct    msm_venc_voptimingcfg {
    unsigned long    voptime_resolution;
};
struct msm_venc_framerate {
    unsigned long    fps_denominator;
    unsigned long    fps_numerator;
};

struct msm_venc_targetbitrate {
    unsigned long    target_bitrate;
};


struct msm_venc_rotation {
    unsigned long    rotation;
};

struct msm_venc_timeout {
    unsigned long    millisec;
};

struct msm_venc_headerextension {
    unsigned long    header_extension;
};

struct msm_venc_video_capability {
    unsigned int min_width;
    unsigned int max_width;
    unsigned int min_height;
    unsigned int max_height;
};

struct msm_venc_idrperiod {
    unsigned long idrperiod;
};

struct msm_venc_slice_delivery {
    unsigned long enable;
};

struct msm_venc_hierlayers {
    unsigned int numlayers;
    enum hier_type hier_mode;
};

struct msm_venc_ltrinfo {
    unsigned int enabled;
    unsigned int count;
};

struct msm_venc_perf_level {
    unsigned int perflevel;
};

struct msm_venc_vui_timing_info {
    unsigned int enabled;
};

struct msm_venc_peak_bitrate {
    unsigned int peakbitrate;
};

struct msm_venc_vpx_error_resilience {
    unsigned int enable;
};

enum v4l2_ports {
    CAPTURE_PORT,
    OUTPUT_PORT,
    MAX_PORT
};

struct extradata_buffer_info {
    unsigned long buffer_size;
    char* uaddr;
    int count;
    int size;
    int allocated;
#ifdef USE_ION
    struct venc_ion ion;
#endif
};

enum rc_modes {
    RC_VBR_VFR = BIT(0),
    RC_VBR_CFR = BIT(1),
    RC_CBR_VFR = BIT(2),
    RC_CBR_CFR = BIT(3),
    RC_ALL = (RC_VBR_VFR | RC_VBR_CFR
        | RC_CBR_VFR | RC_CBR_CFR)
};

class venc_dev
{
    public:
        venc_dev(class omx_venc *venc_class); //constructor
        ~venc_dev(); //des

        static void* async_venc_message_thread (void *);
        bool venc_open(OMX_U32);
        void venc_close();
        unsigned venc_stop(void);
        unsigned venc_pause(void);
        unsigned venc_start(void);
        unsigned venc_flush(unsigned);
#ifdef _ANDROID_ICS_
        bool venc_set_meta_mode(bool);
#endif
        unsigned venc_resume(void);
        unsigned venc_start_done(void);
        unsigned venc_stop_done(void);
        unsigned venc_set_message_thread_id(pthread_t);
        bool venc_use_buf(void*, unsigned,unsigned);
        bool venc_free_buf(void*, unsigned);
        bool venc_empty_buf(void *, void *,unsigned,unsigned);
        bool venc_fill_buf(void *, void *,unsigned,unsigned);

        bool venc_get_buf_req(OMX_U32 *,OMX_U32 *,
                OMX_U32 *,OMX_U32);
        bool venc_set_buf_req(OMX_U32 *,OMX_U32 *,
                OMX_U32 *,OMX_U32);
        bool venc_set_param(void *,OMX_INDEXTYPE);
        bool venc_set_config(void *configData, OMX_INDEXTYPE index);
        bool venc_get_profile_level(OMX_U32 *eProfile,OMX_U32 *eLevel);
        bool venc_get_seq_hdr(void *, unsigned, unsigned *);
        bool venc_loaded_start(void);
        bool venc_loaded_stop(void);
        bool venc_loaded_start_done(void);
        bool venc_loaded_stop_done(void);
        bool venc_is_video_session_supported(unsigned long width, unsigned long height);
        bool venc_color_align(OMX_BUFFERHEADERTYPE *buffer, OMX_U32 width,
                        OMX_U32 height);
        bool venc_get_performance_level(OMX_U32 *perflevel);
        bool venc_get_vui_timing_info(OMX_U32 *enabled);
        bool venc_get_peak_bitrate(OMX_U32 *peakbitrate);
        bool venc_get_output_log_flag();
        int venc_output_log_buffers(const char *buffer_addr, int buffer_len);
        int venc_input_log_buffers(OMX_BUFFERHEADERTYPE *buffer, int fd, int plane_offset);
        int venc_extradata_log_buffers(char *buffer_addr);

        struct venc_debug_cap m_debug;
        OMX_U32 m_nDriver_fd;
        bool m_profile_set;
        bool m_level_set;
        int num_planes;
        int etb, ebd, ftb, fbd;
        struct recon_buffer {
            unsigned char* virtual_address;
            int pmem_fd;
            int size;
            int alignment;
            int offset;
#ifdef USE_ION
            int ion_device_fd;
            struct ion_allocation_data alloc_data;
            struct ion_fd_data ion_alloc_fd;
#endif
        };

        int stopped;
        int resume_in_stopped;
        bool m_max_allowed_bitrate_check;
        pthread_t m_tid;
        bool async_thread_created;
        class omx_venc *venc_handle;
        OMX_ERRORTYPE allocate_extradata();
        void free_extradata();
        int append_mbi_extradata(void *, struct msm_vidc_extradata_header*);
        bool handle_extradata(void *, int);
        int venc_set_format(int);
        bool deinterlace_enabled;
        bool hw_overload;
    private:
        OMX_U32                             m_codec;
        struct msm_venc_basecfg             m_sVenc_cfg;
        struct msm_venc_ratectrlcfg         rate_ctrl;
        struct msm_venc_targetbitrate       bitrate;
        struct msm_venc_intraperiod         intra_period;
        struct msm_venc_profile             codec_profile;
        struct msm_venc_profilelevel        profile_level;
        struct msm_venc_switch              set_param;
        struct msm_venc_voptimingcfg        time_inc;
        struct msm_venc_allocatorproperty   m_sInput_buff_property;
        struct msm_venc_allocatorproperty   m_sOutput_buff_property;
        struct msm_venc_sessionqp           session_qp;
        struct msm_venc_initqp              init_qp;
        struct msm_venc_qprange             session_qp_range;
        struct msm_venc_qprange             session_qp_values;
        struct msm_venc_multiclicecfg       multislice;
        struct msm_venc_entropycfg          entropy;
        struct msm_venc_dbcfg               dbkfilter;
        struct msm_venc_intrarefresh        intra_refresh;
        struct msm_venc_headerextension     hec;
        struct msm_venc_voptimingcfg        voptimecfg;
        struct msm_venc_video_capability    capability;
        struct msm_venc_idrperiod           idrperiod;
        struct msm_venc_slice_delivery      slice_mode;
        struct msm_venc_hierlayers          hier_layers;
        struct msm_venc_perf_level          performance_level;
        struct msm_venc_vui_timing_info     vui_timing_info;
        struct msm_venc_peak_bitrate        peak_bitrate;
        struct msm_venc_ltrinfo             ltrinfo;
        struct msm_venc_vpx_error_resilience vpx_err_resilience;

        bool venc_set_profile_level(OMX_U32 eProfile,OMX_U32 eLevel);
        bool venc_set_intra_period(OMX_U32 nPFrames, OMX_U32 nBFrames);
        bool venc_set_target_bitrate(OMX_U32 nTargetBitrate, OMX_U32 config);
        bool venc_set_ratectrl_cfg(OMX_VIDEO_CONTROLRATETYPE eControlRate);
        bool venc_set_session_qp(OMX_U32 i_frame_qp, OMX_U32 p_frame_qp,OMX_U32 b_frame_qp);
        bool venc_set_session_qp_range(OMX_U32 min_qp, OMX_U32 max_qp);
        bool venc_set_encode_framerate(OMX_U32 encode_framerate, OMX_U32 config);
        bool venc_set_intra_vop_refresh(OMX_BOOL intra_vop_refresh);
        bool venc_set_color_format(OMX_COLOR_FORMATTYPE color_format);
        bool venc_validate_profile_level(OMX_U32 *eProfile, OMX_U32 *eLevel);
        bool venc_set_multislice_cfg(OMX_INDEXTYPE codec, OMX_U32 slicesize);
        bool venc_set_entropy_config(OMX_BOOL enable, OMX_U32 i_cabac_level);
        bool venc_set_inloop_filter(OMX_VIDEO_AVCLOOPFILTERTYPE loop_filter);
        bool venc_set_intra_refresh (OMX_VIDEO_INTRAREFRESHTYPE intrarefresh, OMX_U32 nMBs);
        bool venc_set_error_resilience(OMX_VIDEO_PARAM_ERRORCORRECTIONTYPE* error_resilience);
        bool venc_set_voptiming_cfg(OMX_U32 nTimeIncRes);
        void venc_config_print();
        bool venc_set_slice_delivery_mode(OMX_U32 enable);
        bool venc_set_extradata(OMX_U32 extra_data, OMX_BOOL enable);
        bool venc_set_idr_period(OMX_U32 nPFrames, OMX_U32 nIDRPeriod);
        bool venc_reconfig_reqbufs();
        bool venc_set_vpe_rotation(OMX_S32 rotation_angle);
        bool venc_set_deinterlace(OMX_U32 enable);
        bool venc_set_ltrmode(OMX_U32 enable, OMX_U32 count);
        bool venc_enable_initial_qp(QOMX_EXTNINDEX_VIDEO_INITIALQP* initqp);
        bool venc_set_useltr(OMX_U32 frameIdx);
        bool venc_set_markltr(OMX_U32 frameIdx);
        bool venc_set_inband_video_header(OMX_BOOL enable);
        bool venc_set_au_delimiter(OMX_BOOL enable);
        bool venc_set_hier_layers(QOMX_VIDEO_HIERARCHICALCODINGTYPE type, OMX_U32 num_layers);
        bool venc_set_perf_level(QOMX_VIDEO_PERF_LEVEL ePerfLevel);
        bool venc_set_vui_timing_info(OMX_BOOL enable);
        bool venc_set_peak_bitrate(OMX_U32 nPeakBitrate);
        bool venc_set_searchrange();
        bool venc_set_vpx_error_resilience(OMX_BOOL enable);
        bool venc_set_perf_mode(OMX_U32 mode);
        bool venc_set_hybrid_hierp(OMX_U32 layers);
        bool venc_calibrate_gop();
        bool venc_validate_hybridhp_params(OMX_U32 layers, OMX_U32 bFrames, OMX_U32 count, int mode);

#ifdef MAX_RES_1080P
        OMX_U32 pmem_free();
        OMX_U32 pmem_allocate(OMX_U32 size, OMX_U32 alignment, OMX_U32 count);
        OMX_U32 venc_allocate_recon_buffers();
        inline int clip2(int x) {
            x = x -1;
            x = x | x >> 1;
            x = x | x >> 2;
            x = x | x >> 4;
            x = x | x >> 16;
            x = x + 1;
            return x;
        }
#endif
        int metadatamode;
        bool streaming[MAX_PORT];
        bool extradata;
        struct extradata_buffer_info extradata_info;

        pthread_mutex_t pause_resume_mlock;
        pthread_cond_t pause_resume_cond;
        bool paused;
        int color_format;
        bool is_searchrange_set;
        bool enable_mv_narrow_searchrange;
        int supported_rc_modes;
        bool camera_mode_enabled;

        bool venc_empty_batch (OMX_BUFFERHEADERTYPE *buf, unsigned index);
        static const int kMaxBuffersInBatch = 16;
        bool mInputBatchMode;
        struct BatchInfo {
            BatchInfo();
            /* register a buffer and obtain its unique id (v4l2-buf-id)
             */
            int registerBuffer(int bufferId);
            /* retrieve the buffer given its v4l2-buf-id
             */
            int retrieveBufferAt(int v4l2Id);
            bool isPending(int bufferId);

          private:
            static const int kMaxBufs = 64;
            static const int kBufIDFree = -1;
            pthread_mutex_t mLock;
            int mBufMap[64];  // Map with slots for each buffer
            size_t mNumPending;

          public:
            // utility methods to parse entities in batch
            // payload format for batch of 3
            //| fd0 | fd1 | fd2 | off0 | off1 | off2 | len0 | len1 | len2 | dTS0 | dTS1 | dTS2|
            static inline int getFdAt(native_handle_t *, int index);
            static inline int getOffsetAt(native_handle_t *, int index);
            static inline int getSizeAt(native_handle_t *, int index);
            static inline int getTimeStampAt(native_handle_t *, int index);
        };
        BatchInfo mBatchInfo;
};

enum instance_state {
    MSM_VIDC_CORE_UNINIT_DONE = 0x0001,
    MSM_VIDC_CORE_INIT,
    MSM_VIDC_CORE_INIT_DONE,
    MSM_VIDC_OPEN,
    MSM_VIDC_OPEN_DONE,
    MSM_VIDC_LOAD_RESOURCES,
    MSM_VIDC_LOAD_RESOURCES_DONE,
    MSM_VIDC_START,
    MSM_VIDC_START_DONE,
    MSM_VIDC_STOP,
    MSM_VIDC_STOP_DONE,
    MSM_VIDC_RELEASE_RESOURCES,
    MSM_VIDC_RELEASE_RESOURCES_DONE,
    MSM_VIDC_CLOSE,
    MSM_VIDC_CLOSE_DONE,
    MSM_VIDC_CORE_UNINIT,
};
#endif

