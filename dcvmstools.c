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

#include <sys/cdefs.h>
#include <sys/bitops.h>
#include <sys/endian.h>
#include <sys/stat.h>
#include <ctype.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <err.h>
#include <errno.h>
#include <stdbool.h>
#include <sysexits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "dcvmstools.h"
#define PATH_DEV_MMEM_DEFAULT	"/dev/mmem0.0c"

//#define JP_REGION
#define OUTPUT_ENCODING	"EUC-JP-MS"	/* XXX */

#ifndef __arraycount
#define __arraycount(__x)	(sizeof(__x) / sizeof(__x[0]))
#endif

typedef struct _vmsdirdesc VMSDIR;
struct _vmsdirdesc {
	int loc;
};

struct vmsfs_root *vms_rootblk;
struct vmsfs_fat *vms_fatblk;
struct vmsfs_dir *vms_dirblk;
char *vms_filename;
int vms_fd;

static char strxxx_buf[128];

#ifdef JP_REGION
#include <iconv.h>

const char *gamechar_map[94] = {
	" ",
	"ア", "ァ", "イ", "ィ", "ウ", "ヴ", "ゥ", "エ", "ェ", "オ", "ォ",
	"カ", "ガ", "キ", "ギ", "ク", "グ", "ケ", "ゲ", "コ", "ゴ",
	"サ", "ザ", "シ", "ジ", "ス", "ズ", "セ", "ゼ", "ソ", "ゾ",
	"タ", "ダ", "チ", "ヂ", "ツ", "ヅ", "ッ", "テ", "デ", "ト", "ド",
	"ナ", "ニ", "ヌ", "ネ", "ノ",
	"ハ", "バ", "パ", "ヒ", "ビ", "ピ", "フ", "ブ", "プ", "ヘ", "ベ", "ペ", "ホ", "ボ", "ポ",
	"マ", "ミ", "ム", "メ", "モ",
	"ヤ", "ャ", "ユ", "ュ", "ヨ", "ョ",
	"ラ", "リ", "ル", "レ", "ロ",
	"ワ", "ヰ", "ヱ", "ヲ", "ン",
	"0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
};

static char *
strjpstr(char *str, size_t len)
{
	iconv_t cd;
	char *bufp = strxxx_buf;
	size_t buflen = sizeof(strxxx_buf) - 1;

	cd = iconv_open(OUTPUT_ENCODING, "CP932");
	iconv(cd, &str, &len, &bufp, &buflen);
	iconv_close(cd);

	return strxxx_buf;
}

static char *
strgamestr(uint8_t *gamestr, int len)
{
	int gamech, i;

	memset(strxxx_buf, 0, sizeof(strxxx_buf));
	for (i = 0; i < len; i++) {
		gamech = gamestr[i];
		if (gamech < 0 || gamech > (int)__arraycount(gamechar_map))
			gamech = 0;
		strlcat(strxxx_buf, gamechar_map[gamech], sizeof(strxxx_buf));
	}
	return strxxx_buf;
}
#else /* JP_REGION */
static char *
strgamestr(uint8_t *gamestr, int len)
{
	char *p = strxxx_buf;
	int i, gamech;

	for (i = 0; i < len && p < (strxxx_buf + sizeof(strxxx_buf) - 4); i++) {
		gamech = gamestr[i];
		snprintf(p, 3, "%02x", gamech);
		p += 2;
		if (i < len - 1)
			*p++ = ',';
	}
	return strxxx_buf;
}

static char *
strjpstr(char *str, size_t len)
{
	return strgamestr((uint8_t *)str, (int)len);
}
#endif /* JP_REGION */

static int
vms_open(const char *file, int flags)
{
	/* for reopen */
	if (vms_rootblk != NULL) {
		free(vms_rootblk);
		vms_rootblk = NULL;
	}
	if (vms_fatblk != NULL) {
		free(vms_fatblk);
		vms_fatblk = NULL;
	}
	if (vms_dirblk != NULL) {
		free(vms_dirblk);
		vms_dirblk = NULL;
	}

	if (vms_filename != NULL) {
		free(vms_filename);
	}
	vms_filename = strdup(file);

	vms_fd = open(file, flags);
	if (vms_fd < 0)
		return -1;
	return 0;
}

