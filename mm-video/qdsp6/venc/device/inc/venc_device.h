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

#ifndef _VENC_DEVICE_H
#define _VENC_DEVICE_H

#include <linux/msm_q6venc.h>

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================

                      DEFINITIONS AND DECLARATIONS

===========================================================================*/

/* -----------------------------------------------------------------------
** Constants & Macros
** ----------------------------------------------------------------------- */

/*--------------------------------------------------------------------------*/

/*
 * STATUS CODES
 */


/*--------------------------------------------------------------------------*/

/**
 * INTERFACE VERSION
 *
 * Note: Interface version changes only if existing structures are modified.
 */
#define VEN_INTF_VER       1
#define VEN_FRAME_TYPE_I      1     /**< I frame type */
#define VEN_FRAME_TYPE_P      2     /**< P frame type */
#define VEN_FRAME_TYPE_B      3     /**< B frame type */

/*--------------------------------------------------------------------------*/

/*
 * Video codec types.
 */
#define VEN_CODEC_MPEG4          1     /**< MPEG4 Codec */
#define VEN_CODEC_H264           2     /**< H.264 Codec */
#define VEN_CODEC_H263           3     /**< H.263 Codec */

/*
 * Video codec profile types.
 */
#define VEN_PROFILE_MPEG4_SP        1     /**< 1 - MPEG4 SP profile  */
#define VEN_PROFILE_MPEG4_ASP       2     /**< 2 - MPEG4 ASP profile */
#define VEN_PROFILE_H264_BASELINE   3     /**< 3 - H264 Baseline profile  */
#define VEN_PROFILE_H264_MAIN       4     /**< 4 - H264 Main profile      */
#define VEN_PROFILE_H264_HIGH       5     /**< 5 - H264 High profile      */
#define VEN_PROFILE_H263_BASELINE   6     /**< 6 - H263 Baseline profile */

/*
 * Video codec profile level types.
 */
#define VEN_LEVEL_MPEG4_0           0x1     /**< MPEG4 Level 0   */
#define VEN_LEVEL_MPEG4_0B          0x2     /**< MPEG4 Level 0b  */
#define VEN_LEVEL_MPEG4_1           0x3     /**< MPEG4 Level 1   */
#define VEN_LEVEL_MPEG4_2           0x4     /**< MPEG4 Level 2   */
#define VEN_LEVEL_MPEG4_3           0x5     /**< MPEG4 Level 3   */
#define VEN_LEVEL_MPEG4_3B          0x6     /**< MPEG4 Level 3b  */
#define VEN_LEVEL_MPEG4_4           0x7     /**< MPEG4 Level 4   */
#define VEN_LEVEL_MPEG4_4A          0x8     /**< MPEG4 Level 4a  */
#define VEN_LEVEL_MPEG4_5           0x9     /**< MPEG4 Level 5   */
#define VEN_LEVEL_MPEG4_6           0xA     /**< MPEG4 Level 6   */

#define VEN_LEVEL_H264_1            0x8     /**< H.264 Level 1   */
#define VEN_LEVEL_H264_1B           0x9     /**< H.264 Level 1b  */
#define VEN_LEVEL_H264_1P1          0xA     /**< H.264 Level 1.1 */
#define VEN_LEVEL_H264_1P2          0xB     /**< H.264 Level 1.2 */
#define VEN_LEVEL_H264_1P3          0xC     /**< H.264 Level 1.3 */
#define VEN_LEVEL_H264_2            0xD     /**< H.264 Level 2   */
#define VEN_LEVEL_H264_2P1          0xE     /**< H.264 Level 2.1 */
#define VEN_LEVEL_H264_2P2          0xF     /**< H.264 Level 2.2 */
#define VEN_LEVEL_H264_3            0x10    /**< H.264 Level 3   */
#define VEN_LEVEL_H264_3P1          0x11    /**< H.264 Level 3.1 */

#define VEN_LEVEL_H263_10           0x12    /**< H.263 Level 10  */
#define VEN_LEVEL_H263_20           0x13    /**< H.263 Level 20  */
#define VEN_LEVEL_H263_30           0x14    /**< H.263 Level 30  */
#define VEN_LEVEL_H263_40           0x15    /**< H.263 Level 40  */
#define VEN_LEVEL_H263_45           0x16    /**< H.263 Level 45  */
#define VEN_LEVEL_H263_50           0x17    /**< H.263 Level 50  */
#define VEN_LEVEL_H263_60           0x18    /**< H.263 Level 60  */
#define VEN_LEVEL_H263_70           0x19    /**< H.263 Level 70  */

