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

#ifndef _VENC_FILE_H
#define _VENC_FILE_H

#ifdef __cplusplus
extern "C" {
#endif
/*========================================================================

                     INCLUDE FILES FOR MODULE

==========================================================================*/

/**
 * @brief Opens the file in read or write mode
 *
 * @param handle The new file handle
 * @param filename The name of the file
 * @param readonly Set to 1 for read access, 0 for write access.
 */
int venc_file_open(void** handle,
                   char* filename,
                   int readonly);

/**
 * @brief Closes the file
 */
int venc_file_close(void* handle);

/**
 * @brief Reads the file. Only valid in read mode.
 *
 * @param handle The file handle
 * @param buffer The buffer to read into
 * @param bytes The number of bytes to read
 * @param bytes_read The number of bytes actually read (output)
 */
int venc_file_read(void* handle,
                   void* buffer,
                   int bytes);

/**
 * @brief Writes to the file. Only valid in write mode.
 *
 * @param handle The file handle
 * @param buffer The buffer to write from
 * @param bytes The number of bytes to write
 * @param bytes_write The number of bytes actually written (output)
 */
int venc_file_write(void* handle,
                    void* buffer,
                    int bytes);

/**
 * @brief Reposition the file pointer.
 *
 * @param handle The file handle
 * @param bytes The number of bytes from the start of file
 */
int venc_file_seek_start(void* handle,
                         int bytes);

#ifdef __cplusplus
}
#endif

#endif // #ifndef _VENC_FILE_H
