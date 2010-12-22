/*--------------------------------------------------------------------------
Copyright (c) 2009, Code Aurora Forum. All rights reserved.

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
#ifndef H264_UTILS_H
#define H264_UTILS_H

/*========================================================================

                                 O p e n M M
         U t i l i t i e s   a n d   H e l p e r   R o u t i n e s

*//** @file H264_Utils.h
This module contains H264 video decoder utilities and helper routines.

*//*====================================================================== */


/* =======================================================================

                     INCLUDE FILES FOR MODULE

========================================================================== */
#include<stdio.h>
#include "Map.h"
#include "qtypes.h"
#include "OMX_Core.h"

#define STD_MIN(x,y) (((x) < (y)) ? (x) : (y))

/* =======================================================================

                        DATA DECLARATIONS

========================================================================== */

/* -----------------------------------------------------------------------
** Constant / Define Declarations
** ----------------------------------------------------------------------- */
// Common format block header definitions
#define MT_VIDEO_META_STREAM_HEADER             0x00
#define MT_VIDEO_MEDIA_STREAM_HEADER            0x01
#define MT_VIDEO_META_MEDIA_STREAM_HEADER       0x02

// H.264 format block header definitions
#define MT_VIDEO_H264_ACCESS_UNIT_FORMAT        0x00
#define MT_VIDEO_H264_NAL_FORMT                 0x01
#define MT_VIDEO_H264_BYTE_FORMAT               0x02
#define MT_VIDEO_H264_BYTE_STREAM_FORMAT        0x00
#define MT_VIDEO_H264_NAL_UNIT_STREAM_FORMAT    0x01
#define MT_VIDEO_H264_FORMAT_BLOCK_HEADER_SIZE  18

// MPEG-4 format block header definitions
#define MT_VIDEO_MPEG4_VOP_FORMAT               0x00
#define MT_VIDEO_MPEG4_SLICE_FORMAT             0x01
#define MT_VIDEO_MPEG4_BYTE_FORMAT              0x02
#define MT_VIDEO_MPEG4_FORMAT_BLOCK_HEADER_SIZE 15

// H.263 format block header definitions
#define MT_VIDEO_H263_PICTURE_FORMAT            0x00
#define MT_VIDEO_H263_GOB_FORMAT                0x01
#define MT_VIDEO_H263_SLICE_STRUCTURED_FORMAT   0x02
#define MT_VIDEO_H263_BYTE_FORMAT               0x03
#define MT_VIDEO_H263_FORMAT_BLOCK_HEADER_SIZE  16

/* =======================================================================
**                          Function Declarations
** ======================================================================= */

/* -----------------------------------------------------------------------
** Type Declarations
** ----------------------------------------------------------------------- */

// This type is used when parsing an H.264 bitstream to collect H.264 NAL
// units that need to go in the meta data.
struct H264ParamNalu {
   uint32 picSetID;
   uint32 seqSetID;
   uint32 picOrderCntType;
   bool frameMbsOnlyFlag;
   bool picOrderPresentFlag;
   uint32 picWidthInMbsMinus1;
   uint32 picHeightInMapUnitsMinus1;
   uint32 log2MaxFrameNumMinus4;
   uint32 log2MaxPicOrderCntLsbMinus4;
   bool deltaPicOrderAlwaysZeroFlag;
   //std::vector<uint8> nalu;
   uint32 nalu;
   uint32 crop_left;
   uint32 crop_right;
   uint32 crop_top;
   uint32 crop_bot;
};
//typedef map<uint32, H264ParamNalu> H264ParamNaluSet;
typedef Map < uint32, H264ParamNalu * >H264ParamNaluSet;

typedef enum {
   NALU_TYPE_UNSPECIFIED = 0,
   NALU_TYPE_NON_IDR,
   NALU_TYPE_PARTITION_A,
   NALU_TYPE_PARTITION_B,
   NALU_TYPE_PARTITION_C,
   NALU_TYPE_IDR,
   NALU_TYPE_SEI,
   NALU_TYPE_SPS,
   NALU_TYPE_PPS,
   NALU_TYPE_ACCESS_DELIM,
   NALU_TYPE_EOSEQ,
   NALU_TYPE_EOSTREAM,
   NALU_TYPE_FILLER_DATA,
   NALU_TYPE_RESERVED,
} NALU_TYPE;

// NAL header information
typedef struct {
   uint32 nal_ref_idc;
   uint32 nalu_type;
   uint32 forbidden_zero_bit;
} NALU;

// This structure contains persistent information about an H.264 stream as it
// is parsed.
//struct H264StreamInfo {
//    H264ParamNaluSet pic;
//    H264ParamNaluSet seq;
//};

class RbspParser
/******************************************************************************
 ** This class is used to convert an H.264 NALU (network abstraction layer
 ** unit) into RBSP (raw byte sequence payload) and extract bits from it.
 *****************************************************************************/
{
      public:
   RbspParser(const uint8 * begin, const uint8 * end);

    virtual ~ RbspParser();

   uint32 next();
   void advance();
   uint32 u(uint32 n);
   uint32 ue();
   int32 se();

      private:
   const uint8 *begin, *end;
   int32 pos;
   uint32 bit;
   uint32 cursor;
   bool advanceNeeded;
};

class H264_Utils {
      public:
   H264_Utils();
   ~H264_Utils();
   void initialize_frame_checking_environment();
   void allocate_rbsp_buffer(uint32 inputBufferSize);
   bool isNewFrame(OMX_IN OMX_U8 * bitstream,
         OMX_IN OMX_U32 bitstream_length,
         OMX_IN OMX_U32 size_of_nal_length_field,
         OMX_OUT OMX_BOOL & isNewFrame,
         bool & isUpdateTimestamp);
   bool parseHeader(uint8 * encodedBytes,
          uint32 totalBytes,
          uint32 nal_len,
          unsigned &height,
          unsigned &width,
          bool & bInterlace,
          unsigned &cropx,
          unsigned &cropy, unsigned &cropdx, unsigned &cropdy);
   OMX_U32 parse_first_h264_input_buffer(OMX_IN OMX_BUFFERHEADERTYPE *
                     buffer,
                     OMX_U32 size_of_nal_length_field);
   OMX_U32 check_header(OMX_IN OMX_BUFFERHEADERTYPE * buffer,
              OMX_U32 sizeofNAL, bool & isPartial,
              OMX_U32 headerState);

      private:
    boolean extract_rbsp(OMX_IN OMX_U8 * buffer,
               OMX_IN OMX_U32 buffer_length,
               OMX_IN OMX_U32 size_of_nal_length_field,
               OMX_OUT OMX_U8 * rbsp_bistream,
               OMX_OUT OMX_U32 * rbsp_length,
               OMX_OUT NALU * nal_unit);
   bool validate_profile_and_level(uint32 profile, uint32 level);

   bool m_default_profile_chk;
   bool m_default_level_chk;
   unsigned m_height;
   unsigned m_width;
   H264ParamNaluSet pic;
   H264ParamNaluSet seq;
   uint8 *m_rbspBytes;
   NALU m_prv_nalu;
   bool m_forceToStichNextNAL;
};

#endif /* H264_UTILS_H */