#define VEN_FRAME_SIZE_IN_RANGE(w1, h1, w2, h2)  \
    ((w1) * (h1) <= (w2) * (h2))

#define VEN_QCIF_DX                 176
#define VEN_QCIF_DY                 144

#define VEN_WQVGA_DX                400
#define VEN_WVQGA_DY                240

#define VEN_CIF_DX                  352
#define VEN_CIF_DY                  288

#define VEN_VGA_DX                  640
#define VEN_VGA_DY                  480

#define VEN_PAL_DX                  720
#define VEN_PAL_DY                  576

#define VEN_HD720P_DX               1280
#define VEN_HD720P_DY               720

/*--------------------------------------------------------------------------*/

/*
 * Entropy coding model selection for H.264 encoder.
 */
#define VEN_ENTROPY_MODEL_CAVLC     1      /**< Context-based Adaptive Variable
                                            *   Length Coding (CAVLC)
                                            */
#define VEN_ENTROPY_MODEL_CABAC     2      /**< Context-based Adaptive Binary
                                            *   Arithmetic Coding (CABAC)
                                            */

/*
 * Cabac model number (0,1,2) for encoder.
 */
#define VEN_CABAC_MODEL_0     1        /**< CABAC Model 0. */
#define VEN_CABAC_MODEL_1     2        /**< CABAC Model 1. */
#define VEN_CABAC_MODEL_2     3        /**< CABAC Model 2. */

/*--------------------------------------------------------------------------*/

/*
 * Deblocking filter control type for encoder.
 */
#define VEN_DB_DISABLE                 1        /**< 1 - Disable deblocking
                                                 *   filter
                                                 */
#define VEN_DB_ALL_BLKG_BNDRY          2        /**< 2 - All blocking boundary
                                                 *   filtering
                                                 */
#define VEN_DB_SKIP_SLICE_BNDRY        3        /**< 3 - Filtering except slice
                                                 *   boundary
                                                 */

/*--------------------------------------------------------------------------*/


/*
 * Different methods of Multi slice selection.
 */
#define VEN_MSLICE_OFF           1       /**< 1 - Multi slice OFF */
#define VEN_MSLICE_CNT_MB        2       /**< 2 - Multi slice by number of MBs
                                          *   count per slice
                                          */
#define VEN_MSLICE_CNT_BYTE      3       /**< 3 - Multi slice by number of
                                          *   bytes count per slice. Required
                                          *   minimum byte count value is
                                          *   1920. TODO: check for Q6
                                          */
#define VEN_MSLICE_GOB           4       /**< 4 - Multi slice by GOB for
                                          *   H.263 only.
                                          */

/*--------------------------------------------------------------------------*/

/*
 * Different modes for Rate Control.
 */
#define VEN_RC_OFF               0       /**< Rate Control is OFF.
                                          *   It is not recommended as it will
                                          *   not obey bitrate requirement
                                          */
#define VEN_RC_VBR_VFR           1       /**< Variable bit rate; Variable frame
                                          *   rate. i.e frames may be skipped.
                                          *   Mainly used for TODO:
                                          */
#define VEN_RC_VBR_VFR_TIGHT     2       /**< Variable bit rate; Variable frame
                                          *   rate with tighter RC.
                                          */
#define VEN_RC_VBR_CFR           3       /**< Variable bit rate; Constant frame
                                          *   rate. i.e frames are not skipped.
                                          *   Mainly used for higher quality
                                          *   expectation appliation.
                                          *   (e.g. camcorder)
                                          */
#define VEN_RC_CBR_VFR           4       /**< Constant bit rate; Variable frame
                                          *   rate. i.e frames may be skipped.
                                          *   Mainly used for strict rate
                                          *   control scenario application.
                                          *   (e.g. video telephony)
                                          */

/*--------------------------------------------------------------------------*/

/*
 * Different modes for flushing buffers
 */


/*--------------------------------------------------------------------------*/

/*
 * Different input formats for YUV data.
 */
#define VEN_INPUTFMT_NV12        1       /**< NV12 Linear */
#define VEN_INPUTFMT_NV21        2       /**< NV21 Linear */


/*--------------------------------------------------------------------------*/

/*
 * Different allowed rotation modes.
 */
#define VEN_ROTATION_0           0     /**< 0 degrees */
#define VEN_ROTATION_90          1     /**< 90 degrees */
#define VEN_ROTATION_180         2     /**< 180 degrees */
#define VEN_ROTATION_270         3     /**< 270 degrees */

