#include <solution.h>

#include <fuse.h>
#include <errno.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ext2fs/ext2fs.h>
#include <sys/sendfile.h>

int Img;

// ============================================

int block_size, data_size_left, data_size_left_dir, error;
char wanted_type, *wanted_name;
int wanted_inode_nr;

void find_file(int inode_nr, char type, const char *name)
{
	if( wanted_type == type && (strcmp(name, wanted_name) == 0) ) {
		wanted_inode_nr = inode_nr;
	} else if ( wanted_type != type && (strcmp(name, wanted_name) == 0) ) {
		wanted_inode_nr = -2;
	}
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

	if( wanted_inode_nr == -1 ) error = ENOENT;
	if( wanted_inode_nr == -2 ) error = ENOTDIR;
	free(name);
	return wanted_inode_nr;
}

int get_inode_file(int img, int inode_nr, char *name)
{
	wanted_inode_nr = -1;
	wanted_type = 'f';
	wanted_name = name;

	find_dir(img, inode_nr);

	if( wanted_inode_nr == -1 ) error = ENOENT;
	free(name);
	return wanted_inode_nr;
}

// ============================================
int buffer_offset = 0;
char *file_contents;

int copy_direct(int img, __le32 adr)
{
	if ( data_size_left == 0 ) return 0;

	int length = data_size_left > block_size ? block_size : data_size_left;
	data_size_left -= length;

	if ( adr != 0 ) {
		off_t offset = block_size * adr;
		//error = sendfile(out, img, &offset, length);
		error = pread(img, file_contents + buffer_offset, length, offset);
		if ( error < length ) return -errno;
	} else if ( length > 0 ) {
		//char* Zeros = (char*)malloc(length);
		memset(file_contents + buffer_offset, 0, length);
		//error = write(out, Zeros, length);
		//free(Zeros);

		if ( error < length ) return -errno;
	}
	buffer_offset += length;
	return 0;
}

int copy_indirect_single(int img, __le32 adr)
{
	// there are indirect blocks, with zeros or nothing inside
	if ( adr == 0 ) {
		for (int i = 0; i < (block_size / 4); i++) {
			if ( copy_direct(img, 0) != 0 ) return -errno;
		}
		return 0;
	}
	// int as stored 4-byte block numbers
	int* indirect_block = (int*)malloc(block_size);
	error = pread(img, indirect_block, block_size, block_size * adr);
	if ( error < 0 ) {
		free(indirect_block);
		return -errno;
	}

	// block_size / 4 - number of stored block numbers
	for (int i = 0; i < (block_size / 4); i++) {
		if ( copy_direct(img, indirect_block[i]) != 0 ) return -errno;
	}

	free(indirect_block);

	return 0;
}

int copy_indirect_double(int img, __le32 adr)
{
	// there are indirect blocks, with zeros or nothing inside
	if ( adr == 0 ) {
		for (int i = 0; i < (block_size / 4); i++) {
			if ( copy_indirect_single(img, 0) != 0 ) return -errno;
		}
		return 0;
	}
	int* double_indirect_block = (int*)malloc(block_size);
	error = pread(img, double_indirect_block, block_size, block_size * adr);
	if ( error < 0 ) {
		free(double_indirect_block);
		return -errno;
	}

	// block_size / 4 - number of stored block numbers
	for (int i = 0; i < (block_size / 4); i++) {

		if ( copy_indirect_single(img, double_indirect_block[i]) != 0 ) return -errno;
	}

	free(double_indirect_block);

	return 0;
}

int dump_file_inode(int img, int inode_nr, size_t *file_size)
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
	*file_size = data_size_left;
	file_contents = (char*)malloc(data_size_left);

	for(int i = 0; i < EXT2_N_BLOCKS; i++)
		if (i < EXT2_NDIR_BLOCKS) {                               	/* direct blocks */
			error = copy_direct(img, inode.i_block[i]);
			if ( error < 0 ) return -errno;	
		}
		else if (i == EXT2_IND_BLOCK) {                         	/* single indirect block */
			error = copy_indirect_single(img, inode.i_block[i]);
			if ( error < 0 ) return -errno;	
		}
		else if (i == EXT2_DIND_BLOCK) {                            /* double indirect block */
			error = copy_indirect_double(img, inode.i_block[i]);
			if ( error < 0 ) return -errno;
		}

	return 0;
}

