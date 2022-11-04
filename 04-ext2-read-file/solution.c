#include <solution.h>
#include <unistd.h>
//#include <linux/ext2_fs.h>
#include <ext2fs/ext2fs.h>
#include <sys/sendfile.h>
#include <errno.h>

int block_size, data_size_left, error;

int  copy_direct(int img, int out, __le32 adr)
{
	int length = data_size_left > block_size ? block_size : data_size_left;
	data_size_left -= length;
	off_t offset = block_size * adr;

	error = sendfile(out, img, &offset, length);
	if ( error < 0 ) return -errno;

	return 0;
}

int copy_indirect_single(int img, int out, __le32 adr)
{
	// int as stored 4-byte block numbers
	int* indirect_block = (int*)malloc(block_size);
	error = pread(img, indirect_block, block_size, block_size * adr);
	if ( error < 0 ) {
		free(indirect_block);
		return -errno;
	}

	// block_size / 4 - number of stored block numbers
	for (int i = 0; i < (block_size / 4); i++) {
		if (indirect_block[i] == 0) break;

		copy_direct(img, out, indirect_block[i]);
	}

	free(indirect_block);

	return 0;
}

int copy_indirect_double(int img, int out, __le32 adr)
{
	int* double_indirect_block = (int*)malloc(block_size);
	error = pread(img, double_indirect_block, block_size, block_size * adr);
	if ( error < 0 ) {
		free(double_indirect_block);
		return -errno;
	}

	// block_size / 4 - number of stored block numbers
	for (int i = 0; i < (block_size / 4); i++) {
		if (double_indirect_block[i] == 0) break;

		copy_indirect_single(img, out, double_indirect_block[i]);
	}

	free(double_indirect_block);

	return 0;
}

int dump_file(int img, int inode_nr, int out)
{
	struct ext2_super_block super;
	error = pread(img, &super, sizeof(super), 1024);
	if ( error < 0 ) return -errno;

	block_size = 1024 << super.s_log_block_size;

	int inode_block_group_needed = (inode_nr - 1) / super.s_inodes_per_group;
	int inode_in_block_needed = (inode_nr - 1) % super.s_inodes_per_group;

	struct ext2_group_desc group;
	//s_first_data_block - identifying the first data block, in other word the id of the block 
	//containing the superblock structure.
	//block_size * (super.s_first_data_block + 1) - next block after superblock address
	error = pread(img, &group, sizeof(group), block_size * (super.s_first_data_block + 1)
		+ inode_block_group_needed * sizeof(group));
	if ( error < 0 ) return -errno;

	struct ext2_inode inode;
	error = pread(img, &inode, sizeof(inode), block_size * group.bg_inode_table +
		super.s_inode_size * inode_in_block_needed);
	if ( error < 0 ) return -errno;

	data_size_left = inode.i_size;
	for(int i = 0; i < EXT2_N_BLOCKS; i++)
		if (i < EXT2_NDIR_BLOCKS) {                               	/* direct blocks */
			error = copy_direct(img, out, inode.i_block[i]);
			if ( error < 0 ) return -errno;	
		}
		else if (i == EXT2_IND_BLOCK) {                         	/* single indirect block */
			error = copy_indirect_single(img, out, inode.i_block[i]);
			if ( error < 0 ) return -errno;	
		}
		else if (i == EXT2_DIND_BLOCK) {                            /* double indirect block */
			error = copy_indirect_double(img, out, inode.i_block[i]);
			if ( error < 0 ) return -errno;
		}

	return 0;
}