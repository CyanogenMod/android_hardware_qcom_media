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
/*
 *         An OMX Video Decoder Property Manager application ....
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "cutils/properties.h"

const char *prop_arb_bytes = "persist.omxvideo.arb-bytes";
const char *prop_acc_subframe = "persist.omxvideo.accsubframe";
const char *prop_profile_chk = "persist.omxvideo.profilecheck";
const char *prop_level_chk = "persist.omxvideo.levelcheck";
const char *prop_arb_bytes_vc1 = "persist.omxvideo.arb-bytes-vc1";

const char *key_arbitrary = "arbitrarybytes";
const char *key_arbitrary_vc1 = "vc1-arbitrarybytes";
const char *key_accumulate = "accumulatesubframe";
const char *key_profile = "profilecheck";
const char *key_level = "levelcheck";

/* Function which parses the keyword & returns a matching Android property */
const char* parse_property_key ( char* key )
{
    const char* property_name = NULL;
    int error = 0;

    if(key == NULL)
    {
        printf("\nInvalid key[0x0]\n");
        return NULL;
    }

    printf("\n Input key [%s]\n", key);

    switch(key[0])
    {
        case 'a':
            if(strstr(key_arbitrary, key) == key_arbitrary)
            {
                 property_name = prop_arb_bytes;
            }
            else if(strstr(key_accumulate, key) == key_accumulate)
            {
                 property_name = prop_acc_subframe;
            }
            else
            {
                error = 1;
            }
        break;

        case 'p':
            if(strstr(key_profile, key) == key_profile)
            {
                 property_name = prop_profile_chk;
            }
            else
            {
                 error = 1;
            }
        break;

        case 'l':
            if(strstr(key_level, key) == key_level)
            {
                 property_name = prop_level_chk;
            }
            else
            {
                 error = 1;
            }
        break;

        case 'v':
            if(strstr(key_arbitrary_vc1, key) == key_arbitrary_vc1)
            {
                 property_name = prop_arb_bytes_vc1;
            }
            else
            {
                 error = 1;
            }
        break;

        default:
            error = 1;
        break;
    }

    if(error)
    {
        printf("\n INVALID key given as input \n");
    }
    return property_name;
}

int main ( int argc, char** argv )
{
    const char *property = NULL;
    const char *str_value_true = "true";
    const char *str_value_false = "false";
    char property_value[PROPERTY_VALUE_MAX] = {0};
    int count = argc;
    int i = 1, ret = -1;

    if(count <= 2 || count >= 12)
    {
        printf("\nError: Invalid number of arguments[%d]!\n", count);
        printf("\nOnly 3 to 11 arguments can be given\n");
        printf("\nUsage: ./mm-vdec-omx-property-mgr <property1 name or");
        printf(" key-prefix> <value to set> \n<property2 name or key-prefix>");
        printf(" <value to set> .... upto 5 properties currently\n");
        printf("\n Ex: ./mm-vdec-omx-property-mgr ac true ar 1 p 0\n");
        return -1;
    }

    if(count%2 == 0)
    {
        printf("\nError: Even no. of arguments! Last property has no value\n");
        property = parse_property_key( argv[count-1] );
        if(property == NULL)
        {
            printf("\nInvalid property given at the end\n");
        }
        else
        {
            printf("\n Key [%s] corresponding to property [%s]", argv[count-1],
                property);
            printf("\n has no value in input");
            printf("\n Property [%s] will be ignored \n", property);
            count--;
        }
    }

    while(i < count)
    {
        property = parse_property_key( argv[i] );

        if(property)
        {
            printf("\n Valid property[%s] at argument [%d]\n", property, i);

            if(!strcmp(argv[i+1], "true") || !strcmp(argv[i+1], "1"))
            {
                ret = property_set(property, str_value_true);
            }
            else if(!strcmp(argv[i+1], "false") || !strcmp(argv[i+1], "0"))
            {
                ret = property_set(property, str_value_false);
            }
            else
            {
                printf("\nError: Invalid value{%s} for property [%s]\n", \
                    argv[i+1], property);
                i += 2;
                continue;
            }

            if(ret != 0)
            {
                printf("\nError: property_set failed for \"%s\"\n", property);
            }
        }
        else
        {
            printf("\nError: Invalid key [%s] given at argument [%d]\n",
                argv[i], i);
        }

        i += 2;
    }

    sleep(1);
    /* Read and print the values of all properties */
    printf("\n   PROPERTY :  VALUE\n");

    if(0 != property_get(prop_arb_bytes, property_value, NULL))
    {
        printf("\n   %s :   %s", prop_arb_bytes, property_value);
    }
    else
    {
        printf("\n   %s :   [READ ERROR]", prop_arb_bytes);
    }

    if(0 != property_get(prop_arb_bytes_vc1, property_value, NULL))
    {
        printf("\n   %s :   %s", prop_arb_bytes_vc1, property_value);
    }
    else
    {
        printf("\n   %s :   [READ ERROR]", prop_arb_bytes_vc1);
    }

    if(0 != property_get(prop_acc_subframe, property_value, NULL))
    {
        printf("\n   %s :   %s", prop_acc_subframe, property_value);
    }
    else
    {
        printf("\n   %s :   [READ ERROR]", prop_acc_subframe);
    }

    if(0 != property_get(prop_profile_chk, property_value, NULL))
    {
        printf("\n   %s :   %s", prop_profile_chk, property_value);
    }
    else
    {
        printf("\n   %s :   [READ ERROR]", prop_profile_chk);
    }

    if(0 != property_get(prop_level_chk, property_value, NULL))
    {
        printf("\n   %s :   %s", prop_level_chk, property_value);
    }
    else
    {
        printf("\n   %s :   [READ ERROR]", prop_level_chk);
    }

    printf("\n****************************************************\n");
    printf("\n***********OMX VDEC PROPERTIES TEST COMPLETE********\n");
    printf("\n****************************************************\n");
}

