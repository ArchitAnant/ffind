#include <stdio.h>
#include <fcntl.h>
#include <liburing.h>

#ifndef IORING_OP_GETDENTS64
#define IORING_OP_GETDENTS64 34  // 34 is the value used in Linux 6.x kernels
#endif


struct linux_dirent64 {
    unsigned long   d_ino;
    signed long     d_off;
    unsigned short  d_reclen;
    unsigned char   d_type;
    char            d_name[];
};


int main() {
    struct io_uring ring;
    struct io_uring_sqe *sqe;
    struct io_uring_cqe *cqe;
    char buf[1024];
    int dfd = open("/tmp", O_RDONLY | O_DIRECTORY);
    if (dfd < 0) { perror("open"); return 1; }

    if (io_uring_queue_init(2, &ring, 0) < 0) { perror("init"); return 1; }

    sqe = io_uring_get_sqe(&ring);
    sqe->opcode = IORING_OP_GETDENTS64;
    sqe->fd = dfd;
    sqe->addr = (unsigned long) buf;
    sqe->len = sizeof(buf);
    sqe->off = 0;

    io_uring_submit(&ring);
    io_uring_wait_cqe(&ring, &cqe);

    if (cqe->res < 0) {
        printf("GETDENTS64 failed: %d\n", cqe->res);
    } else {
        printf("GETDENTS64 works, read %d bytes\n", cqe->res);
    }

    io_uring_cqe_seen(&ring, cqe);
    io_uring_queue_exit(&ring);
    close(dfd);
    return 0;
}
