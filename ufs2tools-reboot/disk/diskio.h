/*
 * Copyright (c) 2004 Nehal Mistry
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _DISKIO_H_
#define _DISKIO_H_

#include <stdint.h>
#include <windows.h>

#include "diskmbr.h"
#include "disklabel.h"

struct dos_table {
	unsigned char dt_entrycount;	/* entry count, includes any
					   extended slices */
	unsigned char dt_partcount;	/* slice count */
	int dt_partnum[NEXTDOSPART+1];
					/* linked list, unix slice
					   number => partnum entry,
					   -1 if invalid entry */
	struct dos_partition dt_slices[NEXTDOSPART];
					/* slice entries */
};

extern uint32_t slice_offset;
extern uint32_t partition_offset;

extern int seek_device(HANDLE device, int64_t offset, int whence);
extern int seek_absolute_device(HANDLE device, int64_t offset, int whence);
extern int read_device(HANDLE device, char *buf, int64_t numbytes);

extern HANDLE open_device(int drive);
extern HANDLE open_file_device(char *path);
extern HANDLE open_slice_device(int drive, int slice);
extern HANDLE open_partition_device(int drive, int slice, int partition);
extern void close_device(HANDLE device);

#endif
