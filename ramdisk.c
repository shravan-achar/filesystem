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

static int ramdisk_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
    struct inode * ino = 0, * ino_c = 0;
    uint16_t child_ino;

    ino = get_inode_from_path(path);
    
    /* Get all children */
    /* TODO: Convert all pathnames to filenames */
    if (ino) {
        if (ino->ftype == 1) /* Its a file */
        {
            filler(buf, ino->path_name, NULL, 0);
        } else { 
            filler(buf,".", NULL, 0);
            filler(buf,"..", NULL, 0);
            
            child_ino = ino->child_ino;
            while (child_ino != 0) { /* Root inode can not be child of anyone */
                ino_c = get_inode_from_number(child_ino);
                filler(buf, ino_c->path_name, NULL, 0);
                child_ino = ino_c->sibling_ino;
            }
        }
        return 0;
    }
    
    return -1;

}

static int ramdisk_getattr(const char* path, struct stat* stbuf) 
{
    struct inode * ino = 0;

    struct fuse_context* fc = fuse_get_context();

    memset(stbuf, 0, sizeof(struct stat));
    stbuf->st_dev = 13;
    if ((ino = get_inode_from_path(path)) > 0) {
        if (ino->ftype == 1) {
            stbuf->st_mode = S_IFREG | 0644;
            stbuf->st_nlink = 1;
            stbuf->st_size = ino->size;
            stbuf->st_blocks = ino->num_blocks;
            stbuf->st_blksize = BLOCK_SIZE;
        } else {
            stbuf->st_mode = S_IFDIR | 0755;
            stbuf->st_nlink = 2;
            stbuf->st_size = sizeof(struct inode);
            stbuf->st_blocks = 0;
            stbuf->st_blksize = sizeof(struct inode);

        }
        stbuf->st_uid = fc->uid;
        stbuf->st_gid = fc->gid;
        stbuf -> st_atime = stbuf -> st_ctime = stbuf -> st_mtime = time(0);
    } else {
        return -ENOENT;
    }
    return 0;
}

static int ramdisk_rmdir(const char * path) 
{
    struct inode * ino_p = 0, * par_ino_p = 0;
    struct ino_metadata * ino_m = 0;
    
    ino_p = get_inode_from_path(path);
    if (!ino_p) return -ENOENT;
    
    par_ino_p = get_inode_from_number(ino_p->parent_ino);
    if (!par_ino_p) return -ENOENT; /* Something is really wrong */

    if (par_ino_p->child_ino != 0) return -EISDIR;

    remove_from_child_list(par_ino_p, ino_p->ino);

    /*Remove from file*/
    remove_inode_from_file(ino_p);

    ino_m = get_metadata_from_num(ino_p->ino);
    update_metadata_del(ino_m, ino_p->ino);

    return 0;

}

static int ramdisk_mkdir(const char * path, mode_t mode)
{
    /* Create inode entry*/
    struct inode * ino_p = 0, * par_ino = 0;
    struct ino_metadata * ino_m = 0;
    uint16_t ino_num = 0;
    
    ino_p = get_inode_from_path(path); 
    if (ino_p) handle_error("Dir exists");

    par_ino = get_inode_from_number(ino_p->parent_ino);
    if (!par_ino) handle_error("Invalid path");

    ino_num = find_free_inode_num();
    if (ino_num) {
        ino_p = (struct inode *) calloc(1, sizeof(struct inode));
        ino_p->ino = ino_num;
        ino_p->parent_ino = par_ino->ino;
        strncpy(ino_p->path_name, path, PATH_MAX);
        ino_p->size = sizeof(struct inode);
        ino_p->ftype = 0; /* Rest are all zero */
        add_to_child_list (par_ino, ino_num);
        
        /* Write to file */
        add_inode_to_file(ino_p);
        
        /*Update metadata */
        ino_m = get_metadata_from_num(ino_num);
        update_metadata_add(ino_m, ino_num);
        free(ino_p);
        return 0; 
    }

    /* No free blocks if it comes here */
    return -ENOMEM;
}

void remove_inode_from_file (struct inode * ino_p) 
{
    int pageno = 0;
    int index = 0;

    void * addr = map->addr;

    pageno = (ino_p->ino) / 7;
    index = (ino_p->ino % 7) + 1;

    addr = (void *)((char *)addr + pageno * PAGE_SIZE + index * sizeof(struct inode));
    memset(addr, 0, sizeof(struct inode));
}

void add_inode_to_file (struct inode * ino_p)
{
    int pageno = 0;
    int index = 0;
    void * addr = map->addr;

    pageno = (ino_p->ino) / 7;
    index = (ino_p->ino % 7) + 1; //Skipping metadata on top of the page

    addr = (void *) ((char *)addr + pageno * PAGE_SIZE + index * sizeof(struct inode)); 
    memcpy(addr, (void *)ino_p, sizeof(struct inode));
}

void add_to_child_list (struct inode * par_ino, uint16_t new_child)
{
    uint16_t child = 0;
    struct inode * ino_c = 0;
    child = par_ino->child_ino;

    while (child != 0) {
        ino_c = get_inode_from_number(child);
        child = ino_c->sibling_ino;
    }

    if (ino_c) { 
        ino_c->sibling_ino = new_child;
    } else {
        par_ino->child_ino = new_child;
    }

}

