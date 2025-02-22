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

#include <windows.h>
#include <stdio.h>
#include <stdint.h>

#include "diskio.h"

#ifndef INVALID_SET_FILE_POINTER
#define INVALID_SET_FILE_POINTER ((DWORD)-1)
#endif

uint32_t slice_offset;
uint32_t partition_offset;

static uint32_t sector_offset = 0;
struct dos_table table;
struct disklabel label;

int seek_absolute_device(HANDLE device, int64_t offset, int whence)
{
	unsigned long low, high;
	int read;

	if (device == INVALID_HANDLE_VALUE)
		return -1;

	high = (offset >> 32) & 0x7FFFFFFF;
	low = offset & 0xFFFFFFFF;

	sector_offset = low % 512;
	low -= sector_offset;

	switch (whence) {
		case SEEK_SET:
			read = SetFilePointer(device, low, &high, FILE_BEGIN);
			break;
		case SEEK_CUR:
			read = SetFilePointer(device, low, &high, FILE_CURRENT);
			break;
		case SEEK_END:
			read = SetFilePointer(device, low, &high, FILE_END);
			break;
	}

	if (read == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR) {
		return -1;
	} else {
		return 0;
	}
}

int seek_device(HANDLE device, int64_t offset, int whence)
{
	if (whence == SEEK_SET) {
		if (partition_offset) {
			offset += (int64_t)partition_offset * 512;
		} else {
			offset += (int64_t)slice_offset * 512;
		}
	} else if (whence == SEEK_END) {
		// fixme;
	}

	return seek_absolute_device(device, offset, whence);
}

int read_device(HANDLE device, char *buf, int64_t numbytes)
{
	long read;
	int ret;
	char tmp[512];
	int64_t rem;

	if (device == INVALID_HANDLE_VALUE)
		return -1;

	ret = ReadFile(device, tmp, 512, &read, NULL);
	if (!ret) {
		FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM |
		    FORMAT_MESSAGE_IGNORE_INSERTS, NULL, GetLastError(),
		    MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		    tmp, 512, NULL);
		fprintf(stderr, "%s\n", tmp);
	}

	if (numbytes > 512 - sector_offset) {
		memcpy(buf, tmp + sector_offset, 512 - sector_offset);
		rem = numbytes - (512 - sector_offset);
	} else {
		memcpy(buf, tmp + sector_offset, numbytes);
		rem = 0;
	}

	if (rem % 512) {
		ReadFile(device, buf + 512 - sector_offset, (rem / 512) *
		    512, &read, NULL);
		sector_offset = rem % 512;
		ret = ReadFile(device, tmp, 512, &read, NULL);
		memcpy(buf, tmp, sector_offset);
		seek_device(device, SEEK_CUR, -512);
	} else {
		ret = ReadFile(device, buf + 512 - sector_offset, rem, &read, NULL);
		if (!ret) {
			FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			    NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			    tmp, 512, NULL);
			fprintf(stderr, "%s\n", tmp);
		}
		sector_offset = 0;
	}

	return (!ret);
}

static int partindex = 0;
static int numlogical = 0;

// start - offset of slice table
// offset - offset of the first extended slice
static int read_slice_table(HANDLE device, struct dos_table *dt,
    uint32_t start, uint32_t offset)
{
	int i;
	int32_t extstart;
	char buf[512];
	char emptybuf[DOSPARTSIZE];
	void *tablep;
	struct dos_partition d;
	struct dos_partition *dpnext;	// pointer to next entry to fill

	memset(emptybuf, 0, DOSPARTSIZE);

	seek_device(device, (int64_t)start * 512, SEEK_SET);
	read_device(device, buf, 512);

	if (*(uint16_t*)(buf + DOSMAGICOFFSET) != DOSMAGIC) {
		return -1;
	}

	// intialize the dos_table struct
	if (!offset) {
		dt->dt_entrycount = 0;
		dt->dt_partcount = 0;
		for (i = 0; i < NEXTDOSPART+1; ++i) {
			dt->dt_partnum[i] = -1;
		}
	}

	extstart = -1;

	// read the primary slices
	// FIXME: cleanup
	for (i = 0; i < 4; ++i) {
		tablep = &buf[DOSPARTOFF + i * DOSPARTSIZE];
		dpnext = &dt->dt_slices[partindex];
		dos_partition_dec(tablep, dpnext);
		// set to absolute value
		dpnext->dp_start += start;
		if (dpnext->dp_typ == DOSPTYP_EXT ||
		    dpnext->dp_typ == DOSPTYP_EXTLBA ||
		    dpnext->dp_typ == 0x85) {
			extstart = dpnext->dp_start;
			if (!offset) {
				dt->dt_partnum[i + 1] = partindex;
				++partindex;
				++dt->dt_partcount;
			}
			++dt->dt_entrycount;
		} else {
			if (memcmp(tablep, emptybuf, DOSPARTSIZE)) {
				++dt->dt_entrycount;
				++dt->dt_partcount;
				if (!offset) {
					dt->dt_partnum[i + 1] = partindex;
				} else {
					++numlogical;
					dt->dt_partnum[4 + numlogical] = partindex;
				}
				++partindex;
			} else if (!offset) {
				++partindex;
			}
		}
	}

	// read logical slices
	if (extstart >= 0) {
		for (i = 0; i < 4; ++i) {
			tablep = &buf[DOSPARTOFF + i * DOSPARTSIZE];
			dos_partition_dec(tablep, &d);
			if (d.dp_typ == DOSPTYP_EXT || 
			    d.dp_typ == DOSPTYP_EXTLBA || d.dp_typ == 0x85) {
				d.dp_start += offset;
				read_slice_table(device, dt, d.dp_start,
				    (offset ? offset : extstart));
			}
		}
	}
	return 0;
}

