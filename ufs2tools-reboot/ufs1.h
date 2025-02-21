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

#ifndef _UFS1_H_
#define _UFS1_H_

#include "ufs/dinode.h"
#include "ffs/fs.h"
#include "ufs/dir.h"

#include "ufs.h"

extern ufs_block_list* ufs1_get_block_list(HANDLE device, struct fs *fs,
    ufs_dinode *ufs_dinode);

extern void ufs1_free_block_list(ufs_block_list *list);

extern int ufs1_read_data(HANDLE device, struct fs *fs,
    const ufs_dinode *inode, ufs_block_list *block_list,
    unsigned char *buf, ufs_inop start_block, ufs_inop num_blocks);

extern int ufs1_read_inode(HANDLE device, struct fs *fs, ufs_inop ino,
    ufs_dinode *inode);

extern ufs_inop ufs1_follow_symlinks(HANDLE device, struct fs *fs,
    ufs_inop root_ino, ufs_inop ino);

extern ufs_inop ufs1_lookup_path(HANDLE device, struct fs *fs, char *path,
    int follow, ufs_inop root_ino);

#endif
