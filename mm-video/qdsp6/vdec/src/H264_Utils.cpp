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
/*========================================================================
                      O p e n M M
         V i d e o   U t i l i t i e s

*//** @file H264_Utils.cpp
  This module contains utilities and helper routines.

@par EXTERNALIZED FUNCTIONS

@par INITIALIZATION AND SEQUENCING REQUIREMENTS
  (none)

*//*====================================================================== */


/* =======================================================================

                     INCLUDE FILES FOR MODULE

========================================================================== */
#include "H264_Utils.h"
#include "omx_vdec.h"
#include <string.h>
#include <stdlib.h>

#ifdef _ANDROID_
#include "cutils/properties.h"
#endif

#include "qtv_msg.h"

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

RbspParser::RbspParser(const uint8 * _begin, const uint8 * _end)
:begin(_begin), end(_end), pos(-1), bit(0),
cursor(0xFFFFFF), advanceNeeded(true)
{
}

// Destructor
/*lint -e{1540}  Pointer member neither freed nor zeroed by destructor
 * No problem
 */
RbspParser::~RbspParser()
{
}

// Return next RBSP byte as a word
uint32 RbspParser::next()
{
   if (advanceNeeded)
      advance();
   //return static_cast<uint32> (*pos);
   return static_cast < uint32 > (begin[pos]);
}

// Advance RBSP decoder to next byte
void RbspParser::advance()
{
   ++pos;
   //if (pos >= stop)
   if (begin + pos == end) {
      /*lint -e{730}  Boolean argument to function
       * I don't see a problem here
       */
      //throw false;
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
              "H264Parser-->NEED TO THROW THE EXCEPTION...\n");
   }
   cursor <<= 8;
   //cursor |= static_cast<uint32> (*pos);
   cursor |= static_cast < uint32 > (begin[pos]);
   if ((cursor & 0xFFFFFF) == 0x000003) {
      advance();
   }
   advanceNeeded = false;
}

// Decode unsigned integer
uint32 RbspParser::u(uint32 n)
{
   uint32 i, s, x = 0;
   for (i = 0; i < n; i += s) {
      s = static_cast < uint32 > STD_MIN(static_cast < int >(8 - bit),
                     static_cast < int >(n - i));
      x <<= s;

      x |= ((next() >> ((8 - static_cast < uint32 > (bit)) - s)) &
            ((1 << s) - 1));

      bit = (bit + s) % 8;
      if (!bit) {
         advanceNeeded = true;
      }
   }
   return x;
}

// Decode unsigned integer Exp-Golomb-coded syntax element
uint32 RbspParser::ue()
{
   int leadingZeroBits = -1;
   for (uint32 b = 0; !b; ++leadingZeroBits) {
      b = u(1);
   }
   return ((1 << leadingZeroBits) - 1) +
       u(static_cast < uint32 > (leadingZeroBits));
}

// Decode signed integer Exp-Golomb-coded syntax element
int32 RbspParser::se()
{
   const uint32 x = ue();
   if (!x)
      return 0;
   else if (x & 1)
      return static_cast < int32 > ((x >> 1) + 1);
   else
      return -static_cast < int32 > (x >> 1);
}

void H264_Utils::allocate_rbsp_buffer(uint32 inputBufferSize)
{
   m_rbspBytes = (byte *) malloc(inputBufferSize);
   m_prv_nalu.nal_ref_idc = 0;
   m_prv_nalu.nalu_type = NALU_TYPE_UNSPECIFIED;
}

H264_Utils::H264_Utils():m_height(0), m_width(0), m_rbspBytes(NULL),
    m_default_profile_chk(true), m_default_level_chk(true)
{
#ifdef _ANDROID_
   char property_value[PROPERTY_VALUE_MAX] = {0};
   if(0 != property_get("persist.omxvideo.profilecheck", property_value, NULL))
   {
       if(!strcmp(property_value, "false"))
       {
           m_default_profile_chk = false;
       }
   }
   else
   {
       QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED, "H264_Utils:: Constr failed in \
           getting value for the Android property [persist.omxvideo.profilecheck]");
   }

   if(0 != property_get("persist.omxvideo.levelcheck", property_value, NULL))
   {
       if(!strcmp(property_value, "false"))
       {
           m_default_level_chk = false;
       }
   }
   else
   {
       QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED, "H264_Utils:: Constr failed in \
           getting value for the Android property [persist.omxvideo.levelcheck]");
   }
