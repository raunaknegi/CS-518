
/*
 *  Copyright (C) 2023 CS416 Rutgers CS
 *	Tiny File System
 *	File:	rufs.c
 *
 */

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/time.h>
#include <libgen.h>
#include <limits.h>

#include "block.h"
#include "rufs.h"
#define INODE_BITMAP_BLKNO 1
#define DB_BITMAP_BLKNO 2
#define INODE_BLOCK_START 3
#define DATA_BLOCK_START 67

char diskfile_path[PATH_MAX]; 

// Declare your in-memory data structures here
typedef unsigned char* bitmap_buffer[BLOCK_SIZE];

static struct superblock super_block; 	/* The in-memory superblock (initialized in rufs_init) */
unsigned char* super_block_buffer[BLOCK_SIZE];
bitmap_buffer inode_bitmap_buffer[BLOCK_SIZE];
bitmap_buffer db_bitmap_buffer[BLOCK_SIZE];


// Other utility variables and data structures
#define TYPE_FILE 0		/* Indicates inode type for files */
#define TYPE_DIR 1		/* Indicates inode type for directories */

#define TESTDIR "/"

/* 
 * Get available inode number from bitmap
 */
int get_avail_ino() {
    // Step 1: Read inode bitmap from disk
    int retstat = bio_read(INODE_BITMAP_BLKNO, inode_bitmap_buffer);
    if (retstat < 0) {
        perror("inode bitmap block NOT read from disk in {get_avail_ino}");
        return retstat;
    }

    // Step 2: Traverse inode bitmap to find an available slot
    int i = 0;
    while (i < MAX_INUM && get_bitmap(inode_bitmap_buffer, i) != 0) {
        i++;
    }

    if (i == MAX_INUM) // No inodes available
        return -ENOSPC;

    // Step 3: Update inode bitmap and write to disk
    set_bitmap(inode_bitmap_buffer, i);
    retstat = bio_write(super_block.i_bitmap_blk, inode_bitmap_buffer);
    if (retstat < 0) {
        perror("inode bitmap block FAILED to write to disk in {get_avail_ino}");
        return retstat;
    }

    // Instead of zero, return the inode number
    return i;
}

/* 
 * Get available data block number from bitmap
 */
int get_avail_blkno() {
    // Step 1: Read data block bitmap from disk
    int retstat = bio_read(super_block.d_bitmap_blk, db_bitmap_buffer);
    if (retstat < 0) {
        perror("db_bitmap_block NOT read from disk in {get_avail_blkno}");
        return retstat;
    }

    // Step 2: Traverse data block bitmap to find an available slot
    int i = 0;
    while (i < MAX_DNUM && get_bitmap(db_bitmap_buffer, i) != 0) {
        i++;
    }

    if (i == MAX_DNUM) // No data blocks available
        return -ENOSPC;

    // Step 3: Update data block bitmap and write to disk
    set_bitmap(db_bitmap_buffer, i);
    retstat = bio_write(super_block.d_bitmap_blk, db_bitmap_buffer); // Write the entire block

    if (retstat < 0) {
        perror("db_bitmap_block NOT updated in disk in {get_avail_blkno}");
        return retstat;
    }

    return i;
}

/* 
 * inode operations
 */
int readi(uint16_t ino, struct inode *inode) {
    // printf("In readi \n");

    // Step 1: Get the inode's on-disk block number
    uint16_t inode_total_size = ino * sizeof(struct inode);
    uint16_t blk_num = super_block.i_start_blk + (inode_total_size / BLOCK_SIZE);

    // Step 2: Get offset of the inode in the inode on-disk block
    uint16_t offset = inode_total_size % BLOCK_SIZE;

    // Step 3: Read the block from disk and then copy into inode structure
    static char db_buffer[BLOCK_SIZE]; // Use static allocation
    memset(db_buffer, 0, BLOCK_SIZE); // Initialize buffer to zeros
    int retstat = bio_read(blk_num, db_buffer);
    if (retstat < 0) {
        perror("inode_number NOT read from disk (writei) \n");
        return retstat;
    }

    // Copy only the inode data from the read buffer
    memcpy(inode, db_buffer + offset, sizeof(struct inode));

    return 0;
}


