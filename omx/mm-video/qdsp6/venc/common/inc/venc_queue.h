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

#ifndef _VENC_QUEUE_H
#define _VENC_QUEUE_H

#ifdef __cplusplus
extern "C" {
#endif
/*========================================================================

                     INCLUDE FILES FOR MODULE

==========================================================================*/

/**
 * @brief Constructor
 *
 * @param handle The queue handle
 * @param max_queue_size Max number of items in queue (size)
 * @param max_data_size Max data size
 */
int venc_queue_create(void** handle, int max_queue_size, int max_data_size);

/**
 * @brief Destructor
 *
 * @param handle The queue handle
 */
int venc_queue_destroy(void* handle);

/**
 * @brief Pushes an item onto the queue.
 *
 * @param handle The queue handle
 * @param data Pointer to the data
 * @param data_size Size of the data in bytes
 */
int venc_queue_push(void* handle, void* data, int data_size);

/**
 * @brief Pops an item from the queue.
 *
 * @param handle The queue handle
 * @param data Pointer to the data
 * @param data_size Size of the data buffer in bytes
 */
int venc_queue_pop(void* handle, void* data, int data_size);

/**
 * @brief Peeks at the item at the head of the queue
 *
 * Nothing will be removed from the queue
 *
 * @param handle The queue handle
 * @param data Pointer to the data
 * @param data_size Size of the data in bytes
 *
 * @return 0 if there is no data
 */
int venc_queue_peek(void* handle, void* data, int data_size);

/**
 * @brief Get the size of the queue.
 *
 * @param handle The queue handle
 */
int venc_queue_size(void* handle);

/**
 * @brief Check if the queue is full
 *
 * @param handle The queue handle
 */
int venc_queue_full(void* handle);

#ifdef __cplusplus
}
#endif

#endif // #ifndef _VENC_QUEUE_H
