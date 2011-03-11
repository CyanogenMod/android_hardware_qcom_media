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

                      O p e n M M
         V i d e o   U t i l i t i e s

*//** @file VideoUtils.cpp
  This module contains utilities and helper routines.

@par EXTERNALIZED FUNCTIONS

@par INITIALIZATION AND SEQUENCING REQUIREMENTS
  (none)

*//*====================================================================== */

/* =======================================================================

                     INCLUDE FILES FOR MODULE

========================================================================== */
#include "h264_utils.h"
#include "omx_vdec.h"
#include <string.h>
#include <stdlib.h>

/* =======================================================================

                DEFINITIONS AND DECLARATIONS FOR MODULE

This section contains definitions for constants, macros, types, variables
and other items needed by this module.

========================================================================== */

#define SIZE_NAL_FIELD_MAX  4
#define BASELINE_PROFILE 66
#define MAIN_PROFILE     77
#define HIGH_PROFILE     100

#define MAX_SUPPORTED_LEVEL 32

//#define PARSE_VUI_IN_EXTRADATA
#define PARSE_SEI_IN_EXTRADATA


RbspParser::RbspParser (const uint8 *_begin, const uint8 *_end)
: begin (_begin), end(_end), pos (- 1), bit (0),
cursor (0xFFFFFF), advanceNeeded (true)
{
}

// Destructor
/*lint -e{1540}  Pointer member neither freed nor zeroed by destructor
 * No problem
 */
RbspParser::~RbspParser () {}

// Return next RBSP byte as a word
uint32 RbspParser::next ()
{
    if (advanceNeeded) advance ();
    //return static_cast<uint32> (*pos);
    return static_cast<uint32> (begin[pos]);
}

// Advance RBSP decoder to next byte
void RbspParser::advance ()
{
    ++pos;
    //if (pos >= stop)
    if (begin + pos == end)
    {
        /*lint -e{730}  Boolean argument to function
         * I don't see a problem here
         */
        //throw false;
        DEBUG_PRINT_LOW("H264Parser-->NEED TO THROW THE EXCEPTION...\n");
    }
    cursor <<= 8;
    //cursor |= static_cast<uint32> (*pos);
    cursor |= static_cast<uint32> (begin[pos]);
    if ((cursor & 0xFFFFFF) == 0x000003)
    {
        advance ();
    }
    advanceNeeded = false;
}

// Decode unsigned integer
uint32 RbspParser::u (uint32 n)
{
    uint32 i, s, x = 0;
    for (i = 0; i < n; i += s)
    {
        s = static_cast<uint32>STD_MIN(static_cast<int>(8 - bit),
            static_cast<int>(n - i));
        x <<= s;

        x |= ((next () >> ((8 - static_cast<uint32>(bit)) - s)) &
            ((1 << s) - 1));

        bit = (bit + s) % 8;
        if (!bit)
        {
            advanceNeeded = true;
        }
    }
    return x;
}

// Decode unsigned integer Exp-Golomb-coded syntax element
uint32 RbspParser::ue ()
{
    int leadingZeroBits = -1;
    for (uint32 b = 0; !b; ++leadingZeroBits)
    {
        b = u (1);
    }
    return ((1 << leadingZeroBits) - 1) +
        u (static_cast<uint32>(leadingZeroBits));
}

// Decode signed integer Exp-Golomb-coded syntax element
int32 RbspParser::se ()
{
    const uint32 x = ue ();
    if (!x) return 0;
    else if (x & 1) return static_cast<int32> ((x >> 1) + 1);
    else return - static_cast<int32> (x >> 1);
}

void H264_Utils::allocate_rbsp_buffer(uint32 inputBufferSize)
{
    m_rbspBytes = (byte *) calloc(1,inputBufferSize);
    m_prv_nalu.nal_ref_idc = 0;
    m_prv_nalu.nalu_type = NALU_TYPE_UNSPECIFIED;
}

H264_Utils::H264_Utils(): m_height(0),
                          m_width(0),
                          m_rbspBytes(NULL),
                          m_au_data (false)
{
    initialize_frame_checking_environment();
}

H264_Utils::~H264_Utils()
{
/*  if(m_pbits)
  {
    delete(m_pbits);
    m_pbits = NULL;
  }
*/
  if (m_rbspBytes)
  {
    free(m_rbspBytes);
    m_rbspBytes = NULL;
  }
}

/***********************************************************************/
/*
FUNCTION:
  H264_Utils::initialize_frame_checking_environment

DESCRIPTION:
  Extract RBSP data from a NAL

INPUT/OUTPUT PARAMETERS:
  None

RETURN VALUE:
  boolean

SIDE EFFECTS:
  None.
*/
/***********************************************************************/
void H264_Utils::initialize_frame_checking_environment()
{
  m_forceToStichNextNAL = false;
  m_au_data = false;
  m_prv_nalu.nal_ref_idc = 0;
  m_prv_nalu.nalu_type = NALU_TYPE_UNSPECIFIED;
}

/***********************************************************************/
/*
FUNCTION:
  H264_Utils::extract_rbsp

DESCRIPTION:
  Extract RBSP data from a NAL

INPUT/OUTPUT PARAMETERS:
  <In>
    buffer : buffer containing start code or nal length + NAL units
    buffer_length : the length of the NAL buffer
    start_code : If true, start code is detected,
                 otherwise size nal length is detected
    size_of_nal_length_field: size of nal length field

  <Out>
    rbsp_bistream : extracted RBSP bistream
    rbsp_length : the length of the RBSP bitstream
    nal_unit : decoded NAL header information

RETURN VALUE:
  boolean

SIDE EFFECTS:
  None.
*/
/***********************************************************************/

