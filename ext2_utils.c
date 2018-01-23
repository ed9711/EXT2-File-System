#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdbool.h>
#include "ext2_utils.h"

extern int fixes;

unsigned char *get_disk(char *img) {
	int fd = open(img, O_RDWR);
	unsigned char *disk;
	disk = mmap(NULL, TOTAL_BLOCKS * EXT2_BLOCK_SIZE, PROT_READ | PROT_WRITE,
			MAP_SHARED, fd, 0);
	if (disk == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}
	return disk;
}

void check_valid(char *path) {
	if (path[0] != '/') {
		exit(ENOENT);
	}
}

void check_imode_dir(struct ext2_inode *ino, int desired_ino) {
	if (!((ino + desired_ino)->i_mode & EXT2_S_IFDIR)) {
		exit(ENOENT);
	}
}

void check_imode_file(struct ext2_inode *ino, int desired_ino) {
	if (!((ino + desired_ino)->i_mode & EXT2_S_IFREG)) {
		exit(ENOENT);
	}
}

void notfound(bool flag) {
	if (!flag) {
		exit(ENOENT);
	}
}
//count total number of directories in absolute path
int count_dirs_inpath(char *path) {

	char *p = strtok(path, "/");
	int count = 1;
	while ((p = strtok(NULL, "/"))) {
		count++;
	}
	return count;
}

//build inode bitmap
unsigned char *inode_bitmap(unsigned char *disk, struct ext2_group_desc *gd) {
	unsigned char *ino_bmap = (unsigned char *)(disk + EXT2_BLOCK_SIZE
					+ (gd->bg_inode_bitmap - 1) * EXT2_BLOCK_SIZE);

	return ino_bmap;
}


//build block bitmap
unsigned char *block_bitmap(unsigned char *disk, struct ext2_group_desc *gd) {
	unsigned char *blk_bmap = (unsigned char *)(disk + EXT2_BLOCK_SIZE
					+ (gd->bg_block_bitmap - 1) * EXT2_BLOCK_SIZE);
    
    return blk_bmap;
}

//check if the last directory or file that we want to make or copy exists.
bool check_lasttoken_notexist(unsigned char *disk, char *token, int desired_ino,
		struct ext2_inode *ino) {
	int desired_ino_cpy;
	int len_iblks_cpy;
	int iblk_num_cpy;
	desired_ino_cpy = desired_ino;
    //get total number of i_blocks it contains
	len_iblks_cpy = (ino + desired_ino_cpy)->i_blocks / 2;
	int k = 0;
	struct ext2_dir_entry *dent_cpy;
    //go through i_block[len_iblks_cpy] to check out the data blocks
	while (k < len_iblks_cpy) {
		int sum = 0;
		iblk_num_cpy = (ino + desired_ino_cpy)->i_block[k];
        //get the directory entry
		dent_cpy = (struct ext2_dir_entry *) (disk
				+ EXT2_BLOCK_SIZE * iblk_num_cpy);
        //for each entry, check if file named token exists
		while (sum < EXT2_BLOCK_SIZE) {
			dent_cpy = (struct ext2_dir_entry *) ((char *) dent_cpy
					+ dent_cpy->rec_len);
			if (strcmp(dent_cpy->name, token)) {//not exists, keep checking
				sum += dent_cpy->rec_len;

			} else {
				return false;
			}
		}
	}
	return true;
}

//find a new inode
int allocate_newino(unsigned char *disk, struct ext2_super_block *sb,
		struct ext2_group_desc *gd, unsigned char *ino_bmap) {
	int idx;
	int i = 0;
	bool flag = false;
	int j, k;
	while (i < TOTAL_INODES && !flag) {
		if (ino_bmap[i] == 0) {
			ino_bmap[i] = 1;
			j = i % 8;
			k = (i - j) / 8;
			*(disk + EXT2_BLOCK_SIZE
					+ (gd->bg_inode_bitmap - 1) * EXT2_BLOCK_SIZE + k) |= (1
					<< j);
			idx = i;
			flag = true;
		}
		i++;
	}
	gd->bg_free_inodes_count--;
	sb->s_free_inodes_count--;

	return (int)idx;
}