int dump_file(int img, const char *path, size_t *file_size)
{
	if( *path != '/' ) return -ENOTDIR;

	const char *slash = &path[0], *next;
    int inode_num = EXT2_ROOT_INO; // root inode

	while((next = strpbrk(slash + 1, "\\/"))) {
		inode_num = get_inode_dir(img, inode_num, strndup(slash + 1, next - slash - 1));
		if( error != 0 ) return -error;
        slash = next;
    }

	inode_num = get_inode_file(img, inode_num, strdup(slash + 1));
	if( error != 0 ) return -error;

	return dump_file_inode(img, inode_num, file_size);
}

static int
ext2fs_read(const char *path, char *buf, size_t size, off_t off,
        struct fuse_file_info *ffi)
{
	(void)ffi;
	if (strcmp(path, "/hello") != 0) {
		return -ENOENT;
	}
	size_t len = 0;
	dump_file(Img, path, &len);

	if ((size_t)off < len) {
		if (off + size > len)
			size = len - off;
		memcpy(buf, file_contents + off, size);
	} else
		size = 0;
	free(file_contents);
	return size;
}

// ============================================

void *Data;
fuse_fill_dir_t Filler;

void report_file(const char *name)
{
	Filler(Data, name, NULL, 0, 0);
}

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
		//char type = entry->file_type == 2 ? 'd' : 'f';
		report_file(file_name);
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

int dump_dir_inode(int img, int inode_nr)
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
		else if (i == EXT2_IND_BLOCK) {                         	/* single indirect block */
			error = report_indirect_single(img, inode.i_block[i]);
			if ( error < 0 ) return -errno;	
		}
		else if (i == EXT2_DIND_BLOCK) {                            /* double indirect block */
			error = report_indirect_double(img, inode.i_block[i]);
			if ( error < 0 ) return -errno;
		}

	return 0;
}

int dump_dir(int img, const char *path)
{
	if( *path != '/' ) return -ENOTDIR;

	const char *slash = &path[0], *next;
    int inode_num = EXT2_ROOT_INO; // root inode

	while((next = strpbrk(slash + 1, "\\/"))) {
		inode_num = get_inode_dir(img, inode_num, strndup(slash + 1, next - slash - 1));
		if( error != 0 ) return -error;
        slash = next;
    }

	inode_num = get_inode_dir(img, inode_num, strdup(slash + 1));
	if( error != 0 ) return -error;

	return dump_dir_inode(img, inode_num);
}

static int
ext2fs_readdir(const char *path, void *data, fuse_fill_dir_t filler,
           off_t off, struct fuse_file_info *ffi, enum fuse_readdir_flags frf)
{
	(void)off; (void)ffi; (void)frf;
	if (strcmp(path, "/") != 0)
		return -ENOENT;
	Data = data;
	Filler = filler;
	dump_dir(Img, path);
	return 0;
}

// ============================================

static int
ext2fs_open_(const char *path, struct fuse_file_info *ffi)
{
	(void)path; (void)ffi;
	// if ((ffi->flags & 3) != O_RDONLY)
	// 	return -EROFS;

	return 0;
}

static int
ext2fs_getattr(const char *path, struct stat *st, struct fuse_file_info *ffi)
{
	(void)ffi; (void)st; (void)path;
	// if (strcmp(path, "/") == 0) {
	// 	st->st_mode = S_IFDIR | 0775;
	// 	st->st_nlink = 2;
	// } else if (strcmp(path, "/hello") == 0) {
	// 	st->st_mode = S_IFREG | 0400;
	// 	st->st_nlink = 1;
	// 	st->st_size = 16;
	// } else {
	// 	return -ENOENT;
	// }

	return 0;
}

static int
ext2fs_write(const char *path, const char *buf, size_t size, off_t off,
        struct fuse_file_info *ffi)
{
	(void)path; (void)buf; (void)size; (void)off; (void)ffi;
	return -EROFS;
}

static int
ext2fs_create(const char *path, mode_t mode, struct fuse_file_info *ffi)
{
	(void)path; (void)mode; (void)ffi;
	return -EROFS;
}

static const struct fuse_operations ext2_ops = {
	.readdir = ext2fs_readdir,
	.read = ext2fs_read,
	.open = ext2fs_open_,
	.getattr = ext2fs_getattr,
	.create = ext2fs_create,
	.write = ext2fs_write,
};


int ext2fuse(int img, const char *mntp)
{
	Img = img;

	char *argv[] = {"exercise", "-f", (char *)mntp, NULL};
	return fuse_main(3, argv, &ext2_ops, NULL);
}