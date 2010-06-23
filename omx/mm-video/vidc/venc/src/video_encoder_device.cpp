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
#include<string.h>
#include <sys/ioctl.h>
#include<unistd.h>
#include <fcntl.h>
#include "video_encoder_device.h"
#include "omx_video_encoder.h"

#define MPEG4_SP_START 0
#define MPEG4_ASP_START (MPEG4_SP_START + 8)
#define MPEG4_720P_LEVEL 6
#define H263_BP_START 0
#define H264_BP_START 0
#define H264_HP_START (H264_BP_START + 11)
#define H264_MP_START (H264_BP_START + 21)

/* MPEG4 profile and level table*/
static const unsigned int mpeg4_profile_level_table[][5]=
{
    /*max mb per frame, max mb per sec, max bitrate, level, profile*/
    {99,1485,64000,OMX_VIDEO_MPEG4Level0,OMX_VIDEO_MPEG4ProfileSimple},
    {99,1485,128000,OMX_VIDEO_MPEG4Level1,OMX_VIDEO_MPEG4ProfileSimple},
    {396,5940,128000,OMX_VIDEO_MPEG4Level2,OMX_VIDEO_MPEG4ProfileSimple},
    {396,11880,384000,OMX_VIDEO_MPEG4Level3,OMX_VIDEO_MPEG4ProfileSimple},
    {1200,36000,4000000,OMX_VIDEO_MPEG4Level4a,OMX_VIDEO_MPEG4ProfileSimple},
    {1620,40500,8000000,OMX_VIDEO_MPEG4Level5,OMX_VIDEO_MPEG4ProfileSimple},
    {3600,108000,14000000,OMX_VIDEO_MPEG4Level5,OMX_VIDEO_MPEG4ProfileSimple},
    {0,0,0,0,0},

    {99,2970,128000,OMX_VIDEO_MPEG4Level0,OMX_VIDEO_MPEG4ProfileAdvancedSimple},
    {99,2970,128000,OMX_VIDEO_MPEG4Level1,OMX_VIDEO_MPEG4ProfileAdvancedSimple},
    {396,5940,384000,OMX_VIDEO_MPEG4Level2,OMX_VIDEO_MPEG4ProfileAdvancedSimple},
    {396,11880,768000,OMX_VIDEO_MPEG4Level3,OMX_VIDEO_MPEG4ProfileAdvancedSimple},
    {792,23760,3000000,OMX_VIDEO_MPEG4Level4,OMX_VIDEO_MPEG4ProfileAdvancedSimple},
    {1620,48600,8000000,OMX_VIDEO_MPEG4Level5,OMX_VIDEO_MPEG4ProfileAdvancedSimple},
    {3600,108000,14000000,OMX_VIDEO_MPEG4Level5,OMX_VIDEO_MPEG4ProfileAdvancedSimple},
    {0,0,0,0,0}
};

/* H264 profile and level table*/
static const unsigned int h264_profile_level_table[][5]=
{
     /*max mb per frame, max mb per sec, max bitrate, level, profile*/
    {99,1485,64000,OMX_VIDEO_AVCLevel1,OMX_VIDEO_AVCProfileBaseline},
    {99,1485,128000,OMX_VIDEO_AVCLevel1b,OMX_VIDEO_AVCProfileBaseline},
    {396,3000,192000,OMX_VIDEO_AVCLevel11,OMX_VIDEO_AVCProfileBaseline},
    {396,6000,384000,OMX_VIDEO_AVCLevel12,OMX_VIDEO_AVCProfileBaseline},
    {396,11880,768000,OMX_VIDEO_AVCLevel13,OMX_VIDEO_AVCProfileBaseline},
    {396,11880,2000000,OMX_VIDEO_AVCLevel2,OMX_VIDEO_AVCProfileBaseline},
    {792,19800,4000000,OMX_VIDEO_AVCLevel21,OMX_VIDEO_AVCProfileBaseline},
    {1620,20250,4000000,OMX_VIDEO_AVCLevel22,OMX_VIDEO_AVCProfileBaseline},
    {1620,40500,10000000,OMX_VIDEO_AVCLevel3,OMX_VIDEO_AVCProfileBaseline},
    {3600,108000,14000000,OMX_VIDEO_AVCLevel31,OMX_VIDEO_AVCProfileBaseline},
    {0,0,0,0,0},

    {99,1485,64000,OMX_VIDEO_AVCLevel1,OMX_VIDEO_AVCProfileHigh},
    {99,1485,160000,OMX_VIDEO_AVCLevel1b,OMX_VIDEO_AVCProfileHigh},
    {396,3000,240000,OMX_VIDEO_AVCLevel11,OMX_VIDEO_AVCProfileHigh},
    {396,6000,480000,OMX_VIDEO_AVCLevel12,OMX_VIDEO_AVCProfileHigh},
    {396,11880,960000,OMX_VIDEO_AVCLevel13,OMX_VIDEO_AVCProfileHigh},
    {396,11880,2500000,OMX_VIDEO_AVCLevel2,OMX_VIDEO_AVCProfileHigh},
    {792,19800,5000000,OMX_VIDEO_AVCLevel21,OMX_VIDEO_AVCProfileHigh},
    {1620,20250,5000000,OMX_VIDEO_AVCLevel22,OMX_VIDEO_AVCProfileHigh},
    {1620,40500,12500000,OMX_VIDEO_AVCLevel3,OMX_VIDEO_AVCProfileHigh},
    {3600,108000,17500000,OMX_VIDEO_AVCLevel31,OMX_VIDEO_AVCProfileHigh},
    {0,0,0,0,0},

    {99,1485,64000,OMX_VIDEO_AVCLevel1,OMX_VIDEO_AVCProfileMain},
    {99,1485,160000,OMX_VIDEO_AVCLevel1b,OMX_VIDEO_AVCProfileMain},
    {396,3000,240000,OMX_VIDEO_AVCLevel11,OMX_VIDEO_AVCProfileMain},
    {396,6000,480000,OMX_VIDEO_AVCLevel12,OMX_VIDEO_AVCProfileMain},
    {396,11880,960000,OMX_VIDEO_AVCLevel13,OMX_VIDEO_AVCProfileMain},
    {396,11880,2500000,OMX_VIDEO_AVCLevel2,OMX_VIDEO_AVCProfileMain},
    {792,19800,5000000,OMX_VIDEO_AVCLevel21,OMX_VIDEO_AVCProfileMain},
    {1620,20250,5000000,OMX_VIDEO_AVCLevel22,OMX_VIDEO_AVCProfileMain},
    {1620,40500,12500000,OMX_VIDEO_AVCLevel3,OMX_VIDEO_AVCProfileMain},
    {3600,108000,17500000,OMX_VIDEO_AVCLevel31,OMX_VIDEO_AVCProfileMain},
    {0,0,0,0,0}
};

/* H263 profile and level table*/
static const unsigned int h263_profile_level_table[][5]=
{
    /*max mb per frame, max mb per sec, max bitrate, level, profile*/
    {99,1485,64000,OMX_VIDEO_H263Level10,OMX_VIDEO_H263ProfileBaseline},
    {396,5940,128000,OMX_VIDEO_H263Level20,OMX_VIDEO_H263ProfileBaseline},
    {396,11880,384000,OMX_VIDEO_H263Level30,OMX_VIDEO_H263ProfileBaseline},
    {396,11880,2048000,OMX_VIDEO_H263Level40,OMX_VIDEO_H263ProfileBaseline},
    {99,1485,128000,OMX_VIDEO_H263Level45,OMX_VIDEO_H263ProfileBaseline},
    {396,19800,4096000,OMX_VIDEO_H263Level50,OMX_VIDEO_H263ProfileBaseline},
    {810,40500,8192000,OMX_VIDEO_H263Level60,OMX_VIDEO_H263ProfileBaseline},
    {1620,81000,16384000,OMX_VIDEO_H263Level70,OMX_VIDEO_H263ProfileBaseline},
    {0,0,0,0,0}
};

//constructor
venc_dev::venc_dev()
{
//nothing to do

}

venc_dev::~venc_dev()
{
  //nothing to do
}

void* async_venc_message_thread (void *input)
{
  struct venc_ioctl_msg ioctl_msg ={NULL,NULL};
  struct venc_timeout timeout;
  struct venc_msg venc_msg;
  omx_venc *omx = reinterpret_cast<omx_venc*>(input);

  timeout.millisec = VEN_TIMEOUT_INFINITE;
  while(1)
  {
    ioctl_msg.inputparam = NULL;
    ioctl_msg.outputparam = (void*)&venc_msg;

    /*Wait for a message from the video decoder driver*/
    if(ioctl(omx->handle->m_nDriver_fd,VEN_IOCTL_CMD_READ_NEXT_MSG,(void *)&ioctl_msg) < 0)
    {
      DEBUG_PRINT_ERROR("\nioctl VEN_IOCTL_CMD_READ_NEXT_MSG failed/stopped");
      break;
    }
    else
    {
      /*Call Instance specific process function*/
      if(omx->async_message_process(input,&venc_msg) < 0)
      {
        DEBUG_PRINT_ERROR("\nERROR: Wrong ioctl message");
        break;
      }
    }
  }
  DEBUG_PRINT_HIGH("omx_venc: Async Thread exit\n");
  return NULL;
}

