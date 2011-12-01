/*
 * Copyright (c) 2009, The Android Open-Source Project
 * Copyright (c) 2009-2010, Code Aurora Forum. All rights reserved.
 * Copyright (c) 2011, The CyanogenMod Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __MSM_AUDIO_ALSA_CONTROL
#define __MSM_AUDIO_ALSA_CONTROL

#include <sys/cdefs.h>

__BEGIN_DECLS

extern const char **msm_get_device_list(void);
extern int msm_mixer_count(void);
extern int msm_mixer_open(const char *name, int id);
extern void msm_mixer_close(void);
extern int msm_get_device(const char *name);
extern int msm_en_device(int device, short enable);
extern int msm_route_stream(int dir, int dec_id, int dev_id, int set);
extern int msm_route_voice(int tx, int rx, int set);
extern int msm_set_volume(int dec_id, int vol);
extern int msm_get_device_class(int dev_id);
extern int msm_get_device_capability(int dev_id);
extern int msm_get_device_count(void);
extern void msm_start_voice(void);
extern void msm_end_voice(void);
extern void msm_set_voice_tx_mute(int mute);
extern int msm_set_voice_rx_vol(int volume);
extern void msm_set_device_volume(int dev_id, int volume);
extern void msm_device_mute(int dev_id, int mute);
extern int msm_reset_all_device(void);

__END_DECLS

#endif
