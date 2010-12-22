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
L I N U X   A U T O M A T I O N   S C R I P T I N G   I N T E R F A C E (LASIC)

GENERAL DESCRIPTION
Listener thread interface for controlling test app through automation 

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
#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include<pthread.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<fcntl.h>
#include<signal.h>
#include<unistd.h>
#include<stdlib.h>
#include<string.h>
#include<errno.h>
#include<stdio.h>
#include "lasic_control.h"

/* <EJECT> */
/*===========================================================================

DECLARATIONS FOR MODULE

===========================================================================*/
//#define lasic_DEBUG

#ifdef lasic_DEBUG
#define DEBUGPRINT printf
#else 
  #define DEBUGPRINT 
#endif


#define lasic_LOG_WRITE(lvl, err, file, func, line, str1, str2)   fprintf(stderr, "\n%d :: %d :: %s :: %s() :: Line %d :: LASIC_LOG_LEVEL_STR=%d :: %s :: %s", \
    (lvl), (err), (file), (func), (line), (lvl), (str1), (str2))
#define lasic_LOG_MESG(lvl, err, str1, str2) lasic_LOG_WRITE(lvl, err, __FILE__, __FUNCTION__, __LINE__, str1, str2)
#define lasic_PRINT printf
#define lasic_SMALLER(a,b) (a) < b ? (a) : (b)
#define lasic_TOKEN_SIZE 50

typedef struct {
    void (*handle_lasic_cmd)(const char *cmd_string);
    const char *namedpipe;
} threadarg_t;