bool venc_dev::venc_open(OMX_U32 codec)
{
  struct venc_ioctl_msg ioctl_msg = {NULL,NULL};
  int r;
  unsigned int   alignment = 0,buffer_size = 0, temp =0;

  m_nDriver_fd = open ("/dev/msm_vidc_enc",O_RDWR|O_NONBLOCK);
  if(m_nDriver_fd == 0)
  {
    DEBUG_PRINT_ERROR("ERROR: Got fd as 0 for msm_vidc_enc, Opening again\n");
    m_nDriver_fd = open ("/dev/msm_vidc_enc",O_RDWR|O_NONBLOCK);
  }

  if((int)m_nDriver_fd < 0)
  {
    DEBUG_PRINT_ERROR("ERROR: Omx_venc::Comp Init Returning failure\n");
    return false;
  }

  DEBUG_PRINT_LOW("\nm_nDriver_fd = %d\n", m_nDriver_fd);
  // set the basic configuration of the video encoder driver
  m_sVenc_cfg.input_width = OMX_CORE_QCIF_WIDTH;
  m_sVenc_cfg.input_height= OMX_CORE_QCIF_HEIGHT;
  m_sVenc_cfg.dvs_width = OMX_CORE_QCIF_WIDTH;
  m_sVenc_cfg.dvs_height = OMX_CORE_QCIF_HEIGHT;
  m_sVenc_cfg.fps_num = 30;
  m_sVenc_cfg.fps_den = 1;
  m_sVenc_cfg.targetbitrate = 64000;
  m_sVenc_cfg.inputformat= VEN_INPUTFMT_NV12;
  if(codec == OMX_VIDEO_CodingMPEG4)
  {
    m_sVenc_cfg.codectype = VEN_CODEC_MPEG4;
    codec_profile.profile = VEN_PROFILE_MPEG4_SP;
    profile_level.level = VEN_LEVEL_MPEG4_2;
  }
  else if(codec == OMX_VIDEO_CodingH263)
  {
    m_sVenc_cfg.codectype = VEN_CODEC_H263;
    codec_profile.profile = VEN_PROFILE_H263_BASELINE;
    profile_level.level = VEN_LEVEL_H263_20;
  }
  if(codec == OMX_VIDEO_CodingAVC)
  {
    m_sVenc_cfg.codectype = VEN_CODEC_H264;
    codec_profile.profile = VEN_PROFILE_H264_BASELINE;
    profile_level.level = VEN_LEVEL_H264_1p1;
  }
  ioctl_msg.inputparam = (void*)&m_sVenc_cfg;
  ioctl_msg.outputparam = NULL;
  if(ioctl (m_nDriver_fd,VEN_IOCTL_SET_BASE_CFG,(void*)&ioctl_msg) < 0 )
  {
    DEBUG_PRINT_ERROR("\nERROR: Request for setting base configuration failed");
    return false;
  }

  // Get the I/P and O/P buffer requirements
  ioctl_msg.inputparam = NULL;
  ioctl_msg.outputparam = (void*)&m_sInput_buff_property;
  if(ioctl (m_nDriver_fd,VEN_IOCTL_GET_INPUT_BUFFER_REQ,(void*)&ioctl_msg) < 0)
  {
    DEBUG_PRINT_ERROR("\nERROR: Request for getting i/p buffer requirement failed");
    return false;
  }
  ioctl_msg.inputparam = NULL;
  ioctl_msg.outputparam = (void*)&m_sOutput_buff_property;
  if(ioctl (m_nDriver_fd,VEN_IOCTL_GET_OUTPUT_BUFFER_REQ,(void*)&ioctl_msg) < 0)
  {
    DEBUG_PRINT_ERROR("\nERROR: Request for getting o/p buffer requirement failed");
    return false;
  }

  m_profile_set = false;
  m_level_set = false;
  if(venc_set_profile_level(0, 0))
  {
    DEBUG_PRINT_HIGH("\n %s(): Init Profile/Level setting success",
        __func__);
  }

  return true;
}

void venc_dev::venc_close()
{
  DEBUG_PRINT_LOW("\nvenc_close: fd = %d", m_nDriver_fd);
  if((int)m_nDriver_fd >= 0)
  {
    DEBUG_PRINT_HIGH("\n venc_close(): Calling VEN_IOCTL_CMD_STOP_READ_MSG");
    (void)ioctl(m_nDriver_fd, VEN_IOCTL_CMD_STOP_READ_MSG,
        NULL);
    DEBUG_PRINT_LOW("\nCalling close()\n");
    close(m_nDriver_fd);
    m_nDriver_fd = -1;
  }
}

bool venc_dev::venc_set_buf_req(unsigned long *min_buff_count,
                                unsigned long *actual_buff_count,
                                unsigned long *buff_size,
                                unsigned long port)
{
  struct venc_ioctl_msg ioctl_msg = {NULL,NULL};
  unsigned long temp_count = 0;

  if(port == 0)
  {
    if(*actual_buff_count > m_sInput_buff_property.mincount)
    {
      temp_count = m_sInput_buff_property.actualcount;
      m_sInput_buff_property.actualcount = *actual_buff_count;
      ioctl_msg.inputparam = (void*)&m_sInput_buff_property;
      ioctl_msg.outputparam = NULL;
      if(ioctl (m_nDriver_fd,VEN_IOCTL_SET_INPUT_BUFFER_REQ,(void*)&ioctl_msg) < 0)
      {
        DEBUG_PRINT_ERROR("\nERROR: Request for setting i/p buffer requirement failed");
        m_sInput_buff_property.actualcount = temp_count;
        return false;
      }
      DEBUG_PRINT_LOW("\n I/P Count set to %lu\n", *actual_buff_count);
    }
  }
  else
  {
    if(*actual_buff_count > m_sOutput_buff_property.mincount)
    {
	  temp_count = m_sOutput_buff_property.actualcount;
      m_sOutput_buff_property.actualcount = *actual_buff_count;
      ioctl_msg.inputparam = (void*)&m_sOutput_buff_property;
      ioctl_msg.outputparam = NULL;
      if(ioctl (m_nDriver_fd,VEN_IOCTL_SET_OUTPUT_BUFFER_REQ,(void*)&ioctl_msg) < 0)
      {
        DEBUG_PRINT_ERROR("\nERROR: Request for setting o/p buffer requirement failed");
		m_sOutput_buff_property.actualcount = temp_count;
        return false;
      }
      DEBUG_PRINT_LOW("\n O/P Count set to %lu\n", *actual_buff_count);
    }
  }

  return true;

}

bool venc_dev::venc_get_buf_req(unsigned long *min_buff_count,
                                unsigned long *actual_buff_count,
                                unsigned long *buff_size,
                                unsigned long port)
{
  struct venc_ioctl_msg ioctl_msg = {NULL,NULL};

  if(port == 0)
  {
    ioctl_msg.inputparam = NULL;
    ioctl_msg.outputparam = (void*)&m_sInput_buff_property;
    if(ioctl (m_nDriver_fd,VEN_IOCTL_GET_INPUT_BUFFER_REQ,(void*)&ioctl_msg) < 0)
    {
      DEBUG_PRINT_ERROR("\nERROR: Request for getting i/p buffer requirement failed");
      return false;
    }
    *min_buff_count = m_sInput_buff_property.mincount;
    *actual_buff_count = m_sInput_buff_property.actualcount;
    *buff_size = m_sInput_buff_property.datasize
                 + (m_sInput_buff_property.datasize % m_sInput_buff_property.alignment) ;
  }
  else
  {
    ioctl_msg.inputparam = NULL;
    ioctl_msg.outputparam = (void*)&m_sOutput_buff_property;
    if(ioctl (m_nDriver_fd,VEN_IOCTL_GET_OUTPUT_BUFFER_REQ,(void*)&ioctl_msg) < 0)
    {
      DEBUG_PRINT_ERROR("\nERROR: Request for getting o/p buffer requirement failed");
      return false;
    }
    *min_buff_count = m_sOutput_buff_property.mincount;
    *actual_buff_count = m_sOutput_buff_property.actualcount;
    *buff_size = m_sOutput_buff_property.datasize
                 + (m_sOutput_buff_property.datasize % m_sOutput_buff_property.alignment) ;
  }

  return true;

}

