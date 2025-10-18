#include "dzfs.h"
#include <stdbool.h>
#include "common/lib.h"
/**
 * The dnode which superblock resides in
 */
#define SUPERBLOCK_DNODE 1
/**
 * Try to do an IO operation
 */
#define TRY_IO(func) do { if (func) {result = DZFS_ERR_IO; goto end;} } while (0);

#define MIN(x, y) ((x < y) ? (x) : (y))

/**
 * Gets the length of the next part in the path. For example, if the given string
 * is "hello/world/path" the result would be 5. An empty string yields 0.
 * @param str The path
 * @return Length of next part in path
 */
static size_t path_next_part_len(const char *str) {
    size_t len = 0;
    while (str[len] != '\0' && str[len] != '/')
        len++;
    return len;
}

/**
 * Detects if this is the last part of a path or not. For example,
 * "hello" is the last part of the path same is "hello/". However,
 * "hello/world" is not the last part of the path.
 * @param str The path to check
 * @return True if last part, otherwise false
 */
static bool path_last_part(const char *str) {
    size_t current_len = path_next_part_len(str);
    if (str[current_len] == '\0') // null terminator is always the last part
        return true;
    // Here it means that the last char is /
    return str[current_len + 1] == '\0';
}

/**
 * Sets the nth bit in a bitmap to one
 * @param bitmap The bitmap
 * @param index The index in range [0, DZFS_BLOCK_SIZE*8]
 */
static void bitmap_set(struct dzFSBitmapBlock *bitmap, uint32_t index) {
    size_t char_index = index / 8;
    uint32_t bit_index = index % 8;
    if (char_index >= DZFS_BLOCK_SIZE) // out of bounds
        return;
    bitmap->bitmap[char_index] |= 1 << bit_index;
}

/**
 * Clears the nth bit in a bitmap to one
 * @param bitmap The bitmap
 * @param index The index in range [0, DZFS_BLOCK_SIZE*8]
 */
static void bitmap_clear(struct dzFSBitmapBlock *bitmap, uint32_t index) {
    size_t char_index = index / 8;
    uint32_t bit_index = index % 8;
    if (char_index >= DZFS_BLOCK_SIZE) // out of bounds
        return;
    bitmap->bitmap[char_index] &= ~(1 << bit_index);
}

/**
 * Allocates a free dnode and returns it
 * @param fs The filesystem
 * @return The dnode number or zero if no free dnode is available
 */
static uint32_t block_alloc(struct dzFS *fs) {
    uint32_t allocated_dnode = 0;
    union dzFSBlock *block = fs->allocate_mem_block();
    // Look for free blocks
    for (uint32_t free_block = 0; free_block < fs->free_bitmap_blocks; free_block++) {
        // Read the bitmap
        if (fs->read_block(free_block + 1 + 1, block)) // well fuck?
            goto end;
        // Look for free block...
        for (uint32_t i = 0; i < DZFS_BLOCK_SIZE; i++)
            if (block->bitmap.bitmap[i] != 0) {
                allocated_dnode =
                        free_block * DZFS_BITSET_COVERED_BLOCKS + i * 8 + __builtin_ctz(block->bitmap.bitmap[i]);
                break;
            }
        if (allocated_dnode != 0)
            break;
    }
    // Anything found?
    if (allocated_dnode == 0)
        goto end;
    // Mark this dnode as occupied
    if (fs->read_block(allocated_dnode / 8 / DZFS_BLOCK_SIZE + 1 + 1, block)) {
        allocated_dnode = 0;
        goto end;
    }
    bitmap_clear(&block->bitmap, allocated_dnode % DZFS_BITSET_COVERED_BLOCKS);
    if (fs->write_block(allocated_dnode / 8 / DZFS_BLOCK_SIZE + 1 + 1, block)) {
        allocated_dnode = 0;
        goto end;
    }

end:
    fs->free_mem_block(block);
    return allocated_dnode;
}

/**
 * Gets the block index which a pointer is pointing to. If the block does not exists
 * it will try to allocate a new block on disk and return the new block number.
 * @param fs The filesystem
 * @param from The block index to check
 * @return The block index if successful or 0 if disk is full
 */