static void
xdump(const char *data, int len)
{
	char ascii[17];
	int i;

	ascii[16] = '\0';
	for (i = 0; i < len; i++) {
		char c;
		if ((i & 15) == 0)
			printf("%08x:", i);
		c = *data++;
		printf(" %02x", c & 0xff);
		ascii[i & 15] = (0x20 <= c && c < 0x7f) ? c : '.';
		if ((i & 15) == 15)
			printf(" <%s>\n", ascii);
	}
	ascii[len & 15] = '\0';
	if (len & 15) {
		printf("%*s <%s>\n", 48 - (((int)len & 15) * 3), "", ascii);
	}
}

static int
vms_nextblock(int blkno)
{
	int nextblk;

	if (blkno > VMS_MAXBLOCKNO)
		return -1;

	nextblk = le16toh(vms_fatblk->block[blkno]);
	if (nextblk == BLOCK_UNALLOCATED)
		return -1;
	if (nextblk == BLOCK_LAST)
		return -1;

	return nextblk;
}

static int
vms_readwrite_blocks(void *buf, int startblk, int nblk, bool writemode)
{
	ssize_t len;
	off_t rc;
	int blk, nblkread;

	for (nblkread = 0, blk = startblk; blk <= VMS_MAXBLOCKNO;
	    blk = le16toh(vms_fatblk->block[blk])) {

		rc = lseek(vms_fd, VMS_BLOCKSIZE * blk, SEEK_SET);
		if (rc == -1)
			return -1;
		if (rc != VMS_BLOCKSIZE * blk) {
			errno = ESPIPE;
			return -1;
		}

		if (writemode)
			len = write(vms_fd, buf, VMS_BLOCKSIZE);
		else
			len = read(vms_fd, buf, VMS_BLOCKSIZE);
		if (len != VMS_BLOCKSIZE) {
			errno = ESPIPE;
			return -1;
		}

		buf = (char *)buf + VMS_BLOCKSIZE;

		if (++nblkread >= nblk)
			break;
	}

	if (nblkread != nblk) {
		errno = ENXIO;
		return -1;
	}

	return 0;
}

static int
vms_read_blocks(void *buf, int startblk, int nblk)
{
	return vms_readwrite_blocks(buf, startblk, nblk, false);
}

static int
vms_write_blocks(void *buf, int startblk, int nblk)
{
	return vms_readwrite_blocks(buf, startblk, nblk, true);
}

static int
vms_load_root(void)
{
	int rc;

	if (vms_rootblk != NULL)
		return 0;

	vms_rootblk = malloc(VMS_BLOCKSIZE);
	if (vms_rootblk == NULL)
		return -1;

	rc = vms_read_blocks(vms_rootblk, VMS_ROOTBLOCKNO, 1);
	if (rc != 0)
		return rc;

	return 0;
}

static int
vms_load_fat(void)
{
	int rc;

	rc = vms_load_root();
	if (rc != 0)
		return rc;

	if (vms_fatblk != NULL)
		return 0;

	vms_fatblk = malloc(VMS_BLOCKSIZE);
	if (vms_fatblk == NULL)
		return -1;

	return vms_read_blocks(vms_fatblk, le16toh(vms_rootblk->fat_blockno), 1);
}

static int
vms_save_fat(void)
{
	if (vms_fatblk == NULL)
		return -1;

	return vms_write_blocks(vms_fatblk, le16toh(vms_rootblk->fat_blockno), 1);
}

static int
vms_load_dir(void)
{
	int rc, dir_blkno, dir_blksize;

	rc = vms_load_fat();
	if (rc != 0)
		return rc;

	if (vms_dirblk != NULL)
		return 0;

	dir_blkno = le16toh(vms_rootblk->directory_blockno);
	dir_blksize = le16toh(vms_rootblk->directory_blocksize);

	if (dir_blksize != 13)
		fprintf(stderr, "WARNING: directory blocksize != 13\n");

	vms_dirblk = malloc((size_t)dir_blksize * VMS_BLOCKSIZE);
	if (vms_dirblk == NULL)
		return -1;

	return vms_read_blocks(vms_dirblk, dir_blkno, dir_blksize);
}

