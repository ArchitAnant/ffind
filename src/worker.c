#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include <limits.h>
#include <liburing.h>
#include "../include/workqueue.h"

typedef struct WorkerArgs {
    WorkQueue *queue;
    const char *search_term;
    struct io_uring *ring;
    //int *inflight_ops;
    pthread_mutex_t *ring_mutex;
} WorkerArgs;

void *worker_function(void *args){
    WorkerArgs *wargs = (WorkerArgs*)args;

    WorkQueue *q = wargs->queue;

    const char *search_term = wargs->search_term;

    while (1)
    {
        DirTask task;
        if (dequeue_work_queue(q,&task)!=0)
        {
            break;
        }

        DIR *dir = fdopendir(task.dir_fd);
        if (!dir)
        {
            perror("fdopendir in worker");
            close(task.dir_fd);
            continue;
        }

        struct dirent *entry;
        while ((entry = readdir(dir))!=NULL)
        {
            if (strcmp(entry->d_name,".")==0 || strcmp(entry->d_name,"..")==0) continue;

            if (entry->d_type == DT_DIR)
            {
                char full_path[PATH_MAX];
                snprintf(full_path, sizeof(full_path), "%s/%s", task.path, entry->d_name);
                printf("[APPENDING] %s\n",full_path);
                pthread_mutex_lock(wargs->ring_mutex);
                printf("submitting new openpath : %s\n",full_path);
                submit_open_request(full_path,wargs->ring);
                pthread_mutex_unlock(wargs->ring_mutex);
            }
            else if (entry->d_type==DT_REG)
            {
               if (strstr(entry->d_name,wargs->search_term)!=NULL)
               {
                    char full_path[PATH_MAX];
                    snprintf(full_path, sizeof(full_path), "%s/%s", task.path, entry->d_name);
                    printf("[FOUND] %s\n",full_path);

               }
               
            }
            /*
            TODO: DT_UNKOWN with lstat
            */
            
        }
        closedir(dir);
    }

    return NULL;
    
}