static uint32_t get_or_allocate_block(struct dzFS *fs, uint32_t *from) {
    uint32_t content_block = *from;
    if (content_block == 0) {
        content_block = block_alloc(fs);
        if (content_block == 0)
            return 0;
        *from = content_block;
    }
    return content_block;
}

/**
 * Frees an allocated block
 * @param dnode The dnode or block number
 */
static void block_free(struct dzFS *fs, uint32_t dnode) {
    union dzFSBlock *block = fs->allocate_mem_block();
    if (fs->read_block(dnode / 8 / DZFS_BLOCK_SIZE + 1 + 1, block))
        goto end;

    bitmap_set(&block->bitmap, dnode % DZFS_BITSET_COVERED_BLOCKS);
    fs->write_block(dnode / 8 / DZFS_BLOCK_SIZE + 1 + 1, block);

end:
    fs->free_mem_block(block);
}

/**
 * Look for a content in this folder by the given name
 * @param fs The file system to search in
 * @param dir The given folder to search in
 * @param name The name of the file/folder to search
 * @return 0 if not found, the dnode of the block if found
 */
static uint32_t
folder_lookup_name(struct dzFS *fs, const struct dzFSDirectoryBlock *dir, const char *name, size_t name_len) {
    // Allocate block for dnodes
    union dzFSBlock *temp_dnode = fs->allocate_mem_block();
    uint32_t result = 0;
    for (int i = 0; i < DZFS_MAX_DIR_CONTENTS; i++) {
        if (dir->content_dnodes[i] == 0) // File/Folder not found
            break;
        // Read the dnode
        if (fs->read_block(dir->content_dnodes[i], temp_dnode) != 0)
            break; // IO Error
        // Compare filenames
        if (memcmp(temp_dnode->header.name, name, name_len) == 0 &&
            temp_dnode->header.name[name_len] == '\0') {
            // Matched!
            result = dir->content_dnodes[i];
            break;
        }
        // Continue searching...
    }
    // Deallocate
    fs->free_mem_block(temp_dnode);
    return result;
}

/**
 * Counts the number of files or folders inside a folder
 * @param dir The folder to count
 * @return The number of files or folder in the given directory
 */
static uint32_t folder_content_count(const struct dzFSDirectoryBlock *dir) {
    uint32_t count;
    for (count = 0; count < DZFS_MAX_DIR_CONTENTS; count++)
        if (dir->content_dnodes[count] == 0)
            return count;
    return count; // dir is full!
}

/**
 * Removes a file dnode from folder content. This function does not free the dnode
 * or do anything with the file. It just removes it from the content_dnodes list.
 * @param dir The directory to perform the action on
 * @param target_dnode The target dnode to remove from the directory
 * @return 0 if target is removed, 1 if the target is not found
 */
static int folder_remove_content(struct dzFSDirectoryBlock *dir, uint32_t target_dnode) {
    int dnode_index = -1;
    for (int i = 0; i < DZFS_MAX_DIR_CONTENTS; i++)
        if (dir->content_dnodes[i] == target_dnode) {
            dnode_index = i;
            break;
        }
    if (dnode_index == -1)
        return 1; // what?
    int last_index = (int) (folder_content_count(dir) - 1);
    if (last_index == 0) {
        // only one content so remove it
        dir->content_dnodes[0] = 0;
    } else if (last_index == dnode_index) {
        // dnode is last. Replace it
        dir->content_dnodes[last_index] = 0;
    } else {
        // perform a swap
        dir->content_dnodes[dnode_index] = dir->content_dnodes[last_index];
        dir->content_dnodes[last_index] = 0;
    }
    return 0;
}

/**
 * Counts the number of 1 bits in a number. Will either use the builtin
 * instruction, or the gcc builtin function or a simple implementation.
 * @param a The number to count the number of bits
 * @return Number of bits that has been one in the number.
 */
