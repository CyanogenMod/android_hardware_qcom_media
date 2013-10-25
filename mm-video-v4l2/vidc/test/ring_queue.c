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

/*
	Ring/Circular buffer
*/

#include "ring_queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#ifdef DEBUG
#define D(fmt, args...) \
	do { \
		printf("D/ring_queue: " fmt, ##args); \
	}while(0)
#else
#define D(fmt, args...)
#endif
#define E(fmt, args...) \
	do { \
		printf("E/ring_queue: " fmt, ##args); \
	}while(0)


/*ring_buf_write()
 * data_size:
 *	IN: The data to be write.
 *	OUT: The actual data been written.
 * data:
 *	Source of the data to be copy to the ring buffer.
 * return:
 *	The index to the start of the data written in the ring buffer.
 */
__u32 ring_buf_write(ring_buf_header *rhdr, __u8 *data, __u32 *data_size)
{
	__u32 new_write_idx = 0;
	__u32 prev_write_idx = 0;
	__u32 available_space = 0;
	__u32 read_idx = 0;
	__u8 *write_ptr = NULL;

	if (!rhdr || !data) {
		E("Invalid Params\n");
		return -EINVAL;
	}

	if (!rhdr->ring_base_addr) {
		E("Ring buffer have already been freed\n");
		*data_size = 0;
		return 0;
	}

	if (*data_size == 0) {
		D("Zero data size\n");
		return rhdr->ring_write_idx;
	}

	read_idx = rhdr->ring_read_idx;

	available_space = (rhdr->ring_write_idx >=  read_idx) ?
				(rhdr->ring_size - (rhdr->ring_write_idx - read_idx)) :
				(read_idx - rhdr->ring_write_idx);
	D("Available space: %d, try to write: %d\n",
		available_space, *data_size);
	if (available_space <= *data_size) {
		rhdr->ring_is_full =  1;
		E("Error: available size (%d) to write (%d)\n",
			available_space, *data_size);
		*data_size = 0;
		return rhdr->ring_write_idx;
	}

	rhdr->ring_is_full =  0;

	new_write_idx = (rhdr->ring_write_idx + *data_size);
	write_ptr = (__u8 *)((rhdr->ring_base_addr) +
			(rhdr->ring_write_idx));
	D("Base addr: %p, Write ptr: %p\n",
		(__u8 *)(rhdr->ring_base_addr),
		write_ptr);
	if (new_write_idx < rhdr->ring_size) {
		memcpy(write_ptr, data, *data_size);
		D("writed to: %p\n", write_ptr);
	} else {
		new_write_idx -= rhdr->ring_size;
		memcpy(write_ptr, data, (*data_size - new_write_idx));
		memcpy((void *)rhdr->ring_base_addr,
			data + (*data_size - new_write_idx),
			new_write_idx);
	}

	prev_write_idx = rhdr->ring_write_idx;
	rhdr->ring_write_idx = new_write_idx;

	return prev_write_idx;
}

/*ring_buf_read()
 * data_size:
 *	IN: The new read index since it has been read.
 * data:
 *	Copy the data from the ring buffer to the provided address,
 *	starting at the previous index up to the new index.
 *	If NULL, the index's values are updated but no copy performed.
 * return:
 *	Any error in the read process.
 */
int ring_buf_read(ring_buf_header *rhdr, __u8 *data, __u32 new_read_idx)
{
	__u32 data_size = 0;
	__u8 *read_ptr = NULL;
	int rc = 0;

	if (!rhdr || data_size > rhdr->ring_size) {
		E("Error: Invalid Params\n");
		return -EINVAL;
	}

	if (!rhdr->ring_base_addr) {
		E("Error: Ring buffer have already been freed\n");
		return -EINVAL;
	}

	if (rhdr->ring_read_idx == rhdr->ring_write_idx) {
		rhdr->ring_is_empty = 1;
		D("Info: Ring is empty!\n");
		return rc;
	}

	if ((rhdr->ring_read_idx < rhdr->ring_write_idx &&
			new_read_idx > rhdr->ring_write_idx) ||
			(new_read_idx > rhdr->ring_write_idx &&
			new_read_idx < rhdr->ring_read_idx)) {
		E("Error: new_read_idx overlap write_idx\n");
		return -EINVAL;
	}

	if (data) {
		read_ptr = (__u8*)(rhdr->ring_base_addr + rhdr->ring_read_idx);
		data_size = new_read_idx > rhdr->ring_read_idx ?
				new_read_idx - rhdr->ring_read_idx :
				(rhdr->ring_size - rhdr->ring_read_idx) +
				new_read_idx;
		D("Copy data readed: %d\n", data_size);
		if (new_read_idx < rhdr->ring_read_idx) {
			memcpy(data, read_ptr, data_size);
		} else {
			memcpy(data, read_ptr,
				rhdr->ring_size - rhdr->ring_read_idx);
			memcpy(data + (rhdr->ring_size - rhdr->ring_read_idx),
				(__u8 *)rhdr->ring_base_addr,
				new_read_idx);
		}
	}

	D("ring: new read_idx = %d\n", new_read_idx);
	rhdr->ring_read_idx = new_read_idx;

	if (rhdr->ring_read_idx == rhdr->ring_write_idx)
		rhdr->ring_is_empty = 1;
	else
		rhdr->ring_is_empty = 0;

	return rc;
}
