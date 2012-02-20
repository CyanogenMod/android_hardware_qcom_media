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

#ifndef _VENC_DEBUG_H
#define _VENC_DEBUG_H

/*========================================================================

                     INCLUDE FILES FOR MODULE

==========================================================================*/
#include <stdio.h>
#include <string.h>

#define LOG_NDEBUG 0

#ifdef _ANDROID_LOG_
#ifndef LOG_TAG
#define LOG_TAG "VENC_ENC"
#include <utils/Log.h>
#endif
#include <utils/threads.h>
#include <sys/prctl.h>
#include <sys/resource.h>

#define VENC_MSG_HIGH(fmt, ...) LOGE("%s::%d "fmt"\n",__FUNCTION__, __LINE__,## __VA_ARGS__)
#define VENC_MSG_PROFILE(fmt, ...) LOGE("%s::%d "fmt"\n",__FUNCTION__, __LINE__,## __VA_ARGS__)
#define VENC_MSG_ERROR(fmt, ...) LOGE("VENC_ERROR %s::%d "fmt"\n",__FUNCTION__, __LINE__,## __VA_ARGS__)
#define VENC_MSG_FATAL(fmt, ...) LOGE("VENC_ERROR %s::%d "fmt"\n",__FUNCTION__, __LINE__,## __VA_ARGS__)

#ifdef _ANDROID_LOG_DEBUG
#define VENC_MSG_LOW(fmt, ...) LOGE("%s::%d "fmt"\n",__FUNCTION__, __LINE__,## __VA_ARGS__)
#define VENC_MSG_MEDIUM(fmt, ...) LOGE("%s::%d "fmt"\n",__FUNCTION__, __LINE__,## __VA_ARGS__)
#else
#define VENC_MSG_LOW(fmt, ...)
#define VENC_MSG_MEDIUM(fmt, ...)
#endif

#else

#define VENC_MSG_HIGH(fmt, ...) fprintf(stderr, "VENC_HIGH %s::%d "fmt"\n",__FUNCTION__, __LINE__,## __VA_ARGS__)
#define VENC_MSG_PROFILE(fmt, ...) fprintf(stderr, "VENC_PROFILE %s::%d "fmt"\n",__FUNCTION__, __LINE__,## __VA_ARGS__)
#define VENC_MSG_ERROR(fmt, ...) fprintf(stderr, "VENC_ERROR %s::%d "fmt"\n",__FUNCTION__, __LINE__,## __VA_ARGS__)
#define VENC_MSG_FATAL(fmt, ...) fprintf(stderr, "VENC_ERROR %s::%d "fmt"\n",__FUNCTION__, __LINE__,## __VA_ARGS__)

#ifdef  VENC_MSG_LOG_DEBUG
#define VENC_MSG_LOW(fmt, ...) fprintf(stderr, "VENC_LOW %s::%d "fmt"\n",__FUNCTION__, __LINE__,## __VA_ARGS__)
#define VENC_MSG_MEDIUM(fmt, ...) fprintf(stderr, "VENC_MEDIUM %s::%d "fmt"\n",__FUNCTION__, __LINE__,## __VA_ARGS__)
#else
#define VENC_MSG_LOW(fmt, ...)
#define VENC_MSG_MEDIUM(fmt, ...)
#endif

/*
#define QC_OMX_MSG_LOW(fmt, a, b, c)      printf("QC_OMX (LOW): (%s::%d) "fmt"\n", __FUNCTION__, __LINE__, (a), (b), (c))
#define QC_OMX_MSG_MEDIUM(fmt, a, b, c)   printf("QC_OMX (MEDIUM): (%s::%d) "fmt"\n", __FUNCTION__, __LINE__, (a), (b), (c))
#define QC_OMX_MSG_HIGH(fmt, a, b, c)     printf("QC_OMX (HIGH): (%s::%d) "fmt"\n", __FUNCTION__, __LINE__, (a), (b), (c))
#define QC_OMX_MSG_ERROR(fmt, a, b, c)    printf("QC_OMX (ERROR): (%s::%d) "fmt"\n", __FUNCTION__, __LINE__, (a), (b), (c))
#define QC_OMX_MSG_FATAL(fmt, a, b, c)    printf("QC_OMX (FATAL): (%s::%d) "fmt"\n", __FUNCTION__, __LINE__, (a), (b), (c))
#define QC_OMX_MSG_PROFILE(fmt, a, b, c)  printf("QC_OMX (PROFILE): (%s::%d) "fmt"\n", __FUNCTION__, __LINE__, (a), (b), (c))
*/

#endif //#ifdef _ANDROID_LOG_

#define QC_OMX_MSG_LOW      VENC_MSG_LOW
#define QC_OMX_MSG_MEDIUM    VENC_MSG_MEDIUM
#define QC_OMX_MSG_HIGH      VENC_MSG_HIGH
#define QC_OMX_MSG_ERROR    VENC_MSG_ERROR
#define QC_OMX_MSG_FATAL    VENC_MSG_FATAL
#define QC_OMX_MSG_PROFILE     VENC_MSG_PROFILE

#endif // #ifndef _VENC_DEBUG_H
