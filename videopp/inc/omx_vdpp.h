/*--------------------------------------------------------------------------
Copyright (c) 2013-2014, The Linux Foundation. All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:

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
#ifndef __OMX_VDPP_H__
#define __OMX_VDPP_H__
/*============================================================================
                            O p e n M A X   Component
                                Video Decoder

*//** @file omx_vdpp.h
  This module contains the class definition for openMAX video post-processing component.

*//*========================================================================*/

//////////////////////////////////////////////////////////////////////////////
//                             Include Files
//////////////////////////////////////////////////////////////////////////////

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <cstddef>
#include <math.h>

#ifdef _ANDROID_
#define LOG_TAG "OMX-VDPP"

#ifdef USE_ION
#include <linux/msm_ion.h>
#endif
#include <binder/MemoryHeapBase.h>
#include <ui/ANativeObjectBase.h>
extern "C"{
#include <utils/Log.h>
}
#include <linux/videodev2.h>
#include <poll.h>
#define TIMEOUT 5000


#define DEBUG_PRINT_LOW(x, ...) ALOGV("[Entry] "x, ##__VA_ARGS__)
#define DEBUG_PRINT_HIGH(x, ...) ALOGV("[Step] "x, ##__VA_ARGS__)
#define DEBUG_PRINT_ERROR(x, ...) ALOGE("[Error] "x, ##__VA_ARGS__)

/*#define DEBUG_PRINT_LOW(x, ...)
#define DEBUG_PRINT_HIGH(x, ...)
#define DEBUG_PRINT_ERROR(x, ...) */

#else //_ANDROID_
#define DEBUG_PRINT_LOW printf
#define DEBUG_PRINT_HIGH printf
#define DEBUG_PRINT_ERROR printf
#endif // _ANDROID_

#if defined (_ANDROID_HONEYCOMB_) || defined (_ANDROID_ICS_)
#include <media/hardware/HardwareAPI.h>
#endif

#include <unistd.h>

#if defined (_ANDROID_ICS_)
#include <gralloc_priv.h>
#endif

#include <pthread.h>
#ifndef PC_DEBUG
#include <semaphore.h>
#endif
#include "OMX_Core.h"
#include "OMX_QCOMExtns.h"
#include "OMX_CoreExt.h"
#include "OMX_IndexExt.h"
#include "qc_omx_component.h"
#include <linux/android_pmem.h>
#include <dlfcn.h>

#include <media/msm_vpu.h>
extern "C" {
  OMX_API void * get_omx_component_factory_fn(void); //used by qc_omx_core
}

#ifdef _ANDROID_
    using namespace android;
#endif // _ANDROID_
//////////////////////////////////////////////////////////////////////////////
//                       Module specific globals
//////////////////////////////////////////////////////////////////////////////
#define OMX_SPEC_VERSION  0x00000101

// #define OUTPUT_BUFFER_LOG 1
// #define INPUT_BUFFER_LOG  1
//////////////////////////////////////////////////////////////////////////////
//               Macros
//////////////////////////////////////////////////////////////////////////////
#define PrintFrameHdr(bufHdr) DEBUG_PRINT("bufHdr %x buf %x size %d TS %d\n",\
                       (unsigned) bufHdr,\
                       (unsigned)((OMX_BUFFERHEADERTYPE *)bufHdr)->pBuffer,\
                       (unsigned)((OMX_BUFFERHEADERTYPE *)bufHdr)->nFilledLen,\
                       (unsigned)((OMX_BUFFERHEADERTYPE *)bufHdr)->nTimeStamp)

// BitMask Management logic
#define BITS_PER_BYTE        32
#define BITMASK_SIZE(mIndex) (((mIndex) + BITS_PER_BYTE - 1)/BITS_PER_BYTE)
#define BITMASK_OFFSET(mIndex) ((mIndex)/BITS_PER_BYTE)
#define BITMASK_FLAG(mIndex) (1 << ((mIndex) % BITS_PER_BYTE))
#define BITMASK_CLEAR(mArray,mIndex) (mArray)[BITMASK_OFFSET(mIndex)] \
        &=  ~(BITMASK_FLAG(mIndex))
#define BITMASK_SET(mArray,mIndex)  (mArray)[BITMASK_OFFSET(mIndex)] \
        |=  BITMASK_FLAG(mIndex)
#define BITMASK_PRESENT(mArray,mIndex) ((mArray)[BITMASK_OFFSET(mIndex)] \
        & BITMASK_FLAG(mIndex))
#define BITMASK_ABSENT(mArray,mIndex) (((mArray)[BITMASK_OFFSET(mIndex)] \
        & BITMASK_FLAG(mIndex)) == 0x0)
