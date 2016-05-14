/*
 * Copyright (c) 2012-2015, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *      contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef QC_META_DATA_H_

#define QC_META_DATA_H_

namespace android {

enum {
    kKeyAacCodecSpecificData = 'nacc' , // for native aac files

#if 0
    kKeyRawCodecSpecificData = 'rcsd',  // raw data - added to support mmParser
    kKeyDivXVersion          = 'DivX',  // int32_t
    kKeyDivXDrm              = 'QDrm',  // void *
    kKeyWMAEncodeOpt         = 'eopt',  // int32_t
    kKeyWMABlockAlign        = 'blka',  // int32_t
    kKeyWMAVersion           = 'wmav',  // int32_t
    kKeyWMAAdvEncOpt1        = 'ade1',  // int16_t
    kKeyWMAAdvEncOpt2        = 'ade2',  // int32_t
    kKeyWMAFormatTag         = 'fmtt',  // int64_t
    kKeyWMABitspersample     = 'bsps',  // int64_t
    kKeyWMAVirPktSize        = 'vpks',  // int64_t
#endif
    kKeyWMAChannelMask       = 'chmk',  // int32_t
    kKeyVorbisData           = 'vdat',  // raw data

    kKeyFileFormat           = 'ffmt',  // cstring

    kkeyAacFormatAdif        = 'adif',  // bool (int32_t)
    kKeyInterlace            = 'intL',  // bool (int32_t)
    kkeyAacFormatLtp         = 'ltp ',


    //DTS subtype
    kKeyDTSSubtype           = 'dtss',  //int32_t

    //Extractor sets this
    kKeyUseArbitraryMode     = 'ArbM',  //bool (int32_t)
    kKeySmoothStreaming      = 'ESmS',  //bool (int32_t)
    kKeyHFR                  = 'hfr ',  // int32_t
    kKeyHSR                  = 'hsr ',  // int32_t

    kKeySampleBits           = 'sbit', // int32_t (audio sample bit-width)
    kKeyPcmFormat            = 'pfmt', //int32_t (pcm format)
    kKeyMinBlkSize           = 'mibs', //int32_t
    kKeyMaxBlkSize           = 'mabs', //int32_t
    kKeyMinFrmSize           = 'mifs', //int32_t
    kKeyMaxFrmSize           = 'mafs', //int32_t
    kKeyMd5Sum               = 'md5s', //cstring

    kKeyBatchSize            = 'btch', //int32_t
    kKeyIsByteMode           = 'bytm', //int32_t
    kKeyUseSetBuffers        = 'setb', //bool (int32_t)
};

#if 0
enum {
    kTypeDivXVer_3_11,
    kTypeDivXVer_4,
    kTypeDivXVer_5,
    kTypeDivXVer_6,
};
enum {
    kTypeWMA,
    kTypeWMAPro,
    kTypeWMALossLess,
};
#endif

//This enum should be keep in sync with "enum Flags" in MediaExtractor.h in AOSP,
//Value should reflect as last entry in the enum
enum {
    CAN_SEEK_TO_ZERO   = 16, // the "previous button"
};

enum {
    USE_SET_BUFFERS = 0x1,
    USE_AUDIO_BIG_BUFFERS = 0x2,
};
}  // namespace android

#endif  // QC_META_DATA_H_
