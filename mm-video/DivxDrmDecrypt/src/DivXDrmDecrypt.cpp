/*--------------------------------------------------------------------------
Copyright (c) 2010-2011, Code Aurora Forum. All rights reserved.

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

#include "DivXDrmDecrypt.h"
#include <dlfcn.h>  // for dlopen/dlclose

#define LOG_NDEBUG 0
#define LOG_TAG "DivXDrmDecrypt"
#include <utils/Log.h>

static const char* MM_PARSER_LIB = "libmmparser.so";

void* MmParserLib() {
    static void* mmParserLib = NULL;

    if( mmParserLib != NULL ) {
        return mmParserLib;
    }

    mmParserLib = ::dlopen(MM_PARSER_LIB, RTLD_NOW);

    if (mmParserLib == NULL) {
        LOGE("Failed to open MM_PARSER_LIB \n");
    }

    return mmParserLib;
}

DivXDrmDecryptFactory DrmDecryptFactoryFunction() {
    static DivXDrmDecryptFactory drmDecryptFactoryFunction = NULL;

    if( drmDecryptFactoryFunction != NULL ) {
        return drmDecryptFactoryFunction;
    }

    void *mmParserLib = MmParserLib();
    if (mmParserLib == NULL) {
        return NULL;
    }

    drmDecryptFactoryFunction = (DivXDrmDecryptFactory) dlsym(mmParserLib, MEDIA_CREATE_DIVX_DRM_DECRYPT);

    if( drmDecryptFactoryFunction == NULL ) {
        LOGE(" dlsym for DrmDecrypt factory function failed  \n");
    }

    return drmDecryptFactoryFunction;
}

DivXDrmDecrypt* DivXDrmDecrypt::Create( OMX_PTR drmHandle ) {
    DivXDrmDecryptFactory drmCreateFunc = DrmDecryptFactoryFunction();
    if( drmCreateFunc == NULL ) {
        return NULL;
    }

    DivXDrmDecrypt* decrypt = drmCreateFunc( drmHandle );
    if( decrypt == NULL ) {
        LOGE(" failed to instantiate DrmDecoder \n");
    }
    return decrypt;
}

