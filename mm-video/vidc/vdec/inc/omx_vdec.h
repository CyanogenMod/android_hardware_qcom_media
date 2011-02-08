/*--------------------------------------------------------------------------
Copyright (c) 2010, Code Aurora Forum. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of Code Aurora nor
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
#ifndef __OMX_VDEC_H__
#define __OMX_VDEC_H__
/*============================================================================
                            O p e n M A X   Component
                                Video Decoder

*//** @file comx_vdec.h
  This module contains the class definition for openMAX decoder component.

*//*========================================================================*/

//////////////////////////////////////////////////////////////////////////////
//                             Include Files
//////////////////////////////////////////////////////////////////////////////

#include<stdlib.h>

#include <stdio.h>

#ifdef _ANDROID_
#include <binder/MemoryHeapBase.h>
extern "C"{
#include<utils/Log.h>
}
//#define LOG_TAG "OMX-VDEC-720P"
#ifdef ENABLE_DEBUG_LOW
#define DEBUG_PRINT_LOW LOGE
#else
#define DEBUG_PRINT_LOW
#endif
#ifdef ENABLE_DEBUG_HIGH
#define DEBUG_PRINT_HIGH LOGE
#else
#define DEBUG_PRINT_HIGH
#endif
#ifdef ENABLE_DEBUG_ERROR
#define DEBUG_PRINT_ERROR LOGE
#else
#define DEBUG_PRINT_ERROR
#endif
#endif // _ANDROID_

#include <pthread.h>
#ifndef PC_DEBUG
#include <semaphore.h>
#endif
#include "OMX_Core.h"
#include "OMX_QCOMExtns.h"
//#include "vdec.h"
#include "qc_omx_component.h"
//#include "Map.h"
//#include "OmxUtils.h"
#include <linux/msm_vidc_dec.h>
#include "frameparser.h"
#include <linux/android_pmem.h>

extern "C" {
  OMX_API void * get_omx_component_factory_fn(void);
}


#ifdef _ANDROID_
    using namespace android;
    // local pmem heap object
    class VideoHeap : public MemoryHeapBase
    {
    public:
        VideoHeap(size_t size);
        virtual ~VideoHeap();
        int getPmemFd();
        void dispose_pmem();
    private:
        int mPmemFd;
    };
#endif // _ANDROID_
//////////////////////////////////////////////////////////////////////////////
//                       Module specific globals
//////////////////////////////////////////////////////////////////////////////
#define OMX_SPEC_VERSION  0x00000101


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

#define OMX_VIDEO_DEC_NUM_INPUT_BUFFERS   2
#define OMX_VIDEO_DEC_NUM_OUTPUT_BUFFERS  2

#ifdef FEATURE_QTV_WVGA_ENABLE
#define OMX_VIDEO_DEC_INPUT_BUFFER_SIZE   (256*1024)
#else
#define OMX_VIDEO_DEC_INPUT_BUFFER_SIZE   (128*1024)
#endif

#define OMX_CORE_CONTROL_CMDQ_SIZE   100
#define OMX_CORE_QCIF_HEIGHT         144
#define OMX_CORE_QCIF_WIDTH          176
#define OMX_CORE_VGA_HEIGHT          480
#define OMX_CORE_VGA_WIDTH           640
#define OMX_CORE_WVGA_HEIGHT         480
#define OMX_CORE_WVGA_WIDTH          800


struct video_driver_context
{
    int video_driver_fd;
    enum vdec_codec decoder_format;
    enum vdec_output_fromat output_format;
    struct vdec_picsize video_resoultion;
    struct vdec_allocatorproperty input_buffer;
    struct vdec_allocatorproperty output_buffer;
    struct vdec_bufferpayload *ptr_inputbuffer;
    struct vdec_bufferpayload *ptr_outputbuffer;
    struct vdec_output_frameinfo *ptr_respbuffer;
    char kind[128];
};

class OmxUtils;

// OMX video decoder class
class omx_vdec: public qc_omx_component
{

public:
    omx_vdec();  // constructor
    virtual ~omx_vdec();  // destructor

    static int async_message_process (void *context, void* message);
    static void process_event_cb(void *ctxt,unsigned char id);

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


    OMX_ERRORTYPE use_EGL_image(OMX_HANDLETYPE     hComp,
                                OMX_BUFFERHEADERTYPE **bufferHdr,
                                OMX_U32              port,
                                OMX_PTR              appData,
                                void *               eglImage);