/*--------------------------------------------------------------------------*/

/*
 * IOCTL timeout values
 */
#define VEN_TIMEOUT_INFINITE     0xffffffff  /**< Infinite timeout value.
                                              *   i.e. don't timeout.
                                              */

/*--------------------------------------------------------------------------*/

/**
 * Different allowed intra refresh modes.
 */
#define VEN_IR_OFF              1         /**< Intra refresh is disabled */
#define VEN_IR_CYCLIC           2         /**< Cyclic IR mode. Here configured
                                           *   no. of consecutive macroblocks
                                           *   are intra coded.
                                           */
#define VEN_IR_RANDOM           3         /**< Random IR mode. Here configured
                                           *   no. of macroblocks are randomly
                                           *   picked per frame by an internal
                                           *   algorithm for intra coding.
                                           */

enum q6_frame_type_enum
{
  VENC_IDR_FRAME,
  VENC_I_FRAME,
  VENC_P_FRAME
};

enum q6_rotation_angle_enum
{
  VENC_ROTATION_0,
  VENC_ROTATION_90,
  VENC_ROTATION_180,
  VENC_ROTATION_270
};

enum q6_rate_control_enum
{
  VENC_RC_OFF,
  VENC_RC_VBR_VFR,
  VENC_RC_VBR_VFR_TIGHT,
  VENC_RC_VBR_CFR,
  VENC_RC_CBR_VFR
};

enum q6_codec_type_enum
{
  VENC_CODEC_MPEG4,
  VENC_CODEC_H263,
  VENC_CODEC_H264
};

enum q6_mpeg4_profile_enum
{
  VENC_MPEG4_PROFILE_SIMPLE
};

enum q6_mpeg4_level_enum
{
  VENC_MPEG4_LEVEL_0,
  VENC_MPEG4_LEVEL_0B,
  VENC_MPEG4_LEVEL_1,
  VENC_MPEG4_LEVEL_2,
  VENC_MPEG4_LEVEL_3,
  //VENC_MPEG4_LEVEL_3B,
  //VENC_MPEG4_LEVEL_4,
  VENC_MPEG4_LEVEL_4A,
  //VENC_MPEG4_LEVEL_4B,
  VENC_MPEG4_LEVEL_5,
  VENC_MPEG4_LEVEL_6
};

enum q6_h263_profile_enum
{
  VENC_H263_PROFILE_P0,
  VENC_H263_PROFILE_P3
};

enum q6_h263_level_enum
{
  VENC_H263_LEVEL_10,
  VENC_H263_LEVEL_20,
  VENC_H263_LEVEL_30,
  VENC_H263_LEVEL_40,
  VENC_H263_LEVEL_45,
  VENC_H263_LEVEL_50,
  VENC_H263_LEVEL_60,
  VENC_H263_LEVEL_70
};

enum q6_h264_profile_enum
{
  VENC_H264_PROFILE_BASELINE
};

enum q6_h264_level_enum
{
  VENC_H264_LEVEL_1,
  VENC_H264_LEVEL_1B,
  VENC_H264_LEVEL_1P1,
  VENC_H264_LEVEL_1P2,
  VENC_H264_LEVEL_1P3,
  VENC_H264_LEVEL_2,
  VENC_H264_LEVEL_2P1,
  VENC_H264_LEVEL_2P2,
  VENC_H264_LEVEL_3,
  VENC_H264_LEVEL_3P1
};

enum q6_slice_mode_enum
{
  VENC_SLICE_MODE_DEFAULT,
  VENC_SLICE_MODE_GOB,
  VENC_SLICE_MODE_MB,
  VENC_SLICE_MODE_BIT
};


struct ven_switch
{
  unsigned char           status;       /**< TRUE (1) if setting is enabled
                 *   or ON,
                 *   FALSE (0) if disabled or OFF.
                 */
};

