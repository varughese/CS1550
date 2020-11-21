/*
	FUSE: Filesystem in Userspace
	Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

	This program can be distributed under the terms of the GNU GPL.
	See the file COPYING.
*/

/*

	Commands:

	Make new disk file:
	dd bs=1K count=5K if=/dev/zero of=.disk

	Mount `testmount`
	./cs1550 testmount

	Mount `testmount` in debug mode
	./cs1550 -d testmount

	Unmount `testmount`
	fusermount -u testmount

	If a device is busy error
	kill -9 cs1550

*/

#define	FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>

//size of a disk block
#define	BLOCK_SIZE 512

#define TOTAL_DISK_SIZE 512 * 1024 * 1024

// Round Up[ (TOTAL_DISK_SIZE/(512*8))/512 ]
#define BITMAP_SIZE_IN_BLOCKS 3 
// blocks, not counting the bitmap
#define TOTAL_BLOCKS (TOTAL_DISK_SIZE / BLOCK_SIZE) - BITMAP_SIZE_IN_BLOCKS

//we'll use 8.3 filenames
#define	MAX_FILENAME 8
#define	MAX_EXTENSION 3

// directorie names same size as file
#define MAX_DIRNAME 8

//How many files can there be in one directory?
#define MAX_FILES_IN_DIR (BLOCK_SIZE - sizeof(int)) / ((MAX_FILENAME + 1) + (MAX_EXTENSION + 1) + sizeof(size_t) + sizeof(long))

//The attribute packed means to not align these things
struct cs1550_directory_entry
{
	int nFiles;	//How many files are in this directory.
				//Needs to be less than MAX_FILES_IN_DIR

	struct cs1550_file_directory
	{
		char fname[MAX_FILENAME + 1];	//filename (plus space for nul)
		char fext[MAX_EXTENSION + 1];	//extension (plus space for nul)
		size_t fsize;					//file size
		long nStartBlock;				//where the first block is on disk
	} __attribute__((packed)) files[MAX_FILES_IN_DIR];	//There is an array of these

	//This is some space to get this to be exactly the size of the disk block.
	//Don't use it for anything.  
	char padding[BLOCK_SIZE - MAX_FILES_IN_DIR * sizeof(struct cs1550_file_directory) - sizeof(int)];
} ;

typedef struct cs1550_root_directory cs1550_root_directory;

#define MAX_DIRS_IN_ROOT (BLOCK_SIZE - sizeof(int)) / ((MAX_DIRNAME + 1) + sizeof(long))

struct cs1550_root_directory
{
	int nDirectories;	//How many subdirectories are in the root
						//Needs to be less than MAX_DIRS_IN_ROOT
	struct cs1550_directory
	{
		char dname[MAX_DIRNAME + 1];	//directory name (plus space for nul)
		long nStartBlock;				//where the directory block is on disk
	} __attribute__((packed)) directories[MAX_DIRS_IN_ROOT];	//There is an array of these

	//This is some space to get this to be exactly the size of the disk block.
	//Don't use it for anything.  
	char padding[BLOCK_SIZE - MAX_DIRS_IN_ROOT * sizeof(struct cs1550_directory) - sizeof(int)];
} ;


typedef struct cs1550_directory_entry cs1550_directory_entry;

//How much data can one block hold?
#define	MAX_DATA_IN_BLOCK (BLOCK_SIZE - sizeof(long))

struct cs1550_disk_block
{
	// All of the space in the block can be used for actual data
	// storage.
	char data[MAX_DATA_IN_BLOCK];

	// Pointer, like a node in a linked list
	long next;
};

typedef struct cs1550_disk_block cs1550_disk_block;

// Start this at 1 to ignore the first block index, which will hold only the root
static long next_free_block_index = 1;

////////////////// DISK OPERATIONS //////////////////

