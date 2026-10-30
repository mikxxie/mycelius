#ifndef _SYS_STAT_H_
#define _SYS_STAT_H_

#include <sys/types.h>

#define S_IRWXU 00700
#define S_IRUSR 00400
#define S_IWUSR 00200

struct stat {
    unsigned long st_dev;
    unsigned long st_ino;
    unsigned long st_mode;
    unsigned long st_nlink;
    unsigned long st_uid;
    unsigned long st_gid;
    unsigned long st_rdev;
    long st_size;
    long st_blksize;
    long st_blocks;
    long st_atime;
    long st_mtime;
    long st_ctime;
};

int chmod(const char *path, unsigned int mode);
int stat(const char *path, struct stat *buf);

#endif
