all:
		gcc -g -Wall ramdisk.c `pkg-config fuse --cflags --libs` -o ramdisk

clean:
		rm ramdisk