bool venc_dev::venc_set_param(void *paramData,OMX_INDEXTYPE index )
{
  venc_ioctl_msg ioctl_msg = {NULL,NULL};
  OMX_U32 temp_out_buf_count = 0;
  DEBUG_PRINT_LOW("venc_set_param:: venc-720p\n");
  switch(index)
  {
  case OMX_IndexParamPortDefinition:
    {
      OMX_PARAM_PORTDEFINITIONTYPE *portDefn;
      portDefn = (OMX_PARAM_PORTDEFINITIONTYPE *) paramData;
      DEBUG_PRINT_LOW("venc_set_param: OMX_IndexParamPortDefinition\n");
      if(portDefn->nPortIndex == PORT_INDEX_IN)
      {
        if(!venc_set_color_format(portDefn->format.video.eColorFormat))
        {
          return false;
        }
        if(m_sVenc_cfg.input_height != portDefn->format.video.nFrameHeight ||
          m_sVenc_cfg.input_width != portDefn->format.video.nFrameWidth)
        {
          DEBUG_PRINT_LOW("\n Basic parameter has changed");
          m_sVenc_cfg.input_height = portDefn->format.video.nFrameHeight;
          m_sVenc_cfg.input_width = portDefn->format.video.nFrameWidth;

          temp_out_buf_count = m_sOutput_buff_property.actualcount;
          ioctl_msg.inputparam = (void*)&m_sVenc_cfg;
          ioctl_msg.outputparam = NULL;
          if(ioctl (m_nDriver_fd,VEN_IOCTL_SET_BASE_CFG,(void*)&ioctl_msg) < 0)
          {
            DEBUG_PRINT_ERROR("\nERROR: Request for setting base config failed");
            return false;
          }

          DEBUG_PRINT_LOW("\n Updating the buffer count/size for the new resolution");
          ioctl_msg.inputparam = NULL;
          ioctl_msg.outputparam = (void*)&m_sInput_buff_property;
          if(ioctl (m_nDriver_fd, VEN_IOCTL_GET_INPUT_BUFFER_REQ,(void*)&ioctl_msg) < 0)
          {
            DEBUG_PRINT_ERROR("\nERROR: Request for getting i/p bufreq failed");
            return false;
          }
          DEBUG_PRINT_LOW("\n Got updated m_sInput_buff_property values: "
                      "datasize = %u, maxcount = %u, actualcnt = %u, "
                      "mincount = %u", m_sInput_buff_property.datasize,
                      m_sInput_buff_property.maxcount, m_sInput_buff_property.actualcount,
                      m_sInput_buff_property.mincount);

          ioctl_msg.inputparam = NULL;
          ioctl_msg.outputparam = (void*)&m_sOutput_buff_property;
          if(ioctl (m_nDriver_fd, VEN_IOCTL_GET_OUTPUT_BUFFER_REQ,(void*)&ioctl_msg) < 0)
          {
            DEBUG_PRINT_ERROR("\nERROR: Request for getting o/p bufreq failed");
            return false;
          }

          DEBUG_PRINT_LOW("\n Got updated m_sOutput_buff_property values: "
                      "datasize = %u, maxcount = %u, actualcnt = %u, "
                      "mincount = %u", m_sOutput_buff_property.datasize,
                      m_sOutput_buff_property.maxcount, m_sOutput_buff_property.actualcount,
                      m_sOutput_buff_property.mincount);

          if(temp_out_buf_count < 7)
            temp_out_buf_count = 7;
          m_sOutput_buff_property.actualcount = temp_out_buf_count;
          ioctl_msg.inputparam = (void*)&m_sOutput_buff_property;
          ioctl_msg.outputparam = NULL;
          if(ioctl (m_nDriver_fd, VEN_IOCTL_SET_OUTPUT_BUFFER_REQ,(void*)&ioctl_msg) < 0)
          {
            DEBUG_PRINT_ERROR("\nERROR: Request for setting o/p bufreq failed");
            return false;
          }

          if((portDefn->nBufferCountActual >= m_sInput_buff_property.mincount) &&
           (portDefn->nBufferCountActual <= m_sInput_buff_property.maxcount))
          {
            m_sInput_buff_property.actualcount = portDefn->nBufferCountActual;
            ioctl_msg.inputparam = (void*)&m_sInput_buff_property;
            ioctl_msg.outputparam = NULL;
            if(ioctl(m_nDriver_fd,VEN_IOCTL_SET_INPUT_BUFFER_REQ,(void*)&ioctl_msg) < 0)
            {
              DEBUG_PRINT_ERROR("\nERROR: Request for setting i/p buffer requirements failed");
              return false;
            }
          }
          if(m_sInput_buff_property.datasize != portDefn->nBufferSize)
          {
            DEBUG_PRINT_ERROR("\nWARNING: Requested i/p bufsize[%u],"
                              "Driver's updated i/p bufsize = %u", portDefn->nBufferSize,
                              m_sInput_buff_property.datasize);
          }
          m_level_set = false;
          if(venc_set_profile_level(0, 0))
          {
            DEBUG_PRINT_HIGH("\n %s(): Dynamic Profile/Level setting success",
                __func__);
          }
        }
        else
        {
          if((portDefn->nBufferCountActual >= m_sInput_buff_property.mincount) &&
           (m_sInput_buff_property.actualcount <= portDefn->nBufferCountActual) &&
            (m_sInput_buff_property.datasize == portDefn->nBufferSize))
          {
            m_sInput_buff_property.actualcount = portDefn->nBufferCountActual;
            ioctl_msg.inputparam = (void*)&m_sInput_buff_property;
            ioctl_msg.outputparam = NULL;
            if(ioctl (m_nDriver_fd,VEN_IOCTL_SET_INPUT_BUFFER_REQ,(void*)&ioctl_msg) < 0)
            {
              DEBUG_PRINT_ERROR("\nERROR: ioctl VEN_IOCTL_SET_INPUT_BUFFER_REQ failed");
              return false;
            }
          }
          else
          {
            DEBUG_PRINT_ERROR("\nERROR: Setting Input buffer requirements failed");
            return false;
          }
        }
      }
      else if(portDefn->nPortIndex == PORT_INDEX_OUT)
      {
        if(!venc_set_encode_framerate(portDefn->format.video.xFramerate))
        {
          return false;
        }

        if(!venc_set_target_bitrate(portDefn->format.video.nBitrate))
        {
          return false;
        }

        if( (portDefn->nBufferCountActual >= m_sOutput_buff_property.mincount)
            &&
            (m_sOutput_buff_property.actualcount <= portDefn->nBufferCountActual)
            &&
            (m_sOutput_buff_property.datasize == portDefn->nBufferSize)
          )
        {
          m_sOutput_buff_property.actualcount = portDefn->nBufferCountActual;
          ioctl_msg.inputparam = (void*)&m_sOutput_buff_property;
          ioctl_msg.outputparam = NULL;
          if(ioctl (m_nDriver_fd,VEN_IOCTL_SET_OUTPUT_BUFFER_REQ,(void*)&ioctl_msg) < 0)
          {
            DEBUG_PRINT_ERROR("\nERROR: ioctl VEN_IOCTL_SET_OUTPUT_BUFFER_REQ failed");
            return false;
          }
        }
        else
        {
          DEBUG_PRINT_ERROR("\nERROR: Setting Output buffer requirements failed");
          return false;
        }
      }
      else
      {
        DEBUG_PRINT_ERROR("\nERROR: Invalid Port Index for OMX_IndexParamPortDefinition");
      }
      break;
    }
  case OMX_IndexParamVideoPortFormat:
    {
      OMX_VIDEO_PARAM_PORTFORMATTYPE *portFmt;
      portFmt =(OMX_VIDEO_PARAM_PORTFORMATTYPE *)paramData;
      DEBUG_PRINT_LOW("venc_set_param: OMX_IndexParamVideoPortFormat\n");

      if(portFmt->nPortIndex == (OMX_U32) PORT_INDEX_IN)
      {
        if(!venc_set_color_format(portFmt->eColorFormat))
        {
          return false;
        }
      }
      else if(portFmt->nPortIndex == (OMX_U32) PORT_INDEX_OUT)
      {
        if(!venc_set_encode_framerate(portFmt->xFramerate))
        {
          return false;
        }
      }
      else
      {
        DEBUG_PRINT_ERROR("\nERROR: Invalid Port Index for OMX_IndexParamVideoPortFormat");
      }
      break;
    }
  case OMX_IndexParamVideoBitrate:
    {
      OMX_VIDEO_PARAM_BITRATETYPE* pParam;
      pParam = (OMX_VIDEO_PARAM_BITRATETYPE*)paramData;
      DEBUG_PRINT_LOW("venc_set_param: OMX_IndexParamVideoBitrate\n");

      if(pParam->nPortIndex == (OMX_U32) PORT_INDEX_OUT)
      {
        if(!venc_set_target_bitrate(pParam->nTargetBitrate))
        {
          DEBUG_PRINT_ERROR("\nERROR: Target Bit Rate setting failed");
          return false;
        }
        if(!venc_set_ratectrl_cfg(pParam->eControlRate))
        {
          DEBUG_PRINT_ERROR("\nERROR: Rate Control setting failed");
          return false;
        }
      }
      else
      {
        DEBUG_PRINT_ERROR("\nERROR: Invalid Port Index for OMX_IndexParamVideoBitrate");
      }
      break;
    }
  case OMX_IndexParamVideoMpeg4:
    {
      OMX_VIDEO_PARAM_MPEG4TYPE* pParam;
      pParam = (OMX_VIDEO_PARAM_MPEG4TYPE*)paramData;
      DEBUG_PRINT_LOW("venc_set_param: OMX_IndexParamVideoMpeg4\n");
      if(pParam->nPortIndex == (OMX_U32) PORT_INDEX_OUT)
      {
        if(!venc_set_intra_period (pParam->nPFrames))
        {
          DEBUG_PRINT_ERROR("\nERROR: Request for setting intra period failed");
          return false;
        }

        m_profile_set = false;
        m_level_set = false;
        if(!venc_set_profile_level (pParam->eProfile, pParam->eLevel))
        {
          DEBUG_PRINT_ERROR("\nWARNING: Unsuccessful in updating Profile and level");
          return false;
        }
      }
      else
      {
        DEBUG_PRINT_ERROR("\nERROR: Invalid Port Index for OMX_IndexParamVideoMpeg4");
      }
      break;
    }
  case OMX_IndexParamVideoH263:
    {
      OMX_VIDEO_PARAM_H263TYPE* pParam = (OMX_VIDEO_PARAM_H263TYPE*)paramData;
      DEBUG_PRINT_LOW("venc_set_param: OMX_IndexParamVideoH263\n");
      if(pParam->nPortIndex == (OMX_U32) PORT_INDEX_OUT)
      {
        if(venc_set_intra_period (pParam->nPFrames) == false)
        {
          DEBUG_PRINT_ERROR("\nERROR: Request for setting intra period failed");
          return false;
        }

        m_profile_set = false;
        m_level_set = false;
        if(!venc_set_profile_level (pParam->eProfile, pParam->eLevel))
        {
          DEBUG_PRINT_ERROR("\nWARNING: Unsuccessful in updating Profile and level");
          return false;
        }
      }
      else
      {
        DEBUG_PRINT_ERROR("\nERROR: Invalid Port Index for OMX_IndexParamVideoH263");
      }
      break;
    }
  case OMX_IndexParamVideoAvc:
    {
      DEBUG_PRINT_LOW("venc_set_param:OMX_IndexParamVideoAvc\n");
      OMX_VIDEO_PARAM_AVCTYPE* pParam = (OMX_VIDEO_PARAM_AVCTYPE*)paramData;
      if(pParam->nPortIndex == (OMX_U32) PORT_INDEX_OUT)
      {
        if(venc_set_intra_period (pParam->nPFrames) == false)
        {
          DEBUG_PRINT_ERROR("\nERROR: Request for setting intra period failed");
          return false;
        }
        DEBUG_PRINT_LOW("pParam->eProfile :%d ,pParam->eLevel %d\n",
            pParam->eProfile,pParam->eLevel);

        m_profile_set = false;
        m_level_set = false;
        if(!venc_set_profile_level (pParam->eProfile,pParam->eLevel))
        {
          DEBUG_PRINT_ERROR("\nWARNING: Unsuccessful in updating Profile and level");
          return false;
        }
      }
      else
      {
        DEBUG_PRINT_ERROR("\nERROR: Invalid Port Index for OMX_IndexParamVideoAvc");
      }
      //TBD, lot of other variables to be updated, yet to decide
      break;
    }
  case OMX_IndexParamVideoProfileLevelCurrent:
    {
      DEBUG_PRINT_LOW("venc_set_param:OMX_IndexParamVideoProfileLevelCurrent\n");
      OMX_VIDEO_PARAM_PROFILELEVELTYPE *profile_level =
      (OMX_VIDEO_PARAM_PROFILELEVELTYPE *)paramData;
      if(profile_level->nPortIndex == (OMX_U32) PORT_INDEX_OUT)
      {
        m_profile_set = false;
        m_level_set = false;
        if(!venc_set_profile_level (profile_level->eProfile,
                                   profile_level->eLevel))
        {
          DEBUG_PRINT_ERROR("\nWARNING: Unsuccessful in updating Profile and level");
          return false;
        }
      }
      else
      {
        DEBUG_PRINT_ERROR("\nERROR: Invalid Port Index for OMX_IndexParamVideoProfileLevelCurrent");
      }
      break;
    }
  case OMX_IndexParamVideoQuantization:
    {
      DEBUG_PRINT_LOW("venc_set_param:OMX_IndexParamVideoQuantization\n");
      OMX_VIDEO_PARAM_QUANTIZATIONTYPE *session_qp =
        (OMX_VIDEO_PARAM_QUANTIZATIONTYPE *)paramData;
      if(session_qp->nPortIndex == (OMX_U32) PORT_INDEX_OUT)
      {
        if(venc_set_session_qp (session_qp->nQpI,
                                session_qp->nQpP) == false)
        {
          DEBUG_PRINT_ERROR("\nERROR: Setting Session QP failed");
          return false;
        }
      }
      else
      {
        DEBUG_PRINT_ERROR("\nERROR: Invalid Port Index for OMX_IndexParamVideoQuantization");
      }
      break;
    }
  case OMX_IndexParamVideoSliceFMO:
    {
      DEBUG_PRINT_LOW("venc_set_param:OMX_IndexParamVideoSliceFMO\n");
      OMX_VIDEO_PARAM_AVCSLICEFMO *avc_slice_fmo =
        (OMX_VIDEO_PARAM_AVCSLICEFMO*)paramData;
      DEBUG_PRINT_LOW("\n portindex = %u", avc_slice_fmo->nPortIndex);
      if(avc_slice_fmo->nPortIndex == (OMX_U32) PORT_INDEX_OUT)
      {
        if(venc_set_multislice_cfg(avc_slice_fmo->eSliceMode) == false)
        {
          DEBUG_PRINT_ERROR("\nERROR: Setting Multislice cfg failed");
          return false;
        }
      }
      else
      {
        DEBUG_PRINT_ERROR("\nERROR: Invalid Port Index for Multislice cfg");
      }
      break;
    }
  default:
	  DEBUG_PRINT_ERROR("\nERROR: Unsupported parameter in venc_set_param: %u",
      index);
    break;
    //case
  }

  return true;
}