static uint32_t popcount(uint32_t a) {
#ifdef __DZOS__
    uint32_t c = 0;
    for (; a; ++c)
        a &= a - 1;
    return c;
#else
    return __builtin_popcount(a);
#endif
}

/**
 * Checks if a string has a prefix or not.
 * @param str The string to check if it has a prefix or not.
 * @param pre The prefix to see if string has
 * @return True if it has the prefix otherwise false
 */
bool string_prefix(const char *str, const char *pre) {
    return strncmp(pre, str, strlen(pre)) == 0;
}

int dzfs_new(struct dzFS *fs) {
    int result = DZFS_OK;
    // Check if all functions exists
    if (fs->allocate_mem_block == NULL || fs->free_mem_block == NULL || fs->write_block == NULL ||
        fs->read_block == NULL || fs->current_date == NULL || fs->total_blocks == NULL)
        return DZFS_ERR_ARGUMENT;
    // Overwrite the superblock
    union dzFSBlock *block = fs->allocate_mem_block();
    block->superblock = (struct dzFSSuperblock){
        .magic = {0}, // fill later
        .version = DZFS_VERSION,
        .blocks = fs->total_blocks(),
    };
    memcpy(block->superblock.magic, DZFS_MAGIC, sizeof(block->superblock.magic));
    // Check if the blocks on the disk is enough
    // We need 4 blocks at least:
    // 1. Bootloader
    // 2. Superblock
    // 3. Free bitmap
    // 4. Root directory
    if (block->superblock.blocks <= 4) {
        result = DZFS_ERR_TOO_SMALL;
        goto end;
    }
    fs->superblock = block->superblock;
    // Write the superblock
    TRY_IO(fs->write_block(SUPERBLOCK_DNODE, block))
    // Calculate the free bitmap size
    fs->free_bitmap_blocks =
            (block->superblock.blocks + DZFS_BITSET_COVERED_BLOCKS - 1) / DZFS_BITSET_COVERED_BLOCKS;
    if (block->superblock.blocks <= 3 + fs->free_bitmap_blocks) {
        result = DZFS_ERR_TOO_SMALL;
        goto end;
    }
    // bootloader + superblock
    fs->root_dnode = 1 + 1 + fs->free_bitmap_blocks;
    // Set the free bitmaps to all one in non-last and first pages
    memset(block->bitmap.bitmap, 0xFF, sizeof(block->bitmap.bitmap));
    // Write to disk
    for (uint32_t i = 1; i < fs->free_bitmap_blocks - 1; i++)
        TRY_IO(fs->write_block(1 + 1 + i, block))
    // Set the first free block
    // Note: Because at last we are going to use 32 free blocks
    // this works fine
    for (uint32_t i = 0; i < fs->free_bitmap_blocks + 3; i++)
        bitmap_clear(&block->bitmap, i);
    TRY_IO(fs->write_block(2, block))
    // If the last block is different from the first block, zero the block
    if (fs->free_bitmap_blocks != 1)
        memset(block->bitmap.bitmap, 0xFF, sizeof(block->bitmap.bitmap));

    for (uint32_t last_block_id = DZFS_BITSET_COVERED_BLOCKS * fs->free_bitmap_blocks - 1;
         last_block_id >= fs->superblock.blocks;
         last_block_id--)
        bitmap_clear(&block->bitmap, last_block_id);
    TRY_IO(fs->write_block(2 + fs->free_bitmap_blocks - 1, block))
    // Create the root directory
    block->folder = (struct dzFSDirectoryBlock){
        .header = (struct dzFSDnodeHeader){
            .type = DZFS_ENTITY_FOLDER,
            .name = "/",
            .creation_date = fs->current_date(),
        },
        .parent = fs->root_dnode,
        .content_dnodes = {0},
    };
    TRY_IO(fs->write_block(fs->root_dnode, block))

end:
    fs->free_mem_block(block);
    return result;
}

