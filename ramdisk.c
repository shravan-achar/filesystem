/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.

  gcc -Wall ramdisk.c `pkg-config fuse --cflags --libs` -o ramdisk
*/

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

#include "ramdisk.h"


static int ramdisk_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    int rc = 0;
    size_t sz = size;
    size_t new_size = 0;
    struct inode *ino = get_inode_from_number((int) fi->fh);
    if (size == 0) return 0;

    if (offset >= ino->size) {
        offset = ino->size;
    } 
    if (offset + sz < ino->size) {
        new_size = sz;   /* Only a part of file is updated */
    } else if (offset + sz > ino->size) {
        new_size = offset + sz;   /* Grow the file */
    } else {
        new_size = sz;  /* Size remains the same but contents may be updated*/
    }
    
    /*Allocate blocks for the new_size*/
    rc = allocate_blocks(ino, ino->size, new_size);
    if (rc < 0) handle_error("Write Failed");
    ino->size = new_size;
    
    /* Move to offset bytes in file */
    int offset_block_index = (offset / BLOCK_SIZE);
    off_t offset_within_block = offset % BLOCK_SIZE;
    
    rc = write_bytes_from_oft(ino, (char *)buf, offset_block_index, offset_within_block, sz);
    return rc;
}

static int write_bytes_from_oft(struct inode *ino, char * buf, int offset_block_ind, off_t offset, size_t bytes)
{
    int block = 0, i;
    size_t bytes_written, min;
    char rcv_buf[BLOCK_SIZE];
    memset(rcv_buf, 0, BLOCK_SIZE);

    if (offset) {
        /* If bytes can not outfill the block */
        if (bytes < (BLOCK_SIZE - (offset % BLOCK_SIZE))) {
            min = bytes;
        } else {
            min = BLOCK_SIZE - (offset % BLOCK_SIZE);
        }
        fetch_data_from_block(rcv_buf, ino->block_list[offset_block_ind], BLOCK_SIZE);
        memcpy(rcv_buf + offset, buf, min);
        write_data_to_block(rcv_buf, ino->block_list[offset_block_ind], offset + min);
        bytes = bytes - min;
        bytes_written = min;
        if (bytes == 0) return bytes_written;
    }
    
    for (i = offset_block_ind + 1; i < ino->num_blocks; i++)
    {
        block = ino->block_list[i];
        if (bytes / BLOCK_SIZE) {
            /*If it is non-zero, this is not the last block*/
            write_data_to_block(buf + bytes_written, block, BLOCK_SIZE);
            bytes -= BLOCK_SIZE;
            bytes_written += BLOCK_SIZE;
        } else {
            /* This is the last block*/
            write_data_to_block(buf + bytes_written, block, bytes % BLOCK_SIZE);
            bytes -= bytes % BLOCK_SIZE;
            bytes_written += bytes % BLOCK_SIZE;
        }
    }
    return bytes_written;
}

static int allocate_blocks(struct inode *ino, size_t old_size, size_t new_size)
{
    int extra_blocks = 0, new_full_blocks = 0;
    char extra_needed = 0;
    int blocks = 0;
    int block = 0;
    int rc = 0;
    if (new_size <= old_size) {
        return 0;
    } else {
        if ((old_size / BLOCK_SIZE) < ino->num_blocks) {
            /* This means that the last block is partially filled*/
            if (new_size > old_size + (BLOCK_SIZE - (old_size % BLOCK_SIZE))) {
                /*Atleast one extra block needed*/
                extra_needed = 1;
            }
        } else {
            /*All blocks are fully filled or no blocks are present. Need to allocate */
            extra_needed = 1;
        }
        
        if(extra_needed) {
            new_full_blocks = new_size / BLOCK_SIZE;
            if (new_size % BLOCK_SIZE) {
                extra_blocks = new_full_blocks - ino->num_blocks + 1;
            } else {
                extra_blocks = new_full_blocks - ino->num_blocks;
            }
            
            while (blocks < extra_blocks) {
                block = find_free_block_num();
                if (block < 0) handle_error("No more space left");
                ino->block_list[ino->num_blocks++] = block;
                blocks++;
            }
        }
    }

    return rc;
}

static int ramdisk_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) 
{
    int block = 0;
    size_t sz = size;
    size_t bytes_read = 0;
    char rcv_buf[BLOCK_SIZE];
    memset(rcv_buf, 0, BLOCK_SIZE);
    struct inode *ino = get_inode_from_number((int) fi->fh);
    if (offset >= ino->size) return 0;
    if (size == 0) return 0;

    /* If size > BLOCK_SIZE then num_blocks > 1 */
    while (block < ino->num_blocks - 1) {
	fetch_data_from_block(rcv_buf, block, BLOCK_SIZE);
        memcpy(buf + bytes_read, rcv_buf, BLOCK_SIZE);
        sz -= BLOCK_SIZE;
        bytes_read += BLOCK_SIZE;
        block++;
        /* If the next block exists, then the assumption is that the current block is full */
    }

    memset(rcv_buf, 0, BLOCK_SIZE);
    /* Last block */
    fetch_data_from_block(rcv_buf, ino->block_list[ino->num_blocks - 1], sz);
    memcpy(buf + bytes_read, rcv_buf, sz);
    bytes_read += sz;
    return bytes_read;
}

