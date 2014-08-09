/*
 * cachetest.cpp
 *
 *  Created on: Jul 25, 2014
 *      Author: duanye
 */



#include "memcache.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#define BATCH
char cbuf[65536000], dbuf[65536000];
void dumpbuf(char* buf, int size){
	for (int i = 0; i < size; i ++)
		printf("%02x", buf[i] & 0xff);
	puts("");
}
int main(int argc, char** argv){
	srand(time(NULL));
	int offset, size;
	memcache_rnd *cache;
	cache = new memcache_rnd(4096, 4096 * 4096, 0, 80, argv[1]);
	FILE *f = fopen(argv[1], "rb");
	while (1){
#ifdef BATCH
		offset = rand() % cache->file_size;
		size = rand() % 65536000;
		printf("%d %d\n", offset, size);
#else
		scanf("%d%d", &offset, &size);
#endif
		cache->read(cbuf, offset, size);
//		dumpbuf(cbuf, size);
		fseek(f, offset, SEEK_SET);
		fread(dbuf, 1, size, f);
//		dumpbuf(dbuf, size);
		for (int i = 0; i < size; i ++)
			if (cbuf[i] != dbuf[i]){
				printf("%d\n", i);
				break;
			}
		int res = memcmp(cbuf, dbuf, size);
		puts(res ? "Fault" : "OK");
		if (res){
			break;
		}
	}
	return 0;
}