int dzfs_init(struct dzFS *fs) {
    int result = DZFS_OK;
    // Check if all functions exists
    if (fs->allocate_mem_block == NULL || fs->free_mem_block == NULL || fs->write_block == NULL ||
        fs->read_block == NULL || fs->current_date == NULL)
        return DZFS_ERR_ARGUMENT;
    // Check for superblock
    union dzFSBlock *block = fs->allocate_mem_block();
    TRY_IO(fs->read_block(SUPERBLOCK_DNODE, block))
    if (memcmp(block->superblock.magic, DZFS_MAGIC, sizeof(block->superblock.magic)) != 0) {
        result = DZFS_ERR_INIT_INVALID_FS;
        goto end;
    }
    fs->superblock = block->superblock;
    // Calculate the root dnode index
    fs->free_bitmap_blocks =
            (block->superblock.blocks + DZFS_BITSET_COVERED_BLOCKS - 1) / DZFS_BITSET_COVERED_BLOCKS;
    fs->root_dnode = 1 + 1 + fs->free_bitmap_blocks;

end:
    fs->free_mem_block(block);
    return result;
}

int dzfs_open_absolute(struct dzFS *fs, const char *path, uint32_t *dnode, uint32_t *parent_dnode, uint32_t flags) {
    if (path[0] != '/') // paths must be absolute
        return DZFS_ERR_ARGUMENT;
    if (strcmp(path, "/") == 0) {
        // Asking for root. fuck the flags
        *dnode = fs->root_dnode;
        *parent_dnode = fs->root_dnode;
        return DZFS_OK;
    }
    path++; // skip the /
    return dzfs_open_relative(fs, path, fs->root_dnode, dnode, parent_dnode, flags);
}

