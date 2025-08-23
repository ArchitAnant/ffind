#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <liburing.h>
#include <dirent.h>   // for linux_dirent64

#define BUF_SIZE 4096

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <dirpath>\n", argv[0]);
        return 1;
    }

    const char *dirpath = argv[1];
    int dfd = open(dirpath, O_RDONLY | O_DIRECTORY);
    if (dfd < 0) {
        perror("open");
        return 1;
    }

    struct io_uring ring;
    if (io_uring_queue_init(8, &ring, 0) < 0) {
        perror("io_uring_queue_init");
        close(dfd);
        return 1;
    }

    char buf[BUF_SIZE];
    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    if (!sqe) {
        fprintf(stderr, "get_sqe failed\n");
        io_uring_queue_exit(&ring);
        close(dfd);
        return 1;
    }

    // Manually prep GETDENTS64
    sqe->opcode = IORING_OP_GETDENTS64;
    sqe->fd     = dfd;
    sqe->addr   = (unsigned long) buf;
    sqe->len    = sizeof(buf);

    io_uring_submit(&ring);

    struct io_uring_cqe *cqe;
    if (io_uring_wait_cqe(&ring, &cqe) < 0) {
        perror("io_uring_wait_cqe");
        io_uring_queue_exit(&ring);
        close(dfd);
        return 1;
    }

    if (cqe->res < 0) {
        fprintf(stderr, "getdents64 failed: %s\n", strerror(-cqe->res));
    } else {
        int nread = cqe->res;
        int bpos = 0;
        while (bpos < nread) {
            struct linux_dirent64 *d = (struct linux_dirent64 *)(buf + bpos);
            printf("%s\n", d->d_name);
            bpos += d->d_reclen;
        }
    }

    io_uring_cqe_seen(&ring, cqe);
    io_uring_queue_exit(&ring);
    close(dfd);
    return 0;
}