static const char Lasic_delims[] = " =";
static const char Lasic_token_list[][lasic_TOKEN_SIZE] = {
    /* RULES for usage / changing / adding :  
    <a> Each command is a single word (eg. PLAY_FILE)
    <b> Any field (entered on the linux shell) with multiple words must be enclosed in single quotes. Eg. 'Input file name'
    ** NOTE that the Linux shell will eat unescaped spaces & even the single quotes. Hence from the linux shell
    any multi-word string must be enclosed in doublequotes also. An example command will be something like : 
    PLAY_FROM_FILE "'long file name'" AAC_DEC
    <c> Commands are described below.  
    ** NOTE (1) It is best that any settings that have a predefined range of values (that need validation) are created as 
    separate commands. Eg. display RESOLUTION in all cases will belong to a set of values like {QVGA, VGA, .... SXGA etc}. 
    Hence don't make it a generic SET_PARAM parameter name. Instead of a rule like "SET_PARAM RESOLUTION=<res>", 
    have a separate command rule like "SET_RESOLUTION <res> ". This will allow the rule to validate the permissible range of 
    values (see below) 
    ** NOTE (2) DO NOT change the position or value of the  1st & last tokens in any logical group below ie. do not change 
    any tokens of the form "NOT_TOKEN_...." since these are boundary markers
    ** NOTE (3) ALL ADDITIONS of valid tokens to be made anywhere in between the boundary marker tokens
                Also update the parse rules table appropriately for any new tokens added 
    ** NOTE (4) If updating the COMMANDS list below, also update the enum LASIC_cmd_type in the header file lasic_control.h
    ** NOTE (5) ALWAYS PUT SHORTER STRINGS BEFORE LONGER ONES if the shorter strings are contained in the longer ones
                (ie. "PLAY" should come BEFORE "PLAY_from_file" since the "PLAY_from_file" contains the string "PLAY")
    */
        "NOT_TOKEN_CMD_START",  /* Anything starting with "NOT_TOKEN" is an invalid token & just an internal marker */
        "SET_CODEC",        /* Useful when sending generic commands below to apps supporting multi codecs */
        "PLAY",             /* Generic start (eg. can be used to start Camera preview or start/continue video decode). No params */
        "STOP",             /* Generic Stop (decode/encode/camera etc). No params */
        "PAUSE",            /* Generic Pause. No params */
        "FAST_FORWARD",     /* Generic Ffwd (for audio/video) */      
        "REWIND",           /* Generic Rwd (for audio/video)*/
        "EXIT",             /* Quit app - preferably after cleanup */
        "ABORT",            /* Just call exit() */
        "TOGGLE_LOOPING",   /* For looped playback of audio/video */
        "TOGGLE_FB_DISPLAY", /* FB Display on / off (for video decode) */
        "TOGGLE_FILE_WRITE", /* Output file write on / off (for all decoders) */
        "SET_RESOLUTION",   /* Generic Set/Get resolution (will apply to components like video encode/decode, camera, display, etc */
        "GET_RESOLUTION",
        "SET_FRAMERATE",    /* Generic Set/Get framerate (video decode/encode, camera preview etc) */
        "GET_FRAMERATE",
        "PLAY_FROM_FILE",   /* PLAY_FROM_FILE <codec> <filename> */
        "SET_PARAM",         /* SET_PARAM <codec> Param_Name1=<value1> Param_Name2=<value2> Param_Name3=<value3>  --> values are set without validation */
        "GET_PARAM",         /* GET_PARAM <codec> Param_Name1 Param_Name2 Param_Name3     --> the value(s) will be printed to console */
                            /* NOTE: MAXIMUM 3 param/value pairs can be given in a single SET/GET PARAM line. More settings can be done 
                               using another SET/GET */
        "ENCODE",           /* ENCODE <codec> <output filename>  --> Generic encode (eg. encode from camera / record audio from mic or other input etc) */
        "ENCODE_FROM_FILE", /* ENCODE_FROM_FILE <Input codec> <Output codec> <Input file name> <Output file name>    --> Can be for audio/video */
        "SET_AUDIO_SAMPLING_RATE",  /* Audio sampling rate */
        "GET_AUDIO_SAMPLING_RATE", 
        "SET_AUDIO_CHANNELS",       /* Audio channels (mono/stereo etc) */
        "GET_AUDIO_CHANNELS", 
        "SET_LOGLEVEL",
        "NOT_TOKEN_CMD_END",
 /* <d> Valid codec names ... */
        /* Audio */
        "NOT_TOKEN_ACX_START", 
        "MP3_DEC", "MP3_ENC", "AAC_DEC", "AAC_ENC", "PCM_DEC", "PCM_ENC", "AMR_DEC", "AMR_ENC", 
        "NOT_TOKEN_ACX_END",
        /* Video */
        "NOT_TOKEN_VCX_START",
        "H264_DEC", "H264_ENC", "MP4_DEC", "MP4_ENC", "H263_DEC", "H263_ENC", 
        "NOT_TOKEN_VCX_END",
 /* <e> Valid parameter names (that're passed to SET_PARAM commands & whose values are not verified by LASIC) - Common */
        "NOT_TOKEN_PRM_START",
        "BITRATE",              /* Can be audio/video bitrate */
        "OUTPUT_TO_FILENAME",   /* */
        "APP_STATE",            /* INTERNAL */
        /* Valid parameter names - Video specific */
        /* Valid parameter names - Audio specific */
        "NOT_TOKEN_PRM_END",
 /* <f> Range of predefined parameter values (which can be validated using the Lasic_parse_rules below) */
        /* Resolutions */
        "NOT_TOKEN_RES_START",
        "CIF", "VGA", "XGA", "QCIF", "QVGA", "HVGA", "4CIF", "SVGA", "720pHD", "4VGA", "SXGA", "4SVGA", 
        "1080HD", "UXGA", /* etc - check and add all possible values */
        "NOT_TOKEN_RES_END",
        /* Framerates */
        "NOT_TOKEN_FRT_START",
        "15", "24", "30",
        "NOT_TOKEN_FRT_END",
        /* Audio sampling rates */
        "NOT_TOKEN_ASR_START",
        "8", "11", "16", "22", "32", "44", "48", "96", "128", /* add more here */ "256",
        "NOT_TOKEN_ASR_END",
        /* Audio channels*/
        "NOT_TOKEN_CHN_START", "MONO", "STEREO", "NOT_TOKEN_CHN_END",
        /* Logging levels */
        "NOT_TOKEN_LOGLVL_START", "DEBUG", "INFO", "WARNING", "ERROR", "NOT_TOKEN_LOGLVL_END",
        /* Genetic values */
        "NOT_TOKEN_GEN_START", "ON", "OFF", "YES", "NO", "ALL", "NONE", "NOT_TOKEN_GEN_END",
};

