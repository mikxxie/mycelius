#ifndef _UNISTD_H_
#define _UNISTD_H_

#include <sys/types.h>

unsigned int sleep(unsigned int seconds);
int close(int fd);
int unlink(const char *path);

int getopt(int argc, char * const argv[], const char *optstring);
extern char *optarg;
extern int optind;
extern int opterr;
extern int optopt;

#endif