#define BITMASK_PRESENT(mArray,mIndex) ((mArray)[BITMASK_OFFSET(mIndex)] \
        & BITMASK_FLAG(mIndex))
#define BITMASK_ABSENT(mArray,mIndex) (((mArray)[BITMASK_OFFSET(mIndex)] \
        & BITMASK_FLAG(mIndex)) == 0x0)

#define OMX_CORE_CONTROL_CMDQ_SIZE   200//100
#define OMX_CORE_QCIF_HEIGHT         144
#define OMX_CORE_QCIF_WIDTH          176
#define OMX_CORE_VGA_HEIGHT          480
#define OMX_CORE_VGA_WIDTH           640
#define OMX_CORE_WVGA_HEIGHT         480
#define OMX_CORE_WVGA_WIDTH          800

#ifdef _ANDROID_
#define MAX_NUM_INPUT_OUTPUT_BUFFERS 32
#endif

#define OMX_FRAMEINFO_EXTRADATA 0x00010000
#define OMX_INTERLACE_EXTRADATA 0x00020000
#define OMX_TIMEINFO_EXTRADATA  0x00040000
#define OMX_PORTDEF_EXTRADATA   0x00080000
#define OMX_EXTNUSER_EXTRADATA  0x00100000
#define DRIVER_EXTRADATA_MASK   0x0000FFFF

#define OMX_INTERLACE_EXTRADATA_SIZE ((sizeof(OMX_OTHER_EXTRADATATYPE) +\
                                       sizeof(OMX_STREAMINTERLACEFORMAT) + 3)&(~3))
#define OMX_FRAMEINFO_EXTRADATA_SIZE ((sizeof(OMX_OTHER_EXTRADATATYPE) +\
                                       sizeof(OMX_QCOM_EXTRADATA_FRAMEINFO) + 3)&(~3))
#define OMX_PORTDEF_EXTRADATA_SIZE ((sizeof(OMX_OTHER_EXTRADATATYPE) +\
                                       sizeof(OMX_PARAM_PORTDEFINITIONTYPE) + 3)&(~3))

#define VALID_TS(ts)      ((ts < LLONG_MAX)? true : false)

/* STATUS CODES */
/* Base value for status codes */
#define VDPP_S_BASE	0x40000000
/* Success */
#define VDPP_S_SUCCESS	(VDPP_S_BASE)
/* General failure */
#define VDPP_S_EFAIL	(VDPP_S_BASE + 1)
/* Fatal irrecoverable  failure. Need to  tear down session. */
#define VDPP_S_EFATAL   (VDPP_S_BASE + 2)
/* Error detected in the passed  parameters */
#define VDPP_S_EBADPARAM	(VDPP_S_BASE + 3)
/* Command called in invalid  state. */
#define VDPP_S_EINVALSTATE	(VDPP_S_BASE + 4)
 /* Insufficient OS  resources - thread, memory etc. */
#define VDPP_S_ENOSWRES	(VDPP_S_BASE + 5)
 /* Insufficient HW resources -  core capacity  maxed  out. */
#define VDPP_S_ENOHWRES	(VDPP_S_BASE + 6)
/* Invalid command  called */
#define VDPP_S_EINVALCMD	(VDPP_S_BASE + 7)
/* Command timeout. */
#define VDPP_S_ETIMEOUT	(VDPP_S_BASE + 8)
/* Pre-requirement is  not met for API. */
#define VDPP_S_ENOPREREQ	(VDPP_S_BASE + 9)
/* Command queue is full. */
#define VDPP_S_ECMDQFULL	(VDPP_S_BASE + 10)
/* Command is not supported  by this driver */
#define VDPP_S_ENOTSUPP	(VDPP_S_BASE + 11)
/* Command is not implemented by thedriver. */
#define VDPP_S_ENOTIMPL	(VDPP_S_BASE + 12)
/* Command is not implemented by the driver.  */
#define VDPP_S_BUSY	(VDPP_S_BASE + 13)
#define VDPP_S_INPUT_BITSTREAM_ERR (VDPP_S_BASE + 14)

#define VDPP_INTF_VER	1
#define VDPP_MSG_BASE	0x0000000
/* Codes to identify asynchronous message responses and events that driver
  wants to communicate to the app.*/
