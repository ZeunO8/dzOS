// syscall.c
#include "../include/syscall.h"
#include "dzfs.h"
#include "common/printf.h"
#include "device.h"
#include "file.h"
#include "../include/file.h"
#include "mem/vmm.h"
#include "userspace/proc.h"
#include "mem/kmalloc.h"

/**
 * Validate and copy a user string argument.
 * Returns pointer to kernel buffer (must be freed), or NULL on error.
*/
char *validate_user_string(const char *user_str, size_t max_len)
{
	struct process *p = my_process();
	if (!p) return NULL;

	char *kernel_buf = kmalloc(max_len);
	if (!kernel_buf) return NULL;

	int result = vmm_copy_user_string(p->pagetable, user_str, kernel_buf, max_len);
	if (result < 0) {
		kmfree(kernel_buf);
		return NULL;
	}

	return kernel_buf;
}

/**
 * Validate a user pointer for read access.
 */
bool validate_user_read(const void *ptr, size_t len)
{
	struct process *p = my_process();
	if (!p) return false;
	return vmm_validate_user_ptr(p->pagetable, ptr, len, false);
}

/**
 * Validate a user pointer for write access.
 */
bool validate_user_write(void *ptr, size_t len)
{
	struct process *p = my_process();
	if (!p) return false;
	return vmm_validate_user_ptr(p->pagetable, ptr, len, true);
}

/**
 * open syscall. Either opens a file on physical disk or a device.
 */
int sys_open(const char *path, int flags) {
	// Validate path string
	char *kernel_path = validate_user_string(path, MAX_PATH_LENGTH);
	if (!kernel_path) {
		return -1; // EFAULT equivalent
	}

	// Flags validation
	if (flags & ~(O_RDONLY | O_WRONLY | O_RDWR | O_CREAT | O_TRUNC | 
	              O_APPEND | O_DEVICE | O_DIR)) {
		kmfree(kernel_path);
		return -1; // EINVAL equivalent
	}

	// Determine if device or file
	int result;
	if (flags & O_DEVICE) {
		result = device_open(kernel_path);
	} else {
		result = file_open(kernel_path, flags);
	}

	kmfree(kernel_path);
	return result;
}

/**
 * read syscall. Either read from a file on physical disk or a device.
 */
int sys_read(int fd, void *buffer, size_t len) {
	// Validate fd range
	struct process *p = my_process();
	if (fd < 0 || fd >= MAX_OPEN_FILES || p->open_files[fd].type == FD_EMPTY) {
		return -1; // EBADF
	}

	// Validate buffer is writable
	if (!validate_user_write(buffer, len)) {
		return -1; // EFAULT
	}

	// Check if fd is readable
	if (!p->open_files[fd].readble) {
		return -1; // EBADF
	}

	// Perform read based on type
	switch (p->open_files[fd].type) {
	case FD_INODE:
		return file_read(fd, buffer, len);
	case FD_DEVICE: {
		struct device *dev = device_get(p->open_files[fd].structures.device);
		if (dev == NULL) return -1;
		return dev->read((char *)buffer, len);
	}
	default:
		return -1;
	}
}

/**
 * write syscall. Either write to a file on physical disk or a device.
 */
int sys_write(int fd, const void *buffer, size_t len) {
	// Validate fd range
	struct process *p = my_process();
	if (fd < 0 || fd >= MAX_OPEN_FILES || p->open_files[fd].type == FD_EMPTY) {
		return -1; // EBADF
	}

	// Validate buffer is readable
	if (!validate_user_read(buffer, len)) {
		return -1; // EFAULT
	}

	// Check if fd is writable
	if (!p->open_files[fd].writable) {
		return -1; // EBADF
	}

	// Perform write based on type
	switch (p->open_files[fd].type) {
	case FD_INODE:
		return file_write(fd, buffer, len);
	case FD_DEVICE: {
		struct device *dev = device_get(p->open_files[fd].structures.device);
		if (dev == NULL) return -1;
		return dev->write((const char *)buffer, len);
	}
	default:
		return -1;
	}
}

/**
 * Closes a file descriptor. This is an no-op on devices.
 */
int sys_close(int fd) {
  // Is this fd valid?
  struct process *p = my_process();
  if (fd < 0 || fd > MAX_OPEN_FILES || p->open_files[fd].type == FD_EMPTY)
    return -1;
  // Read from the file/device
  switch (p->open_files[fd].type) {
  case FD_INODE:
    fs_close(p->open_files[fd].structures.inode);
    p->open_files[fd].type = FD_EMPTY;
    p->open_files[fd].readble = false;
    p->open_files[fd].writable = false;
    p->open_files[fd].structures.inode = NULL;
    p->open_files[fd].offset = 0;
    return 0;
  case FD_DEVICE: // nothing to do
    return 0;
  default: // not implemented
    return -1;
  }
}