// drive (0-based)
HANDLE open_device(int drive)
{
	HANDLE device;
	char path[32];

	sprintf(path, "\\\\.\\PhysicalDrive%d", drive);

	device = CreateFile(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
	    NULL, OPEN_EXISTING, 0, NULL);
	if (device == INVALID_HANDLE_VALUE)
		return INVALID_HANDLE_VALUE;

	read_slice_table(device, &table, 0, 0);
	slice_offset = 0;

	return device;
}

// drive (0-based)
// slice (1-based)
HANDLE open_slice_device(int drive, int slice)
{
	HANDLE device;
	char path[32];

	sprintf(path, "\\\\.\\PhysicalDrive%d", drive);

	device = CreateFile(path, GENERIC_READ, FILE_SHARE_READ |
	    FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	if (device == INVALID_HANDLE_VALUE) {
		printf("open_slice_device: invalid handle\n");
		return INVALID_HANDLE_VALUE;
	}

	read_slice_table(device, &table, 0, 0);

	printf("ec: %u\npc: %u\n\n\n", table.dt_entrycount, table.dt_partcount);

	if (slice > NEXTDOSPART+1) {
		printf("open_slice_device: invalid slice\n");
		close_device(device);
		return INVALID_HANDLE_VALUE;
	}

	
	if (table.dt_partnum[slice] == -1 ||
	    table.dt_slices[table.dt_partnum[slice]].dp_size == 0) {
		printf("open_slice_device: invalid size\n");
		close_device(device);
		return INVALID_HANDLE_VALUE;
	} else {
		slice_offset = table.dt_slices[table.dt_partnum[slice]].dp_start;
	}

	return device;
}

// drive (0-based)
// slice (1-based)
// partition (0-based)
HANDLE open_partition_device(int drive, int slice, int partition)
{
	HANDLE device;
	char path[32];
	char buf[BBSIZE];

	sprintf(path, "\\\\.\\PhysicalDrive%d", drive);
	device = CreateFile(path, GENERIC_READ, FILE_SHARE_READ |
	    FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	if (device == INVALID_HANDLE_VALUE)
		return INVALID_HANDLE_VALUE;

	if (slice > NEXTDOSPART+1 || partition > MAXPARTITIONS) {
		close_device(device);
		return INVALID_HANDLE_VALUE;
	}

	if (slice) {
		read_slice_table(device, &table, 0, 0);
		if (table.dt_partnum[slice] == -1 ||
		    table.dt_slices[table.dt_partnum[slice]].dp_size == 0) {
			close_device(device);
			return INVALID_HANDLE_VALUE;
		} else {
			slice_offset = table.dt_slices[table.dt_partnum[slice]].dp_start;
		}
	} else {
		slice_offset = 0;
	}

	seek_device(device, 0, SEEK_SET);
	read_device(device, buf, BBSIZE);

	if (bsd_disklabel_le_dec(buf + 512, &label, MAXPARTITIONS)) {
		close_device(device);
		return INVALID_HANDLE_VALUE;
	}

	if (partition >= MAXPARTITIONS || !label.d_partitions[partition].p_size) {
		close_device(device);
		return INVALID_HANDLE_VALUE;
	}

	partition_offset = label.d_partitions[partition].p_offset;

	return device;
}

// open a file
HANDLE open_file_device(char *path)
{
	HANDLE device;

	device = CreateFile(path, GENERIC_READ, FILE_SHARE_READ |
	    FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	if (device == INVALID_HANDLE_VALUE)
		return INVALID_HANDLE_VALUE;

	read_slice_table(device, &table, 0, 0);

	slice_offset = 0;

	return device;
}

void close_device(HANDLE device)
{
	CloseHandle(device);
}
