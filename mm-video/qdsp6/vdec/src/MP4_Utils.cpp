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
#include "MP4_Utils.h"
#include "omx_vdec.h"
# include <stdio.h>

#ifdef _ANDROID_
#include "cutils/properties.h"
#endif


/* -----------------------------------------------------------------------
** Forward Declarations
** ----------------------------------------------------------------------- */

/* =======================================================================
**                            Function Definitions
** ======================================================================= */

/*<EJECT>*/
/*===========================================================================
FUNCTION:
  MP4_Utils constructor

DESCRIPTION:
  Constructs an instance of the Mpeg4 Utilitys.

RETURN VALUE:
  None.
===========================================================================*/
MP4_Utils::MP4_Utils()
{
#ifdef _ANDROID_
   char property_value[PROPERTY_VALUE_MAX] = {0};
#endif

   m_SrcWidth = 0;
   m_SrcHeight = 0;
   m_default_profile_chk = true;
   m_default_level_chk = true;

#ifdef _ANDROID_
   if(0 != property_get("persist.omxvideo.profilecheck", property_value, NULL))
   {
       if(!strcmp(property_value, "false"))
       {
           m_default_profile_chk = false;
       }
   }
   else
   {
       QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED, "MP4_Utils:: Constr failed in \
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
       QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_MED, "MP4_Utils:: Constr failed in \
           getting value for the Android property [persist.omxvideo.levelcheck]");
   }
#endif
}

/* <EJECT> */
/*===========================================================================

FUNCTION:
  MP4_Utils destructor

DESCRIPTION:
  Destructs an instance of the Mpeg4 Utilities.

RETURN VALUE:
  None.
===========================================================================*/
MP4_Utils::~MP4_Utils()
{
}

/* <EJECT> */
/*===========================================================================
FUNCTION:
  read_bit_field

DESCRIPTION:
  This helper function reads a field of given size (in bits) out of a raw
  bitstream.

INPUT/OUTPUT PARAMETERS:
  posPtr:   Pointer to posInfo structure, containing current stream position
            information

  size:     Size (in bits) of the field to be read; assumed size <= 32

  NOTE: The bitPos is the next available bit position in the byte pointed to
        by the bytePtr. The bit with the least significant position in the byte
        is considered bit number 0.

RETURN VALUE:
  Value of the bit field required (stored in a 32-bit value, right adjusted).

SIDE EFFECTS:
  None.
---------------------------------------------------------------------------*/
uint32 MP4_Utils::read_bit_field(posInfoType * posPtr, uint32 size) {
   uint8 *bits = &posPtr->bytePtr[0];
   uint32 bitBuf =
       (bits[0] << 24) | (bits[1] << 16) | (bits[2] << 8) | bits[3];

   uint32 value = (bitBuf >> (32 - posPtr->bitPos - size)) & MASK(size);

   /* Update the offset in preparation for next field    */
   posPtr->bitPos += size;

   while (posPtr->bitPos >= 8) {
      posPtr->bitPos -= 8;
      posPtr->bytePtr++;
   }
   return value;

}

/* <EJECT> */
/*===========================================================================
FUNCTION:
  find_code

DESCRIPTION:
  This helper function searches a bitstream for a specific 4 byte code.

INPUT/OUTPUT PARAMETERS:
  bytePtr:          pointer to starting location in the bitstream
  size:             size (in bytes) of the bitstream
  codeMask:         mask for the code we are looking for
  referenceCode:    code we are looking for

RETURN VALUE:
  Pointer to a valid location if the code is found; 0 otherwise.

SIDE EFFECTS:
  None.
---------------------------------------------------------------------------*/
static uint8 *find_code
    (uint8 * bytePtr, uint32 size, uint32 codeMask, uint32 referenceCode) {
   uint32 code = 0xFFFFFFFF;
   for (uint32 i = 0; i < size; i++) {
      code <<= 8;
      code |= *bytePtr++;

      if ((code & codeMask) == referenceCode) {
         return bytePtr;
      }
   }

   printf("Unable to find code\n");

   return NULL;
}

