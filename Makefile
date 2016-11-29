all:
		gcc -g ramdisk.c `pkg-config fuse --cflags --libs` -o ramdisk

clean:
		rm ramdisk