bool venc_dev::venc_set_config(void *configData, OMX_INDEXTYPE index)
{
  venc_ioctl_msg ioctl_msg = {NULL,NULL};
  DEBUG_PRINT_LOW("\n Inside venc_set_config");

  switch(index)
  {
  case OMX_IndexConfigVideoBitrate:
    {
      OMX_VIDEO_CONFIG_BITRATETYPE *bit_rate = (OMX_VIDEO_CONFIG_BITRATETYPE *)
        configData;
      DEBUG_PRINT_LOW("\n venc_set_config: OMX_IndexConfigVideoBitrate");
      if(bit_rate->nPortIndex == (OMX_U32)PORT_INDEX_OUT)
      {
        if(venc_set_target_bitrate(bit_rate->nEncodeBitrate) == false)
        {
          DEBUG_PRINT_ERROR("\nERROR: Setting Target Bit rate failed");
          return false;
        }
      }
      else
      {
        DEBUG_PRINT_ERROR("\nERROR: Invalid Port Index for OMX_IndexConfigVideoBitrate");
      }
      break;
    }
  case OMX_IndexConfigVideoFramerate:
    {
      OMX_CONFIG_FRAMERATETYPE *frame_rate = (OMX_CONFIG_FRAMERATETYPE *)
        configData;
      DEBUG_PRINT_LOW("\n venc_set_config: OMX_IndexConfigVideoFramerate");
      if(frame_rate->nPortIndex == (OMX_U32)PORT_INDEX_OUT)
      {
        if(venc_set_encode_framerate(frame_rate->xEncodeFramerate) == false)
        {
          DEBUG_PRINT_ERROR("\nERROR: Setting Encode Framerate failed");
          return false;
        }
      }
      else
      {
        DEBUG_PRINT_ERROR("\nERROR: Invalid Port Index for OMX_IndexConfigVideoFramerate");
      }
      break;
    }
  case OMX_IndexConfigVideoIntraVOPRefresh:
    {
      OMX_CONFIG_INTRAREFRESHVOPTYPE *intra_vop_refresh = (OMX_CONFIG_INTRAREFRESHVOPTYPE *)
        configData;
      DEBUG_PRINT_LOW("\n venc_set_config: OMX_IndexConfigVideoIntraVOPRefresh");
      if(intra_vop_refresh->nPortIndex == (OMX_U32)PORT_INDEX_OUT)
      {
        if(venc_set_intra_vop_refresh(intra_vop_refresh->IntraRefreshVOP) == false)
        {
          DEBUG_PRINT_ERROR("\nERROR: Setting Encode Framerate failed");
          return false;
        }
      }
      else
      {
        DEBUG_PRINT_ERROR("\nERROR: Invalid Port Index for OMX_IndexConfigVideoFramerate");
      }
      break;
    }
  default:
    DEBUG_PRINT_ERROR("\n Unsupported config index = %u", index);
    break;
  }

  return true;
}

unsigned venc_dev::venc_stop( void)
{
  return ioctl(m_nDriver_fd,VEN_IOCTL_CMD_STOP,NULL);
}

unsigned venc_dev::venc_pause(void)
{
  return ioctl(m_nDriver_fd,VEN_IOCTL_CMD_PAUSE,NULL);
}

unsigned venc_dev::venc_resume(void)
{
  return ioctl(m_nDriver_fd,VEN_IOCTL_CMD_RESUME,NULL) ;
}

unsigned venc_dev::venc_start(void)
{
  DEBUG_PRINT_HIGH("\n %s(): Check Profile/Level set in driver before start",
        __func__);
  if (!venc_set_profile_level(0, 0))
  {
    DEBUG_PRINT_ERROR("\n ERROR: %s(): Driver Profile/Level is NOT SET",
      __func__);
  }
  else
  {
    DEBUG_PRINT_HIGH("\n %s(): Driver Profile[%lu]/Level[%lu] successfully SET",
      __func__, codec_profile.profile, profile_level.level);
  }

  return ioctl(m_nDriver_fd, VEN_IOCTL_CMD_START, NULL);
}

unsigned venc_dev::venc_flush( unsigned port)
{
  struct venc_ioctl_msg ioctl_msg;
  struct venc_bufferflush buffer_index;

  if(port == PORT_INDEX_IN)
  {
    buffer_index.flush_mode = VEN_FLUSH_INPUT;
    ioctl_msg.inputparam = (void*)&buffer_index;
    ioctl_msg.outputparam = NULL;

    return ioctl (m_nDriver_fd,VEN_IOCTL_CMD_FLUSH,(void*)&ioctl_msg);
  }
  else if(port == PORT_INDEX_OUT)
  {
    buffer_index.flush_mode = VEN_FLUSH_OUTPUT;
    ioctl_msg.inputparam = (void*)&buffer_index;
    ioctl_msg.outputparam = NULL;
    return ioctl (m_nDriver_fd,VEN_IOCTL_CMD_FLUSH,(void*)&ioctl_msg);
  }
  else
  {
    return -1;
  }
}

//allocating I/P memory from pmem and register with the device


