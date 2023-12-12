#include "simplefs-ops.h"

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

extern struct filehandle_t file_handle_array[MAX_OPEN_FILES]; // Array for storing opened files

int simplefs_create(char *filename){
    /*
	    Create file with name `filename` from disk.
	*/
	// Checking if file already exists //
	struct superblock_t *superblock = (struct superblock_t *)malloc(sizeof(struct superblock_t));
    simplefs_readSuperBlock(superblock);
	struct inode_t *inodeptr = (struct inode_t *)malloc(sizeof(struct inode_t));
	for (int i = 0; i < NUM_INODES; i++){
		if (superblock->inode_freelist[i] == INODE_IN_USE){
			simplefs_readInode(i,inodeptr);
			if (strcmp(inodeptr->name, filename) == 0){
				return -1; // Found a file with the same name
			}
		}
	}
	free(inodeptr);
	struct inode_t *newinodeptr = (struct inode_t *)malloc(sizeof(struct inode_t));
	int nodenum;
	nodenum = simplefs_allocInode();
	if (nodenum > -1){
		memcpy(newinodeptr->name, filename, MAX_NAME_STRLEN);
		newinodeptr->status = INODE_IN_USE;
		for(int i=0; i<MAX_FILE_SIZE; i++){
			newinodeptr->direct_blocks[i] = -1;
		}
		newinodeptr->file_size = 0;

		simplefs_writeInode(nodenum, newinodeptr);
		free(newinodeptr);
		free(superblock);
		return nodenum;
		
	}
	free(newinodeptr);
	free(superblock);
    return -1; //No space for a new file
}


void simplefs_delete(char *filename){
    /*
	    delete file with name `filename` from disk
	*/

	struct superblock_t *superblock = (struct superblock_t *)malloc(sizeof(struct superblock_t));
	simplefs_readSuperBlock(superblock);
	struct inode_t *inodeptr = (struct inode_t *)malloc(sizeof(struct inode_t));
	for (int i = 0; i < NUM_INODES; i++){
		if (superblock->inode_freelist[i] == INODE_IN_USE){
			simplefs_readInode( i , inodeptr );
			if (strcmp(inodeptr->name, filename) == 0){
				for (int j = 0; j < MAX_FILE_SIZE; j++){
					if (inodeptr->direct_blocks[j] != -1 ){
						simplefs_freeDataBlock(inodeptr->direct_blocks[j]);
					}
				}
				simplefs_freeInode(i);
				free(inodeptr);
				free(superblock);
				return;
			}
		}
	}

}

int simplefs_open(char *filename){
    /*
	    open file with name `filename`
	*/

	// Search if the file is already created or not //
	struct superblock_t *superblock = (struct superblock_t *)malloc(sizeof(struct superblock_t));
	simplefs_readSuperBlock(superblock);
	struct inode_t *inodeptr = (struct inode_t *)malloc(sizeof(struct inode_t));
	for (int i = 0; i < NUM_INODES; i++){
		if (superblock->inode_freelist[i] == INODE_IN_USE){
			simplefs_readInode( i , inodeptr );
			if (strcmp(inodeptr->name, filename) == 0){
				for(int j=0; j<MAX_OPEN_FILES; j++){
					if(file_handle_array[j].inode_number == -1){
						file_handle_array[j].inode_number = i;
						file_handle_array[j].offset = 0;
						// No changes made to the superblock, so don't need to waste time 
						// writing back into the disk.
						free(inodeptr);
						free(superblock);
						return j;
					}
				}
			}
		}
	}
	free(inodeptr);
	free(superblock);
    return -1;
}

void simplefs_close(int file_handle){
    /*
	    close file pointed by `file_handle`
	*/
	if (file_handle < 0 || file_handle >= MAX_OPEN_FILES){
		return;
	}
	file_handle_array[file_handle].inode_number = -1;
	file_handle_array[file_handle].offset = 0;

}

