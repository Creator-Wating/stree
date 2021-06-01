#include <dirent.h> 
#include <stdio.h> 
#include <ctype.h>

int main(void) {
  DIR *d;
  struct dirent *dir;
  d = opendir("/proc");
  if (d != NULL) {
    while ((dir = readdir(d)) != NULL ) {
        if (opendir (dir->d_name) != NULL && isdigit(*dir->d_name) )
      printf("%s\n", dir->d_name);
    }
  }
  closedir(d);
  return(0);
}