int writei(uint16_t ino, struct inode *inode) {
    // Step 1: Get the block number where this inode resides on disk
    uint16_t inode_total_size = ino * sizeof(struct inode);
    uint16_t blk_num = super_block.i_start_blk + (inode_total_size / BLOCK_SIZE);

    // Step 2: Get offset of the inode in the inode on-disk block
    uint16_t offset = inode_total_size % BLOCK_SIZE;

    // Step 2: Read the block from disk where the inode is located
    static char db_buffer[BLOCK_SIZE]; // Use static allocation
    memset(db_buffer, 0, BLOCK_SIZE); // Initialize buffer to zeros

    // Create an in-memory copy (i.e., data_block_buffer) of the data block that has the inode
    int retstat = bio_read(blk_num, db_buffer);
    if (retstat < 0) {
        perror("inode_number NOT updated on disk(writei)\n");
        return retstat;
    }

    // Update the in-memory copy (i.e., data_block_buffer) => update the particular inode stored in this data block
    int i = 0;
    while (i < sizeof(struct inode)) {
        db_buffer[offset + i] = ((char *)inode)[i];
        i++;
    }

    // Step 3: Write inode to disk
    // basically write the updated in-memory copy of the data block to disk
    retstat = bio_write(blk_num, db_buffer);
    if (retstat < 0) {
        return retstat;
    }

    return 0;
}

/* 
 * directory operations
 */
int dir_find(uint16_t ino, const char *fname, size_t name_len, struct dirent *dirent) {
    // printf("--------Find----- Parent Inode: %d, Target: %s \n",ino, fname);
    // Step 1: Call readi() to get the inode using ino
    struct inode cur_inode;
    int retstat = readi(ino, &cur_inode);
    
    if (retstat < 0) {
        return retstat;
    }

    // Step 2: Get ALL the data blocks of the current directory from the inode
    char curr_dir_block[BLOCK_SIZE];
    
    int block = 0;
    while (block < 16 && cur_inode.direct_ptr[block] != 0) { 
        // Read the directory's data block
        // printf("Current Directory block: %d \n", cur_inode.direct_ptr[block]);
        retstat = bio_read(cur_inode.direct_ptr[block], curr_dir_block);
        
        if (retstat < 0) {
            return retstat;
        }

        // Check each directory entry within the block
        int i = 0;
        while (i < BLOCK_SIZE / sizeof(struct dirent)) {
            struct dirent *cur_dirent = (struct dirent *)(curr_dir_block + (i * sizeof(struct dirent)));
            // printf("Current Directory Entry(%d): {%s} in the directory block: %d \n", i, cur_dirent->name, cur_inode.direct_ptr[block]);
            if ((cur_dirent->valid == 1) && (strncmp(cur_dirent->name, fname, name_len) == 0)) {
                memcpy(dirent, cur_dirent, sizeof(struct dirent));
                return 0; // Entry found
            }

            i++;
        }

        block++;
    }

    return -ENOENT; // Entry not found
}

int dir_add(struct inode dir_inode, uint16_t f_ino, const char *fname, size_t name_len) {
    // printf("--------Add----- Parent Inode: %d, Target inode: %d, Target: %s \n",dir_inode.ino, f_ino, fname);

    static char db_buffer[BLOCK_SIZE];
    static struct dirent new_dirent;
    memset(db_buffer, 0, BLOCK_SIZE);
    // Initialize new dirent
    new_dirent.ino = f_ino;
    new_dirent.valid = 1;
    strncpy(new_dirent.name, fname, name_len);
    new_dirent.len = name_len;

    // Check if directory name already exists
    int retstat = dir_find(dir_inode.ino, fname, name_len, &new_dirent);
    if (retstat != -ENOENT) {
        return retstat;
    } else if (retstat == 0) {
        return -EEXIST;
    }
    

    int found = 0;
    int block = 0;

    while (block < 16 && !found) {
        if (dir_inode.direct_ptr[block] == 0) {
            int new_blk = get_avail_blkno();
            if (new_blk < 0) {
                return new_blk;
            }
            retstat = bio_write(new_blk, db_buffer);
            if (retstat < 0) {
                return retstat;
            }

            dir_inode.direct_ptr[block] = new_blk;
            dir_inode.link++;
            memcpy(db_buffer, &new_dirent, sizeof(struct dirent));

            // Initialize remaining slots as empty
            struct dirent *cur_dirent;
            int d = 1;

            while (d < BLOCK_SIZE / sizeof(struct dirent)) {
                cur_dirent = (struct dirent *)(db_buffer + (d * sizeof(struct dirent)));
                cur_dirent->valid = 0;
                d++;
            }

            retstat = bio_write(new_blk, db_buffer);
            if (retstat < 0) {
                return retstat;
            }

            found = 1;
        } else {
            retstat = bio_read(dir_inode.direct_ptr[block], db_buffer);
            if (retstat < 0) {
                return retstat;
            }

            int i = 0;
            struct dirent *cur_dirent;

            while (i < BLOCK_SIZE / sizeof(struct inode) && !found) {
                cur_dirent = (struct dirent *)(db_buffer + (i * sizeof(struct dirent)));

                if (cur_dirent->valid == 0) {
                    dir_inode.link++;
                    memcpy(cur_dirent, &new_dirent, sizeof(struct dirent));
                    retstat = bio_write(dir_inode.direct_ptr[block], db_buffer);
                    if (retstat < 0) {
                        return retstat;
                    }
                    found = 1;
                    break;
                }

                i++;
            }
        }

        block++;
    }

    if (!found) {
        return -ENOSPC;
    }

    // Write updated dir_inode back to disk
    retstat = writei(dir_inode.ino, &dir_inode);
    if (retstat < 0) {
        return retstat;
    }

    return 0;
}


