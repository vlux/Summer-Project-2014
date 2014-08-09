CC = icc
CFLAGS = -std=c++0x -lscif -g
OBJS = common.o memcache.o
BINS = scif-file-daemon.out memcache-test.out scif_fs.out
all:	$(OBJS) $(BINS)
#	scp $(BINS) dxh@micserver:SCIF_FUSE_test
memcache.o:			memcache.cpp common.o
	$(CC) $(CFLAGS) -c memcache.cpp
common.o:			scif_common.cpp
	$(CC) $(CFLAGS) -c scif_common.cpp -o common.o
scif-file-daemon.out:	scif_file_daemon.cpp common.o
	$(CC) $(CFLAGS) -pthread scif_file_daemon.cpp common.o -o scif-file-daemon.out 
memcache-test.out:		memcache_test.cpp common.o memcache.o
	$(CC) $(CFLAGS) memcache_test.cpp memcache.o common.o -o memcache-test.out
scif_fs.out:	scif_fuse.cpp common.o memcache.o
	$(CC) $(CFLAGS) -lfuse common.o memcache.o scif_fuse.cpp -o scif_fs.out -D_FILE_OFFSET_BITS=64
clean:
	rm $(OBJS) $(BINS)