/* Open the disk file */
static FILE* open_disk(void) {
	FILE *file_ptr = fopen(".disk", "rb+");
	if (file_ptr == NULL) {
		return NULL;
	}

	return file_ptr;
}

/* Close the disk file */
static int close_disk(FILE *file_ptr) {
	return fclose(file_ptr);
}

/* Open a BLOCK_SIZE block on the disk */
static void *open_block(FILE* disk, long index) {
	fseek(disk, BLOCK_SIZE * index, SEEK_SET);
	void* block = calloc(1, BLOCK_SIZE);
	fread(block, BLOCK_SIZE, 1, disk);
	return block;
}

/* Write a BLOCK_SIZE block on the disk */
static int write_block(FILE* disk, long index, void *block) {
	fseek(disk, BLOCK_SIZE * index, SEEK_SET);
	fwrite(block, BLOCK_SIZE, 1, disk);
	return 0;
}

////////////////// BIT MAP  /////////////////////////

/*
	In the disk, the last BITMAP_SIZE_IN_BLOCKS blocks hold the
	bitmap that contains which blocks are in use or not. 

	DISK:
	[_ _ _ _ _ _ _ _ _ _ BITMAP]
	Each bit in the BITMAP represents a disk blocks
*/

/* Gets the ith bit */
static int get_ith_bit(unsigned char byte, int position) {
   return (byte >> (8-position-1)) & 1;
}

/* Sets the ith bit */
static int set_ith_bit(unsigned char byte, int position, char val) {
	if (val == 1)
   		return byte |  (1 << (8-position-1));
	else
		return byte & ~(1 << (8-position-1));
}

/*	Return the index of the next free block. Usually should be 
	whatever is next up since this filesystem does not do removes
*/
static long find_next_free_block_index(FILE* disk) {
	int i;
	for(i = next_free_block_index; i < TOTAL_BLOCKS; i++) {
		long position = -(BLOCK_SIZE * BITMAP_SIZE_IN_BLOCKS) + (i/8);
		fseek(disk, position, SEEK_END);
		unsigned char byte = 0;
		fread(&byte, 1, 1, disk);
		int bit_position = i % 8;
		if(get_ith_bit(byte, bit_position) == 0) {
			next_free_block_index = i;
			return next_free_block_index;
		}
	}
	return -1;
}

/* Sets the block index in the bitmap */
static long set_bitmap(FILE* disk, long index, char is_taken) {
	long position = -(BLOCK_SIZE * BITMAP_SIZE_IN_BLOCKS) + (index / 8);
	fseek(disk, position, SEEK_END);
	unsigned char byte;
	fread(&byte, 1, 1, disk);
	int bit_position = index % 8;
	byte = set_ith_bit(byte, bit_position, is_taken);
	fseek(disk, -1, SEEK_CUR);
	fwrite(&byte, 1, 1, disk);
	return -1;
}

////////////////// ROOT OPERATIONS //////////////////

/* Open up the root block */
static cs1550_root_directory *open_root(FILE* disk) {
	return (cs1550_root_directory*) open_block(disk, 0);
}

/* Save the root block back to disk */
static void save_root(FILE* disk, cs1550_root_directory *root) {
	write_block(disk, 0, root);
}

/* Open a directory given its block index */
static cs1550_directory_entry *open_dir(FILE* disk, long index) {
	return (cs1550_directory_entry*)(open_block(disk, index));
}

/* Saves a directory back to this disk given its block index */
static void save_dir(FILE* disk, long disk_dir_block_index, cs1550_directory_entry *dir) {
	write_block(disk, disk_dir_block_index, dir);
}


/*
 * Called whenever the system wants to know the file attributes, including
 * simply whether the file exists or not. 
 *
 * man -s 2 stat will show the fields of a stat structure
 */
