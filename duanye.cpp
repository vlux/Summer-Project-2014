/*
 * memcache.cpp
 *
 *  Created on: Jul 24, 2014
 *      Author: duanye
 */

#include "memcache.h"
memcache_rnd::memcache_rnd(size_t block_size, size_t cache_size, uint16_t node,
		uint16_t port, const char *filepath) {

	//For performance, block size should be aligned to power of 2 and at least one page.
	this->block_hbit = high_bit(block_size);
	if (this->block_hbit < 12)
		this->block_hbit = 12;
	this->block_size = 1 << this->block_hbit;
	this->block_vbits = this->block_size - 1;

	//Calculate number of blocks, and align cache_size.
	this->block_count = cache_size >> block_hbit;
	if (!this->block_count)
		this->block_count = 1;

	cache_size = this->block_count << this->block_hbit;

	//Initialize spin_count
	this->spin_count = 0;

	//Create flag array, map array.
	this->cache_flags = (uint8_t *) malloc(this->block_count * sizeof(uint8_t));
	memset(this->cache_flags, 1, this->block_count * sizeof(uint8_t));

	this->cfmap = (off_t *) malloc(this->block_count * sizeof(off_t));
	this->fcmap.clear();

	//Connect to server.
	this->server_epd = scif_connect_s(node, port);

	//Register RMA window, and put file path into window.
	this->cache = scif_register_window_s(this->server_epd, 0,
			this->block_size * this->block_count);
	strcpy((char *) this->cache, filepath);

	//Notify server to map the buffer by sending a message containing cache size.
	scif_send(this->server_epd, &cache_size, sizeof(size_t), 0);

	//Wait for server reporting file size.
	scif_recv(this->server_epd, &this->file_size, sizeof(size_t),
			SCIF_RECV_BLOCK);
}

size_t memcache_rnd::read(void *buffer, off_t file_offset, size_t data_size) {
	//Judge if there is an overflow.
	data_size = min_size(data_size, this->file_size - file_offset);

	//Return value.
	size_t sum = 0;

	//Copy data to buffer.
	while (data_size > 0) {

		//Calculate file block and fetch the block if it is not cached.
		off_t file_block = file_offset >> this->block_hbit;
		if (!this->cached(file_block))
			this->async_fetch(file_block);

		//Calculate cache block and offset in block and data size to copy in this block.
		off_t cache_block = this->fcmap[file_block];

		//Mark the cache block before reading it.
		this->mark(cache_block, MEMCACHE_READING);

		off_t off_inblock = file_offset & this->block_vbits;
		size_t cpy = min_size(data_size, this->block_size - off_inblock);

		//Fetch next block if this block is half read.
		if (off_inblock + cpy > (this->block_size >> 1)
				&& !this->cached(file_block + 1)) {
			this->async_fetch(file_block + 1);
		}

		//Wait this block ready if it is fetching.
		if (this->cache_flags[cache_block] & MEMCACHE_FETCHING)
			this->sync_block(cache_block);

		//Copy data and update pointers.
		off_t off_incache = (cache_block << this->block_hbit) + off_inblock;
		memcpy(buffer, this->cache + off_incache, cpy);
		file_offset += cpy;
		data_size -= cpy;
		buffer += cpy;
		sum += cpy;

		//Unmark the block when reading is done.
		this->unmark(cache_block, MEMCACHE_READING);
	}
	return sum;
}

bool memcache_rnd::cached(off_t file_block_no) {
	return this->fcmap.find(file_block_no) != this->fcmap.end();
}

void memcache_rnd::async_fetch(off_t file_block_no) {
	if (this->e_free()
			&& (file_block_no << this->block_hbit) < this->file_size) {
		//Find a block to use.
		off_t cache_block = this->find();

		//Fill a request and send the request.
		SCIF_DATA_REQUEST req;
		req.type = 'r';
		req.data_size = this->block_size;
		req.file_offset = file_block_no << this->block_hbit;
		req.cache_offset = cache_block << this->block_hbit;
		scif_send(this->server_epd, &req, sizeof(SCIF_DATA_REQUEST), 0);

		//Mark that block's FETCHING flag, and unmark it's UNUSED flag.
		this->mark(cache_block, MEMCACHE_FETCHING);
		this->unmark(cache_block, MEMCACHE_UNUSED);

		//Update mapping.
		this->map_put(file_block_no, cache_block);
	}
}

void memcache_rnd::sync_block(off_t cache_block_no) {
	SCIF_DATA_REQUEST req;
	//This function receives response message from server one by one.
	//until the target block is fetched.
	while (this->cache_flags[cache_block_no] & MEMCACHE_FETCHING) {
		//Receive one request, and unmark the FETCHING flag.
		scif_recv(this->server_epd, &req, sizeof(SCIF_DATA_REQUEST),
				SCIF_RECV_BLOCK);
		this->unmark(req.cache_offset >> this->block_hbit, MEMCACHE_FETCHING);
	}
}

void memcache_rnd::map_put(off_t file_block_no, off_t cache_block_no) {
	//Unmap last file block if exists.
	if (!(this->cache_flags[cache_block_no] & MEMCACHE_UNUSED)) {
		off_t last = this->cfmap[cache_block_no];
		this->fcmap.erase(last);
	}

	//Update both maps.
	this->cfmap[cache_block_no] = file_block_no;
	this->fcmap[file_block_no] = cache_block_no;
}

void memcache_rnd::mark(off_t cache_block_no, uint8_t flag) {
	//Calculate old spin state.
	bool spo = this->cache_flags[cache_block_no] & MEMCACHE_SPIN;

	//Do marking.
	this->cache_flags[cache_block_no] |= flag;

	//If the block's spin state changed update spin count.
	bool spn = this->cache_flags[cache_block_no] & MEMCACHE_SPIN;
	this->spin_count += spo ^ spn;
}

void memcache_rnd::unmark(off_t cache_block_no, uint8_t flag) {
	//Calculate old spin state.
	bool spo = this->cache_flags[cache_block_no] & MEMCACHE_SPIN;

	//Do marking.
	this->cache_flags[cache_block_no] &= (~flag);

	//If the block's spin state changed update spin count.
	bool spn = this->cache_flags[cache_block_no] & MEMCACHE_SPIN;
	this->spin_count -= spo ^ spn;
}

off_t memcache_rnd::find() {
	off_t p = rand64() % this->block_count;
	while (this->cache_flags[p] & MEMCACHE_SPIN)
		p = rand64() % this->block_count;
	return p;
}

bool memcache_rnd::e_free() {
	return this->spin_count < this->block_count;
}
