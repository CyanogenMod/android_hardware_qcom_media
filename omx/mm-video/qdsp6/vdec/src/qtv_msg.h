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

#ifndef QTV_MSG_H
#define QTV_MSG_H

#include <string.h>
#include <stdio.h>

#define LOG_NDEBUG 1
/* LOG_NDEBUG is log no debug
 * If need to debug, please put #define LOG_NDEBUG 0 at the beginning line of .c/.cpp file that you are working on
 * before #include <utils/Log.h>
 *
 * To enable all LOGV at all files, put #define LOG_NDEBUG 0 at this file (qtv_msg.h)
 *
 * #define LOG_NDEBUG 1 will disable LOGV
 * #define LOG_NDEBUG 0 will enable LOGV
 */
//#define LOG_NDEBUG 0

#ifdef _ANDROID_
#ifndef LOG_TAG
#define LOG_TAG "QCvdec"
#include <utils/Log.h>
#endif
#endif

#if  1
/* Usage of QTV_MSG, QTV_MSG_PRIO, etc
 * QTV_MSG(<QTV Message Sub-Group Id>, <message>)
 * QTV_MSG_PRIOx(<QTV Message Sub-Group Id>, <QTV Message Priorities>, <message>, args)
 */

/* QTV Message Sub-Group Ids */
#define QTVDIAG_GENERAL           0
#define QTVDIAG_DEBUG             1
#define QTVDIAG_STATISTICS        2
#define QTVDIAG_UI_TASK           3
#define QTVDIAG_MP4_PLAYER        4
#define QTVDIAG_AUDIO_TASK        5
#define QTVDIAG_VIDEO_TASK        6
#define QTVDIAG_STREAMING         7
#define QTVDIAG_MPEG4_TASK        8
#define QTVDIAG_FILE_OPS          9
#define QTVDIAG_RTP               10
#define QTVDIAG_RTCP              11
#define QTVDIAG_RTSP              12
#define QTVDIAG_SDP_PARSE         13
#define QTVDIAG_ATOM_PARSE        14
#define QTVDIAG_TEXT_TASK         15
#define QTVDIAG_DEC_DSP_IF        16
#define QTVDIAG_STREAM_RECORDING  17
#define QTVDIAG_CONFIGURATION     18
#define QTVDIAG_BCAST_FLO         19
#define QTVDIAG_GENERIC_BCAST     20

/* QTV Message Priorities */
#define QTVDIAG_PRIO_LOW
#define QTVDIAG_PRIO_MED
#define QTVDIAG_PRIO_HIGH
#define QTVDIAG_PRIO_ERROR
#define QTVDIAG_PRIO_FATAL
#define QTVDIAG_PRIO_DEBUG
#endif

#if (defined _ANDROID_) || (LOG_NDEBUG==1)
#define QTV_MSG_PRIO(a,b,c)                   LOG_##b(c)
#define QTV_MSG_PRIO1(a,b,c,d)                LOG_##b((c),(d))
#define QTV_MSG_PRIO2(a,b,c,d,e)              LOG_##b((c),(d),(e))
#define QTV_MSG_PRIO3(a,b,c,d,e,f)            LOG_##b((c),(d),(e),(f))
#define QTV_MSG_PRIO4(a,b,c,d,e,f,g)          LOG_##b((c),(d),(e),(f),(g))
#define QTV_MSG_PRIO5(a,b,c,d,e,f,g,h)        LOG_##b((c),(d),(e),(f),(g),(h))
#define QTV_MSG_PRIO6(a,b,c,d,e,f,g,h,i)      LOG_##b((c),(d),(e),(f),(g),(h),(i))
#define QTV_MSG_PRIO7(a,b,c,d,e,f,g,h,i,j)    LOG_##b((c),(d),(e),(f),(g),(h),(i),(j))
#define QTV_MSG_PRIO8(a,b,c,d,e,f,g,h,i,j,k)  LOG_##b((c),(d),(e),(f),(g),(h),(i),(j),(k))
#else
#define QTV_MSG_PRIO(a,b,c)                   {LOG_##b(c); printf("\n");}
#define QTV_MSG_PRIO1(a,b,c,d)                {LOG_##b((c),(d)); printf("\n");}
#define QTV_MSG_PRIO2(a,b,c,d,e)              {LOG_##b((c),(d),(e)); printf("\n");}
#define QTV_MSG_PRIO3(a,b,c,d,e,f)            {LOG_##b((c),(d),(e),(f)); printf("\n");}
#define QTV_MSG_PRIO4(a,b,c,d,e,f,g)          {LOG_##b((c),(d),(e),(f),(g)); printf("\n");}
#define QTV_MSG_PRIO5(a,b,c,d,e,f,g,h)        {LOG_##b((c),(d),(e),(f),(g),(h)); printf("\n");}
#define QTV_MSG_PRIO6(a,b,c,d,e,f,g,h,i)      {LOG_##b((c),(d),(e),(f),(g),(h),(i)); printf("\n");}
#define QTV_MSG_PRIO7(a,b,c,d,e,f,g,h,i,j)    {LOG_##b((c),(d),(e),(f),(g),(h),(i),(j)); printf("\n");}
#define QTV_MSG_PRIO8(a,b,c,d,e,f,g,h,i,j,k)  {LOG_##b((c),(d),(e),(f),(g),(h),(i),(j),(k)); printf("\n");}
#endif

