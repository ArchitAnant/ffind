#include <stdio.h>      
#include <stdlib.h>     
#include <liburing.h>   
#include <string.h>     
#include <limits.h>     
#include <fcntl.h>      
#include <unistd.h>     
#include <dirent.h>     
#include <sys/stat.h>   
#include <pthread.h>
#include <errno.h>      

#include "../headers/request.h"
#include "../headers/submissions.h"
#include "../headers/thpool.h"
#include "../headers/expr.h"

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

/*
 * readdir_worker_function
 *
 * This is the worker that runs in the thread pool.  For each directory
 * entry it:
 *   1. Recurses into subdirectories (submit_open_request)
 *   2. Evaluates the expression tree against the entry
 *   3. Prints the path if the expression matches
 *
 * The expression tree evaluation mirrors findutils' per-file
 * apply_predicate() call from ftsfind.c visit().
 */
void readdir_worker_function(void *args) {
    WorkerTaskArgs *task = (WorkerTaskArgs*)args;

    DIR *dir_stream = fdopendir(task->dir_fd);
    if (!dir_stream) {
        close(task->dir_fd);
        free(task);
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir_stream)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        
        char full_path[PATH_MAX]; 
        snprintf(full_path, sizeof(full_path), "%s/%s", task->path, entry->d_name);

        /* 1. Always recurse into directories.
         *    This matches findutils' fts_read() loop which traverses
         *    the directory tree independently of expression evaluation. */
        if (entry->d_type == DT_DIR) {
            pthread_mutex_lock(task->ring_mutex);
            submit_open_request(full_path, task->ring, task->inflight_ops);
            pthread_mutex_unlock(task->ring_mutex);
        } else if (entry->d_type == DT_UNKNOWN) {
            /* Some filesystems don't fill in d_type.  Fall back to lstat
             * to check if this is a directory we need to recurse into.
             * From findutils ftsfind.c consider_visiting() / digest_mode(). */
            struct stat st;
            if (lstat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) {
                pthread_mutex_lock(task->ring_mutex);
                submit_open_request(full_path, task->ring, task->inflight_ops);
                pthread_mutex_unlock(task->ring_mutex);
            }
        }

        /* 2. Evaluate the expression tree against this entry.
         *    This replaces the old strstr() substring matching.
         *
         *    Mirrors findutils' call chain:
         *      visit() → apply_predicate(path, &statbuf, eval_tree)
         *
         *    The stat buffer starts zeroed (st_ino == 0 is our "not yet
         *    stated" sentinel).  evaluate() will call lstat() lazily only
         *    if a predicate actually needs stat info.  Name-only predicates
         *    like -name and -iname never trigger a stat call. */
        struct stat st = {0};
        if (evaluate(task->filter_tree, full_path, entry->d_name, entry->d_type, &st)) {
            printf("%s\n", full_path);
        }
    }
    closedir(dir_stream);
    pthread_mutex_lock(task->task_counter_mutex);
    (*task->active_task)--;
    pthread_mutex_unlock(task->task_counter_mutex);
    free(task);
}

/* Submit an async openat */
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
        fprintf(stderr, "Warning: Could not get SQE, dropping task for %s\n", path);
        free(req);
        return;
    }

    io_uring_prep_openat(sqe, AT_FDCWD, path, O_RDONLY | O_DIRECTORY, 0);
    io_uring_sqe_set_data(sqe, req);

    // Submit immediately so kernel sees it
    int ret = io_uring_submit(ring);
    if (ret < 0) {
        fprintf(stderr, "io_uring_submit failed: %s\n", strerror(-ret));
        free(req);
        return;
    }

    (*inflight_ops)++;
}


void handle_completion(struct io_uring_cqe *cqe, AppContext *ctx) {
    Request *req = (Request *)io_uring_cqe_get_data(cqe);

    if (cqe->res < 0) {
        free(req);
        pthread_mutex_lock(ctx->ring_mutex);
        (*ctx->inflight_ops)--;
        pthread_mutex_unlock(ctx->ring_mutex);
        return;
    }

    WorkerTaskArgs *task_args = malloc(sizeof(WorkerTaskArgs));
    if (!task_args) { 
        perror("malloc WorkerTaskArgs");
        close(cqe->res);
        free(req);
        pthread_mutex_lock(ctx->ring_mutex);
        (*ctx->inflight_ops)--;
        pthread_mutex_unlock(ctx->ring_mutex);
        return;
     }

    task_args->dir_fd = cqe->res;
    strncpy(task_args->path, req->path, PATH_MAX);
    task_args->filter_tree = ctx->filter_tree;
    task_args->ring = ctx->ring; 
    task_args->inflight_ops = ctx->inflight_ops;
    task_args->ring_mutex = ctx->ring_mutex;
    task_args->active_task = ctx->active_task;
    task_args->task_counter_mutex = ctx->task_counter_mutex;

    pthread_mutex_lock(ctx->task_counter_mutex);
    (*ctx->active_task)++;
    pthread_mutex_unlock(ctx->task_counter_mutex);

    thpool_add_work(ctx->pool, readdir_worker_function, task_args);
    free(req);

    pthread_mutex_lock(ctx->ring_mutex);
    (*ctx->inflight_ops)--;
    pthread_mutex_unlock(ctx->ring_mutex);
}
