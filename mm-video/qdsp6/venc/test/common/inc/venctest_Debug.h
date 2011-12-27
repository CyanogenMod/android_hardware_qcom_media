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

#ifndef _VENC_TEST_DEBUG_H
#define _VENC_TEST_DEBUG_H

/*========================================================================

                     INCLUDE FILES FOR MODULE

==========================================================================*/
#include <stdio.h>
#include <string.h>

#define LOG_NDEBUG 0

#ifdef _ANDROID_LOG_
#ifndef LOG_TAG
#define LOG_TAG "VENC_TEST"
#include <utils/Log.h>
#endif

#define VENC_TEST_MSG_HIGH(fmt, ...) LOGE("%s::%d "fmt"\n",__FUNCTION__, __LINE__,## __VA_ARGS__)
#define VENC_TEST_MSG_PROFILE(fmt, ...) LOGE("%s::%d "fmt"\n",__FUNCTION__, __LINE__,## __VA_ARGS__)
#define VENC_TEST_MSG_ERROR(fmt, ...) LOGE("VENC_ERROR %s::%d "fmt"\n",__FUNCTION__, __LINE__, ## __VA_ARGS__)
#define VENC_TEST_MSG_FATAL(fmt, ...) LOGE("VENC_ERROR %s::%d "fmt"\n",__FUNCTION__, __LINE__,## __VA_ARGS__)

#ifdef _ANDROID_LOG_DEBUG
#define VENC_TEST_MSG_LOW(fmt, ...) LOGE("%s::%d "fmt"\n",__FUNCTION__, __LINE__,## __VA_ARGS__)
#define VENC_TEST_MSG_MEDIUM(fmt, ...) LOGE("%s::%d "fmt"\n",__FUNCTION__, __LINE__,## __VA_ARGS__)
#else
#define VENC_TEST_MSG_LOW(fmt, ...)
#define VENC_TEST_MSG_MEDIUM(fmt, ...)
#endif

#else

#define VENC_TEST_REMOVE_SLASH(x) strrchr(x, '/') != NULL ? strrchr(x, '/') + 1 : x

#define VENC_TEST_MSG_HIGH(fmt, ...) fprintf(stderr, "VENC_TEST_HIGH %s::%d "fmt"\n",__FUNCTION__, __LINE__,## __VA_ARGS__)
#define VENC_TEST_MSG_PROFILE(fmt, ...) fprintf(stderr, "VENC_TEST_PROFILE %s::%d "fmt"\n",__FUNCTION__, __LINE__,## __VA_ARGS__)
#define VENC_TEST_MSG_ERROR(fmt, ...) fprintf(stderr, "VENC_TEST_ERROR %s::%d "fmt"\n",__FUNCTION__, __LINE__,## __VA_ARGS__)
#define VENC_TEST_MSG_FATAL(fmt, ...) fprintf(stderr, "VENC_TEST_ERROR %s::%d "fmt"\n",__FUNCTION__, __LINE__,## __VA_ARGS__)

#ifdef  VENC_MSG_LOG_DEBUG
#define VENC_TEST_MSG_LOW(fmt, ...) fprintf(stderr, "VENC_TEST_LOW %s::%d "fmt"\n",__FUNCTION__, __LINE__,## __VA_ARGS__)
#define VENC_TEST_MSG_MEDIUM(fmt, ...) fprintf(stderr, "VENC_TEST_MEDIUM %s::%d "fmt"\n",__FUNCTION__, __LINE__,## __VA_ARGS__)
#else
#define VENC_TEST_MSG_LOW(fmt, ...)
#define VENC_TEST_MSG_MEDIUM(fmt, ...)
#endif

#endif //#ifdef _ANDROID_LOG_

#endif // #ifndef _VENC_TEST_DEBUG_H