int dir_remove(struct inode dir_inode, const char *fname, size_t name_len) {

	// Step 1: Read dir_inode's data block and checks each directory entry of dir_inode
    int retstat = 0;
    char curr_dir_block[BLOCK_SIZE];
    
    int block = 0;
    while (block < 16 && dir_inode.direct_ptr[block] != 0) { 
        // Read the directory's data block
        retstat = bio_read(dir_inode.direct_ptr[block], curr_dir_block);
        if (retstat < 0) {
            return retstat;
        }

        // Check each directory entry within the block
        int i = 0;
        while (i < BLOCK_SIZE / sizeof(struct dirent)) {
            struct dirent *cur_dirent = (struct dirent *)(curr_dir_block + (i * sizeof(struct dirent)));
            // Step 2: Check if fname exist
            if ((cur_dirent->valid == 1) && (strncmp(cur_dirent->name, fname, name_len) == 0)) {
                // Step 3: If exist, then remove it from dir_inode's data block and write to disk
                cur_dirent->valid = 0;
                retstat = bio_write(dir_inode.direct_ptr[block], curr_dir_block);
                if (retstat < 0) {
                    return retstat;
                }
                return 0; 
            }

            i++;
        }

        block++;
    }
    return -ENOENT; // Entry not found
}

/* 
 * namei operation
 */
int get_node_by_path(const char *path, uint16_t ino, struct inode *inode) {
	// printf("In get_node_by_path Path: %s\n", path);

    char temp_path[PATH_MAX]; // Static allocation for path
    strncpy(temp_path, path, PATH_MAX - 1);
    temp_path[PATH_MAX - 1] = '\0'; // Ensure null termination
	// printf("temp_path: %s \n", temp_path);


    char *token;
    const char delim[2] = "/";
    int retstat = 0;

    // Base cases: Empty path or root directory
    if (strlen(temp_path) == 0 || strcmp(temp_path, "/") == 0) {
		// printf("Empty Path \n");=> ino of the root of the path is the is the final ino
        retstat = readi(ino, inode);
        if (retstat < 0) {
            return retstat;
        }
        return retstat;
    }

    // Iterative approach
    token = strtok(temp_path, delim);

    while (token != NULL) {
		// printf("Token: %s \n", temp_path);

        struct dirent matched_dirent;
        retstat = dir_find(ino, token, strlen(token), &matched_dirent);

        if (retstat == -ENOENT) {
            // printf("NO such named dirent in the given ino of root_dir in get_node_by_path \n");
            return -ENOENT;
        } else if (retstat < 0) {
            return retstat;
        }

        ino = matched_dirent.ino; // Update the inode number for the next iteration
        token = strtok(NULL, delim);
		// Ex. /foo/break => ino = 0 token = foo => matched_dirent
    }

    // Final readi call for the last token
    retstat = readi(ino, inode);
    if (retstat < 0) {
        return retstat;
    }

    return 0;
}



/* 
 * Make file system
 */
void init_superblock() {
    super_block.magic_num = MAGIC_NUM;
    super_block.max_inum = MAX_INUM;
    super_block.max_dnum = MAX_DNUM;
    super_block.i_bitmap_blk = INODE_BITMAP_BLKNO;
    super_block.d_bitmap_blk = DB_BITMAP_BLKNO;
    super_block.i_start_blk = INODE_BLOCK_START;
    super_block.d_start_blk = DATA_BLOCK_START;
}

