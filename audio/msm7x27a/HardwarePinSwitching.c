/*
 * Copyright (c) 2011, Code Aurora Forum. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *        * Redistributions of source code must retain the above copyright
 *            notice, this list of conditions and the following disclaimer.
 *        * Redistributions in binary form must reproduce the above copyright
 *            notice, this list of conditions and the following disclaimer in the
 *            documentation and/or other materials provided with the distribution.
 *        * Neither the name of Code Aurora nor
 *            the names of its contributors may be used to endorse or promote
 *            products derived from this software without specific prior written
 *            permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT ARE DISCLAIMED.    IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <HardwarePinSwitching.h>
#include <stdint.h>
#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

#define LOG_TAG "BTFMPinSwitching"
#include <utils/Log.h>
//#define DEBUG_CHK 1

#define BT_PCM_BCLK_MODE  0x88
#define BT_PCM_DIN_MODE   0x89
#define BT_PCM_DOUT_MODE  0x8A
#define BT_PCM_SYNC_MODE  0x8B
#define FM_I2S_SD_MODE    0x8E
#define FM_I2S_WS_MODE    0x8F
#define FM_I2S_SCK_MODE   0x90
#define I2C_PIN_CTL       0x15
#define I2C_NORMAL        0x40


#define MARIMBA_ADDR                  (0x0C)
#define ARRAY_SIZE(a)   (sizeof(a) / sizeof(*a))

#define LINE_LEN 80
#define FM_DEVICE_PATH "/dev/i2c-1"


/*==============================================================
FUNCTION:  do_rdwr
==============================================================*/

static int do_rdwr(int fd, struct i2c_msg *msgs, int nmsgs) {
   struct i2c_rdwr_ioctl_data msgset;
      msgset.msgs = msgs;
      msgset.nmsgs = nmsgs;

   if (NULL == msgs || nmsgs <= 0) {
      return -1;
   }

   if (ioctl(fd, I2C_RDWR, &msgset) < 0) {
      return -1;
   }

   return 0;
}
/*==============================================================
FUNCTION:  marimba_read
=============================================================*/
static int marimba_read(int fd, unsigned char offset, unsigned char* buf, int count)
{

        unsigned char offset_data[] =  {offset};
        struct i2c_msg msgs[2];
        msgs[0].addr = MARIMBA_ADDR;
        msgs[0].flags = 0;
        msgs[0].buf = (__u8*)offset_data;
        msgs[0].len = ARRAY_SIZE(offset_data);
        msgs[1].addr = MARIMBA_ADDR;
        msgs[1].flags = I2C_M_RD;
        msgs[1].buf = (__u8*)buf;
        msgs[1].len = count;

        return do_rdwr(fd, msgs, ARRAY_SIZE(msgs));
}
/*==============================================================
FUNCTION:  marimba_write
==============================================================*/
/**
* This function provides bus interface to write to the Marimba chip
*
* @return  int - negative number on failure.
*
*/
static int marimba_write
(
        int fd,
        unsigned char offset,
        const unsigned char* buf,
        unsigned char len
)
{
        unsigned char offset_data[((1 + len) * sizeof(unsigned char))];
        struct i2c_msg msgs[1];
              msgs[0].addr = MARIMBA_ADDR;
              msgs[0].flags = 0;
              msgs[0].buf = (__u8*)offset_data;
              msgs[0].len = (1 + len) * sizeof(*offset_data);

        if (NULL == offset_data) {
                return -1;
        }

        offset_data[0] = offset;
        memcpy(offset_data + 1, buf, len);

        return do_rdwr(fd, msgs, ARRAY_SIZE(msgs));
}

/*==============================================================
FUNCTION:  switch_pins
==============================================================*/
/**
* This function provides interface to change the mode of operation
* from I2S mode  to AUX PCM or vice versa. This function programs the
* wcn2243 registers to TRISTATE or ON mode.
*
* @return  int - negative number on failure.
*
*/
static int switch_pins( int fd, int nPath )
{
    unsigned char value =0;
    unsigned char reg =0;
    int retval = -1;
    unsigned char set = I2C_PIN_CTL; // SET PIN CTL mode
    unsigned char unset = I2C_NORMAL; // UNSET PIN CTL MODE
    if(nPath == MODE_FM ) {
        // as we need to switch path to FM we need to move
        // BT AUX PCM lines to PIN CONTROL mode then move
        // FM to normal mode.
        for( reg = BT_PCM_BCLK_MODE; reg <= BT_PCM_SYNC_MODE; reg++ ) {
#ifdef DEBUG_CHK
            retval = marimba_read(fd, reg,&value, 1);
            LOGD("value read is:%d\n",value);
#endif
            retval = marimba_write(fd, reg, &set,1);
            if (retval < 0) {
                goto err_all;
            }
        }
        for( reg = FM_I2S_SD_MODE; reg <= FM_I2S_SCK_MODE; reg++ ) {
#ifdef DEBUG_CHK
            retval = marimba_read(fd, reg,&value, 1);
            LOGD("value read is:%d\n",value);
#endif
            retval = marimba_write(fd, reg, &unset,1);
            if (retval < 0) {
               goto err_all;
            }
        }
    } else {
        // as we need to switch path to AUXPCM we need to move
        // FM I2S lines to PIN CONTROL mode then move
        // BT AUX_PCM to normal mode.
        for( reg = FM_I2S_SD_MODE; reg <= FM_I2S_SCK_MODE; reg++ ) {
#ifdef DEBUG_CHK
            retval = marimba_read(fd, reg,&value, 1);
            LOGD("value read is:%d\n",value);
#endif
            retval = marimba_write(fd, reg, &set,1);
            if (retval < 0) {
               goto err_all;
            }
        }
        for( reg = BT_PCM_BCLK_MODE; reg <= BT_PCM_SYNC_MODE; reg++ ) {
#ifdef DEBUG_CHK
            retval = marimba_read(fd, reg,&value, 1);
            LOGD("value read is:%d\n",value);
#endif
            retval = marimba_write(fd, reg, &unset,1);
            if (retval < 0) {
                goto err_all;
            }
        }
    }
    LOGD("switch pin called with : %d\n",nPath);
    return 0;

err_all:
        return retval;
}

/*==============================================================
FUNCTION:  switch_mode
==============================================================*/
/**
* This function provides interface to change the mode of operation
* from I2S mode to AUX PCM or vice versa. This function programs the
* wcn2243 registers to TRISTATE or ON mode.
*
* @return  int - negative number on failure.
*
*/
extern int switch_mode( int nMode ) {
    int i2cfd = -1, rc= -1 ;
#ifdef WITH_QCOM_FM
    i2cfd = open(FM_DEVICE_PATH, O_RDWR);
    if( i2cfd >= 0) {
        rc = switch_pins(i2cfd, nMode);
        close(i2cfd);
    }
    if( 0 != rc ) {
        LOGE("switch mode failed with error:%d",rc);
    }
#else
    LOGE("switch mode failed because QCOM_FM feature is not available");
#endif
    return rc;
}
