#include "ext2_utils.h"
#include "ext2.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

unsigned char *disk;
static int fixes = 0;

void inode_check(unsigned char*disk, struct ext2_super_block *sb,
		struct ext2_group_desc *gd, struct ext2_inode *ino,
		unsigned char *ino_map, int ino_num) {
	int bits = ino_num % 8;
	int bytes = ino_num / 4;
	if (!(*(ino_map + bytes) >> bits & 1)) {
		//not in use
		ino_map[bytes] = ino_map[bytes] | 1 << bits;
		sb->s_free_inodes_count--;
		gd->bg_free_inodes_count--;
		printf("Fixed: inode [%d] not marked as in-use\n", ino_num + 1);
		fixes++;
	}
}
void dtime_check(struct ext2_inode *ino, int ino_num) {
	if ((ino + ino_num)->i_dtime != 0) {
		(ino + ino_num)->i_dtime = 0;
		printf("Fixed: valid inode marked for deletion: [%d]\n", ino_num + 1);
	}
}

void block_check(unsigned char*disk, struct ext2_super_block *sb,
		struct ext2_group_desc *gd, struct ext2_inode *ino,
		unsigned char *blk_map, int ino_num) {
	//check if indirect block is used
	int bits, bytes, i;
	int local_fix = 0;
	if ((ino + ino_num)->i_blocks / 2 > 12) {
		for (i = 0; i < ((ino + ino_num)->i_blocks / 2); i++) {
			if (i > 12) {
				//before indirect block
				bits = (ino + ino_num)->i_block[i] % 8;
				bytes = (ino + ino_num)->i_block[i] / 16;
				if (!(*(blk_map + bytes) >> bits & 1)) {
					//not in use
					blk_map[bytes] = blk_map[bytes] | 1 << bits;
					sb->s_free_blocks_count--;
					gd->bg_free_blocks_count--;
					local_fix++;
				}

			} else {
				//in indirect block
				bits = (disk + (ino + ino_num)->i_block[12] * EXT2_BLOCK_SIZE)[i
						- 12] % 8;
				bytes =
						(disk + (ino + ino_num)->i_block[12] * EXT2_BLOCK_SIZE)[i
								- 12] / 16;

				if (!(*(blk_map + bytes) >> bits & 1)) {
					//not in use
					blk_map[bytes] = blk_map[bytes] | 1 << bits;
					sb->s_free_blocks_count--;
					gd->bg_free_blocks_count--;
					local_fix++;
				}

			}
		}

	} else {
		//indirect block not used
		for (i = 0; i < ((ino + ino_num)->i_blocks / 2); i++) {
			bits = (ino + ino_num)->i_block[i] % 8;
			bytes = (ino + ino_num)->i_block[i] / 16;
			if (!(*(blk_map + bytes) >> bits & 1)) {
				//not in use
				blk_map[bytes] = blk_map[bytes] | 1 << bits;
				sb->s_free_blocks_count--;
				gd->bg_free_blocks_count--;
				local_fix++;
			}
		}
	}
	printf(
			"Fixed: %d in-use data blocks not marked in data bitmap for inode: [%d]\n",
			local_fix, ino_num + 1);
	fixes += local_fix;
}

