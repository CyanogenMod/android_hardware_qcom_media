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

/*========================================================================
  Include Files
 ==========================================================================*/
#include <stdio.h>
#include <linux/msm_q6venc.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/android_pmem.h>
#include <fcntl.h>

#include "venc_device.h"
#include "venc_debug.h"


/*----------------------------------------------------------------------------
* Type Declarations
* -------------------------------------------------------------------------*/
struct venc_pmem_buffer
{
  unsigned int phys;
  unsigned int virt;
  struct file *file;
  struct venc_pmem pmem_buf;
};
static int ven_ref;

/**
 * @brief Max bitrate for MPEG4 level 0
 */
#define  VEN_MPEG4_L0_MAX_BIT_RATE    (64 * 1000)      // 64k

/**
 * @brief Max bitrate for MPEG4 level 0b
 */
#define  VEN_MPEG4_L0B_MAX_BIT_RATE   (128 * 1000)     // 128k

/**
 * @brief Max bitrate for MPEG4 level 1
 */
#define  VEN_MPEG4_L1_MAX_BIT_RATE    (64 * 1000)      // 64k

/**
 * @brief Max bitrate for MPEG4 level 2
 */
#define  VEN_MPEG4_L2_MAX_BIT_RATE    (128 * 1000)     // 128k

/**
 * @brief Max bitrate for MPEG4 level 3
 */
#define  VEN_MPEG4_L3_MAX_BIT_RATE    (384 * 1000)     // 384k

/**
 * @brief Max bitrate for MPEG4 level 4a
 */
#define  VEN_MPEG4_L4A_MAX_BIT_RATE   (4000 * 1000)    // 4000k

/**
 * @brief Max bitrate for MPEG4 level 5
 */
#define  VEN_MPEG4_L5_MAX_BIT_RATE    (8000 * 1000)    // 8000k

/**
 * @brief Max bitrate for MPEG4 level 6
 */
#define  VEN_MPEG4_L6_MAX_BIT_RATE    (12000 * 1000)   // 12000k

/**
 * @brief Max bitrate for H264 level 1
 */
#define VEN_H264_L1_MAX_BIT_RATE       (64 * 1000)

/**
 * @brief Max bitrate for H264 level 1b
 */
#define VEN_H264_L1B_MAX_BIT_RATE      (128 * 1000)

/**
 * @brief Max bitrate for H264 level 1.1
 */
#define VEN_H264_L1P1_MAX_BIT_RATE     (192 * 1000)

/**
 * @brief Max bitrate for H264 level 1.2
 */
#define VEN_H264_L1P2_MAX_BIT_RATE     (384 * 1000)

/**
 * @brief Max bitrate for H264 level 1.3
 */
#define VEN_H264_L1P3_MAX_BIT_RATE     (768 * 1000)

/**
 * @brief Max bitrate for H264 level 2
 */
#define VEN_H264_L2_MAX_BIT_RATE       (2000 * 1000)

/**
 * @brief Max bitrate for H264 level 2.1
 */
#define VEN_H264_L2P1_MAX_BIT_RATE     (4000 * 1000)

/**
 * @brief Max bitrate for H264 level 2.2
 */
#define VEN_H264_L2P2_MAX_BIT_RATE     (4000 * 1000)

/**
 * @brief Max bitrate for H264 level 3
 */
#define VEN_H264_L3_MAX_BIT_RATE       (10000 * 1000)

/**
 * @brief Max bitrate for H264 level 3.1
 */
#define VEN_H264_L3P1_MAX_BIT_RATE     (14000 * 1000)

/**
 * @brief Max number of mbs for H264 level 1
 */
#define VEN_H264_L1_MAX_MB       99

/**
 * @brief Max number of mbs for H264 level 1b
 */
#define VEN_H264_L1B_MAX_MB      99

/**
 * @brief Max number of mbs for H264 level 1.1
 */
#define VEN_H264_L1P1_MAX_MB     396

/**
 * @brief Max number of mbs for H264 level 1.2
 */
#define VEN_H264_L1P2_MAX_MB     396

/**
 * @brief Max number of mbs for H264 level 1.3
 */
#define VEN_H264_L1P3_MAX_MB     396

/**
 * @brief Max number of mbs for H264 level 2
 */
#define VEN_H264_L2_MAX_MB       396

/**
 * @brief Max number of mbs for H264 level 2.1
 */
#define VEN_H264_L2P1_MAX_MB     792

/**
 * @brief Max number of mbs for H264 level 2.2
 */
#define VEN_H264_L2P2_MAX_MB     1620

/**
 * @brief Max number of mbs for H264 level 3
 */
#define VEN_H264_L3_MAX_MB       1620

/**
 * @brief Max number of mbs for H264 level 3.1
 */
#define VEN_H264_L3P1_MAX_MB     3600


/**
 * @brief used in validate params to return the error state along with log msg
 */
#define VEN_INVAL_PARAM(msg)        \
{                                   \
   QC_OMX_MSG_ERROR(msg);     \
   result = VENC_S_EFAIL;        \
}

typedef int int32;
typedef unsigned char boolean;

//////////////////////////////////////////////////////////////////////////////////////////////////////////////
boolean Ven_ValidateLevel(int32 eCodec,
                       int32 nFrameWidth,
                       int32 nFrameHeight,
                       int32 nTargetBitrate,
                       int32 eLevel,
                       int32 eRCMode)
{
   int32 result = 0;
   boolean condition = 0;

   if (eCodec == VEN_CODEC_MPEG4)
   {
      switch (eLevel)
      {
         case VEN_LEVEL_MPEG4_0:
            condition = (nTargetBitrate > VEN_MPEG4_L0_MAX_BIT_RATE && eRCMode != VEN_RC_OFF);
            condition = condition || !VEN_FRAME_SIZE_IN_RANGE(nFrameWidth, nFrameHeight, VEN_QCIF_DX, VEN_QCIF_DY);
            if (condition)
            {
               VEN_INVAL_PARAM("invalid config for level 0");
            }
            break;

         case VEN_LEVEL_MPEG4_0B:
            condition = (nTargetBitrate > VEN_MPEG4_L0B_MAX_BIT_RATE && eRCMode != VEN_RC_OFF);
            condition = condition || !VEN_FRAME_SIZE_IN_RANGE(nFrameWidth, nFrameHeight, VEN_QCIF_DX, VEN_QCIF_DY);
            if (condition)
            {
               VEN_INVAL_PARAM("invalid config for level 0B");
            }
            break;

         case VEN_LEVEL_MPEG4_1:
            condition = (nTargetBitrate > VEN_MPEG4_L1_MAX_BIT_RATE && eRCMode != VEN_RC_OFF);
            condition = condition || !VEN_FRAME_SIZE_IN_RANGE(nFrameWidth, nFrameHeight, VEN_QCIF_DX, VEN_QCIF_DY);
            if (condition)
            {
               VEN_INVAL_PARAM("invalid config for level 1");
            }
            break;

         case VEN_LEVEL_MPEG4_2:
            condition = (nTargetBitrate > VEN_MPEG4_L2_MAX_BIT_RATE && eRCMode != VEN_RC_OFF);
            condition = condition || !VEN_FRAME_SIZE_IN_RANGE(nFrameWidth, nFrameHeight, VEN_CIF_DX, VEN_CIF_DY);
            if (condition)
            {
               VEN_INVAL_PARAM("invalid config for level 2");
            }
            break;

         case VEN_LEVEL_MPEG4_3:
            condition = (nTargetBitrate > VEN_MPEG4_L3_MAX_BIT_RATE && eRCMode != VEN_RC_OFF);
            condition = condition || !VEN_FRAME_SIZE_IN_RANGE(nFrameWidth, nFrameHeight, VEN_CIF_DX, VEN_CIF_DY);
            if (condition)
            {
               VEN_INVAL_PARAM("invalid config for level 3");
            }
            break;

         case VEN_LEVEL_MPEG4_4A:
            condition = (nTargetBitrate > VEN_MPEG4_L4A_MAX_BIT_RATE && eRCMode != VEN_RC_OFF);
            condition = condition || !VEN_FRAME_SIZE_IN_RANGE(nFrameWidth, nFrameHeight, VEN_VGA_DX, VEN_VGA_DY);
            if (condition)
            {
               VEN_INVAL_PARAM("invalid config for level 4A");
            }
            break;

         case VEN_LEVEL_MPEG4_5:
            condition = (nTargetBitrate > VEN_MPEG4_L5_MAX_BIT_RATE && eRCMode != VEN_RC_OFF);
            condition = condition || !VEN_FRAME_SIZE_IN_RANGE(nFrameWidth, nFrameHeight, VEN_PAL_DX, VEN_PAL_DY);
            if (condition)
            {
               VEN_INVAL_PARAM("invalid config for level 5");
            }
            break;

         case VEN_LEVEL_MPEG4_6:
            condition = (nTargetBitrate > VEN_MPEG4_L6_MAX_BIT_RATE && eRCMode != VEN_RC_OFF);
            condition = condition || !VEN_FRAME_SIZE_IN_RANGE(nFrameWidth, nFrameHeight, VEN_HD720P_DX, VEN_HD720P_DY);
            if (condition)
            {
               VEN_INVAL_PARAM("invalid config for level 6");
            }
            break;

         default:
            VEN_INVAL_PARAM("invalid level");
            break;
      }
   }
   else if (eCodec == VEN_CODEC_H264)
   {
      int32 numMBs = nFrameWidth * nFrameHeight / 256;

      switch (eLevel)
      {
         case VEN_LEVEL_H264_1:
            condition = (nTargetBitrate > VEN_H264_L1_MAX_BIT_RATE && eRCMode != VEN_RC_OFF);
            condition = condition || numMBs > VEN_H264_L1_MAX_MB;
            if (condition)
            {
               VEN_INVAL_PARAM("invalid config for level 1");
            }
            break;

         case VEN_LEVEL_H264_1B:
            condition = (nTargetBitrate > VEN_H264_L1B_MAX_BIT_RATE && eRCMode != VEN_RC_OFF);
            condition = condition || numMBs > VEN_H264_L1B_MAX_MB;
            if (condition)
            {
               VEN_INVAL_PARAM("invalid config for level 1b");
            }
            break;

         case VEN_LEVEL_H264_1P1:
            condition = (nTargetBitrate > VEN_H264_L1P1_MAX_BIT_RATE && eRCMode != VEN_RC_OFF);
            condition = condition || numMBs > VEN_H264_L1P1_MAX_MB;
            if (condition)
            {
               VEN_INVAL_PARAM("invalid config for level 1.1");
            }
            break;

         case VEN_LEVEL_H264_1P2:
            condition = (nTargetBitrate > VEN_H264_L1P2_MAX_BIT_RATE && eRCMode != VEN_RC_OFF);
            condition = condition || numMBs > VEN_H264_L1P2_MAX_MB;
            if (condition)
            {
               VEN_INVAL_PARAM("invalid config for level 1.2");
            }
            break;

         case VEN_LEVEL_H264_1P3:
            condition = (nTargetBitrate > VEN_H264_L1P3_MAX_BIT_RATE && eRCMode != VEN_RC_OFF);
            condition = condition || numMBs > VEN_H264_L1P3_MAX_MB;
            if (condition)
            {
               VEN_INVAL_PARAM("invalid config for level 1.3");
            }
            break;

         case VEN_LEVEL_H264_2:
            condition = (nTargetBitrate > VEN_H264_L2_MAX_BIT_RATE && eRCMode != VEN_RC_OFF);
            condition = condition || numMBs > VEN_H264_L2_MAX_MB;
            if (condition)
            {
               VEN_INVAL_PARAM("invalid config for level 2");
            }
            break;

         case VEN_LEVEL_H264_2P1:
            condition = (nTargetBitrate > VEN_H264_L2P1_MAX_BIT_RATE && eRCMode != VEN_RC_OFF);
            condition = condition || numMBs > VEN_H264_L2P1_MAX_MB;
            if (condition)
            {
               VEN_INVAL_PARAM("invalid config for level 2.1");
            }
            break;

         case VEN_LEVEL_H264_2P2:
            condition = (nTargetBitrate > VEN_H264_L2P2_MAX_BIT_RATE && eRCMode != VEN_RC_OFF);
            condition = condition || numMBs > VEN_H264_L2P2_MAX_MB;
            if (condition)
            {
               VEN_INVAL_PARAM("invalid config for level 2.2");
            }
            break;

         case VEN_LEVEL_H264_3:
            condition = (nTargetBitrate > VEN_H264_L3_MAX_BIT_RATE && eRCMode != VEN_RC_OFF);
            condition = condition || numMBs > VEN_H264_L3_MAX_MB;
            if (condition)
            {
               VEN_INVAL_PARAM("invalid config for level 3");
            }
            break;

         case VEN_LEVEL_H264_3P1:
            condition = (nTargetBitrate > VEN_H264_L3P1_MAX_BIT_RATE && eRCMode != VEN_RC_OFF);
            condition = condition || numMBs > VEN_H264_L3P1_MAX_MB;
            if (condition)
            {
               VEN_INVAL_PARAM("invalid config for level 3.1");
            }
            break;

         default:
            VEN_INVAL_PARAM("invalid level");
            break;
      }
   }

    return result;
}


