#include <stdio.h>
#include <stdlib.h>
#include <liburing.h>
#include <string.h>
#include <dirent.h>

#include "../headers/request.h"
#include <linux/fcntl.h>


void submit_open_request(const char *path, struct io_uring *ring, int *inflight_ops){
    Request *req = malloc(sizeof(Request));
    
    req->type = OP_OPEN;
    strncpy(req->path, path, sizeof(req->path)-1);
    
    struct io_uring *sqe = io_uring_get_sqe(ring);
    io_uring_prep_openat(sqe, AT_FDCWD, path,O_RDONLY | O_DIRECTORY , 0);
    io_uring_sqe_set_data(sqe,req);
}

void handle_completion(struct io_uring_cqe *cqe,Request *req,
const char *search_term, struct io_uring *ring, int *inflight_ops){

    if (cqe->res <0)
    {
        return;
    }

    switch (req->type)
    {
    case OP_OPEN:{
        int dir_fd = cqe->res;
        submit_getdents_requests(dir_fd,ring,inflight_ops);
        break;
    }
    case OP_READ_DIRENTS:{
        if (cqe->res >0)
        {
            struct linux_dirent64 *d;
            long bpos = 0;
            while (bpos < cqe->res)
            {
                //d = (struct linux_dirent64 *)(req->dirents + bpos);
            }
            
        }
        
        break;
    }

    
    default:
        break;
    }
    
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
    int inflight_ops = 0;

    submit_open_request(search_path,&ring,&inflight_ops);
    while (1)
    {
        io_uring_submit_and_wait(&ring,1);
    
    
        struct io_uring_cqe *cqe;
        unsigned head;
        unsigned count=0;

        io_uring_for_each_cqe(&ring,head,cqe){
            Request *req = (Request *)io_uring_cqe_get_data(cqe);

            handle_completion(cqe,req,search_term,&ring,&inflight_ops);

            free(req);
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