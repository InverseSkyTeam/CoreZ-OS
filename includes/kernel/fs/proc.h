#ifndef FS_PROC_H
#define FS_PROC_H
#include <stdint.h>
struct file;
struct stat;
int proc_match(const char *path);
int proc_open(const char *path, uint8_t flags);
uint32_t proc_read(struct file *file, void *buf, uint32_t count);
int proc_stat(const char *path, struct stat *buf);
int proc_fstat(struct file *file, struct stat *buf);
int proc_access(const char *path);
int proc_lseek(struct file *file, int32_t offset, uint8_t whence);
#endif
