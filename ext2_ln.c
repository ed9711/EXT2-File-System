#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <libgen.h>
#include <errno.h>
#include "ext2_utils.h"

unsigned char *disk = NULL;

int main(int argc, char **argv) {

	if (!(argc == 4 || argc == 5)) {
		fprintf(stderr,
				"Usage: %s <image file name> [-s] <absolute original path on disk> <absolute link path>\n",
				argv[0]);
		exit(1);
	}

	disk = get_disk(argv[1]);
	bool symlnk = false;
	if (!strcmp(argv[2], "-s")) {
		symlnk = true;
	}
	char *path_o = NULL;
	char *path_ln = NULL;
	if (symlnk) {
		path_o = malloc(sizeof(argv[3]) + sizeof(char));
		strcpy(path_o, argv[3]);
		check_valid(path_o);

		path_ln = malloc(sizeof(argv[4]) + sizeof(char));
		strcpy(path_ln, argv[4]);
		check_valid(path_ln);

	} else {
		path_o = malloc(sizeof(argv[2]) + sizeof(char));
		strcpy(path_o, argv[2]);
		check_valid(path_o);

		path_ln = malloc(sizeof(argv[3]) + sizeof(char));
		strcpy(path_ln, argv[3]);
		check_valid(path_ln);

	}
	struct ext2_super_block *sb = (struct ext2_super_block *) (disk
			+ EXT2_BLOCK_SIZE);
	struct ext2_group_desc *gd = (struct ext2_group_desc *) (disk
			+ EXT2_BLOCK_SIZE * 2);
	struct ext2_inode *ino = (struct ext2_inode *) (disk
			+ EXT2_BLOCK_SIZE * gd->bg_inode_table);

	unsigned char *imap = inode_bitmap(disk, gd);
	unsigned char *bmap = block_bitmap(disk, gd);

	int orig_ino = get_origfile_ino(disk, path_o, ino, sb, gd);
	int lnkdir_ino = get_secondlast_ino(disk, path_ln, ino, sb, gd);
	char *lnkfilename = basename(path_ln);
	check_lasttoken_notexist(disk, lnkfilename, lnkdir_ino, ino);
	if (!symlnk) {
		create_hardlnk_dir_entry(disk, lnkfilename, imap, bmap, orig_ino,
				lnkdir_ino, sb, gd, ino);
	} else {
		create_symlnk_dir_entry(disk, lnkfilename, path_ln, imap, bmap,
				orig_ino, lnkdir_ino, sb, gd, ino);
	}

	free(path_o);
	free(path_ln);
//	free(imap);
	//free(bmap);
	return 0;
}
