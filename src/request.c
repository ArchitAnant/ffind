#include <limits.h>
#include <linux/limits.h>

typedef struct Request{
    char path[PATH_MAX];
}Request;

typedef struct WorkerTaskArgs{
    int dir_fd;
    char path[PATH_MAX];
    const char * search_term;
    struct io_uring *ring;
    int *inflight_ops;
    pthread_mutex_t *ring_mutex; 
}WorkerTaskArgs;

typedef struct AppContext{
    const char *search_term;
    struct io_uring *ring;
    int *inflight_ops;
    pthread_mutex_t *ring_mutex;
    threadpool pool;
}AppContext;

