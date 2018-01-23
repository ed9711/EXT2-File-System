#include <string.h>
#include <stdbool.h>
#include "ext2.h"

#define TOTAL_BLOCKS 128
#define TOTAL_INODES 32


//unsigned char *disk = NULL;
unsigned char *get_disk(char *img);
void check_valid(char *path);
void check_imode_dir(struct ext2_inode *ino, int desired_ino);
void check_imode_file(struct ext2_inode *ino, int desired_ino);
void notfound(bool flag);
int count_dirs_inpath(char *path);
unsigned char *inode_bitmap(unsigned char *disk, struct ext2_group_desc *gd);
unsigned char *block_bitmap(unsigned char *disk, struct ext2_group_desc *gd);
bool check_lasttoken_notexist(unsigned char *disk, char *token, int desired_ino,
		struct ext2_inode *ino);
int allocate_newino(unsigned char *disk, struct ext2_super_block *sb,
		struct ext2_group_desc *gd, unsigned char *ino_bmap);
int allocate_newblk(unsigned char *disk, struct ext2_super_block *sb,
		struct ext2_group_desc *gd, unsigned char *blk_bmap);
int create_final_dir_entry(unsigned char *disk, char *token, unsigned char *ino_bmap,
		unsigned char *blk_bmap, int desired_ino, struct ext2_super_block *sb,
		struct ext2_group_desc *gd, struct ext2_inode *ino);
void set_ino_and_blk(unsigned char *disk, struct ext2_super_block *sb,
		struct ext2_group_desc *gd, struct ext2_inode *ino, int created_ino,
		unsigned char *blk_bmap, int desired_ino);
int get_secondlast_ino(unsigned char *disk, char *path, struct ext2_inode *ino,
		struct ext2_super_block *sb, struct ext2_group_desc *gd);
int get_origfile_ino(unsigned char *disk, char *path, struct ext2_inode *ino,
		struct ext2_super_block *sb, struct ext2_group_desc *gd);
void create_hardlnk_dir_entry(unsigned char *disk, char *lnkfilename,
		unsigned char *ino_bmap, unsigned char *blk_bmap, int origfile_ino, int lnkdir_ino,
		struct ext2_super_block *sb, struct ext2_group_desc *gd,
		struct ext2_inode *ino);
void create_symlnk_dir_entry(unsigned char *disk, char *lnkfilename,
		char *lnkpath, unsigned char *ino_bmap, unsigned char *blk_bmap, int origfile_ino,
		int lnkdir_ino, struct ext2_super_block *sb, struct ext2_group_desc *gd,
		struct ext2_inode *ino);
int deallocate(unsigned char*disk, struct ext2_super_block *sb,
		struct ext2_group_desc *gd, struct ext2_inode *ino, unsigned char *ino_map,
		unsigned char *blk_map, int ino_num);
int reallocate(unsigned char *disk, struct ext2_super_block *sb,
		struct ext2_group_desc *gd, struct ext2_inode *ino, unsigned char *ino_map,
		unsigned char *blk_map, int ino_num);