void file_check(unsigned char*disk, struct ext2_super_block *sb,
		struct ext2_group_desc *gd, struct ext2_inode *ino,
		unsigned char *ino_map, unsigned char *blk_map, int ino_num) {

	int i;
	struct ext2_dir_entry *entry1;
	for (i = 0; i < ((ino + ino_num)->i_blocks / 2); i++) {
		entry1 = (struct ext2_dir_entry *) (disk
				+ EXT2_BLOCK_SIZE * ((ino + ino_num)->i_block[i]));
		int total = 0;

		//go through every entry in this block
		while (total < 1024) {
			total += entry1->rec_len;
			if (strcmp(entry1->name, ".") == 0
					|| strcmp(entry1->name, "..") == 0) {
				//skip "." and ".."
				entry1 = (struct ext2_dir_entry *) ((char *) entry1
						+ entry1->rec_len);
				continue;
			}
			if (((ino + entry1->inode - 1)->i_mode & EXT2_S_IFREG)
					|| ((ino + entry1->inode - 1)->i_mode & EXT2_S_IFLNK)) {
				//is reg file or link
				if ((ino + entry1->inode - 1)->i_mode & EXT2_S_IFREG) {
					//is reg file
					if (entry1->file_type != EXT2_FT_REG_FILE) {
						entry1->file_type = EXT2_FT_REG_FILE;
						printf(
								"Fixed: Entry type vs inode mismatch: inode [%d]\n",
								entry1->inode);
						fixes++;
					}
				} else if ((ino + entry1->inode - 1)->i_mode & EXT2_S_IFLNK) {
					//is link
					if (entry1->file_type != EXT2_FT_SYMLINK) {
						entry1->file_type = EXT2_FT_SYMLINK;
						printf(
								"Fixed: Entry type vs inode mismatch: inode [%d]\n",
								entry1->inode);
						fixes++;
					}
				}
				//check if inode is in use
				inode_check(disk, sb, gd, ino, ino_map, entry1->inode - 1);
				//check if dtime is correct
				dtime_check(ino, entry1->inode - 1);
				//check if block is in use
				block_check(disk, sb, gd, ino, blk_map, entry1->inode - 1);

				//finished checking file/symlink

			} else if ((ino + entry1->inode - 1)->i_mode & EXT2_S_IFDIR) {
				//is directory
				if (entry1->file_type != EXT2_FT_DIR) {
					entry1->file_type = EXT2_FT_DIR;
					printf("Fixed: Entry type vs inode mismatch: inode [%d]\n",
							entry1->inode);
					fixes++;
				}
				if (entry1->inode != 11) {
					//check if block is in use
					block_check(disk, sb, gd, ino, blk_map, entry1->inode - 1);
					file_check(disk, sb, gd, ino, ino_map, blk_map,
							entry1->inode - 1);
				}
				//check if inode is in use
				inode_check(disk, sb, gd, ino, ino_map, entry1->inode - 1);
				//check if dtime is correct
				dtime_check(ino, entry1->inode - 1);

				//finish checking
				//go into directory

			}
			entry1 = (struct ext2_dir_entry *) ((char *) entry1
					+ entry1->rec_len);
		}
	}

}

int main(int argc, char *argv[]) {

	if (argc != 2) {
		fprintf(stderr, "Usage: %s <disk name>\n", argv[0]);
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
	//int *ino_map = inode_bitmap(disk, gd);
	unsigned char *ino_map = inode_bitmap(disk, gd);
	//(unsigned char *) (disk+ 1024 * gd->bg_block_bitmap);
	int i, j;
	//count free inodes
	int free_ino_count = 0;
	for (i = 0; i < 4; i++) {
		for (j = 0; j < 8; j++) {
			//printf("ino: %d \n", ino_map[i] & (1 << j));
			printf("ino: %d \n", *(ino_map + i) >> j & 1);
			//printf("ino: %d \n", ino_map[i]& (1 << j));
			//printf("ino: %d \n", ino_map[8 * i + j]);
			printf("%d\n",
					*(disk + EXT2_BLOCK_SIZE
							+ (gd->bg_block_bitmap - 1) * EXT2_BLOCK_SIZE + i)
							>> j & 1);
			if ((*(ino_map + i) >> j & 1) == 0) {
				free_ino_count++;
			}
		}
	}
	//check free inodes
	if (free_ino_count != sb->s_free_inodes_count) {
		int difference = abs(free_ino_count - sb->s_free_inodes_count);
		printf("free inodes: %d \n", sb->s_free_inodes_count);
		printf("free inodes count: %d \n", free_ino_count);
		sb->s_free_inodes_count = free_ino_count;
		gd->bg_free_inodes_count = free_ino_count;
		//from sb and from gd
		fixes += difference;
		printf(
				"Fixed: Superblock's free inodes counter was off by %d compared to the bitmap\n",
				difference);
		printf(
				"Fixed: Block group's free inodes counter was off by %d compared to the bitmap\n",
				difference);
	}

	//count free blocks
	int free_block_count = 0;
	for (i = 0; i < 16; i++) {
		for (j = 0; j < 8; j++) {
			if ((*(block_map + i) >> j & 1) == 0) {
				free_block_count++;
			}
		}

	}

	//check free blocks
	if (free_block_count != sb->s_free_blocks_count) {
		int difference2 = abs(free_block_count - sb->s_free_blocks_count);
		sb->s_free_blocks_count = free_block_count;
		gd->bg_free_blocks_count = free_block_count;
		//sb and gd difference
		fixes += difference2;
		printf(
				"Fixed: Superblock's free blocks counter was off by %d compared to the bitmap\n",
				difference2);
		printf(
				"Fixed: Block group's free blocks counter was off by %d compared to the bitmap\n",
				difference2);
	}

	//check b, c, d, e using recursive function to traverse all files/dir
	//root directory entry
	file_check(disk, sb, gd, ino, ino_map, block_map, EXT2_ROOT_INO - 1);

	//finished checking
	if (fixes == 0) {
		printf("No file system inconsistencies detected!\n");
	} else {
		printf("%d file system inconsistencies repaired!\n", fixes);
	}

}