/*
=============================================================================
FUNCTION:
  populateHeightNWidthFromShortHeader

DESCRIPTION:
  This function parses the short header and populates frame height and width
  into MP4_Utils.

INPUT/OUTPUT PARAMETERS:
  psBits - pointer to input stream of bits

RETURN VALUE:
  Error code

SIDE EFFECTS:
  None.

=============================================================================
*/
int16 MP4_Utils::populateHeightNWidthFromShortHeader(mp4StreamType * psBits) {
   bool extended_ptype = false;
   bool opptype_present = false;
   bool fCustomSourceFormat = false;
   uint32 marker_bit;
   uint32 source_format;
   m_posInfo.bitPos = 0;
   m_posInfo.bytePtr = psBits->data;
   m_dataBeginPtr = psBits->data;
   //22 -> short_video_start_marker
   if (SHORT_VIDEO_START_MARKER != read_bit_field(&m_posInfo, 22))
      return MP4_INVALID_VOL_PARAM;
   //8 -> temporal_reference
   //1 -> marker bit
   //1 -> split_screen_indicator
   //1 -> document_camera_indicator
   //1 -> full_picture_freeze_release
   read_bit_field(&m_posInfo, 13);
   source_format = read_bit_field(&m_posInfo, 3);
   switch (source_format) {
   case 1:
      // sub-QCIF
      m_SrcWidth = 128;
      m_SrcHeight = 96;
      break;

   case 2:
      // QCIF
      m_SrcWidth = 176;
      m_SrcHeight = 144;
      break;

   case 3:
      // CIF
      m_SrcWidth = 352;
      m_SrcHeight = 288;
      break;

   case 4:
      // 4CIF
      m_SrcWidth = 704;
      m_SrcHeight = 576;
      break;

   case 5:
      // 16CIF
      m_SrcWidth = 1408;
      m_SrcHeight = 1152;
      break;

   case 7:
      extended_ptype = true;
      break;

   default:
      return MP4_INVALID_VOL_PARAM;
   }

   if (extended_ptype) {
      /* Plus PTYPE (PLUSPTYPE)
       ** This codeword of 12 or 30 bits is comprised of up to three subfields:
       ** UFEP, OPPTYPE, and MPPTYPE.  OPPTYPE is present only if UFEP has a
       ** particular value.
       */

      /* Update Full Extended PTYPE (UFEP) */
      uint32 ufep = read_bit_field(&m_posInfo, 3);
      switch (ufep) {
      case 0:
         /* Only MMPTYPE fields are included in current picture header, the
          ** optional part of PLUSPTYPE (OPPTYPE) is not present
          */
         opptype_present = false;
         break;

      case 1:
         /* all extended PTYPE fields (OPPTYPE and MPPTYPE) are included in
          ** current picture header
          */
         opptype_present = true;
         break;

      default:
         return MP4ERROR_UNSUPPORTED_UFEP;
      }

      if (opptype_present) {
         /* The Optional Part of PLUSPTYPE (OPPTYPE) (18 bits) */
         /* source_format */
         source_format = read_bit_field(&m_posInfo, 3);
         switch (source_format) {
         case 1:
            /* sub-QCIF */
            m_SrcWidth = 128;
            m_SrcHeight = 96;
            break;

         case 2:
            /* QCIF */
            m_SrcWidth = 176;
            m_SrcHeight = 144;
            break;

         case 3:
            /* CIF */
            m_SrcWidth = 352;
            m_SrcHeight = 288;
            break;

         case 4:
            /* 4CIF */
            m_SrcWidth = 704;
            m_SrcHeight = 576;
            break;

         case 5:
            /* 16CIF */
            m_SrcWidth = 1408;
            m_SrcHeight = 1152;
            break;

         case 6:
            /* custom source format */
            fCustomSourceFormat = true;
            break;

         default:
            return MP4ERROR_UNSUPPORTED_SOURCE_FORMAT;
         }

         /* Custom PCF */
         read_bit_field(&m_posInfo, 1);

         /* Continue parsing to determine whether H.263 Profile 1,2, or 3 is present.
          ** Only Baseline profile P0 is supported
          ** Baseline profile doesn't have any ANNEX supported.
          ** This information is used initialize the DSP. First parse past the
          ** unsupported optional custom PCF and Annexes D, E, and F.
          */
         uint32 PCF_Annex_D_E_F = read_bit_field(&m_posInfo, 3);
         if (PCF_Annex_D_E_F != 0)
            return MP4ERROR_UNSUPPORTED_SOURCE_FORMAT;

         /* Parse past bit for Annex I, J, K, N, R, S, T */
         uint32 PCF_Annex_I_J_K_N_R_S_T =
             read_bit_field(&m_posInfo, 7);
         if (PCF_Annex_I_J_K_N_R_S_T != 0)
            return MP4ERROR_UNSUPPORTED_SOURCE_FORMAT;

         /* Parse past one marker bit, and three reserved bits */
         read_bit_field(&m_posInfo, 4);

         /* Parse past the 9-bit MPPTYPE */
         read_bit_field(&m_posInfo, 9);

         /* Read CPM bit */
         uint32 continuous_presence_multipoint =
             read_bit_field(&m_posInfo, 1);
         if (fCustomSourceFormat) {
            if (continuous_presence_multipoint) {
               /* PSBI always follows immediately after CPM if CPM = "1", so parse
                ** past the PSBI.
                */
               read_bit_field(&m_posInfo, 2);
            }
            /* Extract the width and height from the Custom Picture Format (CPFMT) */
            uint32 pixel_aspect_ration_code =
                read_bit_field(&m_posInfo, 4);
            if (pixel_aspect_ration_code == 0)
               return MP4_INVALID_VOL_PARAM;

            uint32 picture_width_indication =
                read_bit_field(&m_posInfo, 9);
            m_SrcWidth =
                ((picture_width_indication & 0x1FF) +
                 1) << 2;

            marker_bit = read_bit_field(&m_posInfo, 1);
            if (marker_bit == 0)
               return MP4_INVALID_VOL_PARAM;

            uint32 picture_height_indication =
                read_bit_field(&m_posInfo, 9);
            m_SrcHeight =
                (picture_height_indication & 0x1FF) << 2;
            QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                     QTVDIAG_PRIO_FATAL,
                     "m_SrcHeight =  %d\n",
                     m_SrcHeight);
            QTV_MSG_PRIO1(QTVDIAG_GENERAL,
                     QTVDIAG_PRIO_FATAL,
                     "m_SrcWidth =  %d\n", m_SrcWidth);
         }
      } else {
         /* UFEP must be "001" for INTRA picture types */
         return MP4_INVALID_VOL_PARAM;
      }
   }
   if (m_SrcWidth * m_SrcHeight >
       MP4_MAX_DECODE_WIDTH * MP4_MAX_DECODE_HEIGHT) {
      /* Frame dimesions greater than maximum size supported */
      QTV_MSG_PRIO2(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
               "Frame Dimensions not supported %d %d",
               m_SrcWidth, m_SrcHeight);
      return MP4ERROR_UNSUPPORTED_SOURCE_FORMAT;
   }
   return MP4ERROR_SUCCESS;
}