int rufs_mkfs() {

	// Call dev_init() to initialize (Create) Diskfile

	dev_init(diskfile_path);

	// write superblock information

	// initialising => update the in-memory copy of super block
	init_superblock();

	// write the in-memory copy to disk 
	// Since the all operations on disk should be of BLOCK_SIZE and our in-memory copy of superblock is not that big we copy them in a buffer of size BLOCK_SIZE and then write to the disk and the desired block num location
    memset(super_block_buffer, 0, BLOCK_SIZE);
    memcpy(super_block_buffer, &super_block, sizeof(super_block));

	// write the super_block_buffer to disk at data block num 0
	if (bio_write(0, super_block_buffer) < 0) {
		perror("super_block_buffer NOT able to write to disk during initialisation(rufs_mkfs)");
		exit(EXIT_FAILURE);
	}

	// initialize inode bitmap

	// we know the total bits to be assigned => convert it to bytes and set all bytes to 0 initially 
	memset(inode_bitmap_buffer,0, BLOCK_SIZE);

	// update bitmap information for root directory

	// Now as we know the first node should be the root node => set the bit corresponding to the first inode and write the in-memory copy to disk 
	set_bitmap(inode_bitmap_buffer, 0);

	// Writing the inode bitmaps to disk 
	// Since the all operations on disk should be of BLOCK_SIZE and our bitmap is not that big enough we copy it in a buffer of size BLOCK_SIZE and then write to the disk and the desired block num location
	// write inode_bitmap buffer to disk
	if (bio_write(INODE_BITMAP_BLKNO, inode_bitmap_buffer) < 0) {
		perror("inode_bitmap_buffer NOT able to write to disk during initialisation(rufs_mkfs)");
		exit(EXIT_FAILURE);
	}

	// initialize data block bitmap

	// We know already occupied data blocks during superblock initialisation
	// DATA BLOCK 0 = SUPERBLOCK
	// DATA BLOCK 1 = INODE_BITMAP_BLOCK
	// DATA BLOCK 2 = DATA_BLOCK_BITMAP_BLOCK
	// DATA BLOCK 3 = INODE_BLOCKS
	// DATA BLOCK 67 = DATA_BLOCKS
	memset(db_bitmap_buffer, 0, BLOCK_SIZE);
	for (int i = 0; i < DATA_BLOCK_START; i++) {
        set_bitmap(db_bitmap_buffer, i); 
	}
	// write db_bitmap buffer to disk
	if (bio_write(DB_BITMAP_BLKNO, db_bitmap_buffer) < 0) {
		perror("db_bitmap_buffer NOT able to write to disk during initialisation(rufs_mkfs)");
		exit(EXIT_FAILURE);
	}

	
	// we had assigned the first node as root node in the bitmap 
	// Initialise the first node as root node. Steps to follow:
	// 1. create a in-memory copy of the root inode.
	static struct inode root_inode;
	memset(&root_inode, 0, sizeof(root_inode));
    root_inode.ino = 0;
    root_inode.valid = 1;
    root_inode.type = TYPE_DIR;
    root_inode.link = 2;  // For . and ..
    root_inode.vstat.st_mode = S_IFDIR | 0755;
    root_inode.vstat.st_nlink = 2;
    root_inode.vstat.st_uid = getuid();
    root_inode.vstat.st_gid = getgid();
    root_inode.vstat.st_blksize = BLOCK_SIZE;
    root_inode.vstat.st_ctime = root_inode.vstat.st_atime = root_inode.vstat.st_mtime = time(NULL);

	for(int i=0;i<16;i++){
		root_inode.direct_ptr[i]=0;
	}

	for(int i=0;i<8;i++){
		root_inode.indirect_ptr[i]=0;
	}

	// 2. in order to write it to the disk 
	// Create an in-memory copy(i.e data block buffer) of the data block that stores the inode (i.e INODE_BLOCK_START)
	// update the buffer(i.e update the inode that it stores)
	//  write that buffer to the disk at the block num which stores the first inode (i.e INODE_BLOCK_START)
	// Sine we will have to repeat the task of updating inodes a lot we have already been given a helper function (i.e writei)
	if (writei(0, &root_inode) < 0) {
		perror("root_inode NOT updated on disk during initialisation(rufs_mkfs)");
		exit(EXIT_FAILURE);
	}

	return 0;
}

