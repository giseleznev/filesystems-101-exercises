#include <solution.h>
#include <unistd.h>
#include <ext2fs/ext2fs.h>
#include <errno.h>

int block_size, data_size_left, error;

int report(int img, __le32 adr)
{
	void* block = malloc(block_size);
	error = pread(img, block, block_size, block_size * adr);
	if ( error < 0 ) {
		free(block);
		return -errno;
	}

	int size = 0;
	struct ext2_dir_entry_2 *entry;

	while(size < block_size && size < data_size_left) {
		entry = (void*) block + size;
		size += entry->rec_len;
		char file_name[EXT2_NAME_LEN + 1];
		memcpy(file_name, entry->name, entry->name_len);
		file_name[entry->name_len] = '\0';
		char type = entry->file_type == 2 ? 'd' : 'f';
		report_file(entry->inode , type, file_name);
	}

	data_size_left -= size;
	free(block);

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
		if(inode.i_block[i] != 0) {
			error = report(img, inode.i_block[i]);
			if ( error < 0 ) return -errno;
		}
	return 0;
}