static int cs1550_getattr(const char *path, struct stat *stbuf) {
	int res = 0;
	FILE *disk = open_disk();

	memset(stbuf, 0, sizeof(struct stat));
	
	//is path the root dir?
	if (strcmp(path, "/") == 0) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
	} else {
		char directory[MAX_DIRNAME + 1];
		char filename[MAX_FILENAME + 1];
		char extension[MAX_EXTENSION + 1];
		int count = sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
		
		/* If there is no directory, error */
		if (count < 1) {
			close_disk(disk);
			return -ENOENT;
		}
		
		cs1550_root_directory* root = open_root(disk);
		

		int looking_for_a_dir = count == 1;

		/* This code is common throughout functions. Looks for a directory by
		   name */	
		int dir_index = 0;
		int nDirectories = root->nDirectories;

		for(dir_index = 0; dir_index < nDirectories; dir_index++) {
			if(strcmp(root->directories[dir_index].dname, directory) == 0) {
				break;
			}
		}
		int dir_not_found = dir_index == nDirectories;
		
		if(dir_not_found) {
			free(root);
			close_disk(disk);
			return -ENOENT;
		}

		//Check if name is subdirectory
		if(looking_for_a_dir) {
			//Might want to return a structure with these fields
			stbuf->st_mode = S_IFDIR | 0755;
			stbuf->st_nlink = 2;
			res = 0;
			free(root);
			close_disk(disk);
			return res;
		}
		
		long dir_block_location = root->directories[dir_index].nStartBlock;
		cs1550_directory_entry *current_dir = open_dir(disk, dir_block_location);

		int file_index = 0;
		int nFiles = current_dir->nFiles;

		//Check if name is a regular file
		//regular file, probably want to be read and write
		for(file_index = 0; file_index < nFiles; file_index++) {
			if(strcmp(current_dir->files[file_index].fname, filename) == 0 &&
			   strcmp(current_dir->files[file_index].fext, extension) == 0) {
				   break;
			}
		}

		if(file_index == nFiles) {
			// File Not Found
			free(current_dir);
			free(root);
			close_disk(disk);
			return -ENOENT;
		}

		stbuf->st_mode = S_IFREG | 0666; 
		stbuf->st_nlink = 1; //file links
		stbuf->st_size = current_dir->files[file_index].fsize; //file size - make sure you replace with real size!
		res = 0; // no error
		free(current_dir);
		free(root);
	}
	close_disk(disk);
	return res;
}

/* 
 * Called whenever the contents of a directory are desired. Could be from an 'ls'
 * or could even be when a user hits TAB to do autocompletion
 */
static int cs1550_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
	//Since we're building with -Wall (all warnings reported) we need
	//to "use" every parameter, so let's just cast them to void to
	//satisfy the compiler
	(void) offset;
	(void) fi;


	FILE *disk = open_disk();
	cs1550_root_directory* root = open_root(disk);

	//the filler function allows us to add entries to the listing
	//read the fuse.h file for a description (in the ../include dir)
	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);


	/* If we are at path, fill all directory names */
	if (strcmp(path, "/") == 0) {
		int dir_index = 0;
		int nDirectories = root->nDirectories;

		for(dir_index = 0; dir_index < nDirectories; dir_index++) {
			filler(buf, root->directories[dir_index].dname, NULL, 0);
		}

		free(root);
		close_disk(disk);
		return 0;
	}

	char directory[MAX_DIRNAME + 1];
	char filename[MAX_FILENAME + 1];
	char extension[MAX_EXTENSION + 1];
	int count = sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);

	/* If we have more than a directory then error */
	if (count > 1) return -ENOENT;

	/* If we are in a subdirectory, then fill all file names */

	/* First, look for the directory node */
	int nDirectories = root->nDirectories;
	int dir_index = 0;
	for(dir_index = 0; dir_index < nDirectories; dir_index++) {
		if(strcmp(root->directories[dir_index].dname, directory) == 0) {
			break;
		}
	}

	int dir_not_found = dir_index == nDirectories;
		
	if(dir_not_found) {
		free(root);
		close_disk(disk);
		return -ENOENT;
	}

	long dir_block_location = root->directories[dir_index].nStartBlock;
	cs1550_directory_entry *current_dir = open_dir(disk, dir_block_location);

	int file_index = 0;
	int nFiles = current_dir->nFiles;

	// Add each file name from this directory
	for(file_index = 0; file_index < nFiles; file_index++) {
		char current_filename[MAX_FILENAME + MAX_EXTENSION + 1];
        strcpy(current_filename, current_dir->files[file_index].fname);
        strcat(current_filename, ".");
        strcat(current_filename, current_dir->files[file_index].fext);
		filler(buf, current_filename, NULL, 0);
	}

	free(root);
	free(current_dir);
	close_disk(disk);
	return 0;
}