boolean H264_Utils::extract_rbsp(OMX_IN   OMX_U8  *buffer,
                                 OMX_IN   OMX_U32 buffer_length,
                                 OMX_IN   OMX_U32 size_of_nal_length_field,
                                 OMX_OUT  OMX_U8  *rbsp_bistream,
                                 OMX_OUT  OMX_U32 *rbsp_length,
                                 OMX_OUT  NALU    *nal_unit)
{
  byte coef1, coef2, coef3;
  uint32 pos = 0;
  uint32 nal_len = buffer_length;
  uint32 sizeofNalLengthField = 0;
  uint32 zero_count;
  boolean eRet = true;
  boolean start_code = (size_of_nal_length_field==0)?true:false;

  if(start_code) {
    // Search start_code_prefix_one_3bytes (0x000001)
    coef2 = buffer[pos++];
    coef3 = buffer[pos++];
    do {
      if(pos >= buffer_length)
      {
        DEBUG_PRINT_ERROR("ERROR: In %s() - line %d", __func__, __LINE__);
        return false;
      }

      coef1 = coef2;
      coef2 = coef3;
      coef3 = buffer[pos++];
    } while(coef1 || coef2 || coef3 != 1);
  }
  else if (size_of_nal_length_field)
  {
    /* This is the case to play multiple NAL units inside each access unit*/
    /* Extract the NAL length depending on sizeOfNALength field */
    sizeofNalLengthField = size_of_nal_length_field;
    nal_len = 0;
    while(size_of_nal_length_field--)
    {
      nal_len |= buffer[pos++]<<(size_of_nal_length_field<<3);
    }
    if (nal_len >= buffer_length)
    {
      DEBUG_PRINT_ERROR("ERROR: In %s() - line %d", __func__, __LINE__);
      return false;
    }
  }

  if (nal_len > buffer_length)
  {
    DEBUG_PRINT_ERROR("ERROR: In %s() - line %d", __func__, __LINE__);
    return false;
  }
  if(pos + 1 > (nal_len + sizeofNalLengthField))
  {
    DEBUG_PRINT_ERROR("ERROR: In %s() - line %d", __func__, __LINE__);
    return false;
  }
  if (nal_unit->forbidden_zero_bit = (buffer[pos] & 0x80))
  {
    DEBUG_PRINT_ERROR("ERROR: In %s() - line %d", __func__, __LINE__);
  }
  nal_unit->nal_ref_idc   = (buffer[pos] & 0x60) >> 5;
  nal_unit->nalu_type = buffer[pos++] & 0x1f;
  DEBUG_PRINT_LOW("\n@#@# Pos = %x NalType = %x buflen = %d",
      pos-1, nal_unit->nalu_type, buffer_length);
  *rbsp_length = 0;


  if( nal_unit->nalu_type == NALU_TYPE_EOSEQ ||
      nal_unit->nalu_type == NALU_TYPE_EOSTREAM)
    return (nal_len + sizeofNalLengthField);

  zero_count = 0;
  while (pos < (nal_len+sizeofNalLengthField))    //similar to for in p-42
   {
    if( zero_count == 2 ) {
      if( buffer[pos] == 0x03 ) {
        pos ++;
        zero_count = 0;
        continue;
      }
      if( buffer[pos] <= 0x01 ) {
        if( start_code ) {
          *rbsp_length -= 2;
          pos -= 2;
          return pos;
        }
      }
      zero_count = 0;
    }
    zero_count ++;
    if( buffer[pos] != 0 )
      zero_count = 0;

    rbsp_bistream[(*rbsp_length)++] = buffer[pos++];
  }

  return eRet;
}

/*===========================================================================
FUNCTION:
  H264_Utils::iSNewFrame

DESCRIPTION:
  Returns true if NAL parsing successfull otherwise false.

INPUT/OUTPUT PARAMETERS:
  <In>
    buffer : buffer containing start code or nal length + NAL units
    buffer_length : the length of the NAL buffer
    start_code : If true, start code is detected,
                 otherwise size nal length is detected
    size_of_nal_length_field: size of nal length field
  <out>
    isNewFrame: true if the NAL belongs to a differenet frame
                false if the NAL belongs to a current frame

RETURN VALUE:
  boolean  true, if nal parsing is successful
           false, if the nal parsing has errors

SIDE EFFECTS:
  None.
===========================================================================*/
bool H264_Utils::isNewFrame(OMX_BUFFERHEADERTYPE *p_buf_hdr,
                            OMX_IN OMX_U32 size_of_nal_length_field,
                            OMX_OUT OMX_BOOL &isNewFrame,
                            extra_data_parser *extradata_parser)
{
    NALU nal_unit;
    uint16 first_mb_in_slice = 0;
    uint32 numBytesInRBSP = 0;
    OMX_IN OMX_U8 *buffer = p_buf_hdr->pBuffer;
    OMX_IN OMX_U32 buffer_length = p_buf_hdr->nFilledLen;
    bool eRet = true;

    DEBUG_PRINT_LOW("isNewFrame: buffer %p buffer_length %d "
        "size_of_nal_length_field %d\n", buffer, buffer_length,
        size_of_nal_length_field);

    if ( false == extract_rbsp(buffer, buffer_length, size_of_nal_length_field,
                               m_rbspBytes, &numBytesInRBSP, &nal_unit) )
    {
        DEBUG_PRINT_ERROR("ERROR: In %s() - extract_rbsp() failed", __func__);
        isNewFrame = OMX_FALSE;
        eRet = false;
    }
    else
    {
#ifndef PARSE_VUI_IN_EXTRADATA
      if (nal_unit.nalu_type == NALU_TYPE_SPS)
        extradata_parser->parse_sps(buffer, buffer_length);
#endif
#ifndef PARSE_SEI_IN_EXTRADATA
      if (nal_unit.nalu_type == NALU_TYPE_SEI)
        extradata_parser->parse_sei(p_buf_hdr);
#endif
      switch (nal_unit.nalu_type)
      {
        case NALU_TYPE_IDR:
        case NALU_TYPE_NON_IDR:
        {
          DEBUG_PRINT_LOW("\n AU Boundary with NAL type %d ",nal_unit.nalu_type);
          if (m_forceToStichNextNAL)
          {
            isNewFrame = OMX_FALSE;
          }
          else
          {
            RbspParser rbsp_parser(m_rbspBytes, (m_rbspBytes+numBytesInRBSP));
            first_mb_in_slice = rbsp_parser.ue();

            if((!first_mb_in_slice) || /*(slice.prv_frame_num != slice.frame_num ) ||*/
               ( (m_prv_nalu.nal_ref_idc != nal_unit.nal_ref_idc) && ( nal_unit.nal_ref_idc * m_prv_nalu.nal_ref_idc == 0 ) ) ||
               /*( ((m_prv_nalu.nalu_type == NALU_TYPE_IDR) && (nal_unit.nalu_type == NALU_TYPE_IDR)) && (slice.idr_pic_id != slice.prv_idr_pic_id) ) || */
               ( (m_prv_nalu.nalu_type != nal_unit.nalu_type ) && ((m_prv_nalu.nalu_type == NALU_TYPE_IDR) || (nal_unit.nalu_type == NALU_TYPE_IDR)) ) )
            {
              //DEBUG_PRINT_LOW("Found a New Frame due to NALU_TYPE_IDR/NALU_TYPE_NON_IDR");
              isNewFrame = OMX_TRUE;
            }
            else
            {
              isNewFrame = OMX_FALSE;
            }
          }
          m_au_data = true;
          m_forceToStichNextNAL = false;
          break;
        }
        case NALU_TYPE_SPS:
        case NALU_TYPE_PPS:
        case NALU_TYPE_SEI:
        {
          DEBUG_PRINT_LOW("\n Non-AU boundary with NAL type %d", nal_unit.nalu_type);
          if(m_au_data)
          {
            isNewFrame = OMX_TRUE;
            m_au_data = false;
          }
          else
          {
            isNewFrame =  OMX_FALSE;
          }

          m_forceToStichNextNAL = true;
          break;
        }
        case NALU_TYPE_ACCESS_DELIM:
        case NALU_TYPE_UNSPECIFIED:
        case NALU_TYPE_EOSEQ:
        case NALU_TYPE_EOSTREAM:
        default:
        {
          isNewFrame =  OMX_FALSE;
          // Do not update m_forceToStichNextNAL
          break;
        }
      } // end of switch
    } // end of if
    m_prv_nalu = nal_unit;
    DEBUG_PRINT_LOW("get_h264_nal_type - newFrame value %d\n",isNewFrame);
    return eRet;
}