/**
 * Changes the offset of a file descriptor. Returns the new offset of the file
 * descriptor.
 */
int sys_lseek(int fd, int64_t offset, int whence) {
  // Is this fd valid?
  struct process *p = my_process();
  if (fd < 0 || fd > MAX_OPEN_FILES || p->open_files[fd].type == FD_EMPTY)
    return -1;
  // Read from the file/device
  switch (p->open_files[fd].type) {
  case FD_INODE:
    return file_seek(fd, offset, whence);
  case FD_DEVICE:
  {
    struct device *dev = device_get(p->open_files[fd].structures.device);
    if (dev == NULL || dev->lseek == NULL)
      return -1;
    return dev->lseek(offset, whence);
  }
  default: // not implemented
    return -1;
  }
}

/**
 * Sends a command to a device and returns the result of the command or the
 * input in the third argument.
 */
int sys_ioctl(int fd, int command, void *data) {
	struct process *p = my_process();
	if (fd < 0 || fd >= MAX_OPEN_FILES || p->open_files[fd].type == FD_EMPTY) {
		return -1;
	}

	// Validate data pointer based on command (command-specific validation)
	// For now, assume data is a pointer that needs validation
	// Real implementation would validate based on command type
	if (data && !validate_user_write(data, sizeof(uint64_t))) {
		// Most ioctls pass pointers to uint64_t or similar
		return -1;
	}

	switch (p->open_files[fd].type) {
	case FD_INODE:
		return -1; // Files don't support ioctl
	case FD_DEVICE: {
		struct device *dev = device_get(p->open_files[fd].structures.device);
		if (dev == NULL || dev->control == NULL) return -1;
		return dev->control(command, data);
	}
	default:
		return -1;
	}
}

/**
 * Renames a file or a folder. Renaming can be the same as moving the
 * file/folder.
 *
 * On success, zero is returned.  On error, -1 is returned.
 */
int sys_rename(const char *old_path, const char *new_path) {
	char *kernel_old = validate_user_string(old_path, MAX_PATH_LENGTH);
	if (!kernel_old) return -1;

	char *kernel_new = validate_user_string(new_path, MAX_PATH_LENGTH);
	if (!kernel_new) {
		kmfree(kernel_old);
		return -1;
	}

	int result = fs_rename(kernel_old, kernel_new, my_process()->working_directory);

	kmfree(kernel_old);
	kmfree(kernel_new);
	return result;
}

/**
 * Remove a file or *empty* directory.
 * In case that you want to delete a non empty directory, you have to
 * recursively delete all files in it.
 *
 * On success, zero is returned.  On error, -1 is returned.
 */
int sys_unlink(const char *path) {
	char *kernel_path = validate_user_string(path, MAX_PATH_LENGTH);
	if (!kernel_path) return -1;

	int result = fs_delete(kernel_path, my_process()->working_directory);

	kmfree(kernel_path);
	return result;
}

/**
 * Creates an empty directory with the given path.
 *
 * On success, zero is returned.  On error, -1 is returned.
 */
int sys_mkdir(const char *directory) {
	char *kernel_path = validate_user_string(directory, MAX_PATH_LENGTH);
	if (!kernel_path) return -1;

	int result = fs_mkdir(kernel_path, my_process()->working_directory);

	kmfree(kernel_path);
	return result;
}

/**
 * Changes the working directory of the current process to
 * the given relative path.
 */
int sys_chdir(const char *directory) {
	char *kernel_path = validate_user_string(directory, MAX_PATH_LENGTH);
	if (!kernel_path) return -1;

	struct process *p = my_process();
	struct fs_inode *new_chdir = fs_open(kernel_path, p->working_directory, DZFS_O_DIR);
	
	kmfree(kernel_path);

	if (new_chdir == NULL) {
		return -1;
	}

	// Free the old inode
	fs_close(p->working_directory);
	p->working_directory = new_chdir;
	return 0;
}

/**
 * Reads the content of a directory into the given buffer.
 * Returns the number of entries read or a negative item if
 * there was an error. If the result is zero, it means that either
 * the buffer is so small that nothing can be read or we have reached
 * the end of the directory.
 * The len must be the size of the given buffer.
 * Buffer will contain an array of dirent type defined in file.h
 */
int sys_readdir(int fd, void *buffer, size_t len) {
	struct process *p = my_process();
	if (!p) return -1;

	// Validate fd
	if (fd < 0 || fd >= MAX_OPEN_FILES || p->open_files[fd].type != FD_INODE) {
		return -1;
	}

	// Validate buffer
	if (!validate_user_write(buffer, len)) {
		return -1;
	}

	// Read directories
	int result = fs_readdir(p->open_files[fd].structures.inode, buffer, len,
	                        p->open_files[fd].offset);
	if (result <= 0) return result;

	// Save how many entries we have read
	p->open_files[fd].offset += result;
	return result;
}