#endif
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
   if (m_rbspBytes) {
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

boolean H264_Utils::extract_rbsp(OMX_IN OMX_U8 * buffer,
             OMX_IN OMX_U32 buffer_length,
             OMX_IN OMX_U32 size_of_nal_length_field,
             OMX_OUT OMX_U8 * rbsp_bistream,
             OMX_OUT OMX_U32 * rbsp_length,
             OMX_OUT NALU * nal_unit)
{
   byte coef1, coef2, coef3;
   uint32 pos = 0;
   uint32 nal_len = buffer_length;
   uint32 sizeofNalLengthField = 0;
   uint32 zero_count;
   boolean eRet = true;
   boolean start_code = (size_of_nal_length_field == 0) ? true : false;

   QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED, "extract_rbsp\n");

   if (start_code) {
      // Search start_code_prefix_one_3bytes (0x000001)
      coef2 = buffer[pos++];
      coef3 = buffer[pos++];
      do {
         if (pos >= buffer_length) {
            QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                     QTVDIAG_PRIO_HIGH,
                     "Error at extract rbsp line %d",
                     __LINE__);
            return false;
         }

         coef1 = coef2;
         coef2 = coef3;
         coef3 = buffer[pos++];
      } while (coef1 || coef2 || coef3 != 1);
   } else if (size_of_nal_length_field) {
      /* This is the case to play multiple NAL units inside each access unit */
      /* Extract the NAL length depending on sizeOfNALength field */
      sizeofNalLengthField = size_of_nal_length_field;
      nal_len = 0;
      while (size_of_nal_length_field--) {
         nal_len |=
             buffer[pos++] << (size_of_nal_length_field << 3);
      }
      if (nal_len >= buffer_length) {
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                  "Error at extract rbsp line %d",
                  __LINE__);
         return false;
      }
   }

   if (nal_len > buffer_length) {
      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
               "Error at extract rbsp line %d", __LINE__);
      return false;
   }
   if (pos + 1 > (nal_len + sizeofNalLengthField)) {
      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
               "Error at extract rbsp line %d", __LINE__);
      return false;
   }
   if (nal_unit->forbidden_zero_bit = (buffer[pos] & 0x80)) {
      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
               "Error at extract rbsp line %d", __LINE__);
   }
   nal_unit->nal_ref_idc = (buffer[pos] & 0x60) >> 5;
   nal_unit->nalu_type = buffer[pos++] & 0x1f;
   *rbsp_length = 0;

   if (nal_unit->nalu_type == NALU_TYPE_EOSEQ ||
       nal_unit->nalu_type == NALU_TYPE_EOSTREAM)
      return eRet;

   zero_count = 0;
   while (pos < (nal_len + sizeofNalLengthField))   //similar to for in p-42
   {
      if (zero_count == 2) {
         if (buffer[pos] == 0x03) {
            pos++;
            zero_count = 0;
            continue;
         }
         if (buffer[pos] <= 0x01) {
            if (start_code) {
               *rbsp_length -= 2;
               pos -= 2;
               break;
            }
         }
         zero_count = 0;
      }
      zero_count++;
      if (buffer[pos] != 0)
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
bool H264_Utils::isNewFrame(OMX_IN OMX_U8 * buffer,
             OMX_IN OMX_U32 buffer_length,
             OMX_IN OMX_U32 size_of_nal_length_field,
             OMX_OUT OMX_BOOL & isNewFrame,
             bool & isUpdateTimestamp)
{
   NALU nal_unit;
   uint16 first_mb_in_slice = 0;
   uint32 numBytesInRBSP = 0;
   bool eRet = true;
   isUpdateTimestamp = false;

   QTV_MSG_PRIO3(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
            "get_h264_nal_type %p nal_length %d nal_length_field %d\n",
            buffer, buffer_length, size_of_nal_length_field);

   if (false ==
       extract_rbsp(buffer, buffer_length, size_of_nal_length_field,
          m_rbspBytes, &numBytesInRBSP, &nal_unit)) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "get_h264_nal_type - ERROR at extract_rbsp\n");
      isNewFrame = OMX_FALSE;
      eRet = false;
   } else {

      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
      "Nalu type: %d",nal_unit.nalu_type);

      switch (nal_unit.nalu_type) {
      case NALU_TYPE_IDR:
      case NALU_TYPE_NON_IDR:
         {
               RbspParser rbsp_parser(m_rbspBytes,
                            (m_rbspBytes +
                        numBytesInRBSP));
               first_mb_in_slice = rbsp_parser.ue();

            if (m_forceToStichNextNAL) {
               isNewFrame = OMX_FALSE;
               if(!first_mb_in_slice){
                  isUpdateTimestamp = true;
               }
            } else {
               if ((!first_mb_in_slice) ||   /*(slice.prv_frame_num != slice.frame_num ) || */
                   ((m_prv_nalu.nal_ref_idc !=
                     nal_unit.nal_ref_idc)
                    && (nal_unit.nal_ref_idc *
                   m_prv_nalu.nal_ref_idc == 0))
                   ||
                   /*( ((m_prv_nalu.nalu_type == NALU_TYPE_IDR) && (nal_unit.nalu_type == NALU_TYPE_IDR)) && (slice.idr_pic_id != slice.prv_idr_pic_id) ) || */
                   ((m_prv_nalu.nalu_type !=
                     nal_unit.nalu_type)
                    &&
                    ((m_prv_nalu.nalu_type ==
                      NALU_TYPE_IDR)
                     || (nal_unit.nalu_type ==
                    NALU_TYPE_IDR)))) {
                  isNewFrame = OMX_TRUE;
               } else {
                  isNewFrame = OMX_FALSE;
               }
            }
            m_forceToStichNextNAL = false;
            break;
         }
      default:
         {
            isNewFrame =
                (m_forceToStichNextNAL ? OMX_FALSE :
                 OMX_TRUE);
            m_forceToStichNextNAL = true;
            break;
         }
      }      // end of switch
   }         // end of if
   m_prv_nalu = nal_unit;
   QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
            "get_h264_nal_type - newFrame value %d\n", isNewFrame);
   return eRet;
}

/**************************************************************************
 ** This function parses an H.264 Annex B formatted bitstream, returning the
 ** next frame in the format required by MP4 (specified in ISO/IEC 14496-15,
 ** section 5.2.3, "AVC Sample Structure Definition"), and recovering any
 ** header (sequence and picture parameter set NALUs) information, formatting
 ** it as a header block suitable for writing to video format services.
 **
 ** IN const uint8 *encodedBytes
 **     This points to the H.264 Annex B formatted video bitstream, starting
 **     with the next frame for which to locate frame boundaries.
 **
 ** IN uint32 totalBytes
 **     This is the total number of bytes left in the H.264 video bitstream,
 **     from the given starting position.
 **
 ** INOUT H264StreamInfo &streamInfo
 **     This structure contains state information about the stream as it has
 **     been so far parsed.
 **
 ** OUT vector<uint8> &frame
 **     The properly MP4 formatted H.264 frame will be stored here.
 **
 ** OUT uint32 &bytesConsumed
 **     This is set to the total number of bytes parsed from the bitstream.
 **
 ** OUT uint32 &nalSize
 **     The true size of the NAL (without padding zeroes)
 **
 ** OUT bool &keyFrame
 **     Indicator whether this is an I-frame
 **
 ** IN bool stripSeiAud
 **     If set, any SEI or AU delimiter NALUs are stripped out.
 *************************************************************************/
