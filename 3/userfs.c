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

    /** Flag to mark the file as deleted. */
    int is_deleted;

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
    // check if the file descriptor is valid.
    if (fd < 0 || fd >= file_descriptor_capacity || file_descriptors[fd] == NULL) {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }

    struct filedesc *fdesc = file_descriptors[fd];
    struct file *file = fdesc->file;
    struct block *current_block = file->block_list;
    ssize_t written = 0;

    // check if there's any block in the file.
    if (!current_block) {
        // allocate the first block if it doesn't exist.
        file->block_list = malloc(sizeof(struct block));
        if (!file->block_list) {
            ufs_error_code = UFS_ERR_NO_MEM;
            return -1;
        }
        current_block = file->block_list;
        current_block->memory = malloc(BLOCK_SIZE);
        if (!current_block->memory) {
            free(current_block);
            file->block_list = NULL;
            ufs_error_code = UFS_ERR_NO_MEM;
            return -1;
        }
        current_block->occupied = 0;
        current_block->next = NULL;
        current_block->prev = NULL;
        file->last_block = current_block;
    } else {
        // find the last block to continue writing.
        while (current_block->next != NULL) {
            current_block = current_block->next;
        }
    }

    // start writing data.
    while (size > 0) {
        size_t available_space = BLOCK_SIZE - current_block->occupied;
        size_t to_write = size < available_space ? size : available_space;
        memcpy(current_block->memory + current_block->occupied, buf, to_write);

        current_block->occupied += to_write;
        written += to_write;
        buf += to_write;
        size -= to_write;

        // check if we need to allocate a new block.
        if (size > 0) {
            current_block->next = malloc(sizeof(struct block));
            if (!current_block->next) {
                ufs_error_code = UFS_ERR_NO_MEM;
                return written; // return the amount written so far.
            }
            current_block->next->prev = current_block;
            current_block = current_block->next;
            current_block->memory = malloc(BLOCK_SIZE);
            if (!current_block->memory) {
                current_block->prev->next = NULL;
                free(current_block);
                ufs_error_code = UFS_ERR_NO_MEM;
                return written; // return the amount written so far.
            }
            current_block->occupied = 0;
            current_block->next = NULL;
            file->last_block = current_block;
        }
    }

    ufs_error_code = UFS_ERR_NO_ERR;
    return written;
}

ssize_t
ufs_read(int fd, char *buf, size_t size)
{
    // validate the file descriptor.
    if (fd < 0 || fd >= file_descriptor_capacity || file_descriptors[fd] == NULL) {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }

    struct filedesc *fdesc = file_descriptors[fd];
    struct file *file = fdesc->file;
    if (!file || !file->block_list) {
        // the file has no content.
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }

    struct block *current_block = file->block_list;
    ssize_t total_read = 0;
    ssize_t bytes_to_read = 0;

    // start reading data.
    while (size > 0 && current_block != NULL) {
        // calculate the number of bytes to read from the current block.
        bytes_to_read = current_block->occupied < (int)size ? current_block->occupied : (int)size;

        // copy data from the current block to the buffer.
        memcpy(buf, current_block->memory, bytes_to_read);
        size -= bytes_to_read; // decrease the remaining size to read.
        total_read += bytes_to_read; // increase the total number of bytes read.
        buf += bytes_to_read; // move the buffer pointer forward.

        // move to the next block if there is still data to read.
        if (size > 0) {
            current_block = current_block->next;
        }
    }

    ufs_error_code = UFS_ERR_NO_ERR;
    return total_read;
}

int
ufs_close(int fd)
{
    // check if the file descriptor is valid.
    if (fd < 0 || fd >= file_descriptor_capacity || file_descriptors[fd] == NULL) {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }

    // retrieve the file descriptor and associated file.
    struct filedesc *fdesc = file_descriptors[fd];
    struct file *file = fdesc->file;

    // decrement the reference count for the file.
    if (--file->refs == 0) {
        // may be it is needed to implement some future logic there
    }

    // free or reset the file descriptor entry.
    free(fdesc);
    file_descriptors[fd] = NULL; // mark the file descriptor as available.

    ufs_error_code = UFS_ERR_NO_ERR;
    return 0;
}

int
ufs_delete(const char *filename)
{
    struct file *current = file_list, *prev = NULL;

    // search for the file by its name.
    while (current != NULL && strcmp(current->name, filename) != 0) {
        prev = current;
        current = current->next;
    }

    // ff the file was not found.
    if (current == NULL) {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }

    // if the file is currently opened by any descriptor.
    if (current->refs > 0) {
        // TODO: Optionally may be it is needed to mark the file as deleted for later removal when all refs are closed.
        // TODO: check later condition in ufs_close:     if (--file->refs == 0) { do_smth;}
        current->is_deleted = 1;
        ufs_error_code = UFS_ERR_NO_ERR;
        return 0;
    }

    // no open references, proceed with deletion.
    // First, free all data blocks.
    struct block *block = current->block_list;
    while (block != NULL) {
        struct block *next_block = block->next;
        free(block->memory);  // free the data within the block.
        free(block);          // free the block itself.
        block = next_block;
    }

    // remove the file from the list.
    if (prev != NULL) prev->next = current->next;
    else file_list = current->next; // file was the first in the list.

    if (current->next != NULL) current->next->prev = prev;

    // free the file name and the file structure.
    free(current->name);
    free(current);

    ufs_error_code = UFS_ERR_NO_ERR;
    return 0;
}

void
ufs_destroy(void)
{
}
