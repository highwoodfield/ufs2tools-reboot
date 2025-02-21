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

#include "disk/diskio.h"
#include "ufs.h"
#include "ufs1.h"

ufs_block_list* ufs1_get_block_list(HANDLE device, struct fs *fs,
    ufs_dinode *ufs_dinode)
{
	int i, j, k;
	int32_t *block_list;
	ufs_block_list *list;
	struct ufs1_dinode *dinode;
	uint64_t count, totalsize;
	uint32_t *bufx;
	uint32_t *bufy;
	uint32_t *bufz;

	dinode = &ufs_dinode->din.ufs1;
	block_list = malloc((dinode->di_size / fs->fs_bsize + 1)
	    * sizeof(*block_list));
	list = malloc(sizeof(*list));
	list->ufs1 = block_list;

	count = 0;
	totalsize = 0;

	// direct blocks
	for (i = 0; i < NDADDR; ++i) {
		block_list[count] = dinode->di_db[i];
		totalsize += sblksize(fs, dinode->di_size, count);
		++count;

		if (totalsize >= dinode->di_size)
			return list;
	}

	// indirect blocks
	bufx = malloc(fs->fs_bsize);
	seek_device(device, dinode->di_ib[0] * fs->fs_fsize, SEEK_SET);
	read_device(device, (char*)bufx, fs->fs_bsize);

	for (i = 0; i * sizeof(bufx[0]) < fs->fs_bsize; ++i) {
		block_list[count] = bufx[i];
		totalsize += sblksize(fs, dinode->di_size, count);
		++count;

		if (totalsize >= dinode->di_size) {
			free(bufx);
			return list;
		}
	}

	// double indirect blocks
	bufy = malloc(fs->fs_bsize);
	seek_device(device, dinode->di_ib[1] * fs->fs_fsize, SEEK_SET);
	read_device(device, (char*)bufy, fs->fs_bsize);

	for (j = 0; j * sizeof(bufy[0]) < fs->fs_bsize; ++j) {
		seek_device(device, bufy[j] * fs->fs_fsize,
		    SEEK_SET);
		read_device(device, (char*)bufx, fs->fs_bsize);

		for (i = 0; i * sizeof(bufx[0]) < fs->fs_bsize; ++i) {
			block_list[count] = bufx[i];
			totalsize += sblksize(fs, dinode->di_size, count);
			++count;

			if (totalsize >= dinode->di_size) {
				free(bufx);
				free(bufy);
				return list;
			}
		}
	}

	// triple indirect blocks
	bufz = malloc(fs->fs_bsize);
	seek_device(device, dinode->di_ib[2] * fs->fs_fsize, SEEK_SET);
	read_device(device, (char*)bufz, fs->fs_bsize);

	for (k = 0; k * sizeof(bufz[0]) < fs->fs_bsize; ++k) {
		seek_device(device, bufz[k] * fs->fs_fsize,
		    SEEK_SET);
		read_device(device, (char*)bufy, fs->fs_bsize);

		for (j = 0; j * sizeof(bufy[0]) < fs->fs_bsize; ++j) {
			seek_device(device, bufy[j] * 
			    fs->fs_fsize, SEEK_SET);
			read_device(device, (char*)bufx, fs->fs_bsize);

			for (i = 0; i * sizeof(bufx[0]) < fs->fs_bsize; ++i) {
				block_list[count] = bufx[i];
				totalsize += sblksize(fs, dinode->di_size, count);
				++count;

				if (totalsize >= dinode->di_size) {
					free(bufx);
					free(bufy);
					free(bufz);
					return list;
				}
			}
		}
	}

	free(bufx);
	free(bufy);
	free(bufz);
	return list;
}

void ufs1_free_block_list(ufs_block_list *list)
{
	free(list->ufs1);
	free(list);
}

