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

/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*
H E A D E R  FOR  L I N U X  A U T O M A T I O N  S C R I P T I N G     
                I N T E R F A C E (LASIC)

GENERAL DESCRIPTION
Listener thread interface for control by automation 

REFERENCES

EXTERNALIZED FUNCTIONS


INITIALIZATION AND SEQUENCING REQUIREMENTS
*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/

/* <EJECT> */
/*===========================================================================

EDIT HISTORY FOR MODULE

This section contains comments describing changes made to the module.
Notice that changes are listed in reverse chronological order.

$Header:

when       who      what, where, why
--------   ---      ---------------------------------------------------------- 
===========================================================================*/

/* <EJECT> */
/*===========================================================================

INCLUDE FILES FOR MODULE

===========================================================================*/
#ifndef _LASIC_CONTROL_H_ 
#define _LASIC_CONTROL_H_

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include<pthread.h>

#define LASIC_MAX_MESG_LEN 500
#define LASIC_PARSERULE_MAXARGS 7

/* <EJECT> */
/*===========================================================================

                    DEFINITIONS AND DECLARATIONS FOR MODULE

This section contains definitions for constants, macros, types, variables
and other items needed to interface with the QDSP Services module.

===========================================================================*/
/* Values for commands that can be sent from the command line */
/* These values MUST reflect the relevant commands in the Acli_token_list[] array IN THE SAME ORDER */
typedef enum {
    LASIC_CMD_INVALID_PARAMS = -2,
    LASIC_CMD_UNKNOWN = -1,
    LASIC_CMD_ILLEGAL_MIN = 0,
    LASIC_CMD_SET_CODEC,
    LASIC_CMD_PLAY,
    LASIC_CMD_STOP,
    LASIC_CMD_PAUSE,
    LASIC_CMD_FFWD,
    LASIC_CMD_RWND,
    LASIC_CMD_EXIT,
    LASIC_CMD_ABORT,
    LASIC_CMD_TOGGLE_LOOPING,
    LASIC_CMD_TOGGLE_FB_DISPLAY,
    LASIC_CMD_TOGGLE_FILE_WRITE,
    LASIC_CMD_SET_RESOLUTION,
    LASIC_CMD_GET_RESOLUTION,
    LASIC_CMD_SET_FRAMERATE,
    LASIC_CMD_GET_FRAMERATE,
    LASIC_CMD_PLAY_FROM_FILE,
    LASIC_CMD_SETPARAM,
    LASIC_CMD_GETPARAM,
    LASIC_CMD_ENCODE,
    LASIC_CMD_ENCODE_FROM_FILE,
    LASIC_CMD_SET_AUDIO_SAMPLING_RATE,
    LASIC_CMD_GET_AUDIO_SAMPLING_RATE,
    LASIC_CMD_SET_AUDIO_CHANNELS,
    LASIC_CMD_GET_AUDIO_CHANNELS,
    LASIC_CMD_SET_LOGLEVEL,
    LASIC_CMD_ILLEGAL_MAX,
} LASIC_cmd_type;

/*===========================================================================

FUNCTION LASIC_Start_Listener

DESCRIPTION
Creates listener pipe in its own thread. This should be called from the init thread of the test app
NOTE: handle_lasic_cmd is a prototype for a callback function which receives the message sent through the automation interface. 
      This callback should copy the cmd_string into its own buffer before returning.

DEPENDENCIES
None

RETURN VALUE
0 (success) or error

SIDE EFFECTS
Writes the newly created thread id into newtid
===========================================================================*/
int 
LASIC_Start_Listener(const char *namedpipe, void (*handle_lasic_cmd)(const char *cmd_string), pthread_t *newtid);

/*===========================================================================

FUNCTION 

DESCRIPTION
Parse commands entered through command line (refer lasic_control.h)

DEPENDENCIES

RETURN VALUE
The command identifier or error (eg. LASIC_CMD_UNKNOWN)

SIDE EFFECTS
Updates all parameters except mesg (which is the line to be parsed)
word1-4 : pointers to start of each of the first 4 words that occur after the command name. These pointers refer to chars within 'mesg'
len1-4 :  lengths of each of the words pointed to by word1-4 above
** NOTE : If less than 4 words follow the command then only the relevant pointers mentioned above are updated & the rest are left untouched
          word1-4 are NOT strings. They are POINTERS to places within the mesg string. Calling function can just declare 
          char pointers & pass their addresses
===========================================================================*/
LASIC_cmd_type 
LASIC_ParseCommand(const char *mesg, int *nrargs, char *words[], int *lens);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* #ifdef _LASIC_CONTROL_H_ */