#define VDPP_MSG_INVALID	(VDPP_MSG_BASE + 0)
#define VDPP_MSG_RESP_INPUT_BUFFER_DONE	(VDPP_MSG_BASE + 1)
#define VDPP_MSG_RESP_OUTPUT_BUFFER_DONE	(VDPP_MSG_BASE + 2)
#define VDPP_MSG_RESP_INPUT_FLUSHED	(VDPP_MSG_BASE + 3)
#define VDPP_MSG_RESP_OUTPUT_FLUSHED	(VDPP_MSG_BASE + 4)
#define VDPP_MSG_RESP_FLUSH_INPUT_DONE	(VDPP_MSG_BASE + 5)
#define VDPP_MSG_RESP_FLUSH_OUTPUT_DONE	(VDPP_MSG_BASE + 6)
#define VDPP_MSG_RESP_START_DONE	(VDPP_MSG_BASE + 7)
#define VDPP_MSG_RESP_STOP_DONE	(VDPP_MSG_BASE + 8)
#define VDPP_MSG_RESP_PAUSE_DONE	(VDPP_MSG_BASE + 9)
#define VDPP_MSG_RESP_RESUME_DONE	(VDPP_MSG_BASE + 10)
#define VDPP_MSG_RESP_RESOURCE_LOADED	(VDPP_MSG_BASE + 11)
#define VDPP_EVT_RESOURCES_LOST	(VDPP_MSG_BASE + 12)
#define VDPP_MSG_EVT_CONFIG_CHANGED	(VDPP_MSG_BASE + 13)
#define VDPP_MSG_EVT_HW_ERROR	(VDPP_MSG_BASE + 14)
#define VDPP_MSG_EVT_INFO_CONFIG_CHANGED	(VDPP_MSG_BASE + 15)
#define VDPP_MSG_EVT_INFO_FIELD_DROPPED	(VDPP_MSG_BASE + 16)

#define VDPP_MSG_EVT_ACTIVE_REGION_DETECTION_STATUS	(VDPP_MSG_BASE + 17)


#define VP_INPUT_BUFFER_COUNT_INTERLACE 4
#define VP_OUTPUT_BUFFER_COUNT 4

#define VDPP_SESSION 1
//#define STUB_VPU 1
//#define DEFAULT_EXTRADATA (OMX_FRAMEINFO_EXTRADATA|OMX_INTERLACE_EXTRADATA)

enum port_indexes
{
    OMX_CORE_INPUT_PORT_INDEX        =0,
    OMX_CORE_OUTPUT_PORT_INDEX       =1
};
#ifdef USE_ION
struct vdpp_ion
{
    int ion_device_fd;
    struct ion_fd_data fd_ion_data;
    struct ion_allocation_data ion_alloc_data;
};
#endif

struct extradata_buffer_info {
	int buffer_size;
	char* uaddr;
	int count;
	int size;
#ifdef USE_ION
	struct vdpp_ion ion;
#endif
};

struct vdpp_picsize {
	uint32_t frame_width;
	uint32_t frame_height;
	uint32_t stride;
	uint32_t scan_lines;
};

enum vdpp_interlaced_format {
	VDPP_InterlaceFrameProgressive = 0x1,
	VDPP_InterlaceInterleaveFrameTopFieldFirst = 0x2,
	VDPP_InterlaceInterleaveFrameBottomFieldFirst = 0x4
};

enum vdpp_buffer {
	VDPP_BUFFER_TYPE_INPUT,
	VDPP_BUFFER_TYPE_OUTPUT
};

struct vdpp_allocatorproperty {
	enum vdpp_buffer buffer_type;
	uint32_t mincount;
	uint32_t maxcount;
	uint32_t actualcount;
	size_t buffer_size;
	uint32_t alignment;
	uint32_t buf_poolid;
	size_t frame_size;
};

struct vdpp_bufferpayload {
	void *bufferaddr;
	size_t buffer_len;
	int pmem_fd;
	size_t offset;
	size_t mmaped_size;
};

struct vdpp_framesize {
	uint32_t   left;
	uint32_t   top;
	uint32_t   right;
	uint32_t   bottom;
};

struct vdpp_setbuffer_cmd {
	enum vdpp_buffer buffer_type;
	struct vdpp_bufferpayload buffer;
};

struct vdpp_output_frameinfo {
	void *bufferaddr;
	size_t offset;
	size_t len;
	uint32_t flags;
	int64_t time_stamp;
	void *client_data;
	struct vdpp_framesize framesize;

};

union vdpp_msgdata {
    struct v4l2_rect ar_result;
	struct vdpp_output_frameinfo output_frame;
	void *input_frame_clientdata;
};

struct vdpp_msginfo {
	uint32_t status_code;
	uint32_t msgcode;
	union vdpp_msgdata msgdata;
	size_t msgdatasize;
};

struct vdpp_framerate {
	unsigned long fps_denominator;
	unsigned long fps_numerator;
};

#ifdef STUB_VPU
typedef struct vcap_etb_ftb_info
{
    int etb_cnt; // only simulate 1-to-1
    __u32	etb_index;
	size_t  etb_len;
    int ftb_cnt;
    __u32   ftb_index;
	size_t  ftb_len;
}vcap_etb_ftb_info;
#endif