/* 
 * Creates a directory. We can ignore mode since we're not dealing with
 * permissions, as long as getattr returns appropriate ones for us.
 */
static int cs1550_mkdir(const char *path, mode_t mode) {
	(void) path;
	(void) mode;

	char directory[MAX_DIRNAME + 1];
	char filename[MAX_FILENAME + 1];
	char extension[MAX_EXTENSION + 1];
	int count = sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);

	int is_a_dir = count == 1;


	if (!is_a_dir) return -EPERM;
	if(strlen(directory) > MAX_DIRNAME) return -ENAMETOOLONG;

	FILE *disk = open_disk();
	cs1550_root_directory* root = open_root(disk);

	int dir_index = 0;
	int nDirectories = root->nDirectories;

	for(dir_index = 0; dir_index < nDirectories; dir_index++) {
		if(strcmp(root->directories[dir_index].dname, directory) == 0) {
			close_disk(disk);
			free(root);
			return -EEXIST;
		}
	}

	if(nDirectories >= MAX_DIRS_IN_ROOT) {
		close_disk(disk);
		free(root);
		return -ENOSPC;
	}

	dir_index = root->nDirectories;
	root->nDirectories++;
	strcpy(root->directories[dir_index].dname, directory);
	long next_open_block = find_next_free_block_index(disk);
	
	if (next_open_block < 0) {
		free(root);
		close_disk(disk);
		return -ENOSPC;
	}
	
	root->directories[dir_index].nStartBlock = next_open_block;
	set_bitmap(disk, next_open_block, 1);

	save_root(disk, root);
	free(root);
	close_disk(disk);
	return 0;
}

/* 
 * Removes a directory.
 */
static int cs1550_rmdir(const char *path) {
	(void) path;
    return 0;
}

/* 
 * Does the actual creation of a file. Mode and dev can be ignored.
 *
 */
static int cs1550_mknod(const char *path, mode_t mode, dev_t dev) {
	(void) mode;
	(void) dev;

	char directory[MAX_DIRNAME + 1];
	char filename[MAX_FILENAME + 1];
    char extension[MAX_EXTENSION + 1];
  	int count = sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
	
	/* Make sure we are creating in a directory */ 
	if (count < 3) return -EPERM;

	/* Validate filename */	
	if(strlen(filename) > MAX_FILENAME || strlen(extension) > MAX_EXTENSION) return -ENAMETOOLONG;

	FILE *disk = open_disk();
	cs1550_root_directory* root = open_root(disk);

	int nDirectories = root->nDirectories;
	int dir_index = 0;
	for(dir_index = 0; dir_index < nDirectories; dir_index++) {
		if(strcmp(root->directories[dir_index].dname, directory) == 0) {
			break;
		}
	}

	int dir_not_found = dir_index == nDirectories;
	if(dir_not_found) {
		free(root);
		close_disk(disk);
		return -ENOENT;
	}

	long dir_block_location = root->directories[dir_index].nStartBlock;
	cs1550_directory_entry *current_dir = open_dir(disk, dir_block_location);
	free(root);

	int file_index = 0;
	int nFiles = current_dir->nFiles;

	/* Check if file exists in the directory already */
	for(file_index = 0; file_index < nFiles; file_index++) {
		if (strcmp(current_dir->files[file_index].fname, filename) == 0 &&
			strcmp(current_dir->files[file_index].fext, extension) == 0) {
			free(current_dir);
			close_disk(disk);
			return -EEXIST;
		}
	}

	if (nFiles > MAX_FILES_IN_DIR) {
		free(current_dir);
		close_disk(disk);
		return -ENOSPC; 	
	}

	file_index = nFiles;
	current_dir->nFiles++;
	strcpy(current_dir->files[file_index].fname, filename);
	strcpy(current_dir->files[file_index].fext, extension);
	current_dir->files[file_index].fsize = 0;
	
	long next_open_block = find_next_free_block_index(disk);
	if (next_open_block < 0) {
		free(current_dir);
		close_disk(disk);
		return -ENOSPC;
	}


	current_dir->files[file_index].nStartBlock = next_open_block;
	set_bitmap(disk, next_open_block, 1);

	save_dir(disk, dir_block_location, current_dir);
	// free(current_dir) here causes an error for some reason
	close_disk(disk);
	return 0;
}