static void *rufs_init(struct fuse_conn_info *conn) {

	// Step 1a: If disk file is not found, call mkfs
	int diskfile = -1;
	diskfile = open(diskfile_path, O_RDWR, S_IRUSR | S_IWUSR);

	if (diskfile < 0) {
		if (rufs_mkfs() != 0) {
			exit(EXIT_FAILURE);
		}
	}

	// Step 1b: If disk file is found, just initialize in-memory data structures
	// and read superblock from disk

	else {
		if (dev_open(diskfile_path) < 0) {
			
			exit(EXIT_FAILURE);
		}

		// Superblock
		if (bio_read(0, super_block_buffer) < 0) {
			
			exit(EXIT_FAILURE);
		}
		memcpy(&super_block, super_block_buffer, sizeof(struct superblock));

		// Inode bitmap
		if (bio_read(1, inode_bitmap_buffer) < 0) {
			
			exit(EXIT_FAILURE);
		}

		// Data block bitmap
		if (bio_read(2, db_bitmap_buffer) < 0) {
			exit(EXIT_FAILURE);
		}
	}
	return NULL;
}

/* 
 * FUSE based operations
 */

static void rufs_destroy(void *userdata) {

	// Step 1: De-allocate in-memory data structures
	// Step 2: Close diskfile
	dev_close();
}

static int rufs_getattr(const char *path, struct stat *stbuf) {
    // printf("in getattr------");
    // Step 1: call get_node_by_path() to get inode from path
    struct inode inode = {0}; // Initialize to zeros

    int retstat = get_node_by_path(path, 0, &inode);
    if (retstat < 0) {
        return retstat;
    }

    // Step 2: fill attribute of file into stbuf from inode
    memcpy(stbuf, &(inode.vstat), sizeof(struct stat));

    return 0;
}

/* 
 * FUSE based DIRECTORY operations
 */
static int rufs_opendir(const char *path, struct fuse_file_info *fi) {
    // Step 1: Call get_node_by_path() to get inode from path
    char duplicated_path[PATH_MAX];

    // Check for buffer overflow
    if (strlen(path) >= PATH_MAX) {
        fprintf(stderr, "Error: Path is too long in rufs_opendir\n");
        return -1;
    }

    // Copy the path into duplicated_path
    strncpy(duplicated_path, path, PATH_MAX);
    duplicated_path[PATH_MAX - 1] = '\0'; // Ensure null termination

    struct inode directory_inode;
    int result = get_node_by_path(duplicated_path, 0, &directory_inode); // inode number for root is 0
    
    // Step 2: If not find, return -1
    if (result < 0) {
        fprintf(stderr, "Error: Failed to open directory '%s'\n", path);
        return -1;
    }

    return result;
}

static int rufs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
    // Step 1: Call get_node_by_path() to get inode from path
    struct inode directory_inode;
    int inode_lookup_result = get_node_by_path(path, 0, &directory_inode); // inode number for root is 0
    if (inode_lookup_result < 0) {
        return inode_lookup_result;
    }

    if (directory_inode.type != TYPE_DIR) {
        return -ENOTDIR;
    }

    static char directory_block[BLOCK_SIZE];
    static struct dirent current_entry;
    
    // Step 2: Read directory entries from its data blocks, and copy them to filler
    int block_index = 0, entry_offset = 0;
    while (block_index < 16 && directory_inode.direct_ptr[block_index] != 0) {
        int read_result = bio_read(directory_inode.direct_ptr[block_index], directory_block);
        if (read_result < 0) {
            return read_result;
        }

        entry_offset = 0;
        while (entry_offset < BLOCK_SIZE / sizeof(struct dirent)) {
            memcpy(&current_entry, directory_block + (entry_offset * sizeof(struct dirent)), sizeof(struct dirent));
            if (current_entry.valid) {
                filler(buffer, current_entry.name, NULL, 0);
            }
            entry_offset++;
        }
        block_index++;
    }

    return 0;
}

void get_parent_path(const char *path, char *parent) {
    strcpy(parent, path);
    char *last_slash = strrchr(parent, '/');
    if (last_slash != NULL) {
        *last_slash = '\0';  // Cut off the part after the last '/'
    }
}

void get_target_name(const char *path, char *target) {
    const char *last_slash = strrchr(path, '/');
    if (last_slash != NULL) {
        strcpy(target, last_slash + 1);  // Copy the part after the last '/'
    } else {
        strcpy(target, path);  // The path is already the target name
    }
}

