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
#include <winioctl.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#define DKTYPENAMES
#define FSTYPENAMES
#include "../ufs2tools-reboot/disk/diskio.h"

static struct disklabel label;
static char specname[256];
static int allfields = 1;
static uint32_t mbroffset;

static void display(FILE *f, const struct disklabel *lp)
{
    int i, j;
    const struct partition *pp;

    fprintf(f, "# %s:\n", specname);
    if (allfields) {
	if (lp->d_type < DKMAXTYPES)
	    fprintf(f, "type: %s\n", dktypenames[lp->d_type]);
	else
	    fprintf(f, "type: %u\n", lp->d_type);
	fprintf(f, "disk: %.*s\n", (int)sizeof(lp->d_typename),
	    lp->d_typename);
	fprintf(f, "label: %.*s\n", (int)sizeof(lp->d_packname),
	    lp->d_packname);
	fprintf(f, "flags:");
	if (lp->d_flags & D_REMOVABLE)
	    fprintf(f, " removeable");
	if (lp->d_flags & D_ECC)
	    fprintf(f, " ecc");
	if (lp->d_flags & D_BADSECT)
	    fprintf(f, " badsect");
	fprintf(f, "\n");
	fprintf(f, "bytes/sector: %lu\n", (u_long)lp->d_secsize);
	fprintf(f, "sectors/track: %lu\n", (u_long)lp->d_nsectors);
	fprintf(f, "tracks/cylinder: %lu\n", (u_long)lp->d_ntracks);
	fprintf(f, "sectors/cylinder: %lu\n", (u_long)lp->d_secpercyl);
	fprintf(f, "cylinders: %lu\n", (u_long)lp->d_ncylinders);
	fprintf(f, "sectors/unit: %lu\n", (u_long)lp->d_secperunit);
	fprintf(f, "rpm: %u\n", lp->d_rpm);
	fprintf(f, "interleave: %u\n", lp->d_interleave);
	fprintf(f, "trackskew: %u\n", lp->d_trackskew);
	fprintf(f, "cylinderskew: %u\n", lp->d_cylskew);
	fprintf(f, "headswitch: %lu\t\t# milliseconds\n",
	    (u_long)lp->d_headswitch);
	fprintf(f, "track-to-track seek: %ld\t# milliseconds\n",
	    (u_long)lp->d_trkseek);
	fprintf(f, "drivedata: ");
	for (i = NDDATA - 1; i >= 0; i--)
	    if (lp->d_drivedata[i])
		break;
	if (i < 0)
	    i = 0;
	for (j = 0; j <= i; j++)
	    fprintf(f, "%lu ", (u_long)lp->d_drivedata[j]);
	fprintf(f, "\n\n");
    }
    fprintf(f, "%u partitions:\n", lp->d_npartitions);
    fprintf(f,
        "#        size   offset    fstype   [fsize bsize bps/cpg]\n");
    pp = lp->d_partitions;
    for (i = 0; i < lp->d_npartitions; i++, pp++) {
	if (pp->p_size) {
	    fprintf(f, "  %c: %8lu %8lu  ", 'a' + i,
	       (u_long)pp->p_size, (u_long)pp->p_offset);
	    if (pp->p_fstype < FSMAXTYPES)
		fprintf(f, "%8.8s", fstypenames[pp->p_fstype]);
	    else
		fprintf(f, "%8d", pp->p_fstype);
	    switch (pp->p_fstype) {

	    case FS_UNUSED:				/* XXX */
		fprintf(f, "    %5lu %5lu %5.5s ",
		    (u_long)pp->p_fsize,
		    (u_long)(pp->p_fsize * pp->p_frag), "");
		break;

	    case FS_BSDFFS:
		fprintf(f, "    %5lu %5lu %5u ",
		    (u_long)pp->p_fsize,
		    (u_long)(pp->p_fsize * pp->p_frag),
		    pp->p_cpg);
		break;

	    case FS_BSDLFS:
		fprintf(f, "    %5lu %5lu %5d",
		    (u_long)pp->p_fsize,
		    (u_long)(pp->p_fsize * pp->p_frag),
		    pp->p_cpg);
		break;

	    default:
		fprintf(f, "%20.20s", "");
		break;
	    }
	    if (i == RAW_PART) {
		fprintf(f, "  # \"raw\" part, don't edit");
	    }
	    fprintf(f, "\n");
	}
    }
    fflush(f);
}

void usage()
{
	fprintf(stderr,
	"%s\n%s\n",
	"usage: bsdlabel disk[/slice]",
	"\t\t(to read label)"
	);
	exit(-1);
}

int main(int argc, char **argv)
{
	HANDLE device = NULL;
	int ret, i;
	int drive, slice;
	char buf[BBSIZE];
	char *tmp;
	char *fname = NULL;

	if (argc < 2)
		usage();

	for (i = 1; i < argc; ++i) {
		if (argv[i][0] == '-' && argv[i][2] == '\0') {
			switch(argv[i][1]) {
				case 'h':
					// FALLTHROUGH
				default:
					usage();
					break;
			}
		} else {
			device = open_file_device(argv[i]);
			if (device != INVALID_HANDLE_VALUE) {
				fname = argv[i];
				continue;
			}

			tmp = argv[i];
			drive = strtol(tmp, &tmp, 0);

			if (tmp[0] != '/' && tmp[0] != '\0')
				usage();

			if (tmp[0] == '\0') {
				device = open_device(drive);
			} else {
				++tmp;
				slice = strtol(tmp, &tmp, 0);
				if (tmp[0] != '\0') {
					usage();
				}
				printf("drive: %d, slice: %d\n", drive, slice);
				device = open_slice_device(drive, slice);
			}

			if (device != INVALID_HANDLE_VALUE) {
				continue;
			} else {
				break;
			}
		}
	}

	if (device == INVALID_HANDLE_VALUE) {
		fprintf(stderr, "bsdlabel: could not open device\n");
		exit(-1);
	}

	seek_device(device, 0, SEEK_SET);
	ret = read_device(device, buf, BBSIZE);
	if (ret) {
		fprintf(stderr, "bsdlabel: could not read from device\n");
		exit(-1);
	}

	ret = bsd_disklabel_le_dec(buf + 512, &label, MAXPARTITIONS);
	if (ret) {
		fprintf(stderr, "bsdlabel: bsdlabel not found\n");
		exit(-1);
	}

	mbroffset = slice_offset;

	if (label.d_partitions[RAW_PART].p_offset == mbroffset) {
		for (i = 0; i < label.d_npartitions; i++) {
			if (label.d_partitions[i].p_size)
				label.d_partitions[i].p_offset -= mbroffset;
		}
	}

	if (fname) {
		sprintf(specname, "File %s", fname);
	} else {
		sprintf(specname, "Drive %d, Slice %d", drive, slice);
	}

	display(stdout, &label);

	return 0;
}
