//
// при решении не списывал, a вдохновлялся официальными общедоступными примерами из https://github.com/axboe/liburing !
//

#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <liburing.h>
#include <solution.h>

#define QD 4
#define BS (256 * 1024)

struct io_data {
    bool read;
    off_t offset;
    size_t length;
    struct iovec iov;
};

int get_file_size(int fd, off_t *size) {
    struct stat st;

    if (fstat(fd, &st) < 0 )
        return -1;
    if(S_ISREG(st.st_mode)) {
        *size = st.st_size;
        return 0;
    } else if (S_ISBLK(st.st_mode)) {
        unsigned long long bytes;

        if (ioctl(fd, BLKGETSIZE64, &bytes) != 0)
            return -1;

        *size = bytes;
        return 0;
    }
    return -1;
}

int copy(int in, int out)
{
	struct io_uring ring;
	off_t file_size, size_to_write, offset = 0;
	unsigned long reads_num = 0, writes_num = 0;

	struct io_uring_sqe *sqe;
	struct io_data *data;
	struct io_uring_cqe *cqe;

	io_uring_queue_init(QD, &ring, 0);

	get_file_size(in, &file_size);
	size_to_write = file_size;

	while ( size_to_write > 0 ) {
		while (file_size > 0) {
            off_t read_size = (file_size > BS) ? BS : file_size;

            if (reads_num + writes_num >= QD) break;

			data = malloc(read_size + sizeof(*data));
			sqe = io_uring_get_sqe(&ring);

			data->read = true;
			data->offset = offset;
			data->iov.iov_base = data + 1;
			data->iov.iov_len = read_size;
			data->length = read_size;

			io_uring_prep_readv(sqe, in, &data->iov, 1, offset);
			io_uring_sqe_set_data(sqe, data);

            offset += read_size;
            reads_num++;
        }

		io_uring_submit(&ring);

		while (size_to_write > 0) {
			io_uring_wait_cqe(&ring, &cqe);

			data = io_uring_cqe_get_data(cqe);

			if (data->read) {
				sqe = io_uring_get_sqe(&ring);

                data->read = false;
				data->iov.iov_base = data + 1;
				data->iov.iov_len = data->length;

				io_uring_prep_writev(sqe, out, &data->iov, 1, data->offset);
				io_uring_sqe_set_data(sqe, data);
				io_uring_submit(&ring);

                size_to_write -= data->length;
                reads_num--;
                writes_num++;
            } else {
                free(data);
                writes_num--;
            }

            io_uring_cqe_seen(&ring, cqe);
		}
	}

	return 0;
}