/**************************************************************************
 * @brief Set buffer properties based on current config
 *************************************************************************/
static int ven_set_default_buf_properties(struct ven_device* dvenc)
{
  int ret = 0;
  struct ven_base_cfg* pcfg;

  if (dvenc == NULL) {
    QC_OMX_MSG_ERROR("%s: null driver", __func__);
    ret = -1;
  }

  pcfg = &(dvenc->config.base_config);

  QC_OMX_MSG_MEDIUM("Update input buffer requirements pcfg: %p", pcfg);
  dvenc->input_attrs.min_count = 1;
  dvenc->input_attrs.actual_count = 6;
  dvenc->input_attrs.suffix_size = 0;
  dvenc->input_attrs.data_size = pcfg->input_width * pcfg->input_height * 3 / 2;
  dvenc->input_attrs.alignment = VEN_PMEM_ALIGN;

  QC_OMX_MSG_MEDIUM("Update output buffer requirements pcfg:%p", pcfg);
  dvenc->output_attrs.min_count = 1;
  dvenc->output_attrs.actual_count = 8;
  dvenc->output_attrs.suffix_size = 0;
  dvenc->output_attrs.data_size = pcfg->input_width * pcfg->input_height * 3 / 2;
  dvenc->output_attrs.alignment = VEN_PMEM_ALIGN;


  return ret;
}

/**********************************************************************//**
 * @brief Updates the output buffer size requirements
 *************************************************************************/
void ven_update_output_size(struct ven_device* dvenc)
{

  int width = dvenc->config.base_config.dvs_width;
  int height = dvenc->config.base_config.dvs_height;

  if (dvenc->config.base_config.codec_type == VEN_CODEC_MPEG4)
  {
    switch (dvenc->config.profile_level.level)
    {
      case VEN_LEVEL_MPEG4_0:
      case VEN_LEVEL_MPEG4_1:
        dvenc->output_attrs.data_size = 10 << 11;
        break;
      case VEN_LEVEL_MPEG4_0B:
        dvenc->output_attrs.data_size = 20 << 11;
        break;
      case VEN_LEVEL_MPEG4_2:
      case VEN_LEVEL_MPEG4_3:
        dvenc->output_attrs.data_size = 40 << 11;
        break;
      case VEN_LEVEL_MPEG4_4A:
        dvenc->output_attrs.data_size = 80 << 11;
        break;
      case VEN_LEVEL_MPEG4_5:
        dvenc->output_attrs.data_size = 112 << 11;
        break;
      case VEN_LEVEL_MPEG4_6:
        dvenc->output_attrs.data_size = 248 << 11;
        break;
    }
    dvenc->output_attrs.data_size = dvenc->output_attrs.data_size;
  }
  else if (dvenc->config.base_config.codec_type == VEN_CODEC_H263)
  {
    if (VEN_FRAME_SIZE_IN_RANGE(width, height, VEN_QCIF_DX, VEN_QCIF_DY))
    {
      dvenc->output_attrs.data_size = 20 << 11;
    }
    else if (VEN_FRAME_SIZE_IN_RANGE(width, height, VEN_CIF_DX, VEN_CIF_DY))
    {
      dvenc->output_attrs.data_size = 40 << 11;
    }
    else if (VEN_FRAME_SIZE_IN_RANGE(width, height, VEN_VGA_DX, VEN_VGA_DY))
    {
      dvenc->output_attrs.data_size = 80 << 11;
    }
    else if (VEN_FRAME_SIZE_IN_RANGE(width, height, VEN_PAL_DX, VEN_PAL_DY))
    {
      dvenc->output_attrs.data_size = 112 << 11;
    }
    else
    {
      dvenc->output_attrs.data_size = 248 << 11;
    }
  }
  else if (dvenc->config.base_config.codec_type == VEN_CODEC_H264)
  {
    // Compression of 50% of the YUV size
    dvenc->output_attrs.data_size = (int) (width * height * 0.5) * 3 / 2;
    // FIX IT as per the standard !!!
    dvenc->output_attrs.data_size = 2*dvenc->output_attrs.data_size;
  }
  else
  {
    QC_OMX_MSG_ERROR("invalid codec specified");
  }
  QC_OMX_MSG_HIGH("output width = %d, height = %d", width, height);
  QC_OMX_MSG_HIGH("new output buffer size [%d] required for spcified level [%d]",
    dvenc->output_attrs.data_size,
	dvenc->config.profile_level.level);
}

static int ven_update_buf_properties(struct ven_device *dvenc)
{
  int result = VENC_S_SUCCESS;
  struct ven_base_cfg* pcfg;

  if (dvenc == NULL) {
    QC_OMX_MSG_ERROR("null driver");
    result = VENC_S_EBADPARAM;
  }

  pcfg = &dvenc->config.base_config;
  dvenc->input_attrs.data_size = pcfg->input_height * pcfg->input_width * 3 / 2;
  QC_OMX_MSG_HIGH("input width = %d, height = %d",
    pcfg->input_width, pcfg->input_height);
  QC_OMX_MSG_HIGH("new input buffer size: %d",
    dvenc->input_attrs.data_size);

  ven_update_output_size(dvenc);

  return result;
}

/**************************************************************************
 * @brief Set the default driver configuration.
 *************************************************************************/
