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

  DEBUG_PRINT_LOW("extract_rbsp\n");

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
  DEBUG_PRINT_LOW("\n@#@# Pos = %x NalType = %x buflen = %d",pos-1,nal_unit->nalu_type,buffer_length);
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
bool H264_Utils::isNewFrame(OMX_IN OMX_U8 *buffer,
                            OMX_IN OMX_U32 buffer_length,
                            OMX_IN OMX_U32 size_of_nal_length_field,
                            OMX_OUT OMX_BOOL &isNewFrame)
{
    NALU nal_unit;
    uint16 first_mb_in_slice = 0;
    uint32 numBytesInRBSP = 0;
    bool eRet = true;

    DEBUG_PRINT_LOW("get_h264_nal_type %p nal_length %d nal_length_field %d\n",
                 buffer, buffer_length, size_of_nal_length_field);

    if ( false == extract_rbsp(buffer, buffer_length, size_of_nal_length_field,
                               m_rbspBytes, &numBytesInRBSP, &nal_unit) )
    {
        DEBUG_PRINT_ERROR("ERROR: In %s() - extract_rbsp() failed", __func__);
        isNewFrame = OMX_FALSE;
        eRet = false;
    }
    else
    {
      switch (nal_unit.nalu_type)
      {
        case NALU_TYPE_IDR:
        case NALU_TYPE_NON_IDR:
        {
          DEBUG_PRINT_LOW("\n Found a AU Boundary %d ",nal_unit.nalu_type);
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
        case NALU_TYPE_UNSPECIFIED:
        case NALU_TYPE_EOSEQ:
        case NALU_TYPE_EOSTREAM:
        {
          DEBUG_PRINT_LOW("\n Non AU boundary NAL %d",nal_unit.nalu_type);
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
