/*
 * memcache.h
 *
 *  Created on: Jul 24, 2014
 *      Author: duanye
 */

#ifndef MEMCACHE_H_
#define MEMCACHE_H_

#include "scif_common.h"
#include <scif.h>

#include <unordered_map>
#include <cstring>
#include <algorithm>
#define MEMCACHE_UNUSED 1
#define MEMCACHE_FETCHING 2
#define MEMCACHE_READING 4
#define MEMCACHE_SPIN 6
using std::unordered_map;

class memcache_rnd{
public:
	memcache_rnd(size_t block_size, size_t block_count, uint16_t node, uint16_t port, const char* filepath);
	size_t read(void * buffer, off_t file_offset, size_t data_size);
//private:
	bool cached(off_t file_block_no);
	void async_fetch(off_t file_block_no);
	void sync_block(off_t cache_block_no);
	void map_put(off_t file_block_no, off_t cache_block_no);
	void mark(off_t cache_block_no, uint8_t flag);
	void unmark(off_t cache_block_no, uint8_t flag);
	off_t find();
	bool e_free();

	//Variables for caching data.
	void *cache;
	uint8_t *cache_flags;

	//Variable recording number of spin block, avoiding dead lock.
	size_t spin_count;

	scif_epd_t server_epd;

	size_t block_size, block_count, file_size;

	//Variable for easy calculating.
	uint8_t block_hbit;//Number of highest bit of block_size, for calculating block number from offset.
	size_t block_vbits;//block_size - 1, for calculating offset inside a block.

	//Variables for cache mapping.
	unordered_map<off_t, off_t> fcmap;
	off_t *cfmap;
};



#endif /* MEMCACHE_H_ */
