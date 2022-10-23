#include <solution.h>

#include <errno.h>
#include <fuse.h>
#include <string.h>
#include <stdio.h>

static int
hellofs_readdir(const char *path, void *data, fuse_fill_dir_t filler,
           off_t off, struct fuse_file_info *ffi, enum fuse_readdir_flags frf)
{
	(void)off; (void)ffi; (void)frf;
	if (strcmp(path, "/") != 0)
		return -ENOENT;

	filler(data, ".", NULL, 0, 0);
	filler(data, "..", NULL, 0, 0);
	filler(data, "hello", NULL, 0, 0);
	return 0;

	return -ENOENT;
}

static int
hellofs_read(const char *path, char *buf, size_t size, off_t off,
        struct fuse_file_info *ffi)
{
	(void)ffi;
	if (strcmp(path, "/hello") != 0) {
		return -ENOENT;
	}
	size_t len;
	char file_contents[32];
	sprintf(file_contents, "hello, %d\n", fuse_get_context()->pid);

	len = strlen(file_contents) + 1;

	if ((size_t)off < len) {
		if (off + size > len)
			size = len - off;
		memcpy(buf, file_contents + off, size);
	} else
		size = 0;

	return size;
}

static int
hellofs_open(const char *path, struct fuse_file_info *ffi)
{
	if (strcmp(path, "/hello") != 0)
		return -ENOENT;

	if ((ffi->flags & 3) != O_RDONLY)
		return -EACCES;

	return 0;
}

static int
hellofs_getattr(const char *path, struct stat *st, struct fuse_file_info *ffi)
{
	(void)ffi;
	if (strcmp(path, "/") == 0) {
		st->st_mode = 0x4000;
		st->st_nlink = 2;
	} else if (strcmp(path, "/hello") == 0) {
		st->st_mode = 0x4000;
		st->st_nlink = 1;
		st->st_size = 16;
	} else {
		return -ENOENT;
	}

	return 0;
}

static int
hellofs_write(const char *path, const char *buf, size_t size, off_t off,
        struct fuse_file_info *ffi)
{
	(void)path; (void)buf; (void)size; (void)off; (void)ffi;
	return -EROFS;
}

static int
hellofs_create(const char *path, mode_t mode, struct fuse_file_info *ffi) {
	(void)path; (void)mode; (void)ffi;
	return -EROFS;
}

// static void*
// hellofs_init(struct fuse_conn_info *conn)
// {
// 	(void) conn;
// 	return NULL;
// }

static const struct fuse_operations hellofs_ops = {
	.readdir = hellofs_readdir,
	.read = hellofs_read,
	.open = hellofs_open,
	.getattr = hellofs_getattr,
	.create = hellofs_create,
	.write = hellofs_write,
	//.init = hellofs_init,
};

int helloworld(const char *mntp)
{
	char *argv[] = {"exercise", "-f", (char *)mntp, NULL};
	return fuse_main(3, argv, &hellofs_ops, NULL);
}