int simplefs_read(int file_handle, char *buf, int nbytes){
    /*
	    read `nbytes` of data into `buf` from file pointed by `file_handle` starting at current offset
	*/
	if (file_handle == -1){
		return -1;
	}
	if (nbytes <= 0){
		return 0;
	}
	struct filehandle_t *file = &file_handle_array[file_handle];
	if (file->inode_number == -1){
		return -1;
	}
	int blocknum,blockoffset,left,chars_read,j;
	left = nbytes;
	j = 0;
	struct inode_t *inodeptr = (struct inode_t *)malloc(sizeof(struct inode_t));
	int off = file->offset;
	simplefs_readInode(file->inode_number, inodeptr);
	while (left){
		blocknum = off / BLOCKSIZE;
		blockoffset = off % BLOCKSIZE;
		chars_read = MIN(left,BLOCKSIZE-blockoffset);
		char data[BLOCKSIZE+1];
		data[BLOCKSIZE] = '\0';
		if (inodeptr->direct_blocks[blocknum] == -1){
			// Reached the end of the file.
			return 0;
		}
		simplefs_readDataBlock(inodeptr->direct_blocks[blocknum], data);
		for (int i = 0; i < chars_read; i++) {
            buf[j + i] = data[blockoffset + i];
        }
		j += chars_read;
		off += chars_read;
		left -= chars_read;
	}
    if (left == 0) {
		free(inodeptr);
        return 0;
    }
	free(inodeptr);
    return -1;
}

int simplefs_write(int file_handle, char *buf, int nbytes) {
    /*
	    write `nbytes` of data from `buf` to file pointed by `file_handle` starting at current offset
	*/
	// if file_handle is -1, then return -1
	if (file_handle == -1){
		return -1;
	}
	struct filehandle_t *file = &file_handle_array[file_handle];
	if (file->inode_number == -1){
		return -1;
	}
	// Begin allocating data blocks. If there is an error, then remove all the assigned data blocks. //
	int start_blocknum = file->offset/BLOCKSIZE;
	int indexinblock = file->offset%BLOCKSIZE;
	struct inode_t *inodeptr = (struct inode_t *)malloc(sizeof(struct inode_t));
	int left = nbytes;
	simplefs_readInode(file->inode_number, inodeptr);
	int curr_index_start_buf = 0;
	char init_data[BLOCKSIZE+1];
	init_data[BLOCKSIZE] = '\0';
	simplefs_readDataBlock(inodeptr->direct_blocks[start_blocknum], init_data);
	int off = file->offset;
	while (left) {
		int block_num = off / BLOCKSIZE;
		int index_in_block = off % BLOCKSIZE;
		int bytes_to_write = MIN(left,BLOCKSIZE-index_in_block);
		char data[BLOCKSIZE+1];
		data[BLOCKSIZE] = '\0';
		for (int i = 0; i < BLOCKSIZE; i++){
			data[i] = '\0'; // need to make sure that the array is empty!
		}
		if (block_num >= 4){
			// Time to remove written data :(
			// Block Number can't be greater than 4
			for (int i = start_blocknum; i < block_num; i++){
				simplefs_freeDataBlock(inodeptr->direct_blocks[i]);
			}
			simplefs_writeDataBlock(inodeptr->direct_blocks[start_blocknum], init_data);
			free(inodeptr);
			return -1;	
		}
		if (inodeptr->direct_blocks[block_num] == -1){
			inodeptr->direct_blocks[block_num] = simplefs_allocDataBlock();
			if (inodeptr->direct_blocks[block_num] == -1 || block_num >= 4){
				// Time to remove written data :(
				for (int i = start_blocknum; i < block_num; i++){
					simplefs_freeDataBlock(inodeptr->direct_blocks[i]);
				}
				simplefs_writeDataBlock(inodeptr->direct_blocks[start_blocknum], init_data);
				free(inodeptr);
				return -1;
			}
		}
		if (index_in_block > 0){
			simplefs_readDataBlock(inodeptr->direct_blocks[block_num], data);
		}
		for (int i = 0; i < bytes_to_write; i++) {
			data[index_in_block + i] = buf[curr_index_start_buf+i];
		}
		simplefs_writeDataBlock(inodeptr->direct_blocks[block_num], data);
		off += bytes_to_write;
		left -= bytes_to_write;
		curr_index_start_buf += bytes_to_write;
		
	}
    if (left == 0) {
		inodeptr->file_size += nbytes;
		simplefs_writeInode(file->inode_number, inodeptr);
		free(inodeptr);
        return 0;
    }
	free(inodeptr);
    return -1;
}

int simplefs_seek(int file_handle, int nseek) {
    /*
       Increase `file_handle` offset by `nseek`
    */
   	if (file_handle == -1){
		return -1;
	}
    struct filehandle_t *file = &file_handle_array[file_handle];
    int new_offset = file->offset + nseek;

    if (new_offset >= 0 && new_offset < MAX_FILE_SIZE * BLOCKSIZE) {
        file->offset = new_offset;
        return 0;
    }
    return -1;
}