/*
 * Deletes a file
 */
static int cs1550_unlink(const char *path) {
    (void) path;

    return 0;
}

/* 
 * Read size bytes from file into buf starting from offset
 *
 */
static int cs1550_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
	(void) buf;
	(void) offset;
	(void) fi;
	(void) path;

	char directory[MAX_DIRNAME + 1];
	char filename[MAX_FILENAME + 1];
	char extension[MAX_EXTENSION + 1];
	int count = sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
	

	if (size <= 0) return -EPERM;
	if (count != 3) return -EISDIR;

	if (strlen(directory) > MAX_DIRNAME || strlen(filename) > MAX_FILENAME
										|| strlen(extension) > MAX_EXTENSION) return -ENOENT;

	FILE *disk = open_disk();
	cs1550_root_directory* root = open_root(disk);

	int dir_index = 0;
	int nDirectories = root->nDirectories;
	for (dir_index = 0; dir_index < nDirectories; dir_index++) {
		if (strcmp(root->directories[dir_index].dname, directory) == 0) {
			break;
		}
	}

	int dir_not_found = dir_index == nDirectories;

	if(dir_not_found) {
		free(root);
		close_disk(disk);
		return -ENOENT;
	}

	long dir_block_location = root->directories[dir_index].nStartBlock;
	cs1550_directory_entry *current_dir = open_dir(disk, dir_block_location);
	free(root);

	/* Check the file exists in the directory already */
	int file_index = 0;
	int nFiles = current_dir->nFiles;
	for(file_index = 0; file_index < nFiles; file_index++) {
		if(strcmp(current_dir->files[file_index].fname, filename) == 0 &&
			strcmp(current_dir->files[file_index].fext, extension) == 0) {
			break;
		}
	}

	if (file_index == nFiles) {
		// File Not Found
		free(current_dir);
		close_disk(disk);
		return -ENOENT;
	}
		
	long block_index = current_dir->files[file_index].nStartBlock;
	cs1550_disk_block *block = (cs1550_disk_block *)open_block(disk, block_index);

	size_t fsize = current_dir->files[file_index].fsize;
	/* The file read size might be bigger than the file, so shrink it if too big */
	if ((offset + size) > fsize) {
		size = fsize - offset;
	}

	/* Similar to write logic, move offset til find correct block */
	size_t adjusted_offset = offset;
	size_t pos_so_far = MAX_DATA_IN_BLOCK;
	while (offset > pos_so_far) {
		if(!(block->next)) {
			free(block);
			free(current_dir);
			close_disk(disk);
			return -ENOENT;
		}
		block_index = block->next;
		free(block);
		block = (cs1550_disk_block *)open_block(disk, block_index);
		pos_so_far += MAX_DATA_IN_BLOCK;
		adjusted_offset -= MAX_DATA_IN_BLOCK;		
	}

	size_t original_size = size;
	size_t size_read = 0;

	/* If we need to read more than one block, then read the
	   remainder of the current block in */
	if ((adjusted_offset + size) > MAX_DATA_IN_BLOCK) {
		size_t size_read_so_far = MAX_DATA_IN_BLOCK - adjusted_offset;
		memcpy(buf, block->data + adjusted_offset, size_read_so_far);
		size_read += size_read_so_far;
		size -= size_read_so_far;
		buf += size_read_so_far;
		adjusted_offset = 0;
		if(!(block->next)) {
			free(block);
			free(current_dir);
			close_disk(disk);
			return -ENOENT;
		}
		block_index = block->next;
		free(block);
		block = (cs1550_disk_block *)open_block(disk, block_index);
	}


	while (size_read < (fsize - offset)) {
		size_t size_left = (fsize - offset) - size_read;
		if (size_left > MAX_DATA_IN_BLOCK) {
			size_read += MAX_DATA_IN_BLOCK;
			memcpy(buf, block->data + adjusted_offset, MAX_DATA_IN_BLOCK);
			buf += MAX_DATA_IN_BLOCK;
			if(!(block->next)) break;
			block_index = block->next;
			free(block);
			block = (cs1550_disk_block *)open_block(disk, block_index);
		} else {
			size_read += size_left;
			memcpy(buf, block->data + adjusted_offset, size_left);
			buf += size_left;
		}
	}
	
	free(block);
	free(current_dir);
	close_disk(disk);
	return original_size;
}