/* <EJECT> */
/*===========================================================================

FUNCTION:
  populateHeightNWidthFromVolHeader

DESCRIPTION:
  This function parses the VOL header and populates frame height and width
  into MP4_Utils.

NOTE:
 Q6 repeates the same parsing at its end and in the case of parsing failure it
 wont tell us the reason, so we need to repeat atleast a abridged version
 of parse, to know the reason if there is a parse failure.

INPUT/OUTPUT PARAMETERS:
  psBits - pointer to input stream of bits

RETURN VALUE:
  Error code

SIDE EFFECTS:
  None.

===========================================================================*/

bool MP4_Utils::parseHeader(mp4StreamType * psBits) {
   uint32 profile_and_level_indication = 0;
   uint8 VerID = 1; /* default value */
   long hxw = 0;

   m_posInfo.bitPos = 0;
   m_posInfo.bytePtr = psBits->data;
   m_dataBeginPtr = psBits->data;



   /* parsing Visual Object Seqence(VOS) header */
   m_posInfo.bytePtr = find_code(m_posInfo.bytePtr,
                                 psBits->numBytes,
                                 MASK(32),
                                 VISUAL_OBJECT_SEQUENCE_START_CODE);

   if ( m_posInfo.bytePtr == NULL )
   {
      QTV_MSG(QTVDIAG_VIDEO_TASK,"Video bit stream is not starting \
         with VISUAL_OBJECT_SEQUENCE_START_CODE");
      m_posInfo.bitPos  = 0;
      m_posInfo.bytePtr = psBits->data;

      uint32 start_marker = read_bit_field (&m_posInfo, 32);
      if ( (start_marker & SHORT_HEADER_MASK) == SHORT_HEADER_START_MARKER )
      {
         if(MP4ERROR_SUCCESS == populateHeightNWidthFromShortHeader(psBits))
         {
            QTV_MSG(QTVDIAG_VIDEO_TASK,"Short Header Found and parsed succesfully");
            return true;
         }
         else
         {
            QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_FATAL,
               "Short Header parsing failure");
            return false;
         }
      }
      else
      {
          QTV_MSG(QTVDIAG_VIDEO_TASK, "Could not find short header either");
          m_posInfo.bitPos = 0;
          m_posInfo.bytePtr = psBits->data;
      }
   }
   else
   {
      uint32 profile_and_level_indication = read_bit_field (&m_posInfo, 8);
      QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
         "MP4 profile and level %lx",profile_and_level_indication);

      if ((m_default_profile_chk && m_default_level_chk)
       && (profile_and_level_indication != RESERVED_OBJECT_TYPE)
       && (profile_and_level_indication != SIMPLE_PROFILE_LEVEL0)
       && (profile_and_level_indication != SIMPLE_PROFILE_LEVEL1)
       && (profile_and_level_indication != SIMPLE_PROFILE_LEVEL2)
       && (profile_and_level_indication != SIMPLE_PROFILE_LEVEL3)
       && (profile_and_level_indication != SIMPLE_PROFILE_LEVEL4A)
       && (profile_and_level_indication != SIMPLE_PROFILE_LEVEL5)
       && (profile_and_level_indication != SIMPLE_PROFILE_LEVEL6)
       && (profile_and_level_indication != SIMPLE_PROFILE_LEVEL0B)
       && (profile_and_level_indication != SIMPLE_SCALABLE_PROFILE_LEVEL0)
       && (profile_and_level_indication != SIMPLE_SCALABLE_PROFILE_LEVEL1)
       && (profile_and_level_indication != SIMPLE_SCALABLE_PROFILE_LEVEL2)
       && (profile_and_level_indication != ADVANCED_SIMPLE_PROFILE_LEVEL0)
       && (profile_and_level_indication != ADVANCED_SIMPLE_PROFILE_LEVEL1)
       && (profile_and_level_indication != ADVANCED_SIMPLE_PROFILE_LEVEL2)
       && (profile_and_level_indication != ADVANCED_SIMPLE_PROFILE_LEVEL3)
       && (profile_and_level_indication != ADVANCED_SIMPLE_PROFILE_LEVEL4)
       && (profile_and_level_indication != ADVANCED_SIMPLE_PROFILE_LEVEL5))
      {
         QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_FATAL,
              "Caution: INVALID_PROFILE_AND_LEVEL 0x%lx \n",profile_and_level_indication);
         return false;
      }
   }




   /* parsing Visual Object(VO) header*/
   /* note: for now, we skip over the user_data */
   m_posInfo.bytePtr = find_code(m_posInfo.bytePtr,
                             psBits->numBytes,
                             MASK(32),
                             VISUAL_OBJECT_START_CODE);
   if(m_posInfo.bytePtr == NULL)
   {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
         "Could not find VISUAL_OBJECT_START_CODE");

      m_posInfo.bitPos = 0;
      m_posInfo.bytePtr = psBits->data;
   }
   else
   {
      uint32 is_visual_object_identifier = read_bit_field (&m_posInfo, 1);
      if ( is_visual_object_identifier )
      {
         /* visual_object_verid*/
         read_bit_field (&m_posInfo, 4);
         /* visual_object_priority*/
         read_bit_field (&m_posInfo, 3);
      }

      /* visual_object_type*/
      uint32 visual_object_type = read_bit_field (&m_posInfo, 4);
      if ( visual_object_type != VISUAL_OBJECT_TYPE_VIDEO_ID )
      {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
            "visual_object_type can only be VISUAL_OBJECT_TYPE_VIDEO_ID");
        return false;
      }
      /* skipping video_signal_type params*/


      /*parsing Video Object header*/
      m_posInfo.bytePtr = find_code(m_posInfo.bytePtr,
                                    psBits->numBytes,
                                    VIDEO_OBJECT_START_CODE_MASK,
                                    VIDEO_OBJECT_START_CODE);
      if ( m_posInfo.bytePtr == NULL )
      {
         QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_FATAL,
            "Unable to find VIDEO_OBJECT_START_CODE");
        return false;
      }
   }

   /* parsing Video Object Layer(VOL) header */
   m_posInfo.bitPos = 0;
   m_posInfo.bytePtr = find_code(m_posInfo.bytePtr,
                            psBits->numBytes,
                            VIDEO_OBJECT_LAYER_START_CODE_MASK,
                            VIDEO_OBJECT_LAYER_START_CODE);
   if ( m_posInfo.bytePtr == NULL )
   {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
         "Unable to find VIDEO_OBJECT_LAYER_START_CODE");
      m_posInfo.bitPos = 0;
      m_posInfo.bytePtr = psBits->data;
      m_posInfo.bytePtr = find_code(m_posInfo.bytePtr,
                                psBits->numBytes,
                                SHORT_HEADER_MASK,
                                SHORT_HEADER_START_CODE);
      if( m_posInfo.bytePtr )
      {
         if(MP4ERROR_SUCCESS == populateHeightNWidthFromShortHeader(psBits))
         {
            return true;
         }
         else
         {
            QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_FATAL,
               "Short Header parsing failure");
            return false;
         }
      }
      else
      {
         QTV_MSG(QTVDIAG_VIDEO_TASK,
                 "Unable to find VIDEO_OBJECT_LAYER or SHORT_HEADER START CODE");
         return MP4_INVALID_VOL_PARAM;
      }
   }

   // 1 -> random accessible VOL
   read_bit_field(&m_posInfo, 1);

   uint32 video_object_type_indication = read_bit_field (&m_posInfo, 8);
   QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_MED,
      "Video Object Type %lx",video_object_type_indication);
   if ( (video_object_type_indication != SIMPLE_OBJECT_TYPE) &&
       (video_object_type_indication != SIMPLE_SCALABLE_OBJECT_TYPE) &&
       (video_object_type_indication != CORE_OBJECT_TYPE) &&
       (video_object_type_indication != ADVANCED_SIMPLE) &&
       (video_object_type_indication != RESERVED_OBJECT_TYPE) &&
       (video_object_type_indication != MAIN_OBJECT_TYPE))
   {
      QTV_MSG_PRIO1(QTVDIAG_GENERAL,QTVDIAG_PRIO_FATAL,
         "Video Object Type not supported %lx",video_object_type_indication);
      return false;
   }
   /* is_object_layer_identifier*/
   uint32 is_object_layer_identifier = read_bit_field (&m_posInfo, 1);
   if ( is_object_layer_identifier )
   {
      uint32 video_object_layer_verid = read_bit_field (&m_posInfo, 4);
      uint32 video_object_layer_priority = read_bit_field (&m_posInfo, 3);
      VerID = (unsigned char)video_object_layer_verid;
   }

  /* aspect_ratio_info*/
  uint32 aspect_ratio_info = read_bit_field (&m_posInfo, 4);
  if ( aspect_ratio_info == EXTENDED_PAR )
  {
    /* par_width*/
    read_bit_field (&m_posInfo, 8);
    /* par_height*/
    read_bit_field (&m_posInfo, 8);
  }



   /* vol_control_parameters */
   uint32 vol_control_parameters = read_bit_field (&m_posInfo, 1);
   if ( vol_control_parameters )
   {
      /* chroma_format*/
      uint32 chroma_format = read_bit_field (&m_posInfo, 2);
      if ( chroma_format != 1 )
      {
         QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_FATAL,
            "returning CHROMA_FORMAT_NOT_4_2_0 ");
         return false;
      }

      /* low_delay*/
      uint32 low_delay = read_bit_field (&m_posInfo, 1);
      if ( !low_delay )
      {
         QTV_MSG(QTVDIAG_VIDEO_TASK,"Possible B_VOPs in bitstream.");
      }

      /* vbv_parameters (annex D)*/
      uint32 vbv_parameters = read_bit_field (&m_posInfo, 1);
      if ( vbv_parameters )
      {
         /* first_half_bitrate*/
         uint32 first_half_bitrate = read_bit_field (&m_posInfo, 15);
         uint32 marker_bit = read_bit_field (&m_posInfo, 1);
         if ( marker_bit != 1)
         {
            QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_FATAL,
               "Marker bit not enabled, VOL header parsing failure ");
            return false;
         }

         /* latter_half_bitrate*/
         uint32 latter_half_bitrate = read_bit_field (&m_posInfo, 15);
         marker_bit = read_bit_field (&m_posInfo, 1);
         if ( marker_bit != 1)
         {
            QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_FATAL,
               "Marker bit not enabled, VOL header parsing failure ");
            return false;
         }

         uint32 VBVPeakBitRate = (first_half_bitrate << 15) + latter_half_bitrate;
         if ( VBVPeakBitRate > MAX_BITRATE )
         {
             QTV_MSG_PRIO1(QTVDIAG_VIDEO_TASK, QTVDIAG_PRIO_ERROR,
                            "VBV MAX_BITRATE_EXCEEDED %lx",VBVPeakBitRate);
         }
         else
         {
             QTV_MSG_PRIO1(QTVDIAG_VIDEO_TASK, QTVDIAG_PRIO_ERROR,
                            "VBV Peak bit rate %lx",VBVPeakBitRate);
         }

         /* first_half_vbv_buffer_size*/
         uint32 first_half_vbv_buffer_size = read_bit_field (&m_posInfo, 15);
         marker_bit = read_bit_field (&m_posInfo, 1);
         if ( marker_bit != 1)
         {
            QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_FATAL,
               "Marker bit not enabled, VOL header parsing failure ");
            return false;
         }

         /* latter_half_vbv_buffer_size*/
         uint32 latter_half_vbv_buffer_size = read_bit_field (&m_posInfo, 3);

         uint32 VBVBufferSize = (first_half_vbv_buffer_size << 3) + latter_half_vbv_buffer_size;
         if ( VBVBufferSize > MAX_VBVBUFFERSIZE )
         {
             QTV_MSG_PRIO1(QTVDIAG_VIDEO_TASK, QTVDIAG_PRIO_ERROR,
               "VBV MAX_VBVBUFFERSIZE_EXCEEDED %lx",VBVBufferSize);
         }

         /* first_half_vbv_occupancy*/
         uint32 first_half_vbv_occupancy = read_bit_field (&m_posInfo, 11);
         marker_bit = read_bit_field (&m_posInfo, 1);
         if ( marker_bit != 1)
         {
            QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_FATAL,
            "Marker bit not enabled, VOL header parsing failure ");
            return false;
         }

         /* latter_half_vbv_occupancy*/
         uint32 latter_half_vbv_occupancy = read_bit_field (&m_posInfo, 15);
         marker_bit = read_bit_field (&m_posInfo, 1);
         if ( marker_bit != 1)
         {
            QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_FATAL,
               "Marker bit not enabled, VOL header parsing failure ");
            return false;
         }
      }/* vbv_parameters*/
   }/*vol_control_parameters*/


   /* video_object_layer_shape*/
   uint32 video_object_layer_shape = read_bit_field (&m_posInfo, 2);
   uint8 VOLShape = (unsigned char)video_object_layer_shape;
   if ( VOLShape != MPEG4_SHAPE_RECTANGULAR )
   {
       QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_FATAL,
         "NON RECTANGULAR_SHAPE detected, it is not supported");
       return false;
   }

   /* marker_bit*/
   uint32 marker_bit = read_bit_field (&m_posInfo, 1);
   if ( marker_bit != 1 )
   {
      QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_FATAL,
         "Marker bit not enabled, VOL header parsing failure ");
      return false;
   }

   /* vop_time_increment_resolution*/
   uint32 vop_time_increment_resolution = read_bit_field (&m_posInfo, 16);
   uint16 TimeIncrementResolution = (unsigned short)vop_time_increment_resolution;
   /* marker_bit*/
   marker_bit = read_bit_field (&m_posInfo, 1);
   if ( marker_bit != 1 )
   {
      QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_FATAL,
         "Marker bit not enabled, VOL header parsing failure ");
      return false;
   }

   /* compute the nr. of bits for time information in bitstream*/
   {
       int i,j;
       uint8 NBitsTime=0;
       i = TimeIncrementResolution-1;
       j = 0;
       while (i)
       {
         j++;
         i>>=1;
       }
           if (j)
            NBitsTime = j;
           else
               /* the time increment resolution is 1, so we need one bit to
               ** represent it
               */
               NBitsTime = 1;

      /* fixed_vop_rate*/
      uint32 fixed_vop_rate = read_bit_field (&m_posInfo, 1);
      if ( fixed_vop_rate )
      {
          /* fixed_vop_increment*/
         read_bit_field (&m_posInfo, NBitsTime);
      }
   }


  /* marker_bit*/
   marker_bit = read_bit_field (&m_posInfo, 1);
   if ( marker_bit != 1 )
   {
      QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_FATAL,
         "Marker bit not enabled, VOL header parsing failure ");
      return false;
   }

   /* video_object_layer_width*/
   m_SrcWidth  = (uint16)read_bit_field (&m_posInfo, 13);

   /* marker_bit*/
   marker_bit = read_bit_field (&m_posInfo, 1);
   if ( marker_bit != 1 )
   {
      QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_FATAL,
         "Marker bit not enabled, VOL header parsing failure ");
      return false;
   }

   /* video_object_layer_height*/
   m_SrcHeight  = (uint16)read_bit_field (&m_posInfo, 13);

   /* marker_bit*/
   marker_bit = read_bit_field (&m_posInfo, 1);
   if ( marker_bit != 1 )
   {
      QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_FATAL,
         "Marker bit not enabled, VOL header parsing failure ");
      return false;
   }


   /* interlaced*/
   uint32 interlaced = read_bit_field (&m_posInfo, 1);
   if (interlaced)
   {
      QTV_MSG(QTVDIAG_VIDEO_TASK,"INTERLACED frames detected");
   }

   /* obmc_disable*/
   uint32 obmc_disable = read_bit_field (&m_posInfo, 1);
   if ( !obmc_disable )
   {
       QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_FATAL,
         "returning OBMC_Enabled it is not supported ");
       return false;
   }

   /* Nr. of bits for sprite_enabled is 1 for version 1, and 2 for
   ** version 2, according to p. 114, Table v2-2. */
   /* sprite_enable*/
   uint32 sprite_enable = read_bit_field (&m_posInfo,VerID);
   if ( sprite_enable  )
   {
      QTV_MSG_PRIO(QTVDIAG_GENERAL,QTVDIAG_PRIO_FATAL,
         "returning SPRITE_VOP_NG Not Supported ");
      return false;
   }

   uint32 not_8_bit = read_bit_field (&m_posInfo, 1);
   if ( not_8_bit )
   {
      /* quant_precision*/
      uint32 quant_precision = read_bit_field (&m_posInfo, 4);
       if ( quant_precision  < MIN_QUANTPRECISION
            || quant_precision  > MAX_QUANTPRECISION )
      {
         QTV_MSG(QTVDIAG_VIDEO_TASK,"returning INVALID_QUANT_PRECISION ");
/* though per standard we can fail playback here, we choose not to
 * we will simply leave a msg
 */
//         return false;
      }

      /* bits_per_pixel*/
      uint32 BitsPerPixel = read_bit_field (&m_posInfo, 4);
      if ( BitsPerPixel < 4 || BitsPerPixel > 12 )
      {
      QTV_MSG(QTVDIAG_VIDEO_TASK,"returning INVALID_BITS_PER_PIXEL ");
/* though per standard we can fail playback here, we choose not to
 * we will simply leave a msg
 */
//      return false;
      }
   }

   /* quant_type*/
   if (read_bit_field (&m_posInfo, 1)) {
     /*load_intra_quant_mat*/
     if (read_bit_field (&m_posInfo, 1)) {
       unsigned char cnt = 2, data;
       /*intra_quant_mat */
       read_bit_field (&m_posInfo, 8);
       data = read_bit_field (&m_posInfo, 8);
       while (data && cnt < 64) {
         data = read_bit_field (&m_posInfo, 8);
         cnt++;
       }
     }
     /*load_non_intra_quant_mat*/
     if (read_bit_field (&m_posInfo, 1)) {
       unsigned char cnt = 2, data;
       /*non_intra_quant_mat */
       read_bit_field (&m_posInfo, 8);
       data = read_bit_field (&m_posInfo, 8);
       while (data && cnt < 64) {
         data = read_bit_field (&m_posInfo, 8);
         cnt++;
       }
     }
   }
   if ( VerID != 1 )
   {
     /* quarter_sample*/
     read_bit_field (&m_posInfo, 1);
   }
   /* complexity_estimation_disable*/
   read_bit_field (&m_posInfo, 1);
   /* resync_marker_disable*/
   read_bit_field (&m_posInfo, 1);
   /* data_partitioned*/
   if ( read_bit_field (&m_posInfo, 1) ) {
     hxw = m_SrcWidth* m_SrcHeight;
     if(hxw > (OMX_CORE_WVGA_WIDTH*OMX_CORE_WVGA_HEIGHT)) {
       QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_ERROR,
         "Data partition clips not supported for Greater than WVGA resolution \n");
       return false;
     }
   }

   return true;

}

