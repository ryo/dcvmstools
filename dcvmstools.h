/*-
 * Copyright (c) 2021 Ryo Shimizu <ryo@nerv.org>
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _DCVMSTOOLS_H_
#define _DCVMSTOOLS_H_

#include <sys/cdefs.h>

#define VMS_BLOCKSIZE	512
#define VMS_ROOTBLOCKNO	255
#define VMS_MAXBLOCKNO	255
#define VMS_NUM_BLOCKS	256

struct timestamp {
	uint8_t bcd[8];
};

struct vmsfs_root {
	uint8_t magic[16];		/* +0x00 */
	uint8_t color;			/* +0x10 */
	uint8_t color_blue;		/* +0x11 */
	uint8_t color_green;		/* +0x12 */
	uint8_t color_red;		/* +0x13 */
	uint8_t color_alpha;		/* +0x14 */
	uint8_t reserved1[27];		/* +0x15-0x2f */
	struct timestamp timestamp;	/* +0x30-0x37 */
	uint8_t reserved2[8];		/* +0x38-0x3f */
	uint8_t reserved3[6];		/* +0x40-0x45 */
	uint16_t fat_blockno;		/* +0x46-0x47 */
	uint16_t fat_nblocksize;	/* +0x48-0x49 */
	uint16_t directory_blockno;	/* +0x4a-0x4b */
	uint16_t directory_blocksize;	/* +0x4c-0x4d */
	uint16_t icon_block;		/* +0x4e-0x4f */
	uint16_t user_blocks;		/* +0x50-0x51 */
	uint8_t reserved4[430];		/* +0x52-0xff */
};

struct vmsfs_fat {
	uint16_t block[256];
#define BLOCK_UNALLOCATED	0xfffc
#define BLOCK_LAST		0xfffa
};

struct vmsfs_dirent {
	uint8_t type;
#define DIR_TYPE_NONE	0x00
#define DIR_TYPE_DATA	0x33
#define DIR_TYPE_GAME	0xcc
	uint8_t attr;
#define DIR_ATTR_COPIABLE	0x00
#define DIR_ATTR_PROHIBIT	0xff
	uint16_t block;		/* first block */
#define DIR_NAMELEN		12
	char name[DIR_NAMELEN];
	struct timestamp timestamp;
	uint16_t size;
	uint16_t header_block_offset;
	uint8_t reserved[4];
};

struct vmsfs_dir {
#define VMSFS_DIR_NENTRIES_PER_BLOCK	16
	struct vmsfs_dirent entries[VMSFS_DIR_NENTRIES_PER_BLOCK];
};

struct vmsfile_icon {
	uint8_t data[512];
};

struct vmsfile_header {
	char vms_name[16];		/* +0x00 */
	char rom_name[32];		/* +0x10 */
	uint8_t game_name[16];		/* +0x30 */
	uint16_t icon_num;		/* +0x40 */
	uint16_t icon_speed;		/* +0x42 */
	uint16_t type;			/* +0x44 */
	uint16_t crc;			/* +0x46 */
	uint32_t datasize;		/* +0x48 */
	uint8_t reserved[20];		/* +0x4c */
	uint16_t palette[16];		/* +0x60 */
	struct vmsfile_icon icondata[];	/* +0x80 */
};

#endif /* _DCVMSTOOLS_H_ */
