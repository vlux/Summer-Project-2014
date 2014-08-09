/*
 * scif_common.h
 *
 *  Created on: Jul 14, 2014
 *      Author: Xiaohui Duan
 */

#ifndef SCIF_COMMON_H_
#define SCIF_COMMON_H_

#include <scif.h>
#include <cstring>
#include <cstdlib>

#define PAGEBT 12
const int PAGESZ = 1 << PAGEBT;
struct SCIF_DATA_REQUEST{
	off_t file_offset, cache_offset;
	size_t data_size;
	char type;
};
scif_epd_t scif_connect_s(uint16_t node, uint16_t port);

scif_epd_t scif_listen_s(uint16_t port, int backlog);

scif_epd_t scif_accept_block_s(scif_epd_t server_epd);

void * scif_mmap_window_s(scif_epd_t epd, off_t offset, size_t size);

void * scif_register_window_s(scif_epd_t epd, off_t offset, size_t size);

char * scif_recv_str(scif_epd_t epd);

void scif_send_str(scif_epd_t epd, const char * str);

char *strcat_malloc(const char *a, const char *b);

uint8_t high_bit(uint64_t p);

size_t min_size(size_t a, size_t b);

int64_t rand64();

#endif /* SCIF_COMMON_H_ */

