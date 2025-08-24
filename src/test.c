#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <liburing.h>
#include <linux/dirent.h> // For struct linux_dirent64

#define QUEUE_DEPTH 8
#define BUFFER_SIZE 8192

// Helper to handle fatal errors
void fatal_error(const char *msg) {
    perror(msg);
    exit(1);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <directory_path>\n", argv[0]);
        return 1;
    }
    const char *dir_path = argv[1];

    struct io_uring ring;
    struct io_uring_sqe *sqe;
    struct io_uring_cqe *cqe;

    // 1. Initialize io_uring
    int ret = io_uring_queue_init(QUEUE_DEPTH, &ring, 0);
    if (ret < 0) {
        fatal_error("io_uring_queue_init failed");
    }

    // 2. Prepare and submit the 'openat' request
    sqe = io_uring_get_sqe(&ring);
    if (!sqe) {
        fatal_error("Could not get SQE for openat");
    }
    io_uring_prep_openat(sqe, AT_FDCWD, dir_path, O_RDONLY | O_DIRECTORY, 0);
    io_uring_submit(&ring);

    // Wait for the completion
    ret = io_uring_wait_cqe(&ring, &cqe);
    if (ret < 0) {
        fatal_error("io_uring_wait_cqe for openat failed");
    }

    int dir_fd = cqe->res;
    io_uring_cqe_seen(&ring, cqe); // Mark completion as seen

    if (dir_fd < 0) {
        fprintf(stderr, "Error opening directory %s: %s\n", dir_path, strerror(-dir_fd));
        io_uring_queue_exit(&ring);
        return 1;
    }

    printf("Contents of '%s':\n", dir_path);

    // 3. Loop to read directory entries
    char buffer[BUFFER_SIZE];
    loff_t offset = 0;

    for (;;) {
        sqe = io_uring_get_sqe(&ring);
        if (!sqe) {
            fatal_error("Could not get SQE for read");
        }
        
        // Prepare a read request with an explicit offset
        io_uring_prep_read(sqe, dir_fd, buffer, BUFFER_SIZE, offset);
        io_uring_submit(&ring);
        
        ret = io_uring_wait_cqe(&ring, &cqe);
        if (ret < 0) {
            fatal_error("io_uring_wait_cqe for read failed");
        }
        
        long bytes_read = cqe->res;
        io_uring_cqe_seen(&ring, cqe);

        if (bytes_read < 0) {
            fprintf(stderr, "Error reading directory: %s\n", strerror(-bytes_read));
            break;
        }
        
        // If bytes_read is 0, we've reached the end of the directory
        if (bytes_read == 0) {
            break;
        }

        // 4. Parse the buffer containing directory entries
        long bpos = 0;
        while (bpos < bytes_read) {
            // The buffer contains a stream of linux_dirent64 structures
            struct linux_dirent64 *d = (struct linux_dirent64 *)(buffer + bpos);
            
            // Skip the special '.' and '..' entries
            if (strcmp(d->d_name, ".") != 0 && strcmp(d->d_name, "..") != 0) {
                printf("  - %s\n", d->d_name);
            }
            
            // Move to the next entry using its record length
            bpos += d->d_reclen;
        }
        
        // The offset for the next read is simply after the bytes we just read
        offset += bytes_read;
    }

    // 5. Prepare and submit the 'close' request for the directory fd
    sqe = io_uring_get_sqe(&ring);
    if (!sqe) {
        fatal_error("Could not get SQE for close");
    }
    io_uring_prep_close(sqe, dir_fd);
    io_uring_submit(&ring);

    // Wait for the close to complete
    ret = io_uring_wait_cqe(&ring, &cqe);
    if (ret < 0) {
        // Not a fatal error, but good to know
        fprintf(stderr, "Warning: io_uring_wait_cqe for close failed.\n");
    }
    io_uring_cqe_seen(&ring, cqe);

    // 6. Clean up the io_uring instance
    io_uring_queue_exit(&ring);

    return 0;
}