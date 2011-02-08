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
#include "frameparser.h"
#include "omx_vdec.h"

#ifdef _ANDROID_
    extern "C"{
        #include<utils/Log.h>
    }
#endif//_ANDROID_

static unsigned char H264_mask_code[4] = {0xFF,0xFF,0xFF,0xFF};
static unsigned char H264_start_code[4] = {0x00,0x00,0x00,0x01};

static unsigned char MPEG4_start_code[4] = {0x00,0x00,0x01,0xB6};
static unsigned char MPEG4_mask_code[4] = {0xFF,0xFF,0xFF,0xFF};

static unsigned char H263_start_code[4] = {0x00,0x00,0x80,0x00};
static unsigned char H263_mask_code[4] = {0xFF,0xFF,0xFC,0x00};

static unsigned char VC1_AP_start_code[4] = {0x00,0x00,0x01,0x0D};
static unsigned char VC1_AP_mask_code[4] = {0xFF,0xFF,0xFF,0xFF};

frame_parse::frame_parse():mutils(NULL),
                           parse_state(A0),
                           last_byte(0),
                           prev_one(0),
                           state_nal(NAL_LENGTH_ACC),
                           nal_length(0),
                           accum_length(0),
                           bytes_tobeparsed(0),
                           time_stamp (0),
                           flags (0)
{
    memset (start_code,0,sizeof (start_code));
    memset (mask_code,0,sizeof (mask_code));
}

frame_parse::~frame_parse ()
{
    if (mutils)
        delete mutils;

    mutils = NULL;
}

int frame_parse::init_start_codes (codec_type codec_type_parse)
{
	/*Check if Codec Type is proper and we are in proper state*/
	if (codec_type_parse > CODEC_TYPE_VC1 || parse_state != A0)
	{
	  return -1;
	}

	switch (codec_type_parse)
	{
	case CODEC_TYPE_MPEG4:
		memcpy (start_code,MPEG4_start_code,4);
		memcpy (mask_code,MPEG4_mask_code,4);
		break;
	case CODEC_TYPE_H263:
		memcpy (start_code,H263_start_code,4);
		memcpy (mask_code,H263_mask_code,4);
		break;
	case CODEC_TYPE_H264:
		memcpy (start_code,H264_start_code,4);
		memcpy (mask_code,H264_mask_code,4);
		break;
	case CODEC_TYPE_VC1:
		memcpy (start_code,VC1_AP_start_code,4);
		memcpy (mask_code,VC1_AP_mask_code,4);
		break;
	}
	return 1;
}


int frame_parse::init_nal_length (unsigned int nal_len)
{
    if (nal_len == 0 || nal_len > 4 || state_nal != NAL_LENGTH_ACC)
    {
       return -1;
    }
    nal_length = nal_len;

    return 1;
}