void remove_from_child_list (struct inode * par_ino, uint16_t deleted_child)
{
    uint16_t child = 0;
    struct inode * ino_c = 0;
    child = par_ino->child_ino;

    while (child != deleted_child) {
        ino_c = get_inode_from_number(child);
        child = ino_c->sibling_ino;
    }

    if (ino_c) { 
        ino_c->sibling_ino = 0;
    } else {
        par_ino->child_ino = 0;
    }
}

void update_metadata_add (struct ino_metadata * ino_m, uint16_t ino)
{
    int bitpos = 0;
    bitpos = ino % 15; /* 15 inodes per page */

    if (!((ino_m->bitmap >> bitpos) & 1)) {
        ino_m->bitmap |= 1 << bitpos;
    }

}

void update_metadata_del (struct ino_metadata * ino_m, uint16_t ino)
{
    int bitpos = 0;
    bitpos = ino % 15; /* 15 inodes per page 0-14 */

    if ((ino_m->bitmap >> bitpos) & 1) {
        ino_m->bitmap &= 1 << bitpos;
    }
}

struct ino_metadata * get_metadata_from_num (uint16_t ino)
{
    int page = 0;
    void * addr = map->addr;
    struct ino_metadata * ino_m = 0;
    page = (ino / 15); /* 15 Inodes per page */

    ino_m = (struct ino_metadata *) ((char *)addr + (page * PAGE_SIZE));
    return ino_m;
}

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
int find_free_inode_num() {
    void * addr = map->addr;
    struct ino_metadata *ino_m = 0;
    int pageno = 0;
    int bitpos = 0;

    while(pageno < metadata_pages)
    {
        ino_m = (struct ino_metadata *)((char *)addr + (pageno * PAGE_SIZE));
        for (bitpos = 0; (!((ino_m->bitmap >> bitpos) & 1)) && (bitpos < 15); bitpos++) 
        {
            return ((pageno * 15) + bitpos);

        }
    }

    return 0;

}

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
    if (get_inode_from_path(path))
        return 0; 
    else return -1;
}

struct inode * get_inode_from_number(int ino)
{
    struct inode *ino_p = 0;
    int page_no = ino / 15; // 15 inodes per page
    int index = (ino % 15) + 1; //Skipping metadata
    ino_p = (struct inode *) ((char *)root_inode + (page_no * PAGE_SIZE) + (index * sizeof(struct inode)));
    return ino_p; 
}

struct inode * get_inode_from_path(const char * path)
{
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
                return ino;
            } 

        }
        pages++;
    }
    return NULL;
}

static struct fuse_operations ramdisk_oper = {
    //.init        = ramdisk_init,
    //.destroy     = ramdisk_destroy,
    .read = ramdisk_read,
    .open = ramdisk_open,
    .write = ramdisk_write,
    .readdir = ramdisk_readdir,
    .getattr     = ramdisk_getattr,
    .mkdir       = ramdisk_mkdir,
    .rmdir       = ramdisk_rmdir,
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
        fd = open(argv[3], O_RDWR);
        if (fd < 0) {
            if (errno == ENOENT) {
                /*Create the file*/
                fd = open(argv[3], O_RDWR | O_CREAT, 0666);
            } else {
                handle_error(strerror(errno));
            }
            image_read = 0;
        } else {
            image_read = 1;
        }
    } else if (argc == 3) {
        /* If the optional filename argument is not provided, we use a temporary file */
        /* A tempfile is mmaped */
        image_read = 0;
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
/* BUG: Cannot reallocate more metadata pages if fs size is increased */
void init_metapages(int page) {
    struct ino_metadata * ino_meta;
    void *addr = map->addr;

    while (page < metadata_pages) {
        ino_meta = calloc(1, sizeof(struct ino_metadata));
        memcpy((char *)addr + (page * PAGE_SIZE), ino_meta, sizeof(struct ino_metadata));
        free(ino_meta);
        page++;
    }
}

void init_datapages(int page) {
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
    if (image_read) {
        data_pages = pages - ino_m->metadatapages;
        init_datapages(ino_m->datapages); /* Extra pages if any */
    } else {
        ino_m->bitmap |= 1;
        ino_m->metadatapages = metadata_pages;
        ino_m->datapages = data_pages;
    }
}

void init_root_ino () {
    void * addr = map->addr;
    root_inode = (struct inode *)((char *)addr + sizeof(struct inode));
    strcpy(root_inode->path_name, "/");
    /* Size of directory = size of inode struct */
    root_inode->size = sizeof(struct inode);
}

void init_globals(int size) {
    gsize = size;
    pages = gsize / PAGE_SIZE;
    if (!image_read) {
        metadata_pages = pages / 128; /* for a 512 MB fs, 4MB is for metadata. Min fs size is 512 KB */
        data_pages = pages - metadata_pages;
        init_metapages(0);
        init_root_ino();
        init_root_ino_meta();
        init_datapages(metadata_pages);
    } else {
        init_root_ino();
        init_root_ino_meta();
    }
    space_waste = data_pages * 511 + metadata_pages * 254;
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