/*===========================================================================

FUNCTION:
  parseSparkHeader

DESCRIPTION:
  This function decodes the Spark header and populates the frame width and
  frame height info in the MP4_Utils members.

INPUT/OUTPUT PARAMETERS:
  psBits - pointer to input stream of bits

RETURN VALUE:
  Error code

SIDE EFFECTS:
  None.

===========================================================================*/
bool MP4_Utils::parseSparkHeader(mp4StreamType * psBits)
{
    m_posInfo.bitPos = 0;
    m_posInfo.bytePtr = psBits->data;
    m_dataBeginPtr = psBits->data;

    if (psBits->numBytes < 30)
    {
        printf("SPARK header length less than 30\n");
        return false;
    }
    // try to find SPARK format 0 start code which is the same as h263 start code

    m_posInfo.bytePtr = find_code(m_posInfo.bytePtr,
                                  psBits->numBytes,
                                  SHORT_HEADER_MASK,
                                  SHORT_HEADER_START_CODE);

    if ( m_posInfo.bytePtr == NULL )
    {
         m_posInfo.bitPos = 0;
         m_posInfo.bytePtr = psBits->data;
        //Could not find SPARK format 0 start code
        //Now, try to find SPARK format 1 start code
        m_posInfo.bytePtr = find_code(m_posInfo.bytePtr,
                                      psBits->numBytes,
                                      SHORT_HEADER_MASK,
                                      SPARK1_START_CODE);
    }

    if (m_posInfo.bytePtr == NULL)
    {
        printf("Could not find SPARK format 0 or format 1 headers\n");
        return false;
    }
    m_posInfo.bitPos = 0;
    m_posInfo.bytePtr = psBits->data;

    // skip 22 bits of the start code
    read_bit_field(&m_posInfo, 22);

    // skip 8 bits of Temporal reference field
    read_bit_field(&m_posInfo, 8);

    // read the source format
    uint32 sourceFormat = 0;
    sourceFormat = read_bit_field(&m_posInfo, 3);

    switch (sourceFormat)
    {
    case 0:
        m_SrcWidth = read_bit_field(&m_posInfo, 8);
        m_SrcHeight = read_bit_field(&m_posInfo, 8);
        break;
    case 1:
        m_SrcWidth = read_bit_field(&m_posInfo, 16);
        m_SrcHeight = read_bit_field(&m_posInfo, 16);
        break;
      case 2:           // CIF
          m_SrcWidth = 352;
        m_SrcHeight = 288;
        break;
    case 3:       // QCIF
        m_SrcWidth = 176;
        m_SrcHeight = 144;
        break;
    case 4:       // SQCIF
        m_SrcWidth = 128;
        m_SrcHeight = 96;
        break;
    case 5:       // QVGA
        m_SrcWidth = 320;
        m_SrcHeight = 240;
        break;
    case 6:
        m_SrcWidth = 160;
        m_SrcHeight = 120;
        break;
    default:
        return false;
    }

    return true;
}