struct video_vpu_context
{
    int video_vpu_fd;
    uint32_t output_format;
    enum   v4l2_field interlace;
    struct vdpp_picsize video_resolution_input;
    struct vdpp_picsize video_resolution_output;
    struct vdpp_allocatorproperty ip_buf;
    struct vdpp_allocatorproperty op_buf;
    struct vdpp_bufferpayload *ptr_inputbuffer;
    struct vdpp_bufferpayload *ptr_outputbuffer;
    struct vdpp_output_frameinfo *ptr_respbuffer;
#ifdef USE_ION
    struct vdpp_ion *ip_buf_ion_info;
    struct vdpp_ion *op_buf_ion_info;
#endif
    struct vdpp_framerate frame_rate;
    bool timestamp_adjust;
    char kind[128];
	struct extradata_buffer_info extradata_info;
    int input_num_planes;
    int output_num_planes;
    double input_bytesperpixel[2];
    double output_bytesperpixel[2];
    int sessionsSupported;
    int sessionAttached;
#ifdef STUB_VPU
    bool thread_exit;
    vcap_etb_ftb_info etb_ftb_info;
    sem_t             async_lock;
#endif
};

typedef struct _VdppExtensionData_t
{
    bool                           activeRegionDetectionDirtyFlag;
    QOMX_ACTIVEREGIONDETECTIONTYPE activeRegionDetection;
    bool                                  activeRegionDetectionStatusDirtyFlag;
    QOMX_ACTIVEREGIONDETECTION_STATUSTYPE activeRegionDetectionStatus;
    bool                  scalingModeDirtyFlag;
    QOMX_SCALINGMODETYPE  scalingMode;
    bool                    noiseReductionDirtyFlag;
    QOMX_NOISEREDUCTIONTYPE noiseReduction;
    bool                      imageEnhancementDirtyFlag;
    QOMX_IMAGEENHANCEMENTTYPE imageEnhancement;
} VdppExtensionData_t;

// OMX video decoder class
class omx_vdpp: public qc_omx_component
{

public:
    omx_vdpp();  // constructor
    virtual ~omx_vdpp();  // destructor

    static int async_message_process (void *context, void* message);
    static void process_event_cb(void *ctxt,unsigned char id);
    struct omx_event
    {
        unsigned param1;
        unsigned param2;
        unsigned id;
    };
    struct omx_cmd_queue
    {
        omx_event m_q[OMX_CORE_CONTROL_CMDQ_SIZE];
        unsigned m_read;
        unsigned m_write;
        unsigned m_size;

        omx_cmd_queue();
        ~omx_cmd_queue();
        bool insert_entry(unsigned p1, unsigned p2, unsigned id);
        bool pop_entry(unsigned *p1,unsigned *p2, unsigned *id);
        // get msgtype of the first ele from the queue
        unsigned get_q_msg_type();

    };

    OMX_ERRORTYPE allocate_buffer(
                                   OMX_HANDLETYPE hComp,
                                   OMX_BUFFERHEADERTYPE **bufferHdr,
                                   OMX_U32 port,
                                   OMX_PTR appData,
                                   OMX_U32 bytes
                                  );


    OMX_ERRORTYPE component_deinit(OMX_HANDLETYPE hComp);

    OMX_ERRORTYPE component_init(OMX_STRING role);

    OMX_ERRORTYPE component_role_enum(
                                       OMX_HANDLETYPE hComp,
                                       OMX_U8 *role,
                                       OMX_U32 index
                                      );

    OMX_ERRORTYPE component_tunnel_request(
                                            OMX_HANDLETYPE hComp,
                                            OMX_U32 port,
                                            OMX_HANDLETYPE  peerComponent,
                                            OMX_U32 peerPort,
                                            OMX_TUNNELSETUPTYPE *tunnelSetup
                                           );

    OMX_ERRORTYPE empty_this_buffer(
                                     OMX_HANDLETYPE hComp,
                                     OMX_BUFFERHEADERTYPE *buffer
                                    );



    OMX_ERRORTYPE fill_this_buffer(
                                    OMX_HANDLETYPE hComp,
                                    OMX_BUFFERHEADERTYPE *buffer
                                   );


    OMX_ERRORTYPE free_buffer(
                              OMX_HANDLETYPE hComp,
                              OMX_U32 port,
                              OMX_BUFFERHEADERTYPE *buffer
                              );

    OMX_ERRORTYPE get_component_version(
                                        OMX_HANDLETYPE hComp,
                                        OMX_STRING componentName,
                                        OMX_VERSIONTYPE *componentVersion,
                                        OMX_VERSIONTYPE *specVersion,
                                        OMX_UUIDTYPE *componentUUID
                                        );

