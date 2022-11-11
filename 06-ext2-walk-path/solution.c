#include <solution.h>
#include <unistd.h>
#include <ext2fs/ext2fs.h>
#include <sys/sendfile.h>
#include <errno.h>

int block_size, data_size_left, data_size_left_dir, error;
char wanted_type;
char *wanted_name;
int wanted_inode_nr;

void find_file(int inode_nr, char type, const char *name)
{
	if( wanted_type == type && (strcmp(name, wanted_name) == 0) ) wanted_inode_nr = inode_nr;
}

int copy_direct(int img, int out, __le32 adr)
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
	int* indirect_block = (int*)malloc(block_size);
	error = pread(img, indirect_block, block_size, block_size * adr);
	if ( error < 0 ) {
		free(indirect_block);
		return -errno;
	}

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

	for (int i = 0; i < (block_size / 4); i++) {
		if (double_indirect_block[i] == 0) break;

		copy_indirect_single(img, out, double_indirect_block[i]);
	}

	free(double_indirect_block);

	return 0;
}

int dump_inode(int img, int inode_nr, int out)
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

int find_direct(int img, __le32 adr)
{
	int length = data_size_left_dir > block_size ? block_size : data_size_left_dir;
	data_size_left_dir -= length;

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
		find_file(entry->inode , type, file_name);
	}

	free(block);

	return 0;
}

int find_indirect_single(int img, __le32 adr)
{
	int* indirect_block = (int*)malloc(block_size);
	error = pread(img, indirect_block, block_size, block_size * adr);
	if ( error < 0 ) {
		free(indirect_block);
		return -errno;
	}

	for (int i = 0; i < (block_size / 4); i++) {
		if (indirect_block[i] == 0) break;

		find_direct(img, indirect_block[i]);
	}

	free(indirect_block);

	return 0;
}

int find_indirect_double(int img, __le32 adr)
{
	int* double_indirect_block = (int*)malloc(block_size);
	error = pread(img, double_indirect_block, block_size, block_size * adr);
	if ( error < 0 ) {
		free(double_indirect_block);
		return -errno;
	}

	for (int i = 0; i < (block_size / 4); i++) {
		if (double_indirect_block[i] == 0) break;

		find_indirect_single(img, double_indirect_block[i]);
	}

	free(double_indirect_block);

	return 0;
}

int find_dir(int img, int inode_nr)
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

	data_size_left_dir = inode.i_size;

	for(int i = 0; i < EXT2_N_BLOCKS; i++)
		if (i < EXT2_NDIR_BLOCKS) {                               	/* direct blocks */
			error = find_direct(img, inode.i_block[i]);
			if ( error < 0 ) return -errno;	
		}
		else if (i == EXT2_IND_BLOCK) {                         	/* single indirect block */
			error = find_indirect_single(img, inode.i_block[i]);
			if ( error < 0 ) return -errno;	
		}
		else if (i == EXT2_DIND_BLOCK) {                            /* double indirect block */
			error = find_indirect_double(img, inode.i_block[i]);
			if ( error < 0 ) return -errno;
		}

	return 0;
}

int get_inode_dir(int img, int inode_nr, char *name)
{
	wanted_inode_nr = -1;
	wanted_type = 'd';
	wanted_name = name;

	find_dir(img, inode_nr);

	if( wanted_inode_nr == -1 ) {
		free(name);
		return -1;
	}
	free(name);
	return wanted_inode_nr;
}

int get_inode_file(int img, int inode_nr, char *name)
{
	wanted_inode_nr = -1;
	wanted_type = 'f';
	wanted_name = name;

	find_dir(img, inode_nr);

	if( wanted_inode_nr == -1 ) {
		free(name);
		return -1;
	}
	free(name);
	return wanted_inode_nr;
}

int dump_file(int img, const char *path, int out)
{
	if( *path != '/' ) return -ENOTDIR;

	const char *slash = &path[0], *next;
    int inode_num = EXT2_ROOT_INO; // root inode

	while((next = strpbrk(slash + 1, "\\/"))) {
		inode_num = get_inode_dir(img, inode_num, strndup(slash + 1, next - slash - 1));
		if( inode_num < 0 ) return -ENOENT;
        slash = next;
    }

	inode_num = get_inode_file(img, inode_num, strdup(slash + 1));

	if( inode_num < 0 ) return -ENOENT;

	return dump_inode(img, inode_num, out);
}