static int ven_set_default_config(struct ven_device *dvenc)
{
  int ret = 0;
  struct ven_config_type *pconfig;

  pconfig = &(dvenc->config);
  QC_OMX_MSG_HIGH("%s:  pconfig: %p base_cfg %p", __func__, pconfig, &(pconfig->base_config) );
  // base configuration MPEG4 720p @ 30fps / 8Mbps
  pconfig->base_config.input_width = 1280;
  pconfig->base_config.input_height = 720;
  pconfig->base_config.dvs_width = 1280;
  pconfig->base_config.dvs_height = 720;
  pconfig->base_config.codec_type = VEN_CODEC_MPEG4;
  pconfig->base_config.fps_num = 24;
  pconfig->base_config.fps_den = 1;
  pconfig->base_config.target_bitrate = 6000000;
  pconfig->base_config.input_format = VEN_INPUTFMT_NV21;

  // profile and level setting for 720p
  pconfig->profile.profile = VEN_PROFILE_MPEG4_SP;
  pconfig->profile_level.level = VEN_LEVEL_MPEG4_6;

  // variable bitrate with frame skip
  pconfig->rate_control.rc_mode = VEN_RC_VBR_CFR;

  // disable slicing
  pconfig->multi_slice.mslice_mode = VENC_SLICE_MODE_DEFAULT;
  pconfig->multi_slice.mslice_size = 0;

  // no rotation
  pconfig->rotation.rotation = VEN_ROTATION_0;

  // default bitrate
  pconfig->bitrate.target_bitrate = pconfig->base_config.target_bitrate;

  // default frame rate
  pconfig->frame_rate.fps_numerator = pconfig->base_config.fps_num;
  pconfig->frame_rate.fps_denominator = pconfig->base_config.fps_den;

  // disable int ra refresh
  pconfig->intra_refresh.mb_count = 0;

  // two second int ra period
  pconfig->intra_period.num_pframes = pconfig->base_config.fps_num * 2 - 1;

  // conservative QP setting
  pconfig->session_qp.iframe_qp = 14;
  pconfig->session_qp.pframe_qp = 14;

  // full QP range
  pconfig->qp_range.min_qp = 2;
  pconfig->qp_range.max_qp = 31;

  // 2 ticks per frame
  pconfig->vop_timing.vop_time_resolution = pconfig->base_config.fps_num * 2;

  // enable AC prediction
  pconfig->ac_prediction.status = 0;

  // disable error resilience
  pconfig->short_header.status = 0;
  pconfig->header_extension.hec_interval = 0;
  pconfig->data_partition.status = 0;

  return ret;
}

static void ven_change_codec(struct ven_device * dvenc)
{
  struct ven_config_type *pconfig =&(dvenc->config);

  QC_OMX_MSG_HIGH("%s", __func__);
  if (pconfig->base_config.codec_type == VEN_CODEC_MPEG4)
  {
    pconfig->qp_range.min_qp = 2;
    pconfig->qp_range.max_qp = 31;

    pconfig->multi_slice.mslice_mode = VENC_SLICE_MODE_DEFAULT;
    pconfig->multi_slice.mslice_size = 0;

    pconfig->profile.profile = VEN_PROFILE_MPEG4_SP;
    pconfig->profile_level.level = VEN_LEVEL_MPEG4_6;

    pconfig->session_qp.iframe_qp = 14;
    pconfig->session_qp.pframe_qp = 14;

    pconfig->short_header.status = 0;
    pconfig->header_extension.hec_interval = 0;
    pconfig->data_partition.status = 0;
  }
  else if (pconfig->base_config.codec_type == VEN_CODEC_H263)
  {
    pconfig->qp_range.min_qp = 2;
    pconfig->qp_range.max_qp = 31;

    pconfig->multi_slice.mslice_mode = VENC_SLICE_MODE_DEFAULT;
    pconfig->multi_slice.mslice_size = 0;

    pconfig->profile.profile = VEN_PROFILE_H263_BASELINE;
    pconfig->profile_level.level =  VEN_LEVEL_H263_70;

    pconfig->session_qp.iframe_qp = 14;
    pconfig->session_qp.pframe_qp = 14;

    pconfig->short_header.status = 0;
    pconfig->ac_prediction.status = 0;
    pconfig->header_extension.hec_interval = 0;
    pconfig->data_partition.status = 0;
  }
  else if (pconfig->base_config.codec_type == VEN_CODEC_H264)
  {
    pconfig->qp_range.min_qp = 2;
    pconfig->qp_range.max_qp = 51;

    pconfig->multi_slice.mslice_mode = VENC_SLICE_MODE_DEFAULT;
    pconfig->multi_slice.mslice_size = 0;

    pconfig->profile.profile = VEN_PROFILE_H264_BASELINE;
    pconfig->profile_level.level =  VEN_LEVEL_H264_3P1;

    pconfig->session_qp.iframe_qp = 30;
    pconfig->session_qp.pframe_qp = 30;

    pconfig->short_header.status = 0;
    pconfig->ac_prediction.status = 0;
    pconfig->header_extension.hec_interval = 0;
    pconfig->data_partition.status = 0;
  }
  else
  {
    QC_OMX_MSG_ERROR("invalid codec type");
  }
}

/**************************************************************************
 * @brief Validate the given encoder configuration
 *************************************************************************/