int dzfs_open_relative(struct dzFS *fs, const char *path, uint32_t relative_to, uint32_t *dnode,
                         uint32_t *parent_dnode, uint32_t flags) {
    // Is this an absolute path?
    if (path[0] == '/') // just call dzfs_open_absolute
        return dzfs_open_absolute(fs, path, dnode, parent_dnode, flags);
    // Sanity check the relative to argument to not be zero
    if (relative_to == 0)
        return DZFS_ERR_ARGUMENT;

    *parent_dnode = relative_to;
    int result = DZFS_OK;
    union dzFSBlock *current_dnode = fs->allocate_mem_block(),
            *temp_dnode = fs->allocate_mem_block();
    // Relative to must be folder
    TRY_IO(fs->read_block(relative_to, current_dnode))
    // Check . and ..
    while (1) {
        // Is this pointing to the current directory?
        if (string_prefix(path, "./")) {
            // We are looking in current directory so just skip the ./
            path += 2;
            continue;
        }
        if (string_prefix(path, "../")) {
            // Move one directory up
            TRY_IO(fs->read_block(relative_to, current_dnode))
            // Are we at root?
            if (current_dnode->folder.parent != 0) {
                // Not yet, go up
                relative_to = current_dnode->folder.parent;
            }
            path += 3;
            continue;
        }
        // Last .. in the path. Just return the dnode of the folder above
        if (strcmp(path, "..") == 0) {
            // Move one directory up
            TRY_IO(fs->read_block(relative_to, current_dnode))
            // Are we at root?
            if (current_dnode->folder.parent != 0) {
                // Not yet, go up
                relative_to = current_dnode->folder.parent;
            }
            path += 2;
            break; // end of path
        }
        // None of above, bail out
        break;
    }

    // Is the path empty? This means that we should return the current relative to as the dnode
    if (path[0] == '\0' || strcmp(path, ".") == 0) {
        *dnode = relative_to;
        TRY_IO(fs->read_block(relative_to, current_dnode))
        *parent_dnode = current_dnode->folder.parent;
        goto end;
    }

    // Traverse the file system
    uint32_t current_dnode_index = relative_to;
    TRY_IO(fs->read_block(current_dnode_index, current_dnode))
    // Traverse the file system
    while (true) {
        size_t next_path_size = path_next_part_len(path);
        // Search for this file in the directory
        uint32_t dnode_search_result = 0;
        for (int i = 0; i < DZFS_MAX_DIR_CONTENTS; i++) {
            if (current_dnode->folder.content_dnodes[i] == 0) // File/Folder not found
                break;
            // Compare filenames
            TRY_IO(fs->read_block(current_dnode->folder.content_dnodes[i], temp_dnode))
            if (memcmp(temp_dnode->header.name, path, next_path_size) == 0 &&
                temp_dnode->header.name[next_path_size] == '\0') {
                // Matched!
                dnode_search_result = current_dnode->folder.content_dnodes[i];
                break;
            }
            // Continue searching...
        }
        // There are some ways this can go...
        if ((flags & DZFS_O_CREATE) && dnode_search_result == 0) {
            // File not found
            if (path_last_part(path)) {
                // Create the file/folder
                // Does the parent directory has empty slots?
                size_t folder_size = folder_content_count(&current_dnode->folder);
                if (folder_size == DZFS_MAX_DIR_CONTENTS) {
                    result = DZFS_ERR_LIMIT;
                    goto end;
                }
                // Allocate dnode
                *dnode = block_alloc(fs);
                if (*dnode == 0) {
                    result = DZFS_ERR_FULL;
                    goto end;
                }
                current_dnode->folder.content_dnodes[folder_size] = *dnode;
                *parent_dnode = current_dnode_index;
                // Create the dnode
                memset(temp_dnode, 0, sizeof(*temp_dnode));
                temp_dnode->header.creation_date = fs->current_date();
                memcpy(temp_dnode->header.name, path, next_path_size);
                temp_dnode->header.name[next_path_size] = '\0';
                if (flags & DZFS_O_DIR) {
                    temp_dnode->header.type = DZFS_ENTITY_FOLDER;
                    temp_dnode->folder.parent = *parent_dnode;
                } else {
                    temp_dnode->header.type = DZFS_ENTITY_FILE;
                }
                // Write to disk
                TRY_IO(fs->write_block(*parent_dnode, current_dnode))
                TRY_IO(fs->write_block(*dnode, temp_dnode))
                break;
            }
            result = DZFS_ERR_NOT_FOUND;
            goto end;
        } else if (!(flags & DZFS_O_CREATE) && dnode_search_result == 0) {
            // File not found
            result = DZFS_ERR_NOT_FOUND;
            goto end;
        }
        // We have found something
        if (path_last_part(path)) {
            // Is it the thing?
            *dnode = dnode_search_result;
            *parent_dnode = current_dnode_index;
            break;
        } else {
            // Traverse more into the directories...
            TRY_IO(fs->read_block(dnode_search_result, current_dnode))
            if (current_dnode->header.type != DZFS_ENTITY_FOLDER) {
                // We found a file instead of a folder...
                result = DZFS_ERR_NOT_FOUND;
                goto end;
            }
            current_dnode_index = dnode_search_result;
            path += next_path_size + 1; // skip the current directory
        }
    }

end:
    fs->free_mem_block(current_dnode);
    fs->free_mem_block(temp_dnode);
    return result;
}