//find a new block
int allocate_newblk(unsigned char *disk, struct ext2_super_block *sb,
		struct ext2_group_desc *gd, unsigned char *blk_bmap) {
	int idx;
	int i = 0;
	bool flag = false;
	int j, k;
	while (i < TOTAL_BLOCKS && !flag) {
		if (blk_bmap[i] == 0) {
			blk_bmap[i] = 1;
			j = i % 8;
			k = (i - j) / 8;
			*(disk + EXT2_BLOCK_SIZE
					+ (gd->bg_block_bitmap - 1) * EXT2_BLOCK_SIZE + k) |= (1
					<< j);
			idx = i;
			flag = true;
		}
		i++;
	}
	gd->bg_free_blocks_count--;
	sb->s_free_blocks_count--;

	return idx;

}

//attach the directory named token to the parent directory with desired_ino
int create_final_dir_entry(unsigned char *disk, char *token, unsigned char *ino_bmap,
		unsigned char *blk_bmap, int desired_ino, struct ext2_super_block *sb,
		struct ext2_group_desc *gd, struct ext2_inode *ino) {
	int total = 0;
	struct ext2_dir_entry *entry = (struct ext2_dir_entry *) (disk
			+ desired_ino * EXT2_BLOCK_SIZE);
	while (total < EXT2_BLOCK_SIZE) {
		total += entry->rec_len;
		entry = (struct ext2_dir_entry *) ((char *) entry + entry->rec_len);
	}
	int len_before_padding;
	len_before_padding = 8 + entry->name_len;
	int padding_len = entry->rec_len - len_before_padding;

	int newino_namelen = strlen(token);
	int new_rec_len_needed;
	int reminder = newino_namelen % 4;
	if (!reminder) {
		new_rec_len_needed = 8 + newino_namelen;
	} else {
		new_rec_len_needed = 8 + newino_namelen + (4 - reminder);
	}

	int allo_newino = allocate_newino(disk, sb, gd, ino_bmap);
	if (padding_len >= new_rec_len_needed) {
		int new_padding = (4 - entry->name_len % 4);
		entry->rec_len = len_before_padding + new_padding;
		entry = (struct ext2_dir_entry *) ((char *) entry + entry->rec_len);

		entry->inode = allo_newino;
		entry->rec_len = padding_len - new_padding;
		entry->name_len = newino_namelen;
		entry->file_type = EXT2_FT_DIR;
		strcpy(entry->name, token);

	} else {
		int allo_newblk = allocate_newblk(disk, sb, gd, blk_bmap);
		(ino + desired_ino)->i_blocks += 2;
		int len_iblks = (ino + desired_ino)->i_blocks / 2;
		(ino + desired_ino)->i_block[len_iblks - 1] = allo_newblk;
		entry =
				(struct ext2_dir_entry *) (disk + EXT2_BLOCK_SIZE * allo_newblk);

		entry->inode = allo_newino;
		entry->rec_len = EXT2_BLOCK_SIZE;
		entry->name_len = newino_namelen;
		entry->file_type = EXT2_FT_DIR;
		strcpy(entry->name, token);
	}

	return allo_newino;

}