static int
vms_save_dir(void)
{
	int dir_blkno, dir_blksize;

	if (vms_dirblk == NULL)
		return -1;

	dir_blkno = le16toh(vms_rootblk->directory_blockno);
	dir_blksize = le16toh(vms_rootblk->directory_blocksize);

	return vms_write_blocks(vms_dirblk, dir_blkno, dir_blksize);
}

static int
vms_getfreeblock(void)
{
	int rc, i, nfreeblk;

	rc = vms_load_fat();
	if (rc != 0)
		return rc;

	nfreeblk = 0;
	for (i = 0; i <= VMS_MAXBLOCKNO; i++) {
		if (le16toh(vms_fatblk->block[i]) == BLOCK_UNALLOCATED)
			nfreeblk++;
	}
	return nfreeblk;
}

static int
vms_allocate_fat(int nblock)
{
	int rc, nfreeblk, blk, i;

	rc = vms_load_fat();
	if (rc != 0)
		return rc;

	nfreeblk = vms_getfreeblock();
	if (nblock > nfreeblk) {
		errno = ENOSPC;
		return -1;
	}

	blk = BLOCK_LAST;
	for (i = 0; nblock > 0 && i <= VMS_MAXBLOCKNO; i++) {
		if (le16toh(vms_fatblk->block[i]) == BLOCK_UNALLOCATED) {
			vms_fatblk->block[i] = htole16((uint16_t)blk);
			blk = i;
			nblock--;
		}
	}

	return blk;
}

static char *
strbcdtimestamp(const struct timestamp *timestamp)
{
	snprintf(strxxx_buf, sizeof(strxxx_buf), "%02x%02x-%02x-%02x %02x:%02x:%02x",
	    timestamp->bcd[0], timestamp->bcd[1], timestamp->bcd[2], timestamp->bcd[3],
	    timestamp->bcd[4], timestamp->bcd[5], timestamp->bcd[6]);
	return strxxx_buf;
}

static VMSDIR *
vmsfs_opendir(void)
{
	int rc;
	VMSDIR *dirp;

	rc = vms_load_dir();
	if (rc != 0)
		return NULL;

	dirp = malloc(sizeof(VMSDIR));
	if (dirp == NULL)
		return NULL;

	memset(dirp, 0, sizeof(*dirp));
	return dirp;
}

static struct vmsfs_dirent *
vmsfs_readdir(VMSDIR *dirp)
{
	int dir_blksize;

	dir_blksize = le16toh(vms_rootblk->directory_blocksize);

	while (dirp->loc < VMSFS_DIR_NENTRIES_PER_BLOCK * dir_blksize) {
		if (vms_dirblk->entries[dirp->loc].type == DIR_TYPE_NONE) {
			dirp->loc++;
			continue;
		}
		return &vms_dirblk->entries[dirp->loc++];
	}
	errno = 0;
	return NULL;
}

static void
vmsfs_closedir(VMSDIR *dirp)
{
	free(dirp);
}

static int
vms_dirent_filenamecmp(struct vmsfs_dirent *dp, const char *filename)
{
	char name[DIR_NAMELEN + 1];

	memcpy(name, dp->name, sizeof(dp->name));
	name[DIR_NAMELEN] = '\0';

	return strcasecmp(name, filename);
}

static struct vmsfs_dirent *
vms_dirent_alloc(void)
{
	int rc, i, dir_blksize;

	rc = vms_load_dir();
	if (rc != 0)
		return NULL;

	dir_blksize = le16toh(vms_rootblk->directory_blocksize);

	for (i = 0; i < VMSFS_DIR_NENTRIES_PER_BLOCK * dir_blksize; i++) {
		if (vms_dirblk->entries[i].type == DIR_TYPE_NONE)
			return &vms_dirblk->entries[i];
	}

	errno = ENOSPC;
	return NULL;
}