static int ven_validate_config(struct ven_config_type* pConfig)
{
  int32 result = 0;

   unsigned long minQp = 1;
   unsigned long maxQp = 31;
   boolean condition = 0;

   result = Ven_ValidateLevel(pConfig->base_config.codec_type,
                       pConfig->base_config.input_width,
                       pConfig->base_config.input_height,
                       pConfig->bitrate.target_bitrate,
                       pConfig->profile_level.level,
                       pConfig->rate_control.rc_mode);

   // verify input width and height, width must be greater than height
   condition = pConfig->base_config.input_width == 0 ||
               pConfig->base_config.input_height ==  0;
   if (condition)
   {
      VEN_INVAL_PARAM("invalid input width or height");
   }

   // verify non-zero width and height
   condition = pConfig->base_config.dvs_width == 0 ||
               pConfig->base_config.dvs_height ==  0;
   if (condition)
   {
      VEN_INVAL_PARAM("invalid dvs width or height");
   }

   // verify codec
   condition = pConfig->base_config.codec_type !=  VEN_CODEC_MPEG4 &&
               pConfig->base_config.codec_type !=  VEN_CODEC_H263 &&
               pConfig->base_config.codec_type !=  VEN_CODEC_H264;
   if (condition)
   {
      VEN_INVAL_PARAM("invalid codec");
   }

   // non-zero fps, also protect against div by zero
   condition = pConfig->base_config.fps_num == 0 ||
               pConfig->base_config.fps_den == 0;
   if (condition)
   {
      VEN_INVAL_PARAM("invalid fps numerator or denominator");
   }

   // non-zero bitrate
   condition = pConfig->base_config.target_bitrate == 0 &&
               pConfig->rate_control.rc_mode != VEN_RC_OFF;
   if (condition)
   {
      VEN_INVAL_PARAM("invalid bitrate");
   }

   // must be nv21 yuv format (refer to 4cc color formats)
   if (pConfig->base_config.input_format != VEN_INPUTFMT_NV21)
   {
      VEN_INVAL_PARAM("invalid yuv format");
   }

   // verify rate control flavor
   condition = pConfig->rate_control.rc_mode != VEN_RC_OFF &&
               pConfig->rate_control.rc_mode != VEN_RC_VBR_VFR &&
               pConfig->rate_control.rc_mode != VEN_RC_VBR_CFR &&
               pConfig->rate_control.rc_mode != VEN_RC_CBR_VFR;
   if (condition)
   {
      VEN_INVAL_PARAM("invalid rc flavor");
   }

   // For MMS mode only VBR VFR is valid
   //condition = (pConfig->sEncMode.nEncoderMode == VEN_MODE_MMS) && 
   //            (pConfig->rate_control.rc_mode != VEN_RC_VBR_VFR);
   //if (condition)
   //{
   //   VEN_INVAL_PARAM("invalid rc flavor for MMS enc mode");
   //}   
   
   // VBR CFR is only valid for resolutions less than or equal to wqvga
   //if (pConfig->rate_control.rc_mode == VEN_RC_CBR_VFR)
   //{
   //   condition = pConfig->base_config.input_width > VEN_WQVGA_DX ||
   //               pConfig->base_config.input_width > VEN_WVQGA_DY;
   //   if (condition)
   //   {
   //      VEN_INVAL_PARAM("VEN_RC_VBR_CFR is only valid for small frames");
   //   }
   //}

   // non-zero fps, also protect against div by zero
   condition = pConfig->frame_rate.fps_numerator == 0 ||
               pConfig->frame_rate.fps_denominator == 0;
   if (condition)
   {
      VEN_INVAL_PARAM("invalid fps numerator or denominator");
   }

   // ir only valid for qcif
   //condition = pConfig->intra_refresh.ir_mode == VEN_IR_RANDOM ||
   //            pConfig->intra_refresh.ir_mode == VEN_IR_CYCLIC;
   //if (condition)
   //{
   //  condition = (pConfig->base_config.dvs_width == VEN_QCIF_DX &&
   //               pConfig->base_config.dvs_height == VEN_QCIF_DY) || 
   //               (pConfig->base_config.dvs_width == VEN_QCIF_DY &&
   //               pConfig->base_config.dvs_height == VEN_QCIF_DX);
   //  if (!condition)
   //  {
   //    VEN_INVAL_PARAM("intra refresh is only valid for qcif");
   //  } 
   //}

   // validate intra period, we don't support all iframes
   if (pConfig->intra_period.num_pframes == 0)
   {
      VEN_INVAL_PARAM("we dont support all iframes");
   }

   // validate QPs
   if (pConfig->base_config.codec_type == VEN_CODEC_H264)
   {
      maxQp = 51;
   }
   if (pConfig->rate_control.rc_mode == VEN_RC_OFF)
   {
      condition = pConfig->session_qp.iframe_qp < minQp ||
                  pConfig->session_qp.pframe_qp < minQp ||
                  pConfig->session_qp.iframe_qp > maxQp ||
                  pConfig->session_qp.pframe_qp > maxQp;
      if (condition)
      {
         VEN_INVAL_PARAM("invalid session QPs");
      }
   }
   else
   {
      condition = pConfig->qp_range.max_qp < minQp ||
                  pConfig->qp_range.min_qp < minQp ||
                  pConfig->qp_range.max_qp > maxQp ||
                  pConfig->qp_range.min_qp > maxQp ||
                  pConfig->qp_range.min_qp > pConfig->qp_range.max_qp;
      if (condition)
      {
         VEN_INVAL_PARAM("invalid qp range");
      }
   }
#if 0
   // verify slice configuration
   if (pConfig->multi_slice.mslice_mode == VEN_MSLICE_CNT_MB)
   {
      unsigned long numMBs = (pConfig->base_config.input_width / 16) *
                             (pConfig->base_config.input_height / 16);

      condition = pConfig->multi_slice.mslice_size == 0 ||
                  pConfig->multi_slice.mslice_size > numMBs;
      if (condition)
      {
         VEN_INVAL_PARAM("invalid slice MBs");
      }
   }
   else if (pConfig->multi_slice.mslice_mode == VEN_MSLICE_CNT_BYTE)
   {
      if (pConfig->multi_slice.mslice_size == 0)
      {
         VEN_INVAL_PARAM("invalid slice bytes");
      }
   }
   else if (pConfig->multi_slice.mslice_mode != VEN_MSLICE_OFF &&
            pConfig->multi_slice.mslice_mode != VEN_MSLICE_GOB)
   {
      VEN_INVAL_PARAM("invalid slice flavor");
   }

   //if (pConfig->multi_slice.mslice_mode == VEN_MSLICE_OFF)
   //{
   //   // Disable the slice info
   //   VEN_MSG_HIGH("Disabling extra data when slicing is off",0,0,0);
   //   pConfig->sExtraData.nEnableVideoExtraData = 0;
   //}
#endif   
   // codec-specific validation
   if (pConfig->base_config.codec_type == VEN_CODEC_MPEG4)
   {
      // validate profile / level
      if (pConfig->profile.profile != VEN_PROFILE_MPEG4_SP)
      {
         VEN_INVAL_PARAM("invalid profile specified");
      }

      switch (pConfig->profile_level.level)
      {
         case VEN_LEVEL_MPEG4_0:
         case VEN_LEVEL_MPEG4_0B:
         case VEN_LEVEL_MPEG4_1:
         case VEN_LEVEL_MPEG4_2:
         case VEN_LEVEL_MPEG4_3:
         case VEN_LEVEL_MPEG4_4A:
         case VEN_LEVEL_MPEG4_5:
         case VEN_LEVEL_MPEG4_6:
            break;
         default:
            VEN_INVAL_PARAM("invalid level specified");
            break;
      }
#if 0
      // gob is only valid for h263
      if (pConfig->multi_slice.mslice_mode == VEN_MSLICE_GOB)
      {
         VEN_INVAL_PARAM("invalid slice flavor for mpeg4");
      }
      // ER is only valid for resolutions less than or equal to wqvga
      if (pConfig->multi_slice.mslice_mode != VEN_MSLICE_OFF)
      {
         condition = pConfig->base_config.input_width > VEN_WQVGA_DX ||
                     pConfig->base_config.input_height > VEN_WVQGA_DY;
         if (condition)
         {
            VEN_INVAL_PARAM("slicing only valid for small frames");
         }
      }
#endif
      // non-zero time resolution
      if (pConfig->vop_timing.vop_time_resolution == 0)
      {
         VEN_INVAL_PARAM("invalid time inc res");
      }

      // validate enums
      condition = pConfig->ac_prediction.status != 0 &&
                  pConfig->ac_prediction.status != 1;
      if (condition)
      {
         VEN_INVAL_PARAM("invalid ac prediction boolean");
      }

      // validate enums
      condition = pConfig->short_header.status != 0 &&
                  pConfig->short_header.status != 1;
      if (condition)
      {
         VEN_INVAL_PARAM("invalid short header boolean");
      }

      // validate enums
      condition = pConfig->data_partition.status != 0 &&
                  pConfig->data_partition.status != 1;
      if (condition)
      {
         VEN_INVAL_PARAM("invalid data partition boolean");
      }

      // dp only valid for resolutions less than or equal to wqvga
      if (pConfig->data_partition.status == 1)
      {
         condition = pConfig->base_config.input_width > VEN_WQVGA_DX ||
                     pConfig->base_config.input_height > VEN_WVQGA_DY;
         if (condition)
         {
            VEN_INVAL_PARAM("dp only valid for small frames");
         }
      }

      // hec only valid for resolutions less than or equal to wqvga
      if (pConfig->header_extension.hec_interval > 0)
      {
         condition = pConfig->base_config.input_width > VEN_WQVGA_DX ||
                     pConfig->base_config.input_height > VEN_WVQGA_DY;
         if (condition)
         {
            VEN_INVAL_PARAM("hec only valid for resolutions less than or equal to wqvga");
         }
      }
   }
   else if (pConfig->base_config.codec_type == VEN_CODEC_H263)
   {
      // validate profile / level
      if (pConfig->profile.profile != VEN_PROFILE_H263_BASELINE)
      {
         VEN_INVAL_PARAM("invalid profile specified");
      }

      switch (pConfig->profile_level.level)
      {
         case VEN_LEVEL_H263_10:
         case VEN_LEVEL_H263_20:
         case VEN_LEVEL_H263_30:
         case VEN_LEVEL_H263_40:
         case VEN_LEVEL_H263_45:
         case VEN_LEVEL_H263_50:
         case VEN_LEVEL_H263_60:
         case VEN_LEVEL_H263_70:
            break;
         default:
            VEN_INVAL_PARAM("invalid level specified");
            break;
      }
#if 0
      // we don't support annex k
      condition = pConfig->multi_slice.mslice_mode == VEN_MSLICE_CNT_BYTE ||
                  pConfig->multi_slice.mslice_mode == VEN_MSLICE_CNT_MB;
      if (condition)
      {
         VEN_INVAL_PARAM("invalid slice flavor for h263");
      }
#endif
      // make sure no mp4 params are configured for h263
      condition = pConfig->ac_prediction.status != 0 ||
                  pConfig->short_header.status != 0 ||
                  pConfig->data_partition.status != 0 ||
                  pConfig->header_extension.hec_interval != 0;
      if (condition)
      {
         VEN_INVAL_PARAM("mpeg4 params can't be set for h263");
      }
   }
   else if (pConfig->base_config.codec_type == VEN_CODEC_H264)
   {
      // validate profile / level
      if (pConfig->profile.profile != VEN_PROFILE_H264_BASELINE)
      {
         VEN_INVAL_PARAM("invalid profile specified");
      }

      switch (pConfig->profile_level.level)
      {
         case VEN_LEVEL_H264_1:
         case VEN_LEVEL_H264_1B:
         case VEN_LEVEL_H264_1P1:
         case VEN_LEVEL_H264_1P2:
         case VEN_LEVEL_H264_1P3:
         case VEN_LEVEL_H264_2:
         case VEN_LEVEL_H264_2P1:
         case VEN_LEVEL_H264_2P2:
         case VEN_LEVEL_H264_3:
         case VEN_LEVEL_H264_3P1:
            break;
         default:
            VEN_INVAL_PARAM("invalid level specified");
            break;
      }
#if 0
      // gob is only valid for h263
      if (pConfig->multi_slice.mslice_mode == VEN_MSLICE_GOB)
      {
         VEN_INVAL_PARAM("invalid slice flavor for h264");
      }
      // ER is only valid for resolutions less than or equal to CIF
      if (pConfig->multi_slice.mslice_mode != VEN_MSLICE_OFF)
      {
         condition = pConfig->base_config.input_width > VEN_CIF_DX ||
                     pConfig->base_config.input_height > VEN_CIF_DY;
         if (condition)
         {
            VEN_INVAL_PARAM("slicing only valid for resolutions less than or equal to CIF");
         }
      }
#endif
      // make sure no mp4 params are configured for h264
      condition = pConfig->ac_prediction.status != 0 ||
                  pConfig->short_header.status != 0 ||
                  pConfig->data_partition.status != 0 ||
                  pConfig->header_extension.hec_interval != 0;
      if (condition)
      {
         VEN_INVAL_PARAM("mpeg4 params can't be set for h264");
      }
   }

    return result;
}

/**************************************************************************
 * @brief Print  out configuration
 *************************************************************************/