typedef struct pr_t {
    int cmd;
    int numargs;
    const char * const arg_minmax[14];   /* Pointers to hold min & max valid tokens for each arg in the rule */
} Lasic_parse_rule_t;
static const Lasic_parse_rule_t Lasic_parse_rules[] = {
    /* Each line is a parse rule for a particular command as detailed in the comments above for each command.
       The format is : COMMAND, MIN nr.of args, First valid token for arg1, Last valid token for arg1, ... and so on ... upto max args (currently 7) 
       A first/last valid token given as NULL indicates no token validation needed for that arg (eg. for filenames or inputs ignored by a test). 
       Both first & last valid tokens should be the same in this case */

    {LASIC_CMD_PLAY_FROM_FILE, 2,  {"NOT_TOKEN_ACX_START", "NOT_TOKEN_VCX_END", NULL, NULL}},
    {LASIC_CMD_SET_CODEC, 1, {"NOT_TOKEN_ACX_START", "NOT_TOKEN_VCX_END"}}, 
    {LASIC_CMD_PLAY, 0}, 
    {LASIC_CMD_STOP, 0},
    {LASIC_CMD_PAUSE, 0},
    {LASIC_CMD_FFWD, 0},
    {LASIC_CMD_RWND, 0},
    {LASIC_CMD_EXIT, 0},
    {LASIC_CMD_ABORT, 0},
    {LASIC_CMD_TOGGLE_LOOPING, 0},
    {LASIC_CMD_TOGGLE_FB_DISPLAY, 0},
    {LASIC_CMD_TOGGLE_FILE_WRITE, 0},
    /* 09/17/08: Don't verify parameter names (will be done in handler) */
    //{LASIC_CMD_SETPARAM, 3, {"NOT_TOKEN_ACX_START", "NOT_TOKEN_VCX_END",  "NOT_TOKEN_PRM_START", "NOT_TOKEN_PRM_END",  NULL, NULL,     /* for 1st param=value pair */
    //                         "NOT_TOKEN_PRM_START", "NOT_TOKEN_PRM_END",  NULL, NULL,  "NOT_TOKEN_PRM_START", "NOT_TOKEN_PRM_END",  NULL, NULL}}, /* for 3rd such pair */
    {LASIC_CMD_SETPARAM, 3, {"NOT_TOKEN_ACX_START", "NOT_TOKEN_VCX_END",  NULL, NULL,  NULL, NULL,     /* for 1st param=value pair */
                             NULL, NULL,  NULL, NULL,  NULL, NULL,  NULL, NULL}}, /* for 3rd such pair */
    /* 09/17/08: Don't verify parameter names (will be done in handler) */
    {LASIC_CMD_GETPARAM, 2, {"NOT_TOKEN_ACX_START", "NOT_TOKEN_VCX_END",  NULL, NULL, 
                             NULL, NULL,  NULL, NULL}},

    {LASIC_CMD_ENCODE_FROM_FILE, 4, {"NOT_TOKEN_ACX_START", "NOT_TOKEN_VCX_END",  "NOT_TOKEN_ACX_START", "NOT_TOKEN_VCX_END",  NULL, NULL,  NULL, NULL}},
    {LASIC_CMD_ENCODE, 2, {"NOT_TOKEN_ACX_START", "NOT_TOKEN_VCX_END",  NULL, NULL}},
    {LASIC_CMD_SET_RESOLUTION, 1,  {"NOT_TOKEN_RES_START", "NOT_TOKEN_RES_END"}},   /* Video Res / Camera preview Res, etc */
    {LASIC_CMD_GET_RESOLUTION, 0}, 
    {LASIC_CMD_SET_AUDIO_SAMPLING_RATE, 1, {"NOT_TOKEN_ASR_START", "NOT_TOKEN_ASR_END"}},
    {LASIC_CMD_GET_AUDIO_SAMPLING_RATE, 0},
    {LASIC_CMD_SET_AUDIO_CHANNELS, 1, {"NOT_TOKEN_CHN_START", "NOT_TOKEN_CHN_END"}},
    {LASIC_CMD_GET_AUDIO_CHANNELS, 0}, 
    {LASIC_CMD_SET_FRAMERATE, 1, {"NOT_TOKEN_FRT_START", "NOT_TOKEN_FRT_END"}}, 
    {LASIC_CMD_GET_FRAMERATE, 0},
    {LASIC_CMD_SET_LOGLEVEL, 1, {"NOT_TOKEN_LOGLVL_START", "NOT_TOKEN_LOGLVL_END"}},
};



