/*
 * Copyright (c) 2010 The Khronos Group Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject
 * to the following conditions:
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

/** OMX_VideoExt.h - OpenMax IL version 1.1.2
 * The OMX_VideoExt header file contains extensions to the
 * definitions used by both the application and the component to
 * access video items.
 */

#ifndef OMX_VideoExt_h
#define OMX_VideoExt_h

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Each OMX header shall include all required header files to allow the
 * header to compile without errors.  The includes below are required
 * for this header file to compile successfully
 */
#include <OMX_Core.h>
#include <OMX_Video.h>

/** NALU Formats */
typedef enum OMX_NALUFORMATSTYPE {
    OMX_NaluFormatStartCodes = 1,
    OMX_NaluFormatOneNaluPerBuffer = 2,
    OMX_NaluFormatOneByteInterleaveLength = 4,
    OMX_NaluFormatTwoByteInterleaveLength = 8,
    OMX_NaluFormatFourByteInterleaveLength = 16,
    OMX_NaluFormatCodingMax = 0x7FFFFFFF
} OMX_NALUFORMATSTYPE;

/** NAL Stream Format */
typedef struct OMX_NALSTREAMFORMATTYPE{
    OMX_U32 nSize;
    OMX_VERSIONTYPE nVersion;
    OMX_U32 nPortIndex;
    OMX_NALUFORMATSTYPE eNaluFormat;
} OMX_NALSTREAMFORMATTYPE;

/** Enum for standard video codingtype extensions */
typedef enum OMX_VIDEO_CODINGEXTTYPE {
    OMX_VIDEO_ExtCodingUnused = OMX_VIDEO_CodingKhronosExtensions,
    OMX_VIDEO_CodingVP8,        /**< VP8/WebM */
} OMX_VIDEO_CODINGEXTTYPE;

/** VP8 profiles */
typedef enum OMX_VIDEO_VP8PROFILETYPE {
    OMX_VIDEO_VP8ProfileMain = 0x01,
    OMX_VIDEO_VP8ProfileUnknown = 0x6EFFFFFF,
    OMX_VIDEO_VP8ProfileMax = 0x7FFFFFFF
} OMX_VIDEO_VP8PROFILETYPE;

/** VP8 levels */
typedef enum OMX_VIDEO_VP8LEVELTYPE {
    OMX_VIDEO_VP8Level_Version0 = 0x01,
    OMX_VIDEO_VP8Level_Version1 = 0x02,
    OMX_VIDEO_VP8Level_Version2 = 0x04,
    OMX_VIDEO_VP8Level_Version3 = 0x08,
    OMX_VIDEO_VP8LevelUnknown = 0x6EFFFFFF,
    OMX_VIDEO_VP8LevelMax = 0x7FFFFFFF
} OMX_VIDEO_VP8LEVELTYPE;

/** VP8 Param */
typedef struct OMX_VIDEO_PARAM_VP8TYPE {
    OMX_U32 nSize;
    OMX_VERSIONTYPE nVersion;
    OMX_U32 nPortIndex;
    OMX_VIDEO_VP8PROFILETYPE eProfile;
    OMX_VIDEO_VP8LEVELTYPE eLevel;
    OMX_U32 nDCTPartitions;
    OMX_BOOL bErrorResilientMode;
} OMX_VIDEO_PARAM_VP8TYPE;

typedef enum OMX_VIDEO_HEVCPROFILETYPE {
    OMX_VIDEO_HEVCProfileMain = 0x01,
    OMX_VIDEO_HEVCProfileMain10 = 0x02,
    OMX_VIDEO_HEVCProfileUnknown = 0x6EFFFFFF,
    OMX_VIDEO_HEVCrofileMax = 0x7FFFFFFF
} OMX_VIDEO_HEVCPROFILETYPE;