static void ven_print_config(struct venc_q6_config* pconfig)
{

  QC_OMX_MSG_PROFILE("Config for video encoder");
  QC_OMX_MSG_PROFILE("config standard=%d, input_frame_height=%d, input_frame_width=%d",
      (int ) pconfig->config_params.standard,
      (int ) pconfig->config_params.input_frame_height,
      (int ) pconfig->config_params.input_frame_width);

  QC_OMX_MSG_PROFILE("config output_frame_height=%d, output_frame_width=%d, rotation_angle=%d",
      (int ) pconfig->config_params.output_frame_height,
      (int ) pconfig->config_params.output_frame_width,
      (int ) pconfig->config_params.rotation_angle);

  QC_OMX_MSG_PROFILE("config intra_period=%d",
      (int ) pconfig->config_params.intra_period);

  QC_OMX_MSG_PROFILE("config rate_control=%d",
      (int ) pconfig->config_params.rate_control);

  ////////////////////////////////////////
  //////////////////////// slice
  QC_OMX_MSG_PROFILE("config mslice_mode=%d, slice_size=%d",
      (int ) pconfig->config_params.slice_config.slice_mode,
      (int ) pconfig->config_params.slice_config.units_per_slice);

  ////////////////////////////////////////
  //////////////////////// quality
  QC_OMX_MSG_PROFILE("config frame_numerator=%d, fps_denominator=%d, bitrate=%d",
      (int ) pconfig->config_params.frame_rate.frame_rate_num,
      (int ) pconfig->config_params.frame_rate.frame_rate_den,
      (int ) pconfig->config_params.bitrate);

  QC_OMX_MSG_PROFILE("config iframe_qp=%d, pframe_qp=%d, min_qp=%d",
      (int ) pconfig->config_params.iframe_qp,
      (int ) pconfig->config_params.pframe_qp,
      (int ) pconfig->config_params.qp_range.min_qp);

  QC_OMX_MSG_PROFILE("config max_qp=%d",
      (int ) pconfig->config_params.qp_range.max_qp);

  ////////////////////////////////////////
  //////////////////////// mp4
  if (pconfig->config_params.standard == VENC_CODEC_MPEG4)
  {
    QC_OMX_MSG_PROFILE("config mp4 profile=%d, level=%d, time_resolution=%d",
        (int ) pconfig->codec_params.mpeg4_params.profile,
        (int ) pconfig->codec_params.mpeg4_params.level,
        (int ) pconfig->codec_params.mpeg4_params.time_resolution);
    QC_OMX_MSG_PROFILE("config ac_prediction=%d, hec_interval=%d, data_partition=%d",
        (int ) pconfig->codec_params.mpeg4_params.ac_prediction,
        (int ) pconfig->codec_params.mpeg4_params.hec_interval,
        (int ) pconfig->codec_params.mpeg4_params.data_partition);
    QC_OMX_MSG_HIGH("config short_header=%d",
        (int ) pconfig->codec_params.mpeg4_params.short_header);
  }

  ////////////////////////////////////////
  //////////////////////// h263
  else if (pconfig->config_params.standard == VENC_CODEC_H263)
  {
    QC_OMX_MSG_PROFILE("config h263 profile=%d, level=%d",
        (int ) pconfig->codec_params.h263_params.profile,
        (int ) pconfig->codec_params.h263_params.level);
  }

  ////////////////////////////////////////
  //////////////////////// h264
  else if (pconfig->config_params.standard == VENC_CODEC_H264)
  {
    QC_OMX_MSG_PROFILE("config h264 profile=%d, level=%d",
        (int ) pconfig->codec_params.h264_params.profile,
        (int ) pconfig->codec_params.h264_params.level);
  }

}

/**************************************************************************
 * @brief Translate driver to Q6 config
 *************************************************************************/
static int ven_translate_config(struct ven_config_type* psrc,
                                union venc_codec_config* pcodec,
                               struct venc_common_config* pcommon)
{
  int ret = 0;

  if (psrc == NULL ||
      pcommon == NULL ||
      pcodec == NULL) {
    QC_OMX_MSG_ERROR( "%s: failed with null parameter", __func__);
     ret = VENC_S_EFAIL;
     return ret;
  }
  
  ret = ven_validate_config(psrc);
  if (ret != 0)
  {
    QC_OMX_MSG_ERROR( "%s: validate config parameters failed", __func__);
    return ret;
  }
  memset(pcommon, 0, sizeof(*pcommon));
  memset(pcodec, 0, sizeof(*pcodec));

  // codec specific
  if (psrc->base_config.codec_type == VEN_CODEC_MPEG4)
  {
    struct venc_mpeg4_config* pmp4 = &(pcodec->mpeg4_params);

    QC_OMX_MSG_HIGH("Configuring for mpeg4... psrc = %p",psrc);
    pcommon->standard = VENC_CODEC_MPEG4;

    if (psrc->profile.profile == VEN_PROFILE_MPEG4_SP) {
      pmp4->profile = VENC_MPEG4_PROFILE_SIMPLE;
    }
    else
      QC_OMX_MSG_ERROR("Invalid mp4 configuration \n");

    switch (psrc->profile_level.level)
    {
      case VEN_LEVEL_MPEG4_0:
        pmp4->level = VENC_MPEG4_LEVEL_0;
        break;
      case VEN_LEVEL_MPEG4_0B:
        pmp4->level = VENC_MPEG4_LEVEL_0B;
        break;
      case VEN_LEVEL_MPEG4_1:
        pmp4->level = VENC_MPEG4_LEVEL_1;
        break;
      case VEN_LEVEL_MPEG4_2:
        pmp4->level = VENC_MPEG4_LEVEL_2;
        break;
      case VEN_LEVEL_MPEG4_3:
        pmp4->level = VENC_MPEG4_LEVEL_3;
        break;
      case VEN_LEVEL_MPEG4_4A:
        pmp4->level = VENC_MPEG4_LEVEL_4A;
        break;
      case VEN_LEVEL_MPEG4_5:
        pmp4->level = VENC_MPEG4_LEVEL_5;
        break;
      case VEN_LEVEL_MPEG4_6:
        pmp4->level = VENC_MPEG4_LEVEL_6;
        break;
      default:
        QC_OMX_MSG_ERROR("invalid level specified %d",
            (int) psrc->profile_level.level);
        break;
    }

    pmp4->time_resolution = psrc->vop_timing.vop_time_resolution;
    pmp4->ac_prediction = psrc->ac_prediction.status == 1 ? 1 : 0;
    pmp4->hec_interval = psrc->header_extension.hec_interval; /// @int egrate need to have HEC int erval in driver api. also fix this hack in the OMX layer
    pmp4->data_partition = psrc->data_partition.status == 1 ? 1 : 0;
    pmp4->short_header = psrc->short_header.status == 1 ? 1 : 0;
  }
  else if (psrc->base_config.codec_type == VEN_CODEC_H263)
  {
    struct venc_h263_config* p263 = &(pcodec->h263_params);

    QC_OMX_MSG_HIGH("Configuring for h263...");
    pcommon->standard = VENC_CODEC_H263;
    if (psrc->profile.profile == VEN_PROFILE_H263_BASELINE)
    {
      p263->profile = VENC_H263_PROFILE_P0;
    }
    else
    {
      QC_OMX_MSG_ERROR("invalid profile %d",
          (int) psrc->profile.profile);
    }

    switch (psrc->profile_level.level)
    {
      case VEN_LEVEL_H263_10:
        p263->level = VENC_H263_LEVEL_10;
        break;
      case VEN_LEVEL_H263_20:
        p263->level = VENC_H263_LEVEL_20;
        break;
      case VEN_LEVEL_H263_30:
        p263->level = VENC_H263_LEVEL_30;
        break;
      case VEN_LEVEL_H263_40:
        p263->level = VENC_H263_LEVEL_40;
        break;
      case VEN_LEVEL_H263_45:
        p263->level = VENC_H263_LEVEL_45;
        break;
      case VEN_LEVEL_H263_50:
        p263->level = VENC_H263_LEVEL_50;
        break;
      case VEN_LEVEL_H263_60:
        p263->level = VENC_H263_LEVEL_60;
        break;
      case VEN_LEVEL_H263_70:
        p263->level = VENC_H263_LEVEL_70;
        break;
      default:
        QC_OMX_MSG_ERROR("invalid level specified %d",
            (int) psrc->profile_level.level);
        break;
    }
  }
  else if (psrc->base_config.codec_type == VEN_CODEC_H264)
  {
    struct venc_h264_config* p264 = &(pcodec->h264_params);

    QC_OMX_MSG_HIGH("Configuring for h264...");
    pcommon->standard = VENC_CODEC_H264;

    if (psrc->profile.profile == VEN_PROFILE_H264_BASELINE)
    {
      p264->profile = VENC_H264_PROFILE_BASELINE;
    }
    else
    {
      QC_OMX_MSG_ERROR("invalid profile %d",
          (int) psrc->profile.profile);
    }

    switch (psrc->profile_level.level)
    {
      case VEN_LEVEL_H264_1:
        p264->level = VENC_H264_LEVEL_1;
        break;
      case VEN_LEVEL_H264_1B:
        p264->level = VENC_H264_LEVEL_1B;
        break;
      case VEN_LEVEL_H264_1P1:
        p264->level = VENC_H264_LEVEL_1P1;
        break;
      case VEN_LEVEL_H264_1P2:
        p264->level = VENC_H264_LEVEL_1P2;
        break;
      case VEN_LEVEL_H264_1P3:
        p264->level = VENC_H264_LEVEL_1P3;
        break;
      case VEN_LEVEL_H264_2:
        p264->level = VENC_H264_LEVEL_2;
        break;
      case VEN_LEVEL_H264_2P1:
        p264->level = VENC_H264_LEVEL_2P1;
        break;
      case VEN_LEVEL_H264_2P2:
        p264->level = VENC_H264_LEVEL_2P2;
        break;
      case VEN_LEVEL_H264_3:
        p264->level = VENC_H264_LEVEL_3;
        break;
      case VEN_LEVEL_H264_3P1:
        p264->level = VENC_H264_LEVEL_3P1;
        break;
      default:
        QC_OMX_MSG_ERROR("invalid level specified %d",
            (int) psrc->profile_level.level);
        break;
    }
  }

  pcommon->slice_config.slice_mode = psrc->multi_slice.mslice_mode;
  pcommon->slice_config.units_per_slice = psrc->multi_slice.mslice_size;
  pcommon->input_frame_width   = psrc->base_config.input_width;
  pcommon->input_frame_height  = psrc->base_config.input_height;
  pcommon->output_frame_width  = psrc->base_config.dvs_width;
  pcommon->output_frame_height  = psrc->base_config.dvs_height;
  
  if ((pcommon->input_frame_width != ((pcommon->input_frame_width/16)*16)) ||
     (pcommon->input_frame_height != ((pcommon->input_frame_height/16)*16)) ||
     (pcommon->output_frame_width != ((pcommon->output_frame_width/16)*16)) ||
     (pcommon->output_frame_height != ((pcommon->output_frame_height/16)*16)) )
  {
    QC_OMX_MSG_ERROR("non multiple of 16 width or height is not supported");
    ret = VENC_S_ENOTSUPP;
  }

