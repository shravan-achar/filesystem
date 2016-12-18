File system in Userspace using libfuse library (https://github.com/libfuse/libfuse). 
Supported system calls include -
open, close
read, write
creat, mkdir
unlink, rmdir
opendir, readdir
This filesystem minimises program erase (PE) cycles and employs a separate cleaning routine. Best used as a filesystem for flash (Ofcourse, with more modifications ;)).
