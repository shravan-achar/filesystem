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

static struct fuse_operations ramdisk_oper = {
    //.init        = ramdisk_init,
    //.destroy     = ramdisk_destroy,
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

void init_ramfs (int argc, char *argv[])
{
    struct stat sb;
    off_t len = 0;
    void * addr = 0;
    int fd = -1;
    off_t size = argv[2] * 1024 * 1024;
    printf ("size %llu\n", size);
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
        fprintf(stderr, "File system is larger than specified size. Current size is %uMB\n", sb.st_size / (1024 * 1024));
        return -1;
    } 
    if (addr = mmap(0, sb.st_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0) < 0) {
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
        memcpy(addr + (page * PAGE_SIZE), ino_meta, sizeof(struct ino_metadata));
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
        memcpy(addr + (page * PAGE_SIZE), pg_meta, sizeof(struct page_metadata));
        free(pg_meta);
        page++;

    }
}

void init_root_ino_meta() {
    void * addr = map->addr;
    (struct ino_metadata *) addr -> bitmap |= 1; 
}

void init_root_ino () {
    void * addr = map->addr;
    root_inode = (struct inode *)((char *)addr + sizeof(struct inode));
    snprintf(root_inode->path_name, "/");
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
    init_ramfs();
    init_globals(argv[2]);
    //printf("Size %lu\n", sizeof(struct inode));
    return fuse_main(argc, argv, &ramdisk_oper, NULL);
}
