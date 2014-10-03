/*--------------------------------------------------------------------------
Copyright (c) 2010-2014, The Linux Foundation. All rights reserved.

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
#ifndef __OMX_SWVENC__H
#define __OMX_SWVENC__H

#include <unistd.h>
#include "omx_video_base.h"
#include "SwVencTypes.h"
#include "SwVencAPI.h"

extern "C" {
    OMX_API void * get_omx_component_factory_fn(void);
}

class omx_swvenc: public omx_video
{
public:
    omx_swvenc(); //constructor
    ~omx_swvenc(); //des

    OMX_ERRORTYPE component_init(OMX_STRING role);

    OMX_ERRORTYPE set_parameter(OMX_HANDLETYPE hComp,
            OMX_INDEXTYPE  paramIndex,
            OMX_PTR        paramData);
    OMX_ERRORTYPE set_config(OMX_HANDLETYPE hComp,
            OMX_INDEXTYPE  configIndex,
            OMX_PTR        configData);
    OMX_ERRORTYPE component_deinit(OMX_HANDLETYPE hComp);
    bool is_secure_session();

    static SWVENC_STATUS swvenc_input_buffer_done_cb(SWVENC_HANDLE pSwEnc, SWVENC_IPBUFFER *pIpBuffer, void *pClientHandle);
    static SWVENC_STATUS swvenc_fill_buffer_done_cb(SWVENC_HANDLE pSwEnc, SWVENC_OPBUFFER *pOpBuffer, void *pClientHandle);
    static SWVENC_STATUS swvenc_handle_event_cb (SWVENC_HANDLE pSwEnc, SWVENC_EVENTHANDLER* pEventHandler, void *pClientHandle);
    void swvenc_input_buffer_done(SWVENC_IPBUFFER *pIpBuffer);
    void swvenc_fill_buffer_done(SWVENC_OPBUFFER *pOpBuffer);
    void swvenc_handle_event(SWVENC_EVENTHANDLER *pEvent);
    bool update_profile_level();

    //OMX strucutres
    OMX_U32 m_nVenc_format;

private:
    virtual OMX_U32 dev_stop(void);
    virtual OMX_U32 dev_pause(void);
    virtual OMX_U32 dev_start(void);
    virtual OMX_U32 dev_flush(unsigned);
    virtual OMX_U32 dev_resume(void);
    virtual OMX_U32 dev_start_done(void);
    virtual OMX_U32 dev_set_message_thread_id(pthread_t);
    virtual bool dev_use_buf(void *,unsigned,unsigned);
    virtual bool dev_free_buf(void *,unsigned);
    virtual bool dev_empty_buf(void *, void *,unsigned,unsigned);
    virtual bool dev_fill_buf(void *buffer, void *,unsigned,unsigned);
    virtual bool dev_get_buf_req(OMX_U32 *,OMX_U32 *,OMX_U32 *,OMX_U32);
    bool dev_set_buf_req(OMX_U32 *,OMX_U32 *,OMX_U32 *,OMX_U32);
    virtual bool dev_get_seq_hdr(void *, unsigned, unsigned *);
    virtual bool dev_loaded_start(void);
    virtual bool dev_loaded_stop(void);
    virtual bool dev_loaded_start_done(void);
    virtual bool dev_loaded_stop_done(void);
#ifdef _MSM8974_
    virtual int dev_handle_extradata(void*, int);
    virtual int dev_set_format(int);
#endif
    virtual bool dev_is_video_session_supported(OMX_U32 width, OMX_U32 height);
    virtual bool dev_get_capability_ltrcount(OMX_U32 *, OMX_U32 *, OMX_U32 *);
    virtual bool dev_get_performance_level(OMX_U32 *);
    virtual bool dev_get_vui_timing_info(OMX_U32 *);
    virtual bool dev_get_peak_bitrate(OMX_U32 *);
    virtual bool dev_color_align(OMX_BUFFERHEADERTYPE *buffer, OMX_U32 width,
                        OMX_U32 height);
    virtual bool dev_get_output_log_flag();
    virtual int dev_output_log_buffers(const char *buffer_addr, int buffer_len);
    virtual int dev_extradata_log_buffers(char *buffer_addr);

    SWVENC_HANDLE    m_pSwVenc;
    SWVENC_CALLBACK  m_callBackInfo;
    SWVENC_IPBUFFER  m_pSwVencIpBuffer[32];
    SWVENC_OPBUFFER  m_pSwVencOpBuffer[32];
};

#endif //__OMX_SWVENC__H