bool H264_Utils::parseHeader(uint8 * encodedBytes,
              uint32 totalBytes,
              uint32 sizeOfNALLengthField,
              unsigned &height,
              unsigned &width,
              bool & bInterlace,
              unsigned &cropx,
              unsigned &cropy,
              unsigned &cropdx, unsigned &cropdy)
{
   bool keyFrame = FALSE;
   bool stripSeiAud = FALSE;
   bool nalSize = FALSE;
   uint64 bytesConsumed = 0;
   uint8 frame[64];
   struct H264ParamNalu temp = { 0 };

   // Scan NALUs until a frame boundary is detected.  If this is the first
   // frame, scan a second time to find the end of the frame.  Otherwise, the
   // first boundary found is the end of the current frame.  While scanning,
   // collect any sequence/parameter set NALUs for use in constructing the
   // stream header.
   bool inFrame = true;
   bool inNalu = false;
   bool vclNaluFound = false;
   uint8 naluType = 0;
   uint32 naluStart = 0, naluSize = 0;
   uint32 prevVclFrameNum = 0, vclFrameNum = 0;
   bool prevVclFieldPicFlag = false, vclFieldPicFlag = false;
   bool prevVclBottomFieldFlag = false, vclBottomFieldFlag = false;
   uint8 prevVclNalRefIdc = 0, vclNalRefIdc = 0;
   uint32 prevVclPicOrderCntLsb = 0, vclPicOrderCntLsb = 0;
   int32 prevVclDeltaPicOrderCntBottom = 0, vclDeltaPicOrderCntBottom = 0;
   int32 prevVclDeltaPicOrderCnt0 = 0, vclDeltaPicOrderCnt0 = 0;
   int32 prevVclDeltaPicOrderCnt1 = 0, vclDeltaPicOrderCnt1 = 0;
   uint8 vclNaluType = 0;
   uint32 vclPicOrderCntType = 0;
   uint64 pos;
   uint64 posNalDetected = 0xFFFFFFFF;
   uint32 cursor = 0xFFFFFFFF;
   unsigned int profile_id = 0, level_id = 0;

   H264ParamNalu *seq = NULL, *pic = NULL;
   // used to  determin possible infinite loop condition
   int loopCnt = 0;
   for (pos = 0;; ++pos) {
      // return early, found possible infinite loop
      if (loopCnt > 100000)
         return 0;
      // Scan ahead next byte.
      cursor <<= 8;
      cursor |= static_cast < uint32 > (encodedBytes[pos]);

      if (sizeOfNALLengthField != 0) {
         inNalu = true;
         naluStart = sizeOfNALLengthField;
      }
      // If in NALU, scan forward until an end of NALU condition is
      // detected.
      if (inNalu) {
         if (sizeOfNALLengthField == 0) {
            // Detect end of NALU condition.
            if (((cursor & 0xFFFFFF) == 0x000000)
                || ((cursor & 0xFFFFFF) == 0x000001)
                || (pos >= totalBytes)) {
               inNalu = false;
               if (pos < totalBytes) {
                  pos -= 3;
               }
               naluSize =
                   static_cast < uint32 >
                   ((static_cast < uint32 >
                     (pos) - naluStart) + 1);
               QTV_MSG_PRIO3(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_MED,
                        "H264Parser-->1.nalusize=%x pos=%x naluStart=%x\n",
                        naluSize, pos, naluStart);
            } else {
               ++loopCnt;
               continue;
            }
         }
         // Determine NALU type.
         naluType = (encodedBytes[naluStart] & 0x1F);
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "H264Parser-->2.naluType=%x....\n",
                  naluType);
         if (naluType == 5)
            keyFrame = true;

         // For NALUs in the frame having a slice header, parse additional
         // fields.
         bool isVclNalu = false;
         if ((naluType == 1) || (naluType == 2)
             || (naluType == 5)) {
            QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                     "H264Parser-->3.naluType=%x....\n",
                     naluType);
            // Parse additional fields.
            RbspParser rbsp(&encodedBytes[naluStart + 1],
                  &encodedBytes[naluStart +
                           naluSize]);
            vclNaluType = naluType;
            vclNalRefIdc =
                ((encodedBytes[naluStart] >> 5) & 0x03);
            (void)rbsp.ue();
            (void)rbsp.ue();
            const uint32 picSetID = rbsp.ue();
            pic = this->pic.find(picSetID);
            QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                     "H264Parser-->4.sizeof %x %x\n",
                     this->pic.size(),
                     this->seq.size());
            if (!pic) {
               if (this->pic.empty()) {
                  // Found VCL NALU before needed picture parameter set
                  // -- assume that we started parsing mid-frame, and
                  // discard the rest of the frame we're in.
                  inFrame = false;
                  //frame.clear ();
                  QTV_MSG_PRIO(QTVDIAG_GENERAL,
                          QTVDIAG_PRIO_MED,
                          "H264Parser-->5.pic empty........\n");
               } else {
                  QTV_MSG_PRIO(QTVDIAG_GENERAL,
                          QTVDIAG_PRIO_MED,
                          "H264Parser-->6.FAILURE to parse..break frm here");
                  break;
               }
            }
            if (pic) {
               QTV_MSG_PRIO2(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_MED,
                        "H264Parser-->7.sizeof %x %x\n",
                        this->pic.size(),
                        this->seq.size());
               seq = this->seq.find(pic->seqSetID);
               if (!seq) {
                  if (this->seq.empty()) {
                     QTV_MSG_PRIO
                         (QTVDIAG_GENERAL,
                          QTVDIAG_PRIO_MED,
                          "H264Parser-->8.seq empty........\n");
                     // Found VCL NALU before needed sequence parameter
                     // set -- assume that we started parsing
                     // mid-frame, and discard the rest of the frame
                     // we're in.
                     inFrame = false;
                     //frame.clear ();
                  } else {
                     QTV_MSG_PRIO
                         (QTVDIAG_GENERAL,
                          QTVDIAG_PRIO_MED,
                          "H264Parser-->9.FAILURE to parse...break");
                     break;
                  }
               }
            }
            QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                     "H264Parser-->10.sizeof %x %x\n",
                     this->pic.size(),
                     this->seq.size());
            if (pic && seq) {
               QTV_MSG_PRIO2(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_MED,
                        "H264Parser-->11.pic and seq[%x][%x]........\n",
                        pic, seq);
               isVclNalu = true;
               vclFrameNum =
                   rbsp.u(seq->log2MaxFrameNumMinus4 +
                     4);
               if (!seq->frameMbsOnlyFlag) {
                  vclFieldPicFlag =
                      (rbsp.u(1) == 1);
                  if (vclFieldPicFlag) {
                     vclBottomFieldFlag =
                         (rbsp.u(1) == 1);
                  }
               } else {
                  vclFieldPicFlag = false;
                  vclBottomFieldFlag = false;
               }
               if (vclNaluType == 5) {
                  (void)rbsp.ue();
               }
               vclPicOrderCntType =
                   seq->picOrderCntType;
               if (seq->picOrderCntType == 0) {
                  vclPicOrderCntLsb = rbsp.u
                      (seq->
                       log2MaxPicOrderCntLsbMinus4
                       + 4);
                  if (pic->picOrderPresentFlag
                      && !vclFieldPicFlag) {
                     vclDeltaPicOrderCntBottom
                         = rbsp.se();
                  } else {
                     vclDeltaPicOrderCntBottom
                         = 0;
                  }
               } else {
                  vclPicOrderCntLsb = 0;
                  vclDeltaPicOrderCntBottom = 0;
               }
               if ((seq->picOrderCntType == 1)
                   && !seq->
                   deltaPicOrderAlwaysZeroFlag) {
                  vclDeltaPicOrderCnt0 =
                      rbsp.se();
                  if (pic->picOrderPresentFlag
                      && !vclFieldPicFlag) {
                     vclDeltaPicOrderCnt1 =
                         rbsp.se();
                  } else {
                     vclDeltaPicOrderCnt1 =
                         0;
                  }
               } else {
                  vclDeltaPicOrderCnt0 = 0;
                  vclDeltaPicOrderCnt1 = 0;
               }
            }
         }
         //////////////////////////////////////////////////////////////////
         // Perform frame boundary detection.
         //////////////////////////////////////////////////////////////////

         // The end of the bitstream is always a boundary.
         bool boundary = (pos >= totalBytes);

         // The first of these NALU types always mark a boundary, but skip
         // any that occur before the first VCL NALU in a new frame.
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "H264Parser-->12.naluType[%x].....\n",
                  naluType);
         if ((((naluType >= 6) && (naluType <= 9))
              || ((naluType >= 13) && (naluType <= 18)))
             && (vclNaluFound || !inFrame)) {
            boundary = true;
         }
         // If a VCL NALU is found, compare with the last VCL NALU to
         // determine if they belong to different frames.
         else if (vclNaluFound && isVclNalu) {
            // Clause 7.4.1.2.4 -- detect first VCL NALU through
            // parsing of portions of the NALU header and slice
            // header.
            /*lint -e{731} Boolean argument to equal/not equal
             * It's ok
             */
            if ((prevVclFrameNum != vclFrameNum)
                || (prevVclFieldPicFlag != vclFieldPicFlag)
                || (prevVclBottomFieldFlag !=
               vclBottomFieldFlag)
                || ((prevVclNalRefIdc != vclNalRefIdc)
               && ((prevVclNalRefIdc == 0)
                   || (vclNalRefIdc == 0)))
                || ((vclPicOrderCntType == 0)
               &&
               ((prevVclPicOrderCntLsb !=
                 vclPicOrderCntLsb)
                || (prevVclDeltaPicOrderCntBottom !=
                    vclDeltaPicOrderCntBottom)))
                || ((vclPicOrderCntType == 1)
               && ((prevVclDeltaPicOrderCnt0
                    != vclDeltaPicOrderCnt0)
                   || (prevVclDeltaPicOrderCnt1
                  != vclDeltaPicOrderCnt1)))) {
               boundary = true;
            }
         }
         // If a frame boundary is reached and we were in the frame in
         // which at least one VCL NALU was found, we are done processing
         // this frame.  Remember to back up to NALU start code to make
         // sure it is available for when the next frame is parsed.
         if (boundary && inFrame && vclNaluFound) {
            pos = static_cast < uint64 > (naluStart - 3);
            QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                    "H264Parser-->13.Break  \n");
            break;
         }
         inFrame = (inFrame || boundary);

         // Process sequence and parameter set NALUs specially.
         if ((naluType == 7) || (naluType == 8)) {
            QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                     "H264Parser-->14.naluType[%x].....\n",
                     naluType);
            QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                     "H264Parser-->15.sizeof %x %x\n",
                     this->pic.size(),
                     this->seq.size());
            H264ParamNaluSet & naluSet =
                ((naluType == 7) ? this->seq : this->pic);

            // Parse parameter set ID and other stream information.
            H264ParamNalu newParam;
            RbspParser rbsp(&encodedBytes[naluStart + 1],
                  &encodedBytes[naluStart +
                           naluSize]);
            uint32 id;
            if (naluType == 7) {

               unsigned int tmp;
               QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_MED,
                        "H264Parser-->16.naluType[%x].....\n",
                        naluType);
               profile_id = rbsp.u(8);
               QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_MED,
                        "H264Parser-->33.prfoile[%d].....\n",
                        profile_id);

               tmp = rbsp.u(8);
               QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_MED,
                        "H264Parser-->33.prfoilebytes[%x].....\n",
                        tmp);

               level_id = rbsp.u(8);
               QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_MED,
                        "H264Parser-->33.level[%d].....\n",
                        level_id);

               id = newParam.seqSetID = rbsp.ue();
               QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_MED,
                        "H264Parser-->33.seqID[%d].....\n",
                        id);
               if (profile_id == 100) {
                  //Chroma_format_idc
                  tmp = rbsp.ue();
                  if (tmp == 3) {
                     //residual_colour_transform_flag
                     (void)rbsp.u(1);
                  }
                  //bit_depth_luma_minus8
                  (void)rbsp.ue();
                  //bit_depth_chroma_minus8
                  (void)rbsp.ue();
                  //qpprime_y_zero_transform_bypass_flag
                  (void)rbsp.u(1);
                  // seq_scaling_matrix_present_flag
                  tmp = rbsp.u(1);
                  if (tmp) {
                     unsigned int tmp1, t;
                     //seq_scaling_list_present_flag
                     for (t = 0; t < 6; t++) {
                        tmp1 =
                            rbsp.u(1);
                        if (tmp1) {
                           unsigned
                               int
                               last_scale
                               =
                               8,
                               next_scale
                               =
                               8,
                               delta_scale;
                           for (int
                                j =
                                0;
                                j <
                                16;
                                j++)
                           {
                              if (next_scale) {
                                 delta_scale
                                     =
                                     rbsp.
                                     se
                                     ();
                                 next_scale
                                     =
                                     (last_scale
                                      +
                                      delta_scale
                                      +
                                      256)
                                     %
                                     256;
                              }
                              last_scale
                                  =
                                  next_scale
                                  ?
                                  next_scale
                                  :
                                  last_scale;
                           }
                        }
                     }
                     for (t = 0; t < 2; t++) {
                        tmp1 =
                            rbsp.u(1);
                        if (tmp1) {
                           unsigned
                               int
                               last_scale
                               =
                               8,
                               next_scale
                               =
                               8,
                               delta_scale;
                           for (int
                                j =
                                0;
                                j <
                                64;
                                j++)
                           {
                              if (next_scale) {
                                 delta_scale
                                     =
                                     rbsp.
                                     se
                                     ();
                                 next_scale
                                     =
                                     (last_scale
                                      +
                                      delta_scale
                                      +
                                      256)
                                     %
                                     256;
                              }
                              last_scale
                                  =
                                  next_scale
                                  ?
                                  next_scale
                                  :
                                  last_scale;
                           }
                        }
                     }
                  }

               }
               newParam.log2MaxFrameNumMinus4 =
                   rbsp.ue();
               QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_MED,
                        "H264Parser-->33.log2MaxFrameNumMinu[%d].....\n",
                        newParam.
                        log2MaxFrameNumMinus4);
               newParam.picOrderCntType = rbsp.ue();
               QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_MED,
                        "H264Parser-->33.picOrderCntType[%d].....\n",
                        newParam.picOrderCntType);
               if (newParam.picOrderCntType == 0) {
                  newParam.
                      log2MaxPicOrderCntLsbMinus4
                      = rbsp.ue();
                  QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                           QTVDIAG_PRIO_MED,
                           "H264Parser-->33.log2MaxPicOrderCntLsbMinus4 [%d].....\n",
                           newParam.
                           log2MaxPicOrderCntLsbMinus4);
               } else if (newParam.picOrderCntType ==
                     1) {
                  newParam.
                      deltaPicOrderAlwaysZeroFlag
                      = (rbsp.u(1) == 1);
                  (void)rbsp.se();
                  (void)rbsp.se();
                  const uint32
                      numRefFramesInPicOrderCntCycle
                      = rbsp.ue();
                  for (uint32 i = 0;
                       i <
                       numRefFramesInPicOrderCntCycle;
                       ++i) {
                     (void)rbsp.se();
                  }
               }
               tmp = rbsp.ue();
               QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_MED,
                        "H264Parser-->33.numrefFrames[%d].....\n",
                        tmp);
               tmp = rbsp.u(1);
               QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_MED,
                        "H264Parser-->33.gapsflag[%x].....\n",
                        tmp);
               newParam.picWidthInMbsMinus1 =
                   rbsp.ue();
               QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_MED,
                        "H264Parser-->33.picWidthInMbsMinus1[%d].....\n",
                        newParam.
                        picWidthInMbsMinus1);
               newParam.picHeightInMapUnitsMinus1 =
                   rbsp.ue();
               QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_MED,
                        "H264Parser-->33.gapsflag[%d].....\n",
                        newParam.
                        picHeightInMapUnitsMinus1);
               newParam.frameMbsOnlyFlag =
                   (rbsp.u(1) == 1);
               if (!newParam.frameMbsOnlyFlag)
                  (void)rbsp.u(1);
               (void)rbsp.u(1);
               tmp = rbsp.u(1);
               newParam.crop_left = 0;
               newParam.crop_right = 0;
               newParam.crop_top = 0;
               newParam.crop_bot = 0;

               if (tmp) {
                  newParam.crop_left = rbsp.ue();
                  newParam.crop_right = rbsp.ue();
                  newParam.crop_top = rbsp.ue();
                  newParam.crop_bot = rbsp.ue();
               }
               QTV_MSG_PRIO4(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_MED,
                        "H264Parser--->34 crop left %d, right %d, top %d, bot %d\n",
                        newParam.crop_left,
                        newParam.crop_right,
                        newParam.crop_top,
                        newParam.crop_bot);
            } else {
               QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_MED,
                        "H264Parser-->17.naluType[%x].....\n",
                        naluType);
               id = newParam.picSetID = rbsp.ue();
               newParam.seqSetID = rbsp.ue();
               (void)rbsp.u(1);
               newParam.picOrderPresentFlag =
                   (rbsp.u(1) == 1);
            }

            // We currently don't support updating existing parameter
            // sets.
            //const H264ParamNaluSet::const_iterator it = naluSet.find (id);
            H264ParamNalu *it = naluSet.find(id);
            if (it) {
               const uint32 tempSize = static_cast < uint32 > (it->nalu);   // ???
               if ((naluSize != tempSize)
                   || (0 !=
                  memcmp(&encodedBytes[naluStart],
                         &it->nalu,
                         static_cast <
                         int >(naluSize)))) {
                  QTV_MSG_PRIO(QTVDIAG_GENERAL,
                          QTVDIAG_PRIO_MED,
                          "H264Parser-->18.H264 stream contains two or \
                                      more parameter set NALUs having the \
                                      same ID -- this requires either a \
                                      separate parameter set ES or \
                                      multiple sample description atoms, \
                                      neither of which is currently \
                                      supported!");
                  break;
               }
            }
            // Otherwise, add NALU to appropriate NALU set.
            else {
               H264ParamNalu *newParamInSet =
                   naluSet.find(id);
               QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_MED,
                        "H264Parser-->19.newParamInset[%x]\n",
                        newParamInSet);
               if (!newParamInSet) {
                  QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                           QTVDIAG_PRIO_MED,
                           "H264Parser-->20.newParamInset[%x]\n",
                           newParamInSet);
                  newParamInSet = &temp;
                  memcpy(newParamInSet, &newParam,
                         sizeof(struct
                           H264ParamNalu));
               }
               QTV_MSG_PRIO4(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_MED,
                        "H264Parser-->21.encodebytes=%x naluStart=%x\n",
                        encodedBytes, naluStart,
                        naluSize, newParamInSet);
               QTV_MSG_PRIO2(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_MED,
                        "H264Parser-->22.naluSize=%x newparaminset=%p\n",
                        naluSize, newParamInSet);
               QTV_MSG_PRIO4(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_MED,
                        "H264Parser-->23.-->0x%x 0x%x 0x%x 0x%x\n",
                        (encodedBytes +
                         naluStart),
                        (encodedBytes +
                         naluStart + 1),
                        (encodedBytes +
                         naluStart + 2),
                        (encodedBytes +
                         naluStart + 3));

               memcpy(&newParamInSet->nalu,
                      (encodedBytes + naluStart),
                      sizeof(newParamInSet->nalu));
               QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_MED,
                        "H264Parser-->24.nalu=0x%x \n",
                        newParamInSet->nalu);
               naluSet.insert(id, newParamInSet);
            }
         }
         // Otherwise, if we are inside the frame, convert the NALU
         // and append it to the frame output, if its type is acceptable.
         else if (inFrame && (naluType != 0) && (naluType < 12)
             && (!stripSeiAud || (naluType != 9)
                 && (naluType != 6))) {
            uint8 sizeBuffer[4];
            sizeBuffer[0] =
                static_cast < uint8 > (naluSize >> 24);
            sizeBuffer[1] =
                static_cast < uint8 >
                ((naluSize >> 16) & 0xFF);
            sizeBuffer[2] =
                static_cast < uint8 >
                ((naluSize >> 8) & 0xFF);
            sizeBuffer[3] =
                static_cast < uint8 > (naluSize & 0xFF);
            /*lint -e{1025, 1703, 119, 64, 534}
             * These are known lint issues
             */
            //frame.insert (frame.end (), sizeBuffer,
            //              sizeBuffer + sizeof (sizeBuffer));
            /*lint -e{1025, 1703, 119, 64, 534, 632}
             * These are known lint issues
             */
            //frame.insert (frame.end (), encodedBytes + naluStart,
            //              encodedBytes + naluStart + naluSize);
         }
         // If NALU was a VCL, save VCL NALU parameters
         // for use in frame boundary detection.
         if (isVclNalu) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                    "H264Parser-->25.isvclnalu check passed\n");
            vclNaluFound = true;
            prevVclFrameNum = vclFrameNum;
            prevVclFieldPicFlag = vclFieldPicFlag;
            prevVclNalRefIdc = vclNalRefIdc;
            prevVclBottomFieldFlag = vclBottomFieldFlag;
            prevVclPicOrderCntLsb = vclPicOrderCntLsb;
            prevVclDeltaPicOrderCntBottom =
                vclDeltaPicOrderCntBottom;
            prevVclDeltaPicOrderCnt0 = vclDeltaPicOrderCnt0;
            prevVclDeltaPicOrderCnt1 = vclDeltaPicOrderCnt1;
         }
      }
      // If not currently in a NALU, detect next NALU start code.
      if ((cursor & 0xFFFFFF) == 0x000001) {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                 "H264Parser-->26..here\n");
         inNalu = true;
         naluStart = static_cast < uint32 > (pos + 1);
         if (0xFFFFFFFF == posNalDetected)
            posNalDetected = pos - 2;
      } else if (pos >= totalBytes) {
         QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "H264Parser-->27.pos[%x] totalBytes[%x]\n",
                  pos, totalBytes);
         break;
      }
   }

   uint64 tmpPos = 0;
   // find the first non-zero byte
   if (pos > 0) {
      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
               "H264Parser-->28.last loop[%x]\n", pos);
      tmpPos = pos - 1;
      while (tmpPos != 0 && encodedBytes[tmpPos] == 0)
         --tmpPos;
      // add 1 to get the beginning of the start code
      ++tmpPos;
   }
   QTV_MSG_PRIO3(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
            "H264Parser-->29.tmppos=%ld bytesConsumed=%x %x\n",
            tmpPos, bytesConsumed, posNalDetected);
   bytesConsumed = tmpPos;
   nalSize = static_cast < uint32 > (bytesConsumed - posNalDetected);

   // Fill in the height and width
   QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
            "H264Parser-->30.seq[%x] pic[%x]\n", this->seq.size(),
            this->pic.size());
   if (this->seq.size()) {
      m_height =
          (unsigned)(16 *
                (2 -
            (this->seq.begin()->frameMbsOnlyFlag)) *
                (this->seq.begin()->picHeightInMapUnitsMinus1 +
            1));
      m_width =
          (unsigned)(16 *
                (this->seq.begin()->picWidthInMbsMinus1 + 1));
      if ((m_height % 16) != 0) {
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "\n Height %d is not a multiple of 16",
                  m_height);
         m_height = (m_height / 16 + 1) * 16;
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "\n Height adjusted to %d \n", m_height);
      }
      if ((m_width % 16) != 0) {
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "\n Width %d is not a multiple of 16",
                  m_width);
         m_width = (m_width / 16 + 1) * 16;
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                  "\n Width adjusted to %d \n", m_width);
      }
      height = m_height;
      width = m_width;
      bInterlace = (!this->seq.begin()->frameMbsOnlyFlag);
      cropx = this->seq.begin()->crop_left << 1;
      cropy = this->seq.begin()->crop_top << 1;
      cropdx =
          width -
          ((this->seq.begin()->crop_left +
            this->seq.begin()->crop_right) << 1);
      cropdy =
          height -
          ((this->seq.begin()->crop_top +
            this->seq.begin()->crop_bot) << 1);

      QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
               "H264Parser-->31.cropdy [%x] cropdx[%x]\n",
               cropdy, cropdx);
      QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
               "H264Parser-->31.Height [%x] Width[%x]\n", height,
               width);
   }
   this->seq.eraseall();
   this->pic.eraseall();
   return validate_profile_and_level(profile_id, level_id);;
}

