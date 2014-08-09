/*
 * scif_helper.h
 *
 *  Created on: Jul 14, 2014
 *      Author: Xiaohui Duan
 */

#ifndef SCIF_HELPER_H_
#define SCIF_HELPER_H_

#include "scif_common.h"
#define PAGESZ 4096

scif_epd_t scif_connect_s(uint16_t node, uint16_t port){
	scif_portID server;
	server.node = node;
	server.port = port;

	scif_epd_t ret = scif_open();
	if (ret == SCIF_OPEN_FAILED)
		return ret;

	scif_connect(ret, &server);


	return ret;
}

scif_epd_t scif_listen_s(uint16_t port, int backlog){
	scif_epd_t server_epd = scif_open();
	scif_bind(server_epd, port);
	scif_listen(server_epd, backlog);

	return server_epd;
}

scif_epd_t scif_accept_block_s(scif_epd_t server_epd){
	scif_epd_t client;
	scif_portID cp;
	int a = scif_accept(server_epd, &cp, &client, SCIF_ACCEPT_SYNC);
	return client;
}

void * scif_mmap_window_s(scif_epd_t epd, off_t offset, size_t size){
	void * win;
	posix_memalign(&win, PAGESZ, size);
	win = scif_mmap(win, size, SCIF_PROT_READ | SCIF_PROT_WRITE, SCIF_MAP_FIXED, epd, offset);
	return win;
}

void * scif_register_window_s(scif_epd_t epd, off_t offset, size_t size){
	void * win;
	posix_memalign(&win, PAGESZ, size);
	off_t po = scif_register(epd, win, size, offset, SCIF_PROT_READ | SCIF_PROT_WRITE, SCIF_MAP_FIXED);
	if (po < 0)
		return NULL;
	return win;
}

char * scif_recv_str(scif_epd_t epd){
	size_t clen = 256;
	char *str = (char*)malloc(clen);
	char *hdr = str;
	scif_recv(epd, hdr, 256, SCIF_RECV_BLOCK);
	while (strnlen(hdr, 256) == 256){
		clen += 256;
		str = (char *)realloc(str, clen);
		hdr = str + clen - 256;
		scif_recv(epd, hdr, 256, SCIF_RECV_BLOCK);
	}
	return str;
}

void scif_send_str(scif_epd_t epd, const char * str){
	char buf[256];
	do{
		strncpy(buf, str, 256);
		scif_send(epd, buf, 256, SCIF_SEND_BLOCK);
		str += 256;
	} while (strnlen(buf, 256) == 256);
}

char * strcat_malloc(const char *a, const char *b){
	char *ret = (char *)malloc(strlen(a) + strlen(b));
	strcpy(ret, a);
	strcat(ret, b);
	return ret;
}

uint8_t high_bit(uint64_t p){
	return 64 - __builtin_clzll(p) & p != 1;
}

size_t min_size(size_t a, size_t b){
	return b ^ ((a ^ b) & -(a < b));
}

size_t max_size(size_t a, size_t b){
	return a ^ ((a ^ b) & -(a < b));
}

int64_t rand64(){
	return (((int64_t)rand()) << 32) | rand();
}
#endif /* SCIF_HELPER_H_ */
