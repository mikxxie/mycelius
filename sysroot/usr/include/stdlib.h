#ifndef _STDLIB_H_
#define _STDLIB_H_

#include <sys/types.h>

void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
int atoi(const char *nptr);
long atol(const char *nptr);
void exit(int status);
int system(const char *command);
int setenv(const char *name, const char *value, int overwrite);
int unsetenv(const char *name);

#endif
