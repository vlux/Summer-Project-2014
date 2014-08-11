/*
 * memcache_write.cpp
 *
 *  Created on: Aug 9, 2014
 *      Author: Xuanwu Yue
 */

#include "memcache_write.h"
memcache_wrt::memcache_wrt(size_t block_size, size_t cache_size, uint16_t node,
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

	this->cache_size = this->block_count << this->block_hbit;

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

	//Value the cache_offset
	this->cache_offset = (char *) this-> cache;											//if it's right

}

size_t memcache_wrt::write(void *buffer, off_t file_offset, size_t data_size) {
	//Judge if there is an overflow.
	data_size = min_size(data_size, this->file_size - file_offset);

	//Return value.
	size_t sum = 0;

	//Variable to calculate the number of cache blocks to write
	size_t idx = 0;

	//Variable to remember the offset in file to send to the buffer
	size_t offset_infile = 0;

	//Copy data to buffer to write back
	while (data_size > 0) {

		//Calculate cache_block and fetch the block if it is not cached.
		off_t cache_block = this->cache_offset >> this->block_hbit;
		if (!this->cached(cache_block))
			this->async_fetch(cache_block, file_offset + offset_infile);

		//Calculate the file block
		off_t file_block = this->cfmap[cache_block];

		//Mark the cache block before writing it.
		this->mark(cache_block, MEMCACHE_WRITING);

		//Calculate the offset in block and data size to copy in this block.
		off_t off_inblock = cache_offset & this->block_vbits;
		size_t cpy = min_size(data_size, this->block_size - off_inblock);

		//Fetch next block if this block is half read.
		 if (off_inblock + cpy > (this->block_size >> 1)
		 		&& !this->cached(cache_block + 1)) {
		 	this->async_fetch(cache_block + 1, file_offset + offset_infile + cpy);				//if it's right
		 }

		//Wait this block ready if it is fetching.
		if (this->cache_flags[cache_block] & MEMCACHE_FETCHING)
			this->sync_block(cache_block);

		//Plus the number of cache block to write back
		idx += 1;

		//Copy data and update pointers.
		off_t off_incache = (cache_block << this->block_hbit) + off_inblock;
		memcpy(buffer + offset_infile, this->cache + off_incache, cpy);
		offset_infile += cpy;
		cache_offset = (cache_offset + cpy) % this->cache_size;
		data_size -= cpy;
		buffer += cpy;
		sum += cpy;

		//Judge if it is qualified to write back
		if(idx >= block_count >> 1)
		{
			//Write the buffer back to the host
			scif_vwriteto(this->server_epd, buffer, offset_infile, file_offset,SCIF_RMA_ORDERED);

			//update the file_offset,buffer and idx,
			file_offset += offset_infile;
			idx = 0;
			buffer += offset_infile;

 		}

		//Unmark the block when writing is done.
		this->unmark(cache_block, MEMCACHE_WRITING);
	}
	return sum;
}

//Check if the cache block can be used to write
bool memcache_wrt::cached(off_t cache_block_no) {
	return this->cache_flags[cache_block_no] & MEMCACHE_WRITING;
}

void memcache_wrt::async_fetch(off_t cache_block, off_t file_offset) {
	if (this->e_free()
			&& (file_block_no << this->block_hbit) < this->file_size){
		//Find a block to use.
		off_t file_block = this -> find(file_offset);

		//Fill a request and send the request.
		SCIF_DATA_REQUEST req;
		req.type = 'w';
		req.data_size = this->block_size;
		req.file_offset = file_block_<< this->block_hbit;
		req.cache_offset = cache_block << this->block_hbit;
		scif_send(this->server_epd, &req, sizeof(SCIF_DATA_REQUEST), 0);

		//Mark that block's FETCHING flag, and unmark it's UNUSED flag.
		this->mark(cache_block, MEMCACHE_FETCHING);
		this->unmark(cache_block, MEMCACHE_UNUSED);

		//Update mapping.
		this->map_put(file_block, cache_block);
	}
}

void memcache_wrt::sync_block(off_t cache_block_no) {									//if it should be changed
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

//Map the cache block and the file block
void memcache_wrt::map_put(off_t file_block_no, off_t cache_block_no) {
	//Unmap last file block if exists.
	if (!(this->cache_flags[cache_block_no] & MEMCACHE_UNUSED)) {
		off_t last = this->cfmap[cache_block_no];
		this->fcmap.erase(last);
	}

	//Update both maps.
	this->cfmap[cache_block_no] = file_block_no;
	this->fcmap[file_block_no] = cache_block_no;
}

void memcache_wrt::mark(off_t cache_block_no, uint8_t flag) {
	//Calculate old spin state.
	bool spo = this->cache_flags[cache_block_no] & MEMCACHE_SPIN;

	//Do marking.
	this->cache_flags[cache_block_no] |= flag;

	//If the block's spin state changed update spin count.
	bool spn = this->cache_flags[cache_block_no] & MEMCACHE_SPIN;
	this->spin_count += spo ^ spn;
}

void memcache_wrt::unmark(off_t cache_block_no, uint8_t flag) {
	//Calculate old spin state.
	bool spo = this->cache_flags[cache_block_no] & MEMCACHE_SPIN;

	//Do marking.
	this->cache_flags[cache_block_no] &= (~flag);

	//If the block's spin state changed update spin count.
	bool spn = this->cache_flags[cache_block_no] & MEMCACHE_SPIN;
	this->spin_count -= spo ^ spn;
}

//Find the block that the pointer 'file_offset' points
off_t memcache_wrt::find(off_t file_offset) {
	return file_offset >> this->block_hbit;
}

bool memcache_wrt::e_free() {
	return this->spin_count < this->block_count;
}