bool venc_dev::venc_use_buf(void *buf_addr, unsigned port)
{
  struct venc_ioctl_msg ioctl_msg = {NULL,NULL};
  struct pmem *pmem_tmp;
  struct venc_bufferpayload dev_buffer = {0};

  pmem_tmp = (struct pmem *)buf_addr;

  DEBUG_PRINT_LOW("\n venc_use_buf:: pmem_tmp = %p", pmem_tmp);

  if(port == PORT_INDEX_IN)
  {
    dev_buffer.pbuffer = (OMX_U8 *)pmem_tmp->buffer;
    dev_buffer.fd  = pmem_tmp->fd;
    dev_buffer.maped_size = pmem_tmp->size;
    dev_buffer.nsize = pmem_tmp->size;
    dev_buffer.offset = pmem_tmp->offset;
    ioctl_msg.inputparam  = (void*)&dev_buffer;
    ioctl_msg.outputparam = NULL;

    DEBUG_PRINT_LOW("\n venc_use_buf:pbuffer = %x,fd = %x, offset = %d, maped_size = %d", \
                dev_buffer.pbuffer, \
                dev_buffer.fd, \
                dev_buffer.offset, \
                dev_buffer.maped_size);

    if(ioctl (m_nDriver_fd,VEN_IOCTL_SET_INPUT_BUFFER,&ioctl_msg) < 0)
    {
      DEBUG_PRINT_ERROR("\nERROR: venc_use_buf:set input buffer failed ");
      return false;
    }
  }
  else if(port == PORT_INDEX_OUT)
  {
    dev_buffer.pbuffer = (OMX_U8 *)pmem_tmp->buffer;
    dev_buffer.fd  = pmem_tmp->fd;
    dev_buffer.nsize = pmem_tmp->size;
    dev_buffer.maped_size = pmem_tmp->size;
    dev_buffer.offset = pmem_tmp->offset;
    ioctl_msg.inputparam  = (void*)&dev_buffer;
    ioctl_msg.outputparam = NULL;

    DEBUG_PRINT_LOW("\n venc_use_buf:pbuffer = %x,fd = %x, offset = %d, maped_size = %d", \
                dev_buffer.pbuffer, \
                dev_buffer.fd, \
                dev_buffer.offset, \
                dev_buffer.maped_size);

    if(ioctl (m_nDriver_fd,VEN_IOCTL_SET_OUTPUT_BUFFER,&ioctl_msg) < 0)
    {
      DEBUG_PRINT_ERROR("\nERROR: venc_use_buf:set output buffer failed ");
      return false;
    }
  }
  else
  {
    DEBUG_PRINT_ERROR("\nERROR: venc_use_buf:Invalid Port Index ");
    return false;
  }

  return true;
}

bool venc_dev::venc_free_buf(void *buf_addr, unsigned port)
{
  struct venc_ioctl_msg ioctl_msg = {NULL,NULL};
  struct pmem *pmem_tmp;
  struct venc_bufferpayload dev_buffer = {0};

  pmem_tmp = (struct pmem *)buf_addr;

  DEBUG_PRINT_LOW("\n venc_use_buf:: pmem_tmp = %p", pmem_tmp);

  if(port == PORT_INDEX_IN)
  {
    dev_buffer.pbuffer = (OMX_U8 *)pmem_tmp->buffer;
    dev_buffer.fd  = pmem_tmp->fd;
    dev_buffer.maped_size = pmem_tmp->size;
    dev_buffer.nsize = pmem_tmp->size;
    dev_buffer.offset = pmem_tmp->offset;
    ioctl_msg.inputparam  = (void*)&dev_buffer;
    ioctl_msg.outputparam = NULL;

    DEBUG_PRINT_LOW("\n venc_free_buf:pbuffer = %x,fd = %x, offset = %d, maped_size = %d", \
                dev_buffer.pbuffer, \
                dev_buffer.fd, \
                dev_buffer.offset, \
                dev_buffer.maped_size);

    if(ioctl (m_nDriver_fd,VEN_IOCTL_CMD_FREE_INPUT_BUFFER,&ioctl_msg) < 0)
    {
      DEBUG_PRINT_ERROR("\nERROR: venc_free_buf: free input buffer failed ");
      return false;
    }
  }
  else if(port == PORT_INDEX_OUT)
  {
    dev_buffer.pbuffer = (OMX_U8 *)pmem_tmp->buffer;
    dev_buffer.fd  = pmem_tmp->fd;
    dev_buffer.nsize = pmem_tmp->size;
    dev_buffer.maped_size = pmem_tmp->size;
    dev_buffer.offset = pmem_tmp->offset;
    ioctl_msg.inputparam  = (void*)&dev_buffer;
    ioctl_msg.outputparam = NULL;

    DEBUG_PRINT_LOW("\n venc_free_buf:pbuffer = %x,fd = %x, offset = %d, maped_size = %d", \
                dev_buffer.pbuffer, \
                dev_buffer.fd, \
                dev_buffer.offset, \
                dev_buffer.maped_size);

    if(ioctl (m_nDriver_fd,VEN_IOCTL_CMD_FREE_OUTPUT_BUFFER,&ioctl_msg) < 0)
    {
      DEBUG_PRINT_ERROR("\nERROR: venc_free_buf: free output buffer failed ");
      return false;
    }
  }
  else
  {
    DEBUG_PRINT_ERROR("\nERROR: venc_free_buf:Invalid Port Index ");
    return false;
  }

  return true;
}

bool venc_dev::venc_empty_buf(void *buffer, void *pmem_data_buf)
{
  struct venc_buffer frameinfo;
  struct pmem *temp_buffer;
  struct venc_ioctl_msg ioctl_msg;
  struct OMX_BUFFERHEADERTYPE *bufhdr;

  if(buffer == NULL)
  {
    DEBUG_PRINT_ERROR("\nERROR: venc_etb: buffer is NULL");
    return false;
  }
  bufhdr = (OMX_BUFFERHEADERTYPE *)buffer;

  DEBUG_PRINT_LOW("\n Input buffer length %d",bufhdr->nFilledLen);

  if(pmem_data_buf)
  {
    DEBUG_PRINT_LOW("\n Internal PMEM addr for i/p Heap UseBuf: %p", pmem_data_buf);
    frameinfo.ptrbuffer = (OMX_U8 *)pmem_data_buf;
  }
  else
  {
    DEBUG_PRINT_LOW("\n Shared PMEM addr for i/p PMEM UseBuf/AllocateBuf: %p", bufhdr->pBuffer);
    frameinfo.ptrbuffer = (OMX_U8 *)bufhdr->pBuffer;
  }

  frameinfo.clientdata = (void *) buffer;
  frameinfo.size = bufhdr->nFilledLen;
  frameinfo.len = bufhdr->nFilledLen;
  frameinfo.flags = bufhdr->nFlags;
  frameinfo.offset = bufhdr->nOffset;
  frameinfo.timestamp = bufhdr->nTimeStamp;
  DEBUG_PRINT_LOW("\n i/p TS = %u", (OMX_U32)frameinfo.timestamp);
  ioctl_msg.inputparam = &frameinfo;
  ioctl_msg.outputparam = NULL;

  DEBUG_PRINT_LOW("DBG: i/p frameinfo: bufhdr->pBuffer = %p, ptrbuffer = %p, offset = %u, len = %u",
      bufhdr->pBuffer, frameinfo.ptrbuffer, frameinfo.offset, frameinfo.len);
  if(ioctl(m_nDriver_fd,VEN_IOCTL_CMD_ENCODE_FRAME,&ioctl_msg) < 0)
  {
    /*Generate an async error and move to invalid state*/
    return false;
  }

  return true;
}
bool venc_dev::venc_fill_buf(void *buffer, void *pmem_data_buf)
{
  struct venc_ioctl_msg ioctl_msg = {NULL,NULL};
  struct pmem *temp_buffer = NULL;
  struct venc_buffer  frameinfo;
  struct OMX_BUFFERHEADERTYPE *bufhdr;

  if(buffer == NULL)
  {
    return false;
  }
  bufhdr = (OMX_BUFFERHEADERTYPE *)buffer;

  if(pmem_data_buf)
  {
    DEBUG_PRINT_LOW("\n Internal PMEM addr for o/p Heap UseBuf: %p", pmem_data_buf);
    frameinfo.ptrbuffer = (OMX_U8 *)pmem_data_buf;
  }
  else
  {
    DEBUG_PRINT_LOW("\n Shared PMEM addr for o/p PMEM UseBuf/AllocateBuf: %p", bufhdr->pBuffer);
    frameinfo.ptrbuffer = (OMX_U8 *)bufhdr->pBuffer;
  }

  frameinfo.clientdata = buffer;
  frameinfo.size = bufhdr->nAllocLen;
  frameinfo.flags = bufhdr->nFlags;
  frameinfo.offset = bufhdr->nOffset;

  ioctl_msg.inputparam = &frameinfo;
  ioctl_msg.outputparam = NULL;
  DEBUG_PRINT_LOW("DBG: o/p frameinfo: bufhdr->pBuffer = %p, ptrbuffer = %p, offset = %u, len = %u",
      bufhdr->pBuffer, frameinfo.ptrbuffer, frameinfo.offset, frameinfo.len);
  if(ioctl (m_nDriver_fd,VEN_IOCTL_CMD_FILL_OUTPUT_BUFFER,&ioctl_msg) < 0)
  {
    DEBUG_PRINT_ERROR("\nERROR: ioctl VEN_IOCTL_CMD_FILL_OUTPUT_BUFFER failed");
    return false;
  }

  return true;
}

bool venc_dev::venc_set_session_qp(OMX_U32 i_frame_qp, OMX_U32 p_frame_qp)
{
  venc_ioctl_msg ioctl_msg = {NULL,NULL};
  struct venc_sessionqp qp = {0, 0};
  DEBUG_PRINT_LOW("venc_set_session_qp:: i_frame_qp = %d, p_frame_qp = %d", i_frame_qp,
    p_frame_qp);

  qp.iframeqp = i_frame_qp;
  qp.pframqp = p_frame_qp;

  ioctl_msg.inputparam = (void*)&qp;
  ioctl_msg.outputparam = NULL;
  if(ioctl (m_nDriver_fd,VEN_IOCTL_SET_SESSION_QP,(void*)&ioctl_msg)< 0)
  {
    DEBUG_PRINT_ERROR("\nERROR: Request for setting session qp failed");
    return false;
  }

  session_qp.iframeqp = i_frame_qp;
  session_qp.pframqp = p_frame_qp;

  return true;
}

