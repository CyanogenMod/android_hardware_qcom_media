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
#ifndef MP4_UTILS_H
#define MP4_UTILS_H
/*=============================================================================
      MP4_UTILS.h

DESCRIPTION
  This file declares an MP4_Utils helper class to help parse the
  VOL (Video Object Layer) header and the short header in a raw bitstream.

  There is a dependency on MPEG4 decoder.

  Historocally, this was part of MP4_TL.h, and the only way to parse headers
  was to have an instance of MP4_TL. However, the future direction for MP4_TL
  is that (a) it's constructor be private, (b) instances are to be created by
  a static member factory, and (c) this factory can only be called at
  vdec_create time.

  This poses a problem when we wish to parse VOL (or other) headers before
  calling vdec_create.

  The initial solution was to permit MP4_TL to be contructed, and have its
  iDecodeVOLHeader method called without having its Initialize method called,
  but this required hacking its destructor to not ASSERT when called in the
  NOTINIT state. This goes against the desire to restrict how and when an
  MP4_TL object is created. Thus, we factor the code out, in the hopes
  that the future MP4_TL implementation will use it.

LIMITATIONS:

ABSTRACT:
   MPEG 4 video decoding and encoding functionality is accomplished by
   several modules including but not limited to the: media player
   application, MPEG 4 engine, MPEG 4 video codec, & MPEG 4 audio codec
   as outlined below:

EXTERNALIZED FUNCTIONS
  List functions and a brief description that are exported from this file

INITIALIZATION AND SEQUENCING REQUIREMENTS
  is only needed if the order of operations is important.


============================================================================*/

/* ==========================================================================

                     INCLUDE FILES FOR MODULE

========================================================================== */
#include "qtv_msg.h"
#include "OMX_Core.h"
#include "qtypes.h"

/* ==========================================================================

                DEFINITIONS AND DECLARATIONS FOR MODULE

This section contains definitions for constants, macros, types, variables
and other items needed by this module.

========================================================================== */

/*---------------------------------------------------------------------------
** MPEG 4 Command Queue, Statistics Queue, Free Queues and Available Packets
**--------------------------------------------------------------------------- */

/* Structure used to manage input data.
*/
//#if 0
typedef unsigned long int uint32;   /* Unsigned 32 bit value */
typedef unsigned short uint16;   /* Unsigned 16 bit value */
typedef unsigned char uint8;   /* Unsigned 8  bit value */

typedef signed long int int32;   /* Signed 32 bit value */
typedef signed short int16;   /* Signed 16 bit value */
typedef signed char int8;   /* Signed 8  bit value */

typedef unsigned char byte;   /* Unsigned 8  bit value type. */
//#endif
#define SIMPLE_PROFILE_LEVEL0            0x08
#define SIMPLE_PROFILE_LEVEL1            0x01
#define SIMPLE_PROFILE_LEVEL2            0x02
#define SIMPLE_PROFILE_LEVEL3            0x03
#define SIMPLE_PROFILE_LEVEL4A            0x04
#define SIMPLE_PROFILE_LEVEL5            0x05
#define SIMPLE_PROFILE_LEVEL6            0x06
#define SIMPLE_PROFILE_LEVEL0B           0x09

#define SIMPLE_SCALABLE_PROFILE_LEVEL0                  0x10
#define SIMPLE_SCALABLE_PROFILE_LEVEL1                  0x11
#define SIMPLE_SCALABLE_PROFILE_LEVEL2                  0x12

#define SIMPLE_SCALABLE_PROFILE_LEVEL0  0x10
#define SIMPLE_SCALABLE_PROFILE_LEVEL1  0x11
#define SIMPLE_SCALABLE_PROFILE_LEVEL2  0x12
#define ADVANCED_SIMPLE_PROFILE_LEVEL0  0xF0
#define ADVANCED_SIMPLE_PROFILE_LEVEL1  0xF1
#define ADVANCED_SIMPLE_PROFILE_LEVEL2  0xF2
#define ADVANCED_SIMPLE_PROFILE_LEVEL3  0xF3
#define ADVANCED_SIMPLE_PROFILE_LEVEL4  0xF4
#define ADVANCED_SIMPLE_PROFILE_LEVEL5  0xF5