#if (defined _ANDROID_)
#define QTV_MSG_SPRINTF(a,b)            LOGE(b)
#define QTV_MSG_SPRINTF_1(a,b,c)        LOGE((b),(c))
#define QTV_MSG_SPRINTF_2(a,b,c,d)      LOGE((b),(c),(d))
#define QTV_MSG_SPRINTF_3(a,b,c,d,e)    LOGE((b),(c),(d),(e))
#define QTV_MSG_SPRINTF_4(a,b,c,d,e,f)  LOGE((b),(c),(d),(e),(f))
#else
#define QTV_MSG_SPRINTF(a,b)            {LOGE(b); printf("\n");}
#define QTV_MSG_SPRINTF_1(a,b,c)        {LOGE((b),(c)); printf("\n");}
#define QTV_MSG_SPRINTF_2(a,b,c,d)      {LOGE((b),(c),(d)); printf("\n");}
#define QTV_MSG_SPRINTF_3(a,b,c,d,e)    {LOGE((b),(c),(d),(e)); printf("\n");}
#define QTV_MSG_SPRINTF_4(a,b,c,d,e,f)  {LOGE((b),(c),(d),(e),(f)); printf("\n");}
#endif

#if (defined _ANDROID_)  || (LOG_NDEBUG==1)
#define QTV_MSG(a,b)           LOGV(b)
#define QTV_MSG1(a,b,c)        LOGV((b),(c))
#define QTV_MSG2(a,b,c,d)      LOGV((b),(c),(d))
#define QTV_MSG3(a,b,c,d,e)    LOGV((b),(c),(d),(e))
#define QTV_MSG4(a,b,c,d,e,f)  LOGV((b),(c),(d),(e),(f))
#else
#define QTV_MSG(a,b)           {LOGV(b); printf("\n");}
#define QTV_MSG1(a,b,c)        {LOGV((b),(c)); printf("\n");}
#define QTV_MSG2(a,b,c,d)      {LOGV((b),(c),(d)); printf("\n");}
#define QTV_MSG3(a,b,c,d,e)    {LOGV((b),(c),(d),(e)); printf("\n");}
#define QTV_MSG4(a,b,c,d,e,f)  {LOGV((b),(c),(d),(e),(f)); printf("\n");}
#endif

#define LOG_QTVDIAG_PRIO_FATAL                LOGE
#define LOG_QTVDIAG_PRIO_ERROR                LOGW
#define LOG_QTVDIAG_PRIO_HIGH                 LOGV
#define LOG_QTVDIAG_PRIO_MED                  LOGV
#define LOG_QTVDIAG_PRIO_LOW                  LOGV
#define LOG_QTVDIAG_PRIO_DEBUG                LOGV

#ifdef PROFILE_DECODER
#define QTV_PERF(a)
#define QTV_PERF_MSG_PRIO(a,b,c)              LOGE((c))
#define QTV_PERF_MSG_PRIO1(a,b,c,d)           LOGE((c),(d))
#define QTV_PERF_MSG_PRIO2(a,b,c,d,e)         LOGE((c),(d),(e))
#define QTV_PERF_MSG_PRIO3(a,b,c,d,e,f)       LOGE((c),(d),(e),(f))
#else
#define QTV_PERF(a)
#define QTV_PERF_MSG_PRIO(a,b,c) //FARF(ALWAYS,(&F, c))
#define QTV_PERF_MSG_PRIO1(a,b,c,d) //FARF(ALWAYS,(&F, c,d))
#define QTV_PERF_MSG_PRIO2(a,b,c,d,e) //FARF(ALWAYS,(&F, c,d,e))
#define QTV_PERF_MSG_PRIO3(a,b,c,d,e,f) //FARF(ALWAYS,(&F, c,d,e,f))
#endif

#define ERR(a, b, c, d)  //FARF(ALWAYS,(&F, a, b, c, d))
#define ERR_FATAL(a, b, c, d) //FARF(ALWAYS, (&F, a, b, c, d))

#define MSG_ERROR(a, b)  //FARF(ALWAYS, (&F, a, b))
#define MSG_ERROR4(a, b, c, d) //FARF(ALWAYS, (&F, a, b, c, d))

//#define ASSERT(x) if (!(x)) {char *pp=0; LOGE("%s:%d *** ERROR ASSERT(0)\n", __FILE__, __LINE__); *pp=0;}
#if !defined(ASSERT)
#define ASSERT(x) if (!(x)) {LOGE("%s:%d *** ERROR ASSERT(0)\n", __FILE__, __LINE__);}
#endif

#ifndef _ANDROID_
#define LOGE printf
#define LOGW printf
#if (LOG_NDEBUG==1)
#define LOGV(...)
#else
#define LOGV printf
#endif
#endif // _ANDROID_

#endif // QTV_MSG_H
