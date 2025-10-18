#include "file.h"
#include "../../include/file.h"
#include "dzfs.h"
#include "common/printf.h"
#include "userspace/proc.h"

/**
 * Opens a file/directory in the running process.
 * The path must be a FILE in the filesystem. Devices and
 * pipes are not handled here.
 *
 * Returns the fd which was allocated. -1 on failure.
 */
int file_open(const char *path, uint32_t flags) {
  // Look for an empty file.
  int fd = proc_allocate_fd();
  if (fd == -1)
    return -1;
  struct process *p = my_process(); // p is not null
  // Note: I can defer the p->open_files[i].type = FD_... because
  // of single threaded.
  // Try to open the file
  // Flags for now are very simple.
  uint32_t fs_flags = 0;
  if (flags & O_CREAT)
    fs_flags |= DZFS_O_CREATE;
  if (flags & O_DIR)
    fs_flags |= DZFS_O_DIR;
  struct fs_inode *inode = fs_open(path, p->working_directory, fs_flags);
  if (inode == NULL)
    return -1;
  // Now open the file
  p->open_files[fd].type = FD_INODE;
  p->open_files[fd].structures.inode = inode;
  p->open_files[fd].offset = 0;
  p->open_files[fd].readble = (flags & O_WRONLY) == 0;
  p->open_files[fd].writable = (flags & O_WRONLY) || (flags & O_RDWR);
  // TODO: If we are creating a directory and the parent directory is open
  // somewhere, we shall update the number of entries in the parent directory.
  return fd;
}

/**
 * Writes a data to a file. Expects the fd to be valid.
 *
 * Returns the number of bytes written or a negative value on error.
 */
int file_write(int fd, const char *buffer, size_t len) {
  struct process *p = my_process();
  if (p == NULL)
    panic("file_write: no process");
  if (fd < 0 || fd >= MAX_OPEN_FILES || !p->open_files[fd].writable ||
      p->open_files[fd].type != FD_INODE)
    panic("file_write: fd");
  int result = fs_write(p->open_files[fd].structures.inode, buffer, len,
                        p->open_files[fd].offset);
  if (result < 0)
    return result;
  p->open_files[fd].offset += result;
  return result;
}

/**
 * Reads data from a file. Expects the fd to be valid.
 *
 * Returns the number of bytes written or a negative value on error.
 */
int file_read(int fd, char *buffer, size_t len) {
  struct process *p = my_process();
  if (p == NULL)
    panic("file_read: no process");
  if (fd < 0 || fd >= MAX_OPEN_FILES || !p->open_files[fd].readble ||
      p->open_files[fd].type != FD_INODE)
    panic("file_read: fd");
  int result = fs_read(p->open_files[fd].structures.inode, buffer, len,
                       p->open_files[fd].offset);
  if (result < 0)
    return result;
  p->open_files[fd].offset += result;
  return result;
}

/**
 * Seek to a specific part of file based on whence.
 * This function is almost like the lseek syscall on Linux
 */
int file_seek(int fd, int64_t offset, int whence) {
  struct process *p = my_process();
  if (p == NULL)
    panic("file_seek: no process");
  if (fd < 0 || fd >= MAX_OPEN_FILES || p->open_files[fd].type != FD_INODE)
    panic("file_seek: fd");
  uint32_t file_size = p->open_files[fd].structures.inode->size;
  switch (whence) {
  case SEEK_SET:
    p->open_files[fd].offset = (uint32_t)offset;
    break;
  case SEEK_CUR:
    p->open_files[fd].offset =
        (uint32_t)((int64_t)p->open_files[fd].offset + offset);
    break;
  case SEEK_END:
    p->open_files[fd].offset = file_size - (uint32_t)offset;
    break;
  default:
    return -1;
  }
  // At last, check if we are out of the bounds of the file
  if (p->open_files[fd].offset > file_size)
    p->open_files[fd].offset = file_size;
  return p->open_files[fd].offset;
}