int frame_parse::parse_mpeg4_frame ( OMX_BUFFERHEADERTYPE *source,
                                     OMX_BUFFERHEADERTYPE *dest ,
                                     OMX_U32 *partialframe)
{
    OMX_U8 *pdest = NULL,*psource = NULL;
    OMX_U32 dest_len =0, source_len = 0, temp_len = 0;
    OMX_U32 parsed_length = 0,i=0;
    int residue_byte = 0;

    if (source == NULL || dest == NULL || partialframe == NULL)
    {
        return -1;
    }

  /*Calculate how many bytes are left in source and destination*/
    dest_len = dest->nAllocLen - (dest->nFilledLen + dest->nOffset);
    psource = source->pBuffer + source->nOffset;
    pdest = dest->pBuffer + (dest->nFilledLen + dest->nOffset);
    source_len = source->nFilledLen;

    /*Need Minimum of 4 for destination to copy atleast Start code*/
    if (dest_len < 4 || source_len == 0)
    {
        DEBUG_PRINT_LOW("\n Dest_len %d source_len %d",dest_len,source_len);
        if (source_len == 0 && (source->nFlags & 0x01))
        {
            DEBUG_PRINT_LOW("\n EOS condition Inform Client that it is complete frame");
            *partialframe = 0;
            return 1;
        }
        DEBUG_PRINT_LOW("\n Error in Parsing bitstream");
        return -1;
    }

    /*Check if State of the previous find is a Start code*/
    if (parse_state == A4)
    {
        /*Check for minimun size should be 4*/
        dest->nFlags = flags;
        dest->nTimeStamp = time_stamp;
        update_metadata(source->nTimeStamp,source->nFlags);
        memcpy (pdest,start_code,4);
        pdest [2] = prev_one;
        pdest [3] = last_byte;
        dest->nFilledLen += 4;
        pdest += 4;
        parse_state = A0;
    }

    /*Entry State Machine*/
    while ( source->nFilledLen > 0 && parse_state != A0
            && parse_state != A4 && dest_len > 0
          )
    {
        //printf ("\n In the Entry Loop");
        switch (parse_state)
        {
         case A3:
             /*If fourth Byte is matching then start code is found*/
             if ((*psource & mask_code [3]) == start_code [3])
             {
               last_byte = *psource;
               parse_state = A4;
               source->nFilledLen--;
               source->nOffset++;
               psource++;
             }
             else if ((start_code [1] == start_code [0]) && (start_code [2]  == start_code [1]))
             {
                 parse_state = A2;
                 memcpy (pdest,start_code,1);
                 pdest++;
                 dest->nFilledLen++;
                 dest_len--;
             }
             else if (start_code [2] == start_code [0])
             {
                 parse_state = A1;
                 memcpy (pdest,start_code,2);
                 pdest += 2;
                 dest->nFilledLen += 2;
                 dest_len -= 2;
             }
             else
             {
                 parse_state = A0;
                 memcpy (pdest,start_code,3);
                 pdest += 3;
                 dest->nFilledLen +=3;
                 dest_len -= 3;
             }
             break;

             case A2:
                 if ((*psource & mask_code [2])  == start_code [2])
                 {
                     prev_one = *psource;
                     parse_state = A3;
                     source->nFilledLen--;
                     source->nOffset++;
                     psource++;
                 }
                 else if ( start_code [1] == start_code [0])
                 {
                     parse_state = A1;
                     memcpy (pdest,start_code,1);
                     dest->nFilledLen +=1;
                     dest_len--;
                     pdest++;
                 }
                 else
                 {
                     parse_state = A0;
                     memcpy (pdest,start_code,2);
                     dest->nFilledLen +=2;
                     dest_len -= 2;
                     pdest += 2;
                 }
             break;

         case A1:
             if ((*psource & mask_code [1]) == start_code [1])
             {
                 parse_state = A2;
                 source->nFilledLen--;
                 source->nOffset++;
                 psource++;
             }
             else
             {
                 memcpy (pdest,start_code,1);
                 dest->nFilledLen +=1;
                 pdest++;
                 dest_len--;
                 parse_state = A0;
             }
             break;
         case A4:
         case A0:
             break;
        }
        dest_len = dest->nAllocLen - (dest->nFilledLen + dest->nOffset);
    }

     if (parse_state == A4)
     {
         *partialframe = 0;
         DEBUG_PRINT_LOW("\n Nal Found length is %d",dest->nFilledLen);
         return 1;
     }

     /*Partial Frame is true*/
     *partialframe = 1;

    /*Calculate how many bytes are left in source and destination*/
    dest_len = dest->nAllocLen - (dest->nFilledLen + dest->nOffset);
    psource = source->pBuffer + source->nOffset;
    pdest = dest->pBuffer + (dest->nFilledLen + dest->nOffset);
    source_len = source->nFilledLen;

    temp_len = (source_len < dest_len)?source_len:dest_len;

    /*Check if entry state machine consumed source or destination*/
    if (temp_len == 0)
    {
        return 1;
    }

    /*Parsing State Machine*/
    while  (parsed_length < temp_len)
    {
      switch (parse_state)
      {
      case A0:
          if ((psource [parsed_length] & mask_code [0])  == start_code[0])
          {
            parse_state = A1;
          }
          parsed_length++;
          break;
      case A1:
          if ((psource [parsed_length] & mask_code [1]) == start_code [1])
          {
            parsed_length++;
            parse_state = A2;
          }
          else
          {
            parse_state = A0;
          }
      break;
      case A2:
          if ((psource [parsed_length] & mask_code [2]) == start_code [2])
          {
            prev_one = psource [parsed_length];
            parsed_length++;
            parse_state = A3;
          }
          else if (start_code [1] == start_code [0])
          {
            parse_state = A1;
          }
          else
          {
            parse_state = A0;
          }
          break;
      case A3:
          if ((psource [parsed_length] & mask_code [3]) == start_code [3])
          {
            last_byte = psource [parsed_length];
            parsed_length++;
            parse_state = A4;
          }
          else if ((start_code [1] == start_code [0]) && (start_code [2] == start_code [1]))
          {
             parse_state = A2;
          }
          else if (start_code [2] == start_code [0])
          {
              parse_state = A1;
          }
          else
          {
              parse_state = A0;
          }
          break;
      default:
          break;
      }

      /*Found the code break*/
      if (parse_state == A4)
      {
          break;
      }
    }

    /*Exit State Machine*/
    psource = source->pBuffer + source->nOffset;
    switch (parse_state)
    {
    case A4:
      *partialframe = 0;
      if (parsed_length > 4)
      {
        memcpy (pdest,psource,(parsed_length-4));
        dest->nFilledLen += (parsed_length-4);
      }
      break;
    case A3:
      if (parsed_length > 3)
      {
        memcpy (pdest,psource,(parsed_length-3));
        dest->nFilledLen += (parsed_length-3);
      }
      break;
    case A2:
        if (parsed_length > 2)
        {
          memcpy (pdest,psource,(parsed_length-2));
          dest->nFilledLen += (parsed_length-2);
        }
      break;
    case A1:
        if (parsed_length > 1)
        {
          memcpy (pdest,psource,(parsed_length-1));
          dest->nFilledLen += (parsed_length-1);
        }
      break;
    case A0:
      memcpy (pdest,psource,(parsed_length));
      dest->nFilledLen += (parsed_length);
      break;
    }

     if (source->nFilledLen < parsed_length)
     {
         printf ("\n FATAL Error");
         return -1;
     }
      source->nFilledLen -= parsed_length;
      source->nOffset += parsed_length;

    return 1;
}