/* 
 * Write size bytes from buf into file starting from offset
 *
 */
static int cs1550_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi){
	(void) buf;
	(void) offset;
	(void) fi;
	(void) path;

	char directory[MAX_DIRNAME + 1];
	char filename[MAX_FILENAME + 1];
	char extension[MAX_EXTENSION + 1];
	int count = sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
	

	if (size <= 0) return -EPERM;
	if (count != 3) return -EEXIST;

	FILE *disk = open_disk();
	cs1550_root_directory* root = open_root(disk);

	int dir_index = 0;
	int nDirectories = root->nDirectories;
	for (dir_index = 0; dir_index < nDirectories; dir_index++) {
		if (strcmp(root->directories[dir_index].dname, directory) == 0) {
			break;
		}
	}

	int dir_not_found = dir_index == nDirectories;

	if(dir_not_found) {
		free(root);
		close_disk(disk);
		return -EEXIST;
	}

	long dir_block_location = root->directories[dir_index].nStartBlock;
	cs1550_directory_entry *current_dir = open_dir(disk, dir_block_location);
	free(root);

	/* Check the file exists in the directory already */
	int file_index = 0;
	int nFiles = current_dir->nFiles;
	for(file_index = 0; file_index < nFiles; file_index++) {
		if(strcmp(current_dir->files[file_index].fname, filename) == 0 &&
			strcmp(current_dir->files[file_index].fext, extension) == 0) {
			break;
		}
	}

	if(file_index == nFiles) {
		// File Not Found
		free(current_dir);
		close_disk(disk);
		return -ENOENT;
	}

	

	size_t current_fsize = current_dir->files[file_index].fsize;
	if (offset > current_fsize) {
		close_disk(disk);
		return -EFBIG;
	}

	size_t total_fsize = offset + size;

	long block_index = current_dir->files[file_index].nStartBlock;
	
	cs1550_disk_block *block = (cs1550_disk_block *)open_block(disk, block_index);
	
	size_t original_size = size;
	size_t size_so_far = MAX_DATA_IN_BLOCK; 
	size_t adjusted_offset = offset;
	while (offset > size_so_far) {
		/* In the case the offset is at the exact boundary of block,
		   we need to break and append
		*/
		if(!(block->next)) break;
		block_index = block->next;
		free(block);
		block = (cs1550_disk_block *)open_block(disk, block_index);
		size_so_far += MAX_DATA_IN_BLOCK;
		adjusted_offset -= MAX_DATA_IN_BLOCK;
	}

	/* Check if adding will bleed into a new block. If so, add a new block */
	if ((adjusted_offset + size) > MAX_DATA_IN_BLOCK) {
		long next_open_block = find_next_free_block_index(disk);
		block->next = next_open_block;
		set_bitmap(disk, next_open_block, 1);
		size_t size_written_so_far = MAX_DATA_IN_BLOCK - adjusted_offset;
		memcpy(block->data + adjusted_offset, buf, size_written_so_far);
		buf += size_written_so_far;
		size -= size_written_so_far;
		write_block(disk, block_index, block);
		free(block);
		block_index = next_open_block;
		block = (cs1550_disk_block *)open_block(disk, block_index);
		adjusted_offset = 0;
	}

	/* Add as many blocks is neccesary */
	while (size > 0) {
		if (size > MAX_DATA_IN_BLOCK ) {
			memcpy(block->data + adjusted_offset, buf, MAX_DATA_IN_BLOCK);
			buf += MAX_DATA_IN_BLOCK;
			size -= MAX_DATA_IN_BLOCK;
			long next_open_block = find_next_free_block_index(disk);
			block->next = next_open_block;
			write_block(disk, block_index, block);
			free(block);
			block_index = next_open_block;
			block = (cs1550_disk_block *)open_block(disk, block_index);
		} else {
			memcpy(block->data + adjusted_offset, buf, size);
			write_block(disk, block_index, block);
			size = 0;
		}	
	}
	

	current_dir->files[file_index].fsize = total_fsize;
	save_dir(disk, dir_block_location, current_dir);
	free(block);
	free(current_dir);
	close_disk(disk);
	return original_size;
}