int dzfs_write(struct dzFS *fs, uint32_t dnode, const char *data, size_t size, size_t offset) {
    int result = DZFS_OK;
    // Read the dnode block at first
    union dzFSBlock *dnode_block = fs->allocate_mem_block(),
            *data_block = fs->allocate_mem_block(),
            *indirect_block = fs->allocate_mem_block();
    TRY_IO(fs->read_block(dnode, dnode_block))
    if (dnode_block->header.type != DZFS_ENTITY_FILE) {
        // this is a file right?
        result = DZFS_ERR_ARGUMENT;
        goto end;
    }
    // Will we pass the size limit of files?
    if (size + offset > DZFS_MAX_FILESIZE) {
        result = DZFS_ERR_LIMIT;
        goto end;
    }
    // TODO: File growing. Allocate empty dnodes
    if (offset > dnode_block->file.size) {
        result = DZFS_ERR_ARGUMENT;
        goto end;
    }
    // Read the indirect block list as well
    if (dnode_block->file.indirect_block != 0)
        TRY_IO(fs->read_block(dnode_block->file.indirect_block, indirect_block))
    // Copy to disk
    size_t to_write_bytes = size;
    while (to_write_bytes > 0) {
        size_t content_block_index = offset / DZFS_BLOCK_SIZE;
        size_t raw_data_index = offset % DZFS_BLOCK_SIZE;
        uint32_t content_block;
        if (content_block_index >= DZFS_DIRECT_BLOCKS) {
            // Is indirect block available?
            if (dnode_block->file.indirect_block == 0) {
                dnode_block->file.indirect_block = block_alloc(fs);
                if (dnode_block->file.indirect_block == 0) {
                    result = DZFS_ERR_FULL;
                    goto end;
                }
            }
            // Get from indirect block
            content_block = get_or_allocate_block(fs, &indirect_block->indirect_block[content_block_index -
                                                      DZFS_DIRECT_BLOCKS]);
            if (content_block == 0) {
                result = DZFS_ERR_FULL;
                goto end;
            }
        } else {
            content_block = get_or_allocate_block(fs, &dnode_block->file.direct_blocks[content_block_index]);
            if (content_block == 0) {
                result = DZFS_ERR_FULL;
                goto end;
            }
        }
        // We might need to partially write to a block. For this, we must issue a
        // read and then issue a write to disk.
        if (offset > 0)
            TRY_IO(fs->read_block(content_block, data_block))
        size_t to_copy = MIN(DZFS_BLOCK_SIZE - raw_data_index, to_write_bytes);
        memcpy(data_block->raw_data + raw_data_index, data, to_copy);
        TRY_IO(fs->write_block(content_block, data_block))
        data += to_copy;
        to_write_bytes -= to_copy;
        offset += to_copy;
    }
    // Update dnode and indirect blocks
    if (dnode_block->file.indirect_block != 0)
        TRY_IO(fs->write_block(dnode_block->file.indirect_block, indirect_block))
    dnode_block->file.size += size;
    TRY_IO(fs->write_block(dnode, dnode_block))

end:
    fs->free_mem_block(dnode_block);
    fs->free_mem_block(data_block);
    fs->free_mem_block(indirect_block);
    return result;
}

int dzfs_read(struct dzFS *fs, uint32_t dnode, char *buf, size_t size, size_t offset) {
    int result = DZFS_OK, read_bytes = 0;
    // Read the dnode
    union dzFSBlock *dnode_block = fs->allocate_mem_block(),
            *data_block = fs->allocate_mem_block(),
            *indirect_block = fs->allocate_mem_block();
    TRY_IO(fs->read_block(dnode, dnode_block))
    if (dnode_block->header.type != DZFS_ENTITY_FILE) {
        // this is a file right?
        result = DZFS_ERR_ARGUMENT;
        goto end;
    }
    if (dnode_block->file.indirect_block != 0)
        TRY_IO(fs->read_block(dnode_block->file.indirect_block, indirect_block))
    if (offset >= dnode_block->file.size) // nothing to read...
        goto end;
    int to_read_bytes = MIN(dnode_block->file.size - offset, size);
    // Read the corresponding data blocks
    while (to_read_bytes > 0) {
        size_t content_block_index = offset / DZFS_BLOCK_SIZE;
        size_t raw_data_index = offset % DZFS_BLOCK_SIZE;
        uint32_t content_block;
        if (content_block_index >= DZFS_DIRECT_BLOCKS)
            content_block = indirect_block->indirect_block[content_block_index - DZFS_DIRECT_BLOCKS];
        else
            content_block = dnode_block->file.direct_blocks[content_block_index];
        TRY_IO(fs->read_block(content_block, data_block))
        int to_copy = MIN((int) (DZFS_BLOCK_SIZE - raw_data_index), to_read_bytes);
        memcpy(buf, data_block->raw_data + raw_data_index, to_copy);
        buf += to_copy;
        to_read_bytes -= to_copy;
        offset += to_copy;
        read_bytes += to_copy;
    }

end:
    fs->free_mem_block(dnode_block);
    fs->free_mem_block(data_block);
    fs->free_mem_block(indirect_block);
    if (result == DZFS_OK)
        return read_bytes;
    else
        return result;
}