struct ven_allocator_property
{
  unsigned int  min_count;    /**< Minimum number of buffers required. */
  unsigned int  actual_count;       /**< When used in Get: \n
             *     If there hasn't been a Set yet: \n
             *       It's the recommended number of buffers
             *       to allocate for optimal performance.\n
             *     If there was a Set made: \n
             *       The value provided in last Set is
             *       returned. \n
             *
             *   When used in Set:
             *     It should be the actual number of
             *     buffers that are allocated or are to be
             *     allocated.
             */
  unsigned int  data_size;    /**< Data size of each buffer in bytes ( i.e
             *   size excluding suffix bytes )
             *   This is the region that will contain the
             *   YUV or bitstream content.
             */
  unsigned int  suffix_size;  /**< Number of bytes as suffix;
             *   A suffix of these many bytes follows the
             *   buffer data region.
             *   This region could contain metadata.
             */
  unsigned int  alignment;   /**< Buffer's address (start of data region)
            *   would be aligned to multiple of these
            *   many bytes.
            *   The start of suffix would always be
            *   aligned to 4 bytes (32 bits)
            */
  unsigned int  buf_pool_id;   /**< Unique pool identifier which can be set
              *   for buffer allocations. This can come
              *   from driver or come from some local
              *   policy component might have.
              */

};

struct ven_buffer_payload
{
  unsigned char*   buffer;       /**< Virtual pointer to buffer */
  unsigned int   size;         /**< Size of buffer in bytes.
              *   Field can be ignored in few IOCTLs
              *   Refer to the individual IOCTLs for
              *   details.
              */

};

struct ven_base_cfg
{
  unsigned int  input_width;     /**< Input YUV frame width */
  unsigned int  input_height;    /**< Input YUV frame height */
  unsigned int  dvs_width;    /**< Encoded frame width. Used only
             *   in Digital Video Stabilization (DVS).
             *   In DVS, nDVSWidth < nIpWidth &
             *     (nIpWidth - nDVSWidth) is used for
             *      DVS horizontal margin. \n
             *   If DVS is not being used, nDVSWidth should
             *   be set equal to nIpWidth. \n
             */
  unsigned int  dvs_height;   /**< Encoded frame height. Used only in
             *   Digital Video Stabilization (DVS).
             *   For DVS, nDVSHeight < nIpHeight &
             *     (nIpHeight - nDVSHeight) is used for
             *      DVS vertical margin. \n
             *   If DVS is not being used, nDVSHeight
             *   should be set equal to nIpHeight. \n
             */
  unsigned int  codec_type;   /**< Video codec type \n
             *   Refer to codec type constants in the
             *   constants section above. \n
             *    VEN_CODEC_MPEG4 - For MPEG4 simple
             *                     profile is defaulted \n
             *    VEN_CODEC_H264 - H264 baseline
             *                     profile is defaulted \n
             *    VEN_CODEC_H263 - H263 baseline
             *                     profile is defaulted \n
             */
  unsigned int fps_num;    /**< Numerator of frame rate */
  unsigned int fps_den;  /**< Denominator of frame rate */
  unsigned int target_bitrate;   /**< Target bit rate in bits per sec */
  unsigned int input_format;     /**< Input memory access format; \n
          *   Refer to input format constants in the
          *   constants section above.
          */

};

struct ven_profile
{
  unsigned int   profile;   /**< Codec profile payload.
           *   Refer to codec profile constants in the
           *   constants section above.
           */

};

struct ven_profile_level
{
  unsigned int      level;     /**< Codec-profile specific level payload */

};

struct ven_session_qp
{
  unsigned int          iframe_qp;    /**< QP value for the first I Frame.
               *   This value will be used for all
               *   I frames if rate control is
               *   disabled
               */
  unsigned int          pframe_qp;    /**< QP value for the first P Frame.
               *   This value will be used for all
               *   P frames if rate control is
               *   disabled.
               */

};

struct ven_qp_range
{
  unsigned int             max_qp;      /**< Maximum QP value */
  unsigned int             min_qp;      /**< Minimum QP value */

};

struct ven_intra_period
{
  unsigned int         num_pframes;       /**< No of P-Frames between 2
             *   I-Frames.
             */
};


struct ven_capability
{
  unsigned int       codec_types;
  unsigned int       max_frame_width;
  unsigned int       max_frame_height;
  //min resoln??
  unsigned int       max_target_bitrate;
  unsigned int       max_frame_rate;
  unsigned int       input_formats;
  unsigned char        dvs;

};

struct ven_entropy_cfg
{
  unsigned int       entropy_sel; /**< Set Entropy coding model selection
           *   Refer to entropy coding model constants
           *   in the constants section above.
           */
  unsigned int       cabac_model; /**< Valid only when Entropy Selection
           *   is CABAC. This is model number for
           *   CABAC.
           *   Refer to CABAC model number constants
           *   in the constants section above.
           */

};