void perf_metrics::start()
{
  if (!active)
  {
    start_time = get_act_time();
    active = true;
  }
}

void perf_metrics::stop()
{
  OMX_U64 stop_time = get_act_time();
  if (active)
  {
    proc_time += (stop_time - start_time);
    active = false;
  }
}

void perf_metrics::end(OMX_U32 units_cntr)
{
  stop();
  DEBUG_PRINT_HIGH("--> Processing time : [%.2f] Sec", (float)proc_time / 1e6);
  if (units_cntr)
  {
    DEBUG_PRINT_HIGH("--> Avrg proc time  : [%.2f] mSec", proc_time / (float)(units_cntr * 1e3));
  }
}

void perf_metrics::reset()
{
  start_time = 0;
  proc_time = 0;
  active = false;
}

OMX_U64 perf_metrics::get_act_time()
{
  struct timeval act_time = {0, 0};
  gettimeofday(&act_time, NULL);
  return (act_time.tv_usec + act_time.tv_sec * 1e6);
}

OMX_U64 perf_metrics::processing_time_us()
{
  return proc_time;
}

void extra_data_parser::reset_params()
{
  curr_32_bit = 0;
  bits_read = 0;
  zero_cntr = 0;
  emulation_code_skip_cntr = 0;
  au_num = 0;
  bitstream = NULL;
  bitstream_bytes = 0;
  memset(&vui_param, 0, sizeof(vui_param));
  memset(&sei_buf_period, 0, sizeof(sei_buf_period));
  memset(&pan_scan_param, 0, sizeof(pan_scan_param));
  pan_scan_param.rect_id = NO_PAN_SCAN_BIT;
}

bool extra_data_parser::parse_extradata(OMX_BUFFERHEADERTYPE *p_buf_hdr,
  OMX_U32 decoder_fmt, OMX_U32 interlace_fmt)
{
  OMX_OTHER_EXTRADATATYPE *p_extra = NULL, *p_sei = NULL, *p_vui = NULL;
  bool ts_in_metadata = false;
  p_extra = (OMX_OTHER_EXTRADATATYPE *)
           ((unsigned)(p_buf_hdr->pBuffer + p_buf_hdr->nOffset +
            p_buf_hdr->nFilledLen + 3)&(~3));
  if (p_buf_hdr->nFlags & OMX_BUFFERFLAG_EXTRADATA)
  {
    while(p_extra &&
          (OMX_U8*)p_extra < (p_buf_hdr->pBuffer + p_buf_hdr->nAllocLen) &&
          p_extra->eType != OMX_ExtraDataNone )
    {
      DEBUG_PRINT_LOW("ExtraData : pBuf(%p) BufTS(%lld) Type(%x) DataSz(%u)",
           p_buf_hdr, p_buf_hdr->nTimeStamp, p_extra->eType, p_extra->nDataSize);
      if (decoder_fmt == VDEC_CODECTYPE_H264)
      {
        if (p_extra->eType == VDEC_OMX_SEI)
          p_sei = p_extra;
        else if (p_extra->eType == VDEC_OMX_VUI)
          p_vui = p_extra;
      }
      p_extra = (OMX_OTHER_EXTRADATATYPE *) (((OMX_U8 *) p_extra) + p_extra->nSize);
    }
#ifdef PARSE_VUI_IN_EXTRADATA
    if (p_vui && p_vui->nSize)
    {
      init_bitstream((OMX_U8*)p_vui->data, p_vui->nSize);
      parse_vui(true);
    }
#endif
#ifdef PARSE_SEI_IN_EXTRADATA
    if (p_sei && p_sei->nSize)
    {
      init_bitstream((OMX_U8*)p_sei->data, p_sei->nSize);
      ts_in_metadata = parse_sei(&p_buf_hdr->nTimeStamp);
    }
#endif
    if (!(pan_scan_param.rect_id & NO_PAN_SCAN_BIT))
    {
      append_frame_info_extradata(p_extra, interlace_fmt);
      p_extra = (OMX_OTHER_EXTRADATATYPE *) (((OMX_U8 *) p_extra) + p_extra->nSize);
    }
  }
  if (interlace_fmt != VDEC_InterlaceFrameProgressive && p_extra != NULL &&
      (OMX_U8*)p_extra < (p_buf_hdr->pBuffer + p_buf_hdr->nAllocLen))
  {
    p_buf_hdr->nFlags |= OMX_BUFFERFLAG_EXTRADATA;
    append_interlace_extradata(p_extra, interlace_fmt);
    p_extra = (OMX_OTHER_EXTRADATATYPE *) (((OMX_U8 *) p_extra) + p_extra->nSize);
  }
  if (p_buf_hdr->nFlags & OMX_BUFFERFLAG_EXTRADATA)
    append_terminator_extradata(p_extra);
  return ts_in_metadata;
}