static int rufs_create_or_mkdir(const char *path, mode_t mode, int is_directory) {

    // Step 1: Separate parent directory path and target name
    static char parent_path[PATH_MAX], target_name[PATH_MAX];
    get_parent_path(path, parent_path);
    get_target_name(path, target_name);


    // Step 2: Call get_node_by_path() to get the inode of the parent directory
    static struct inode parent_inode;
    int parent_inode_lookup_result = get_node_by_path(parent_path, 0, &parent_inode);
    if (parent_inode_lookup_result < 0) {
        return parent_inode_lookup_result;
    }

    // Step 3: Call get_avail_ino() to get an available inode number
    uint16_t new_inode_num = get_avail_ino();
    if (new_inode_num < 0) {
        return new_inode_num;
    }

    // Step 4: Call dir_add() to add entry to the parent directory
    int dir_add_result = dir_add(parent_inode, new_inode_num, target_name, strlen(target_name));
    if (dir_add_result < 0) {
        return dir_add_result;
    }

    // Step 5: Update the inode for the new entry
    static struct inode new_inode = {0};
    memset(&new_inode, 0, sizeof(struct inode));
    new_inode.ino = new_inode_num;
    new_inode.valid = 1;
    new_inode.type = is_directory ? TYPE_DIR : TYPE_FILE;
    new_inode.link = is_directory ? 2 : 1;  // for . and .. in directories
    new_inode.vstat.st_mode = is_directory ? (S_IFDIR | mode) : (S_IFREG | mode);
    new_inode.vstat.st_nlink = is_directory ? 2 : 1;
    new_inode.vstat.st_uid = getuid();
    new_inode.vstat.st_gid = getgid();
    new_inode.vstat.st_blksize = BLOCK_SIZE;
    new_inode.vstat.st_ctime = new_inode.vstat.st_atime = new_inode.vstat.st_mtime = time(NULL);

    // Step 6: Call writei() to write the inode to disk
    int write_inode_result = writei(new_inode_num, &new_inode);
    if (write_inode_result < 0) {
        return write_inode_result;
    }

    return 0;
}

static int rufs_mkdir(const char *path, mode_t mode) {
    return rufs_create_or_mkdir(path, mode, 1);  // 1 indicates it's a directory
}

// static int recursive_remove(int blk_num){
//     // Base Case 
//     if (blk_num == 0){
//         return 0;
//     }
//     // put valid children on the stack

//     int retstat = 0;
//     char curr_dir_block[BLOCK_SIZE];
//     // Read the directory's data block
//     retstat = bio_read(blk_num, curr_dir_block);
//     if (retstat < 0) {
//         return retstat;
//     }

//     // Check each directory entry within the block
//     int i = 0;
//     while (i < BLOCK_SIZE / sizeof(struct dirent)) {
//         struct dirent *cur_dirent = (struct dirent *)(curr_dir_block + (i * sizeof(struct dirent)));
//         if (cur_dirent->valid == 1){
//             struct inode current_inode;
//             retstat = readi(cur_dirent->ino, &current_inode);
//             if (retstat < 0) {
//                 return retstat;
//             }
//             int block_index;
//             for (block_index = 0; block_index < 16; block_index++) {
//                 retstat = recursive_remove(current_inode.direct_ptr[block_index]);
//                 current_inode.direct_ptr[block_index] = 0;
//             }
//             unset_bitmap(inode_bitmap_buffer, cur_dirent->ino);
//         }
//         i++;
//     }

//     unset_bitmap(db_bitmap_buffer, blk_num);

//     return retstat;
// }