struct ven_db_cfg
{
  unsigned int  db_mode;           /**< Deblocking Control
            *   Refer to deblocking mode constants
            *   in the constants section above.
            */
  unsigned int  slice_alpha_offset; /**< Aplha offset value in Slice Header (
             *   slice_alpha_c0_offset_div2).
             *   This value is in 2’s complement form.
             *   This value’s range is -6 to 6
             */
  unsigned int  slice_beta_offset; /**< Beta offset value in Slice Header (
            *   Slice_beta_offset_div2).
            *   This value is in 2’s complement form.
            *   This value’s range is -6 to 6
            */
};

struct ven_intra_refresh
{
  unsigned int     ir_mode;     /**< Intra refresh method to be used.
               *   Refer to intra refresh mode constants
               *   in the constants section above.
               */
  unsigned int     mb_count;    /**< Number of macroblocks to be intra
               *   coded per frame.
               */

};

struct ven_multi_slice_cfg
{
  unsigned int   mslice_mode;  /**< To enable/disable Multi-slice.
              *   Refer to multi slice mode constants
              *   in the constants section above.
              */
  unsigned int   mslice_size;  /**< If nMSliceMode is VEN_MSLICE_OFF or
              *   VEN_MSLICE_GOB, then this field is
              *   ignored.
              *   Otherwise, this is size of slice;
              *   if mode is MB count, then this is
              *   No of MB per slice.
              *   if mode is byte count, then this is
              *   No of bytes per slice.
              */

};


struct ven_rate_ctrl_cfg
{
  unsigned int    rc_mode;    /**< Rate Control mode.
             *   Refer to rate control mode constants
             *   in the constants section above.
             */

};

struct  ven_vop_timing_cfg
{
  unsigned int     vop_time_resolution;  /**< VOP Time resolution value
            *   expressed in number of ticks
            *   per second.
            */

};

struct ven_frame_rate
{
  unsigned int        fps_denominator;  /**< Denominator of frame rate */
  unsigned int        fps_numerator;    /**< Numerator of frame rate */

};

struct ven_target_bitrate
{
  unsigned int        target_bitrate;  /**< target bitrate (bits/second) */

};

struct ven_rotation
{
  unsigned int   rotation;    /**< Degree of frame rotation
             *   Refer to rotation mode constants
             *   in the constants section above.
             */

};

struct ven_timeout
{
  unsigned int     millisec;  /**< Time out interval in milliseconds.
             *   0 = IOCTL will return immediately
             *       (no wait) if operation cannot
             *       be compelted at the time of call.
             *   VEN_TIMEOUT_INFINITE = infinite wait
             *       for operation to be completed.
             *
             *   if 0 < nMilliSec < VEN_TIMEOUT_INFINITE,
             *   IOCTL will return with a timeout status
             *   if operation cannot be completed in
             *   nMilliSec amount of time.
             */

};

struct ven_header_extension
{
  unsigned int    hec_interval;   /**< If 0 then header extension is
               *   disabled.
               *   If > 0, then it specifies number
               *   of consecutive video packets
               *   between header extension codes
               *   i.e, header extension would be
               *   inserted every nHeaderExtension
               *   number of packets.
               */
};

struct ven_seq_header
{
  unsigned char*       pHdrBuf;
  unsigned int         nBufSize;
  unsigned int         nHdrLen;
};

struct ven_config_type
{
  struct ven_base_cfg           base_config;
  struct ven_profile           profile;
  struct ven_profile_level     profile_level;
  struct ven_rate_ctrl_cfg      rate_control;
  struct ven_multi_slice_cfg    multi_slice;
  struct ven_rotation         rotation;
  struct ven_target_bitrate    bitrate;
  struct ven_frame_rate        frame_rate;
  struct ven_intra_refresh     intra_refresh;
  struct ven_intra_period      intra_period;
  struct ven_session_qp        session_qp;
  struct ven_qp_range           qp_range;
  struct ven_vop_timing_cfg     vop_timing;
  struct ven_switch           ac_prediction;
  struct ven_switch           short_header;
  struct ven_header_extension header_extension;
  struct ven_switch           data_partition;
  void   *                    callback_event;
};

struct ven_device {
  int  fd;
  enum venc_state_type state;
  enum venc_state_type pending_state;
  struct ven_config_type config;

  /* Attributes for input buffers */
  struct ven_allocator_property input_attrs;
  /* Attribute for output buffers */
  struct ven_allocator_property output_attrs;

};

#define VEN_PMEM_ALIGN                    4096
#define VEN_PMEM_ALIGN_MASK               0xFFFFF000