  if (psrc->rotation.rotation == VEN_ROTATION_0)
  {
    pcommon->rotation_angle = VEN_ROTATION_0;
  }
  else if (psrc->rotation.rotation == VEN_ROTATION_90)
  {
    // swap width and height
    pcommon->output_frame_width  = psrc->base_config.dvs_height;
    pcommon->output_frame_height  = psrc->base_config.dvs_width;
    pcommon->rotation_angle = VEN_ROTATION_90;
  }
  else if (psrc->rotation.rotation == VEN_ROTATION_180)
  {
    pcommon->rotation_angle = VEN_ROTATION_180;
  }
  else if (psrc->rotation.rotation == VEN_ROTATION_270)
  {
    // swap width and height
    pcommon->output_frame_width  = psrc->base_config.dvs_height;
    pcommon->output_frame_height  = psrc->base_config.dvs_width;
    pcommon->rotation_angle = VEN_ROTATION_270;
  }
  else
  {
    QC_OMX_MSG_ERROR("invalid rotation %d", psrc->rotation.rotation);
  }
  pcommon->intra_period = psrc->intra_period.num_pframes;

  // rate control config
  if (psrc->rate_control.rc_mode == VEN_RC_OFF)
  {
    pcommon->rate_control = VEN_RC_OFF;
  }
  else if (psrc->rate_control.rc_mode == VEN_RC_VBR_CFR)
  {
    pcommon->rate_control = VEN_RC_VBR_CFR;
  }
  else if (psrc->rate_control.rc_mode == VEN_RC_VBR_VFR)
  {
    pcommon->rate_control = VEN_RC_VBR_VFR;
  }
  else if (psrc->rate_control.rc_mode == VEN_RC_CBR_VFR)
  {
    pcommon->rate_control = VEN_RC_CBR_VFR;
  }

  // quality config
  pcommon->frame_rate.frame_rate_num    = psrc->base_config.fps_num;
  pcommon->frame_rate.frame_rate_den    = psrc->base_config.fps_den;
  pcommon->bitrate          = psrc->bitrate.target_bitrate;

  if (psrc->rate_control.rc_mode == VEN_RC_OFF)
  {
    // if rc is off, then use the client supplied qp
    pcommon->iframe_qp                = psrc->session_qp.iframe_qp;
    pcommon->pframe_qp                = psrc->session_qp.pframe_qp;
  }
  else
  {
    // if rc is on, then we have to supply the qp since driver
    // interface qp ony applies when rc is off
    if (psrc->base_config.codec_type == VEN_CODEC_H264)
    {
      pcommon->iframe_qp             = 30;
      pcommon->pframe_qp             = 30;
    }
    else
    {
      pcommon->iframe_qp             = 14;
      pcommon->pframe_qp             = 14;
    }

    if (pcommon->iframe_qp > psrc->qp_range.max_qp ||
        pcommon->iframe_qp < psrc->qp_range.min_qp)
    {
      pcommon->iframe_qp = (psrc->qp_range.max_qp + psrc->qp_range.min_qp) / 2;
    }

    if (pcommon->pframe_qp > psrc->qp_range.max_qp ||
        pcommon->pframe_qp < psrc->qp_range.min_qp)
    {
      pcommon->pframe_qp = (psrc->qp_range.max_qp + psrc->qp_range.min_qp) / 2;
    }
  }

  pcommon->qp_range.min_qp           = psrc->qp_range.min_qp;
  pcommon->qp_range.max_qp           = psrc->qp_range.max_qp;

  return ret;
}


int ven_start(struct ven_device *dvenc,
              struct venc_buffers *venc_bufs)
{
  int ret = 0;
  struct venc_init_config vcfg;

  QC_OMX_MSG_HIGH("Processing ven_start...");
  ret = ven_translate_config(&(dvenc->config), &(vcfg.q6_config.codec_params),
      &(vcfg.q6_config.config_params));

  if (ret != VENC_S_SUCCESS) {
      QC_OMX_MSG_ERROR("%s ven_translate_config(...) failed (%d) \n", __func__, ret);
      return ret;
    }

  memcpy(&(vcfg.q6_bufs), venc_bufs, sizeof(struct venc_buffers));

  ven_print_config(&(vcfg.q6_config));
  ret = ioctl(dvenc->fd, VENC_IOCTL_CMD_START,&vcfg);
  if (ret) {
    QC_OMX_MSG_ERROR("%s failed (%d) \n", __func__, ret);
    return ret;
  }
  return ret;
}

int ven_get_sequence_hdr(struct ven_device *dvenc,
                         struct venc_pmem *pbuf,
                         int *psize)
{
  int ret = 0;
  struct venc_seq_config vcfg;

  QC_OMX_MSG_HIGH("Processing ven_get_sequence_hdr...");
  ven_translate_config(&(dvenc->config), &(vcfg.q6_config.codec_params),
      &(vcfg.q6_config.config_params));
  memcpy(&(vcfg.buf), pbuf, sizeof(struct venc_pmem));

  ret = ioctl(dvenc->fd, VENC_IOCTL_GET_SEQUENCE_HDR, &vcfg);
  if (ret) {
    QC_OMX_MSG_ERROR("%s failed (%d) \n", __func__, ret);
    return ret;
  }
  else
    *psize = vcfg.size;

  return ret;
}

int ven_set_input_req(struct ven_device* dvenc,
                              struct ven_allocator_property* pprop)
{
  int ret = 0;
  QC_OMX_MSG_MEDIUM("Processing ven_set_input_req...");

  if (pprop == NULL)
  {  QC_OMX_MSG_ERROR( "null params(s)");
    ret = -1;
    return ret;
  }

  if (pprop->min_count == dvenc->input_attrs.min_count)
  {
    if (pprop->actual_count >= dvenc->input_attrs.min_count)
    {
      if (pprop->data_size >= dvenc->input_attrs.data_size)
      {
        if (pprop->alignment == dvenc->input_attrs.alignment)
        {
          dvenc->input_attrs = *pprop;
        }
        else
        {
          QC_OMX_MSG_ERROR( "alignment is read only");
          ret = VENC_S_EBUFFREQ;
        }
      }
      else
      {
        QC_OMX_MSG_ERROR( "buffer is too small");
        ret = VENC_S_EBUFFREQ;
      }
    }
    else
    {
      QC_OMX_MSG_ERROR( "not enough buffers");
      ret = VENC_S_EBUFFREQ;
    }
  }
  else
  {
    QC_OMX_MSG_ERROR( "min buffer count is read only");
    ret = VENC_S_EBUFFREQ;
  }
  return ret;
}


int ven_get_input_req(struct ven_device* dvenc,
    struct ven_allocator_property* pprop)
{
  if(!pprop || !dvenc)
    return -1;
  memcpy(pprop, &dvenc->input_attrs, sizeof(struct ven_allocator_property));
  return 0;
}


/**************************************************************************
 * @brief
 *************************************************************************/
int ven_set_output_req(struct ven_device* dvenc,
                               struct ven_allocator_property* pprop)
{
  int ret = 0;
  QC_OMX_MSG_MEDIUM("Processing ven_set_output_req...");

  if (dvenc != NULL && pprop != NULL)
  {
    if (pprop->min_count == dvenc->output_attrs.min_count)
    {
      if (pprop->actual_count >= dvenc->output_attrs.min_count)
      {
        if (pprop->data_size >= dvenc->output_attrs.data_size)
        {
          if (pprop->alignment == dvenc->output_attrs.alignment)
          {
            dvenc->output_attrs = *pprop;
          }
          else
          {
            QC_OMX_MSG_ERROR( "alignment is read only");
            ret = VENC_S_EBUFFREQ;
          }
        }
        else
        {
          QC_OMX_MSG_ERROR( "buffer is too small");
          ret = VENC_S_EBUFFREQ;
        }
      }
      else
      {
        QC_OMX_MSG_ERROR( "not enough buffers");
        ret = VENC_S_EBUFFREQ;
      }
    }
    else
    {
      QC_OMX_MSG_ERROR( "min buffer count is read only");
      ret = VENC_S_EBUFFREQ;
    }
  }
  else
  {
    QC_OMX_MSG_ERROR( "null params(s)");
    ret = -1;
  }
  return ret;
}
int ven_get_output_req(struct ven_device* dvenc,
    struct ven_allocator_property* pprop)
{
  if(!pprop || !dvenc)
    return -1;
  memcpy(pprop, &dvenc->output_attrs, sizeof(struct ven_allocator_property));
  return 0;
}

/**************************************************************************
 * @brief
 *************************************************************************/
int ven_set_qp_range(struct ven_device* dvenc,
                     struct ven_qp_range *ptr)
{
  int ret = 0;
  struct venc_qp_range qp;

  memcpy(&(dvenc->config.qp_range), ptr, sizeof(struct ven_qp_range));
  QC_OMX_MSG_HIGH("%s: min = %d, max = %d",__func__,
    (dvenc->config.qp_range).min_qp,
    (dvenc->config.qp_range).max_qp);

  if (dvenc->state == VENC_STATE_START) {

    qp.min_qp = (dvenc->config.qp_range).min_qp;
    qp.max_qp = (dvenc->config.qp_range).max_qp;
    ret = ioctl(dvenc->fd, VENC_IOCTL_SET_QP_RANGE,&qp);
    if (ret) {
      QC_OMX_MSG_ERROR("%s failed (%d) \n", __func__, ret);
      return ret;
    }
  }
  return ret;
}

int ven_get_qp_range(struct ven_device* dvenc,
                             struct ven_qp_range *ptr)
{
  int ret = 0;
  QC_OMX_MSG_HIGH("%s: min = %d, max = %d",__func__,
    (dvenc->config.qp_range).min_qp,
    (dvenc->config.qp_range).max_qp);
  memcpy(ptr, &(dvenc->config.session_qp),
      sizeof(struct ven_qp_range));
  return ret;
}
/**************************************************************************
 * @brief
 *
 * @todo document
 *************************************************************************/