static struct vmsfs_dirent *
vms_dirent_lookup(const char *filename)
{
	size_t len;
	int rc, i, dir_blksize;

	rc = vms_load_dir();
	if (rc != 0)
		return NULL;

	len = strlen(filename);
	if (len > DIR_NAMELEN) {
		errno = ENOENT;
		return NULL;
	}

	dir_blksize = le16toh(vms_rootblk->directory_blocksize);

	for (i = 0; i < VMSFS_DIR_NENTRIES_PER_BLOCK * dir_blksize; i++) {
		if (vms_dirblk->entries[i].type == DIR_TYPE_NONE)
			continue;

		if (vms_dirent_filenamecmp(&vms_dirblk->entries[i], filename) == 0)
			return &vms_dirblk->entries[i];
	}

	errno = ENOENT;
	return NULL;
}

static int
vms_dirent_print(struct vmsfs_dirent *dp, int verbose)
{
	printf("%s ", strbcdtimestamp(&dp->timestamp));

	switch (dp->attr) {
	case DIR_ATTR_COPIABLE:
		printf("         ");
		break;
	case DIR_ATTR_PROHIBIT:
		printf("PROHIBIT ");
		break;
	default:
		printf("0x%02x     ", dp->attr);
		break;
	}

	switch (dp->type) {
	case DIR_TYPE_DATA:
		printf("DATA ");
		break;
	case DIR_TYPE_GAME:
		printf("GAME ");
		break;
	default:
		printf("0x%02x ", dp->type);
		break;
	}

	int nblk = le16toh(dp->size);
	if (nblk <= 1)
		printf("%3d block  ", nblk);
	else
		printf("%3d blocks ", nblk);

	printf("%-.12s", dp->name);

	if (dp->header_block_offset != 0)
		printf("  (+%d)", le16toh(dp->header_block_offset));

	if (verbose) {
		printf("\n\tFAT:");

		int j, blk;
		for (blk = le16toh(dp->block), j = 0;
		    blk >= 0; j++) {

			if (j > nblk) {
				printf(" <ERR:over FAT chain!>");
				break;
			}

			printf(" %d", blk);
			blk = vms_nextblock(blk);
		}
	}

	printf("\n");

	return nblk;
}

static int
vmsfs_unlink(const char *file)
{
	struct vmsfs_dirent *dp;

	dp = vms_dirent_lookup(file);
	if (dp == NULL) {
		errno = ENOENT;
		return -1;
	}

	/* free fat */
	int blk, nextblk;
	for (blk = le16toh(dp->block); blk >= 0;
	    blk = nextblk) {
		nextblk = vms_nextblock(blk);

		if (blk > VMS_MAXBLOCKNO) {
			errno = ENXIO;
			fprintf(stderr, "illegal block number: %d\n", blk);
			return -1;
		}
		vms_fatblk->block[blk] = htole16(BLOCK_UNALLOCATED);
	}

	/* erase the directory entry */
	dp->type = DIR_TYPE_NONE;
#if 0
	dp->attr = 0;
	dp->block = 0;
	memset(dp->name, 0, sizeof(dp->name));
	dp->size = 0;
	dp->header_block_offset = 0;
	memset(dp->reserved, 0, sizeof(dp->reserved));
#endif

	vms_save_dir();
	vms_save_fat();

	return 0;
}

static char *
vms_loadfile(const char *filename, size_t *sizep)
{
	struct vmsfs_dirent *dp;
	size_t size;
	int rc, startblk, nblk;
	char *buf0, *buf;

	dp = vms_dirent_lookup(filename);
	if (dp == NULL)
		return NULL;

	startblk = le16toh(dp->block);
	nblk = le16toh(dp->size);
	size = (size_t)nblk * VMS_BLOCKSIZE;
	buf = buf0 = malloc(size);
	if (buf == NULL)
		return NULL;

	rc = vms_read_blocks(buf, startblk, nblk);
	if (rc != 0) {
		free(buf);
		return NULL;
	}

	if (sizep != NULL)
		*sizep = size;

	return buf0;
}

