#include <stdio.h>
#include <string.h>
#include <stdlib.h>
int main(int argn, char *argv[]) {
FILE *g=fopen(argv[1],"w");
int i;
for(i = 0 ; i< 10 * 1024 * 1024 && fprintf(g,"*");i++);
return 0; }
