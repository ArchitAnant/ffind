#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include <limits.h>
#include "include/workqueue.h"

typedef struct WorkerArgs{
    WorkQueue *queue;
    const char *search_term;
}WorkerArgs;


void process_dir(const char* path,const char *search_term,WorkQueue *q){

    DIR *dir = opendir(path);

    if (!dir)
    {
        perror(path);
        return;
    }

    struct dirent *entry;

    while ((entry=readdir(dir))!=NULL)
    {
       if (strcmp(entry->d_name,".")==0||strcmp(entry->d_name,"..")==0)
       {
        continue;
       }

       char full_path[PATH_MAX];
       snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);

       struct stat st;
       if(lstat(full_path, &st) == -1) {
           perror(full_path);
           continue;
       }

       if (S_ISDIR(st.st_mode))
       {
        work_queue_enque(q,full_path);
       }
       else{

        if (strstr(entry->d_name,search_term)!=NULL)
        {
            printf("[FOUND] %s\n", full_path);
        }
        
       }
    }
    closedir(dir);
}



void *worker_function(void *args){
    WorkerArgs *wargs = (WorkerArgs*)args;

    WorkQueue *q = wargs->queue;

    const char *search_term = wargs->search_term;

    while (1)
    {
        char *path = dequeue_work_queue(q);

        if (path==NULL)
        {
            break;
        }

        process_dir(path,search_term,q);

        free(path);
        
    }

    return NULL;
    
}