/*
 * Copyright (C) 2012 The CyanogenMod Project
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

#ifndef __MSM_ACDB_CONTROL
#define __MSM_ACDB_CONTROL

__BEGIN_DECLS

int acdb_loader_send_anc_cal(int id);
void acdb_loader_send_audio_cal(int id, int capability);
void acdb_loader_send_voice_cal(int tx_id, int rx_id);
void acdb_mapper_get_acdb_id_from_dev_name(char *name, int *id);
int acdb_loader_init_ACDB();
void acdb_loader_deallocate_ACDB();

__END_DECLS

#endif
