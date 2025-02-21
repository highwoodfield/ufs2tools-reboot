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
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "disk\diskio.h"
#include "ufs.h"
#include "ufs1.h"
#include "ufs2.h"

ufs_block_list* (*ufs_get_block_list)(HANDLE device, struct fs *fs,
    ufs_dinode *ufs_dinode);

void (*ufs_free_block_list)(ufs_block_list *list);

int (*ufs_read_data)(HANDLE device, struct fs *fs,
    const ufs_dinode *inode, ufs_block_list *block_list,
    unsigned char *buf, ufs_inop start_block, ufs_inop num_blocks);

int (*ufs_read_inode)(HANDLE device, struct fs *fs, ufs_inop ino,
    ufs_dinode *inode);

uint16_t (*ufs_read_direntry)(void *buf, struct direct *direct);

ufs_inop (*ufs_follow_symlinks)(HANDLE device, struct fs *fs,
    ufs_inop root_ino, ufs_inop ino);

ufs_inop (*ufs_lookup_path)(HANDLE device, struct fs *fs, char *path,
    int follow, ufs_inop root_ino);

static int ufs_version;

// return bytes read
uint16_t read_direntry(void *buf, struct direct* direct)
{
	memcpy(direct, buf, 8);
	strncpy(direct->d_name, &((char*)buf)[8], direct->d_namlen);
	direct->d_name[direct->d_namlen] = '\0';

	return direct->d_reclen;
}

struct fs* ufs_init(HANDLE device)
{
	int i;
	int sblock_offs[] = SBLOCKSEARCH;
	struct fs *fs;
	char *buf = malloc(SBLOCKSIZE);
	fs = NULL;

	ufs_version = 0;
	for (i = 0; sblock_offs[i] != -1; ++i) {
		seek_device(device, sblock_offs[i], SEEK_SET);
		read_device(device, buf, SBLOCKSIZE);
		fs = (struct fs*)buf;
		if (fs->fs_magic == FS_UFS1_MAGIC) {
			ufs_version = 1;
			break;
		} else if (fs->fs_magic == FS_UFS2_MAGIC) {
			ufs_version = 2;
			break;
		}
	}

	if (ufs_version == 0) {
		free(buf);
		return NULL;
	} else if (ufs_version == 1) {
		ufs_get_block_list = ufs1_get_block_list;
		ufs_free_block_list = ufs1_free_block_list;
		ufs_read_data = ufs1_read_data;
		ufs_read_inode = ufs1_read_inode;
		ufs_read_direntry = read_direntry;
		ufs_follow_symlinks = ufs1_follow_symlinks;
		ufs_lookup_path = ufs1_lookup_path;
	} else if (ufs_version == 2) {
		ufs_get_block_list = ufs2_get_block_list;
		ufs_free_block_list = ufs2_free_block_list;
		ufs_read_data = ufs2_read_data;
		ufs_read_inode = ufs2_read_inode;
		ufs_read_direntry = read_direntry;
		ufs_follow_symlinks = ufs2_follow_symlinks;
		ufs_lookup_path = ufs2_lookup_path;
	}

	return fs;
}
