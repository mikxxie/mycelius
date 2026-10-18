#pragma once
#include <stdint.h>
#include <stddef.h>

/* Mycelius filesystem API - public header for file operations.
 * All paths are absolute, starting with '/'.
 */

/* File open flags */
#define MYCELIUS_O_RDONLY  0x0001
#define MYCELIUS_O_WRONLY  0x0002
#define MYCELIUS_O_RDWR    0x0003
#define MYCELIUS_O_CREAT   0x0008
#define MYCELIUS_O_TRUNC   0x0010
#define MYCELIUS_O_APPEND  0x0020

/* File open return value: negative on error, file descriptor >= 0 */
typedef int mycelius_fd_t;

/* Open a file. Returns fd >= 0 on success, negative on error. */
mycelius_fd_t mycelius_open(const char *path, int flags);

/* Read up to count bytes from fd into buf. Returns bytes read, -1 on error. */
int mycelius_read(mycelius_fd_t fd, void *buf, size_t count);

/* Write up to count bytes from buf to fd. Returns bytes written, -1 on error. */
int mycelius_write(mycelius_fd_t fd, const void *buf, size_t count);

/* Close fd. Returns 0 on success, -1 on error. */
int mycelius_close(mycelius_fd_t fd);

/* Create a directory. Returns 0 on success, -1 on error. */
int mycelius_mkdir(const char *path);

/* Remove a file or directory. If recursive, remove directory contents too.
 * Returns 0 on success, -1 on error. */
int mycelius_rm(const char *path, int recursive);

/* Change current working directory. Returns 0 on success, -1 on error. */
int mycelius_chdir(const char *path);

/* Get current working directory. Writes up to size bytes into buf.
 * Returns 0 on success, -1 on error. */
int mycelius_getcwd(char *buf, size_t size);

/* List directory contents. Returns 0 on success, -1 on error.
 * callback is called for each entry with (name, is_dir, user_data). */
typedef int (*mycelius_ls_cb)(const char *name, int is_dir, void *user_data);
int mycelius_ls(const char *path, mycelius_ls_cb cb, void *user_data);

/* Read file contents into a newly allocated buffer (caller frees).
 * Returns number of bytes read, or -1 on error. */
int mycelius_read_file(const char *path, void **buf_out);

/* Write file contents from buf. Returns 0 on success, -1 on error. */
int mycelius_write_file(const char *path, const void *buf, size_t len);
