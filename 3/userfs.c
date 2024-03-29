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
	size_t occupied;
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
    struct block *current_block;
    size_t offset_in_block;

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
    file_descriptors[fd]->current_block = f->block_list;
    file_descriptors[fd]->offset_in_block = 0;
    f->refs++;
    file_descriptor_count++;

    ufs_error_code = UFS_ERR_NO_ERR;
    return fd;
}

ssize_t ufs_write(int fd, const char *buf, size_t size) {
    if (fd < 0 || fd >= file_descriptor_capacity || file_descriptors[fd] == NULL) {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }

    struct filedesc *fdesc = file_descriptors[fd];
    struct block *current_block = fdesc->current_block;

    // ensure there is a current block to start with
    if (!current_block) {
        if (!fdesc->file->block_list) { // file is empty, create the first block
            current_block = malloc(sizeof(struct block));
            if (!current_block) {
                ufs_error_code = UFS_ERR_NO_MEM;
                return -1;
            }
            current_block->memory = malloc(BLOCK_SIZE);
            if (!current_block->memory) {
                free(current_block);
                ufs_error_code = UFS_ERR_NO_MEM;
                return -1;
            }
            current_block->occupied = 0;
            current_block->next = NULL;
            current_block->prev = NULL;
            fdesc->file->block_list = current_block;
            fdesc->file->last_block = current_block;
            fdesc->current_block = current_block;
        } else {
            // if the file isn't empty but current_block is null, start from the last block
            current_block = fdesc->file->last_block;
            fdesc->current_block = current_block;
        }
        fdesc->offset_in_block = current_block->occupied;
    }

    ssize_t written = 0;
    while (size > 0) {
        if (fdesc->offset_in_block >= BLOCK_SIZE) {
            if (!current_block->next) {
                struct block *new_block = malloc(sizeof(struct block));
                if (!new_block) {
                    ufs_error_code = UFS_ERR_NO_MEM;
                    return written; // return the amount successfully written before running out of memory
                }
                new_block->memory = malloc(BLOCK_SIZE);
                if (!new_block->memory) {
                    free(new_block);
                    ufs_error_code = UFS_ERR_NO_MEM;
                    return written;
                }
                new_block->occupied = 0;
                new_block->next = NULL;
                new_block->prev = current_block;
                current_block->next = new_block;
                fdesc->file->last_block = new_block;
            }
            current_block = current_block->next;
            fdesc->current_block = current_block;
            fdesc->offset_in_block = current_block->occupied;
        }

        size_t space_in_block = BLOCK_SIZE - fdesc->offset_in_block;
        size_t write_amount = (size < space_in_block) ? size : space_in_block;

        memcpy(current_block->memory + fdesc->offset_in_block, buf, write_amount);
        current_block->occupied = fdesc->offset_in_block + write_amount;
        fdesc->offset_in_block += write_amount;
        buf += write_amount;
        size -= write_amount;
        written += write_amount;
    }

    ufs_error_code = UFS_ERR_NO_ERR;
    return written;
}


ssize_t ufs_read(int fd, char *buf, size_t size) {
    if (fd < 0 || fd >= file_descriptor_capacity || file_descriptors[fd] == NULL) {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }

    struct filedesc *fdesc = file_descriptors[fd];
    // check if there's a current block to start reading from; otherwise, start from the first block.
    if (fdesc->current_block == NULL && fdesc->file->block_list != NULL) {
        fdesc->current_block = fdesc->file->block_list;
        fdesc->offset_in_block = 0; // Start reading from the beginning of the block.
    }

    ssize_t total_read = 0;
    while (size > 0 && fdesc->current_block != NULL) {
        size_t remaining_in_block = fdesc->current_block->occupied - fdesc->offset_in_block;
        size_t bytes_to_read = (size < remaining_in_block) ? size : remaining_in_block;

        // copy data from the current block to the buffer.
        memcpy(buf, fdesc->current_block->memory + fdesc->offset_in_block, bytes_to_read);
        buf += bytes_to_read;
        size -= bytes_to_read;
        total_read += bytes_to_read;
        fdesc->offset_in_block += bytes_to_read;

        // check if we have read the entire current block and there's more to read.
        if (fdesc->offset_in_block == fdesc->current_block->occupied && size > 0) {
            // move to the next block, if available.
            fdesc->current_block = fdesc->current_block->next;
            fdesc->offset_in_block = 0; // Reset offset for the new block.
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
    // free all files and their blocks.
    struct file *current_file = file_list;
    while (current_file != NULL) {
        // save next file pointer because we're going to free the current file.
        struct file *next_file = current_file->next;

        // free all blocks associated with this file.
        struct block *current_block = current_file->block_list;
        while (current_block != NULL) {
            struct block *next_block = current_block->next;
            free(current_block->memory);
            free(current_block);
            current_block = next_block;
        }

        // free the file's metadata (e.g., name) if dynamically allocated.
        free(current_file->name);

        // free the file structure itself.
        free(current_file);

        current_file = next_file;
    }
    file_list = NULL;

    // free the file descriptors array if it's dynamically allocated.
    if (file_descriptors != NULL) {
        for (int i = 0; i < file_descriptor_capacity; i++) {
            // if using dynamic allocation for file descriptors, free them.
            free(file_descriptors[i]);
        }
        free(file_descriptors);
        file_descriptors = NULL;
    }

    // reset file descriptor count and capacity.
    file_descriptor_count = 0;
    file_descriptor_capacity = 0;

    // reset the global error code.
    ufs_error_code = UFS_ERR_NO_ERR;
}