#define VISUAL_OBJECT_SEQUENCE_START_CODE   0x000001B0
#define MP4ERROR_SUCCESS     0

#define VIDEO_OBJECT_LAYER_START_CODE_MASK  0xFFFFFFF0
#define VIDEO_OBJECT_LAYER_START_CODE       0x00000120
#define VOP_START_CODE_MASK                 0xFFFFFFFF
#define VOP_START_CODE                      0x000001B6
#define SHORT_HEADER_MASK                   0xFFFFFC00
#define SHORT_HEADER_START_MARKER           0x00008000
#define SHORT_HEADER_START_CODE             0x00008000
#define SPARK1_START_CODE                   0x00008400
#define MPEG4_SHAPE_RECTANGULAR               0x00
#define EXTENDED_PAR                        0xF
#define SHORT_VIDEO_START_MARKER         0x20
#define MP4_INVALID_VOL_PARAM   (0x0001)   // unsupported VOL parameter
#define MP4ERROR_UNSUPPORTED_UFEP                   -1068
#define MP4ERROR_UNSUPPORTED_SOURCE_FORMAT          -1069
#define MASK(x) (0xFFFFFFFF >> (32 - (x)))
#define VISUAL_OBJECT_TYPE_VIDEO_ID         0x1
#define VISUAL_OBJECT_START_CODE            0x000001B5
#define VIDEO_OBJECT_START_CODE_MASK        0xFFFFFFE0
#define VIDEO_OBJECT_START_CODE             0x00000100

#define RESERVED_OBJECT_TYPE                0x0
#define SIMPLE_OBJECT_TYPE                  0x1
#define SIMPLE_SCALABLE_OBJECT_TYPE         0x2
#define CORE_OBJECT_TYPE                    0x3
#define MAIN_OBJECT_TYPE                    0x4
#define N_BIT_OBJECT_TYPE                   0x5
#define BASIC_ANIMATED_2D_TEXTURE           0x6
#define ANIMATED_2D_MESH                    0x7
#define ADVANCED_SIMPLE                     0x11


#define SIMPLE_L1_MAX_VBVBUFFERSIZE 10  /* VBV Max Buffer size=10 (p. 498)  */
#define SIMPLE_L1_MAX_BITRATE       160 /* is is 64kpbs or 160 400bits/sec units */
#define SIMPLE_L2_MAX_VBVBUFFERSIZE 40  /* VBV Max Buffer size = 40 */
#define SIMPLE_L2_MAX_BITRATE       320 /* 320 400bps units = 128kpbs */
#define SIMPLE_L3_MAX_VBVBUFFERSIZE 40  /* VBV Max Buffer size = 40 */
#define SIMPLE_L3_MAX_BITRATE       960 /* 960 400bps units = 384kpbs */

/* The MP4 decoder currently supports Simple Profile@L3 */
#define MAX_VBVBUFFERSIZE (SIMPLE_L3_MAX_VBVBUFFERSIZE)
#define MAX_BITRATE       (SIMPLE_L3_MAX_BITRATE)

#define MAX_QUANTPRECISION 9
#define MIN_QUANTPRECISION 3

#define MP4_VGA_WIDTH             640
#define MP4_VGA_HEIGHT            480
#define MP4_WVGA_WIDTH            800
#define MP4_WVGA_HEIGHT           480
#define MP4_720P_WIDTH            1280
#define MP4_720P_HEIGHT           720

#define MP4_MAX_DECODE_WIDTH    MP4_720P_WIDTH
#define MP4_MAX_DECODE_HEIGHT   MP4_720P_HEIGHT

