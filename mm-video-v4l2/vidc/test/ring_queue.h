/*
 * Copyright (c) 2013, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef RING_QUEUE_H
#define RING_QUEUE_H
#include <stdlib.h>

typedef struct ring_buf_header {
	__u32 ring_base_addr;	/* Same value for the live of the ring buffer*/
	__u32 ring_size;	/* Values is fix after initial allocation */
	__u32 ring_is_empty;
	__u32 ring_is_full;
	__u32 ring_read_idx;	/* Points to next read point
				Updated at ring_buf_read */
	__u32 ring_write_idx;	/* Points to next write point
				 Updated at ring_buf_write */
} ring_buf_header;

/*ring_buf_write()
 * data_size:
 *	IN: The data to be write.
 *	OUT: The actual data been written.
 * data:
 *	Source of the data to be copy to the ring buffer.
 * return:
 *	The index to the start of the data written in the ring buffer.
 */
__u32 ring_buf_write(ring_buf_header *rhdr, __u8 *data, __u32 *data_size);

/*ring_buf_read()
 * new_read_idx:
 *	IN: The new read index since it has been read.
 * data:
 *	Copy the data from the ring buffer to the provided address,
 *	starting at the previous index up to the new index.
 *	If NULL, the index's values are updated but no copy performed.
 * return:
 *	Any error in the read process.
 */
int ring_buf_read(ring_buf_header *rhdr, __u8 *data, __u32 new_read_idx);

#endif