    OMX_ERRORTYPE get_config(
                              OMX_HANDLETYPE hComp,
                              OMX_INDEXTYPE configIndex,
                              OMX_PTR configData
                             );

    OMX_ERRORTYPE get_extension_index(
                                      OMX_HANDLETYPE hComp,
                                      OMX_STRING paramName,
                                      OMX_INDEXTYPE *indexType
                                      );

    OMX_ERRORTYPE get_parameter(OMX_HANDLETYPE hComp,
                                OMX_INDEXTYPE  paramIndex,
                                OMX_PTR        paramData);

    OMX_ERRORTYPE get_state(OMX_HANDLETYPE hComp,
                            OMX_STATETYPE *state);



    OMX_ERRORTYPE send_command(OMX_HANDLETYPE  hComp,
                               OMX_COMMANDTYPE cmd,
                               OMX_U32         param1,
                               OMX_PTR         cmdData);


    OMX_ERRORTYPE set_callbacks(OMX_HANDLETYPE   hComp,
                                OMX_CALLBACKTYPE *callbacks,
                                OMX_PTR          appData);

    OMX_ERRORTYPE set_config(OMX_HANDLETYPE hComp,
                             OMX_INDEXTYPE  configIndex,
                             OMX_PTR        configData);

    OMX_ERRORTYPE set_parameter(OMX_HANDLETYPE hComp,
                                OMX_INDEXTYPE  paramIndex,
                                OMX_PTR        paramData);

    OMX_ERRORTYPE use_buffer(OMX_HANDLETYPE      hComp,
                             OMX_BUFFERHEADERTYPE **bufferHdr,
                             OMX_U32              port,
                             OMX_PTR              appData,
                             OMX_U32              bytes,
                             OMX_U8               *buffer);

    OMX_ERRORTYPE  use_input_heap_buffers(
                          OMX_HANDLETYPE            hComp,
                          OMX_BUFFERHEADERTYPE** bufferHdr,
                          OMX_U32                   port,
                          OMX_PTR                   appData,
                          OMX_U32                   bytes,
                          OMX_U8*                   buffer);

    OMX_ERRORTYPE  use_input_buffers(
                          OMX_HANDLETYPE            hComp,
                          OMX_BUFFERHEADERTYPE** bufferHdr,
                          OMX_U32                   port,
                          OMX_PTR                   appData,
                          OMX_U32                   bytes,
                          OMX_U8*                   buffer);

    OMX_ERRORTYPE use_EGL_image(OMX_HANDLETYPE     hComp,
                                OMX_BUFFERHEADERTYPE **bufferHdr,
                                OMX_U32              port,
                                OMX_PTR              appData,
                                void *               eglImage);

    void complete_pending_buffer_done_cbs();
    struct video_vpu_context drv_ctx;
    int update_resolution(uint32_t width, uint32_t height, uint32_t stride, uint32_t scan_lines);

    int  m_pipe_in;
    int  m_pipe_out;
    int  m_ctrl_in;
    int  m_ctrl_out;

    pthread_t msg_thread_id;
    pthread_t async_thread_id;
    omx_cmd_queue m_index_q_ftb;
    omx_cmd_queue m_index_q_etb;
    bool m_ar_callback_setup;
private:
    // Bit Positions
    enum flags_bit_positions
    {
        // Defer transition to IDLE
        OMX_COMPONENT_IDLE_PENDING            =0x1,
        // Defer transition to LOADING
        OMX_COMPONENT_LOADING_PENDING         =0x2,
        // First  Buffer Pending
        OMX_COMPONENT_FIRST_BUFFER_PENDING    =0x3,
        // Second Buffer Pending
        OMX_COMPONENT_SECOND_BUFFER_PENDING   =0x4,
        // Defer transition to Enable
        OMX_COMPONENT_INPUT_ENABLE_PENDING    =0x5,
        // Defer transition to Enable
        OMX_COMPONENT_OUTPUT_ENABLE_PENDING   =0x6,
        // Defer transition to Disable
        OMX_COMPONENT_INPUT_DISABLE_PENDING   =0x7,
        // Defer transition to Disable
        OMX_COMPONENT_OUTPUT_DISABLE_PENDING  =0x8,
        //defer flush notification
        OMX_COMPONENT_OUTPUT_FLUSH_PENDING    =0x9,
        OMX_COMPONENT_INPUT_FLUSH_PENDING    =0xA,
        OMX_COMPONENT_PAUSE_PENDING          =0xB,
        OMX_COMPONENT_EXECUTE_PENDING        =0xC,
        OMX_COMPONENT_OUTPUT_FLUSH_IN_DISABLE_PENDING =0xD,
        OMX_COMPONENT_DISABLE_OUTPUT_DEFERRED=0xE
    };

