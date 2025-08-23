#ifndef IO_URING_OPS_H
#define IO_URING_OPS_H

#include <liburing.h>
#include "../headers/request.h"

/**
 * Submit an openat request for a directory.
 *
 * @param path          Path to directory
 * @param ring          Pointer to initialized io_uring
 * @param inflight_ops  Counter for in-flight operations
 */
void submit_open_request(const char *path, struct io_uring *ring, int *inflight_ops);

// /**
//  * Low-level helper: prepare a GETDENTS64 SQE.
//  *
//  * @param sqe          Submission Queue Entry
//  * @param fd           Directory file descriptor
//  * @param dirents_buf  Buffer to store directory entries
//  * @param len          Buffer length
//  */
// void _prep_getdents64(struct io_uring_sqe *sqe, int fd, void *dirents_buf, unsigned len);

// /**
//  * Submit a request to read directory entries.
//  *
//  * @param dir_fd        Directory file descriptor
//  * @param dir_path      Path to directory
//  * @param ring          Pointer to initialized io_uring
//  * @param inflight_ops  Counter for in-flight operations
//  */
// void submit_getdents_request(int dir_fd, const char* dir_path, struct io_uring *ring, int *inflight_ops);

/**
 * Handle an io_uring completion event.
 *
 * @param cqe           Completion Queue Entry
 * @param search_term   String to search for in file names
 * @param ring          Pointer to initialized io_uring
 * @param inflight_ops  Counter for in-flight operations
 */
void handle_completion(struct io_uring_cqe *cqe,
                       const char *search_term,
                       struct io_uring *ring,
                       int *inflight_ops);

#endif // IO_URING_OPS_H
