#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <stdbool.h>
#include "ext2_utils.h"

unsigned char *disk = NULL;

int main(int argc, char **argv) {

	if (argc != 3) {
		fprintf(stderr, "Usage: %s <image file name> <absolute path on disk>\n",
				argv[0]);
		exit(1);
	}

	disk = get_disk(argv[1]);

	char *path = malloc(sizeof(argv[2]) + sizeof(char));
	strcpy(path, argv[2]);
	check_valid(path);
    //cannot make root itself
	if (strcmp(path, "/")) {
		return EEXIST;
	}

	struct ext2_super_block *sb = (struct ext2_super_block *) (disk
			+ EXT2_BLOCK_SIZE);
	struct ext2_group_desc *gd = (struct ext2_group_desc *) (disk
			+ EXT2_BLOCK_SIZE * 2);
	struct ext2_inode *ino = (struct ext2_inode *) (disk
			+ EXT2_BLOCK_SIZE * gd->bg_inode_table);

	unsigned char *imap = inode_bitmap(disk, gd);
	unsigned char *bmap = block_bitmap(disk, gd);

	int dir_inpath = count_dirs_inpath(path);
	int dir_visited_count = 0;
    //using inode index instead of inode number
	int desired_ino = EXT2_ROOT_INO - 1;
	bool flag = false;
	int len_iblks;
	char *token;
	token = strtok(path, "/");

	while (token != NULL) {
		len_iblks = (ino + desired_ino)->i_blocks / 2;
		int j = 0;
		struct ext2_dir_entry *dent;
		while (j < len_iblks && !flag) {//going through all data blocks of desired inode
			int total = 0;

			int iblk_num = (ino + desired_ino)->i_block[j];
			dent =
					(struct ext2_dir_entry *) (disk + EXT2_BLOCK_SIZE * iblk_num);
			while (total < EXT2_BLOCK_SIZE && !flag) {
                //incrementing directory entry by rec_len
				dent =
						(struct ext2_dir_entry *) ((char *) dent + dent->rec_len);
				if (strcmp(dent->name, token)) {//not yet, keep searching
					total += dent->rec_len;

				} else {
					desired_ino = dent->inode; //found
					flag = true;
					dir_visited_count++;
				}
			}
			check_imode_dir(ino, desired_ino); //check it is a directory
			j++;
		}
		if ((dir_inpath - dir_visited_count) == 1) {//if reach second last directory,i.e, last existing directory
			break;
		} else {//still not found after searching for all data blocks,i.e., not valid
			notfound(flag);
		}
		token = strtok(NULL, "/");
	}

	if (!check_lasttoken_notexist(disk, token, desired_ino, ino)) {//if directory we want to make already exists
		fprintf(stderr, "EEXIST\n");
		exit(1);
	}

	int created_ino = create_final_dir_entry(disk, token, imap, bmap,
			desired_ino, sb, gd, ino);
	set_ino_and_blk(disk, sb, gd, ino, created_ino, bmap, desired_ino);
	gd->bg_used_dirs_count++;

	free(imap);
	free(bmap);
	free(path);

	return 0;
}
