#include <switch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "cached_stdio.h"

#ifndef MAX_PATH
#define MAX_PATH 512
#endif

#define MAX_MODESTR    8  // just in case
#define MAX_OPEN_FILES 4

typedef struct fileref_s {
    char path[MAX_PATH + 1];
    char mode[MAX_MODESTR + 1];

    FILE *fd;
    long fpos;

    struct fileref_s *prev;
    struct fileref_s *next;
} fileref_t;

// number of currently open and cached files
static int fcache_numopen = 0;

// number of currently cached files
static int fcache_numfiles = 0;

// doubly linked cached file list, sorted from oldest to youngest
static fileref_t *fcache = NULL;

// end of the list
static fileref_t *fcache_end = NULL;

// in case of multithreaded shit
static RMutex fmutex = { 0 };

static int fcache_suspend(fileref_t *fr) {
    if (!fr) {
        fprintf(stderr, "ERROR: fcache_suspend(): fr == NULL\n");
        return -1;
    }

    if (!fr->fd) {
        fprintf(stderr, "ERROR: fcache_suspend(%p(`%s`)): double suspend\n", fr, fr->path);
        return -2;
    }

    fr->fpos = ftell(fr->fd);
    fclose(fr->fd);
    fr->fd = NULL;

    fcache_numopen--;
    return 0;
}

static inline void fcache_ensure_limits(void) {
    for (fileref_t *fr = fcache; fr; fr = fr->next) {
        if (fr->fd) {
            fcache_suspend(fr);
            return;
        }
    }
}

static int fcache_resume(fileref_t *fr) {
    if (!fr) {
        fprintf(stderr, "ERROR: fcache_resume(): fr == NULL\n");
        return -1;
    }

    if (fr->fd) {
        fprintf(stderr, "ERROR: fcache_resume(%p(`%s`)): double resume\n", fr, fr->path);
        return -2;
    }

    if (fcache_numopen >= MAX_OPEN_FILES)
        fcache_ensure_limits();

    fr->fd = fopen(fr->path, fr->mode);
    if (!fr->fd) {
        fprintf(stderr, "ERROR: fcache_resume(%p(`%s`)): could not resume: %s\n", fr, fr->path, strerror(errno));
        return -3;
    }

    fseek(fr->fd, fr->fpos, SEEK_SET);
    fr->fpos = 0;

    fcache_numopen++;
    return 0;
}

static fileref_t *fcache_link(const char *path, const char *mode) {
    fileref_t *fr = calloc(1, sizeof(fileref_t));
    if (!fr) {
        fprintf(stderr, "ERROR: fcache_link(`%s`, `%s`): OOM on fileref_t alloc\n", path, mode);
        return NULL;
    }

    strncpy(fr->path, path, MAX_PATH);
    strncpy(fr->mode, mode, MAX_MODESTR);

    // add to end
    if (fcache_end) {
        fcache_end->next = fr;
        fr->prev = fcache_end;
        fcache_end = fr;
    } else {
        fcache = fcache_end = fr;
    }

    fcache_resume(fr);
    return fr;
}

static int fcache_unlink(fileref_t *fr) {
    if (!fr) {
        fprintf(stderr, "ERROR: fcache_unlink(): NULL fr\n");
        return EINVAL;
    }

    if (fr->fd)
        fcache_suspend(fr);

    if (fr == fcache_end)
        fcache_end = fr->prev;

    if (fr->prev) fr->prev->next = fr->next;
    if (fr->next) fr->next->prev = fr->prev;

    free(fr);
    return 0;
}

static inline FILE *fcache_get(fileref_t *fr) {
    if (!fr) {
        fprintf(stderr, "ERROR: fcache_get(): NULL fr\n");
        return NULL;
    }

    if (!fr->fd)
        fcache_resume(fr);

    return fr->fd;
}

// cached stdio functions

FILE *fcached_fopen(const char *path, const char *mode) {
    rmutexLock(&fmutex);
    fileref_t *fr = fcache_link(path, mode);
    rmutexUnlock(&fmutex);
    return (FILE *)fr;
}

FILE *fcached_freopen(const char *path, const char *mode, FILE *pfd) {
    if (!pfd || (!path && !mode))
        return NULL;

    rmutexLock(&fmutex);
    fileref_t *fr = (fileref_t *)pfd;
    FILE *fd = freopen(path, mode, fr->fd);
    if (fd) {
        fr->fd = fd;
        fr->fpos = ftell(fd);
        if (path) strncpy(fr->path, path, MAX_PATH);
        if (mode) strncpy(fr->mode, mode, MAX_MODESTR);
        // TODO: maybe relink it into the end of the list?
    }
    rmutexUnlock(&fmutex);

    return (FILE *)fr;
}

int fcached_fclose(FILE *pfd) {
    fileref_t *fr = (fileref_t *)pfd;
    rmutexLock(&fmutex);
    int ret = fcache_unlink(fr);
    rmutexUnlock(&fmutex);
    return ret;
}

long fcached_ftell(FILE *pfd) {
    rmutexLock(&fmutex);
    FILE *fd = fcache_get((fileref_t *)pfd);
    long ret = fd ? ftell(fd) : -1;
    rmutexUnlock(&fmutex);
    return ret;
}

int fcached_fseek(FILE *pfd, long pos, int mode) {
    rmutexLock(&fmutex);
    FILE *fd = fcache_get((fileref_t *)pfd);
    int ret = fd ? fseek(fd, pos, mode) : -1;
    rmutexUnlock(&fmutex);
    return ret;
}

off_t fcached_ftello(FILE *pfd) {
    rmutexLock(&fmutex);
    FILE *fd = fcache_get((fileref_t *)pfd);
    off_t ret = fd ? ftello(fd) : -1;
    rmutexUnlock(&fmutex);
    return ret;
}

int fcached_fseeko(FILE *pfd, off_t pos, int mode) {
    rmutexLock(&fmutex);
    FILE *fd = fcache_get((fileref_t *)pfd);
    int ret = fd ? fseeko(fd, pos, mode) : -1;
    rmutexUnlock(&fmutex);
    return ret;
}

int fcached_fflush(FILE *pfd) {
    rmutexLock(&fmutex);
    FILE *fd = fcache_get((fileref_t *)pfd);
    int ret = fd ? fflush(fd) : -1;
    rmutexUnlock(&fmutex);
    return ret;
}

size_t fcached_fread(void *buf, size_t size, size_t n, FILE *pfd) {
    rmutexLock(&fmutex);
    FILE *fd = fcache_get((fileref_t *)pfd);
    size_t ret = fd ? fread(buf, size, n, fd) : 0;
    rmutexUnlock(&fmutex);
    return ret;
}

size_t fcached_fwrite(const void *buf, size_t size, size_t n, FILE *pfd) {
    rmutexLock(&fmutex);
    FILE *fd = fcache_get((fileref_t *)pfd);
    size_t ret = fd ? fwrite(buf, size, n, fd) : 0;
    rmutexUnlock(&fmutex);
    return ret;
}

int fcached_ferror(FILE *pfd) {
    rmutexLock(&fmutex);
    FILE *fd = fcache_get((fileref_t *)pfd);
    int ret = fd ? ferror(fd) : -1;
    rmutexUnlock(&fmutex);
    return ret;
}

int fcached_feof(FILE *pfd) {
    rmutexLock(&fmutex);
    FILE *fd = fcache_get((fileref_t *)pfd);
    int ret = fd ? feof(fd) : -1;
    rmutexUnlock(&fmutex);
    return ret;
}
