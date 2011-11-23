/*--------------------------------------------------------------------------
Copyright (c) 2010, Code Aurora Forum. All rights reserved

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

/* Contains wrappers for Vdec H264 Utils & possibly additional AST utility functions */

#ifdef TARGET_ARCH_8K
  #include "H264_Utils.h"
#else
  #include "OMX_Core.h"
#endif

/* For some reason, Android cannot compile the testapp if the files are C code (on LE it builds fine) hence make everything into cpp code
   LE had problems compiling testpp as CPP code hence it is kept as C code on LE for now & CPP on Android */

#ifdef TARGET_ARCH_8K
  static H264_Utils *gObj_h264_utils = NULL;
#endif 

void
TEST_UTILS_H264_Init(void)
{
/* For non-8K arch, these functions should just be stubs since H264_Utils isn't supported in the decoder code */
#ifdef TARGET_ARCH_8K
    if(NULL == gObj_h264_utils)
        gObj_h264_utils = new H264_Utils();
#else
    return;
#endif
}

void 
TEST_UTILS_H264_DeInit(void)
{
#ifdef TARGET_ARCH_8K
    if(gObj_h264_utils) 
        delete(gObj_h264_utils);
#else
    return;
#endif
}

int 
TEST_UTILS_H264_AllocateRBSPBuffer(unsigned int inbuf_size)
{
#ifdef TARGET_ARCH_8K
    if(gObj_h264_utils) {
		gObj_h264_utils->allocate_rbsp_buffer(inbuf_size);
		return 0;
	}
	else return -1;
#else
    return 0;
#endif
}

int 
TEST_UTILS_H264_IsNewFrame(OMX_IN OMX_U8 *buffer, 
                           OMX_IN OMX_U32 buffer_length, 
                           OMX_IN OMX_U32 size_of_nal_length_field)
{
    int ret = 1;
    bool gotret = false,isforceToStich=false;

#ifdef TARGET_ARCH_8K
    OMX_OUT OMX_BOOL isnew;
    if(gObj_h264_utils) {
        gotret = gObj_h264_utils->isNewFrame(buffer, buffer_length, size_of_nal_length_field, isnew,isforceToStich);
        if(OMX_FALSE == isnew || false == gotret)
            ret = 0;
        //fprintf(stdout, "\n[Msg xxxx] : gObj_h264_utils->isNewFrame() : %s ...", true == ret ? "TRUE" : "FALSE");
        return ((int)ret);
    }
    else
        return -1;
#else
    return 1;
#endif
}