void extra_data_parser::init_bitstream(OMX_U8* data, OMX_U32 size)
{
  bitstream = data;
  bitstream_bytes = size;
  curr_32_bit = 0;
  bits_read = 0;
  zero_cntr = 0;
  emulation_code_skip_cntr = 0;
}

void extra_data_parser::parse_vui(bool vui_in_extradata)
{
  OMX_U32 value = 0;
  DEBUG_PRINT_LOW("parse_vui: IN");
  if (vui_in_extradata)
    while (!extract_bits(1) && more_bits()); // Discard VUI enable flag
  if (!more_bits())
    return;
  if (extract_bits(1)) //aspect_ratio_info_present_flag
    if (extract_bits(8) == 0xFF) //aspect_ratio_idc
    {
      extract_bits(16); //sar_width
      extract_bits(16); //sar_width
    }
  if (extract_bits(1)) //overscan_info_present_flag
    extract_bits(1); //overscan_appropriate_flag
  if (extract_bits(1)) //video_signal_type_present_flag
  {
    extract_bits(3); //video_format
    extract_bits(1); //video_full_range_flag
    if (extract_bits(1)) //colour_description_present_flag
    {
      extract_bits(8); //colour_primaries
      extract_bits(8); //transfer_characteristics
      extract_bits(8); //matrix_coefficients
    }
  }
  if (extract_bits(1)) //chroma_location_info_present_flag
  {
    uev(); //chroma_sample_loc_type_top_field
    uev(); //chroma_sample_loc_type_bottom_field
  }
  vui_param.timing_info_present_flag = extract_bits(1);
  if (vui_param.timing_info_present_flag)
  {
    vui_param.num_units_in_tick = extract_bits(32);
    vui_param.time_scale = extract_bits(32);
    vui_param.fixed_frame_rate_flag = extract_bits(1);
    DEBUG_PRINT_LOW("Timing info present in VUI!");
    DEBUG_PRINT_LOW("  num units in tick  : %u", vui_param.num_units_in_tick);
    DEBUG_PRINT_LOW("  time scale         : %u", vui_param.time_scale);
    DEBUG_PRINT_LOW("  fixed frame rate   : %u", vui_param.fixed_frame_rate_flag);
  }
  else
  {
    DEBUG_PRINT_HIGH("NO TIMING info present in VUI!");
  }
  vui_param.nal_hrd_parameters_present_flag = extract_bits(1);
  if (vui_param.nal_hrd_parameters_present_flag)
  {
    DEBUG_PRINT_LOW("nal hrd params present!");
    hrd_parameters(&vui_param.nal_hrd_parameters);
  }
  vui_param.vcl_hrd_parameters_present_flag = extract_bits(1);
  if (vui_param.vcl_hrd_parameters_present_flag)
  {
    DEBUG_PRINT_LOW("vcl hrd params present!");
    hrd_parameters(&vui_param.vcl_hrd_parameters);
  }
  if (vui_param.nal_hrd_parameters_present_flag ||
      vui_param.vcl_hrd_parameters_present_flag)
    vui_param.low_delay_hrd_flag = extract_bits(1);
  vui_param.pic_struct_present_flag = extract_bits(1);
  DEBUG_PRINT_LOW("pic_struct_present_flag : %u", vui_param.pic_struct_present_flag);
  if (extract_bits(1)) //bitstream_restriction_flag
  {
    extract_bits(1); //motion_vectors_over_pic_boundaries_flag
    uev(); //max_bytes_per_pic_denom
    uev(); //max_bits_per_mb_denom
    uev(); //log2_max_mv_length_vertical
    uev(); //log2_max_mv_length_horizontal
    uev(); //num_reorder_frames
    uev(); //max_dec_frame_buffering
  }
  DEBUG_PRINT_LOW("parse_vui: OUT");
}

void extra_data_parser::hrd_parameters(h264_hrd_param *hrd_param)
{
  int idx;
  DEBUG_PRINT_LOW("hrd_parameters: IN");
  hrd_param->cpb_cnt = uev() + 1;
  hrd_param->bit_rate_scale = extract_bits(4);
  hrd_param->cpb_size_scale = extract_bits(4);
  DEBUG_PRINT_LOW("-->cpb_cnt        : %u", hrd_param->cpb_cnt);
  DEBUG_PRINT_LOW("-->bit_rate_scale : %u", hrd_param->bit_rate_scale);
  DEBUG_PRINT_LOW("-->cpb_size_scale : %u", hrd_param->cpb_size_scale);
  for (idx = 0; idx < hrd_param->cpb_cnt && more_bits(); idx++)
  {
    hrd_param->bit_rate_value[idx] = uev() + 1;
    hrd_param->cpb_size_value[idx] = uev() + 1;
    hrd_param->cbr_flag[idx] = extract_bits(1);
    DEBUG_PRINT_LOW("-->bit_rate_value [%d] : %u", idx, hrd_param->bit_rate_value[idx]);
    DEBUG_PRINT_LOW("-->cpb_size_value [%d] : %u", idx, hrd_param->cpb_size_value[idx]);
    DEBUG_PRINT_LOW("-->cbr_flag       [%d] : %u", idx, hrd_param->cbr_flag[idx]);
  }
  hrd_param->initial_cpb_removal_delay_length = extract_bits(5) + 1;
  hrd_param->cpb_removal_delay_length = extract_bits(5) + 1;
  hrd_param->dpb_output_delay_length = extract_bits(5) + 1;
  hrd_param->time_offset_length = extract_bits(5);
  DEBUG_PRINT_LOW("-->initial_cpb_removal_delay_length : %u", hrd_param->initial_cpb_removal_delay_length);
  DEBUG_PRINT_LOW("-->cpb_removal_delay_length         : %u", hrd_param->cpb_removal_delay_length);
  DEBUG_PRINT_LOW("-->dpb_output_delay_length          : %u", hrd_param->dpb_output_delay_length);
  DEBUG_PRINT_LOW("-->time_offset_length               : %u", hrd_param->time_offset_length);
  DEBUG_PRINT_LOW("hrd_parameters: OUT");
}

void extra_data_parser::parse_sei(OMX_BUFFERHEADERTYPE *p_buf_hdr)
{
  DEBUG_PRINT_LOW("@@parse_sei external: IN");
  init_bitstream(p_buf_hdr->pBuffer, p_buf_hdr->nFilledLen);
  parse_sei(&p_buf_hdr->nTimeStamp);
  DEBUG_PRINT_LOW("@@parse_sei external: OUT");
}