    // Deferred callback identifiers
    enum
    {
        //Event Callbacks from the vidpp component thread context
        OMX_COMPONENT_GENERATE_EVENT       = 0x1,
        //Buffer Done callbacks from the vidpp component thread context
        OMX_COMPONENT_GENERATE_BUFFER_DONE = 0x2,
        //Frame Done callbacks from the vidpp component thread context
        OMX_COMPONENT_GENERATE_FRAME_DONE  = 0x3,
        //Buffer Done callbacks from the vidpp component thread context
        OMX_COMPONENT_GENERATE_FTB         = 0x4,
        //Frame Done callbacks from the vidpp component thread context
        OMX_COMPONENT_GENERATE_ETB         = 0x5,
        //Command
        OMX_COMPONENT_GENERATE_COMMAND     = 0x6,
        //Push-Pending Buffers
        OMX_COMPONENT_PUSH_PENDING_BUFS    = 0x7,
        // Empty Buffer Done callbacks
        OMX_COMPONENT_GENERATE_EBD         = 0x8,
        //Flush Event Callbacks from the vidpp component thread context
        OMX_COMPONENT_GENERATE_EVENT_FLUSH       = 0x9,
        OMX_COMPONENT_GENERATE_EVENT_INPUT_FLUSH = 0x0A,
        OMX_COMPONENT_GENERATE_EVENT_OUTPUT_FLUSH = 0x0B,
        OMX_COMPONENT_GENERATE_FBD = 0xc,
        OMX_COMPONENT_GENERATE_START_DONE = 0xD,
        OMX_COMPONENT_GENERATE_PAUSE_DONE = 0xE,
        OMX_COMPONENT_GENERATE_RESUME_DONE = 0xF,
        OMX_COMPONENT_GENERATE_STOP_DONE = 0x10,
        OMX_COMPONENT_GENERATE_HARDWARE_ERROR = 0x11,
        OMX_COMPONENT_GENERATE_ETB_ARBITRARY = 0x12,
        OMX_COMPONENT_GENERATE_PORT_RECONFIG = 0x13,
        OMX_COMPONENT_GENERATE_EOS_DONE = 0x14,
        OMX_COMPONENT_GENERATE_INFO_PORT_RECONFIG = 0x15,
        OMX_COMPONENT_GENERATE_INFO_FIELD_DROPPED = 0x16,
        OMX_COMPONENT_GENERATE_UNSUPPORTED_SETTING = 0x17,

        // extensions
        OMX_COMPONENT_GENERATE_ACTIVE_REGION_DETECTION_STATUS = 0x18,
    };

    enum v4l2_ports
    {
        CAPTURE_PORT,
        OUTPUT_PORT,
        MAX_PORT
    };

#ifdef _ANDROID_
    struct ts_entry
    {
        OMX_TICKS timestamp;
        bool valid;
    };

    struct ts_arr_list
    {
        ts_entry m_ts_arr_list[MAX_NUM_INPUT_OUTPUT_BUFFERS];

        ts_arr_list();
        ~ts_arr_list();

        bool insert_ts(OMX_TICKS ts);
        bool pop_min_ts(OMX_TICKS &ts);
        bool reset_ts_list();
    };
#endif

    bool allocate_done(void);
    bool allocate_input_done(void);
    bool allocate_output_done(void);

    OMX_ERRORTYPE free_input_buffer(OMX_BUFFERHEADERTYPE *bufferHdr);
    OMX_ERRORTYPE free_output_buffer(OMX_BUFFERHEADERTYPE *bufferHdr);
    void free_output_buffer_header();
    void free_input_buffer_header();

    OMX_ERRORTYPE allocate_input_heap_buffer(OMX_HANDLETYPE       hComp,
                                             OMX_BUFFERHEADERTYPE **bufferHdr,
                                             OMX_U32              port,
                                             OMX_PTR              appData,
                                             OMX_U32              bytes);

    OMX_ERRORTYPE allocate_input_buffer(OMX_HANDLETYPE       hComp,
                                        OMX_BUFFERHEADERTYPE **bufferHdr,
                                        OMX_U32              port,
                                        OMX_PTR              appData,
                                        OMX_U32              bytes);

    OMX_ERRORTYPE allocate_output_buffer(OMX_HANDLETYPE       hComp,
                                         OMX_BUFFERHEADERTYPE **bufferHdr,
                                         OMX_U32 port,OMX_PTR appData,
                                         OMX_U32              bytes);
    OMX_ERRORTYPE use_output_buffer(OMX_HANDLETYPE hComp,
                                   OMX_BUFFERHEADERTYPE   **bufferHdr,
                                   OMX_U32                port,
                                   OMX_PTR                appData,
                                   OMX_U32                bytes,
                                   OMX_U8                 *buffer);