    struct video_driver_context driver_context;
    int  m_pipe_in;
    int  m_pipe_out;
    pthread_t msg_thread_id;
    pthread_t async_thread_id;

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
        OMX_COMPONENT_EXECUTE_PENDING        =0xC

    };

    // Deferred callback identifiers
    enum
    {
        //Event Callbacks from the vdec component thread context
        OMX_COMPONENT_GENERATE_EVENT       = 0x1,
        //Buffer Done callbacks from the vdec component thread context
        OMX_COMPONENT_GENERATE_BUFFER_DONE = 0x2,
        //Frame Done callbacks from the vdec component thread context
        OMX_COMPONENT_GENERATE_FRAME_DONE  = 0x3,
        //Buffer Done callbacks from the vdec component thread context
        OMX_COMPONENT_GENERATE_FTB         = 0x4,
        //Frame Done callbacks from the vdec component thread context
        OMX_COMPONENT_GENERATE_ETB         = 0x5,
        //Command
        OMX_COMPONENT_GENERATE_COMMAND     = 0x6,
        //Push-Pending Buffers
        OMX_COMPONENT_PUSH_PENDING_BUFS    = 0x7,
        // Empty Buffer Done callbacks
        OMX_COMPONENT_GENERATE_EBD         = 0x8,
        //Flush Event Callbacks from the vdec component thread context
        OMX_COMPONENT_GENERATE_EVENT_FLUSH       = 0x9,
        OMX_COMPONENT_GENERATE_EVENT_INPUT_FLUSH = 0x0A,
        OMX_COMPONENT_GENERATE_EVENT_OUTPUT_FLUSH = 0x0B,
        OMX_COMPONENT_GENERATE_FBD = 0xc,
        OMX_COMPONENT_GENERATE_START_DONE = 0xD,
        OMX_COMPONENT_GENERATE_PAUSE_DONE = 0xE,
        OMX_COMPONENT_GENERATE_RESUME_DONE = 0xF,
        OMX_COMPONENT_GENERATE_STOP_DONE = 0x10,
        OMX_COMPONENT_GENERATE_HARDWARE_ERROR = 0x11,
        OMX_COMPONENT_GENERATE_ETB_ARBITRARY = 0x12
    };

    enum port_indexes
    {
        OMX_CORE_INPUT_PORT_INDEX        =0,
        OMX_CORE_OUTPUT_PORT_INDEX       =1
    };

    enum vc1_profile_type
    {
        VC1_SP_MP_RCV = 1,
        VC1_AP = 2
    };

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

    OMX_ERRORTYPE omx_vdec_check_port_settings(bool *port_setting_changed);
    OMX_ERRORTYPE omx_vdec_validate_port_param(int height, int width);


    bool allocate_done(void);
    bool allocate_input_done(void);
    bool allocate_output_done(void);

    OMX_ERRORTYPE free_input_buffer(OMX_BUFFERHEADERTYPE *bufferHdr);
    OMX_ERRORTYPE free_output_buffer(OMX_BUFFERHEADERTYPE *bufferHdr);

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

    OMX_ERRORTYPE use_input_buffer(OMX_HANDLETYPE hComp,
                                   OMX_BUFFERHEADERTYPE  **bufferHdr,
                                   OMX_U32               port,
                                   OMX_PTR               appData,
                                   OMX_U32               bytes,
                                   OMX_U8                *buffer);

    OMX_ERRORTYPE use_output_buffer(OMX_HANDLETYPE hComp,
                                   OMX_BUFFERHEADERTYPE   **bufferHdr,
                                   OMX_U32                port,
                                   OMX_PTR                appData,
                                   OMX_U32                bytes,
                                   OMX_U8                 *buffer);

    bool execute_omx_flush(OMX_U32);
    bool execute_output_flush(OMX_U32);
    bool execute_input_flush(OMX_U32);
    bool register_output_buffers();
    OMX_ERRORTYPE empty_buffer_done(OMX_HANDLETYPE hComp,
                                    OMX_BUFFERHEADERTYPE * buffer);

    OMX_ERRORTYPE fill_buffer_done(OMX_HANDLETYPE hComp,
                                    OMX_BUFFERHEADERTYPE * buffer);
    OMX_ERRORTYPE empty_this_buffer_proxy(OMX_HANDLETYPE       hComp,
                                        OMX_BUFFERHEADERTYPE *buffer);

    OMX_ERRORTYPE empty_this_buffer_proxy_arbitrary(OMX_HANDLETYPE hComp,
                                                   OMX_BUFFERHEADERTYPE *buffer
                                                   );

    OMX_ERRORTYPE push_input_buffer (OMX_HANDLETYPE hComp);
    OMX_ERRORTYPE push_input_sc_codec (OMX_HANDLETYPE hComp);
    OMX_ERRORTYPE push_input_h264 (OMX_HANDLETYPE hComp);
    OMX_ERRORTYPE push_input_vc1 (OMX_HANDLETYPE hComp);

    OMX_ERRORTYPE fill_this_buffer_proxy(OMX_HANDLETYPE       hComp,
                                       OMX_BUFFERHEADERTYPE *buffer);
    bool release_done();

    bool release_output_done();
    bool release_input_done();

    bool align_pmem_buffers(int pmem_fd, OMX_U32 buffer_size,
                            OMX_U32 alignment);

    OMX_ERRORTYPE send_command_proxy(OMX_HANDLETYPE  hComp,
                                     OMX_COMMANDTYPE cmd,
                                     OMX_U32         param1,
                                     OMX_PTR         cmdData);
    bool post_event( unsigned int p1,
                     unsigned int p2,
                     unsigned int id
                    );
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

    inline void omx_report_error ()
    {
        DEBUG_PRINT_ERROR("\nERROR: Sending OMX_EventError to Client");
        if (m_cb.EventHandler && !m_error_propogated)
        {
            m_error_propogated = true;
            m_cb.EventHandler(&m_cmp,m_app_data,
                  OMX_EventError,OMX_ErrorHardware,0,NULL);
        }
    }


    //*************************************************************
    //*******************MEMBER VARIABLES *************************
    //*************************************************************
    pthread_mutex_t       m_lock;
    //sem to handle the minimum procesing of commands
    sem_t                 m_cmd_lock;
    bool              m_error_propogated;
    // compression format
    OMX_VIDEO_CODINGTYPE eCompressionFormat;
    // OMX State
    OMX_STATETYPE m_state;
    // Application data
    OMX_PTR m_app_data;
    // Application callbacks
    OMX_CALLBACKTYPE m_cb;
    OMX_COLOR_FORMATTYPE  m_color_format;
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

    bool input_flush_progress;
    bool output_flush_progress;
    bool input_use_buffer;
    bool output_use_buffer;
    int pending_input_buffers;
    int pending_output_buffers;
    int m_ineos_reached;
    int m_outeos_pending;
    int m_outeos_reached;
    // bitmask array size for output side
    unsigned int m_out_bm_count;
    // Number of Output Buffers
    unsigned int m_out_buf_count;
    unsigned int m_out_buf_count_min;
    unsigned int m_out_buf_size;
    // Number of Input Buffers
    unsigned int m_inp_buf_count;
    unsigned int m_inp_buf_count_min;
    // Size of Input Buffers
    unsigned int m_inp_buf_size;
    // bitmask array size for input side
    unsigned int m_inp_bm_count;
    //Input port Populated
    OMX_BOOL m_inp_bPopulated;
    //Output port Populated
    OMX_BOOL m_out_bPopulated;
    //Height
    unsigned int m_height;
    // Width
    unsigned int m_width;
    unsigned int stride;
    unsigned int scan_lines;
    // Storage of HxW during dynamic port reconfig
    unsigned int m_port_height;
    unsigned int m_port_width;

    unsigned int m_crop_x;
    unsigned int m_crop_y;
    unsigned int m_crop_dx;
    unsigned int m_crop_dy;
    // encapsulate the waiting states.
    unsigned int m_flags;