/* <EJECT> */

/*
=============================================================================
FUNCTION:
  HasFrame

DESCRIPTION:
  This function parses the buffer in the OMX_BUFFERHEADER to check if there is a VOP

INPUT/OUTPUT PARAMETERS:
  buffer - pointer to OMX buffer header

RETURN VALUE:
  true if the buffer contains a VOP, false otherwise.

SIDE EFFECTS:
  None.

=============================================================================
*/
bool MP4_Utils::HasFrame(OMX_IN OMX_BUFFERHEADERTYPE * buffer)
{
   return find_code(buffer->pBuffer, buffer->nFilledLen,
          VOP_START_CODE_MASK, VOP_START_CODE) != NULL;
}

/*===========================================================================
FUNCTION:
  MP4_Utils::parse_frames_in_chunk

DESCRIPTION:
  Calculates number of valid frames present in the chunk based on frame header
  and set the timestamp interval based on the previous timestamp interval

INPUT/OUTPUT PARAMETERS:
  IN const uint8* pBitstream
  IN uint32 size
  IN int64 timestamp_interval
  OUT mp4_frame_info_type *frame_info

RETURN VALUE:
  number of VOPs in chunk

SIDE EFFECTS:
  noOfVopsInSameChunk is modified with number of frames in the chunk.
===========================================================================*/
uint32 MP4_Utils::parse_frames_in_chunk(const uint8* pBitstream,
                                        uint32 size,
                                        int64 timestamp_interval,
                                        mp4_frame_info_type *frame_info)
{
  int i = 0;
  uint32 code = 0;
  uint32 noOfVopsInSameChunk = 0;
  uint32 vopType = -1;

  if (timestamp_interval == 0)
  {
    QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,"Timestamp interval = 0. Setting the timestamp interval into 33");
    timestamp_interval = 33;
  }

  if (size == 0)
  {
    code = (pBitstream[i]<<24) | (pBitstream[i+1]<<16) | (pBitstream[i+2]<<8) | pBitstream[i+3];
    i += 4;
  }

  for (; (i<size) && (noOfVopsInSameChunk < MAX_FRAMES_IN_CHUNK); ++i)
  {
    if (code == VOP_START_CODE)
    {
       frame_info[noOfVopsInSameChunk].offset = i-4;
       if(noOfVopsInSameChunk > 0)
       {
        frame_info[noOfVopsInSameChunk-1].size = (i-4)-(frame_info[noOfVopsInSameChunk-1].offset);
        frame_info[noOfVopsInSameChunk].timestamp_increment = timestamp_interval * (noOfVopsInSameChunk-1);
      }
      vopType = 0x000000c0 & (pBitstream[i]);
      if(0x00000000 == vopType) frame_info[noOfVopsInSameChunk].vopType = MPEG4_I_VOP;
      else if(0x00000040 == vopType)  frame_info[noOfVopsInSameChunk].vopType = MPEG4_P_VOP;
      else if(0x00000080 == vopType)  frame_info[noOfVopsInSameChunk].vopType = MPEG4_B_VOP;
      else if(0x000000c0 == vopType)  frame_info[noOfVopsInSameChunk].vopType = MPEG4_S_VOP;
      noOfVopsInSameChunk++;
      code = 0;
    }
    code <<= 8;
    code |= (0x000000FF & (pBitstream[i]));
  }

  if(noOfVopsInSameChunk > 0 && noOfVopsInSameChunk <= MAX_FRAMES_IN_CHUNK)
  {
     frame_info[noOfVopsInSameChunk-1].size = size-(frame_info[noOfVopsInSameChunk-1].offset);
  }

  if(noOfVopsInSameChunk > 1)
  {
    frame_info[0].timestamp_increment = timestamp_interval * (noOfVopsInSameChunk-1);
  }
  else if (noOfVopsInSameChunk == 1)
  {
    frame_info[0].timestamp_increment = 0;
  }

  if(noOfVopsInSameChunk == MAX_FRAMES_IN_CHUNK)
  {
      QTV_MSG_PRIO(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,"NumFramesinChunk reached Max Value, So possible multiple VOPs in the last frame");
  }
   QTV_MSG_PRIO1(QTVDIAG_GENERAL, QTVDIAG_PRIO_HIGH,"FramesinChunk %d", noOfVopsInSameChunk);
  return noOfVopsInSameChunk;
}