bool venc_dev::venc_set_profile_level(OMX_U32 eProfile,OMX_U32 eLevel)
{
  venc_ioctl_msg ioctl_msg = {NULL,NULL};
  struct venc_profile requested_profile;
  struct ven_profilelevel requested_level;
  unsigned const int *profile_tbl = NULL;
  unsigned long mb_per_frame = 0, mb_per_sec = 0;
  DEBUG_PRINT_LOW("venc_set_profile_level:: eProfile = %d, Level = %d",
    eProfile, eLevel);

  if((eProfile == 0) && (eLevel == 0) && m_profile_set && m_level_set)
  {
    DEBUG_PRINT_LOW("\n Profile/Level setting complete before venc_start");
    return true;
  }

  DEBUG_PRINT_LOW("\n Validating Profile/Level from table");
  if(!venc_validate_profile_level(&eProfile, &eLevel))
  {
    DEBUG_PRINT_LOW("\nERROR: Profile/Level validation failed");
    return false;
  }

  if(m_sVenc_cfg.codectype == VEN_CODEC_MPEG4)
  {
    DEBUG_PRINT_LOW("eProfile = %d, OMX_VIDEO_MPEG4ProfileSimple = %d and "
      "OMX_VIDEO_MPEG4ProfileAdvancedSimple = %d", eProfile,
      OMX_VIDEO_MPEG4ProfileSimple, OMX_VIDEO_MPEG4ProfileAdvancedSimple);
    if(eProfile == OMX_VIDEO_MPEG4ProfileSimple)
    {
      requested_profile.profile = VEN_PROFILE_MPEG4_SP;
      profile_tbl = (unsigned int const *)
          (&mpeg4_profile_level_table[MPEG4_SP_START]);
      profile_tbl += MPEG4_720P_LEVEL*5;
    }
    else if(eProfile == OMX_VIDEO_MPEG4ProfileAdvancedSimple)
    {
      requested_profile.profile = VEN_PROFILE_MPEG4_ASP;
      profile_tbl = (unsigned int const *)
          (&mpeg4_profile_level_table[MPEG4_ASP_START]);
      profile_tbl += MPEG4_720P_LEVEL*5;
    }
    else
    {
      DEBUG_PRINT_LOW("\nERROR: Unsupported MPEG4 profile = %u",
        eProfile);
      return false;
    }

    DEBUG_PRINT_LOW("eLevel = %d, OMX_VIDEO_MPEG4Level0 = %d, OMX_VIDEO_MPEG4Level1 = %d,"
      "OMX_VIDEO_MPEG4Level2 = %d, OMX_VIDEO_MPEG4Level3 = %d, OMX_VIDEO_MPEG4Level4 = %d,"
      "OMX_VIDEO_MPEG4Level5 = %d", eLevel, OMX_VIDEO_MPEG4Level0, OMX_VIDEO_MPEG4Level1,
      OMX_VIDEO_MPEG4Level2, OMX_VIDEO_MPEG4Level3, OMX_VIDEO_MPEG4Level4, OMX_VIDEO_MPEG4Level5);

    switch(eLevel)
    {
    case OMX_VIDEO_MPEG4Level0:
      requested_level.level = VEN_LEVEL_MPEG4_0;
      break;
    case OMX_VIDEO_MPEG4Level1:
      requested_level.level = VEN_LEVEL_MPEG4_1;
      break;
    case OMX_VIDEO_MPEG4Level2:
      requested_level.level = VEN_LEVEL_MPEG4_2;
      break;
    case OMX_VIDEO_MPEG4Level3:
      requested_level.level = VEN_LEVEL_MPEG4_3;
      break;
    case OMX_VIDEO_MPEG4Level4a:
      requested_level.level = VEN_LEVEL_MPEG4_4;
      break;
    case OMX_VIDEO_MPEG4Level5:
      mb_per_frame = ((m_sVenc_cfg.input_height + 15) >> 4)*
                        ((m_sVenc_cfg.input_width + 15) >> 4);
      mb_per_sec = mb_per_frame * (m_sVenc_cfg.fps_num / m_sVenc_cfg.fps_den);

      if((mb_per_frame >= profile_tbl[0]) &&
         (mb_per_sec >= profile_tbl[1]))
      {
        DEBUG_PRINT_LOW("\nMPEG4 Level 6 is set for 720p resolution");
        requested_level.level = VEN_LEVEL_MPEG4_6;
      }
      else
      {
        DEBUG_PRINT_LOW("\nMPEG4 Level 5 is set for non-720p resolution");
        requested_level.level = VEN_LEVEL_MPEG4_5;
      }
      break;
    default:
      return false;
      // TODO update corresponding levels for MPEG4_LEVEL_3b,MPEG4_LEVEL_6
      break;
    }
  }
  else if(m_sVenc_cfg.codectype == VEN_CODEC_H263)
  {
    if(eProfile == OMX_VIDEO_H263ProfileBaseline)
    {
      requested_profile.profile = VEN_PROFILE_H263_BASELINE;
    }
    else
    {
      DEBUG_PRINT_LOW("\nERROR: Unsupported H.263 profile = %u",
        requested_profile.profile);
      return false;
    }
    //profile level
    switch(eLevel)
    {
    case OMX_VIDEO_H263Level10:
      requested_level.level = VEN_LEVEL_H263_10;
      break;
    case OMX_VIDEO_H263Level20:
      requested_level.level = VEN_LEVEL_H263_20;
      break;
    case OMX_VIDEO_H263Level30:
      requested_level.level = VEN_LEVEL_H263_30;
      break;
    case OMX_VIDEO_H263Level40:
      requested_level.level = VEN_LEVEL_H263_40;
      break;
    case OMX_VIDEO_H263Level45:
      requested_level.level = VEN_LEVEL_H263_45;
      break;
    case OMX_VIDEO_H263Level50:
      requested_level.level = VEN_LEVEL_H263_50;
      break;
    case OMX_VIDEO_H263Level60:
      requested_level.level = VEN_LEVEL_H263_60;
      break;
    case OMX_VIDEO_H263Level70:
      requested_level.level = VEN_LEVEL_H263_70;
      break;
    default:
      return false;
      break;
    }
  }
  else if(m_sVenc_cfg.codectype == VEN_CODEC_H264)
  {
    if(eProfile == OMX_VIDEO_AVCProfileBaseline)
    {
      requested_profile.profile = VEN_PROFILE_H264_BASELINE;
    }
    else if(eProfile == OMX_VIDEO_AVCProfileMain)
    {
      requested_profile.profile = VEN_PROFILE_H264_MAIN;
    }
    else if(eProfile == OMX_VIDEO_AVCProfileHigh)
    {
      requested_profile.profile = VEN_PROFILE_H264_HIGH;
    }
    else
    {
      DEBUG_PRINT_LOW("\nERROR: Unsupported H.264 profile = %u",
        requested_profile.profile);
      return false;
    }
    //profile level
    switch(eLevel)
    {
    case OMX_VIDEO_AVCLevel1:
      requested_level.level = VEN_LEVEL_H264_1;
      break;
    case OMX_VIDEO_AVCLevel1b:
      requested_level.level = VEN_LEVEL_H264_1b;
      break;
    case OMX_VIDEO_AVCLevel11:
      requested_level.level = VEN_LEVEL_H264_1p1;
      break;
    case OMX_VIDEO_AVCLevel12:
      requested_level.level = VEN_LEVEL_H264_1p2;
      break;
    case OMX_VIDEO_AVCLevel13:
      requested_level.level = VEN_LEVEL_H264_1p3;
      break;
    case OMX_VIDEO_AVCLevel2:
      requested_level.level = VEN_LEVEL_H264_2;
      break;
    case OMX_VIDEO_AVCLevel21:
      requested_level.level = VEN_LEVEL_H264_2p1;
      break;
    case OMX_VIDEO_AVCLevel22:
      requested_level.level = VEN_LEVEL_H264_2p2;
      break;
    case OMX_VIDEO_AVCLevel3:
      requested_level.level = VEN_LEVEL_H264_3;
      break;
    case OMX_VIDEO_AVCLevel31:
      requested_level.level = VEN_LEVEL_H264_3p1;
      break;
    default :
      return false;
      break;
    }
  }

  if(!m_profile_set)
  {
    ioctl_msg.inputparam = (void*)&requested_profile;
    ioctl_msg.outputparam = NULL;
    if(ioctl (m_nDriver_fd,VEN_IOCTL_SET_CODEC_PROFILE,(void*)&ioctl_msg)< 0)
    {
      DEBUG_PRINT_LOW("\nERROR: Request for setting profile failed");
      return false;
    }
    codec_profile.profile = requested_profile.profile;
    m_profile_set = true;
  }

  if(!m_level_set)
  {
    ioctl_msg.inputparam = (void*)&requested_level;
    ioctl_msg.outputparam = NULL;
    if(ioctl (m_nDriver_fd,VEN_IOCTL_SET_PROFILE_LEVEL,(void*)&ioctl_msg)< 0)
    {
      DEBUG_PRINT_LOW("\nERROR: Request for setting profile level failed");
      return false;
    }
    profile_level.level = requested_level.level;
    m_level_set = true;
  }

  return true;
}

bool venc_dev::venc_set_intra_period(OMX_U32 nPFrames)
{
  venc_ioctl_msg ioctl_msg = {NULL,NULL};
  struct venc_intraperiod intra_period;

  DEBUG_PRINT_LOW("\n venc_set_intra_period: nPFrames = %u",
    nPFrames);
  intra_period.num_pframes = nPFrames;
  ioctl_msg.inputparam = (void*)&intra_period;
  ioctl_msg.outputparam = NULL;
  if(ioctl (m_nDriver_fd,VEN_IOCTL_SET_INTRA_PERIOD,(void*)&ioctl_msg)< 0)
  {
    DEBUG_PRINT_ERROR("\nERROR: Request for setting intra period failed");
    return false;
  }

  return true;
}