/*===========================================================================

FUNCTION strip_newline

DESCRIPTION
Marks first newline char found as a null char
Returns ptr to the char after the first newline found in string

DEPENDENCIES

RETURN VALUE
Returns ptr to the char after the first newline found in string

SIDE EFFECTS
Marks the first newline char found as a '\0'
===========================================================================*/
static char *
strip_newline(char *str)
{
    char delim[]="\n";
    char *nl;

    if (!str) 
        return NULL;
    if((nl = strpbrk(str, delim)) != NULL) {
        *nl = '\0';
        nl += strspn(nl, delim);
    }

    return nl;
}


/*===========================================================================

FUNCTION lasic_listen

DESCRIPTION
Thread function to listen & process commands

DEPENDENCIES

RETURN VALUE

SIDE EFFECTS

===========================================================================*/
static void * 
lasic_listen(void *arg)
{
    char *namedpipe = (char *)((threadarg_t *)arg)->namedpipe;
    void (*handle_lasic_cmd)(const char *cmd_string) = ((threadarg_t *)arg)->handle_lasic_cmd;
    sigset_t threadsigset;
    int err, fd, i=0;
    size_t cnt;
    char mesg[LASIC_MAX_MESG_LEN+1], *curr, *next;
    char temp[LASIC_MAX_MESG_LEN + 300];

    temp[LASIC_MAX_MESG_LEN + 199] = '\0';
    lasic_LOG_MESG(0, 0, "", "LASIC_Listen() Thread : Entered !");
    free((threadarg_t *)arg);
    /* 05/11/09 : Commented out 
    sigfillset(&threadsigset);
    if((err=pthread_sigmask(SIG_BLOCK, &threadsigset, NULL))) {
        lasic_LOG_MESG(3, err, "pthread_sigmask() failed", "Couldn't block signals !");
    } */

    /* Main wait loop */
    while(1) {
        DEBUGPRINT("\n%s(): INFO: Wait Loop Count=%d", __FUNCTION__, i++);

        if((fd=open(namedpipe, O_RDONLY)) < 0) {
            sprintf(mesg, "Couldn't open listen Pipe for READ! fd=%d", fd);
            lasic_LOG_MESG(4, errno, strerror(errno), mesg);
            pthread_exit((void*)(&errno));
        }

        memset(mesg, sizeof(mesg), '\0');
        if((cnt = read(fd, mesg, LASIC_MAX_MESG_LEN)) < 0) {
            lasic_LOG_MESG(4, errno, strerror(errno), "read() failed on listener pipe!");
            close(fd);
            continue;
        }
        close(fd);

        DEBUGPRINT("\n\n**** LISTENER PIPE - FULL MESG : fd=%d, cnt=%d ****", fd, (int)cnt);

        /* Each command ends with a newline & there may be multiple commands in the pipe so process each */
        curr = mesg;
        while(cnt > 0 && curr != NULL) {
            int j=0;

            next = strip_newline(curr);
            if(*curr == '\0') break;
            snprintf(temp, sizeof(temp)-1, "LISTENER PIPE RECEIVED MESG[%d] : fd=%d, Len=%d, MESG=\"%s\"", ++j, fd, (int)strlen(curr), curr);
            lasic_LOG_MESG(1, 0, temp, "");
            //DEBUGPRINT("\n fd=%d, Len=%d, MESG=\"%s\"", fd, (int)strlen(curr), curr);
            /* Trigger the callback */
            handle_lasic_cmd(curr);
            curr = next;
        }
    }
    return NULL;
}



