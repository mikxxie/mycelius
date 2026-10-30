#ifndef _STDIO_H_
#define _STDIO_H_

#include <sys/types.h>

struct FILE {
    int fd;
    int flags;
};

typedef struct FILE FILE;

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

FILE *fopen(const char *path, const char *mode);
int fclose(FILE *stream);
int fprintf(FILE *stream, const char *format, ...);
int printf(const char *format, ...);
int fflush(FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
int fputc(int c, FILE *stream);
int vfprintf(FILE *stream, const char *format, va_list ap);

#endif