int ufs1_read_data(HANDLE device, struct fs *fs,
    const ufs_dinode *dinode, ufs_block_list *block_list,
    unsigned char *buf, ufs_inop start_block, ufs_inop num_blocks)
{
	int i, bsize;
	const struct ufs1_dinode *di;
	int64_t total, read, len, offset;
	int32_t *bl;

	di = &dinode->din.ufs1;
	bl = block_list->ufs1;

	if (!num_blocks) {
		len = di->di_size;
	} else {
		len = num_blocks * fs->fs_fsize;
	}

	// no blocks, data is small enough to fit in dinode.di_db
	if (di->di_blocks == 0) {
		memcpy(buf, di->di_db, di->di_size);
		return di->di_size;
	}

	// find start position
	total = 0;
	offset = 0;
	for (i = 0; total < start_block * fs->fs_fsize; ++i) {
		bsize = sblksize(fs, di->di_size, i);

		if (total + bsize > start_block * fs->fs_fsize) {
			offset = start_block * fs->fs_fsize - total;
			break;
		}

		total += bsize; 
	}

	for (total = 0; total < len; ++i) {
		if (bl[i] != 0 && seek_device(device, (int64_t)bl[i] *
		    fs->fs_fsize + offset, SEEK_SET) < 0)
			return -1;

		read = sblksize(fs, di->di_size, i) - offset;
		offset = 0;

		if (read + total > len)
			read = len - total;

		if (bl[i] == 0) {
			memset(buf, 0, read);
		} else if (read_device(device, buf, read) < 0) {
			return -1;
		}

		total += read;
		buf += read;
	}

	return total;
}

int ufs1_read_inode(HANDLE device, struct fs *fs, ufs_inop ino,
    ufs_dinode *dinode)
{
	int ret;
	struct ufs1_dinode *di;

	di = &dinode->din.ufs1;

	ret = seek_device(device, cgimin(fs,(ino / fs->fs_ipg)) *
	    fs->fs_fsize + ((ino % fs->fs_ipg) * sizeof(struct ufs1_dinode)),
	    SEEK_SET);

	if (ret)
		return -1;

	ret = read_device(device, (char*)di, sizeof(*di));
	if (ret)
		return -1;

	dinode->mode = di->di_mode;
	dinode->size = di->di_size;
	dinode->atime = di->di_atime;
	dinode->mtime = di->di_mtime;

	return 0;
}

ufs_inop ufs1_follow_symlinks(HANDLE device, struct fs *fs,
    ufs_inop root_ino, ufs_inop ino)
{
	ufs_dinode dinode;
	struct ufs1_dinode *di = &dinode.din.ufs1;

	ufs_block_list *block_list;

	ufs1_read_inode(device, fs, ino, &dinode);
	while ((di->di_mode & IFMT) == IFLNK) {
		char tmpname[MAX_PATH];

		block_list = ufs1_get_block_list(device, fs, &dinode);
		ufs1_read_data(device, fs, &dinode, block_list, tmpname, 0, 0);
		ufs1_free_block_list(block_list);

		tmpname[di->di_size] = '\0';
		ino = ufs1_lookup_path(device, fs, tmpname,
		    0, root_ino);

		ufs1_read_inode(device, fs, ino, &dinode);
	}

	return ino;
}

ufs_inop ufs1_lookup_path(HANDLE device, struct fs *fs, char *path,
    int follow, ufs_inop root_ino)
{
	int ret, i;
	char *nexts, *nexte, *tmp, *s, *sorig;
	ufs_dinode dinode;
	struct direct direct;
	ufs_block_list *block_list;
	int64_t found_ino;

	s = malloc(MAX_PATH);
	sorig = s;

	if (path[0] == '/') {
		strcpy(s, &path[1]);
		root_ino = ROOTINO;
	} else {
		strcpy(s, path);
	}

	for (nexts = s; nexts && nexts[0]; nexts = s) {
		if ((s = strchr(s, '/'))) {
			*s = '\0';
			s++;
		}

		ufs1_read_inode(device, fs, root_ino, &dinode);

		block_list = ufs1_get_block_list(device, fs, &dinode);
		tmp = malloc(dinode.din.ufs1.di_size);
		ufs1_read_data(device, fs, &dinode, block_list, tmp, 0, 0);
		ufs1_free_block_list(block_list);

		nexte = tmp;

		for (i = 0;; ++i) {
			ret = ufs_read_direntry(nexte, &direct);

			if (nexte - tmp >= dinode.din.ufs1.di_size) {
				free(tmp);
				return 0;
			}

			if (!strcmp(direct.d_name, nexts)) {
				found_ino = direct.d_ino;
				break;
			}

			nexte += ret;
		}

		free(tmp);
		if (!found_ino)
			return 0;

		if (follow || (s && s[0] != '\0')) {
			root_ino = ufs1_follow_symlinks(device,
			    fs, root_ino, found_ino);
		} else {
			root_ino = found_ino;
		}
	}

	free(sorig);
	return root_ino;
}
