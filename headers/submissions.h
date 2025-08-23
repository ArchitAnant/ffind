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
