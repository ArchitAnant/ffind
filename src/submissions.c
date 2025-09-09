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

void handle_completion(struct io_uring_cqe *cqe, const char *search_term, struct io_uring *ring, int *inflight_ops) {
    Request *req = (Request *)io_uring_cqe_get_data(cqe);

    if (cqe->res < 0) {
        fprintf(stderr, "[OPEN FAILED] %s : %s\n", req->path, strerror(-cqe->res));

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

        // We need the full path for printing and recursion, so build it once.
        char full_path[PATH_MAX];
        snprintf(full_path, sizeof(full_path), "%s/%s", req->path, entry->d_name);

        // Use d_type for a huge performance gain, falling back to lstat.
        if (entry->d_type == DT_UNKNOWN) {
            struct stat st;
            if (lstat(full_path, &st) == -1) {
                perror(full_path);
                continue;
            }
            if (S_ISDIR(st.st_mode)) {
                submit_open_request(full_path, ring, inflight_ops, 0);
            } else if (S_ISREG(st.st_mode)) {
                if (strstr(entry->d_name, search_term)) {
                    printf("[FOUND] %s\n", full_path);
                }
            }
        } else if (entry->d_type == DT_DIR) {
            submit_open_request(full_path, ring, inflight_ops, 0);
        } else if (entry->d_type == DT_REG) {
            if (strstr(entry->d_name, search_term)) {
                printf("[FOUND] %s\n", full_path);
            }
        }

    }

    // Flush any remaining batched submissions for this directory.
    if (pending_in_batch > 0) {
        flush_batch(ring);
    }

    closedir(dir_stream); // This also closes dir_fd
    free(req);
    (*inflight_ops)--;
}