/* ======================================================================
FUNCTION
  H264_Utils::parse_first_h264_input_buffer

DESCRIPTION
  parse first h264 input buffer

PARAMETERS
  OMX_IN OMX_BUFFERHEADERTYPE* buffer.

RETURN VALUE
  true if success
  false otherwise
========================================================================== */
OMX_U32 H264_Utils::parse_first_h264_input_buffer(OMX_IN OMX_BUFFERHEADERTYPE *
                    buffer,
                    OMX_U32
                    size_of_nal_length_field)
{
   OMX_U32 c1, c2, c3, curr_ptr = 0;
   OMX_U32 i, j, aSize[4], size = 0;
   OMX_U32 header_len = 0;
   QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
            "H264 clip, NAL length field %d\n",
            size_of_nal_length_field);

   if (buffer == NULL) {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
              "Error - buffer is NULL\n");
   }

   if (size_of_nal_length_field == 0) {
      /* Start code with a lot of 0x00 before 0x00 0x00 0x01
         Need to move pBuffer to the first 0x00 0x00 0x00 0x01 */
      c1 = 1;
      c2 = buffer->pBuffer[curr_ptr++];
      c3 = buffer->pBuffer[curr_ptr++];
      do {
         if (curr_ptr >= buffer->nFilledLen) {
            QTV_MSG_PRIO(QTVDIAG_GENERAL,
                    QTVDIAG_PRIO_ERROR,
                    "ERROR: parse_first_h264_input_buffer - Couldn't find the first 2 NAL (SPS and PPS)\n");
            return 0;
         }
         c1 = c2;
         c2 = c3;
         c3 = buffer->pBuffer[curr_ptr++];
      } while (c1 || c2 || c3 == 0);

      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
               "curr_ptr = %d\n", curr_ptr);
      if (curr_ptr > 4) {
         // There are unnecessary 0x00
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
                 "Remove unnecessary 0x00 at SPS\n");
         memmove(buffer->pBuffer, &buffer->pBuffer[curr_ptr - 4],
            buffer->nFilledLen - curr_ptr - 4);
      }

      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
               "dat clip, NAL length field %d\n",
               size_of_nal_length_field);
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
              "Start code SPS 0x00 00 00 01\n");
      curr_ptr = 4;
      /* Start code 00 00 01 */
      for (OMX_U8 i = 0; i < 2; i++) {
         c1 = 1;
         c2 = buffer->pBuffer[curr_ptr++];
         c3 = buffer->pBuffer[curr_ptr++];
         do {
            if (curr_ptr >= buffer->nFilledLen) {
               QTV_MSG_PRIO(QTVDIAG_GENERAL,
                       QTVDIAG_PRIO_ERROR,
                       "ERROR: parse_first_h264_input_buffer - Couldn't find the first 2 NAL (SPS and PPS)\n");
               break;
            }
            c1 = c2;
            c2 = c3;
            c3 = buffer->pBuffer[curr_ptr++];
         } while (c1 || c2 || c3 != 1);
      }
      header_len = curr_ptr - 4;
   } else {
      /* NAL length clip */
      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
               "NAL length clip, NAL length field %d\n",
               size_of_nal_length_field);
      /* SPS size */
      for (i = 0; i < SIZE_NAL_FIELD_MAX - size_of_nal_length_field;
           i++) {
         aSize[SIZE_NAL_FIELD_MAX - 1 - i] = 0;
      }
      for (j = 0; i < SIZE_NAL_FIELD_MAX; i++, j++) {
         aSize[SIZE_NAL_FIELD_MAX - 1 - i] = buffer->pBuffer[j];
      }
      size = (uint32) (*((uint32 *) (aSize)));
      header_len = size + size_of_nal_length_field;
      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
               "OMX - SPS length %d\n", header_len);

      /* PPS size */
      for (i = 0; i < SIZE_NAL_FIELD_MAX - size_of_nal_length_field;
           i++) {
         aSize[SIZE_NAL_FIELD_MAX - 1 - i] = 0;
      }
      for (j = header_len; i < SIZE_NAL_FIELD_MAX; i++, j++) {
         aSize[SIZE_NAL_FIELD_MAX - 1 - i] = buffer->pBuffer[j];
      }
      size = (uint32) (*((uint32 *) (aSize)));
      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
               "OMX - PPS size %d\n", size);
      header_len += size + size_of_nal_length_field;
   }
   return header_len;
}

