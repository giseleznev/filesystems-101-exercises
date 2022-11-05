#include <solution.h>
#include <unistd.h>
#include <ext2fs/ext2fs.h>
#include <sys/stat.h>
#include <errno.h>

int block_size, data_size_left, error;

int report_direct(int img, __le32 adr)
{
	int length = data_size_left > block_size ? block_size : data_size_left;
	data_size_left -= length;

	void* block = malloc(block_size);
	error = pread(img, block, block_size, block_size * adr);
	if ( error < 0 ) {
		free(block);
		return -errno;
	}

	int size = 0;
	struct ext2_dir_entry_2 *entry = (void*) block;

	while(size < length) {
		entry = (void*) block + size;
		if(entry->inode == 0) break;
		size += entry->rec_len;
		char file_name[EXT2_NAME_LEN + 1];
		memcpy(file_name, entry->name, entry->name_len);
		file_name[entry->name_len] = '\0';
		char type = entry->file_type == 2 ? 'd' : 'f';
		report_file(entry->inode , type, file_name);
	}

	free(block);

	return 0;
}

int report_indirect_single(int img, __le32 adr)
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

		report_direct(img, indirect_block[i]);
	}

	free(indirect_block);

	return 0;
}

int report_indirect_double(int img, __le32 adr)
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

		report_indirect_single(img, double_indirect_block[i]);
	}

	free(double_indirect_block);

	return 0;
}

int dump_dir(int img, int inode_nr)
{
	struct ext2_super_block super;
	error = pread(img, &super, sizeof(super), 1024);
	if ( error < 0 ) return -errno;

	block_size = 1024 << super.s_log_block_size;

	int inode_block_group_needed = (inode_nr - 1) / super.s_inodes_per_group;
	int inode_in_block_needed = (inode_nr - 1) % super.s_inodes_per_group;

	struct ext2_group_desc group;
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
			error = report_direct(img, inode.i_block[i]);
			if ( error < 0 ) return -errno;	
		}
		// else if (i == EXT2_IND_BLOCK) {                         	/* single indirect block */
		// 	error = report_indirect_single(img, inode.i_block[i]);
		// 	if ( error < 0 ) return -errno;	
		// }
		// else if (i == EXT2_DIND_BLOCK) {                            /* double indirect block */
		// 	error = report_indirect_double(img, inode.i_block[i]);
		// 	if ( error < 0 ) return -errno;
		// }

	return 0;
}