static int
dcvmtool_cmd_dump(int argc, char *argv[])
{
	int rc, ch, opt_x;

	opt_x = 0;
	while ((ch = getopt(argc, argv, "x")) != -1) {
		switch (ch) {
		case 'x':
			opt_x++;
			break;
		default:
			fprintf(stderr, "usage: dcvmtools dump [options]\n");
			return EX_USAGE;
		}
	}
	argc -= optind;
	argv += optind;

	rc = vms_load_dir();
	if (rc != 0)
		return EX_DATAERR;

	if (opt_x)
		xdump((const char *)vms_rootblk, VMS_BLOCKSIZE);

	printf("color               = %d(%s), #%02X%02X%02X * %.1f%%\n",
	    vms_rootblk->color,
	    (vms_rootblk->color == 0) ? "Standard" : "Custom",
	    vms_rootblk->color_blue,
	    vms_rootblk->color_green,
	    vms_rootblk->color_red,
	    100.0 * vms_rootblk->color_alpha / 255);

	printf("timestamp           = %s\n", strbcdtimestamp(&vms_rootblk->timestamp));

	printf("fat_blockno         = %d\n", le16toh(vms_rootblk->fat_blockno));
	printf("fat_nblocksize      = %d\n", le16toh(vms_rootblk->fat_nblocksize));
	printf("directory_blockno   = %d\n", le16toh(vms_rootblk->directory_blockno));
	printf("directory_blocksize = %d\n", le16toh(vms_rootblk->directory_blocksize));
	printf("icon_block          = %d\n", le16toh(vms_rootblk->icon_block));
	printf("user_blocks         = %d\n", le16toh(vms_rootblk->user_blocks));

	return 0;
}

static int
dcvmtool_cmd_fat(int argc, char *argv[])
{
	VMSDIR *dirp;
	struct vmsfs_dirent *dp;
	int rc;
	uint16_t fatno;
	__BITMAP_TYPE(, uint32_t, VMS_NUM_BLOCKS) startfat;

	rc = vms_load_fat();
	if (rc != 0)
		return EX_DATAERR;

	__BITMAP_ZERO(&startfat);

	__BITMAP_SET(le16toh(vms_rootblk->fat_blockno), &startfat);
	__BITMAP_SET(le16toh(vms_rootblk->directory_blockno), &startfat);
	__BITMAP_SET(VMS_ROOTBLOCKNO, &startfat);

	if ((dirp = vmsfs_opendir()) != NULL) {
		while ((dp = vmsfs_readdir(dirp)) != NULL) {
			fatno = le16toh(dp->block);
			if (fatno <= VMS_MAXBLOCKNO)
				__BITMAP_SET(fatno, &startfat);
		}
		vmsfs_closedir(dirp);
	}

	printf("SYS block: %d\n", VMS_ROOTBLOCKNO);
	printf("FAT block: %d\n", le16toh(vms_rootblk->fat_blockno));
	printf("DIR block: %d...\n", le16toh(vms_rootblk->directory_blockno));
	printf("#\n");
	printf("# '*' = beginning of chain\n");
	printf("#\n");
	printf(" FAT|   +0   +1   +2   +3   +4   +5   +6   +7   +8   +9\n");
	printf("----+--------------------------------------------------\n");
	for (int i = 0; i < 256; i++) {
		char mark = __BITMAP_ISSET((unsigned int)i, &startfat) ? '*' : ' ';

		if ((i % 10) == 0)
			printf("+%03d|", i);

		fatno = le16toh(vms_fatblk->block[i]);
		switch (fatno) {
		case BLOCK_UNALLOCATED:
			printf(" %c   ", mark);
			break;
		case BLOCK_LAST:
			printf(" %cEND", mark);
			break;
		default:
			printf(" %c%03d", mark, fatno);
			break;
		}

		if ((i % 10) == 9)
			printf("\n");
	}
	printf("\n");
	printf("----+--------------------------------------------------\n");
	printf("    |   +0   +1   +2   +3   +4   +5   +6   +7   +8   +9\n");

	return 0;
}