/*===========================================================================

FUNCTION LASIC_Start_Listener

DESCRIPTION
Creates listener pipe in its own thread. This should be called from the init thread of the test app
NOTE: handle_lasic_cmd is a prototype for a callback function which receives the message sent through the automation interface. 
      * This callback should copy the cmd_string into its own buffer before returning.

DEPENDENCIES
None

RETURN VALUE
0 (success) or error

SIDE EFFECTS
Writes the newly created thread id into newtid
===========================================================================*/
int 
LASIC_Start_Listener(const char *namedpipe, void (*handle_lasic_cmd)(const char *cmd_string), pthread_t *newtid)
{
    pthread_t tid;
    pthread_attr_t tattrib;
    int err;
    char errbuf[150];
    threadarg_t *arg;     

    if((arg=(threadarg_t *)malloc(sizeof(threadarg_t)))==NULL) {
        err=errno; 
        strerror_r(err, errbuf, sizeof(errbuf)-1);
        lasic_LOG_MESG(4, errno, errbuf, "malloc failed");
        return err;
    }
    else {
        arg->handle_lasic_cmd = handle_lasic_cmd;
        arg->namedpipe = namedpipe;
    }

    snprintf(errbuf, sizeof(errbuf)-1, "ls -l %s >/dev/null 2>&1", namedpipe);
    if(system(errbuf))
        if(mkfifo(namedpipe, 0777)) {
            err=errno;
            strerror_r(err, errbuf, sizeof(errbuf)-1);
            lasic_LOG_MESG(4, errno, errbuf, "Named pipe creation failed!");
            return err;
        }

    lasic_LOG_MESG(0, 0, "Named pipe exists or created !", "");

    if((err = pthread_attr_init(&tattrib)) != 0 || /*
       (err = pthread_attr_setinheritsched(&tattrib, PTHREAD_INHERIT_SCHED)) != 0 || */
       // (err = pthread_attr_setdetachstate(&tattrib, PTHREAD_CREATE_DETACHED)) != 0 || 
       (err = pthread_create(&tid, &tattrib, lasic_listen, (void *)arg)) != 0) {
            lasic_LOG_WRITE(4, err, __FILE__, __FUNCTION__, __LINE__, "pthread allocation failed !","");
            return err;
    }
    lasic_LOG_WRITE(1, 0, __FILE__, __FUNCTION__, __LINE__, "SUCCESS", "pthread allocation done !");

    *newtid=tid;
    return 0;
}



/*===========================================================================

FUNCTION Lasic_Generic_Search_2DArray

DESCRIPTION
Generic search for an element in an array of elements (strings / integers etc)
Parameters:
    arr : Array to search in
    srchfor : element to search for
    srchforsize : size of srchfor
    nelem : Nr. of elements in arr
    elemsize : size of each element in arr
    how : if set to 0, it will do a case insensitive compare for strings

DEPENDENCIES

RETURN VALUE
Index into arr or -1 (error)

SIDE EFFECTS

===========================================================================*/
static int 
Lasic_Generic_Search_2DArray(const void *arr, const void *srchfor, unsigned int srchforsize, unsigned int nelem, unsigned int elemsize, int how)
{
  // GCC allows this typecast but wonder if it's portable to MSM unless gcc is used to build target...
    char (*cabinet)[nelem][elemsize] = (char(*)[nelem][elemsize])arr;
    char *drawer = (char *)srchfor;
    int index;
    size_t numbytes = (elemsize < srchforsize ? elemsize : srchforsize);

    DEBUGPRINT("\n%s(): ENTERED. srchforsize=%d, numelem=%d, elemsize=%d, bytes2cmp=%d, srchstr='%.100s'", __FUNCTION__, srchforsize, nelem, elemsize, (int)numbytes, (char*)srchfor);
    if(numbytes == 0 || arr == NULL || srchfor == NULL) {
        DEBUGPRINT(" ... RETVAL = -1 (NOT FOUND)");
        return -1;
    }

    for(index=0; index<nelem; index++) {
	DEBUGPRINT("\ncabinet[%d]='%s'", index, (*cabinet)[index]);
        if(! (  (0 == how) && strncasecmp((*cabinet)[index], drawer, numbytes) ||
                     (how) && memcmp((void*)(*cabinet)[index], (void*)drawer, numbytes)  ) ) {
            DEBUGPRINT(", RETVAL = %d (FOUND at this index)", index);
            return index;
        }
	}
  return -1;
}