//set info for new inode and block
void set_ino_and_blk(unsigned char *disk, struct ext2_super_block *sb,
		struct ext2_group_desc *gd, struct ext2_inode *ino, int created_ino,
		unsigned char *blk_bmap, int desired_ino) {
	int nblk = allocate_newblk(disk, sb, gd, blk_bmap);
	(ino + created_ino)->i_mode = EXT2_S_IFDIR;
	(ino + created_ino)->i_size = EXT2_BLOCK_SIZE;
	(ino + created_ino)->i_links_count = 2;
	(ino + created_ino)->i_blocks = 1;
	(ino + created_ino)->i_block[0] = nblk;

	struct ext2_dir_entry *entry = (struct ext2_dir_entry *) (disk
			+ EXT2_BLOCK_SIZE * nblk);
	entry->inode = created_ino;
	strcpy(entry->name, ".");
	entry->name_len = strlen(entry->name);
	entry->rec_len = 8 + entry->name_len + (4 - entry->name_len % 4);
	entry->file_type = EXT2_FT_DIR;

	entry = (struct ext2_dir_entry *) ((char *) entry + entry->rec_len);
	entry->inode = desired_ino;
	(ino + desired_ino)->i_links_count++;
	strcpy(entry->name, "..");
	entry->name_len = strlen(entry->name);
	entry->rec_len = EXT2_BLOCK_SIZE - entry->rec_len;
	entry->file_type = EXT2_FT_DIR;

}

//walk through path to check validity of all directories until the second last directory and return its inode
int get_secondlast_ino(unsigned char *disk, char *path, struct ext2_inode *ino,
		struct ext2_super_block *sb, struct ext2_group_desc *gd) {
	int dir_inpath = count_dirs_inpath(path);
	int dir_visited_count = 0;
	int desired_ino = EXT2_ROOT_INO - 1;
	bool flag = false;
	int len_iblks;
	char *token;
	token = strtok(path, "/");

	while (token != NULL) {
		len_iblks = (ino + desired_ino)->i_blocks / 2;
		int j = 0;
		struct ext2_dir_entry *dent;
		while (j < len_iblks && !flag) {
			int total = 0;

			int iblk_num = (ino + desired_ino)->i_block[j];
			dent =
					(struct ext2_dir_entry *) (disk + EXT2_BLOCK_SIZE * iblk_num);
			while (total < EXT2_BLOCK_SIZE && !flag) {
				dent =
						(struct ext2_dir_entry *) ((char *) dent + dent->rec_len);
				if (strcmp(dent->name, token)) {
					total += dent->rec_len;

				} else {
					desired_ino = dent->inode;
					flag = true;
					dir_visited_count++;
				}
			}
			check_imode_dir(ino, desired_ino);
			j++;
		}
		if ((dir_inpath - dir_visited_count) == 1) {
			break;
		} else {
			notfound(flag);
		}
		token = strtok(NULL, "/");
	}

	return desired_ino;

}
//used in ext2_ln
//walk through path to check the validity of all directories before the file we need to link to and check the file we need to link to exists
int get_origfile_ino(unsigned char *disk, char *path, struct ext2_inode *ino,
		struct ext2_super_block *sb, struct ext2_group_desc *gd) {
	int dir_inpath = count_dirs_inpath(path);
	int dir_visited_count = 0;
	int desired_ino = EXT2_ROOT_INO - 1;
	bool flag = false;
	int len_iblks;
	char *token;
	token = strtok(path, "/");

	while (token != NULL) {
		len_iblks = (ino + desired_ino)->i_blocks / 2;
		int j = 0;
		struct ext2_dir_entry *dent;
		while (j < len_iblks && !flag) {
			int total = 0;

			int iblk_num = (ino + desired_ino)->i_block[j];
			dent =
					(struct ext2_dir_entry *) (disk + EXT2_BLOCK_SIZE * iblk_num);
			while (total < EXT2_BLOCK_SIZE && !flag) {
				dent =
						(struct ext2_dir_entry *) ((char *) dent + dent->rec_len);
				if (strcmp(dent->name, token)) {
					total += dent->rec_len;

				} else {
					desired_ino = dent->inode;
					flag = true;
					dir_visited_count++;
				}
			}
			check_imode_dir(ino, desired_ino);
			j++;
		}
		if ((dir_inpath - dir_visited_count) == 1) {
			break;
		} else {
			notfound(flag);
		}
		token = strtok(NULL, "/");
	}
	flag = false;
	int desired_ino_cpy;
	int len_iblks_cpy;
	int iblk_num_cpy;
	desired_ino_cpy = desired_ino;
	len_iblks_cpy = (ino + desired_ino_cpy)->i_blocks / 2;
	int k = 0;
	struct ext2_dir_entry *dent_cpy;
	while (k < len_iblks_cpy && !flag) {
		int sum = 0;
		iblk_num_cpy = (ino + desired_ino_cpy)->i_block[k];
		dent_cpy = (struct ext2_dir_entry *) (disk
				+ EXT2_BLOCK_SIZE * iblk_num_cpy);
		while (sum < EXT2_BLOCK_SIZE) {
			dent_cpy = (struct ext2_dir_entry *) ((char *) dent_cpy
					+ dent_cpy->rec_len);
			if (strcmp(dent_cpy->name, token)) {
				sum += dent_cpy->rec_len;

			} else {
				flag = true;
				check_imode_file(ino, dent_cpy->inode);
			}
		}
	}
	if (!flag) {
		exit(ENOENT);
	}

	return dent_cpy->inode;

}

