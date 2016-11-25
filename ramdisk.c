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
/* If the optional filename argument is not provided, we use a temporary file */
struct stat sb;
off_t len;
char * addr;
int fd;

if (argc < 3) {
fprintf (stderr, "usage: %s <mount_point> <size> [filename]", argv[0]);
return 1;

}

int main(int argc, char *argv[])
{
    printf("Size %lu\n", sizeof(struct inode));
    //printf("Size %lu\n", sizeof(struct inode));
    return fuse_main(argc, argv, &ramdisk_oper, NULL);

}