int dzfs_read_dir(struct dzFS *fs, uint32_t dnode, struct dzFSStat *stat, size_t offset) {
    int result = DZFS_OK;
    // Read the dnode block at first
    union dzFSBlock *dnode_block = fs->allocate_mem_block();
    TRY_IO(fs->read_block(dnode, dnode_block))
    if (dnode_block->header.type != DZFS_ENTITY_FOLDER) {
        // this is a folder right?
        result = DZFS_ERR_ARGUMENT;
        goto end;
    }
    // Is the offset out of the bonds?
    if (offset >= DZFS_MAX_DIR_CONTENTS) {
        result = DZFS_ERR_LIMIT;
        goto end;
    }
    uint32_t requested_dnode = dnode_block->folder.content_dnodes[offset];
    if (requested_dnode == 0) {
        result = DZFS_ERR_LIMIT;
        goto end;
    }
    // Get the stats of the dnode
    result = dzfs_stat(fs, requested_dnode, stat);

end:
    fs->free_mem_block(dnode_block);
    return result;
}

int dzfs_delete(struct dzFS *fs, uint32_t dnode, uint32_t parent_dnode) {
    int result = DZFS_OK;
    if (dnode == fs->root_dnode) // Bruh
        return DZFS_ERR_ARGUMENT;
    // Read the dnode block at first
    union dzFSBlock *dnode_block = fs->allocate_mem_block(),
            *indirect_block = fs->allocate_mem_block();
    TRY_IO(fs->read_block(dnode, dnode_block))
    // What is this entity?
    switch (dnode_block->header.type) {
        case DZFS_ENTITY_FILE:
            // Delete each indirect block of file
            if (dnode_block->file.indirect_block != 0) {
                TRY_IO(fs->read_block(dnode_block->file.indirect_block, indirect_block))
                for (size_t i = 0; i < DZFS_INDIRECT_BLOCK_COUNT && indirect_block->indirect_block[i] != 0; i++)
                    block_free(fs, indirect_block->indirect_block[i]);
            }
        // Delete direct blocks
            for (int i = 0; i < DZFS_DIRECT_BLOCKS && dnode_block->file.direct_blocks[i] != 0; i++)
                block_free(fs, dnode_block->file.direct_blocks[i]);
            break;
        case DZFS_ENTITY_FOLDER:
            // Is the folder emtpy?
            for (int i = 0; i < DZFS_MAX_DIR_CONTENTS; i++)
                if (dnode_block->folder.content_dnodes[i] != 0) {
                    result = DZFS_ERR_NOT_EMPTY;
                    goto end;
                }
            break;
        default:
            result = DZFS_ERR_ARGUMENT;
            goto end;
    }
    // Delete in parent as well
    TRY_IO(fs->read_block(parent_dnode, dnode_block))
    if (dnode_block->header.type != DZFS_ENTITY_FOLDER) {
        result = DZFS_ERR_ARGUMENT;
        goto end;
    }
    if (folder_remove_content(&dnode_block->folder, dnode) != 0) {
        result = DZFS_ERR_ARGUMENT; // child does not exist in parent
        goto end;
    }
    TRY_IO(fs->write_block(parent_dnode, dnode_block))

    // Delete this dnode/block as well
    block_free(fs, dnode);

end:
    fs->free_mem_block(dnode_block);
    fs->free_mem_block(indirect_block);
    return result;
}

int dzfs_stat(struct dzFS *fs, uint32_t dnode, struct dzFSStat *stat) {
    int result = DZFS_OK;
    union dzFSBlock *dnode_block = fs->allocate_mem_block();
    TRY_IO(fs->read_block(dnode, dnode_block))
    // Read the header
    memset(stat, 0, sizeof(*stat));
    stat->type = dnode_block->header.type;
    memcpy(stat->name, dnode_block->header.name, sizeof(stat->name));
    stat->creation_date = dnode_block->file.header.creation_date;
    // Fill the size based on type
    switch (dnode_block->header.type) {
        case DZFS_ENTITY_FILE:
            stat->size = dnode_block->file.size;
            break;
        case DZFS_ENTITY_FOLDER:
            stat->parent = dnode_block->folder.parent;
            stat->size = folder_content_count(&dnode_block->folder);
            break;
        default:
            result = DZFS_ERR_ARGUMENT;
            goto end;
    }

end:
    stat->dnode = dnode;
    fs->free_mem_block(dnode_block);
    return result;
}