/* Given the offset, returns the block number of the offset 
 * If offset is more than the size then this returns a new block number*/
/*
int fetch_offset_blocknum(struct inode * ino, off_t offset)
{
    int block;
    if ((offset / BLOCK_SIZE) >= ino->num_blocks) {
        block = find_free_block_num();
        ino->block_list[ino->num_blocks++] = block;
    } else {
        block = ino->block_list[(offset / BLOCK_SIZE)];
    }
    return block;
}
*/

int find_free_block_num() {
    void * addr = map->addr;
    struct page_metadata *pgm = 0;
    int pageno = metadata_pages;
    int bitpos = 0;

    while(pageno < data_pages) {
        pgm = (struct page_metadata *)((char *)addr + (pageno * PAGE_SIZE));
        for (bitpos = 0; (!((pgm->bitmap >> bitpos) & 1)) && (bitpos < 7); bitpos++) 
        {
            return (((pageno - metadata_pages) * 7) + bitpos);

        }
        pageno++;
    }
    return -1;
}

void fetch_data_from_block(char * buf, int blocknum, size_t size)
{
    memset(buf, 0, BLOCK_SIZE);
    int pageno = metadata_pages + (blocknum / 7);
    int index = (blocknum % 7) + 1; //Skipping page metadata
    void * addr = map->addr;
    char * bl_p = (char *) addr + (pageno * PAGE_SIZE) + (index * BLOCK_SIZE);
    memcpy(buf, bl_p, size);
}

void write_data_to_block(char *buf, int blocknum, size_t size)
{
    int pageno = metadata_pages + (blocknum / 7);
    int index = (blocknum % 7) + 1; //Skipping page metadata
    void * addr = map->addr;
    char * bl_p = (char *) addr + (pageno * PAGE_SIZE) + (index * BLOCK_SIZE);
    memcpy(bl_p, buf, size);
}

static int ramdisk_open(const char *path, struct fuse_file_info *fi) 
{
    if((fi->flags & 3) != O_RDONLY) return -EACCES;

    struct ino_metadata *ino_m = 0;
    struct inode *ino = 0;
    void * addr = map->addr;
    int pages = 0;
    int bitpos = 0;

    while (pages < metadata_pages) {
        ino_m = (struct ino_metadata *) ((char *)addr + (pages*PAGE_SIZE));
        if (ino_m->bitmap == 0) 
        {
            pages++;
            continue;
        }
        for (bitpos = 0; ((ino_m->bitmap >> bitpos) & 1) && (bitpos < 15); bitpos++) 
        {
            ino = (struct inode *)((char *) root_inode + 
                                   (bitpos * sizeof(struct inode))); 
        
            if(!strcmp(ino->path_name, path)) {
                fi->fh = (uint64_t) ino->ino;
                return 0;
            } 

        }
        pages++;
    }
    return -1; /*File not found*/
}

struct inode * get_inode_from_number(int ino)
{
    struct inode *ino_p = 0;
    int page_no = ino / 15; // 15 inodes per page
    int index = (ino % 15) + 1; //Skipping metadata
    ino_p = (struct inode *) ((char *)root_inode + (page_no * PAGE_SIZE) + (index * sizeof(struct inode)));
    return ino_p; 
}


static struct fuse_operations ramdisk_oper = {
    //.init        = ramdisk_init,
    //.destroy     = ramdisk_destroy,
    .read = ramdisk_read,
    .open = ramdisk_open,
    .write = ramdisk_write,
/*    .getattr     = ramdisk_getattr,
    .fgetattr    = ramdisk_fgetattr,
    .access      = ramdisk_access,
    .readlink    = ramdisk_readlink,
    .readdir     = ramdisk_readdir,
    .mknod       = ramdisk_mknod,
    .mkdir       = ramdisk_mkdir,
    .symlink     = ramdisk_symlink,
    .unlink      = ramdisk_unlink,
    .rmdir       = ramdisk_rmdir,
    .rename      = ramdisk_rename,
    .link        = ramdisk_link,
    .chmod       = ramdisk_chmod,
    .chown       = ramdisk_chown,
    .truncate    = ramdisk_truncate,
    .ftruncate   = ramdisk_ftruncate,
    .utimens     = ramdisk_utimens,
    .create      = ramdisk_create,
    .open        = ramdisk_open,
    .read        = ramdisk_read,
    .write       = ramdisk_write,
    .statfs      = ramdisk_statfs,
    .release     = ramdisk_release,
    .opendir     = ramdisk_opendir,
    .releasedir  = ramdisk_releasedir,
    .fsync       = ramdisk_fsync,
    .flush       = ramdisk_flush,
    .fsyncdir    = ramdisk_fsyncdir,
    .lock        = ramdisk_lock,
    .bmap        = ramdisk_bmap,
    .ioctl       = ramdisk_ioctl,
    .poll        = ramdisk_poll,
#ifdef HAVE_SETXATTR
    .setxattr    = ramdisk_setxattr,
    .getxattr    = ramdisk_getxattr,
    .listxattr   = ramdisk_listxattr,
    .removexattr = ramdisk_removexattr,
#endif
    .flag_nullpath_ok = 0,           */    
};

