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
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <io.h>
#include <sys/stat.h>
#include <sys/utime.h>

#include "disk/diskio.h"
#include "ufs.h"
#include "ufs2.h"
#include "misc.h"

// 512 is optimum for speed
#define COPY_FBLOCKS 512

typedef enum {
	command_none,
	command_list,
	command_get
} command_t;

// sorting function for directory listing
int sort_direct(const void *first, const void *second)
{
	const struct direct *a = first;
	const struct direct *b = second;

	if (!strcmp(a->d_name, ".")) {
		return -1;
	} else if (!strcmp(b->d_name, ".")) {
		return 1;
	} else if (!strcmp(a->d_name, "..")) {
		return -1;
	} else if (!strcmp(b->d_name, "..")) {
		return 1;
	} else {
		return strcmp(a->d_name, b->d_name);
	}
}

int print_dir_listing(HANDLE device, struct fs *fs, char *path)
{
	int i, ret, numentries;
	ufs_inop ino;
	ufs_block_list *block_list;
	char *tmp, *buf;
	char timestring[64];
	char sizestring[32];
	char symlinkstring[280];
	struct direct directtmp;
	struct direct *direct;
	ufs_dinode dinode;
	struct tm *tm;

	ino = ufs_lookup_path(device, fs, path, 1, ROOTINO);
	if (!ino) {
		fprintf(stderr, "ufs2tool: \"%s\" does not exist\n", path);
		return -1;
	}

	ufs_read_inode(device, fs, ino, &dinode);

	if (!(dinode.mode & IFDIR)) {
		fprintf(stderr, "ufs2tool: \"%s\" is not a directory\n", path);
		return -1;
	}

	buf = malloc(dinode.size + sizeof(struct direct));
	block_list = ufs_get_block_list(device, fs, &dinode);
	ufs_read_data(device, fs, &dinode, block_list, buf, 0, 0);
	ufs_free_block_list(block_list);

	// this gets number of dir entries
	tmp = buf;
	for (numentries = 0; tmp - buf < dinode.size; ++numentries) {
		ret = ufs_read_direntry(tmp, &directtmp);
		tmp += ret;
	}

	direct = malloc(numentries * sizeof(*direct));

	tmp = buf;
	for (i = 0; i < numentries; ++i) {
		ret = ufs_read_direntry(tmp, &direct[i]);
		tmp += ret;
	}
	free(buf);

	qsort(direct, numentries, sizeof(*direct), sort_direct);

	for (i = 0; i < numentries; ++i) {
		ufs_read_inode(device, fs, direct[i].d_ino, &dinode);
		tm = localtime((const time_t*)(&dinode.mtime));

		if ((dinode.mode & IFMT) == IFDIR) {
			sprintf(sizestring, "<DIR>");
		} else {
			sprintf(sizestring, "%I64u", dinode.size);
		}

		if ((dinode.mode & IFMT) == IFLNK) {
			char tmpname[MAX_PATH];

			block_list = ufs_get_block_list(device, fs, &dinode);
			ufs_read_data(device, fs, &dinode, block_list, tmpname, 0, 0);
			ufs_free_block_list(block_list);
			tmpname[dinode.size] = '\0';

			sprintf(symlinkstring, " -> %s", tmpname);
		} else {
			symlinkstring[0] = '\0';
		}

		strftime(timestring, 64, "%b %d,%Y  %H:%M:%S", tm);
		printf("%s %10s %s%s\n", timestring, sizestring,
		    direct[i].d_name, symlinkstring);
	}

	free(direct);

	return 0;
}