static int rufs_rmdir(const char *path) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name
    static char parent_path[PATH_MAX], target_name[PATH_MAX], target_dir_path[PATH_MAX];

    // Check for buffer overflow
    if (strlen(path) >= PATH_MAX) {
        fprintf(stderr, "Error: Path is too long in rufs_opendir\n");
        return -1;
    }

    get_parent_path(path, parent_path);
    get_target_name(path, target_name);

	// Step 2: Call get_node_by_path() to get inode of target directory
    // Copy the path into target_dir_path
    strncpy(target_dir_path, path, PATH_MAX);
    target_dir_path[PATH_MAX - 1] = '\0'; // Ensure null termination

    static struct inode target_dir_inode;
    int target_inode_lookup_result = get_node_by_path(target_dir_path, 0, &target_dir_inode);
    if (target_inode_lookup_result < 0) {
        return target_inode_lookup_result;
    }

	// Step 3: Clear data block bitmap of target directory
    int block_index;
    for (block_index = 0; block_index < 16 && target_dir_inode.direct_ptr[block_index] != 0; block_index++) {
        unset_bitmap(db_bitmap_buffer, target_dir_inode.direct_ptr[block_index]);
        target_dir_inode.direct_ptr[block_index] = 0;
    }
    // for (block_index = 0; block_index < 16; block_index++) {
    //     int result = recursive_remove(target_dir_inode.direct_ptr[block_index]);
    //     if (result != 0){
    //         return result;
    //     }
    //     target_dir_inode.direct_ptr[block_index] = 0;
    // }

	// Step 4: Clear inode bitmap and its data block
    unset_bitmap(inode_bitmap_buffer, target_dir_inode.ino);

	// Step 5: Call get_node_by_path() to get inode of parent directory
    static struct inode parent_dir_inode;
    int parent_inode_lookup_result = get_node_by_path(parent_path, 0, &parent_dir_inode);
    if (parent_inode_lookup_result < 0) {
        return parent_inode_lookup_result;
    }
    
	// Step 6: Call dir_remove() to remove directory entry of target directory in its parent directory
    int result = dir_remove(parent_dir_inode, target_name, strlen(target_name));
    if (result < 0){
        return result;
    }
	return 0;
}


static int rufs_releasedir(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

/* 
 * FUSE based FILE operations
 */

static int rufs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    return rufs_create_or_mkdir(path, mode, 0);  // 0 indicates it's a regular file
}

static int rufs_open(const char *path, struct fuse_file_info *fi) {
    // Step 1: Call get_node_by_path() to get inode from path
    static struct inode file_inode;
    int inode_lookup_result = get_node_by_path(path, 0, &file_inode);

    if (inode_lookup_result < 0) {
        return -1;
    }

   // Step 2: If not find, return -1
    if (file_inode.valid != 1 || file_inode.type != TYPE_FILE || file_inode.ino >= MAX_INUM) {
        return -1; // Invalid file type or invalid inode number
    }

    return 0;
}

static int rufs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
	// Step 1: You could call get_node_by_path() to get inode from path
    static struct inode temp_inode;
    int retstat = get_node_by_path(path, 0, &temp_inode);
    if (retstat < 0) {
        return retstat;
    }

	// Step 2: Based on size and offset, read its data blocks from disk
    int bytes_read = 0;
    static char data_block[BLOCK_SIZE];

    while (bytes_read < size) {
        int block_num = (offset + bytes_read) / BLOCK_SIZE;
        int block_offset = (offset + bytes_read) % BLOCK_SIZE;
        int block_to_read = super_block.d_start_blk + temp_inode.direct_ptr[block_num];

        if (temp_inode.direct_ptr[block_num] == 0) {
            // No more data to read
            break;
        }

        if (bio_read(block_to_read, data_block) < 0) {
            return -EIO;
        }

        int bytes_to_read = BLOCK_SIZE - block_offset;
        if (bytes_to_read > size - bytes_read) {
            bytes_to_read = size - bytes_read;
        }
	    // Step 3: copy the correct amount of data from offset to buffer
        memcpy(buffer + bytes_read, data_block + block_offset, bytes_to_read);
        bytes_read += bytes_to_read;
    }

    // Update the inode access time
    time(&(temp_inode.vstat.st_atime));
    writei(temp_inode.ino, &temp_inode);

	// Note: this function should return the amount of bytes you copied to buffer
    return bytes_read;
}

