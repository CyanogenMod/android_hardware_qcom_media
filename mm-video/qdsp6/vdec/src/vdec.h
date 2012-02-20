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
/*========================================================================

                         Video Decoder interface

         *//** @file vdec.h
      This file defines the Video decoder interface.

*//*====================================================================== */

#ifndef _SIMPLE_VDEC_H_
#define _SIMPLE_VDEC_H_

#ifdef __cplusplus
extern "C" {
#endif
#include "qtypes.h"
#include "pmem.h"

#define SEI_TRIGGER_BIT_VDEC (signed long long)0x100000000
#define SEI_TRIGGER_BIT_QDSP 0x80000000

/**
 * The following are fourcc hex representations of the video
 * codecs currently supported by the vdec core
 */
#define H264_FOURCC  0x61766331
#define MPEG4_FOURCC 0x646D3476

/**
 * The following frame flags are used to provide additional
 * input/output frame properties
 */
#define FRAME_FLAG_EOS      0x00000001
#define FRAME_FLAG_FLUSHED  0x00000002
#define FRAME_FLAGS_DECODE_ERROR 0x00000004
#define FRAME_FATAL_ERROR          0x00000010

// Post processing flags for for the ConfigParametersType */
typedef enum PostProc_Flags{
     POST_PROC_QUANTIZATION = 0x1,
     POST_PROC_DEBLOCKING = 0x2,
     POST_PROC_METADATAPROPAGATION = 0x4,
     FLAG_THUMBNAIL_MODE = 0x8
   } PostProc_Flags;

   typedef enum Vdec_ReturnType {
      VDEC_SUCCESS = 0,   /* no error */
      VDEC_EFAILED = 1,   /* general failure */
      VDEC_EOUTOFBUFFERS = 2   /* buffer not available for decode */
   } Vdec_ReturnType;

   typedef enum Vdec_PortType {
      VDEC_FLUSH_INPUT_PORT,
      VDEC_FLUSH_OUTPUT_PORT,
      VDEC_FLUSH_ALL_PORT
   } Vdec_PortType;
/**
 * This lists the possible status codes of the output frame.
 */
   typedef enum Vdec_StatusType {
      VDEC_FRAME_DECODE_SUCCESS,
      VDEC_FRAME_DECODE_ERROR,
      VDEC_FATAL_ERROR,
      VDEC_FLUSH_DONE,
      VDEC_END_OF_STREAM,
      VDEC_FRAME_FLUSHED,   //!< Frame is flushed
      VDEC_STREAM_SWITCHED,   //!< Stream has switched
      VDEC_SUSPEND_DONE   //!< Suspend operation finished
   } Vdec_StatusType;

   typedef enum Vdec_BufferOwnerType {
      VDEC_BUFFER_WITH_HW,
      VDEC_BUFFER_WITH_APP,
      VDEC_BUFFER_WITH_VDEC_CORE,
      VDEC_BUFFER_WITH_APP_FLUSHED,
   } Vdec_BufferOwnerType;

/**
 * This lists all the possible encoded frame types
 */
   typedef enum Vdec_PictureType {
      VDEC_PICTURE_TYPE_I,
      VDEC_PICTURE_TYPE_P,
      VDEC_PICTURE_TYPE_B,
      VDEC_PICTURE_TYPE_BI,
      VDEC_PICTURE_TYPE_SKIP,
      VDEC_PICTURE_TYPE_UNKNOWN
   } Vdec_PictureType;

/**
 * This lists all the possible frame types
 */
   typedef enum Vdec_PictureFormat {
      VDEC_PROGRESSIVE_FRAME,
      VDEC_INTERLACED_FRAME,
      VDEC_INTERLACED_FIELD
   } Vdec_PictureFormat;

/**
 * This lists all the possible picture resolutions
 */
   typedef enum Vdec_PictureRes {
      VDEC_PICTURE_RES_1x1,
      VDEC_PICTURE_RES_2x1,
      VDEC_PICTURE_RES_1x2,
      VDEC_PICTURE_RES_2x2
   } Vdec_PictureRes;

   typedef enum Vdec_PerformanceRequestType {
     VDEC_PERF_SET_MIN = 0,
     VDEC_PERF_LOWER,
     VDEC_PERF_RAISE,
     VDEC_PERF_SET_MAX
   }Vdec_PerformanceRequestType;

/**
 * This defines the structure of the pan scan parameters used in
 * Advanced Profile Frame display
 */
#define MAX_VC1_PAN_SCAN_WINDOWS 4
   typedef struct Vdec_VC1PanScanRegions {
      int32 numWindows;
      int32 winWidth[MAX_VC1_PAN_SCAN_WINDOWS];
      int32 winHeight[MAX_VC1_PAN_SCAN_WINDOWS];
      int32 winHorOffset[MAX_VC1_PAN_SCAN_WINDOWS];
      int32 winVerOffset[MAX_VC1_PAN_SCAN_WINDOWS];
   } Vdec_VC1PanScanRegions;

   typedef struct Vdec_CroppingWindow {
      uint32 x1;   //!< Left offset
      uint32 y1;   //!< Top Offset
      uint32 x2;   //!< Right Offset
      uint32 y2;   //!< Bottom Offset
   } Vdec_CroppingWindow;

/**
 * This structure specifies the detials of the
 * output frame.
 *
 * status - Status code of the frame
 * userData1 - User data, used internally by the decoder
 * userData2 - uint32 representation of the associated input cookie
 * nDecPicWidth - Width of the decoded picture
 * nDecPicHeigh - Height of the decoded picture
 * nDispPicWidth - Width of the picture to display
 * nDispPicHeight - Height of the picture to display
 * ePicType[2] - Coding type of the decoded frame (both fields)
 * ePicFormat - Picture coding format
 * nVC1RangeY - Scale factor for Luma Range Mapping
 * nVC1RangeUV - Scale factor for Chroma Range Mapping
 * ePicResolution - Scaling factor for frame wrt full resolution frame
 * nRepeatProgFrames - Number of times frame may be repeated by display
 * bRepeatFirstField - 1st field can be repeated after 2nd in a pair
 * bTopFieldFirst - Field closer to top to be displayed before bottom
 * bFrameInterpFlag - Data not suitable for inter-frame interpolation
 * panScan - Pan scan window regions
 * reserved1 - reserved field, should be set to 0
 * reserved2 - reserved field, should be set to 0
 */
#define MAX_FIELDS 2
   typedef struct Vdec_FrameDetailsType {
      Vdec_StatusType status;
      uint32 userData1;
      uint32 userData2;
      uint64 timestamp;
      uint64 calculatedTimeStamp;
      uint32 nDecPicWidth;
      uint32 nDecPicHeight;
      Vdec_CroppingWindow cwin;
      Vdec_PictureType ePicType[MAX_FIELDS];
      Vdec_PictureFormat ePicFormat;
      uint32 nVC1RangeY;
      uint32 nVC1RangeUV;
      Vdec_PictureRes ePicResolution;
      uint32 nRepeatProgFrames;
      uint32 bRepeatFirstField;
      uint32 bTopFieldFirst;
      uint32 bFrameInterpFlag;
      Vdec_VC1PanScanRegions panScan;
      uint32 nPercentConcealedMacroblocks;
      uint32 flags;
      uint32 performanceStats;   //!< Performance statistics returned by the decoder
   } Vdec_FrameDetailsType;

/**
 * This structure specifies the buffer requirements of the
 * decoder.
 */
   typedef struct Vdec_BufferRequirements {
      uint32 bufferSize;   /* Buffer size, in bytes */
      uint32 numMinBuffers;   //!<  The minimum number of buffers required for functionality.
      uint32 numMaxBuffers;   //!<  The maximum number of buffers for good performance.
   } Vdec_BufferRequirements;

/**
 * This structure specifies information about the input and output allocated
 * buffers.
 */
   typedef struct Vdec_BufferInfo {
      byte *base;   /* Starting virtual address */
      int pmem_id;
      unsigned pmem_offset;
      uint32 bufferSize;   /* Buffer size */
      Vdec_BufferOwnerType state;
      void *omx_cookie;
   } Vdec_BufferInfo;

/**
 * This structure specifies a decoder (input or output) frame.
 */
   typedef struct vdec_frame {
      int64 timestamp;   /* frame timestamp */
      uint32 flags;   /* holds frame flags properties */
      Vdec_FrameDetailsType frameDetails;   /* Details on decoded frame */
      Vdec_BufferInfo buffer;
   } vdec_frame;

/**
 * This structure defines an input video frame sent to decoder
 */
   typedef struct video_input_frame_info {
      void *data;   /* Pointer to the input data */
      unsigned int len;   /* Input frame length in bytess */
      signed long long timestamp;   /* Timestamp,forwarded from i/p to o/p */
      unsigned int flags;   /* Flag to indicate EOS */
      uint32 userData;   /* User data associated with the frame */
      int32 avSyncState;   /* AV sync state */
   } video_input_frame_info;

/**
 * This structure is a decoder context that should be filled up by the client
 * before calling vdec_open
 */
   typedef struct vdec_context {
      /* The following fields are to be filled up by the client if it
       * has the corresponding information.
       */
      uint32 width;   /* Width of the image */
      uint32 height;   /* Height of the image */
      uint32 fourcc;   /* 4cc for type of decoder */
      byte *sequenceHeader;   /* parameter set */
      uint32 sequenceHeaderLen;   /* length of parameter set */
      boolean bRenderDecodeOrder;   /* Render decode order */
      unsigned int size_of_nal_length_field;
      uint32 vc1Rowbase;
      uint32 postProc;
      int32 vdec_fd;
      uint32 color_format;

      /////////////////////////////////To be removed in future.
      char kind[128];   // new code to pass the role from omx vdec.
      // This has to be replaced with fourcc in future
      void (*process_message) (struct vdec_context * ctxt,
                unsigned char id);
      void *extra;
      //////////////////////////////////
      /* The following fields are to be filled up by the vdec (decoder)
       * if it has the corresponding information.
       */

      /* Input Output Buffer requirements
       * This might be filled in by vdec if the vdec decides on the
       * buffer requirements.
       */
      Vdec_BufferRequirements inputReq;
      Vdec_BufferRequirements outputReq;

      /* Input/Ouput Buffers
       * These hold the location and size of the input and output
       * buffers. This might be filled up by the vdec after a call
       * to vdec_commit_memory.
       */
      Vdec_BufferInfo *inputBuffer;
      uint32 nInpBufAllocLen;
      uint32 numInputBuffers;

      vdec_frame *outputBuffer;
      uint32 nOutBufAllocLen;
      uint32 numOutputBuffers;

      /*
       * Callback function that will be called when output frame is complete
       * call vdec_release_frame() when you are done with it
       */
      void (*frame_done) (struct vdec_context * ctxt,
                struct vdec_frame * frame);

     /**
      * Callback function that will be called when input buffer is no longer in
      * use
      */
      void (*buffer_done) (struct vdec_context * ctxt, void *cookie);
   } vdec_context;

   struct Vdec_Input_BufferInfo {
      Vdec_BufferInfo *input;
      unsigned int numbuf;
   };
   struct VDecoder_buf_info {
      uint32 buffer_size;
      unsigned numbuf;
   };

/**
 *  A structure which identifies the decoder.
 *  The stucture is returned when the decoder
 *  is opened and used in subsequent calls
 *  to identify the decoder.
 */
   struct VDecoder {
      unsigned pmem_buffers;
      struct pmem *arena;

      void *adsp_module;
      struct vdec_context *ctxt;
      void *thread_specific_info;
      Vdec_BufferRequirements decReq1;
      Vdec_BufferRequirements decReq2;
      boolean is_commit_memory;
   };

/**
  * This method is to be used to get the input buffer
  * requirements from the Decoder.
  *
  *  @param[inout] buf_req
  *     Video decoder buffer requirements. The client of the
  *     decoder should should pass the appropriate pointer to
  *     the buffer requirements structure and the Decoder will
  *     fill this with the required input buffer requirement
  *     fields.
  *  
  *  @return
  *     0 success
  *     -1 failure
  */
   Vdec_ReturnType vdec_get_input_buf_requirements(struct VDecoder_buf_info
                     *buf_req, int mode);

/**
  * This method is to be used to get the input buffer
  * requirements from the Decoder.
  *
  *  @param[in] unsigned int size
  *     The size of the input buffer that need to be allocated
  *  
  *  @return
  *     NULL if the buffer allocation failures
  *     pointer to virtual address of input buffer allocated
  */
   Vdec_ReturnType vdec_allocate_input_buffer(unsigned int size,
                     Vdec_BufferInfo * buf,
                     int is_pmem);
/**
  * This method is to be used to get the input buffer
  * requirements from the Decoder.
  *
  *  @param[in] Vdec_BufferInfo
  *     the pointer to the input buffer allocated by the call to
  *     vdec_allocate_one_input_buffer
  *  
  *  @return
  *     0 success
  *     -1 failure
  */
   Vdec_ReturnType vdec_free_input_buffer(Vdec_BufferInfo * buf_info,
                      int is_pmem);

/**
  * This method is to be used to open the decoder.
  *
  *  @param[inout] ctxt
  *     Video decoder context. The client of the decoder should
  *     fill in the appropriate fields in the structure.After
  *     the return from the call, the decoder will fill up
  *     certain fields in the structure.
  *  
  *  @return
  *     Video decoder strucure. This value has to be passed as
  *     the first argument in further calls. A NULL return value
  *     indicates that the decoder could not be opened.
  */
   struct VDecoder *vdec_open(struct vdec_context *ctxt);

/**
  * This method is to be used to allocate the memory needed by decoder.
  * This method needs to be called before decoding can begin. Also,
  * vdec_open should have been called before this.
  *
  * Prerequisite: vdec_open should have been called.
  *
  * @param[in] dec
  *     Pointer to the Video decoder.
  *  
  * @return 
  *  
  * 0 - success. Memory is allocated successfully. 
  *  
  * -1 - Failure. Memory allocation failed.
  *
  */
   Vdec_ReturnType vdec_commit_memory(struct VDecoder *dec);

/**
  * This method is to be used to close a video decoder.
  *
  * Prerequisite: vdec_open should have been called. 
  *  
  *  @param[in] dec
  *     Pointer to the Video decoder.
  */
   Vdec_ReturnType vdec_close(struct VDecoder *dec);

/**
  * This method is to be used to post an input buffer.
  *
  * Prerequisite: vdec_open should have been called.
  *
  *  @param[in] dec
  *     Pointer to the Video decoder.
  *
  *  @param[in] frame
  *     The input frame to be decoded. The frame should point to an area in
  *     shared memory for WinMo solution.
  *
  *  @param[in] cookie
  *     A cookie for the input frame. The cookie is used to
  *     identify the frame when the  buffer_done callback
  *     function is called.The cookie is unused by the decoder
  *     otherwise.
  *
  *  @return
  *     0 if the input buffer is posted successfully.
  *     -1 if the input buffer could not be sent to the decoder.
  */
   Vdec_ReturnType vdec_post_input_buffer(struct VDecoder *dec,
                      struct video_input_frame_info
                      *frame, void *cookie,
                      int is_pmem);

/**
  * This method is used to release an output frame back to the decoder.
  *
  * Prerequisite: vdec_open should have been called.
  *
  *  @param[in] dec
  *     Pointer to the Video decoder.
  *
  *  @param[in] frame
  *     The released output frame.
  */
   Vdec_ReturnType vdec_release_frame(struct VDecoder *dec,
                  struct vdec_frame *frame);

/**
  * This method is used to flush the frames in the decoder.
  *
  * Prerequisite: vdec_open should have been called.
  *
  *  @param[in] dec
  *     Pointer to the Video decoder.
  *
  *  @param[in] nFlushedFrames
  *     The number of flushed frames. A value of -1 indicates that n
  *     information is available on number of flushed frames.
  */
   Vdec_ReturnType vdec_flush_port(struct VDecoder *dec,
               int *nFlushedFrames,
               Vdec_PortType port);

/* funtion tells the size of extra data that needs to be appended at the end of 
 * o/p buffer
 */
int getExtraDataSize(void);

   Vdec_ReturnType vdec_performance_change_request(struct VDecoder *dec,
              unsigned int);

#ifdef USE_PMEM_ADSP_CACHED
/**
  * This method is used to perform cache operations on the pmem region in the decoder.
  *
  * Prerequisite: vdec_open should have been called.
  *
  *  @param[in] pmem_id
  *     id of the pmem region to use
  *
  *  @param[in] addr
  *     The virtual addr of the pmem region
  *
  *  @param[in] size
  *     The size of the region
  *
  *  @param[in] op
  *     Cache operation to perform as defined by PMEM_CACHE_OP
  */
  void vdec_cachemaint(int pmem_id, void *addr, unsigned size, PMEM_CACHE_OP op);
#endif

#ifdef __cplusplus
}
#endif
#endif
