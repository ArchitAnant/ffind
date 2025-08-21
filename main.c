#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void traverse_path(const char *path, const char *search_term){

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
    traverse_path(search_path,search_term);
    
    return 0;
}