/** HEVC levels */
typedef enum OMX_VIDEO_HEVCLEVELTYPE {
    OMX_VIDEO_HEVCLevel_Version0  = 0x01,
    OMX_VIDEO_HEVCMainTierLevel1  = 0x02,
    OMX_VIDEO_HEVCHighTierLevel1  = 0x03,
    OMX_VIDEO_HEVCMainTierLevel2  = 0x04,
    OMX_VIDEO_HEVCHighTierLevel2  = 0x05,
    OMX_VIDEO_HEVCMainTierLevel21 = 0x06,
    OMX_VIDEO_HEVCHighTierLevel21 = 0x07,
    OMX_VIDEO_HEVCMainTierLevel3  = 0x08,
    OMX_VIDEO_HEVCHighTierLevel3  = 0x09,
    OMX_VIDEO_HEVCMainTierLevel31 = 0x0A,
    OMX_VIDEO_HEVCHighTierLevel31 = 0x0B,
    OMX_VIDEO_HEVCMainTierLevel4  = 0x0C,
    OMX_VIDEO_HEVCHighTierLevel4  = 0x0D,
    OMX_VIDEO_HEVCMainTierLevel41 = 0x0E,
    OMX_VIDEO_HEVCHighTierLevel41 = 0x0F,
    OMX_VIDEO_HEVCMainTierLevel5  = 0x10,
    OMX_VIDEO_HEVCHighTierLevel5  = 0x11,
    OMX_VIDEO_HEVCMainTierLevel51 = 0x12,
    OMX_VIDEO_HEVCHighTierLevel51 = 0x13,
    OMX_VIDEO_HEVCMainTierLevel52 = 0x14,
    OMX_VIDEO_HEVCHighTierLevel52 = 0x15,
    OMX_VIDEO_HEVCMainTierLevel6  = 0x16,
    OMX_VIDEO_HEVCHighTierLevel6  = 0x17,
    OMX_VIDEO_HEVCMainTierLevel61 = 0x18,
    OMX_VIDEO_HEVCHighTierLevel61 = 0x19,
    OMX_VIDEO_HEVCMainTierLevel62 = 0x1A,
    OMX_VIDEO_HEVCLevelUnknown = 0x6EFFFFFF,
    OMX_VIDEO_HEVCLevelMax = 0x7FFFFFFF
} OMX_VIDEO_HEVCLEVELTYPE;

/** HEVC Param */
typedef struct OMX_VIDEO_PARAM_HEVCTYPE {
    OMX_U32 nSize;
    OMX_VERSIONTYPE nVersion;
    OMX_U32 nPortIndex;
    OMX_VIDEO_HEVCPROFILETYPE eProfile;
    OMX_VIDEO_HEVCLEVELTYPE eLevel;
} OMX_VIDEO_PARAM_HEVCTYPE;




/** Structure for configuring VP8 reference frames */
typedef struct OMX_VIDEO_VP8REFERENCEFRAMETYPE {
    OMX_U32 nSize;
    OMX_VERSIONTYPE nVersion;
    OMX_U32 nPortIndex;
    OMX_BOOL bPreviousFrameRefresh;
    OMX_BOOL bGoldenFrameRefresh;
    OMX_BOOL bAlternateFrameRefresh;
    OMX_BOOL bUsePreviousFrame;
    OMX_BOOL bUseGoldenFrame;
    OMX_BOOL bUseAlternateFrame;
} OMX_VIDEO_VP8REFERENCEFRAMETYPE;

/** Structure for querying VP8 reference frame type */
typedef struct OMX_VIDEO_VP8REFERENCEFRAMEINFOTYPE {
    OMX_U32 nSize;
    OMX_VERSIONTYPE nVersion;
    OMX_U32 nPortIndex;
    OMX_BOOL bIsIntraFrame;
    OMX_BOOL bIsGoldenOrAlternateFrame;
} OMX_VIDEO_VP8REFERENCEFRAMEINFOTYPE;


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* OMX_VideoExt_h */
/* File EOF */