bool venc_dev::venc_set_target_bitrate(OMX_U32 nTargetBitrate)
{
  venc_ioctl_msg ioctl_msg = {NULL, NULL};
  struct venc_targetbitrate bit_rate;

  DEBUG_PRINT_LOW("\n venc_set_target_bitrate: bitrate = %u",
    nTargetBitrate);
  bit_rate.target_bitrate = nTargetBitrate ;
  ioctl_msg.inputparam = (void*)&bit_rate;
  ioctl_msg.outputparam = NULL;
  if(ioctl (m_nDriver_fd,VEN_IOCTL_SET_TARGET_BITRATE,(void*)&ioctl_msg) < 0)
  {
    DEBUG_PRINT_ERROR("\nERROR: Request for setting bit rate failed");
    return false;
  }
  m_sVenc_cfg.targetbitrate = nTargetBitrate;
  m_level_set = false;
  if(venc_set_profile_level(0, 0))
  {
    DEBUG_PRINT_HIGH("\n %s(): Dynamic Profile/Level setting success",
        __func__);
  }

  return true;
}

bool venc_dev::venc_set_encode_framerate(OMX_U32 encode_framerate)
{
  venc_ioctl_msg ioctl_msg = {NULL, NULL};
  struct venc_framerate frame_rate;

  DEBUG_PRINT_LOW("\n venc_set_encode_framerate: framerate(Q16) = %u",
    encode_framerate);
  frame_rate.fps_numerator = 30;
  if((encode_framerate >> 16)== 30)
  {
    frame_rate.fps_denominator = 1;
  }
  else if((encode_framerate >>16) == 15)
  {
    frame_rate.fps_denominator = 2;
  }
  else if((encode_framerate >> 16)== 7.5)
  {
    frame_rate.fps_denominator = 4;
  }
  else
  {
    frame_rate.fps_denominator = 1;
  }

  ioctl_msg.inputparam = (void*)&frame_rate;
  ioctl_msg.outputparam = NULL;
  if(ioctl(m_nDriver_fd, VEN_IOCTL_SET_FRAME_RATE,
      (void*)&ioctl_msg) < 0)
  {
    DEBUG_PRINT_ERROR("\nERROR: Request for setting framerate failed");
    return false;
  }

  m_sVenc_cfg.fps_den = frame_rate.fps_denominator;
  m_sVenc_cfg.fps_num = frame_rate.fps_numerator;
  m_level_set = false;
  if(venc_set_profile_level(0, 0))
  {
    DEBUG_PRINT_HIGH("\n %s(): Dynamic Profile/Level setting success",
        __func__);
  }

  return true;
}

bool venc_dev::venc_set_color_format(OMX_COLOR_FORMATTYPE color_format)
{
  venc_ioctl_msg ioctl_msg = {NULL, NULL};
  DEBUG_PRINT_LOW("\n venc_set_color_format: color_format = %u ", color_format);

  if(color_format == OMX_COLOR_FormatYUV420SemiPlanar)
  {
    m_sVenc_cfg.inputformat = VEN_INPUTFMT_NV12;
  }
  else
  {
    DEBUG_PRINT_ERROR("\nWARNING: Unsupported Color format [%d]", color_format);
    m_sVenc_cfg.inputformat = VEN_INPUTFMT_NV12;
    DEBUG_PRINT_HIGH("\n Default color format YUV420SemiPlanar is set");
  }
  ioctl_msg.inputparam = (void*)&m_sVenc_cfg;
  ioctl_msg.outputparam = NULL;
  if (ioctl(m_nDriver_fd, VEN_IOCTL_SET_BASE_CFG, (void*)&ioctl_msg) < 0)
  {
    DEBUG_PRINT_ERROR("\nERROR: Request for setting color format failed");
    return false;
  }
  return true;
}

bool venc_dev::venc_set_intra_vop_refresh(OMX_BOOL intra_vop_refresh)
{
  DEBUG_PRINT_LOW("\n venc_set_intra_vop_refresh: intra_vop = %uc", intra_vop_refresh);
  if(intra_vop_refresh == OMX_TRUE)
  {
    if(ioctl(m_nDriver_fd, VEN_IOCTL_CMD_REQUEST_IFRAME, NULL) < 0)
    {
      DEBUG_PRINT_ERROR("\nERROR: Request for setting Intra VOP Refresh failed");
      return false;
    }
  }
  else
  {
    DEBUG_PRINT_ERROR("\nERROR: VOP Refresh is False, no effect");
  }
  return true;
}

bool venc_dev::venc_set_ratectrl_cfg(OMX_VIDEO_CONTROLRATETYPE eControlRate)
{
  venc_ioctl_msg ioctl_msg = {NULL,NULL};
  bool status = true;

  //rate control
  switch(eControlRate)
  {
  case OMX_Video_ControlRateDisable:
    rate_ctrl.rcmode = VEN_RC_OFF;
    break;
  case OMX_Video_ControlRateVariableSkipFrames:
    rate_ctrl.rcmode = VEN_RC_VBR_VFR;
    break;
  case OMX_Video_ControlRateVariable:
    rate_ctrl.rcmode = VEN_RC_VBR_CFR;
    break;
  case OMX_Video_ControlRateConstantSkipFrames:
    rate_ctrl.rcmode = VEN_RC_CBR_VFR;
    break;
  default:
    status = false;
    break;
  }

  if(status)
  {
    ioctl_msg.inputparam = (void*)&rate_ctrl;
    ioctl_msg.outputparam = NULL;
    if(ioctl (m_nDriver_fd,VEN_IOCTL_SET_RATE_CTRL_CFG,(void*)&ioctl_msg) < 0)
    {
      DEBUG_PRINT_ERROR("\nERROR: Request for setting rate control failed");
      status = false;
    }
  }
  return status;
}

bool venc_dev::venc_get_profile_level(OMX_U32 *eProfile,OMX_U32 *eLevel)
{
  bool status = true;
  if(eProfile == NULL || eLevel == NULL)
  {
    return false;
  }

  if(m_sVenc_cfg.codectype == VEN_CODEC_MPEG4)
  {
    switch(codec_profile.profile)
    {
    case VEN_PROFILE_MPEG4_SP:
      *eProfile = OMX_VIDEO_MPEG4ProfileSimple;
      break;
    case VEN_PROFILE_MPEG4_ASP:
      *eProfile = OMX_VIDEO_MPEG4ProfileAdvancedSimple;
      break;
    default:
      *eProfile = OMX_VIDEO_MPEG4ProfileMax;
      status = false;
      break;
    }

    if(!status)
    {
      return status;
    }

    //profile level
    switch(profile_level.level)
    {
    case VEN_LEVEL_MPEG4_0:
      *eLevel = OMX_VIDEO_MPEG4Level0;
      break;
    case VEN_LEVEL_MPEG4_1:
      *eLevel = OMX_VIDEO_MPEG4Level1;
      break;
    case VEN_LEVEL_MPEG4_2:
      *eLevel = OMX_VIDEO_MPEG4Level2;
      break;
    case VEN_LEVEL_MPEG4_3:
      *eLevel = OMX_VIDEO_MPEG4Level3;
      break;
    case VEN_LEVEL_MPEG4_4:
      *eLevel = OMX_VIDEO_MPEG4Level4a;
      break;
    case VEN_LEVEL_MPEG4_5:
    case VEN_LEVEL_MPEG4_6:
      *eLevel = OMX_VIDEO_MPEG4Level5;
      break;
    default:
      *eLevel = OMX_VIDEO_MPEG4LevelMax;
      status =  false;
      break;
    }
  }
  else if(m_sVenc_cfg.codectype == VEN_CODEC_H263)
  {
    if(codec_profile.profile == VEN_PROFILE_H263_BASELINE)
    {
      *eProfile = OMX_VIDEO_H263ProfileBaseline;
    }
    else
    {
      *eProfile = OMX_VIDEO_H263ProfileMax;
      return false;
    }
    switch(profile_level.level)
    {
    case VEN_LEVEL_H263_10:
      *eLevel = OMX_VIDEO_H263Level10;
      break;
    case VEN_LEVEL_H263_20:
      *eLevel = OMX_VIDEO_H263Level20;
      break;
    case VEN_LEVEL_H263_30:
      *eLevel = OMX_VIDEO_H263Level30;
      break;
    case VEN_LEVEL_H263_40:
      *eLevel = OMX_VIDEO_H263Level40;
      break;
    case VEN_LEVEL_H263_45:
      *eLevel = OMX_VIDEO_H263Level45;
      break;
    case VEN_LEVEL_H263_50:
      *eLevel = OMX_VIDEO_H263Level50;
      break;
    case VEN_LEVEL_H263_60:
      *eLevel = OMX_VIDEO_H263Level60;
      break;
    case VEN_LEVEL_H263_70:
      *eLevel = OMX_VIDEO_H263Level70;
      break;
    default:
      *eLevel = OMX_VIDEO_H263LevelMax;
      status = false;
      break;
    }
  }
  else if(m_sVenc_cfg.codectype == VEN_CODEC_H264)
  {
    switch(codec_profile.profile)
    {
    case VEN_PROFILE_H264_BASELINE:
      *eProfile = OMX_VIDEO_AVCProfileBaseline;
      break;
    case VEN_PROFILE_H264_MAIN:
      *eProfile = OMX_VIDEO_AVCProfileMain;
      break;
    case VEN_PROFILE_H264_HIGH:
      *eProfile = OMX_VIDEO_AVCProfileHigh;
      break;
    default:
      *eProfile = OMX_VIDEO_AVCProfileMax;
      status = false;
      break;
    }

    if(!status)
    {
      return status;
    }

    switch(profile_level.level)
    {
    case VEN_LEVEL_H264_1:
      *eLevel = OMX_VIDEO_AVCLevel1;
      break;
    case VEN_LEVEL_H264_1b:
      *eLevel = OMX_VIDEO_AVCLevel1b;
      break;
    case VEN_LEVEL_H264_1p1:
      *eLevel = OMX_VIDEO_AVCLevel11;
      break;
    case VEN_LEVEL_H264_1p2:
      *eLevel = OMX_VIDEO_AVCLevel12;
      break;
    case VEN_LEVEL_H264_1p3:
      *eLevel = OMX_VIDEO_AVCLevel13;
      break;
    case VEN_LEVEL_H264_2:
      *eLevel = OMX_VIDEO_AVCLevel2;
      break;
    case VEN_LEVEL_H264_2p1:
      *eLevel = OMX_VIDEO_AVCLevel21;
      break;
    case VEN_LEVEL_H264_2p2:
      *eLevel = OMX_VIDEO_AVCLevel22;
      break;
    case VEN_LEVEL_H264_3:
      *eLevel = OMX_VIDEO_AVCLevel3;
      break;
    case VEN_LEVEL_H264_3p1:
      *eLevel = OMX_VIDEO_AVCLevel31;
      break;
    default :
      *eLevel = OMX_VIDEO_AVCLevelMax;
      status = false;
      break;
    }
  }
  return status;
}