OMX_U32 H264_Utils::check_header(OMX_IN OMX_BUFFERHEADERTYPE * buffer,
             OMX_U32 sizeofNAL, bool & isPartial,
             OMX_U32 headerState)
{
   byte coef1, coef2, coef3;
   uint32 pos = 0;
   uint32 nal_len = 0, nal_len2 = 0;
   uint32 sizeofNalLengthField = 0;
   uint32 zero_count;
   OMX_U32 eRet = -1;
   OMX_U8 *nal1_ptr = NULL, *nal2_ptr = NULL;

   isPartial = true;
   QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_LOW,
           "H264_Utils::check_header ");

   if (!sizeofNAL) {

      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,
               "check_header: start code %d",
               buffer->nFilledLen);
      // Search start_code_prefix_one_3bytes (0x000001)
      coef2 = buffer->pBuffer[pos++];
      coef3 = buffer->pBuffer[pos++];
      do {
         if (pos >= buffer->nFilledLen) {
            QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                     QTVDIAG_PRIO_ERROR,
                     "Error at extract rbsp line %d",
                     __LINE__);
            return eRet;
         }

         coef1 = coef2;
         coef2 = coef3;
         coef3 = buffer->pBuffer[pos++];
      } while (coef1 || coef2 || coef3 != 1);

      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,
               "check_header: start code got fisrt NAL %d", pos);
      nal1_ptr = (OMX_U8 *) & buffer->pBuffer[pos];

      // Search start_code_prefix_one_3bytes (0x000001)
      if (pos + 2 < buffer->nFilledLen) {
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,
                  "check_header: start code looking for second NAL %d",
                  pos);
         isPartial = false;
         coef2 = buffer->pBuffer[pos++];
         coef3 = buffer->pBuffer[pos++];
         do {
            if (pos >= buffer->nFilledLen) {
               QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                        QTVDIAG_PRIO_HIGH,
                        "Error at extract rbsp line %d",
                        __LINE__);
               isPartial = true;
               break;
            }

            coef1 = coef2;
            coef2 = coef3;
            coef3 = buffer->pBuffer[pos++];
         } while (coef1 || coef2 || coef3 != 1);
      }
      if (!isPartial) {
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                  "check_header: start code two nals in one buffer %d",
                  pos);
         nal2_ptr = (OMX_U8 *) & buffer->pBuffer[pos];
         if (((nal1_ptr[0] & 0x1f) == NALU_TYPE_SPS)
             && ((nal2_ptr[0] & 0x1f) == NALU_TYPE_PPS)) {
            QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                     QTVDIAG_PRIO_ERROR,
                     "check_header: start code two nals in one buffer SPS+PPS %d",
                     pos);
            eRet = 0;
         } else if (((nal1_ptr[0] & 0x1f) == NALU_TYPE_SPS) && (buffer->nFilledLen < 512)) {
             eRet = 0;
         }


      } else {
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,
                  "check_header: start code partial nal in one buffer %d",
                  pos);
         if (headerState == 0
             && ((nal1_ptr[0] & 0x1f) == NALU_TYPE_SPS)) {
            eRet = 0;
         } else if (((nal1_ptr[0] & 0x1f) == NALU_TYPE_PPS)) {
            eRet = 0;
         } else
            eRet = -1;
      }

   } else {
      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
               "check_header: size nal %d", sizeofNAL);
      /* This is the case to play multiple NAL units inside each access unit */
      /* Extract the NAL length depending on sizeOfNALength field */
      sizeofNalLengthField = sizeofNAL;
      nal_len = 0;
      while (sizeofNAL--) {
         nal_len |= buffer->pBuffer[pos++] << (sizeofNAL << 3);
      }
      if (nal_len >= buffer->nFilledLen) {
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                  "Error at extract rbsp line %d",
                  __LINE__);
         return eRet;
      }
      QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
               "check_header: size nal  got fist NAL %d",
               nal_len);
      nal1_ptr = (OMX_U8 *) & buffer->pBuffer[pos];
      if ((nal_len + sizeofNalLengthField) < buffer->nFilledLen) {
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                  "check_header: getting second NAL %d",
                  buffer->nFilledLen);
         isPartial = false;
         pos += nal_len;
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                  "check_header: getting second NAL position %d",
                  pos);
         sizeofNAL = sizeofNalLengthField;
         nal_len2 = 0;
         while (sizeofNAL--) {
            nal_len2 |=
                buffer->pBuffer[pos++] << (sizeofNAL << 3);
         }
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                  "check_header: getting second NAL %d",
                  nal_len2);
         if (nal_len + nal_len2 + 2 * sizeofNalLengthField >
             buffer->nFilledLen) {
            QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                     QTVDIAG_PRIO_ERROR,
                     "Error at extract rbsp line %d",
                     __LINE__);
            return eRet;
         }
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                  "check_header: size nal  got second NAL %d",
                  nal_len);
         nal2_ptr = (OMX_U8 *) & buffer->pBuffer[pos];
      }

      if (!isPartial) {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                 "check_header: size nal  partial nal ");
         if (((nal1_ptr[0] & 0x1f) == NALU_TYPE_SPS)
             && ((nal2_ptr[0] & 0x1f) == NALU_TYPE_PPS)) {
            eRet = 0;
         }
      } else {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
                 "check_header: size nal  full header");
         if (headerState == 0
             && ((nal1_ptr[0] & 0x1f) == NALU_TYPE_SPS)) {
            eRet = 0;
         } else if (((nal1_ptr[0] & 0x1f) == NALU_TYPE_PPS)) {
            eRet = 0;
         } else
            eRet = -1;
      }

   }
   return eRet;
}

/*===========================================================================
FUNCTION:
  validate_profile_and_level

DESCRIPTION:
  This function validate the profile and level that is supported.

INPUT/OUTPUT PARAMETERS:
  uint32 profile
  uint32 level

RETURN VALUE:
  false it it's not supported
  true otherwise

SIDE EFFECTS:
  None.
===========================================================================*/
bool H264_Utils::validate_profile_and_level(uint32 profile, uint32 level)
{
   QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED,
            "H264 profile %d, level %d\n", profile, level);

   if ((m_default_profile_chk &&
        profile != BASELINE_PROFILE &&
        profile != MAIN_PROFILE &&
        profile != HIGH_PROFILE) || (m_default_level_chk && level > MAX_SUPPORTED_LEVEL)
       ) {
      return false;
   }

   return true;
}