static int rufs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
    
    // Step 1: You could call get_node_by_path() to get inode from path
    static struct inode temp_inode;
    int retstat = get_node_by_path(path, 0, &temp_inode);
    if (retstat < 0) {
        return retstat;
    }
	// Step 2: Based on size and offset, read its data blocks from disk
    int bytes_written = 0;
    static char data_block[BLOCK_SIZE];
    memset(data_block, 0, sizeof(data_block));


    while (bytes_written < size) {
        int block_num = (offset + bytes_written) / BLOCK_SIZE;
        int block_offset = (offset + bytes_written) % BLOCK_SIZE;
        int block_to_write = super_block.d_start_blk + temp_inode.direct_ptr[block_num];

        if (temp_inode.direct_ptr[block_num] == 0) {
            int new_data_block = get_avail_blkno();
            retstat = bio_write(new_data_block, data_block);
            if (retstat < 0) {
                return retstat;
            }
            if (new_data_block < 0) return new_data_block;
            temp_inode.direct_ptr[block_num] = new_data_block;
            block_to_write = super_block.d_start_blk + new_data_block;
        }
	    // Step 3: Write the correct amount of data from offset to disk
        int bytes_to_write = BLOCK_SIZE - block_offset;
        if (bytes_to_write > size - bytes_written) {			//this is more or less checking if we can write the full block or not
            bytes_to_write = size - bytes_written;
        }

        if (bio_read(block_to_write, data_block) < 0) {
            return -EIO;
        }

        memcpy(data_block + block_offset, buffer + bytes_written, bytes_to_write);

        if (bio_write(block_to_write, data_block) < 0) {
            return -EIO;
        }

        bytes_written += bytes_to_write;
    }

    time(&(temp_inode.vstat.st_atime));
    time(&(temp_inode.vstat.st_mtime));
    temp_inode.vstat.st_size = offset + bytes_written;
    // Step 4: Update the inode info and write it to disk
    writei(temp_inode.ino, &temp_inode);

    // Note: this function should return the amount of bytes you write to disk
    return bytes_written;
}



static int rufs_unlink(const char *path) {
    // Step 1: Use dirname() and basename() to separate parent directory path and target directory name
    static char parent_path[PATH_MAX], target_name[PATH_MAX], target_dir_path[PATH_MAX];

    // Check for buffer overflow
    if (strlen(path) >= PATH_MAX) {
        fprintf(stderr, "Error: Path is too long in rufs_opendir\n");
        return -1;
    }

    get_parent_path(path, parent_path);
    get_target_name(path, target_name);

	// Step 2: Call get_node_by_path() to get inode of target directory
    // Copy the path into target_dir_path
    strncpy(target_dir_path, path, PATH_MAX);
    target_dir_path[PATH_MAX - 1] = '\0'; // Ensure null termination

    static struct inode target_dir_inode;
    int target_inode_lookup_result = get_node_by_path(target_dir_path, 0, &target_dir_inode);
    if (target_inode_lookup_result < 0) {
        return target_inode_lookup_result;
    }

	// Step 3: Clear data block bitmap of target directory
    int block_index;
    for (block_index = 0; block_index < 16 && target_dir_inode.direct_ptr[block_index] != 0; block_index++) {
        unset_bitmap(db_bitmap_buffer, target_dir_inode.direct_ptr[block_index]);
        target_dir_inode.direct_ptr[block_index] = 0;
    }
	// Step 4: Clear inode bitmap and its data block
    unset_bitmap(inode_bitmap_buffer, target_dir_inode.ino);

	// Step 5: Call get_node_by_path() to get inode of parent directory
    static struct inode parent_dir_inode;
    int parent_inode_lookup_result = get_node_by_path(parent_path, 0, &parent_dir_inode);
    if (parent_inode_lookup_result < 0) {
        return parent_inode_lookup_result;
    }
    
	// Step 6: Call dir_remove() to remove directory entry of target directory in its parent directory
    int result = dir_remove(parent_dir_inode, target_name, strlen(target_name));
    if (result < 0){
        return result;
    }
	return 0;
	
}

static int rufs_truncate(const char *path, off_t size) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int rufs_release(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
	return 0;
}

static int rufs_flush(const char * path, struct fuse_file_info * fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int rufs_utimens(const char *path, const struct timespec tv[2]) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static struct fuse_operations rufs_ope = {
	.init		= rufs_init,
	.destroy	= rufs_destroy,

	.getattr	= rufs_getattr,
	.readdir	= rufs_readdir,
	.opendir	= rufs_opendir,
	.releasedir	= rufs_releasedir,
	.mkdir		= rufs_mkdir,
	.rmdir		= rufs_rmdir,

	.create		= rufs_create,
	.open		= rufs_open,
	.read 		= rufs_read,
	.write		= rufs_write,
	.unlink		= rufs_unlink,

	.truncate   = rufs_truncate,
	.flush      = rufs_flush,
	.utimens    = rufs_utimens,
	.release	= rufs_release
};


int main(int argc, char *argv[]) {
	int fuse_stat;

	getcwd(diskfile_path, PATH_MAX);
	strcat(diskfile_path, "/DISKFILE");

	fuse_stat = fuse_main(argc, argv, &rufs_ope, NULL);

	return fuse_stat;
}