//create lnkfilename ,whose parent inode is lnkdir_ino, as a hardlink with the same inode number as origfile_ino,
void create_hardlnk_dir_entry(unsigned char *disk, char *lnkfilename,
		unsigned char *ino_bmap, unsigned char *blk_bmap, int origfile_ino, int lnkdir_ino,
		struct ext2_super_block *sb, struct ext2_group_desc *gd,
		struct ext2_inode *ino) {
	int total = 0;
	struct ext2_dir_entry *entry = (struct ext2_dir_entry *) (disk
			+ lnkdir_ino * EXT2_BLOCK_SIZE);
	while (total < EXT2_BLOCK_SIZE) {
		total += entry->rec_len;
		entry = (struct ext2_dir_entry *) ((char *) entry + entry->rec_len);
	}
	//int reminder = entry->name_len % 4;
	int len_before_padding;
	len_before_padding = 8 + entry->name_len;
	int padding_len = entry->rec_len - len_before_padding;

	int newino_namelen = strlen(lnkfilename);
	int new_rec_len_needed;
	int reminder = newino_namelen % 4;
	if (!reminder) {
		new_rec_len_needed = 8 + newino_namelen;
	} else {
		new_rec_len_needed = 8 + newino_namelen + (4 - reminder);
	}

	//int allo_newino = allocate_newino(disk, sb, gd, ino_bmap);
	if (padding_len >= new_rec_len_needed) {
		int new_padding = (4 - entry->name_len % 4);
		entry->rec_len = len_before_padding + new_padding;
		entry = (struct ext2_dir_entry *) ((char *) entry + entry->rec_len);

		entry->inode = origfile_ino;
		entry->rec_len = padding_len - new_padding;
		entry->name_len = newino_namelen;
		entry->file_type = EXT2_FT_REG_FILE;
		strcpy(entry->name, lnkfilename);

	} else {
		int allo_newblk = allocate_newblk(disk, sb, gd, blk_bmap);
		(ino + lnkdir_ino)->i_blocks += 2;
		int len_iblks = (ino + lnkdir_ino)->i_blocks / 2;
		(ino + lnkdir_ino)->i_block[len_iblks - 1] = allo_newblk;
		entry =
				(struct ext2_dir_entry *) (disk + EXT2_BLOCK_SIZE * allo_newblk);

		entry->inode = origfile_ino;
		entry->rec_len = EXT2_BLOCK_SIZE;
		entry->name_len = newino_namelen;
		entry->file_type = EXT2_FT_REG_FILE;
		strcpy(entry->name, lnkfilename);
	}

	(ino + lnkdir_ino)->i_links_count++;

}