bool venc_dev::venc_validate_profile_level(OMX_U32 *eProfile, OMX_U32 *eLevel)
{
  OMX_U32 new_profile = 0, new_level = 0;
  unsigned const int *profile_tbl = NULL;
  OMX_U32 mb_per_frame, mb_per_sec;
  bool profile_level_found = false;

  DEBUG_PRINT_LOW("\n Init profile table for respective codec");
  //validate the ht,width,fps,bitrate and set the appropriate profile and level
  if(m_sVenc_cfg.codectype == VEN_CODEC_MPEG4)
  {
      if(*eProfile == 0)
      {
        if(!m_profile_set)
        {
          *eProfile = OMX_VIDEO_MPEG4ProfileSimple;
        }
        else
        {
          switch(codec_profile.profile)
          {
          case VEN_PROFILE_MPEG4_ASP:
              *eProfile = OMX_VIDEO_MPEG4ProfileAdvancedSimple;
            break;
          case VEN_PROFILE_MPEG4_SP:
              *eProfile = OMX_VIDEO_MPEG4ProfileSimple;
            break;
          default:
            DEBUG_PRINT_LOW("\n %s(): Unknown Error", __func__);
            return false;
          }
        }
      }

      if(*eLevel == 0 && !m_level_set)
      {
        *eLevel = OMX_VIDEO_MPEG4LevelMax;
      }

      if(*eProfile == OMX_VIDEO_MPEG4ProfileSimple)
      {
        profile_tbl = (unsigned int const *)mpeg4_profile_level_table;
      }
      else if(*eProfile == OMX_VIDEO_MPEG4ProfileAdvancedSimple)
      {
        profile_tbl = (unsigned int const *)
          (&mpeg4_profile_level_table[MPEG4_ASP_START]);
      }
      else
      {
        DEBUG_PRINT_LOW("\n Unsupported MPEG4 profile type %lu", *eProfile);
        return false;
      }
  }
  else if(m_sVenc_cfg.codectype == VEN_CODEC_H264)
  {
      if(*eProfile == 0)
      {
        if(!m_profile_set)
        {
          *eProfile = OMX_VIDEO_AVCProfileBaseline;
        }
        else
        {
          switch(codec_profile.profile)
          {
          case VEN_PROFILE_H264_BASELINE:
            *eProfile = OMX_VIDEO_AVCProfileBaseline;
            break;
          case VEN_PROFILE_H264_MAIN:
            *eProfile = OMX_VIDEO_AVCProfileMain;
            break;
          case VEN_PROFILE_H264_HIGH:
            *eProfile = OMX_VIDEO_AVCProfileHigh;
            break;
          default:
            DEBUG_PRINT_LOW("\n %s(): Unknown Error", __func__);
            return false;
          }
        }
      }

      if(*eLevel == 0 && !m_level_set)
      {
        *eLevel = OMX_VIDEO_AVCLevelMax;
      }

      if(*eProfile == OMX_VIDEO_AVCProfileBaseline)
      {
        profile_tbl = (unsigned int const *)h264_profile_level_table;
      }
      else if(*eProfile == OMX_VIDEO_AVCProfileHigh)
      {
        profile_tbl = (unsigned int const *)
          (&h264_profile_level_table[H264_HP_START]);
      }
      else if(*eProfile == OMX_VIDEO_AVCProfileMain)
      {
        profile_tbl = (unsigned int const *)
          (&h264_profile_level_table[H264_MP_START]);
      }
      else
      {
        DEBUG_PRINT_LOW("\n Unsupported AVC profile type %lu", *eProfile);
        return false;
      }
  }
  else if(m_sVenc_cfg.codectype == VEN_CODEC_H263)
  {
      if(*eProfile == 0)
      {
        if(!m_profile_set)
        {
          *eProfile = OMX_VIDEO_H263ProfileBaseline;
        }
        else
        {
          switch(codec_profile.profile)
          {
          case VEN_PROFILE_H263_BASELINE:
            *eProfile = OMX_VIDEO_H263ProfileBaseline;
            break;
          default:
            DEBUG_PRINT_LOW("\n %s(): Unknown Error", __func__);
            return false;
          }
        }
      }

      if(*eLevel == 0 && !m_level_set)
      {
        *eLevel = OMX_VIDEO_H263LevelMax;
      }

      if(*eProfile == OMX_VIDEO_H263ProfileBaseline)
      {
        profile_tbl = (unsigned int const *)h263_profile_level_table;
      }
      else
      {
        DEBUG_PRINT_LOW("\n Unsupported H.263 profile type %lu", *eProfile);
        return false;
      }
  }
  else
  {
    DEBUG_PRINT_LOW("\n Invalid codec type");
    return false;
  }

  mb_per_frame = ((m_sVenc_cfg.input_height + 15) >> 4)*
                   ((m_sVenc_cfg.input_width + 15)>> 4);

  mb_per_sec = mb_per_frame * m_sVenc_cfg.fps_num / m_sVenc_cfg.fps_den;

  do{
      if(mb_per_frame <= (int)profile_tbl[0])
      {
        if(mb_per_sec <= (int)profile_tbl[1])
        {
          if(m_sVenc_cfg.targetbitrate <= (int)profile_tbl[2])
          {
              DEBUG_PRINT_LOW("\n Appropriate profile/level found \n");
              new_level = (int)profile_tbl[3];
              new_profile = (int)profile_tbl[4];
              profile_level_found = true;
              break;
          }
        }
      }
      profile_tbl = profile_tbl + 5;
  }while(profile_tbl[0] != 0);

  if ((profile_level_found != true) || (new_profile != *eProfile)
      || (new_level > *eLevel))
  {
    DEBUG_PRINT_LOW("\n ERROR: Unsupported profile/level\n");
    return false;
  }

  if((*eLevel == OMX_VIDEO_MPEG4LevelMax) || (*eLevel == OMX_VIDEO_AVCLevelMax)
     || (*eLevel == OMX_VIDEO_H263LevelMax))
  {
    *eLevel = new_level;
  }
  DEBUG_PRINT_HIGH("%s: Returning with eProfile = %lu"
      "Level = %lu", __func__, *eProfile, *eLevel);

  return true;
}
bool venc_dev::venc_set_multislice_cfg(OMX_VIDEO_AVCSLICEMODETYPE eSliceMode)
{
  venc_ioctl_msg ioctl_msg = {NULL, NULL};
  bool status = true;
  DEBUG_PRINT_LOW("\n %s(): eSliceMode = %u", __func__, eSliceMode);
  switch(eSliceMode)
  {
  case OMX_VIDEO_SLICEMODE_AVCDefault:
    DEBUG_PRINT_LOW("\n %s(): OMX_VIDEO_SLICEMODE_AVCDefault", __func__);
    multislice_cfg.mslice_mode = VEN_MSLICE_OFF;
    multislice_cfg.mslice_size = 0;
    break;
  case OMX_VIDEO_SLICEMODE_AVCMBSlice:
    DEBUG_PRINT_LOW("\n %s(): OMX_VIDEO_SLICEMODE_AVCMBSlice", __func__);
    multislice_cfg.mslice_mode = VEN_MSLICE_CNT_MB;
    multislice_cfg.mslice_size = ((m_sVenc_cfg.input_width/16) *
      (m_sVenc_cfg.input_height/16))/2;
    break;
  case OMX_VIDEO_SLICEMODE_AVCByteSlice:
    DEBUG_PRINT_LOW("\n %s(): OMX_VIDEO_SLICEMODE_AVCByteSlice", __func__);
    multislice_cfg.mslice_mode = VEN_MSLICE_CNT_BYTE;
    multislice_cfg.mslice_size = 1920;
    break;
  default:
    DEBUG_PRINT_ERROR("\n %s(): Unsupported SliceMode = %u",__func__, eSliceMode);
    status = false;
    break;
  }
  DEBUG_PRINT_LOW("\n %s(): mode = %u, size = %u", __func__, multislice_cfg.mslice_mode,
    multislice_cfg.mslice_size);

  if(status)
  {
    ioctl_msg.inputparam = (void*)&multislice_cfg;
    ioctl_msg.outputparam = NULL;
    if(ioctl (m_nDriver_fd,VEN_IOCTL_SET_MULTI_SLICE_CFG,(void*)&ioctl_msg) < 0)
    {
      DEBUG_PRINT_ERROR("\nERROR: Request for setting multi-slice cfg failed");
      status = false;
    }
  }
  return status;
}