int ven_device_open(struct ven_device **ptr);
int ven_device_close(struct ven_device *ptr);
int ven_set_input_req(struct ven_device* dvenc, struct ven_allocator_property* pprop);
int ven_get_input_req(struct ven_device* dvenc,  struct ven_allocator_property* pprop);
int ven_set_output_req(struct ven_device* dvenc, struct ven_allocator_property* pprop);
int ven_get_output_req(struct ven_device* dvenc, struct ven_allocator_property* pprop);
int ven_update_properties(struct ven_device* dvenc, struct ven_base_cfg *pcfg);
int ven_start(struct ven_device *dvenc, struct venc_buffers *q6_bufs);

int ven_get_sequence_hdr(struct ven_device *dvenc, struct venc_pmem *pseq, int *psize);
int ven_set_session_qp(struct ven_device* dvenc, struct ven_session_qp *ptr);
int ven_get_session_qp(struct ven_device* dvenc, struct ven_session_qp *ptr);
int ven_set_qp_range(struct ven_device* dvenc, struct ven_qp_range *ptr);
int ven_get_qp_range(struct ven_device* dvenc, struct ven_qp_range *ptr);
int ven_set_ac_prediction(struct ven_device* dvenc, struct ven_switch *ptr);
int ven_get_ac_prediction(struct ven_device* dvenc, struct ven_switch *ptr);
int ven_set_short_hdr(struct ven_device* dvenc, struct ven_switch *ptr);
int ven_get_short_hdr(struct ven_device* dvenc, struct ven_switch *ptr);
int ven_set_base_cfg(struct ven_device * dvenc, struct ven_base_cfg* bcfg);
int ven_get_base_cfg(struct ven_device * dvenc, struct ven_base_cfg* bcfg);
int ven_set_codec_profile(struct ven_device* dvenc, struct ven_profile* prof);
int ven_get_codec_profile(struct ven_device* dvenc, struct ven_profile* prof);
int ven_set_profile_level(struct ven_device* dvenc, struct ven_profile_level* prof_level);
int ven_get_profile_level(struct ven_device* dvenc, struct ven_profile_level* prof_level);
int ven_set_intra_period(struct ven_device* dvenc, struct ven_intra_period *ptr);
int ven_get_intra_period(struct ven_device* dvenc, struct ven_intra_period* intra_period);
int ven_set_frame_rate(struct ven_device* dvenc, struct ven_frame_rate *ptr);
int ven_get_frame_rate(struct ven_device* dvenc, struct ven_frame_rate* frame_rate);
int ven_set_target_bitrate(struct ven_device* dvenc, struct ven_target_bitrate *ptr);
int ven_get_target_bitrate(struct ven_device* dvenc, struct ven_target_bitrate *ptr);
int ven_request_iframe(struct ven_device* dvenc);
int ven_set_intra_refresh_rate(struct ven_device* dvenc, struct ven_intra_refresh* intra_ref);
int ven_get_intra_refresh_rate(struct ven_device* dvenc, struct ven_intra_refresh* intra_ref);
int ven_set_multislice_cfg(struct ven_device* dvenc, struct ven_multi_slice_cfg* multi_slice_cfg);
int ven_get_multislice_cfg(struct ven_device* dvenc, struct ven_multi_slice_cfg* multi_slice_cfg);
int ven_set_rate_control(struct ven_device* dvenc, struct ven_rate_ctrl_cfg* rate_ctrl);
int ven_get_rate_control(struct ven_device* dvenc, struct ven_rate_ctrl_cfg* rate_ctrl);
int ven_set_vop_timing(struct ven_device* dvenc, struct ven_vop_timing_cfg* vop_timing);
int ven_get_vop_timing(struct ven_device* dvenc, struct ven_vop_timing_cfg* vop_timing);
int ven_set_rotation(struct ven_device* dvenc, struct ven_rotation* rotation);
int ven_get_rotation(struct ven_device* dvenc, struct ven_rotation* rotation);
int ven_set_hec(struct ven_device* dvenc, struct ven_header_extension* hex);
int ven_get_hec(struct ven_device* dvenc, struct ven_header_extension* hex);
int ven_set_data_partition(struct ven_device* dvenc, struct ven_switch* dp);
int ven_get_data_partition(struct ven_device* dvenc, struct ven_switch* dp);

#ifdef __cplusplus
}
#endif
#endif /* _VENC_DEVICE_H_ */