//create symbolic link similarly as above but allocate new inode and new block for the link
void create_symlnk_dir_entry(unsigned char *disk, char *lnkfilename,
		char *lnkpath, unsigned char *ino_bmap, unsigned char *blk_bmap, int origfile_ino,
		int lnkdir_ino, struct ext2_super_block *sb, struct ext2_group_desc *gd,
		struct ext2_inode *ino) {
	int total = 0;
	struct ext2_dir_entry *entry = (struct ext2_dir_entry *) (disk
			+ lnkdir_ino * EXT2_BLOCK_SIZE);
	while (total < EXT2_BLOCK_SIZE) {
		total += entry->rec_len;
		entry = (struct ext2_dir_entry *) ((char *) entry + entry->rec_len);
	}

	int len_before_padding;
	len_before_padding = 8 + entry->name_len;
	int padding_len = entry->rec_len - len_before_padding;

	int newino_namelen = strlen(lnkfilename);
	int new_rec_len_needed;
	int reminder = newino_namelen % 4;
	if (!reminder) {
		new_rec_len_needed = 8 + newino_namelen;
	} else {
		new_rec_len_needed = 8 + newino_namelen + (4 - reminder);
	}

	int allo_newino = allocate_newino(disk, sb, gd, ino_bmap);
	if (padding_len >= new_rec_len_needed) {
		int new_padding = (4 - entry->name_len % 4);
		entry->rec_len = len_before_padding + new_padding;
		entry = (struct ext2_dir_entry *) ((char *) entry + entry->rec_len);

		entry->inode = allo_newino;
		entry->rec_len = padding_len - new_padding;
		entry->name_len = newino_namelen;
		entry->file_type = EXT2_FT_SYMLINK;
		strcpy(entry->name, lnkfilename);

	} else {
		int allo_newblk_lnkdir = allocate_newblk(disk, sb, gd, blk_bmap);
		(ino + lnkdir_ino)->i_blocks += 2;
		int len_iblks = (ino + lnkdir_ino)->i_blocks / 2;
		(ino + lnkdir_ino)->i_block[len_iblks - 1] = allo_newblk_lnkdir;
		entry = (struct ext2_dir_entry *) (disk
				+ EXT2_BLOCK_SIZE * allo_newblk_lnkdir);

		entry->inode = allo_newino;
		entry->rec_len = EXT2_BLOCK_SIZE;
		entry->name_len = newino_namelen;
		entry->file_type = EXT2_FT_SYMLINK;
		strcpy(entry->name, lnkfilename);
	}
	int allo_newblk_sym = allocate_newblk(disk, sb, gd, blk_bmap);
	(ino + allo_newino)->i_blocks += 2;
	int len_iblks_sym = (ino + allo_newino)->i_blocks / 2;
	(ino + allo_newino)->i_block[len_iblks_sym - 1] = allo_newblk_sym;
	size_t lnk_size = strlen(lnkpath);
	memcpy((void *) (disk + allo_newblk_sym * EXT2_BLOCK_SIZE), lnkpath,
			lnk_size);

}