bool extra_data_parser::parse_sei(OMX_S64 *p_timestamp)
{
  OMX_U32 value = 0, processed_bytes = 0;
  OMX_U8 *sei_msg_start = bitstream;
  OMX_U32 sei_unit_size = bitstream_bytes;
  bool pic_timing = false;
  DEBUG_PRINT_LOW("@@parse_sei: IN sei_unit_size(%u)", sei_unit_size);
  value = extract_bits(24);
  processed_bytes += 3;
  while (value != 0x00000001 && processed_bytes < sei_unit_size && more_bits())
  {
    value <<= 8;
    value |= extract_bits(8);
    processed_bytes++;
  }
  if (value != 0x00000001)
  {
    DEBUG_PRINT_ERROR("parse_sei: Start code not found!");
  }
  else if (processed_bytes < sei_unit_size)
  {
    extract_bits(1); // forbidden_zero_bit
    value = extract_bits(2);
    DEBUG_PRINT_LOW("-->nal_ref_idc    : %x", value);
    value = extract_bits(5);
    DEBUG_PRINT_LOW("-->nal_unit_type  : %x", value);
    processed_bytes++;
    if (value == NALU_TYPE_SEI)
    {
      bool buf_period_processed = false, pic_timing_processed = false;
      while ((processed_bytes + 2) < sei_unit_size && more_bits())
      {
        init_bitstream(sei_msg_start + processed_bytes, sei_unit_size - processed_bytes);
        DEBUG_PRINT_LOW("-->NALU_TYPE_SEI");
        OMX_U32 payload_type = 0, payload_size = 0, aux = 0;
        do {
          value = extract_bits(8);
          payload_type += value;
          processed_bytes++;
        } while (value == 0xFF);
        DEBUG_PRINT_LOW("-->payload_type   : %u", payload_type);
        do {
          value = extract_bits(8);
          payload_size += value;
          processed_bytes++;
        } while (value == 0xFF);
        DEBUG_PRINT_LOW("-->payload_size   : %u", payload_size);
        if (payload_size > 0)
        {
          switch (payload_type)
          {
            case BUFFERING_PERIOD:
              if (!buf_period_processed)
              {
                sei_buffering_period(*p_timestamp);
                buf_period_processed = true;
              }
            break;
            case PIC_TIMING:
              if (!pic_timing_processed)
              {
                pic_timing = sei_picture_timing(*p_timestamp);
                pic_timing_processed = true;
              }
            break;
            case PAN_SCAN_RECT:
              sei_pan_scan();
            break;
            default:
              DEBUG_PRINT_LOW("-->SEI payload type [%u] not implemented! size[%u]", payload_type, payload_size);
          }
        }
        processed_bytes += (payload_size + emulation_code_skip_cntr);
        DEBUG_PRINT_LOW("-->SEI processed_bytes[%u]", processed_bytes);
      }
    }
    else
    {
      DEBUG_PRINT_ERROR("ERROR: Unexpected metadata nal unit type!");
    }
  }
  DEBUG_PRINT_LOW("@@parse_sei: OUT");
  return pic_timing;
}

void extra_data_parser::sei_buffering_period(OMX_S64 timestamp)
{
  int idx;
  OMX_U32 value = 0;
  h264_hrd_param *hrd_param = NULL;
  DEBUG_PRINT_LOW("@@sei_buffering_period: IN");
  value = uev(); // seq_parameter_set_id
  DEBUG_PRINT_LOW("-->seq_parameter_set_id : %u", value);
  if (value > 31)
  {
    DEBUG_PRINT_LOW("ERROR: Invalid seq_parameter_set_id [%u]!", value);
    return;
  }
  sei_buf_period.is_valid = false;
  if (vui_param.nal_hrd_parameters_present_flag)
  {
    hrd_param = &vui_param.nal_hrd_parameters;
    for (idx = 0; idx < hrd_param->cpb_cnt ; idx++)
    {
      sei_buf_period.is_valid = true;
      sei_buf_period.initial_cpb_removal_delay[idx] = extract_bits(hrd_param->initial_cpb_removal_delay_length);
      sei_buf_period.initial_cpb_removal_delay_offset[idx] = extract_bits(hrd_param->initial_cpb_removal_delay_length);
      DEBUG_PRINT_LOW("-->initial_cpb_removal_delay        : %u", sei_buf_period.initial_cpb_removal_delay[idx]);
      DEBUG_PRINT_LOW("-->initial_cpb_removal_delay_offset : %u", sei_buf_period.initial_cpb_removal_delay_offset[idx]);
    }
  }
  if (vui_param.vcl_hrd_parameters_present_flag)
  {
    hrd_param = &vui_param.vcl_hrd_parameters;
    for (idx = 0; idx < hrd_param->cpb_cnt ; idx++)
    {
      sei_buf_period.is_valid = true;
      sei_buf_period.initial_cpb_removal_delay[idx] = extract_bits(hrd_param->initial_cpb_removal_delay_length);
      sei_buf_period.initial_cpb_removal_delay_offset[idx] = extract_bits(hrd_param->initial_cpb_removal_delay_length);
      DEBUG_PRINT_LOW("-->initial_cpb_removal_delay        : %u", sei_buf_period.initial_cpb_removal_delay[idx]);
      DEBUG_PRINT_LOW("-->initial_cpb_removal_delay_offset : %u", sei_buf_period.initial_cpb_removal_delay_offset[idx]);
    }
  }
  if (sei_buf_period.is_valid)
  {
    sei_buf_period.au_cntr = 0;
    sei_buf_period.new_reference_ts = timestamp;
  }
  DEBUG_PRINT_LOW("@@sei_buffering_period: OUT");
}

