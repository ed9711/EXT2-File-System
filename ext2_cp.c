#include "ext2_utils.h"
#include "ext2.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <libgen.h>

unsigned char *disk;

int main(int argc, char *argv[]) {

	if (argc != 4) {
		fprintf(stderr, "Usage: %s <disk name> <path to file> <path to disk>\n",
				argv[0]);
		exit(1);
	}
	disk = get_disk(argv[1]);
	//disk is not null, if null would have stopped in helper

	FILE *f = fopen(argv[2], "rb");
	if (f == NULL) {
		fprintf(stderr, "Error opening file\n");
		return ENOENT;
	}

	struct ext2_super_block *sb = (struct ext2_super_block *) (disk
			+ EXT2_BLOCK_SIZE);
	struct ext2_group_desc *gd = (struct ext2_group_desc *) (disk
			+ EXT2_BLOCK_SIZE * 2);
	struct ext2_inode *ino = (struct ext2_inode *) (disk
			+ EXT2_BLOCK_SIZE * gd->bg_inode_table);

	check_valid(argv[3]);
	//check if path start with '/'

	//name of file to be copied
	char *file_name = basename(argv[2]);

	//check valid path, all directory
	int desired_ino = get_secondlast_ino(disk, argv[3], ino, sb, gd);

	//check exist
	if (!check_lasttoken_notexist(disk, file_name, desired_ino, ino)) {
		fprintf(stderr, "Error: File already exist");
		return EEXIST;
	}

	struct stat path_stat;
	stat(argv[2], &path_stat);
	if (!S_ISREG(path_stat.st_mode)) {
		fprintf(stderr, "Error: Not regular file");
		return ENOENT;
	}

	//check file size to see if cp possible
	int block_count;
	fseek(f, 0L, SEEK_END);
	if (ftell(f) % EXT2_BLOCK_SIZE > 0) {
		block_count = ftell(f) / EXT2_BLOCK_SIZE + 1;
	} else {
		block_count = ftell(f) / EXT2_BLOCK_SIZE;
	}
	//check if the indirect block is needed
	if (block_count > 12) {
		block_count++;
	}
	if (block_count > gd->bg_free_blocks_count) {
		fprintf(stderr, "Not enough space \n");
		return ENOSPC;
	}
	//set file pointer back to beginning
	rewind(f);
	//allocate blocks and write to them
	unsigned char *block_map = block_bitmap(disk, gd);

	int i;
	int *block_list = malloc(block_count * sizeof(int));
	int *indirect_block = malloc(block_count - 12 * sizeof(int));
	if (block_count <= 12) {
		for (i = 0; i < 12; i++) {
			block_list[i] = allocate_newblk(disk, sb, gd, block_map);
			fread((void*) (disk + block_list[i] * EXT2_BLOCK_SIZE),
					sizeof(char), EXT2_BLOCK_SIZE, f);
		}
	} else {
		for (i = 0; i < 12; i++) {
			block_list[i] = allocate_newblk(disk, sb, gd, block_map);
			fread((void*) (disk + block_list[i] * EXT2_BLOCK_SIZE),
					sizeof(char), EXT2_BLOCK_SIZE, f);
		}

		//allocate 13th block
		block_list[12] = allocate_newblk(disk, sb, gd, block_map);
		//block_count is number of blocks needed + 1 (13th block)
		for (i = 0; i < block_count - 13; i++) {
			indirect_block[i] = allocate_newblk(disk, sb, gd, block_map);
			fread((void*) (disk + indirect_block[i] * EXT2_BLOCK_SIZE),
					sizeof(char), EXT2_BLOCK_SIZE, f);
		}
		for (i = 0; i < block_count - 13; i++) {
			(disk + block_list[12] * EXT2_BLOCK_SIZE)[i] = indirect_block[i];
		}

	}
	//allocate inode
	unsigned char *ino_map = inode_bitmap(disk, gd);
	int new_ino = allocate_newino(disk, sb, gd, ino_map);
	for (i = 0; i < 12; i++) {
		(ino + new_ino)->i_block[i] = block_list[i];
	}
	if (block_count > 12) {
		(ino + new_ino)->i_block[12] = block_list[12];
	}
	(ino + new_ino)->i_blocks = (block_count - 1) * 2;
	(ino + new_ino)->i_dtime = 0;
	(ino + new_ino)->i_mode = 0x8000;
	//place inode in directory
	char *directory_name = basename(argv[3]);
	char *token = strtok(argv[3], "/");

	while (token != NULL) {
		if (strcmp(token, directory_name) == 0) {
			break;
		}
		token = strtok(NULL, "/");
	}
	struct ext2_dir_entry *entry;
	for (i = 0; i < ((ino + desired_ino)->i_blocks) / 2; i++) {
		entry = (struct ext2_dir_entry *) ((disk
				+ EXT2_BLOCK_SIZE * ((ino + desired_ino)->i_block[i])));
		int total = 0;
		while (1 == 1) {
			total += entry->rec_len;
			if (total >= EXT2_BLOCK_SIZE) {
				//found last entry
				//save total length
				total = total - entry->rec_len;
				break;
			}
			entry = (struct ext2_dir_entry *) ((char *) entry + entry->rec_len);

		}
		int old_len = entry->rec_len;
		int new_len = 8 + entry->name_len + (4 - (entry->name_len % 4));
		int new_entry_len = 8 + strlen(file_name)
				+ (4 - (strlen(file_name) % 4));
		//check if enough space in block for new entry
		if (old_len >= (new_len + new_entry_len)) {
			entry->rec_len = new_len;
			total += entry->rec_len;
			entry = (struct ext2_dir_entry *) ((char *) entry + entry->rec_len);
			//new_ino is inode index not number
			entry->inode = new_ino + 1;
			entry->rec_len = EXT2_BLOCK_SIZE - total;
			entry->name_len = strlen(file_name);
			strncpy(entry->name, file_name, strlen(file_name));
			//success
			return 0;
		}
	}
	//no space
	//allocate new block, dont consider over 12 blocks, assume i <= 11
	(ino + desired_ino)->i_block[i + 1] = allocate_newblk(disk, sb, gd,
			block_map);
	entry = (struct ext2_dir_entry *) (disk
			+ EXT2_BLOCK_SIZE * ((ino + desired_ino)->i_block[i + 1]));
	(ino + desired_ino)->i_blocks += 2;
	//first and only entry in block
	entry->rec_len = EXT2_BLOCK_SIZE;
	//new_ino is inode index not number
	entry->inode = new_ino + 1;
	entry->name_len = strlen(file_name);
	strncpy(entry->name, file_name, strlen(file_name));
	//success
	return 0;

}
