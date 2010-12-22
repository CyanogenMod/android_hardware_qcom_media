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
#ifndef _QUTILITY_H_
#define _QUTILITY_H_

#ifdef __cplusplus
extern "C" {
#endif
#include <stdio.h>
#include "qtv_msg.h"

#ifndef T_WINNT
#include <sys/time.h>
#ifdef _ANDROID_
//#include "cutils/log.h"
#endif            //_ANDROID_
#else
#include <Windows.h>
   extern int gettimeofday(struct timeval *tv, void *tz);
#endif            //T_WINNT

//#define LOG_YUV_SLICES 0
//#define LOG_YUV_FRAMES 0

   typedef unsigned long long usecs_t;

#if 0
#define printp(x...) LOGV(x)

#ifdef PROFILE_DECODER
#define printx(x...) do {} while (0)
#else
#ifdef USE_LOGE_FOR_DECODER
#define printx(x...) LOGV(x)
#else
#define printx(x...) fprintf(stderr, x)
#endif
#endif
#else
#ifndef T_WINNT
#define printx(x...) do {} while (0)
#else
#define printx(...) do {} while (0)
#endif            //T_WINNT
#endif

   int fwritex(const void *ptr, int count, FILE * stream);

// To enable profiling on CBSP2 please add "CPPFLAGS += -DPROFILE_DECODER" in
// vdec-omxmp4.mk and vdec_omxh264.mk

#ifdef PROFILE_DECODER

#define QPERF_INIT(PREFIX) \
usecs_t PREFIX ## _total_time_us = 0;\
int PREFIX ## _n_running_timer = 0;\
struct timeval PREFIX ## _tv1, PREFIX ## _tv2;\
struct timezone PREFIX ## _tz1, PREFIX ## _tz2;\
int PREFIX ## _total_iterations = 0

#define QPERF_RESET(PREFIX) \
PREFIX ## _total_time_us = 0;\
PREFIX ## _total_iterations = 0

#define QPERF_START(PREFIX) \
   if (PREFIX ## _n_running_timer == 0) gettimeofday(&PREFIX ## _tv1, &PREFIX ## _tz1);\
   PREFIX ## _n_running_timer++

#define QPERF_END(PREFIX) \
   gettimeofday(&PREFIX ## _tv2, &PREFIX ## _tz2);\
   PREFIX ## _total_time_us += ((PREFIX ## _tv2.tv_sec - PREFIX ## _tv1.tv_sec) * 1000000 + (PREFIX ## _tv2.tv_usec - PREFIX ## _tv1.tv_usec));\
   PREFIX ## _total_iterations++;\
   PREFIX ## _n_running_timer--

#define QPERF_END_AND_START(PREFIX) \
   gettimeofday(&PREFIX ## _tv2, &PREFIX ## _tz2);\
   PREFIX ## _total_time_us += ((PREFIX ## _tv2.tv_sec - PREFIX ## _tv1.tv_sec) * 1000000 + (PREFIX ## _tv2.tv_usec - PREFIX ## _tv1.tv_usec));\
   PREFIX ## _total_iterations++;\
   PREFIX ## _tv1 = PREFIX ## _tv2;\
   PREFIX ## _tz1 = PREFIX ## _tz2;\
   PREFIX ## _n_running_timer--

#define QPERF_TIME(PREFIX, FN)\
  QPERF_START(PREFIX); \
  FN; \
  QPERF_END(PREFIX)

#define QPERF_SET_ITERATION(PREFIX, N)  \
  PREFIX ## _total_iterations = N

#define QPERF_TERMINATE(PREFIX) \
  QTV_PERF_MSG_PRIO1(QTVDIAG_STATISTICS, QTVDIAG_PRIO_DEBUG, "Statistic of %s\n", #PREFIX);\
  QTV_PERF_MSG_PRIO(QTVDIAG_STATISTICS, QTVDIAG_PRIO_DEBUG, "============================================================\n");\
  QTV_PERF_MSG_PRIO1(QTVDIAG_STATISTICS, QTVDIAG_PRIO_DEBUG, "Total number of iteration          %d\n", PREFIX ## _total_iterations);\
  QTV_PERF_MSG_PRIO2(QTVDIAG_STATISTICS, QTVDIAG_PRIO_DEBUG, "%s total_time(us):             %30lld\n",#PREFIX, (PREFIX ## _total_time_us));\
  if (0 != PREFIX ## _total_iterations) {QTV_PERF_MSG_PRIO2(QTVDIAG_STATISTICS, QTVDIAG_PRIO_DEBUG, "%s average_time/iteration(us): %30lld\n",#PREFIX, (PREFIX ## _total_time_us)/(PREFIX ## _total_iterations));}\
  if (0 != PREFIX ## _total_time_us) {QTV_PERF_MSG_PRIO2(QTVDIAG_STATISTICS, QTVDIAG_PRIO_DEBUG, "%s frame per second:           %.2f\n",#PREFIX, (double)(PREFIX ## _total_iterations)/(PREFIX ## _total_time_us)*1000000);}\
  QTV_PERF_MSG_PRIO(QTVDIAG_STATISTICS, QTVDIAG_PRIO_DEBUG, "============================================================\n");

#else
#define QPERF_INIT(PREFIX)
#define QPERF_RESET(PREFIX) do {} while (0)
#define QPERF_START(PREFIX) do {} while (0)
#define QPERF_END(PREFIX) do {} while (0)
#define QPERF_TIME(PREFIX, FN) FN
#define QPERF_END_AND_START(PREFIX) do {} while (0)
#define QPERF_SET_ITERATION(PREFIX, N) do {} while (0)
#define QPERF_TERMINATE(PREFIX) do {} while (0)
#endif

#ifdef __cplusplus
}
#endif
#endif            /* _QUTILITY_H_ */
