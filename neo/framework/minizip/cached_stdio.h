#ifndef __CACHED_STDIO_H
#define __CACHED_STDIO_H

#include <stdio.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

FILE *fcached_fopen(const char *path, const char *mode);
FILE *fcached_freopen(const char *path, const char *mode, FILE *pfd);
int fcached_fclose(FILE *pfd);

long fcached_ftell(FILE *pfd);
int fcached_fseek(FILE *pfd, long pos, int mode);
off_t fcached_ftello(FILE *pfd);
int fcached_fseeko(FILE *pfd, off_t pos, int mode);

size_t fcached_fread(void *buf, size_t size, size_t n, FILE *pfd);
size_t fcached_fwrite(const void *buf, size_t size, size_t n, FILE *pfd);

int fcached_ferror(FILE *pfd);
int fcached_feof(FILE *pfd);
int fcached_fflush(FILE *pfd);

#ifdef __cplusplus
}
#endif

#endif
