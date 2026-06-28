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

/* Print a match using write() instead of printf().
 * write() has no internal locking — printf() serializes all
 * threads through a single stdio mutex, which kills throughput
 * when many workers are matching simultaneously. */
static void print_match(const char *path) {
    char buf[PATH_MAX + 1];
    int len = snprintf(buf, sizeof(buf), "%s\n", path);
    if (len > 0)
        (void)write(STDOUT_FILENO, buf, (size_t)len);
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
    int pending_submits = 0;   /* SQEs queued but not yet submitted this pass */

    while ((entry = readdir(dir_stream)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        char full_path[PATH_MAX];
        snprintf(full_path, sizeof(full_path), "%s/%s", task->path, entry->d_name);

        /* 1. Queue openat SQEs for subdirectories.
         *    Crucially we do NOT call io_uring_submit per directory —
         *    we accumulate all SQEs for this readdir pass and submit
         *    in one single syscall at the end.  This is the standard
         *    io_uring batching pattern and cuts submit overhead by N-1
         *    syscalls per directory (N = number of subdirs found). */
        if (entry->d_type == DT_DIR) {
            pthread_mutex_lock(task->ring_mutex);
            int queued = submit_open_request(full_path, task->ring, task->inflight_ops);
            pthread_mutex_unlock(task->ring_mutex);
            if (queued) pending_submits++;
        } else if (entry->d_type == DT_UNKNOWN) {
            struct stat st;
            if (lstat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) {
                pthread_mutex_lock(task->ring_mutex);
                int queued = submit_open_request(full_path, task->ring, task->inflight_ops);
                pthread_mutex_unlock(task->ring_mutex);
                if (queued) pending_submits++;
            }
        }

        /* 2. Evaluate expression tree — lazy stat, no-op for name-only predicates. */
        struct stat st = {0};
        if (evaluate(task->filter_tree, full_path, entry->d_name, entry->d_type, &st)) {
            print_match(full_path);
        }
    }

    /* Flush all queued SQEs in one submit call instead of one per directory. */
    if (pending_submits > 0) {
        pthread_mutex_lock(task->ring_mutex);
        io_uring_submit(task->ring);
        pthread_mutex_unlock(task->ring_mutex);
    }

    closedir(dir_stream);
    pthread_mutex_lock(task->task_counter_mutex);
    (*task->active_task)--;
    pthread_cond_signal(task->task_done_cond);
    pthread_mutex_unlock(task->task_counter_mutex);
    free(task);
}

/* Queue an async openat SQE without submitting.
 * Returns 1 if the SQE was queued, 0 on failure.
 * The caller is responsible for calling io_uring_submit() at the
 * right time (after the full readdir pass) to batch all opens. */
int submit_open_request(const char *path, struct io_uring *ring, int *inflight_ops) {
    Request *req = malloc(sizeof(Request));
    if (!req) {
        perror("malloc request");
        return 0;
    }
    strncpy(req->path, path, sizeof(req->path) - 1);
    req->path[sizeof(req->path) - 1] = '\0';

    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    if (!sqe) {
        /* Ring is full — submit what we have and retry once. */
        io_uring_submit(ring);
        sqe = io_uring_get_sqe(ring);
        if (!sqe) {
            fprintf(stderr, "Warning: SQ ring full, dropping %s\n", path);
            free(req);
            return 0;
        }
    }

    io_uring_prep_openat(sqe, AT_FDCWD, path, O_RDONLY | O_DIRECTORY, 0);
    io_uring_sqe_set_data(sqe, req);
    (*inflight_ops)++;
    return 1;  /* queued; caller must io_uring_submit() */
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
    task_args->filter_tree      = ctx->filter_tree;
    task_args->ring             = ctx->ring;
    task_args->inflight_ops     = ctx->inflight_ops;
    task_args->ring_mutex       = ctx->ring_mutex;
    task_args->active_task      = ctx->active_task;
    task_args->task_counter_mutex = ctx->task_counter_mutex;
    task_args->task_done_cond   = ctx->task_done_cond;

    pthread_mutex_lock(ctx->task_counter_mutex);
    (*ctx->active_task)++;
    pthread_mutex_unlock(ctx->task_counter_mutex);

    thpool_add_work(ctx->pool, readdir_worker_function, task_args);
    free(req);

    pthread_mutex_lock(ctx->ring_mutex);
    (*ctx->inflight_ops)--;
    pthread_mutex_unlock(ctx->ring_mutex);
}