int dzfs_move(struct dzFS *fs, uint32_t dnode, uint32_t old_parent, uint32_t new_parent, const char *new_name) {
    int result = DZFS_OK;
    if (old_parent == new_parent && new_name == NULL) // no clue why would someone do this
        return DZFS_OK;
    union dzFSBlock *dnode_block = fs->allocate_mem_block(),
            *file_dnode = fs->allocate_mem_block();
    TRY_IO(fs->read_block(dnode, file_dnode))
    // Check same dest and source filename
    if (old_parent == new_parent && strcmp(new_name, file_dnode->header.name) == 0) // do nothing
        goto end;
    // Read the parent and do some sanity checks
    TRY_IO(fs->read_block(new_parent, dnode_block))
    if (dnode_block->header.type != DZFS_ENTITY_FOLDER) {
        result = DZFS_ERR_ARGUMENT;
        goto end;
    }
    // Change the name in the memory (nothing in the disk yet)
    if (new_name != NULL) {
        if (strlen(new_name) > DZFS_MAX_FILENAME) {
            result = DZFS_ERR_LIMIT;
            goto end;
        }
        strcpy(file_dnode->header.name, new_name);
    }
    // Replace the old file if needed (which is just a delete function)
    // NOTE: Because of the first check in this function, we cannot replace the file itself
    uint32_t to_delete_dnode = folder_lookup_name(fs, &dnode_block->folder, file_dnode->header.name,
                                                  strlen(file_dnode->header.name));
    if (to_delete_dnode != 0) {
        int delete_result = dzfs_delete(fs, to_delete_dnode, new_parent);
        if (delete_result != DZFS_OK) {
            result = delete_result;
            goto end;
        }
        TRY_IO(fs->read_block(new_parent, dnode_block))
    }
    // Add the file to directory
    uint32_t new_dnode_index = folder_content_count(&dnode_block->folder);
    if (new_dnode_index == DZFS_MAX_DIR_CONTENTS) {
        result = DZFS_ERR_LIMIT; // we have reached the maximum nodes for this directory
        goto end;
    }
    dnode_block->folder.content_dnodes[new_dnode_index] = dnode;
    TRY_IO(fs->write_block(new_parent, dnode_block))
    // Remove from old parent
    TRY_IO(fs->read_block(old_parent, dnode_block))
    if (dnode_block->header.type != DZFS_ENTITY_FOLDER) {
        result = DZFS_ERR_ARGUMENT;
        goto end;
    }
    if (folder_remove_content(&dnode_block->folder, dnode) != 0) {
        result = DZFS_ERR_ARGUMENT; // child does not exist in parent
        goto end;
    }
    TRY_IO(fs->write_block(old_parent, dnode_block))
    // Was this also a rename?
    if (new_name != NULL)
        TRY_IO(fs->write_block(dnode, file_dnode))

end:
    fs->free_mem_block(dnode_block);
    fs->free_mem_block(file_dnode);
    return result;
}

uint32_t dzfs_free_blocks(struct dzFS *fs) {
    uint32_t free_blocks = 0;
    union dzFSBlock *bitmap = fs->allocate_mem_block();
    for (uint32_t block = 0; block < fs->free_bitmap_blocks; block++) {
        if (fs->read_block(block + 2, bitmap) != 0)
            continue; // just skip this block
        for (size_t i = 0; i < sizeof(bitmap->bitmap.bitmap) / sizeof(bitmap->bitmap.bitmap[0]); i++)
            free_blocks += popcount(bitmap->bitmap.bitmap[i]);
    }
    fs->free_mem_block(bitmap);
    return free_blocks;
}