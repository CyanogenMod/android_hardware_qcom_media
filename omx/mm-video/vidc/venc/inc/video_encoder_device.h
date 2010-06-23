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
#ifndef __OMX_VENC_DEV__
#define __OMX_VENC_DEV__

#include "OMX_Types.h"
#include "OMX_Core.h"
#include "OMX_QCOMExtns.h"
#include "qc_omx_component.h"
#include "omx_video_common.h"
#include <linux/msm_vidc_enc.h>


void* async_venc_message_thread (void *);

class venc_dev
{
public:
  venc_dev(); //constructor
  ~venc_dev(); //des

  bool venc_open(OMX_U32);
  void venc_close();
  unsigned venc_stop(void);
  unsigned venc_pause(void);
  unsigned venc_start(void);
  unsigned venc_flush(unsigned);

  unsigned venc_resume(void);
  bool venc_use_buf(void*, unsigned);
  bool venc_free_buf(void*, unsigned);
  bool venc_empty_buf(void *, void *);
  bool venc_fill_buf(void *, void *);

  bool venc_get_buf_req(unsigned long *,unsigned long *,
                        unsigned long *,unsigned long);
  bool venc_set_buf_req(unsigned long *,unsigned long *,
                        unsigned long *,unsigned long);
  bool venc_set_param(void *,OMX_INDEXTYPE);
  bool venc_set_config(void *configData, OMX_INDEXTYPE index);
  bool venc_get_profile_level(OMX_U32 *eProfile,OMX_U32 *eLevel);
  OMX_U32 m_nDriver_fd;
  bool m_profile_set;
  bool m_level_set;

private:
  struct venc_basecfg             m_sVenc_cfg;
  struct venc_ratectrlcfg         rate_ctrl;
  struct venc_targetbitrate       bit_rate;
  struct venc_intraperiod         intra_period;
  struct venc_profile             codec_profile;
  struct ven_profilelevel         profile_level;
  struct venc_switch              set_param;
  struct venc_voptimingcfg        time_inc;
  struct venc_allocatorproperty   m_sInput_buff_property;
  struct venc_allocatorproperty   m_sOutput_buff_property;
  struct venc_sessionqp           session_qp;
  struct venc_multiclicecfg       multislice_cfg;

  bool venc_set_profile_level(OMX_U32 eProfile,OMX_U32 eLevel);
  bool venc_set_intra_period(OMX_U32 nPFrames);
  bool venc_set_target_bitrate(OMX_U32 nTargetBitrate);
  bool venc_set_ratectrl_cfg(OMX_VIDEO_CONTROLRATETYPE eControlRate);
  bool venc_set_session_qp(OMX_U32 i_frame_qp, OMX_U32 p_frame_qp);
  bool venc_set_encode_framerate(OMX_U32 encode_framerate);
  bool venc_set_intra_vop_refresh(OMX_BOOL intra_vop_refresh);
  bool venc_set_color_format(OMX_COLOR_FORMATTYPE color_format);
  bool venc_validate_profile_level(OMX_U32 *eProfile, OMX_U32 *eLevel);
  bool venc_set_multislice_cfg(OMX_VIDEO_AVCSLICEMODETYPE eSliceMode);
};

#endif
