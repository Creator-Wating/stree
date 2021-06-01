#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <stdlib.h>
#include <libgen.h>
#define PATH "d:\\pstree\\os-workbench"
void main (){
    char* name = malloc (sizeof (char) * 2000);
    char* rname = NULL;
    name = dirname (PATH);
    rname = basename (PATH);
    printf ("%s\n", name);
    printf ("%s\n", rname);
    return;
}

