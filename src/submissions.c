#include <stdio.h>      
#include <stdlib.h>     
#include <liburing.h>   
#include <string.h>     
#include <limits.h>     
#include <fcntl.h>      
#include <unistd.h>     
#include <dirent.h>     
#include <sys/stat.h>   
#include <errno.h>      
#include "../headers/request.h"

#define BATCH_SIZE 64

static int pending_in_batch = 0;

/* Flush batched SQEs */
void flush_batch(struct io_uring *ring) {
    if (pending_in_batch > 0) {
        int ret = io_uring_submit(ring);
        if (ret < 0) {
            fprintf(stderr, "Error in io_uring_submit: %s\n", strerror(-ret));
        }
        pending_in_batch = 0;
    }
}

/* Submit an async openat */
void submit_open_request(const char *path, struct io_uring *ring, int *inflight_ops, int force_flush) {
    Request *req = malloc(sizeof(Request));
    if (!req) {
        perror("malloc request");
        return;
    }
    strncpy(req->path, path, sizeof(req->path) - 1);
    req->path[sizeof(req->path) - 1] = '\0';

    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    if (!sqe) {
        fprintf(stderr, "Warning: Could not get SQE, dropping task for %s\n", path);
        free(req);
        return;
    }
    
    io_uring_prep_openat(sqe, AT_FDCWD, path, O_RDONLY | O_DIRECTORY, 0);
    io_uring_sqe_set_data(sqe, req);

    (*inflight_ops)++;
    pending_in_batch++;

    if (pending_in_batch >= BATCH_SIZE || force_flush) {
        flush_batch(ring);
    }
}

/* Handle one completed openat */
void handle_completion(struct io_uring_cqe *cqe, const char *search_term, struct io_uring *ring, int *inflight_ops) {
    Request *req = (Request *)io_uring_cqe_get_data(cqe);

    if (cqe->res < 0) {
        // Open failed (e.g. permission denied)
        free(req);
        (*inflight_ops)--;
        return;
    }

    int dir_fd = cqe->res;

    DIR *dir_stream = fdopendir(dir_fd);
    if (!dir_stream) {
        perror("fdopendir");
        close(dir_fd);
        free(req);
        (*inflight_ops)--;
        return;
    }

        struct dirent *entry;
    while ((entry = readdir(dir_stream)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char full_path[PATH_MAX];
        snprintf(full_path, sizeof(full_path), "%s/%s", req->path, entry->d_name);

        struct stat st;

        switch (entry->d_type) {
            case DT_DIR:
                st.st_mode = S_IFDIR;
                break;
            case DT_REG:
                st.st_mode = S_IFREG;
                break;
            case DT_LNK:
                st.st_mode = S_IFLNK;
                break;
            default:
                // Fallback if d_type is unknown or unusual (FIFO, socket, etc.)
                if (fstatat(dir_fd, entry->d_name, &st, AT_SYMLINK_NOFOLLOW) == -1) {
                    continue;
                }
                break;
        }

        if (S_ISDIR(st.st_mode)) {
            submit_open_request(full_path, ring, inflight_ops, 0);
        } else if (S_ISREG(st.st_mode)) {
            if (strstr(full_path, search_term)) {
                printf("[FOUND] %s\n", full_path);
            }
        }
        // You could also decide what to do with symlinks here if needed.
    }

    if (pending_in_batch > 0) {
        flush_batch(ring);
    }

    closedir(dir_stream);
    free(req);
    (*inflight_ops)--;
}
