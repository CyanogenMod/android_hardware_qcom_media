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

#ifndef __OMX_VDEC_INPBUF_H__
#define __OMX_VDEC_INPBUF_H__
/*============================================================================
                            O p e n M A X   Component
                                Video Decoder

*//** @file omx_vdec_inpbuf.h
  This module contains the class definition for openMAX input buffer related logic.

*//*========================================================================*/


//////////////////////////////////////////////////////////////////////////////
//                             Include Files
//////////////////////////////////////////////////////////////////////////////

// Number of input buffers defined at one place
#define MAX_NUM_INPUT_BUFFERS                                        8
#define MAX_INPUT_BUFFERS_BITMASK_SIZE ((MAX_NUM_INPUT_BUFFERS/8) + 1)

// OMX video decoder input buffer class
class omx_vdec_inpbuf {
      public:
   omx_vdec_inpbuf();   // constructor
   virtual ~ omx_vdec_inpbuf();   // destructor

   bool add_entry(unsigned int index);
   int remove_top_entry();

   // Any buffer pending?
   bool is_pending();
   // Is buffer with particular index pending?
   bool is_pending(unsigned int index);
   int get_first_pending_index(void);
   bool push_back_entry(unsigned int index);

      private:
   unsigned int m_pend_q[MAX_NUM_INPUT_BUFFERS];
   unsigned int m_read;   // read pointer
   unsigned int m_write;   // write pointer
   int m_size;      // size
   int m_last;      // last entry deleted
   unsigned char m_flags[MAX_INPUT_BUFFERS_BITMASK_SIZE];

};

#endif // __OMX_VDEC_H__