int ven_set_session_qp(struct ven_device* dvenc,
                                struct ven_session_qp *ptr)
{
  int ret = 0;
  memcpy(&(dvenc->config.session_qp), ptr,
      sizeof(struct ven_session_qp));
  QC_OMX_MSG_HIGH("%s: iframe_qp = %d, pframe_qp = %d",__func__,
    (dvenc->config.session_qp).iframe_qp,
    (dvenc->config.session_qp).pframe_qp);
  return ret;
}

int ven_get_session_qp(struct ven_device* dvenc,
                                struct ven_session_qp *ptr)
{
  int ret = 0;
  QC_OMX_MSG_HIGH("%s: iframe_qp = %d, pframe_qp = %d",__func__,
    (dvenc->config.session_qp).iframe_qp,
    (dvenc->config.session_qp).pframe_qp);
  memcpy(ptr, &(dvenc->config.session_qp),
      sizeof(struct ven_session_qp));
  return ret;
}

int ven_set_ac_prediction(struct ven_device* dvenc,
                           struct ven_switch *ptr)
{
  int ret = 0;
  memcpy(&(dvenc->config.ac_prediction), ptr,
      sizeof(struct ven_switch));
  QC_OMX_MSG_HIGH("%s: status = %d",__func__,
    (dvenc->config.ac_prediction).status);
  return ret;
}

int ven_get_ac_prediction(struct ven_device* dvenc,
                           struct ven_switch *ptr)
{
  int ret = 0;
  QC_OMX_MSG_HIGH("%s: status = %d",__func__,
    (dvenc->config.ac_prediction).status);
  memcpy(ptr, &(dvenc->config.ac_prediction),
      sizeof(struct ven_switch));
  return ret;
}

int ven_set_short_hdr(struct ven_device* dvenc,
                           struct ven_switch *ptr)
{
  int ret = 0;
  memcpy(&(dvenc->config.short_header), ptr,
      sizeof(struct ven_switch));
  QC_OMX_MSG_HIGH("%s: status = %d",__func__,
    (dvenc->config.short_header).status);
  return ret;
}

int ven_get_short_hdr(struct ven_device* dvenc,
                           struct ven_switch *ptr)
{
  int ret = 0;
  QC_OMX_MSG_HIGH("%s: status =%d",__func__,
    (dvenc->config.short_header).status);
  memcpy(ptr, &(dvenc->config.short_header),
      sizeof(struct ven_switch));
  return ret;
}

/**************************************************************************
 * @brief
 *
 * @todo document
 *************************************************************************/
int ven_set_intra_period(struct ven_device* dvenc,
                                 struct ven_intra_period *ptr)
{
  int ret = 0;
  unsigned int pnum=0;
  memcpy(&(dvenc->config.intra_period), ptr, sizeof(struct ven_intra_period));
  QC_OMX_MSG_HIGH("%s: num_pframes = %d",__func__,
    (dvenc->config.intra_period).num_pframes);

  if (dvenc->state == VENC_STATE_START) {
    pnum = (dvenc->config.intra_period).num_pframes;
    QC_OMX_MSG_MEDIUM("Process intra period with pnum:%d", pnum);

    ret = ioctl(dvenc->fd, VENC_IOCTL_SET_INTRA_PERIOD, &pnum);
    if (ret) {
      QC_OMX_MSG_ERROR("%s failed (%d) \n", __func__, ret);
      return ret;
    }
  }
  return ret;
}

int ven_get_intra_period(struct ven_device* dvenc,
        struct ven_intra_period* intra_period)
{
  if(!intra_period || !dvenc)
    return -1;
  QC_OMX_MSG_HIGH("%s: num_pframes = %d",__func__,
    (dvenc->config.intra_period).num_pframes);
  memcpy(intra_period, &dvenc->config.intra_period, sizeof(struct ven_intra_period));
  return 0;
}

/**************************************************************************
 * @brief
 *
 * @todo document
 *************************************************************************/
int ven_set_frame_rate(struct ven_device* dvenc,
                       struct ven_frame_rate *ptr)
{
  int ret = 0;
  struct venc_frame_rate pdata;

  memcpy(&(dvenc->config.frame_rate), ptr, sizeof(struct ven_frame_rate));
  QC_OMX_MSG_HIGH("%s: fps_numerator = %d, fps_denominator = %d",__func__,
    (dvenc->config.frame_rate).fps_numerator,
    (dvenc->config.frame_rate).fps_denominator);

  if (dvenc->state == VENC_STATE_START)
  {
    ptr = &(dvenc->config.frame_rate);
    pdata.frame_rate_den = ptr->fps_denominator;
    pdata.frame_rate_num = ptr->fps_numerator;

    ret = ioctl(dvenc->fd, VENC_IOCTL_SET_FRAME_RATE, &pdata);
    if (ret) {
      QC_OMX_MSG_ERROR("%s failed (%d) \n", __func__, ret);
      return ret;
    }
  }
  return ret;
}

int ven_get_frame_rate(struct ven_device* dvenc,
    struct ven_frame_rate* frame_rate)
{
  if(!frame_rate || !dvenc)
    return -1;
  QC_OMX_MSG_HIGH("%s: fps_numerator = %d, fps_denominator = %d",__func__,
    (dvenc->config.frame_rate).fps_numerator,
    (dvenc->config.frame_rate).fps_denominator);
  memcpy(frame_rate, &dvenc->config.frame_rate, sizeof(struct ven_frame_rate));
  return 0;
}

/**************************************************************************
 * @brief
 *
 * @todo document
 *************************************************************************/
int ven_set_target_bitrate(struct ven_device* dvenc,
                                   struct ven_target_bitrate *ptr)
{
  int ret = 0;
  unsigned int pdata = 0;

  memcpy(&(dvenc->config.bitrate), ptr, sizeof(struct ven_target_bitrate));
  QC_OMX_MSG_HIGH("%s: target_bitrate = %d",__func__,
    (dvenc->config.bitrate).target_bitrate);

  if (dvenc->state == VENC_STATE_START)
  {
    pdata = (dvenc->config.bitrate).target_bitrate;
    (dvenc->config.base_config).target_bitrate = pdata;
    ret = ioctl(dvenc->fd, VENC_IOCTL_SET_TARGET_BITRATE, &pdata);
    if (ret) {
      QC_OMX_MSG_ERROR("%s: remote function failed (%d) \n", __func__, ret);
      return ret;
    }
  }
  return ret;
}

int ven_get_target_bitrate(struct ven_device* dvenc,
                                   struct ven_target_bitrate *ptr)
{
  if(!ptr || !dvenc)
    return -1;
  QC_OMX_MSG_HIGH("%s: target_bitrate = %d",__func__,
    (dvenc->config.bitrate).target_bitrate);
  memcpy(ptr, &dvenc->config.bitrate, sizeof(struct ven_target_bitrate));
  return 0;
}
int ven_request_iframe(struct ven_device* dvenc)
{
  if(!dvenc)
    return -1;
  QC_OMX_MSG_HIGH("%s: REQUEST_IFRAME", __func__);
  int rc = ioctl(dvenc->fd, VENC_IOCTL_CMD_REQUEST_IFRAME);
  if(rc) {
    QC_OMX_MSG_ERROR("%s: err: %d \n", __func__, rc);
  }
  return rc;
}


int ven_set_intra_refresh_rate(struct ven_device* dvenc,
    struct ven_intra_refresh* intra_ref)
{
  int ret = 0;
  unsigned int pnum;
  if(!intra_ref || !dvenc)
    return -1;

  memcpy(&dvenc->config.intra_refresh, intra_ref, sizeof(struct ven_intra_refresh));
  QC_OMX_MSG_HIGH("%s: mode = %d, mb_count = %d",__func__,
    (dvenc->config.intra_refresh).ir_mode,
    (dvenc->config.intra_refresh).mb_count);
  pnum = dvenc->config.intra_refresh.mb_count;
  ret = ioctl(dvenc->fd, VENC_IOCTL_SET_INTRA_REFRESH, &pnum);
  if (ret) {
    QC_OMX_MSG_ERROR("%s failed (%d) \n", __func__, ret);
    return ret;
  }
  return 0;
}
int ven_get_intra_refresh_rate(struct ven_device* dvenc,
    struct ven_intra_refresh* intra_ref)
{
  if(!intra_ref || !dvenc)
    return -1;
  QC_OMX_MSG_HIGH("%s: mode = %d, mb_count = %d",__func__,
    (dvenc->config.intra_refresh).ir_mode,
    (dvenc->config.intra_refresh).mb_count);
  memcpy(intra_ref,&dvenc->config.intra_refresh, sizeof(struct ven_intra_refresh));
  return 0;
}

