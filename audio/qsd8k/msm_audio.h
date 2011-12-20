/****************************************************************************
 ****************************************************************************
 ***
 ***   This header was automatically generated from a Linux kernel header
 ***   of the same name, to make information necessary for userspace to
 ***   call into the kernel available to libc.  It contains only constants,
 ***   structures, and macros generated from the original header, and thus,
 ***   contains no copyrightable information.
 ***
 ****************************************************************************
 ****************************************************************************/
#ifndef __LINUX_MSM_AUDIO_H
#define __LINUX_MSM_AUDIO_H

#include <linux/types.h>
#include <linux/ioctl.h>
#include <asm/sizes.h>

#define AUDIO_IOCTL_MAGIC 'a'

#define AUDIO_START                 _IOW(AUDIO_IOCTL_MAGIC, 0, unsigned)
#define AUDIO_STOP                  _IOW(AUDIO_IOCTL_MAGIC, 1, unsigned)
#define AUDIO_FLUSH                 _IOW(AUDIO_IOCTL_MAGIC, 2, unsigned)
#define AUDIO_GET_CONFIG            _IOR(AUDIO_IOCTL_MAGIC, 3, unsigned)
#define AUDIO_SET_CONFIG            _IOW(AUDIO_IOCTL_MAGIC, 4, unsigned)
#define AUDIO_GET_STATS             _IOR(AUDIO_IOCTL_MAGIC, 5, unsigned)
#define AUDIO_ENABLE_AUDPP          _IOW(AUDIO_IOCTL_MAGIC, 6, unsigned)
#define AUDIO_SET_ADRC              _IOW(AUDIO_IOCTL_MAGIC, 7, unsigned)
#define AUDIO_SET_EQ                _IOW(AUDIO_IOCTL_MAGIC, 8, unsigned)
#define AUDIO_SET_RX_IIR            _IOW(AUDIO_IOCTL_MAGIC, 9, unsigned)
#define AUDIO_SET_VOLUME            _IOW(AUDIO_IOCTL_MAGIC, 10, unsigned)
#define AUDIO_ENABLE_AUDPRE         _IOW(AUDIO_IOCTL_MAGIC, 11, unsigned)
#define AUDIO_SET_AGC               _IOW(AUDIO_IOCTL_MAGIC, 12, unsigned)
#define AUDIO_SET_NS                _IOW(AUDIO_IOCTL_MAGIC, 13, unsigned)
#define AUDIO_SET_TX_IIR            _IOW(AUDIO_IOCTL_MAGIC, 14, unsigned)
#define AUDIO_SET_AAC_CONFIG        _IOW(AUDIO_IOCTL_MAGIC, 15, unsigned)
#define AUDIO_WAIT_ADSP_DONE        _IOR(AUDIO_IOCTL_MAGIC, 16, unsigned)
#define AUDIO_ADSP_PAUSE            _IOR(AUDIO_IOCTL_MAGIC, 17, unsigned)
#define AUDIO_ADSP_RESUME           _IOR(AUDIO_IOCTL_MAGIC, 18, unsigned)
#define AUDIO_PLAY_DTMF             _IOW(AUDIO_IOCTL_MAGIC, 19, unsigned)
#define AUDIO_GET_AAC_CONFIG        _IOR(AUDIO_IOCTL_MAGIC, 20, unsigned)
#define AUDIO_GET_AMRNB_ENC_CONFIG  _IOW(AUDIO_IOCTL_MAGIC, 21, unsigned)
#define AUDIO_SET_AMRNB_ENC_CONFIG  _IOR(AUDIO_IOCTL_MAGIC, 22, unsigned)
#define AUDIO_GET_PCM_CONFIG        _IOR(AUDIO_IOCTL_MAGIC, 30, unsigned)
#define AUDIO_SET_PCM_CONFIG        _IOW(AUDIO_IOCTL_MAGIC, 31, unsigned)
#define AUDIO_SWITCH_DEVICE         _IOW(AUDIO_IOCTL_MAGIC, 32, unsigned)
#define AUDIO_SET_MUTE              _IOW(AUDIO_IOCTL_MAGIC, 33, unsigned)
#define AUDIO_UPDATE_ACDB           _IOW(AUDIO_IOCTL_MAGIC, 34, unsigned)
#define AUDIO_START_VOICE           _IOW(AUDIO_IOCTL_MAGIC, 35, unsigned)
#define AUDIO_STOP_VOICE            _IOW(AUDIO_IOCTL_MAGIC, 36, unsigned)

struct msm_audio_config {
 uint32_t buffer_size;
 uint32_t buffer_count;
 uint32_t channel_count;
 uint32_t sample_rate;
 uint32_t type;
 uint32_t unused[3];
};

struct msm_audio_stats {
 uint32_t byte_count;
 uint32_t sample_count;
 uint32_t unused[2];
};

#define SND_IOCTL_MAGIC 's'

#define SND_MUTE_UNMUTED 0
#define SND_MUTE_MUTED 1

struct msm_snd_device_config {
 uint32_t device;
 uint32_t ear_mute;
 uint32_t mic_mute;
};

#define SND_SET_DEVICE _IOW(SND_IOCTL_MAGIC, 2, struct msm_device_config *)

#define SND_METHOD_VOICE 0

struct msm_snd_volume_config {
 uint32_t device;
 uint32_t method;
 uint32_t volume;
};

#define SND_SET_VOLUME _IOW(SND_IOCTL_MAGIC, 3, struct msm_snd_volume_config *)

#define SND_GET_NUM_ENDPOINTS _IOR(SND_IOCTL_MAGIC, 4, unsigned *)

struct msm_snd_endpoint {
 int id;
 char name[64];
};

#define SND_GET_ENDPOINT _IOWR(SND_IOCTL_MAGIC, 5, struct msm_snd_endpoint *)

#endif