static int
dcvmtool_cmd_dir(int argc, char *argv[])
{
	VMSDIR *dirp;
	struct vmsfs_dirent *dp;
	int ch, nfiles, total_blksize, user_freeblks, opt_v;

	opt_v = 0;
	while ((ch = getopt(argc, argv, "v")) != -1) {
		switch (ch) {
		case 'v':
			opt_v++;
			break;
		default:
			fprintf(stderr, "usage: dcvmtools dir [options]\n");
			return EX_USAGE;
		}
	}
	argc -= optind;
	argv += optind;

	dirp = vmsfs_opendir();
	if (dirp == NULL)
		return EX_DATAERR;;

	nfiles = total_blksize = 0;
	while ((dp = vmsfs_readdir(dirp)) != NULL) {
		total_blksize += vms_dirent_print(dp, opt_v);
		nfiles++;
	}
	vmsfs_closedir(dirp);


	user_freeblks = le16toh(vms_rootblk->user_blocks) - total_blksize;
	printf("                       %3d file%s %3d/%3d user blocks used\n",
	    nfiles, (nfiles <= 1) ? ", " : "s,",
	    total_blksize, le16toh(vms_rootblk->user_blocks));
	printf("                  %3d user blocks + %3d system blocks free\n",
	    user_freeblks,
	    vms_getfreeblock() - user_freeblks);

	return 0;
}

static int
dcvmtool_cmd_cat(int argc, char *argv[])
{
	size_t size;
	int i, anyerror;
	char *buf;

	anyerror = 0;
	for (i = 0; i < argc; i++) {
		buf = vms_loadfile(argv[i], &size);
		if (buf == NULL) {
			warn("%s", argv[i]);
			anyerror = 1;
			continue;
		}
		fwrite(buf, size, 1, stdout);
		free(buf);
	}

	return anyerror;
}

static int
dcvmtool_cmd_show_usage(void)
{
	fprintf(stderr, "usage: dcvmtools show [-v] file\n");
	return EX_USAGE;
}

