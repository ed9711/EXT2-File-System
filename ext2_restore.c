#include "ext2_utils.h"
#include "ext2.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>

unsigned char *disk;

int main(int argc, char *argv[]) {

	if (argc != 3) {
		fprintf(stderr, "Usage: %s <disk name> <absolute path>\n", argv[0]);
		exit(1);
	}

	//error checking

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

	char *file_name = basename(argv[2]);

	//error checking

	//check if path start with '/'
	check_valid(argv[3]);

	//check not '.'
	//check if file to rm exist

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
		//go through every entry in this block
		while (total < 1024) {
			total += entry->rec_len;
			if (entry->inode == 0) {
				if (strcmp(entry->name, file_name) == 0) {
					//found entry to be restored, no inode, cannot be restored
					fprintf(stderr, "Cannot be restored");
					return ENOENT;
				}
			}
			//size of gap
			int current_len = entry->rec_len;
			//size of gap afterwards
			int skipped_len = 0;
			if (entry->rec_len
					!= 8 + entry->name_len + (4 - (entry->name_len % 4))) {
				//found a gap
				//set current_len to size of gap
				current_len -= 8 + entry->name_len
						+ (4 - (entry->name_len % 4));
				//set entry1 to the one with gap
				entry1 = (struct ext2_dir_entry *) ((char *) entry);
				//check if anything in the gap is to e restored
				while (current_len != 0) {
					//set entry1 to one to be restored
					entry1 = (struct ext2_dir_entry *) ((char *) entry1 + 8
							+ entry1->name_len + (4 - (entry1->name_len % 4)));
					current_len -= 8 + entry1->name_len
							+ (4 - (entry1->name_len % 4));
					if (strcmp(entry1->name, file_name) == 0) {
						//found one to be restored

						if (entry->file_type == EXT2_FT_DIR) {
							//is directory
							//cannot restore dirctory
							return EISDIR;

						}

						reallocate(disk, sb, gd, ino, ino_map, block_map,
								entry1->inode - 1);
						//set len and i_dtime after restored
						entry1->rec_len = current_len + 8 + entry1->name_len
								+ (4 - (entry1->name_len % 4));
						(ino + entry1->inode - 1)->i_dtime = 0;
						//set new gap size
						entry->rec_len = skipped_len + 8 + entry->name_len
								+ (4 - (entry->name_len % 4));
						//success
						return 0;
					}
					skipped_len += entry1->rec_len;

				}
				entry->rec_len = skipped_len + 8 + entry->name_len
						+ (4 - (entry->name_len % 4));

			}

			entry = (struct ext2_dir_entry *) ((char *) entry + entry->rec_len);

		}

	}
}