/******************************************************************************
 *
 *  DO NOT MODIFY ANYTHING BELOW THIS LINE
 *
 *****************************************************************************/

/*
 * truncate is called when a new file is created (with a 0 size) or when an
 * existing file is made shorter. We're not handling deleting files or 
 * truncating existing ones, so all we need to do here is to initialize
 * the appropriate directory entry.
 *
 */
static int cs1550_truncate(const char *path, off_t size)
{
	(void) path;
	(void) size;

    return 0;
}


/* 
 * Called when we open a file
 *
 */
static int cs1550_open(const char *path, struct fuse_file_info *fi)
{
	(void) path;
	(void) fi;
    /*
        //if we can't find the desired file, return an error
        return -ENOENT;
    */

    //It's not really necessary for this project to anything in open

    /* We're not going to worry about permissions for this project, but 
	   if we were and we don't have them to the file we should return an error

        return -EACCES;
    */

    return 0; //success!
}

/*
 * Called when close is called on a file descriptor, but because it might
 * have been dup'ed, this isn't a guarantee we won't ever need the file 
 * again. For us, return success simply to avoid the unimplemented error
 * in the debug log.
 */
static int cs1550_flush (const char *path , struct fuse_file_info *fi)
{
	(void) path;
	(void) fi;

	return 0; //success!
}


//register our new functions as the implementations of the syscalls
static struct fuse_operations hello_oper = {
    .getattr	= cs1550_getattr,
    .readdir	= cs1550_readdir,
    .mkdir	= cs1550_mkdir,
	.rmdir = cs1550_rmdir,
    .read	= cs1550_read,
    .write	= cs1550_write,
	.mknod	= cs1550_mknod,
	.unlink = cs1550_unlink,
	.truncate = cs1550_truncate,
	.flush = cs1550_flush,
	.open	= cs1550_open,
};

//Don't change this.
int main(int argc, char *argv[])
{
	return fuse_main(argc, argv, &hello_oper, NULL);
}