#ifdef _ANDROID_
    // Heap pointer to frame buffers
    sp<MemoryHeapBase>    m_heap_ptr;
#endif //_ANDROID_
    // store I/P PORT state
    OMX_BOOL m_inp_bEnabled;
    // store O/P PORT state
    OMX_BOOL m_out_bEnabled;
    // to know whether Event Port Settings change has been triggered or not.
    bool m_event_port_settings_sent;
    OMX_U8                m_cRole[OMX_MAX_STRINGNAME_SIZE];
    // Platform specific details
    OMX_QCOM_PLATFORM_PRIVATE_LIST      *m_platform_list;
    OMX_QCOM_PLATFORM_PRIVATE_ENTRY     *m_platform_entry;
    OMX_QCOM_PLATFORM_PRIVATE_PMEM_INFO *m_pmem_info;
    // SPS+PPS sent as part of set_config
    OMX_VENDOR_EXTRADATATYPE            m_vendor_config;

    /*Variables for arbitrary Byte parsing support*/
    frame_parse m_frame_parser;
    omx_cmd_queue m_input_pending_q;
    omx_cmd_queue m_input_free_q;
    bool arbitrary_bytes;
    OMX_BUFFERHEADERTYPE  h264_scratch;
    OMX_BUFFERHEADERTYPE  *psource_frame;
    OMX_BUFFERHEADERTYPE  *pdest_frame;
    OMX_BUFFERHEADERTYPE  *m_inp_heap_ptr;
    OMX_BUFFERHEADERTYPE  **m_phdr_pmem_ptr;
    unsigned int m_heap_inp_bm_count;
    codec_type codec_type_parse;
    bool first_frame_meta;
    unsigned frame_count;
    unsigned nal_count;
    unsigned nal_length;
    bool look_ahead_nal;
    int first_frame;
    unsigned char *first_buffer;
    int first_frame_size;
    unsigned int mp4h263_flags;
    OMX_S64 mp4h263_timestamp;
    bool set_seq_header_done;
    bool gate_output_buffers;
    bool gate_input_buffers;
    bool sent_first_frame;
    unsigned int m_out_buf_count_recon;
    unsigned int m_out_buf_count_min_recon;
    unsigned int m_out_buf_size_recon;
    unsigned char m_hwdevice_name[80];
    FILE *m_device_file_ptr;
    enum vc1_profile_type m_vc1_profile;
};

#endif // __OMX_VDEC_H__
