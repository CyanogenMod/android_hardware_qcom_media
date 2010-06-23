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
#ifndef FRAMEPARSER_H
#define FRAMEPARSER_H

#include "OMX_Core.h"
#include "OMX_QCOMExtns.h"
#include "h264_utils.h"
//#include <stdlib.h>


enum codec_type
{
    CODEC_TYPE_MPEG4 = 0,
    CODEC_TYPE_H263 = 1,
    CODEC_TYPE_H264 = 2,
    CODEC_TYPE_VC1 = 3
};

enum state_start_code_parse
{
   A0,
   A1,
   A2,
   A3,
   A4
};

enum state_nal_parse
{
   NAL_LENGTH_ACC,
   NAL_PARSING
};

class frame_parse
{

public:
	H264_Utils *mutils;
	int init_start_codes (codec_type codec_type_parse);
	int parse_mpeg4_frame (OMX_BUFFERHEADERTYPE *source,
                         OMX_BUFFERHEADERTYPE *dest ,
						             OMX_U32 *partialframe);
	int init_nal_length (unsigned int nal_length);
	int parse_h264_nallength (OMX_BUFFERHEADERTYPE *source,
		                        OMX_BUFFERHEADERTYPE *dest ,
							              OMX_U32 *partialframe);
  void flush ();
  void update_metadata (unsigned int time_stamp ,unsigned int flags);
	 frame_parse ();
	~frame_parse ();

private:
   /*Variables for Start code based Parsing*/
   enum state_start_code_parse parse_state;
   unsigned char start_code[4];
   char mask_code[4];
   unsigned char last_byte,prev_one;

   /*Variables for NAL Length Parsing*/
   enum state_nal_parse state_nal;
   unsigned int nal_length;
   unsigned int accum_length;
   unsigned int bytes_tobeparsed;
   unsigned int time_stamp;
   unsigned int flags;
};

#endif /* FRAMEPARSER_H */
