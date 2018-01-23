#include "ext2_utils.h"
#include "ext2.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <libgen.h>

unsigned char *disk;

int main(int argc, char *argv[]) {

	if (argc != 3) {
		fprintf(stderr, "Usage: %s <disk name> <absolute path>\n", argv[0]);
		exit(1);
	}
	disk = get_disk(argv[1]);
	struct ext2_super_block *sb = (struct ext2_super_block *) (disk
			+ EXT2_BLOCK_SIZE);
	struct ext2_group_desc *gd = (struct ext2_group_desc *) (disk
			+ EXT2_BLOCK_SIZE * 2);
	struct ext2_inode *ino = (struct ext2_inode *) (disk
			+ EXT2_BLOCK_SIZE * gd->bg_inode_table);
	unsigned char *block_map = block_bitmap(disk, gd);
	unsigned char *ino_map = inode_bitmap(disk, gd);
	int i;
	//error checking

	//check if path start with '/'
	check_valid(argv[3]);

	//check not '.'
	//check if file to rm exist

	char *file_name = basename(argv[2]);

	//check valid path, all directory
	int desired_ino = get_secondlast_ino(disk, argv[3], ino, sb, gd);

	//check exist
	if (check_lasttoken_notexist(disk, file_name, desired_ino, ino)) {
		fprintf(stderr, "Error: File not exist");
		return ENOENT;
	}

	struct ext2_dir_entry * entry;
	struct ext2_dir_entry * entry1;

	for (i = 0; i < ((ino + desired_ino)->i_blocks / 2); i++) {
		entry = (struct ext2_dir_entry *) (disk
				+ EXT2_BLOCK_SIZE * ((ino + desired_ino)->i_block[i]));
		int total = 0;
		if ((strcmp(entry->name, file_name) == 0)) {
			//entry to rm is first in block, is not '.' since filename is not '.'
			//check if hardlink
			if ((ino + entry->inode - 1)->i_links_count >= 2) {
				(ino + entry->inode - 1)->i_links_count--;
			}
			//change dtime
			time_t seconds;
			seconds = time(NULL);
			(ino + entry->inode - 1)->i_dtime = seconds;
			deallocate(disk, sb, gd, ino, ino_map, block_map, entry->inode - 1);
			entry->inode = 0;
			//success
			return 0;
		}
		//go through every entry in this directory block
		while (total < 1024) {
			total += entry->rec_len;
			if (strcmp(entry->name, file_name) == 0) {
				//found entry to remove
				if (entry->file_type == EXT2_FT_DIR) {
					//is directory
					//cannot remove dirctory
					return EISDIR;

				}
				//entry1 is set to the entry before to to be deleted one
				entry1 = (struct ext2_dir_entry *) ((char *) entry
						- entry->rec_len);
				break;
			}
			entry = (struct ext2_dir_entry *) ((char *) entry + entry->rec_len);

		}

	}
	//check if hard link
	if ((ino + entry->inode - 1)->i_links_count >= 2) {
		(ino + entry->inode - 1)->i_links_count--;
	}
	//change dtime
	time_t seconds;
	seconds = time(NULL);
	(ino + entry->inode - 1)->i_dtime = seconds;
	deallocate(disk, sb, gd, ino, ino_map, block_map, entry->inode - 1);
	//set to skip the deleted entry
	entry1->rec_len = entry1->rec_len + entry->rec_len;
	//success
	return 0;

}
