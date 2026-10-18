#pragma once
extern "C" {
#include "dev/fs.h"
}

namespace mycelius {

inline int fs_open(const char *path, int flags) {
    return mycelius_open(path, flags);
}

inline int fs_read(mycelius_fd_t fd, void *buf, size_t count) {
    return mycelius_read(fd, buf, count);
}

inline int fs_write(mycelius_fd_t fd, const void *buf, size_t count) {
    return mycelius_write(fd, buf, count);
}

inline int fs_close(mycelius_fd_t fd) {
    return mycelius_close(fd);
}

inline int fs_mkdir(const char *path) {
    return mycelius_mkdir(path);
}

inline int fs_rm(const char *path, int recursive = 0) {
    return mycelius_rm(path, recursive);
}

inline int fs_chdir(const char *path) {
    return mycelius_chdir(path);
}

inline int fs_getcwd(char *buf, size_t size) {
    return mycelius_getcwd(buf, size);
}

inline int fs_read_file(const char *path, void **buf_out) {
    return mycelius_read_file(path, buf_out);
}

inline int fs_write_file(const char *path, const void *buf, size_t len) {
    return mycelius_write_file(path, buf, len);
}

} // namespace mycelius