bool extra_data_parser::sei_picture_timing(OMX_S64 &timestamp)
{
  DEBUG_PRINT_LOW("@@sei_picture_timing: IN");
  h264_sei_pic_param pic_param;
  OMX_S64 clock_ts = 0;
  OMX_U32 time_offset_len = 0, cpb_removal_len = 24, dpb_output_len  = 24;
  OMX_U8 clock_ts_flag = 0, cbr_flag = 0;
  bool calc_ts = false;
  if (vui_param.nal_hrd_parameters_present_flag)
  {
    cpb_removal_len = vui_param.nal_hrd_parameters.cpb_removal_delay_length;
    dpb_output_len = vui_param.nal_hrd_parameters.dpb_output_delay_length;
    time_offset_len = vui_param.nal_hrd_parameters.time_offset_length;
    cbr_flag = vui_param.nal_hrd_parameters.cbr_flag[0];
  }
  else if (vui_param.vcl_hrd_parameters_present_flag)
  {
    cpb_removal_len = vui_param.vcl_hrd_parameters.cpb_removal_delay_length;
    dpb_output_len = vui_param.vcl_hrd_parameters.dpb_output_delay_length;
    time_offset_len = vui_param.vcl_hrd_parameters.time_offset_length;
    cbr_flag = vui_param.vcl_hrd_parameters.cbr_flag[0];
  }
  pic_param.cpb_removal_delay = extract_bits(cpb_removal_len);
  pic_param.dpb_output_delay = extract_bits(dpb_output_len);
  DEBUG_PRINT_LOW("-->cpb_removal_delay : %u", pic_param.cpb_removal_delay);
  DEBUG_PRINT_LOW("-->dpb_output_delay  : %u", pic_param.dpb_output_delay);
  if (vui_param.pic_struct_present_flag)
  {
    pic_param.pic_struct = extract_bits(4);
    switch (pic_param.pic_struct)
    {
      case 0: case 1: case 2: pic_param.num_clock_ts = 1; break;
      case 3: case 4: case 7: pic_param.num_clock_ts = 2; break;
      case 5: case 6: case 8: pic_param.num_clock_ts = 3; break;
      default:
        DEBUG_PRINT_ERROR("sei_picture_timing: pic_struct invalid!");
    }
    DEBUG_PRINT_LOW("-->num_clock_ts      : %u", pic_param.num_clock_ts);
    for (int i = 0; i < pic_param.num_clock_ts && more_bits(); i++)
    {
      clock_ts_flag = extract_bits(1);
      if(clock_ts_flag)
      {
        DEBUG_PRINT_LOW("-->clock_timestamp present!");
        pic_param.ct_type = extract_bits(2);
        pic_param.nuit_field_based_flag = extract_bits(1);
        pic_param.counting_type = extract_bits(5);
        pic_param.full_timestamp_flag = extract_bits(1);
        pic_param.discontinuity_flag = extract_bits(1);
        pic_param.cnt_dropped_flag = extract_bits(1);
        pic_param.n_frames = extract_bits(8);
        DEBUG_PRINT_LOW("-->f_timestamp_flg   : %u", pic_param.full_timestamp_flag);
        DEBUG_PRINT_LOW("-->n_frames          : %u", pic_param.n_frames);
        pic_param.seconds_value = 0;
        pic_param.minutes_value = 0;
        pic_param.hours_value = 0;
        if (pic_param.full_timestamp_flag)
        {
          pic_param.seconds_value = extract_bits(6);
          pic_param.minutes_value = extract_bits(6);
          pic_param.hours_value = extract_bits(5);
        }
        else if (extract_bits(1))
        {
          DEBUG_PRINT_LOW("-->seconds_flag enabled!");
          pic_param.seconds_value = extract_bits(6);
          if (extract_bits(1))
          {
            DEBUG_PRINT_LOW("-->minutes_flag enabled!");
            pic_param.minutes_value = extract_bits(6);
            if (extract_bits(1))
            {
              DEBUG_PRINT_LOW("-->hours_flag enabled!");
              pic_param.hours_value = extract_bits(5);
            }
          }
        }
        else
          clock_ts_flag = 0;
        pic_param.time_offset = 0;
        if (time_offset_len > 0)
          pic_param.time_offset |= extract_bits(time_offset_len); //Update to read integer
        clock_ts = ((pic_param.hours_value * 60 + pic_param.minutes_value) * 60 + pic_param.seconds_value) * 1e6 +
                    (pic_param.n_frames * (vui_param.num_units_in_tick * (1 + pic_param.nuit_field_based_flag)) + pic_param.time_offset) *
                    1e6 / vui_param.time_scale;
        DEBUG_PRINT_LOW("-->seconds_value     : %u", pic_param.seconds_value);
        DEBUG_PRINT_LOW("-->minutes_value     : %u", pic_param.minutes_value);
        DEBUG_PRINT_LOW("-->hours_value       : %u", pic_param.hours_value);
        DEBUG_PRINT_LOW("-->time_offset       : %d", pic_param.time_offset);
        DEBUG_PRINT_LOW("-->CLOCK TIMESTAMP   : %lld", clock_ts);
      }
    }
  }
  DEBUG_PRINT_LOW("-->sei_picture_timing: Original TS[%lld]", timestamp);
  if (clock_ts_flag)
    calc_ts = true;
  else if (sei_buf_period.is_valid)
  {
    clock_ts = calculate_ts(pic_param.cpb_removal_delay);
    calc_ts = true;
  }
  if (!VALID_TS(timestamp))
    if (calc_ts)
    {
      timestamp = clock_ts;
      DEBUG_PRINT_LOW("-->sei_picture_timing: Updated TS[%lld]", timestamp);
    }
    else
    {
      DEBUG_PRINT_ERROR("NO TIMING INFO PRESENT! Cannot calculate timestamp...");
    }
  else
    calc_ts = false;
  au_num++;
  DEBUG_PRINT_LOW("@@sei_picture_timing: OUT");
  return calc_ts;
}