// NOTE: this function is recursive for recursive copying
int read_file(HANDLE device, struct fs *fs, ufs_inop root_ino,
	ufs_inop ino, char *srcpath, char *destpath)
{
	int i;
	int64_t totalsize, readsize, read;
	ufs_block_list *block_list;
	char *buf, *dir, *tmp;
	char newdest[MAX_PATH];
	int using_con;
	FILE *of;
	ufs_dinode dinode;
	struct stat stat_buf;
	struct utimbuf filetime;

	ufs_inop symlink_ino;

	using_con = (destpath && (!stricmp(destpath, "CON") ||
	    !(strnicmp(destpath, "CON.", 4))));

	if (!root_ino || !ino) {
		fprintf(stderr, "ufs2tool: \"%s\" does not exist\n", srcpath);
		return -1;
	}

	// this checks for a recursive symlink loop
	ufs_read_inode(device, fs, ino, &dinode);
	if ((dinode.mode & IFMT) == IFLNK) {
		ino = ufs_lookup_path(device, fs, srcpath, 1, ROOTINO);
		dir = dirname(srcpath);
		symlink_ino = ufs_lookup_path(device, fs, dir, 1, ROOTINO);

		for (;; symlink_ino = ufs_lookup_path(device, fs, "..", 0,
		    symlink_ino)) {
			if (symlink_ino == ino) {
				fprintf(stderr, "(\"%s\" is a recursive symlink loop)\n", srcpath);
				free(dir);
				return -1;
			}

			if (symlink_ino == ROOTINO)
				break;
		}

		free(dir);
	}

	if (destpath == NULL) {
		char *base = basename(srcpath);
		strcpy(newdest, "./");
		strcat(newdest, base);
		free(base);
	} else {
		while (destpath[strlen(destpath) - 1] == '/')
			destpath[strlen(destpath) - 1] = '\0';

		if (!stat(destpath, &stat_buf) && (stat_buf.st_mode & S_IFDIR)) {
			char *base;
			strcpy(newdest, destpath);
			strcat(newdest, "/");
			base = basename(srcpath);
			strcat(newdest, base);
			free(base);
		} else {
			strcpy(newdest, destpath);
		}
	}

	ufs_read_inode(device, fs, ino, &dinode);

	block_list = ufs_get_block_list(device, fs, &dinode);
	totalsize = dinode.size;

	if (dinode.mode & IFDIR) {
		char *dirdest, *tmp;
		char nextsrc[256];
		char nextdest[MAX_PATH];
		int ret;
		struct direct directtmp;
		struct stat sb;

		if (using_con) {
			fprintf(stderr, "ufs2tool: cannot copy directory to console\n");
			ufs_free_block_list(block_list);
			return -1;
		}

		if (!stat(newdest, &sb)) {
			if ((sb.st_mode & S_IFMT) != S_IFDIR)
				return -1;
			dirdest = strdup(newdest);
		} else {
			// check for invalid filename
			if (mkdir(newdest) == -1) {
				dirdest = valid_filename(newdest, 0);
				if (mkdir(dirdest) == -1) {
					free(dirdest);
					return -1;
				}
			} else {
				dirdest = strdup(newdest);
			}
		}

		buf = malloc(dinode.size + sizeof(struct direct));
		ufs_read_data(device, fs, &dinode, block_list, buf, 0, 0);

		tmp = buf;
		for (i = 0;; ++i) {
			ret = ufs_read_direntry(tmp, &directtmp);
			if (tmp - buf >= dinode.size || !ret)
				break;
			tmp += ret;
			if (!strcmp(directtmp.d_name, ".") || !strcmp(directtmp.d_name, ".."))
				continue;

			strcpy(nextsrc, srcpath);
			if (srcpath[strlen(srcpath) - 1] != '/')
				strcat(nextsrc, "/");
			strcat(nextsrc, directtmp.d_name);

			strcpy(nextdest, dirdest);
			strcat(nextdest, "/");
			strcat(nextdest, directtmp.d_name);

			read_file(device, fs, ino, directtmp.d_ino, nextsrc, nextdest);
		}

		ufs_free_block_list(block_list);
		free(buf);

		return 0;
	}

	readsize = 0;
	read = 0;

	fprintf(stderr, "retrieving \"%s\"\n", srcpath);

	tmp = valid_filename(newdest, using_con);
	strcpy(newdest, tmp);
	free(tmp);

	of = fopen(newdest, "wb");
	if (!of) {
		fprintf(stderr, "ufs2tool: cannot open file %s\n", newdest);
		return -1;
	}

	buf = malloc(COPY_FBLOCKS * fs->fs_fsize);

	for (i = 0; readsize < totalsize; i += read / fs->fs_fsize) {
		if (totalsize - readsize < fs->fs_fsize * COPY_FBLOCKS) {
			read = (totalsize - readsize) / fs->fs_fsize;
			read = ufs_read_data(device, fs, &dinode, block_list,
			    buf, i, read ? read : 1);
			if (readsize + read > totalsize) {
				read = totalsize - readsize; // EOF
			}
			fwrite(buf, 1, read, of);
			readsize += read;
		} else {
			// read COPY_FBLOCKS blocks
			read = COPY_FBLOCKS;
			read = ufs_read_data(device, fs, &dinode, block_list,
			    buf, i, read);
			readsize += read;
			fwrite(buf, 1, read, of);
		}
		fprintf(stderr, "%I64d of %I64d bytes copied (%lld%%)\r", readsize, totalsize, readsize * 100 / totalsize);
	}

	if (!totalsize)
		fprintf(stderr, "(empty file)");

	fprintf(stderr, "\n");

	fclose(of);
	filetime.actime = dinode.atime;
	filetime.modtime = dinode.mtime;
	utime(newdest, &filetime);

	free(buf);
	ufs_free_block_list(block_list);

	return 0;
}