int init_ramfs (int argc, char *argv[])
{
    struct stat sb;
    //off_t len = 0;
    void * addr = 0;
    int fd = -1;
    off_t size = atoi(argv[2]) * 1024 * 1024;
    printf ("size %llu\n", (long long unsigned int)size);
    memset(&sb, 0, sizeof(struct stat));

    if (argc < 3) {
        fprintf (stderr, "usage: %s <mount_point> <size> [filename]", argv[0]);
        return -1;

    }
    if (argc == 4) {
        /*Filename is provided */
        if ((fd = open(argv[3], O_RDWR)) < 0) {
            if (errno == ENOENT) {
                /*Create the file*/
                fd = open(argv[3], O_RDWR | O_CREAT, 0666);
            } else {
                handle_error(strerror(errno));
            }
        }
    } else if (argc == 3) {
        /* If the optional filename argument is not provided, we use a temporary file */
        /* A tempfile is mmaped */
        if((fd = open("./.tmpfile", O_RDWR | O_CREAT | O_TRUNC, 0666)) < 0) {
            handle_error(strerror(errno));
        }
    }
    if (fstat(fd, &sb) < 0) {
            handle_error(strerror(errno));
    }
    if (sb.st_size < size) {
        if (ftruncate(fd, size) < 0) {
            /* This is bad */
            handle_error(strerror(errno));
        }
    } else if (sb.st_size > size) {
        fprintf(stderr, "File system is larger than specified size. Current size is %uMB\n", (unsigned int) sb.st_size / (1024 * 1024));
        return -1;
    } 
    if ((addr = mmap(0, sb.st_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0)) < 0) {
        handle_error(strerror(errno));
    }
    close(fd);

    map = (struct mem_map *) calloc (1, sizeof(*map));
    if (map < 0) handle_error(strerror(errno));
    map->addr = addr;
    map->size = sb.st_size;
    
    return 0;
}
void init_metapages() {
    int page = 0;
    struct ino_metadata * ino_meta;
    void *addr = map->addr;

    while (page < metadata_pages) {
        ino_meta = calloc(1, sizeof(struct ino_metadata));
        memcpy((char *)addr + (page * PAGE_SIZE), ino_meta, sizeof(struct ino_metadata));
        free(ino_meta);
        page++;
    }
}

void init_datapages() {
    int page = 0;
    struct page_metadata * pg_meta;
    void *addr = map->addr;

    while (page < data_pages) {
        pg_meta = calloc(1, sizeof(struct page_metadata));
        memcpy((char *)addr + (page * PAGE_SIZE), pg_meta, sizeof(struct page_metadata));
        free(pg_meta);
        page++;

    }
}

void init_root_ino_meta() {
    void * addr = map->addr;
    struct ino_metadata * ino_m = (struct ino_metadata *)addr;
    ino_m->bitmap |= 1; 
}

void init_root_ino () {
    void * addr = map->addr;
    root_inode = (struct inode *)((char *)addr + sizeof(struct inode));
    strcpy(root_inode->path_name, "/");
    /* Size of directory = size of inode struct */
    root_inode->size = sizeof(struct inode);
    init_root_ino_meta();
}

void init_globals(int size) {
    gsize = size;
    pages = gsize / PAGE_SIZE;
    metadata_pages = pages / 128; /* for a 512 MB fs, 4MB is for metadata. Min fs size is 512 KB */
    data_pages = pages - metadata_pages;
    space_waste = data_pages * 511 + metadata_pages * 254;
    init_metapages();
    init_datapages();
}

int main(int argc, char *argv[])
{
    printf("Size %lu\n", sizeof(struct inode));
    printf("Ino_meta %lu\n", sizeof(struct ino_metadata));
    printf("page %lu\n", sizeof(struct page_metadata));
    init_ramfs(argc, argv);
    init_globals(atoi(argv[2]));
    //printf("Size %lu\n", sizeof(struct inode));
    return fuse_main(argc, argv, &ramdisk_oper, NULL);
}
