/*
  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.
*/

#include <sys/types.h>
#include <sys/uio.h>
#include <sys/ioctl.h>
#include <stdint.h>

#define PAGE_SIZE 4096    /*Bytes */
#define BLOCK_SIZE 512    /*Bytes */
#define PATH_MAX 127      /* Bytes */

#define handle_error(msg) \
        do { perror(msg); return(-1); } while (0)

int gsize; /* Global Size of filesystem*/
int pages; /* size of file_system / page_size */
int metadata_pages; /* Metadata having inodes */
/* Total storage used for inodes = metadata_pages * 4096 = 2MB*/
int data_pages; /* pages - metadata_pages */

struct ino_metadata {
    uint16_t bitmap; /*MSB is don't care */
    /* We lose 254 bytes per metapage here */
};

uint16_t max_inode; /*metadata_pages * (PAGE_SIZE - sizeof(inode)) / sizeof(inode) */ 

struct page_metadata {
    uint8_t bitmap; /* PAGE_SIZE / BLOCK_SIZE. MSB bit is don't care 
                    If the bit is set then the corresponding block is occupied*/
    /* We lose 511 bytes per page here */
};

int space_waste;

struct inode {
    uint8_t path_name[PATH_MAX];
    uint8_t  ftype; /* Directory or File */
    uint16_t ino; /* Inode number */
    size_t size; /* Size in bytes */
    uint16_t child_ino; /* First child's inode number */
    uint16_t parent_ino; /* Parent inode number*/
    uint16_t sibling_ino; /* Next inode sharing the same parent */
    uint32_t num_blocks; /* Number of data blocks */
    uint32_t block_list[24]; /* list of block numbers containing data
                                Numbering starts from 1 */
    uint8_t  itype;    /* Direct or indirect inode */
    uint16_t next_ino; /* If itype is set then this is valid 
                          Points to the next inode */
};  /* Size of inode is 256 */

struct inode * root_inode;

/* Memory map struct*/
struct mem_map {
    void * addr;
    off_t size;
};

struct mem_map * map;