static int
dcvmtool_cmd_show(int argc, char *argv[])
{
	struct vmsfile_header *header;
	size_t size;
	int ch, opt_v;
	char *buf;

	opt_v = 0;
	while ((ch = getopt(argc, argv, "v")) != -1) {
		switch (ch) {
		case 'v':
			opt_v++;
			break;
		default:
			return dcvmtool_cmd_show_usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 1)
		return dcvmtool_cmd_show_usage();

	buf = vms_loadfile(argv[0], &size);
	if (buf == NULL)
		errx(1, "%s", argv[0]);

	printf("size         = %d bytes (%d blocks)\n", (int)size, (int)size / VMS_BLOCKSIZE);

	header = (struct vmsfile_header *)buf;
	printf("vms_name     = <%s>\n", strjpstr(header->vms_name, 16));
	printf("rom_name     = <%s>\n", strjpstr(header->rom_name, 32));
	printf("game_name    = <%s>\n", strgamestr(header->game_name, 16));

	printf("icon num    = %d\n", le16toh(header->icon_num));
	printf("icon speed  = %d\n", le16toh(header->icon_speed));
	printf("type        = %d\n", le16toh(header->type));
	printf("crc         = 0x%04x\n", le16toh(header->crc));
	printf("datasize    = %d\n", le32toh(header->datasize));

	free(buf);
	return 0;
}

static int
dcvmtool_cmd_get_usage(void)
{
	fprintf(stderr, "usage: dcvmtools get [-v] file [...]\n");
	return EX_USAGE;
}

static int
dcvmtool_cmd_get(int argc, char *argv[])
{
	VMSDIR *dirp;
	FILE *fh;
	struct vmsfs_dirent *dp;
	size_t size;
	int i, ch, opt_v;
	const char *pattern;
	char *buf;
	int anyerror = 0;

	opt_v = 0;
	while ((ch = getopt(argc, argv, "v")) != -1) {
		switch (ch) {
		case 'v':
			opt_v++;
			break;
		default:
			return dcvmtool_cmd_get_usage();
		}
	}
	argc -= optind;
	argv += optind;

	for (i = 0; i < argc; i++) {
		pattern = argv[i];

		dirp = vmsfs_opendir();
		if (dirp == NULL)
			return EX_DATAERR;;

		while ((dp = vmsfs_readdir(dirp)) != NULL) {
			char name[DIR_NAMELEN + 1];
			memcpy(name, dp->name, DIR_NAMELEN);
			name[DIR_NAMELEN] = '\0';

			if (fnmatch(pattern, name, FNM_CASEFOLD) == 0) {
				buf = vms_loadfile(name, &size);
				if (buf == NULL) {
					warn("%s", name);
					anyerror = 1;
					continue;
				}

				if (opt_v)
					printf("%s\n", name);

				fh = fopen(name, "wb");
				if (fh == NULL) {
					warn("%s", name);
					anyerror = 1;
					continue;
				}
				fwrite(buf, size, 1, fh);
				fclose(fh);

				free(buf);
			}
		}
		vmsfs_closedir(dirp);
	}

	return anyerror;
}

static int
dcvmtool_cmd_del_usage(void)
{
	fprintf(stderr, "usage: dcvmtools del [options] file\n");
	return EX_USAGE;
}

static int
dcvmtool_cmd_del(int argc, char *argv[])
{
	char *filename;
	int rc, ch, opt_v;

	opt_v = 0;
	while ((ch = getopt(argc, argv, "v")) != -1) {
		switch (ch) {
		case 'v':
			opt_v++;
			break;
		default:
			return dcvmtool_cmd_del_usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 1)
		return dcvmtool_cmd_del_usage();

	filename = argv[0];
	rc = vmsfs_unlink(filename);
	if (rc != 0)
		err(1, "del: %s", filename);

	return 0;
}

static int
dcvmtool_cmd_put_usage(void)
{
	fprintf(stderr, "usage: dcvmtools put [options] file\n");
	return EX_USAGE;
}

static int
vmsfs_regular_name(char vmsname[DIR_NAMELEN], const char *filename)
{
	/* const char valid_character[] = ".0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ_"; */
	int i, ch;

	for (i = 0; i < DIR_NAMELEN; i++) {
		ch = toupper(filename[i] & 0xff);
		if (ch == '\0') {
			memset(&vmsname[i], '_', (size_t)(DIR_NAMELEN - i));
			break;
		}

		if (!isalnum(ch) && ch != '_' && ch != '.')
			ch = '_';
		vmsname[i] = (char)ch;
	}

	return 0;
}

static int
vmsfs_unixtime2bcdtimestamp(struct timestamp *timestamp, time_t mtime)
{
	struct tm *tm;
#define DEC2BCD(d)	((uint8_t)((((d) / 10) << 4) | ((d) % 10)))

	tm = localtime(&mtime);
	timestamp->bcd[0] = DEC2BCD((tm->tm_year + 1900) / 100);
	timestamp->bcd[1] = DEC2BCD(tm->tm_year % 100);
	timestamp->bcd[2] = DEC2BCD(tm->tm_mon + 1);
	timestamp->bcd[3] = DEC2BCD(tm->tm_mday);
	timestamp->bcd[4] = DEC2BCD(tm->tm_hour);
	timestamp->bcd[5] = DEC2BCD(tm->tm_min);
	timestamp->bcd[6] = DEC2BCD(tm->tm_sec);
	return 0;
}

static struct vmsfs_dirent *
vmsfs_writefile(const char *filename, char *buf, size_t size, time_t mtime)
{
	struct vmsfs_dirent *dp;
	size_t len, nblk;
	int rc, startblk;

	len = strlen(filename);
	if (len > DIR_NAMELEN) {
		errno = ENAMETOOLONG;
		return NULL;
	}

	if (size == 0) {
		errno = EINVAL;
		return NULL;
	}
	nblk = (size + VMS_BLOCKSIZE - 1) / VMS_BLOCKSIZE;
	if (nblk > VMS_MAXBLOCKNO) {
		errno = ENOSPC;
		return NULL;
	}

	dp = vms_dirent_alloc();
	if (dp == NULL)
		return NULL;

	memset(dp, 0, sizeof(*dp));

	//XXX: NOTYET: AUTO DETECT?
	dp->type = DIR_TYPE_DATA;

	dp->attr = DIR_ATTR_COPIABLE;
	vmsfs_regular_name(dp->name, filename);
	vmsfs_unixtime2bcdtimestamp(&dp->timestamp, mtime);
	dp->size = htole16((uint16_t)nblk);

	//XXX: NOTYET: AUTO DETECT?
	dp->header_block_offset = 0;

	startblk = vms_allocate_fat((int)nblk);
	if (startblk < 0)
		return NULL;

	dp->block = htole16((uint16_t)startblk);

	rc = vms_write_blocks(buf, startblk, (int)nblk);
	if (rc != 0)
		return NULL;

	return dp;
}


static char *
readfile(const char *filename, size_t size)
{
	FILE *fh;
	size_t rc, padsize;
	char *buf;

	padsize = (size + VMS_BLOCKSIZE - 1) / VMS_BLOCKSIZE * VMS_BLOCKSIZE;
	buf = malloc(padsize);
	if (buf == NULL)
		return NULL;

	memset(buf, 0, padsize);

	fh = fopen(filename, "rb");
	rc = fread(buf, size, 1, fh);
	fclose(fh);

	if (rc != 1)
		return NULL;

	return buf;
}

static int
dcvmtool_cmd_put(int argc, char *argv[])
{
	char *filename;
	size_t size;
	int rc, ch, opt_v;
	char *buf;

	opt_v = 0;
	while ((ch = getopt(argc, argv, "v")) != -1) {
		switch (ch) {
		case 'v':
			opt_v++;
			break;
		default:
			return dcvmtool_cmd_put_usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 1)
		return dcvmtool_cmd_del_usage();

	filename = argv[0];

	struct stat statbuf;
	struct vmsfs_dirent *dp;
	rc = stat(filename, &statbuf);
	if (rc != 0)
		err(1, "%s", filename);

	size = (size_t)statbuf.st_size;
	buf = readfile(filename, size);
	if (buf == NULL)
		err(1, "%s", filename);

	vmsfs_unlink(filename);	/* ignore error if the file is not exists */

	/* allocate fat and dirent, and write data */
	dp = vmsfs_writefile(filename, buf, size, statbuf.st_mtime);
	if (dp == NULL)
		err(1, "%s", filename);

	free(buf);

	vms_save_fat();
	vms_save_dir();

	return 0;
}

static int
usage(void)
{
	fprintf(stderr, "usage: dcvmstools [-f <device|VMSimage>] <command> [arg ...]\n");
	return EX_USAGE;
}

int
main(int argc, char *argv[])
{
	int ch;
	const char *cmd;
	const char *filename = PATH_DEV_MMEM_DEFAULT;

	while ((ch = getopt(argc, argv, "f:h")) != -1) {
		switch (ch) {
		case 'f':
			filename = optarg;
			break;
		case 'h':
		default:
			return usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (vms_open(filename, O_RDWR) != 0)
		err(EX_NOINPUT, "open: %s", filename);

	/* for reusing getopt(3) */
	optreset = 1;
	optind = 0;

	if (argc < 1)
		return usage();

	cmd = *argv++;
	argc--;

	if (strcmp(cmd, "dump") == 0) {
		return dcvmtool_cmd_dump(argc, argv);
	} else if (strcmp(cmd, "fat") == 0) {
		return dcvmtool_cmd_fat(argc, argv);
	} else if (strcmp(cmd, "dir") == 0) {
		return dcvmtool_cmd_dir(argc, argv);
	} else if (strcmp(cmd, "cat") == 0) {
		return dcvmtool_cmd_cat(argc, argv);
	} else if (strcmp(cmd, "show") == 0) {
		return dcvmtool_cmd_show(argc, argv);
	} else if (strcmp(cmd, "get") == 0) {
		return dcvmtool_cmd_get(argc, argv);
	} else if (strcmp(cmd, "put") == 0) {
		return dcvmtool_cmd_put(argc, argv);
	} else if (strcmp(cmd, "del") == 0) {
		return dcvmtool_cmd_del(argc, argv);
	}

	return usage();
}