    OMX_ERRORTYPE allocate_output_headers();
    bool execute_omx_flush(OMX_U32);
    bool execute_output_flush();
    bool execute_input_flush();
    OMX_ERRORTYPE empty_buffer_done(OMX_HANDLETYPE hComp,
                                    OMX_BUFFERHEADERTYPE * buffer);

    OMX_ERRORTYPE fill_buffer_done(OMX_HANDLETYPE hComp,
                                    OMX_BUFFERHEADERTYPE * buffer);
    OMX_ERRORTYPE empty_this_buffer_proxy(OMX_HANDLETYPE       hComp,
                                        OMX_BUFFERHEADERTYPE *buffer);

    OMX_ERRORTYPE fill_this_buffer_proxy(OMX_HANDLETYPE       hComp,
                                       OMX_BUFFERHEADERTYPE *buffer);
    bool release_done();

    bool release_output_done();
    bool release_input_done();
    OMX_ERRORTYPE get_buffer_req(vdpp_allocatorproperty *buffer_prop);
    OMX_ERRORTYPE set_buffer_req(vdpp_allocatorproperty *buffer_prop);
    OMX_ERRORTYPE start_port_reconfig();

    int stream_off(OMX_U32 port);
    void adjust_timestamp(OMX_S64 &act_timestamp);
    void set_frame_rate(OMX_S64 act_timestamp);
    OMX_ERRORTYPE enable_extradata(OMX_U32 requested_extradata, bool enable = true);
    OMX_ERRORTYPE update_portdef(OMX_PARAM_PORTDEFINITIONTYPE *portDefn);
    bool align_pmem_buffers(int pmem_fd, OMX_U32 buffer_size,
                            OMX_U32 alignment);
#ifdef USE_ION
    int alloc_map_ion_memory(OMX_U32 buffer_size,
              OMX_U32 alignment, struct ion_allocation_data *alloc_data,
              struct ion_fd_data *fd_data,int flag);
    void free_ion_memory(struct vdpp_ion *buf_ion_info);
#endif


    OMX_ERRORTYPE send_command_proxy(OMX_HANDLETYPE  hComp,
                                     OMX_COMMANDTYPE cmd,
                                     OMX_U32         param1,
                                     OMX_PTR         cmdData);
    bool post_event( unsigned int p1,
                     unsigned int p2,
                     unsigned int id
                    );

    void setFormatParams(int pixelFormat, double bytesperpixel[], unsigned char *planesCount);

    int openInput(const char* inputName);

    /**
      * int clip2 - return an integer value in 2 to the nth power
      * legacy code from video decoder  (rounded up till 256)
      *
      */
    inline int clip2(int x)
    {
        x = x -1;
        x = x | x >> 1;
        x = x | x >> 2;
        x = x | x >> 4;
        x = x | x >> 16;
        x = x + 1;
        return x;
    }
    /**
      * int paddedFrameWidth - return frame width in a multiple of 128 (rounded up).
      *
      * @width - the original frame width.
      */
    inline int paddedFrameWidth128(int width)
    {
        return  (((width + 127) / 128 )* 128);
    }

    /**
      * int paddedFrameWidth32 - return frame width in a multiple of 32 (rounded up).
      *
      * @width - the original frame width.
      */
    inline int paddedFrameWidth32(int width)
    {
        return  (((width + 31) / 32 )* 32);
    }

    /**
      * int roundToNearestInt - round to nearest integer value.
      *
      * @value - the decimal value to be rounded to nearest integer.
      */
    inline int roundToNearestInt(double value)
    {
	    if((ceil(value) - value) > 0.5)
		    return floor(value);
	    else
		    return ceil(value);
    }

    inline void omx_report_error ()
    {
        if (m_cb.EventHandler && !m_error_propogated)
        {
            ALOGE("\nERROR: Sending OMX_EventError to Client");
            m_error_propogated = true;
            m_cb.EventHandler(&m_cmp,m_app_data,
                  OMX_EventError,OMX_ErrorHardware,0,NULL);
        }
    }

    inline void omx_report_unsupported_setting ()
    {
        if (m_cb.EventHandler && !m_error_propogated)
        {
            DEBUG_PRINT_ERROR(
               "\nERROR: Sending OMX_ErrorUnsupportedSetting to Client");
            m_error_propogated = true;
            m_cb.EventHandler(&m_cmp,m_app_data,
                  OMX_EventError,OMX_ErrorUnsupportedSetting,0,NULL);
        }
    }

#if defined (_ANDROID_HONEYCOMB_) || defined (_ANDROID_ICS_)
    OMX_ERRORTYPE use_android_native_buffer(OMX_IN OMX_HANDLETYPE hComp, OMX_PTR data);
#endif
#if defined (_ANDROID_ICS_)
    struct nativebuffer{
        native_handle_t *nativehandle;
	private_handle_t *privatehandle;
        int inuse;
    };
    nativebuffer native_buffer[MAX_NUM_INPUT_OUTPUT_BUFFERS];
#endif


