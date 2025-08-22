#include <stdio.h>
#include <stdlib.h>
#include <liburing.h>
#include <string.h>
#include <linux/types.h>
#include <dirent.h>
#include <sys/types.h>
//#include <linux/dirent.h>
#include <limits.h>   // PATH_MAX
#include <fcntl.h>
#include <linux/fs.h>
#include "../headers/request.h"
#include <unistd.h>

#define DIRENT_BUF_SIZE  32768



void submit_open_request(const char *path, struct io_uring *ring, int *inflight_ops){
    Request *req = calloc(1, sizeof(Request));
    
    req->type = OP_OPEN;
    strncpy(req->path, path, sizeof(req->path)-1);
    req->path[sizeof(req->path)-1] = '\0';
    
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    if (!sqe) {
        perror("io_uring_get_sqe");
        if (req->type == OP_READ_DIRENTS && req->dirents) free(req->dirents);
        free(req);
        return;
    }
    io_uring_prep_openat(sqe, AT_FDCWD, path,O_RDONLY | O_DIRECTORY , 0);
    io_uring_sqe_set_data(sqe,req);

    (*inflight_ops)++;
}

void submit_getdents_request(int dir_fd, const char* dir_path, struct io_uring *ring, int *inflight_ops){
    printf("[SUBMIT] %s\n",dir_path);
    Request *req = calloc(1, sizeof(Request));

    req->type = OP_READ_DIRENTS;
    req->dir_fd = dir_fd;
    strncpy(req->path, dir_path, sizeof(req->path) - 1);

    req->path[sizeof(req->path)-1]='\0';

    req->dirents = malloc(DIRENT_BUF_SIZE);
    if (!req->dirents) { perror("malloc dirents"); free(req); return; }

    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    if (!sqe) {
        perror("io_uring_get_sqe");
        if (req->type == OP_READ_DIRENTS && req->dirents) free(req->dirents);
        free(req);
        return;
    }
    
    io_uring_prep_read(sqe,dir_fd,req->dirents,DIRENT_BUF_SIZE,-1);
    io_uring_sqe_set_data(sqe,req);

    (*inflight_ops)++;
}


void handle_completion(struct io_uring_cqe *cqe,
    const char *search_term, struct io_uring *ring, int *inflight_ops
){
    Request *req = (Request *)io_uring_cqe_get_data(cqe);

    if (cqe->res <0)
    {
        if (req->type == OP_READ_DIRENTS && req->dirents)
            free(req->dirents);
        free(req);
        return;
    }

    switch (req->type)
    {
    case OP_OPEN:{
        int dir_fd = cqe->res;
        
        submit_getdents_request(dir_fd,req->path,ring,inflight_ops);
        break;
    }
    case OP_READ_DIRENTS: {
            if (cqe->res > 0) { // If res > 0, we read some bytes
                struct linux_dirent64 *d;
                long bpos = 0;
                while (bpos < cqe->res) {
                    d = (struct linux_dirent64 *)((char *)req->dirents + bpos);

                    if (strcmp(d->d_name, ".") != 0 && strcmp(d->d_name, "..") != 0) {
                        char full_path[PATH_MAX];
                        // FIXED: Use req->path, which holds the path of the directory we are currently in.
                        snprintf(full_path, sizeof(full_path), "%s/%s", req->path, d->d_name);

                        if (d->d_type == DT_DIR) {
                            printf("\n[GAD]\n");
                            submit_open_request(full_path, ring, inflight_ops);
                        } else if (d->d_type == DT_REG) { // FIXED: Typo d_type
                            if (strstr(d->d_name, search_term) != NULL) {
                                // FIXED: Print the correctly constructed full_path
                                printf("[Found] : %s\n", full_path);
                            }
                        }
                        // We are ignoring DT_UNKNOWN for now for simplicity.
                        // A full implementation would submit an OP_STAT request here.
                    }
                    bpos += d->d_reclen;
                }
                // Re-submit the read request to get the next batch of entries
                submit_getdents_request(req->dir_fd, req->path, ring, inflight_ops);
            } else { // If res is 0, we're done reading this directory
                close(req->dir_fd);
            }
            break;
    }
    // case OP_STAT:{
    //     // statx suceeded. check the results
    //     if (req->statx_buf.stx_mask ==0)
    //     {
    //         break;
    //     }
        
    //     if (S_ISDIR(req->statx_buf.stx_mode))
    //     {
    //         submit_open_request(req->path,ring,inflight_ops);

    //     }
    //     else if (S_ISREG(req->statx_buf.stx_mode))
    //     {
    //         if (strstr(d->d_name,search_term))
    //         {
    //             char full_path[PATH_MAX];
    //             snprintf(full_path, sizeof(full_path), "%s/%s", req->path, entry->d_name);
    //             printf("[FOUND] %s\n",full_path);   
    //         }
                    
    //     }
    //     break;
    // }

    default:
        break;
    }
    if (req->type == OP_READ_DIRENTS) {
        free(req->dirents);
    }   

    free(req);
    
}

int main(int argc,char *argv[]){
    if (argc!=3)
    {
        fprintf(stderr,"Usage: %s <path> <search_term>\n", argv[0]);
        exit(1);
    }
    const char* search_path = argv[1];
    const char* search_term = argv[2];
    printf("[SEARCH] Starting in '%s' for files containing '%s'\n",search_path,search_term);


    struct io_uring ring;

    int ret = io_uring_queue_init(256,&ring,0);
    if (ret < 0) { fprintf(stderr, "io_uring_queue_init: %s\n", strerror(-ret)); return 1; }
    int inflight_ops = 0;

    submit_open_request(search_path,&ring,&inflight_ops);
    while (1)
    {
        io_uring_submit_and_wait(&ring,1);
    
    
        struct io_uring_cqe *cqe;
        unsigned head;
        unsigned count=0;

        io_uring_for_each_cqe(&ring,head,cqe){
            handle_completion(cqe,search_term,&ring,&inflight_ops);

            // free(req);
            inflight_ops--;
            count++;
        }

        io_uring_cq_advance(&ring, count);

        if (inflight_ops==0)
        {
            break;
        }
    }

    io_uring_queue_exit(&ring);
    

    return 0;

}