void usage()
{
	fprintf(stderr, "%s\n\n%s\n\n%s\n%s\n%s\n",
	"    ufs2tool",
	"    usage: ufs2tool drive[/slice]/partition [-lg] srcpath [destpath]",
	"    -l		list directory",
	"    -g		get file to destpath (basename of srcpath if not specified)",
	""
	);
	exit(-1);
}

int main(int argc, char **argv)
{
	HANDLE device;
	int i, x, ret;
	command_t command;
	int drive, slice, partition;
	ufs_inop ino;
	char patha[MAX_PATH];
	char pathb[MAX_PATH];
	char *tmp;
	struct fs *fs;

	patha[0] = pathb[0] = '\0';

	if (argc < 3)
		usage();

	drive = slice = partition = 0;

        device = open_file_device(argv[1]);
        if (device == INVALID_HANDLE_VALUE) {
		tmp = argv[1];

		drive = strtol(tmp, &tmp, 0);
		if (tmp[0] != '/' && tmp[0] != '\0') {
			usage();
		}
		++tmp;

		x = strtol(tmp, &tmp, 0);
		if (tmp[0] != '/' && tmp[0] != '\0')
			usage();

		if (tmp[0]) {
			++tmp;
			slice = x;
			partition = strtol(tmp, &tmp, 0);
		} else {
			slice = 1;
			partition = x;
		}
	}

	command = command_none;

        for (i = 2; i < argc; ++i) {
                if (argv[i][0] == '-' && argv[i][2] == '\0') {
                        switch(argv[i][1]) {
				case 'g':
					if (command != command_none)
						usage();
					command = command_get;
					break;
				case 'l':
					if (command != command_none)
						usage();
					command = command_list;
					break;
                                case 'h':
                                default:
                                        usage();
                                        break;
                        }
                } else if (!strcmp(argv[i], "--help")) {
			usage();
		} else {
			if (!patha[0]) {
				strcpy(patha, argv[i]);
			} else {
				strcpy(pathb, argv[i]);
			}
		}
	}

	if (device == INVALID_HANDLE_VALUE) {
		device = open_partition_device(drive, slice, partition);
		if (device == INVALID_HANDLE_VALUE) {
			fprintf(stderr, "ufs2tool: could not open device\n");
			exit(-1);
		}
	}

	fs = ufs_init(device);

	if (!fs) {
		fprintf(stderr, "ufs2tool: UFS partition not found\n");
		exit(-1);
	}

	switch (command) {
		case command_get:
			ino = ufs_lookup_path(device, fs, patha, 0, ROOTINO);
			if (!pathb[0]) {
				ret = read_file(device, fs, ROOTINO, ino, patha, NULL);
			} else {
				ret = read_file(device, fs, ROOTINO, ino, patha, pathb);
			}
			break;
		case command_list:
		case command_none:
			ret = print_dir_listing(device, fs, patha);
			break;
	}

	free(fs);

	return 0;
}