int deallocate(unsigned char*disk, struct ext2_super_block *sb,
		struct ext2_group_desc *gd, struct ext2_inode *ino, unsigned char *ino_map,
		unsigned char *blk_map, int ino_num) {
	//deallocate inode and its blocks
	int i;
	int bits;
	int bytes;
	//deallocate block
	//check if indirect block is used
	if ((ino + ino_num)->i_blocks / 2 > 12) {
		for (i = 0; i < ((ino + ino_num)->i_blocks / 2); i++) {
			if (i > 12) {
				//before indirect block
				bits = (ino + ino_num)->i_block[i] % 8;
				bytes = (ino + ino_num)->i_block[i] / 16;
				blk_map[bytes] = blk_map[bytes] & ~(1 << bits);
				sb->s_free_blocks_count++;
				gd->bg_free_blocks_count++;
			} else {
				//in indirect block
				bits = (disk + (ino + ino_num)->i_block[12] * EXT2_BLOCK_SIZE)[i
						- 12] % 8;
				bytes =
						(disk + (ino + ino_num)->i_block[12] * EXT2_BLOCK_SIZE)[i
								- 12] / 16;
				blk_map[bytes] = blk_map[bytes] & ~(1 << bits);
				sb->s_free_blocks_count++;
				gd->bg_free_blocks_count++;
			}
		}

	} else {
		//indirect block not used
		for (i = 0; i < ((ino + ino_num)->i_blocks / 2); i++) {
			bits = (ino + ino_num)->i_block[i] % 8;
			bytes = (ino + ino_num)->i_block[i] / 16;
			blk_map[bytes] = blk_map[bytes] & ~(1 << bits);
			sb->s_free_blocks_count++;
			gd->bg_free_blocks_count++;
		}
	}

	//deallocate inode
	int bits1 = ino_num % 8;
	int bytes1 = ino_num / 4;
	ino_map[bytes1] = ino_map[bytes1] & ~(1 << bits1);
	sb->s_free_inodes_count++;
	gd->bg_free_inodes_count++;
	return 0;
}

int reallocate(unsigned char*disk, struct ext2_super_block *sb,
		struct ext2_group_desc *gd, struct ext2_inode *ino, unsigned char *ino_map,
		unsigned char *blk_map, int ino_num) {
	//reallocate inode and its blocks
	int i;
	int bits;
	int bytes;

	//reallocate inode
	bits = ino_num % 8;
	bytes = ino_num / 4;
	if (ino_map[bytes] & 1 << bits) {
		//in use
		fprintf(stderr, "Cannot be restored, inode in use");
		exit(ENOENT);
	}
	ino_map[bytes] = ino_map[bytes] | 1 << bits;
	sb->s_free_inodes_count--;
	gd->bg_free_inodes_count--;

	//reallocate block
	//check if indirect block is used
	if ((ino + ino_num)->i_blocks / 2 > 12) {
		for (i = 0; i < ((ino + ino_num)->i_blocks / 2); i++) {
			if (i > 12) {
				//before indirect block
				bits = (ino + ino_num)->i_block[i] % 8;
				bytes = (ino + ino_num)->i_block[i] / 16;
				if (blk_map[bytes] & 1 << bits) {
					//in use
					fprintf(stderr, "Cannot be restored, block in use");
					exit(ENOENT);
				}
				blk_map[bytes] = blk_map[bytes] | 1 << bits;
				sb->s_free_blocks_count--;
				gd->bg_free_blocks_count--;
			} else {
				//in indirect block
				bits = (disk + (ino + ino_num)->i_block[12] * EXT2_BLOCK_SIZE)[i
						- 12] % 8;
				bytes =
						(disk + (ino + ino_num)->i_block[12] * EXT2_BLOCK_SIZE)[i
								- 12] / 16;

				if (blk_map[bytes] & 1 << bits) {
					//in use
					fprintf(stderr, "Cannot be restored, block in use");
					exit(ENOENT);
				}
				blk_map[bytes] = blk_map[bytes] | 1 << bits;
				sb->s_free_blocks_count--;
				gd->bg_free_blocks_count--;
			}
		}

	} else {
		//indirect block not used
		for (i = 0; i < ((ino + ino_num)->i_blocks / 2); i++) {
			bits = (ino + ino_num)->i_block[i] % 8;
			bytes = (ino + ino_num)->i_block[i] / 16;
			if (blk_map[bytes] & 1 << bits) {
				//in use
				fprintf(stderr, "Cannot be restored, block in use");
				exit(ENOENT);
			}
			blk_map[bytes] = blk_map[bytes] | 1 << bits;
			sb->s_free_blocks_count--;
			gd->bg_free_blocks_count--;
		}
	}
	return 0;
}