    //*************************************************************
    //*******************MEMBER VARIABLES *************************
    //*************************************************************
    pthread_mutex_t       m_lock;
    //sem to handle the minimum procesing of commands
    sem_t                 m_cmd_lock;
    bool              m_error_propogated;

    // OMX State
    OMX_STATETYPE m_state;
    // Application data
    OMX_PTR m_app_data;
    // Application callbacks
    OMX_CALLBACKTYPE m_cb;
    OMX_PRIORITYMGMTTYPE m_priority_mgm ;
    OMX_PARAM_BUFFERSUPPLIERTYPE m_buffer_supplier;
    // fill this buffer queue
    omx_cmd_queue         m_ftb_q;
    // Command Q for rest of the events
    omx_cmd_queue         m_cmd_q;
    omx_cmd_queue         m_etb_q;
    // Input memory pointer
    OMX_BUFFERHEADERTYPE  *m_inp_mem_ptr;
    // Output memory pointer
    OMX_BUFFERHEADERTYPE  *m_out_mem_ptr;
    // number of input bitstream error frame count
    unsigned int m_inp_err_count;
#ifdef _ANDROID_
    // Timestamp list
    ts_arr_list           m_timestamp_list;
#endif

    bool input_flush_progress;
    bool output_flush_progress;
    bool input_use_buffer;
    bool output_use_buffer;
    bool ouput_egl_buffers;
    OMX_BOOL m_use_output_pmem;
    OMX_BOOL m_out_mem_region_smi;
    OMX_BOOL m_out_pvt_entry_pmem;

    int pending_input_buffers;
    int pending_output_buffers;

    int input_qbuf_count;
    int input_dqbuf_count;
    int output_qbuf_count;
    int output_dqbuf_count;

#ifdef OUTPUT_BUFFER_LOG
    int output_buffer_write_counter;
    int input_buffer_write_counter;
#endif
    // bitmask array size for output side
    unsigned int m_out_bm_count;
    // bitmask array size for input side
    unsigned int m_inp_bm_count;
    //Input port Populated
    OMX_BOOL m_inp_bPopulated;
    //Output port Populated
    OMX_BOOL m_out_bPopulated;
    // encapsulate the waiting states.
    unsigned int m_flags;

    // store I/P PORT state
    OMX_BOOL m_inp_bEnabled;
    // store O/P PORT state
    OMX_BOOL m_out_bEnabled;
    OMX_U32 m_in_alloc_cnt;
    OMX_U8                m_cRole[OMX_MAX_STRINGNAME_SIZE];
    // Platform specific details
    OMX_QCOM_PLATFORM_PRIVATE_LIST      *m_platform_list;
    OMX_QCOM_PLATFORM_PRIVATE_ENTRY     *m_platform_entry;
    OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO *m_pmem_info;

    /*Variables for arbitrary Byte parsing support*/
    omx_cmd_queue m_input_pending_q;
    omx_cmd_queue m_input_free_q;

    OMX_BUFFERHEADERTYPE  *psource_frame;
    OMX_BUFFERHEADERTYPE  *pdest_frame;
    OMX_BUFFERHEADERTYPE  *m_inp_heap_ptr;
    OMX_BUFFERHEADERTYPE  **m_phdr_pmem_ptr;
    unsigned int m_heap_inp_bm_count;

    OMX_S64 prev_ts;
    bool rst_prev_ts;
    OMX_U32 frm_int;

    struct vdpp_allocatorproperty op_buf_rcnfg;
    bool in_reconfig;
    OMX_U32 client_extradata;
#ifdef _ANDROID_
    bool m_debug_timestamp;
    bool perf_flag;
    OMX_U32 proc_frms, latency;
    bool m_enable_android_native_buffers;
    bool m_use_android_native_buffers;
    bool m_debug_extradata;
    bool m_debug_concealedmb;
#endif

    OMX_PARAM_PORTDEFINITIONTYPE m_port_def;

    int capture_capability;
    int output_capability;
    bool streaming[MAX_PORT];
    OMX_CONFIG_RECTTYPE rectangle;
	int prev_n_filled_len;

    bool msg_thread_created;
    bool async_thread_created;

    unsigned int m_fill_output_msg;
    bool client_set_fps;
    bool interlace_user_flag;
    // OMX extensions
    VdppExtensionData_t mExtensionData;
};
#endif // __OMX_VDPP_H__
