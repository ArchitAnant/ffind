#include <stdio.h>
#include <stdlib.h>
#include <liburing.h>
#include <string.h>
#include <limits.h>   // PATH_MAX
#include <fcntl.h>    // O_RDONLY, O_DIRECTORY, AT_FDCWD
#include <unistd.h>   // close()
#include "../headers/request.h"




void submit_open_request(const char *path, struct io_uring *ring, int *inflight_ops) {
    Request *req = malloc(sizeof(Request));
    if (!req) {
        perror("malloc request");
        return;
    }
    strncpy(req->path, path, sizeof(req->path) - 1);
    req->path[sizeof(req->path) - 1] = '\0';

    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    if (!sqe) {
        // This is a rare condition, means the submission queue is full.
        // A more robust app might handle this by waiting. We'll just drop the task.
        fprintf(stderr, "Warning: Could not get SQE, dropping task for %s\n", path);
        free(req);
        return;
    }
    
    io_uring_prep_openat(sqe, AT_FDCWD, path, O_RDONLY | O_DIRECTORY, 0);
    io_uring_sqe_set_data(sqe, req);

    (*inflight_ops)++;
}


void handle_completion(struct io_uring_cqe *cqe, const char *search_term, struct io_uring *ring, int *inflight_ops) {
    Request *req = (Request *)io_uring_cqe_get_data(cqe);

    // Check if the openat operation failed (e.g., permission denied).
    if (cqe->res < 0) {
        // fprintf(stderr, "Warning: Failed to open directory '%s': %s\n", req->path, strerror(-cqe->res));
        free(req);
        return;
    }

    int dir_fd = cqe->res;

    // Convert the file descriptor to a DIR stream for use with readdir.
    DIR *dir_stream = fdopendir(dir_fd);
    if (!dir_stream) {
        perror("fdopendir");
        close(dir_fd);
        free(req);
        return;
    }

    struct dirent *entry;
    // Loop through directory entries synchronously. This is CPU-bound work.
    while ((entry = readdir(dir_stream)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char full_path[PATH_MAX];
        snprintf(full_path, sizeof(full_path), "%s/%s", req->path, entry->d_name);

        // Check the type of the directory entry.
        // Using d_type is much faster than calling stat() for every entry.
        if (entry->d_type == DT_DIR) {
            // RECURSIVE STEP: If it's a directory, submit a new async 'openat' request.
            submit_open_request(full_path, ring, inflight_ops);
        } else if (entry->d_type == DT_REG) {
            // It's a regular file. Check if its name matches the search term.
            if (strstr(entry->d_name, search_term) != NULL) {
                printf("[FOUND] %s\n", full_path);
            }
        }
        // Note: We are ignoring DT_UNKNOWN, symlinks, etc. for simplicity.
    }

    // Clean up resources for the directory we just finished scanning.
    closedir(dir_stream); // This also closes the underlying dir_fd.
    free(req);
}

// void _prep_getdents64(struct io_uring_sqe *sqe,int fd, void *dirents_buf, unsigned len){
//     memset(sqe,0,sizeof(*sqe));
//     sqe->opcode = IORING_OP_GETDENTS;
//     sqe->fd = fd;
//     sqe->addr = (uint64_t)dirents_buf;
//     sqe->len = len;
// }

// void submit_getdents_request(int dir_fd, const char* dir_path, struct io_uring *ring, int *inflight_ops){
//     printf("[SUBMIT] %s\n",dir_path);
//     Request *req = calloc(1, sizeof(Request));

//     req->type = OP_READ_DIRENTS;
//     req->dir_fd = dir_fd;
//     strncpy(req->path, dir_path, sizeof(req->path) - 1);

//     req->path[sizeof(req->path)-1]='\0';

//     req->dirents = malloc(DIRENT_BUF_SIZE);
//     if (!req->dirents) { perror("malloc dirents"); free(req); return; }

//     struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
//     if (!sqe) {
//         perror("io_uring_get_sqe");
//         if (req->type == OP_READ_DIRENTS && req->dirents) free(req->dirents);
//         free(req);
//         return;
//     }
    
//     _prep_getdents64(sqe,dir_fd,req->dirents,DIRENT_BUF_SIZE);
//     io_uring_sqe_set_data(sqe,req);

//     (*inflight_ops)++;
// }