int ven_set_multislice_cfg(struct ven_device* dvenc,
    struct ven_multi_slice_cfg* multi_slice_cfg)
{
  if(!multi_slice_cfg || !dvenc)
    return -1;
  memcpy(&dvenc->config.multi_slice, multi_slice_cfg, sizeof(struct ven_multi_slice_cfg));
  QC_OMX_MSG_HIGH("%s: mode = %d, size = %d",__func__,
    (dvenc->config.multi_slice).mslice_mode,
    (dvenc->config.multi_slice).mslice_size);
  return 0;
}
int ven_get_multislice_cfg(struct ven_device* dvenc,
    struct ven_multi_slice_cfg* multi_slice_cfg)
{
  if(!multi_slice_cfg || !dvenc)
    return -1;
  QC_OMX_MSG_HIGH("%s: mode = %d, size = %d",__func__,
    (dvenc->config.multi_slice).mslice_mode,
    (dvenc->config.multi_slice).mslice_size);
  memcpy(multi_slice_cfg, &dvenc->config.multi_slice, sizeof(struct ven_multi_slice_cfg));
  return 0;
}
int ven_set_rate_control(struct ven_device* dvenc,
    struct ven_rate_ctrl_cfg* rate_ctrl)
{
  if(!rate_ctrl || !dvenc)
    return -1;
  memcpy(&dvenc->config.rate_control, rate_ctrl, sizeof(struct ven_rate_ctrl_cfg));
  QC_OMX_MSG_HIGH("%s: rc_mode = %d",__func__,
    (dvenc->config.rate_control).rc_mode);
  return 0;
}
int ven_get_rate_control(struct ven_device* dvenc,
    struct ven_rate_ctrl_cfg* rate_ctrl)
{
  if(!rate_ctrl || !dvenc)
    return -1;
  QC_OMX_MSG_HIGH("%s: rc_mode = %d",__func__,
   (dvenc->config.rate_control).rc_mode);
  memcpy(rate_ctrl, &dvenc->config.rate_control, sizeof(struct ven_rate_ctrl_cfg));
  return 0;
}
int ven_set_vop_timing(struct ven_device* dvenc,
    struct ven_vop_timing_cfg* vop_timing)
{
  if(!vop_timing || !dvenc)
    return -1;
  memcpy(&dvenc->config.vop_timing, vop_timing, sizeof(struct ven_vop_timing_cfg));
  QC_OMX_MSG_HIGH("%s: vop_time_resolution = %d",__func__,
    (dvenc->config.vop_timing).vop_time_resolution);
  return 0;
}
int ven_get_vop_timing(struct ven_device* dvenc,
    struct ven_vop_timing_cfg* vop_timing)
{
  if(!vop_timing || !dvenc)
    return -1;
  QC_OMX_MSG_HIGH("%s: vop_time_resolution = %d",__func__,
    (dvenc->config.vop_timing).vop_time_resolution);
  memcpy(vop_timing, &dvenc->config.vop_timing, sizeof(struct ven_vop_timing_cfg));
  return 0;
}
int ven_set_rotation(struct ven_device* dvenc,
    struct ven_rotation* rotation)
{
  if(!rotation || !dvenc)
    return -1;
  memcpy(&dvenc->config.rotation, rotation, sizeof(struct ven_rotation));
  QC_OMX_MSG_HIGH("%s: rotation = %d",__func__,
    (dvenc->config.rotation).rotation);
  return 0;
}
int ven_get_rotation(struct ven_device* dvenc,
    struct ven_rotation* rotation)
{
  if(!rotation || !dvenc)
    return -1;
  QC_OMX_MSG_HIGH("%s: rotation = %d",__func__,
    (dvenc->config.rotation).rotation);
  memcpy(rotation, &dvenc->config.rotation, sizeof(struct ven_rotation));
  return 0;
}
int ven_set_hec(struct ven_device* dvenc,
    struct ven_header_extension* hex)
{
  if(!hex || !dvenc)
    return -1;
  memcpy(&dvenc->config.header_extension, hex, sizeof(struct ven_header_extension));
  QC_OMX_MSG_HIGH("%s: hec_interval = %d",__func__,
    (dvenc->config.header_extension).hec_interval);
  return 0;
}
int ven_get_hec(struct ven_device* dvenc,
    struct ven_header_extension* hex)
{
  if(!hex || !dvenc)
    return -1;
  QC_OMX_MSG_HIGH("%s: hec_interval = %d",__func__,
    (dvenc->config.header_extension).hec_interval);
  memcpy(hex, &dvenc->config.header_extension, sizeof(struct ven_header_extension));
  return 0;
}
int ven_set_data_partition(struct ven_device* dvenc,
    struct ven_switch* dp)
{
  if(!dp || !dvenc)
    return -1;
  memcpy(&dvenc->config.data_partition, dp, sizeof(struct ven_switch));
  QC_OMX_MSG_HIGH("%s: status = %d",__func__,
    (dvenc->config.data_partition).status);
  return 0;
}
int ven_get_data_partition(struct ven_device* dvenc,
    struct ven_switch* dp)
{
  if(!dp || !dvenc)
    return -1;
  QC_OMX_MSG_HIGH("%s: status = %d",__func__,
    (dvenc->config.data_partition).status);
  memcpy(dp, &dvenc->config.data_partition, sizeof(struct ven_switch));
  return 0;
}
int ven_get_base_cfg(struct ven_device * dvenc,
    struct ven_base_cfg* bcfg)
{
  if(!bcfg || !dvenc)
    return -1;
  QC_OMX_MSG_HIGH("get base_config");
  memcpy(bcfg, &dvenc->config.base_config, sizeof(struct ven_base_cfg));
  return 0;
}

int ven_set_base_cfg(struct ven_device * dvenc,
                 struct ven_base_cfg* pcfg)
{
  struct ven_base_cfg *pconfig;
  int new_codec;
  int result = 0;
  QC_OMX_MSG_HIGH("set base_config");
  if (dvenc == NULL || pcfg == NULL) {
    QC_OMX_MSG_ERROR( "null driver");
    return -1;
  }

  new_codec = (pcfg->codec_type != dvenc->config.base_config.codec_type);
  pconfig = &(dvenc->config.base_config);
  memcpy(pconfig, pcfg, sizeof(struct ven_base_cfg));
  dvenc->config.frame_rate.fps_numerator = pconfig->fps_num;
  dvenc->config.frame_rate.fps_denominator = pconfig->fps_den;
  dvenc->config.bitrate.target_bitrate = pconfig->target_bitrate;
  dvenc->input_attrs.data_size = pconfig->input_width * pconfig->input_height * 3 / 2;
  dvenc->output_attrs.data_size = pconfig->input_width * pconfig->input_height * 3 / 2;
  dvenc->config.intra_period.num_pframes = pconfig->fps_num * 2 - 1;
  /* removed the vop_time_resolution resetting to default value as we have already set it */
  /* properly via set_parameter call */
  //dvenc->config.vop_timing.vop_time_resolution = pconfig->fps_num * 2;
  QC_OMX_MSG_HIGH("fps = %d, bitrate = %d, intra period = %d",
    dvenc->config.frame_rate.fps_numerator,
	dvenc->config.bitrate.target_bitrate,
	dvenc->config.intra_period.num_pframes);
  QC_OMX_MSG_HIGH("input width = %d, height = %d",
    pconfig->input_width,
	pconfig->input_height);
  QC_OMX_MSG_HIGH("input buffer size = %d, output buffer size = %d",
    dvenc->input_attrs.data_size,
	dvenc->output_attrs.data_size);

  if (new_codec)
  {
    ven_change_codec(dvenc);
  }

  result = ven_update_buf_properties(dvenc);

  return 0;
}
int ven_set_codec_profile(struct ven_device* dvenc,
    struct ven_profile* prof)
{
  if(!prof || !dvenc)
    return -1;
  memcpy(&dvenc->config.profile, prof, sizeof(struct ven_profile));
  QC_OMX_MSG_HIGH("profile = %d", (dvenc->config.profile).profile);
  return 0;
}
int ven_get_codec_profile(struct ven_device* dvenc,
    struct ven_profile* prof)
{
  if(!prof || !dvenc)
    return -1;
  QC_OMX_MSG_HIGH("profile = %d", (dvenc->config.profile).profile);
  memcpy(prof, &dvenc->config.profile, sizeof(struct ven_profile));
  return 0;
}
int ven_set_profile_level(struct ven_device* dvenc,
    struct ven_profile_level* prof_level)
{
  int result = 0;

  if(!prof_level || !dvenc)
    return -1;
  memcpy(&dvenc->config.profile_level, prof_level, sizeof(struct ven_profile_level));
  QC_OMX_MSG_HIGH("level = %d", (dvenc->config.profile_level).level);
  result = ven_update_buf_properties(dvenc);

  return result;
}
int ven_get_profile_level(struct ven_device* dvenc,
    struct ven_profile_level* prof_level)
{
  if(!prof_level || !dvenc)
    return -1;
  QC_OMX_MSG_HIGH("level = %d", (dvenc->config.profile_level).level);
  memcpy(prof_level, &dvenc->config.profile_level, sizeof(struct ven_profile_level));
  return 0;
}


int ven_device_open(struct ven_device** handle)
{
  int fd;

  QC_OMX_MSG_HIGH("%s", __func__);
  struct ven_device* dvenc = (struct ven_device *)malloc(sizeof(struct ven_device));
  if (!dvenc) return -1;

  *handle = dvenc;
  QC_OMX_MSG_HIGH("%s: dvenc:%p pconfig: %p", __func__, dvenc, &(dvenc->config.base_config));
  fd = open("/dev/q6venc", O_RDWR);
  if (fd < 0)
  {
    QC_OMX_MSG_ERROR("Cannot open /dev/q6venc ");
    close(fd);
    return -1;
  }
  dvenc->fd = fd;

  (void) ven_set_default_config(dvenc);
  (void) ven_set_default_buf_properties(dvenc);
  dvenc->config.callback_event = NULL;
  dvenc->state = VENC_STATE_STOP;

  //  dvenc->is_active = 1;
  QC_OMX_MSG_HIGH("%s  = %d", __func__,fd);
  return fd;
}

int ven_device_close(struct ven_device* handle)
{
  int ret = 0;
  struct ven_device *dvenc;

  QC_OMX_MSG_HIGH("%s", __func__);

  dvenc = (struct ven_device *)handle;
  if (dvenc) {
    close(dvenc->fd);
  }

  free(dvenc);
  return ret;
}