int frame_parse::parse_h264_nallength (OMX_BUFFERHEADERTYPE *source,
                                       OMX_BUFFERHEADERTYPE *dest ,
                                       OMX_U32 *partialframe)
{
    OMX_U8 *pdest = NULL,*psource = NULL;
    OMX_U32 dest_len =0, source_len = 0, temp_len = 0,parsed_length = 0;

   if (source == NULL || dest == NULL || partialframe == NULL)
   {
       return -1;
   }

   /*Calculate the length's*/
   dest_len = dest->nAllocLen - (dest->nFilledLen + dest->nOffset);
   source_len = source->nFilledLen;

   if (dest_len < 4 || source_len == 0 || nal_length == 0)
   {
       DEBUG_PRINT_LOW("\n Destination %d source %d nal length %d",\
                                    dest_len,source_len,nal_length);
       return -1;
   }
   *partialframe = 1;
   temp_len = (source_len < dest_len)?source_len:dest_len;
   psource = source->pBuffer + source->nOffset;
   pdest = dest->pBuffer + (dest->nFilledLen + dest->nOffset);

   /* Find the Bytes to Accumalte*/
   if (state_nal == NAL_LENGTH_ACC)
   {
      while (parsed_length < temp_len )
      {
        bytes_tobeparsed |= (((OMX_U32)(*psource))) << (((nal_length-accum_length-1) << 3));

        /*COPY THE DATA FOR C-SIM TO BE REOMVED ON TARGET*/
        //*pdest = *psource;
        accum_length++;
        source->nFilledLen--;
        source->nOffset++;
        psource++;
        //dest->nFilledLen++;
        //pdest++;
        parsed_length++;

        if (accum_length == nal_length)
        {
            accum_length = 0;
            state_nal = NAL_PARSING;
            memcpy (pdest,H264_start_code,4);
            dest->nFilledLen += 4;
            break;
        }
      }
   }

   dest_len = dest->nAllocLen - (dest->nFilledLen + dest->nOffset);
   source_len = source->nFilledLen;
   temp_len = (source_len < dest_len)?source_len:dest_len;

   psource = source->pBuffer + source->nOffset;
   pdest = dest->pBuffer + (dest->nFilledLen + dest->nOffset);

   /*Already in Parsing state go ahead and copy*/
   if(state_nal == NAL_PARSING && temp_len > 0)
   {
     if (temp_len < bytes_tobeparsed)
     {
         memcpy (pdest,psource,temp_len);
         dest->nFilledLen += temp_len;
         source->nOffset += temp_len;
         source->nFilledLen -= temp_len;
         bytes_tobeparsed -= temp_len;
     }
     else
     {
         memcpy (pdest,psource,bytes_tobeparsed);
         temp_len -= bytes_tobeparsed;
         dest->nFilledLen += bytes_tobeparsed;
         source->nOffset += bytes_tobeparsed;
         source->nFilledLen -= bytes_tobeparsed;
         bytes_tobeparsed = 0;
     }
   }

   if (bytes_tobeparsed == 0 && state_nal == NAL_PARSING)
   {
       *partialframe = 0;
       state_nal = NAL_LENGTH_ACC;
   }

   return 1;
}

void frame_parse::flush ()
{
    parse_state = A0;
    state_nal = NAL_LENGTH_ACC;
    accum_length = 0;
    bytes_tobeparsed = 0;
}

void frame_parse::update_metadata (OMX_S64 ts ,unsigned int flgs)
{
    time_stamp = ts;
    flags = flgs;
}
