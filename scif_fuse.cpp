/*
 * fuse_static_test.cpp
 *
 *  Created on: Jul 31, 2014
 *      Author: duanye
 */



#define FUSE_USE_VERSION 26
#include <cstdio>
#include <cstring>
#include <fuse.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <cstdlib>
#include "scif_common.h"
#include "memcache.h"


int servnode, servport;
char *basepath;

#define send_path() char * fullpath = strcat_malloc(basepath, path); scif_send_str(epd, fullpath); free(fullpath)
scif_epd_t start_directory_session(char cmd){
	size_t tmp = 0;
	scif_epd_t epd = scif_connect_s(servnode, servport);
	scif_send(epd, &tmp, sizeof(size_t), SCIF_SEND_BLOCK);
	scif_send(epd, &cmd, 1, SCIF_SEND_BLOCK);
	return epd;
}
static int scif_getattr(const char *path, struct stat *stbuf){
	scif_epd_t epd = start_directory_session('a');
	send_path();
	if (scif_recv(epd, stbuf, sizeof(struct stat), SCIF_RECV_BLOCK) == -1)
		return -ENOENT;
	else
		return 0;
}
static int scif_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi){
	scif_epd_t epd = start_directory_session('l');
	send_path();
	char fn[256];
	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);
	while (scif_recv(epd, fn, 256, SCIF_RECV_BLOCK) != -1){
		filler(buf, fn, NULL, 0);
		puts(fn);
	}
	return 0;
}

static int scif_fopen(const char *path, struct fuse_file_info *fi){
	char *fullpath = strcat_malloc(basepath, path);
	if ((fi->flags & 3) == O_RDONLY){
		fi->fh = (int64_t)new memcache_rnd(4096 * 256, 16, servnode, servport, fullpath);
		return 0;
	}
	free(fullpath);
	return -EACCES;
}

static int scif_fread(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi){
	return ((memcache_rnd*)fi->fh)->read(buf, offset, size);
}

static struct fuse_operations fuseopper;
int main(int argc, char *argv[]){
	fuseopper.getattr = scif_getattr;
	fuseopper.readdir = scif_readdir;
	fuseopper.open = scif_fopen;
	fuseopper.read = scif_fread;
	basepath = argv[1];
	servnode = 0;
	servport = 80;
	return fuse_main(argc - 1, argv + 1, &fuseopper, NULL);
}