typedef struct {
   unsigned char *data;
   unsigned long int numBytes;
} mp4StreamType;

#define MAX_FRAMES_IN_CHUNK                 10
#define VOP_START_CODE                      0x000001B6
#define VOL_START_CODE                      0x000001B0

typedef enum VOPTYPE
{
  NO_VOP = -1, // bitstream contains no VOP.
  MPEG4_I_VOP = 0,   // bitstream contains an MPEG4 I-VOP
  MPEG4_P_VOP = 1,   // bitstream contains an MPEG4 P-VOP
  MPEG4_B_VOP = 2,   // bitstream contains an MPEG4 B-VOP
  MPEG4_S_VOP = 3,   // bitstream contains an MPEG4 S-VOP
} VOP_TYPE;

typedef struct
{
  uint32    timestamp_increment;
  uint32    offset;
  uint32    size;
  VOP_TYPE  vopType;
} mp4_frame_info_type;


class MP4_Utils {
      private:
   struct posInfoType {
      uint8 *bytePtr;
      uint8 bitPos;
   };

   posInfoType m_posInfo;
   byte *m_dataBeginPtr;

   uint16 m_SrcWidth, m_SrcHeight;   // Dimensions of the source clip

   bool m_default_profile_chk;
   bool m_default_level_chk;

      public:

    uint16 SrcWidth(void) const {
      return m_SrcWidth;
   } uint16 SrcHeight(void)const {
      return m_SrcHeight;
   } static bool HasFrame(OMX_IN OMX_BUFFERHEADERTYPE * buffer);

   /* <EJECT> */
/*===========================================================================

FUNCTION:
  MP4_Utils constructor

DESCRIPTION:
  Constructs an instance of the MP4 Parser.

RETURN VALUE:
  None.
===========================================================================*/
    MP4_Utils();

/* <EJECT> */
/*===========================================================================

FUNCTION:
  MP4_Utils destructor

DESCRIPTION:
  Destructs an instance of the MP4_Utils.

RETURN VALUE:
  None.
===========================================================================*/
   ~MP4_Utils();

/*
============================================================================
*/
   int16 populateHeightNWidthFromShortHeader(mp4StreamType * psBits);

/* <EJECT> */
/*===========================================================================

FUNCTION:
  iDecodeVOLHeader

DESCRIPTION:
  This function decodes the VOL (Visual Object Layer) header
  (ISO/IEC 14496-2:1999/Amd.1:2000(E), section 6.3.3)

INPUT/OUTPUT PARAMETERS:
  psBits - pointer to input stream of bits
  psVOL  - pointer to structure containing VOL information required
           by the decoder

RETURN VALUE:
  Error code

SIDE EFFECTS:
  None.

===========================================================================*/
   bool parseHeader(mp4StreamType * psBits);

/* <EJECT> */

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
   bool parseSparkHeader(mp4StreamType * psBits);

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
   static uint32 read_bit_field(posInfoType * posPtr, uint32 size);
/*===========================================================================
FUNCTION:
  MP4_Utils::parse_frames_in_chunk

DESCRIPTION:
  Calculates number of valid frames present in the chunk based on frame header
  and set the timestamp interval based on the previous timestamp interval

INPUT/OUTPUT PARAMETERS:
  const uint8* pBitstream [IN]
  uint32 size [IN]
  int64 timestamp_interval [IN]
  mp4_frame_info_type *frame_info [OUT]

RETURN VALUE:
  number of VOPs in chunk

SIDE EFFECTS:
  noOfVopsInSameChunk is modified with number of frames in the chunk.
===========================================================================*/
uint32 parse_frames_in_chunk(const uint8* pBitstream,
                             uint32 size,
                             int64 timestamp_interval,
                             mp4_frame_info_type *frame_info);

};
#endif /*  MP4_UTILS_H */