/*===========================================================================

FUNCTION 

DESCRIPTION
Parse commands entered through command line (refer lasic_control.h)
Parameters: 
    mesg : The string to be parsed (entered on command line)
    (see side effects)

DEPENDENCIES

RETURN VALUE
The command identifier or error (eg. LASIC_CMD_UNKNOWN)

SIDE EFFECTS
Updates all parameters except mesg (which is the line to be parsed)
nrargs : will be updated with the nr. of words parsed after the command
words : pointers to start of each of the first 7 words that occur after the command name. These pointers refer to chars within 'mesg'
lens :  lengths of each of the words pointed to by words above
** NOTE : If less than 4 words follow the command then only the relevant pointers mentioned above are updated & the rest are left untouched
          'words' is NOT an array of strings. It's an array of POINTERS to places within the mesg string. Calling function can just declare 
          char pointers array & pass it
===========================================================================*/
LASIC_cmd_type 
LASIC_ParseCommand(const char *mesg, int *nrargs, char *words[], int *lens)
{
    int i, j, index, len;
    const char *next, *token_start;
    //char token[LASIC_MAX_MESG_LEN];
    LASIC_cmd_type retval = LASIC_CMD_UNKNOWN;
    int token_list_numelem = sizeof(Lasic_token_list)/sizeof(Lasic_token_list[0]);
    int token_list_elemsize = sizeof(Lasic_token_list[0]);
    int parse_rules_numelem = sizeof(Lasic_parse_rules)/sizeof(Lasic_parse_rules[0]);
    int parse_rules_elemsize = sizeof(Lasic_parse_rules[0]);

    DEBUGPRINT("\n%s(): ENTERED...", __FUNCTION__);

    if(NULL == (mesg && nrargs && words && lens)) {
        lasic_LOG_MESG(0, -1, "NULL pointer in function args !", "");
        return LASIC_CMD_ILLEGAL_MIN;
    }
    token_start = mesg;
    *nrargs = 0;

    /* Get first token & ensure it is a valid token (ie. it belongs to Lasic_token_list[] */
    if ((next = strpbrk(token_start, Lasic_delims)) == NULL)
        next = token_start + strlen(token_start);   /* If no delim found then point next to end of string */
    DEBUGPRINT("\n%s(): FOUND first TOKEN='%*s'", __FUNCTION__, (int)(next-token_start), token_start);
    if ((index = Lasic_Generic_Search_2DArray((void*)Lasic_token_list, (void*)token_start, next-token_start, token_list_numelem, token_list_elemsize, 0)) < 0) 
        return LASIC_CMD_UNKNOWN;

    DEBUGPRINT("\n%s(): STEP 1 DONE... index=%d", __FUNCTION__, index);

    /* Is first token a valid command ? */
    if(index <= LASIC_CMD_ILLEGAL_MIN || index >= LASIC_CMD_ILLEGAL_MAX)
        return LASIC_CMD_UNKNOWN;

    DEBUGPRINT("\n%s(): STEP 2 DONE... Finding Appropriate Rule for Index=%d ...", __FUNCTION__, index);

    retval = index;   
    /* Find location of this command in parse rules table */
    if((j = Lasic_Generic_Search_2DArray((void*)Lasic_parse_rules, (void*)&index, sizeof(index), parse_rules_numelem, parse_rules_elemsize, 0)) != index-1)
        DEBUGPRINT("\n%s(): WARNING: Parse Rule position different from Command Index : CmdIndex=%d , ParseRuleIndex=%d !\n", __FUNCTION__, index-1, j); 

    DEBUGPRINT("\n%s(): index=%d, j=%d", __FUNCTION__, index, j);

    DEBUGPRINT("\n%s(): PARSE_RULE[%d]: NumArgs=%d , Min/Max1='%s'/'%s', Min/Max2='%s'/'%s'", __FUNCTION__, j, Lasic_parse_rules[j].numargs, 
               Lasic_parse_rules[j].arg_minmax[0] ? Lasic_parse_rules[j].arg_minmax[0] : "", Lasic_parse_rules[j].arg_minmax[1] ? Lasic_parse_rules[j].arg_minmax[1] : "", 
               Lasic_parse_rules[j].arg_minmax[2] ? Lasic_parse_rules[j].arg_minmax[2] : "", Lasic_parse_rules[j].arg_minmax[3] ? Lasic_parse_rules[j].arg_minmax[3] : "");
    /* Now parse the command based on Lasic_parse_rules[] */
    for(i=0; i < LASIC_PARSERULE_MAXARGS*2; i+=2) {
        int validmin, validmax;
        token_start = next + strspn(next, Lasic_delims);  /* Skip all contiguous delims (eg whitespace) : token_start now points to next token */
        DEBUGPRINT("\n[Line %d]: %s(): [Iteration %d]: SET token_start=0x%x['%c'] / next=0x%x['%c']", __LINE__, __FUNCTION__, i/2, token_start, token_start, next, next);
        if ((next=strpbrk(token_start, Lasic_delims)) == NULL)
            next = token_start + strlen(token_start);
        DEBUGPRINT("\n[Line %d]: %s(): [Iteration %d]: token_start=0x%x['%c'] / SET next=0x%x['%c']", __LINE__, __FUNCTION__, i/2, token_start, token_start, next, next);
        DEBUGPRINT("\n[Line %d]: %s(): [Iteration %d]: IS(token_start == next) : %s", __LINE__, __FUNCTION__, i/2, token_start == next ? "YES" : "NO");
        if(token_start == next || *token_start == '\0') {   /* No more tokens */
            DEBUGPRINT("\n[Line %d]: %s(): No more tokens...returning...", __LINE__, __FUNCTION__);
            if(i < Lasic_parse_rules[j].numargs*2)    
                return LASIC_CMD_INVALID_PARAMS;            /* Min. Nr. of required args not found */
            else 
                return retval;
        }
        DEBUGPRINT("\n[Line %d]: %s(): continuing (token_start != next)...", __LINE__, __FUNCTION__);
        DEBUGPRINT("\n[Line %d]: %s(): [Iteration %d]: FOUND NEXT TOKEN: len=%d, Token='%*s'", __LINE__, __FUNCTION__, i/2, (int)(next-token_start), (int)(next-token_start), token_start);

        len = next-token_start;
        /* If the rule contains NULL for either the valid-min or valid-max values then the token doesn't need to be validated */
        if(Lasic_parse_rules[j].arg_minmax[i] && Lasic_parse_rules[j].arg_minmax[i+1]) {
            index = Lasic_Generic_Search_2DArray((void*)Lasic_token_list, (void*)token_start, len, token_list_numelem, token_list_elemsize, 0); 
            validmin = Lasic_Generic_Search_2DArray((void*)Lasic_token_list, (void*)Lasic_parse_rules[j].arg_minmax[i], 
                                          token_list_elemsize, token_list_numelem, token_list_elemsize, 0);
            validmax = Lasic_Generic_Search_2DArray((void*)Lasic_token_list, (void*)Lasic_parse_rules[j].arg_minmax[i+1], 
                                          token_list_elemsize, token_list_numelem, token_list_elemsize, 0);
            if(validmin < 1 || validmax < 1)
            {
                char temp[300];
                sprintf(temp, "INCORRECT PARSE RULE FOUND in Lasic_parse_rules[%d] : validmin=%d, validmax=%d, token index=%d", j, validmin, validmax, index);
                lasic_LOG_MESG(4, -1, temp, "");
                return LASIC_CMD_INVALID_PARAMS;
            }
            if(index <= validmin || index >= validmax) 
                    return LASIC_CMD_INVALID_PARAMS;    /* Didn't find expected token type as specified in parse rule for this command */
        }

        /* Token type is as expected so assign the appropriate pointer to start location of this token within the 'mesg' string */
        words[i/2] = (char *)token_start;
        lens[i/2] = len;
        *nrargs = i/2 + 1;
    }

    return retval;
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