void extra_data_parser::sei_pan_scan()
{
  DEBUG_PRINT_LOW("@@sei_pan_scan: IN");
  pan_scan_param.rect_id = uev();
  if (pan_scan_param.rect_id > 0xFF)
  {
    DEBUG_PRINT_ERROR("sei_pan_scan: ERROR: Invalid rect_id[%u]!", pan_scan_param.rect_id);
    pan_scan_param.rect_id = NO_PAN_SCAN_BIT;
    return;
  }
  DEBUG_PRINT_LOW("-->rect_id            : %u", pan_scan_param.rect_id);
  pan_scan_param.rect_cancel_flag = extract_bits(1);
  DEBUG_PRINT_LOW("-->rect_cancel_flag   : %u", pan_scan_param.rect_cancel_flag);
  if (pan_scan_param.rect_cancel_flag)
    pan_scan_param.rect_id = NO_PAN_SCAN_BIT;
  else
  {
    pan_scan_param.cnt = uev() + 1;
    if (pan_scan_param.cnt > MAX_PAN_SCAN_RECT)
    {
      DEBUG_PRINT_ERROR("sei_pan_scan: ERROR: Invalid num of rect [%u]!", pan_scan_param.cnt);
      pan_scan_param.rect_id = NO_PAN_SCAN_BIT;
      return;
    }
    DEBUG_PRINT_LOW("-->cnt                : %u", pan_scan_param.cnt);
    for (int i = 0; i < pan_scan_param.cnt; i++)
    {
      pan_scan_param.rect_left_offset[i] = sev();
      pan_scan_param.rect_right_offset[i] = sev();
      pan_scan_param.rect_top_offset[i] = sev();
      pan_scan_param.rect_bottom_offset[i] = sev();
      DEBUG_PRINT_LOW("-->rect_left_offset   : %u", pan_scan_param.rect_left_offset);
      DEBUG_PRINT_LOW("-->rect_right_offset  : %u", pan_scan_param.rect_right_offset);
      DEBUG_PRINT_LOW("-->rect_top_offset    : %u", pan_scan_param.rect_top_offset);
      DEBUG_PRINT_LOW("-->rect_bottom_offset : %u", pan_scan_param.rect_bottom_offset);
    }
    pan_scan_param.rect_repetition_period = uev();
    DEBUG_PRINT_LOW("-->repetition_period  : %u", pan_scan_param.rect_repetition_period);
  }
  DEBUG_PRINT_HIGH("@@sei_pan_scan: OUT");
}

OMX_S64 extra_data_parser::calculate_ts(OMX_U32 cpb_removal_delay)
{
  OMX_S64 clock_ts = 0;
  DEBUG_PRINT_LOW("calculate_ts(): IN");
  if (au_num == 0)
  {
    if (!VALID_TS(sei_buf_period.new_reference_ts))
    {
      DEBUG_PRINT_ERROR("--> Invalid reference timestamp! Resetting to 0...");
      sei_buf_period.reference_ts = 0;
    }
    else
      sei_buf_period.reference_ts = sei_buf_period.new_reference_ts;
    clock_ts = sei_buf_period.reference_ts;
  }
  else
  {
    clock_ts = sei_buf_period.reference_ts + cpb_removal_delay * 1e6 * vui_param.num_units_in_tick / vui_param.time_scale;
    if (sei_buf_period.au_cntr == 0)
    {
      if (!VALID_TS(sei_buf_period.new_reference_ts))
        sei_buf_period.reference_ts = clock_ts;
      else
        clock_ts = sei_buf_period.reference_ts = sei_buf_period.new_reference_ts;
    }
  }
  sei_buf_period.au_cntr++;
  DEBUG_PRINT_LOW("calculate_ts(): OUT");
  return clock_ts;
}

void extra_data_parser::parse_sps(OMX_U8* data, OMX_U32 size)
{
  OMX_U32 value = 0, scaling_matrix_limit;
  DEBUG_PRINT_LOW("@@parse_sps: IN");
  init_bitstream(data, size);
  value = extract_bits(24);
  while (value != 0x00000001 && more_bits())
  {
    value <<= 8;
    value |= extract_bits(8);
  }
  if (value != 0x00000001)
  {
    DEBUG_PRINT_ERROR("parse_sps: Start code not found!");
    return;
  }
  else
  {
    extract_bits(1); // forbidden_zero_bit
    value = extract_bits(2);
    DEBUG_PRINT_LOW("-->nal_ref_idc    : %x", value);
    value = extract_bits(5);
    DEBUG_PRINT_LOW("-->nal_unit_type  : %x", value);
    if (value != NALU_TYPE_SPS)
     return;
  }
  value = extract_bits(8); //profile_idc
  extract_bits(8); //constraint flags and reserved bits
  extract_bits(8); //level_idc
  uev(); //sps id
  if (value == 100 || value == 110 || value == 122 || value == 244 ||
      value ==  44 || value ==  83 || value ==  86 || value == 118)
  {
    if (uev() == 3) //chroma_format_idc
    {
      extract_bits(1); //separate_colour_plane_flag
      scaling_matrix_limit = 12;
    }
    else
      scaling_matrix_limit = 12;
    uev(); //bit_depth_luma_minus8
    uev(); //bit_depth_chroma_minus8
    extract_bits(1); //qpprime_y_zero_transform_bypass_flag
    if (extract_bits(1)) //seq_scaling_matrix_present_flag
      for (int i = 0; i < scaling_matrix_limit && more_bits(); i++)
      {
        if (extract_bits(1)) ////seq_scaling_list_present_flag[ i ]
          if (i < 6)
            scaling_list(16);
          else
            scaling_list(64);
      }
  }
  uev(); //log2_max_frame_num_minus4
  value = uev(); //pic_order_cnt_type
  if (value == 0)
    uev(); //log2_max_pic_order_cnt_lsb_minus4
  else if (value == 1)
  {
    extract_bits(1); //delta_pic_order_always_zero_flag
    sev(); //offset_for_non_ref_pic
    sev(); //offset_for_top_to_bottom_field
    value = uev(); // num_ref_frames_in_pic_order_cnt_cycle
    for (int i = 0; i < value; i++)
      sev(); //offset_for_ref_frame[ i ]
  }
  uev(); //max_num_ref_frames
  extract_bits(1); //gaps_in_frame_num_value_allowed_flag
  value = uev(); //pic_width_in_mbs_minus1
  value = uev(); //pic_height_in_map_units_minus1
  if (!extract_bits(1)) //frame_mbs_only_flag
    extract_bits(1); //mb_adaptive_frame_field_flag
  extract_bits(1); //direct_8x8_inference_flag
  if (extract_bits(1)) //frame_cropping_flag
  {
    uev(); //frame_crop_left_offset
    uev(); //frame_crop_right_offset
    uev(); //frame_crop_top_offset
    uev(); //frame_crop_bottom_offset
  }
  if (extract_bits(1)) //vui_parameters_present_flag
    parse_vui(false);
  DEBUG_PRINT_LOW("@@parse_sps: OUT");
}

void extra_data_parser::scaling_list(OMX_U32 size_of_scaling_list)
{
  OMX_S32 last_scale = 8, next_scale = 8, delta_scale;
  for (int j = 0; j < size_of_scaling_list; j++)
  {
    if (next_scale != 0)
    {
      delta_scale = sev();
      next_scale = (last_scale + delta_scale + 256) % 256;
    }
    last_scale = (next_scale == 0)? last_scale : next_scale;
  }
}

