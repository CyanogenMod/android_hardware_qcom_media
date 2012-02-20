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
/*============================================================================
                            O p e n M A X   w r a p p e r s
                             O p e n  M A X   C o r e

 This module contains the registry table for the QCOM's OpenMAX core.

*//*========================================================================*/

#include "qc_omx_core.h"

omx_core_cb_type core[] =
{
  {
    "OMX.qcom.video.decoder.avc",
    NULL,   // Create instance function
    // Unique instance handle
    {
      NULL,
      NULL,
      NULL,
      NULL
    },
    NULL,   // Shared object library handle
    #ifdef _ANDROID_
    "libOmxH264Dec.so",
    #else
    "libmm-vdec-omxh264.so.1",
    #endif
    {
      "video_decoder.avc"
    }
  },
  {
    "OMX.qcom.video.decoder.mpeg4",
    NULL,   // Create instance function
    // Unique instance handle
    {
      NULL,
      NULL,
      NULL,
      NULL
    },
    NULL,   // Shared object library handle
    #ifdef _ANDROID_
    "libOmxMpeg4Dec.so",
    #else
    "libmm-vdec-omxmp4.so.1",
    #endif
    {
      "video_decoder.mpeg4"
    }
  },
  {
    "OMX.qcom.video.decoder.vc1",
    NULL,   // Create instance function
    // Unique instance handle
    {
      NULL,
      NULL,
      NULL,
      NULL
    },
    NULL,   // Shared object library handle
    #ifdef _ANDROID_
    "libOmxWmvDec.so",
    #else
    "libmm-vdec-omxwmv.so.1",
    #endif
    {
      "video_decoder.vc1"
    }
  },
  {
    "OMX.qcom.video.decoder.h263",
    NULL,   // Create instance function
    // Unique instance handle
    {
      NULL,
      NULL,
      NULL,
      NULL
    },
    NULL,   // Shared object library handle
    #ifdef _ANDROID_
    "libOmxMpeg4Dec.so",
    #else
    "libmm-vdec-omxmp4.so.1",
    #endif
    {
      "video_decoder.h263"
    }
  },
   {
    "OMX.qcom.video.encoder.mpeg4",
    NULL,   // Create instance function
    // Unique instance handle
    {
      NULL,
      NULL,
      NULL,
      NULL
    },
    NULL,   // Shared object library handle
    #ifdef _ANDROID_
    "libOmxVidEnc.so",
    #else
    "libmm-venc-omx.so.1",
    #endif
    {
      "video_encoder.mpeg4"
    }
  },
   {
    "OMX.qcom.video.encoder.h263",
    NULL,   // Create instance function
    // Unique instance handle
    {
      NULL,
      NULL,
      NULL,
      NULL
    },
    NULL,   // Shared object library handle
    #ifdef _ANDROID_
    "libOmxVidEnc.so",
    #else
    "libmm-venc-omx.so.1",
    #endif
    {
      "video_encoder.h263",
    }
  },
  {
    "OMX.qcom.audio.decoder.mp3",
    NULL,   // Create instance function
    // Unique instance handle
    {
      NULL,
      NULL,
      NULL,
      NULL
    },
    NULL,   // Shared object library handle
    #ifdef _ANDROID_
    "libOmxMp3Dec.so",
    #else
    "libmm-adec-omxmp3.so.1",
    #endif
    {
      "audio_decoder.mp3"
    }
  },
  {
    "OMX.qcom.audio.decoder.aac",
    NULL,   // Create instance function
    // Unique instance handle
    {
      NULL,
      NULL,
      NULL,
      NULL
    },
    NULL,   // Shared object library handle
    #ifdef _ANDROID_
    "libOmxAacDec.so",
    #else
    "libmm-adec-omxaac.so.1",
    #endif
    {
      "audio_decoder.aac"
    }
  },
  {
    "OMX.qcom.audio.decoder.tunneled.mp3",
    NULL,   // Create instance function
    // Unique instance handle
    {
      NULL,
      NULL,
      NULL,
      NULL
    },
    NULL,   // Shared object library handle
    #ifdef _ANDROID_
    "libOmxMp3Dec.so",
    #else
    "libmm-adec-omxmp3.so.1",
    #endif
    {
      "audio_decoder.mp3"
    }
  },
  {
    "OMX.qcom.audio.decoder.tunneled.aac",
    NULL,   // Create instance function
    // Unique instance handle
    {
      NULL,
      NULL,
      NULL,
      NULL
    },
    NULL,   // Shared object library handle
    #ifdef _ANDROID_
    "libOmxAacDec.so",
    #else
    "libmm-adec-omxaac.so.1",
    #endif
    {
      "audio_decoder.aac"
    }
  },
  {
    "OMX.qcom.audio.decoder.amrnb",
    NULL,   // Create instance function
    // Unique instance handle
    {
      NULL,
      NULL,
      NULL,
      NULL
    },
    NULL,   // Shared object library handle
    #ifdef _ANDROID_
    "libOmxAmrDec.so",
    #else
    "libmm-adec-omxamr.so.1",
    #endif
    {
      "audio_decoder.amrnb"
    }
  },
  {
    "OMX.qcom.audio.decoder.tunneled.amrnb",
    NULL,   // Create instance function
    // Unique instance handle
    {
      NULL,
      NULL,
      NULL,
      NULL
    },
    NULL,   // Shared object library handle
    #ifdef _ANDROID_
    "libOmxAmrDec.so",
    #else
    "libmm-adec-omxamr.so.1",
    #endif
    {
      "audio_decoder.amrnb"
    }
  },
  {
    "OMX.qcom.audio.encoder.tunneled.aac",
    NULL,   // Create instance function
    // Unique instance handle
    {
      NULL,
      NULL,
      NULL,
      NULL
    },
    NULL,   // Shared object library handle
    #ifdef _ANDROID_
    "libOmxAacEnc.so",
    #else
    "libmm-aenc-omxaac.so.1",
    #endif
    {
      "audio_encoder.aac"
    }
  },
  {
    "OMX.qcom.audio.decoder.Qcelp13Hw",
    NULL,   // Create instance function
    // Unique instance handle
    {
      NULL,
      NULL,
      NULL,
      NULL
    },
    NULL,   // Shared object library handle
    #ifdef _ANDROID_
    "libOmxQcelpHwDec.so",
    #else
    "libmm-adec-omxQcelp13.so.1",
    #endif
    {
      "audio_decoder.Qcelp13"
    }
  },
  {
    "OMX.qcom.audio.decoder.tunneled.Qcelp13",
    NULL,   // Create instance function
    // Unique instance handle
    {
      NULL,
      NULL,
      NULL,
      NULL
    },
    NULL,   // Shared object library handle
    #ifdef _ANDROID_
    "libOmxQcelpHwDec.so",
    #else
    "libmm-adec-omxQcelp13.so.1",
    #endif
    {
      "audio_decoder.Qcelp13"
    }
  },
  {
    "OMX.qcom.audio.encoder.tunneled.amr",
    NULL,   // Create instance function
    // Unique instance handle
    {
      NULL,
      NULL,
      NULL,
      NULL
    },
    NULL,   // Shared object library handle
    #ifdef _ANDROID_
    "libOmxAmrEnc.so",
    #else
    "libmm-aenc-omxamr.so.1",
    #endif
    {
      "audio_encoder.amr"
    }
  }
};

const unsigned int SIZE_OF_CORE = sizeof(core) / sizeof(omx_core_cb_type);


