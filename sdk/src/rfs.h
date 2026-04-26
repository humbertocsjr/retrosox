#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#pragma pack(1)


typedef struct
{
    uint16_t year; // 000-001: Year (e.g., 2024)
    uint8_t month; // 002: Month (1-12)
    uint8_t day; // 003: Day (1-31)
    uint8_t hour; // 004: Hour (0-23)
    uint8_t minute; // 005: Minute (0-59)
    uint8_t second; // 006: Second (0-59)
    uint8_t timezone_offset; // 007: Timezone offset from UTC in hours (-12 to +14)
} rfs_datetime_t;

typedef enum rfs_modes
{
    RFS_MODE_FILE = 01000, // Regular file
    RFS_MODE_DIRECTORY = 02000, // Directory
    RFS_MODE_SYMLINK = 04000,    // Symbolic Link
    RFS_MODE_OTHER_READ = 00001, // Read permission
    RFS_MODE_OTHER_WRITE = 00002, // Write permission
    RFS_MODE_OTHER_EXECUTE = 00004, // Execute permission
    RFS_MODE_OTHER_RWX = 00007, // Read, write, and execute permissions for others
    RFS_MODE_GROUP_READ = 00010, // Read permission for group
    RFS_MODE_GROUP_WRITE = 00020, // Write permission for group
    RFS_MODE_GROUP_EXECUTE = 00040, // Execute permission for group
    RFS_MODE_GROUP_RWX = 00070, // Read, write, and execute permissions for group
    RFS_MODE_OWNER_READ = 00100, // Read permission for owner
    RFS_MODE_OWNER_WRITE = 00200, // Write permission for owner
    RFS_MODE_OWNER_EXECUTE = 00400, // Execute permission for owner  
    RFS_MODE_OWNER_RWX = 00700, // Read, write, and execute permissions for owner
    RFS_MODE_ALL_RWX = 00777 // Read, write, and execute permissions for owner, group, and others
} rfs_modes_t;

typedef struct
{
    uint16_t mode; // 000-001: File mode (e.g., regular file, directory, etc.) and Permissions (e.g., read/write/execute for owner/group/others)
    char name[32]; // 002-033: Null-terminated filename (max 31 characters + null terminator)
    uint16_t starting_block; // 034-035: Starting block of the file's data
    uint32_t file_size; // 036-039: Size of the file in bytes
    rfs_datetime_t creation_time; // 040-047: File creation timestamp
    rfs_datetime_t modification_time; // 048-055: File modification timestamp
    uint16_t user_id; // 056-057: User ID of the file owner
    uint16_t group_id; // 058-059: Group ID of the file owner
    uint8_t reserved[4]; // 060-063: Reserved for future use
} rfs_directory_entry_t;

enum
{
    RFS_BLOCK_SIZE = 512, // Block size in bytes
    RFS_DIRECTORY_ENTRIES_PER_BLOCK = RFS_BLOCK_SIZE / sizeof(rfs_directory_entry_t), // Number of directory entries that can fit in a block
    RFS_POINTERS_PER_BLOCK = RFS_BLOCK_SIZE / sizeof(uint16_t), // Number of block pointers that can fit in a block
    RFS_MAX_NAME_LENGTH = 31, // Maximum filename length (excluding null terminator)
    RFS_LAYOUT_BOOTSECTOR_ADDRESS = 0, // Address of the boot sector (index block)
    RFS_LAYOUT_INDEX_BLOCK_ADDRESS = 9, // Address of the index block (same as boot sector)
    RFS_RESERVED_POINTER = 0xFFFF, // Special value indicating an unused block pointer
    RFS_LAST_BLOCK_POINTER = 0xFFFE, // Special value indicating the last block in a file
    RFS_FREE_POINTER = 0x0000 // Special value indicating a free block pointer
};


typedef struct 
{
    char signature[4]; // 000-003: "RFS1"
    uint16_t total_blocks; // 004-005: Total number of blocks in the filesystem
    uint16_t block_map_start; // 006-007: Starting block of the block map
    uint16_t block_map_blocks; // 008-009: Number of blocks used by the block map
    uint16_t block_map_end; // 010-011: Ending block of the block map
    uint16_t root_dir_start; // 012-013: Starting block of the root directory
    uint16_t geometry_cylinders; // 014-015: Number of cylinders in the disk geometry
    uint16_t geometry_heads; // 016-017: Number of heads in the disk geometry
    uint16_t geometry_sectors; // 018-019: Number of sectors per track in the disk geometry
    uint16_t geometry_sectors_per_cylinder; // 020-021: Number of sectors per cylinder in the disk geometry
    uint8_t reserved[490]; // 022-511: Reserved for future use
} rfs_index_block_t;

typedef struct
{
    uint16_t pointers[RFS_POINTERS_PER_BLOCK]; // Array of block pointers (each pointer is 2 bytes, so we can fit 256 pointers in a 512-byte block)
} rfs_block_map_block_t;

typedef struct
{
    rfs_directory_entry_t entries[RFS_DIRECTORY_ENTRIES_PER_BLOCK]; // Array of directory entries that can fit in a block
} rfs_directory_block_t;

typedef struct
{
    rfs_directory_entry_t entry; // The directory entry for this file or directory
    uint16_t address; // The block address of the directory entry (used for updating the entry when adding files)
    uint16_t entry_index; // The index of the directory entry within its block (used for updating the entry when adding files)
} rfs_directory_entry_with_address_t;

#pragma pack()