#include "userfs.h"
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

enum {
	BLOCK_SIZE = 512,
	MAX_FILE_SIZE = 1024 * 1024 * 100,
};

/** Global error code. Set from any function on any error. */
static enum ufs_error_code ufs_error_code = UFS_ERR_NO_ERR;

struct block {
	/** Block memory. */
	char *memory;
	/** How many bytes are occupied. */
	int occupied;
	/** Next block in the file. */
	struct block *next;
	/** Previous block in the file. */
	struct block *prev;

	/* PUT HERE OTHER MEMBERS */
};

struct file {
	/** Double-linked list of file blocks. */
	struct block *block_list;
	/**
	 * Last block in the list above for fast access to the end
	 * of file.
	 */
	struct block *last_block;
	/** How many file descriptors are opened on the file. */
	int refs;
	/** File name. */
	char *name;
	/** Files are stored in a double-linked list. */
	struct file *next;
	struct file *prev;

	/* PUT HERE OTHER MEMBERS */
};

/** List of all files. */
static struct file *file_list = NULL;

struct filedesc {
	struct file *file;

	/* PUT HERE OTHER MEMBERS */
};

/**
 * An array of file descriptors. When a file descriptor is
 * created, its pointer drops here. When a file descriptor is
 * closed, its place in this array is set to NULL and can be
 * taken by next ufs_open() call.
 */
static struct filedesc **file_descriptors = NULL;
static int file_descriptor_count = 0;
static int file_descriptor_capacity = 0;

enum ufs_error_code
ufs_errno()
{
	return ufs_error_code;
}

int ufs_open(const char *filename, int flags) {
    struct file *f = file_list;
    struct file *prev = NULL;
    int fd = -1;

    // check if file already exists.
    while (f != NULL) {
        if (strcmp(f->name, filename) == 0) {
            break;
        }
        prev = f;
        f = f->next;
    }

    if (f == NULL) {
        if ((flags & UFS_CREATE) == 0) {
            // file does not exist and UFS_CREATE is not set -> raise error
            ufs_error_code = UFS_ERR_NO_FILE;
            return -1;
        }

        // create the file.
        f = (struct file *)malloc(sizeof(struct file));
        if (f == NULL) {
            // error while allocating memory for file
            ufs_error_code = UFS_ERR_NO_MEM;
            return -1;
        }

        f->name = strdup(filename);
        f->block_list = NULL;
        f->last_block = NULL;
        f->refs = 0; // initially, no file descriptors are referring to this file.

        if (prev == NULL) {
            // this is the first file system in the list.
            file_list = f;
            f->prev = NULL;
        } else {
            // insert the file into the list.
            prev->next = f;
            f->prev = prev;
        }
        f->next = NULL; // currently, this is the last file.
    }

    // allocate or reuse a file descriptor.
    for (int i = 0; i < file_descriptor_capacity; ++i) {
        if (file_descriptors[i] == NULL) {
            file_descriptors[i] = (struct filedesc *)malloc(sizeof(struct filedesc));
            if (file_descriptors[i] == NULL) {
                ufs_error_code = UFS_ERR_NO_MEM;
                return -1;
            }
            fd = i;
            break;
        }
    }

    if (fd == -1) {
        // need to expand the file descriptor array.
        int new_capacity = file_descriptor_capacity == 0 ? 1 : file_descriptor_capacity * 2;
        struct filedesc **new_array = (struct filedesc **)realloc(file_descriptors, new_capacity * sizeof(struct filedesc *));
        if (new_array == NULL) {
            ufs_error_code = UFS_ERR_NO_MEM;
            return -1;
        }
        file_descriptors = new_array;
        fd = file_descriptor_capacity; // use the first new slot.
        file_descriptors[fd] = (struct filedesc *)malloc(sizeof(struct filedesc));
        if (file_descriptors[fd] == NULL) {
            ufs_error_code = UFS_ERR_NO_MEM;
            return -1;
        }
        file_descriptor_capacity = new_capacity;
    }

    // initialize the file descriptor.
    file_descriptors[fd]->file = f;
    f->refs++;
    file_descriptor_count++;

    ufs_error_code = UFS_ERR_NO_ERR;
    return fd;
}

ssize_t
ufs_write(int fd, const char *buf, size_t size)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)fd;
	(void)buf;
	(void)size;
	ufs_error_code = UFS_ERR_NOT_IMPLEMENTED;
	return -1;
}

ssize_t
ufs_read(int fd, char *buf, size_t size)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)fd;
	(void)buf;
	(void)size;
	ufs_error_code = UFS_ERR_NOT_IMPLEMENTED;
	return -1;
}

int
ufs_close(int fd)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)fd;
	ufs_error_code = UFS_ERR_NOT_IMPLEMENTED;
	return -1;
}

int
ufs_delete(const char *filename)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)filename;
	ufs_error_code = UFS_ERR_NOT_IMPLEMENTED;
	return -1;
}

void
ufs_destroy(void)
{
}
