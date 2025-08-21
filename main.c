#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

void traverse_path(const char *path, const char *search_term){
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

       char full_path[1024];
       snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);

       struct stat st;
       if(lstat(full_path, &st) == -1) {
           perror(full_path);
           continue;
       }
       if (S_ISDIR(st.st_mode))
       {
           traverse_path(full_path, search_term);
       }
       else 
       {
          if (strstr(full_path, search_term))
          {
            printf("[FOUND] %s\n", full_path);
          }
          
       }
    }
    closedir(dir);
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