OMX_U32 extra_data_parser::extract_bits(OMX_U32 n)
{
  OMX_U32 value = 0;
  if (n > 32)
  {
    DEBUG_PRINT_ERROR("ERROR: extract_bits limit to 32 bits!");
    return value;
  }
  value = curr_32_bit >> (32 - n);
  if (bits_read < n)
  {
    n -= bits_read;
    read_word();
    value |= (curr_32_bit >> (32 - n));
    if (bits_read < n)
    {
      DEBUG_PRINT_ERROR("ERROR: extract_bits underflow!");
      value >>= (n - bits_read);
      n = bits_read;
    }
  }
  bits_read -= n;
  curr_32_bit <<= n;
  return value;
}

void extra_data_parser::read_word()
{
  curr_32_bit = 0;
  bits_read = 0;
  while (bitstream_bytes && bits_read < 32)
  {
    if (*bitstream == EMULATION_PREVENTION_THREE_BYTE &&
        zero_cntr >= 2)
    {
      DEBUG_PRINT_LOW("EMULATION_PREVENTION_THREE_BYTE: Skip 0x03 byte aligned!");
      emulation_code_skip_cntr++;
    }
    else
    {
      curr_32_bit <<= 8;
      curr_32_bit |= *bitstream;
      bits_read += 8;
    }
    if (*bitstream == 0)
      zero_cntr++;
    else
      zero_cntr = 0;
    bitstream++;
    bitstream_bytes--;
  }
  curr_32_bit <<= (32 - bits_read);
}

OMX_U32 extra_data_parser::uev()
{
  OMX_U32 lead_zero_bits = 0, code_num = 0;
  while(!extract_bits(1) && more_bits())
    lead_zero_bits++;
  code_num = lead_zero_bits == 0 ? 0 :
    (1 << lead_zero_bits) - 1 + extract_bits(lead_zero_bits);
  return code_num;
}

bool extra_data_parser::more_bits()
{
	return (bitstream_bytes > 0 || bits_read > 0);
}

OMX_S32 extra_data_parser::sev()
{
  OMX_U32 code_num = uev();
  OMX_S32 ret;
  ret = (int)((code_num + 1) >> 1);
  return (code_num & 1) ? ret : -ret;
}

void extra_data_parser::append_interlace_extradata(OMX_OTHER_EXTRADATATYPE *extra, OMX_U32 interlace_fmt)
{
  OMX_STREAMINTERLACEFORMAT *interlace_format;
  extra->nSize = (sizeof(OMX_OTHER_EXTRADATATYPE) +
                  sizeof(OMX_STREAMINTERLACEFORMAT) + 3)&(~3);
  extra->nVersion.nVersion = OMX_SPEC_VERSION;
  extra->nPortIndex = OMX_CORE_OUTPUT_PORT_INDEX;
  extra->eType = (OMX_EXTRADATATYPE)OMX_ExtraDataInterlaceFormat;
  extra->nDataSize = sizeof(OMX_STREAMINTERLACEFORMAT);
  interlace_format = (OMX_STREAMINTERLACEFORMAT *)extra->data;
  interlace_format->nSize = sizeof(OMX_STREAMINTERLACEFORMAT);
  interlace_format->nVersion.nVersion = OMX_SPEC_VERSION;
  interlace_format->nPortIndex = OMX_CORE_OUTPUT_PORT_INDEX;
  interlace_format->bInterlaceFormat = OMX_TRUE;
  if (interlace_fmt == VDEC_InterlaceInterleaveFrameTopFieldFirst)
    interlace_format->nInterlaceFormats = OMX_InterlaceInterleaveFrameTopFieldFirst;
  else
    interlace_format->nInterlaceFormats = OMX_InterlaceInterleaveFrameBottomFieldFirst;
}

void extra_data_parser::append_frame_info_extradata(OMX_OTHER_EXTRADATATYPE *extra, OMX_U32 interlace_fmt)
{
  OMX_QCOM_EXTRADATA_FRAMEINFO *frame_info = NULL;
  extra->nSize = (sizeof(OMX_OTHER_EXTRADATATYPE) +
                  sizeof(OMX_QCOM_EXTRADATA_FRAMEINFO) + 3)&(~3);
  extra->nVersion.nVersion = OMX_SPEC_VERSION;
  extra->nPortIndex = OMX_CORE_OUTPUT_PORT_INDEX;
  extra->eType = (OMX_EXTRADATATYPE)OMX_ExtraDataFrameInfo;
  extra->nDataSize = sizeof(OMX_QCOM_EXTRADATA_FRAMEINFO);
  frame_info = (OMX_QCOM_EXTRADATA_FRAMEINFO *)extra->data;

  frame_info->panScan.numWindows = pan_scan_param.cnt;

  frame_info->ePicType = (OMX_VIDEO_PICTURETYPE)0; // DATA NOT PARSED

  // Interlace info is propagated with a another extra data type when it's not progressive
  if (interlace_fmt == VDEC_InterlaceInterleaveFrameTopFieldFirst)
    frame_info->interlaceType = OMX_QCOM_InterlaceInterleaveFrameTopFieldFirst;
  else if (interlace_fmt == VDEC_InterlaceInterleaveFrameBottomFieldFirst)
    frame_info->interlaceType = OMX_QCOM_InterlaceInterleaveFrameBottomFieldFirst;
  else
    frame_info->interlaceType = OMX_QCOM_InterlaceFrameProgressive;

  for (int i = 0; i < frame_info->panScan.numWindows; i++)
  {
    frame_info->panScan.window[i].x = pan_scan_param.rect_left_offset[i];
    frame_info->panScan.window[i].y = pan_scan_param.rect_top_offset[i];
    frame_info->panScan.window[i].dx = pan_scan_param.rect_right_offset[i];
    frame_info->panScan.window[i].dy = pan_scan_param.rect_bottom_offset[i];
  }

  frame_info->nConcealedMacroblocks = 0;  // DATA NOT PARSED?
}

void extra_data_parser::append_terminator_extradata(OMX_OTHER_EXTRADATATYPE *extra)
{
  extra->nSize = sizeof(OMX_OTHER_EXTRADATATYPE);
  extra->nVersion.nVersion = OMX_SPEC_VERSION;
  extra->eType = OMX_ExtraDataNone;
  extra->nDataSize = 0;
  extra->data[0] = 0;
}
