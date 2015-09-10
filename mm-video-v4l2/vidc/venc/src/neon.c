/*--------------------------------------------------------------------------
Copyright (c) 2015, The Linux Foundation. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of The Linux Foundation nor
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

#include <arm_neon.h>

void neon_clip_luma_chroma(unsigned char *luma,
    unsigned char *chroma, unsigned int lv, unsigned int cv,
    unsigned int width, unsigned int height)
{
    uint8x16_t PixRow_8x16_1, PixRow_8x16_2, lvv_8x16, cvv_8x16;
    unsigned int loop_luma   = width * height;
    unsigned int loop_chroma = loop_luma/2;
    unsigned char *luma_1, *luma_2;
    unsigned char *chroma_1, *chroma_2;
    unsigned int loop_luma_1 = (loop_luma >> 1)<<1;
    unsigned int loop_luma_2 = loop_luma & 1;
    unsigned int loop_chroma_1 = (loop_chroma >> 1)<<1;
    unsigned int loop_chroma_2 = loop_chroma & 1;

    if (width & 0x1F || height & 0x1F)
        return;

    lvv_8x16 = vdupq_n_u8((unsigned char)lv);
    cvv_8x16 = vdupq_n_u8((unsigned char)cv);
    luma_1 = luma;
    chroma_1 = chroma;

    while (loop_luma_1)
    {
        PixRow_8x16_1 = vld1q_u8(luma_1);
        luma_2 = luma_1 + 16;
        PixRow_8x16_2 = vld1q_u8(luma_2);
        PixRow_8x16_1 = vminq_u8(PixRow_8x16_1, lvv_8x16);
        PixRow_8x16_2 = vminq_u8(PixRow_8x16_2, lvv_8x16);
        vst1q_u8(luma_1, PixRow_8x16_1);
        vst1q_u8(luma_2, PixRow_8x16_2);
        luma_1 = luma_1 + 32;
        loop_luma_1 = loop_luma_1 - 32;
    }

    if (loop_luma_2)
    {
        PixRow_8x16_1 = vld1q_u8(luma_1);
        PixRow_8x16_1 = vminq_u8(PixRow_8x16_1, lvv_8x16);
        vst1q_u8(luma_1, PixRow_8x16_1);
    }

    while (loop_chroma_1)
    {
        PixRow_8x16_1 = vld1q_u8(chroma_1);
        chroma_2 = chroma_1 + 16;
        PixRow_8x16_2 = vld1q_u8(chroma_2);
        PixRow_8x16_1 = vminq_u8(PixRow_8x16_1, cvv_8x16);
        PixRow_8x16_2 = vminq_u8(PixRow_8x16_2, cvv_8x16);
        vst1q_u8(chroma_1, PixRow_8x16_1);
        vst1q_u8(chroma_2, PixRow_8x16_2);
        chroma_1 = chroma_1 + 32;
        loop_chroma_1 = loop_chroma_1 - 32;
    }

    if (loop_chroma_2)
    {
        PixRow_8x16_1 = vld1q_u8(chroma_1);
        PixRow_8x16_1 = vminq_u8(PixRow_8x16_1, cvv_8x16);
        vst1q_u8(chroma_1, PixRow_8x16_1);
    }
}

