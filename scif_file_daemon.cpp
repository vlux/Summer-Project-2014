/*
 * scif_file_daemon.cpp
 *
 *  Created on: Jul 23, 2014
 *      Author: duanye
 */

#include <cstdio>
#include <cstring>
#include <pthread.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <scif.h>
#include "scif_common.h"

void file_session(scif_epd_t epd, size_t cache_size) {
	//Connect RMA buffer.
	void *cache = scif_mmap_window_s(epd, 0, cache_size);

	//Open requested file and report file size.
	FILE *f = fopen((char*) cache, "rb");
	if (!f) {
		goto exit;
	}
	fseek(f, 0, SEEK_END);
	size_t file_size = ftell(f);
	fseek(f, 0, SEEK_SET);

	scif_send(epd, &file_size, sizeof(size_t), 0);

	//Start serving.
	while (1) {

		//Receive request.
		SCIF_DATA_REQUEST req;
		if (scif_recv(epd, &req, sizeof(SCIF_DATA_REQUEST), SCIF_RECV_BLOCK)
				< 0)
			break;
		printf("%d %d %d\n", req.cache_offset, req.data_size, req.file_offset);
		//Process the request.
		switch (req.type) {
		case 'r': //Read
			//If client changed file cursor, sync here.
			if (req.file_offset != ftell(f))
				fseek(f, req.file_offset, SEEK_SET);

			//Transfer data to client.
			fread(cache + req.cache_offset, 1, req.data_size, f);

			//Response the request message.
			scif_send(epd, &req, sizeof(SCIF_DATA_REQUEST), 0);
			break;
		case 'c':
			goto exit;
			break;
		}
	}

	exit: scif_munmap(cache, cache_size);
	free(cache);
	fclose(f);
}

void directory_session(scif_epd_t epd) {
	char cmd, *param;

	//Receive cmd and parameter.
	scif_recv(epd, &cmd, 1, SCIF_RECV_BLOCK);
	param = scif_recv_str(epd);
	switch (cmd) {
	case 'l':
		DIR* dir = opendir(param);
		while (dirent *dirp = readdir(dir)) {
			scif_send(epd, dirp->d_name, 256, SCIF_SEND_BLOCK);
		}
		break;
	case 'a':
		struct stat st;
		int ret = stat(param, &st);
		printf("%d\n", ret);
//			goto exit;
		scif_send(epd, &st, sizeof(struct stat), 0);
		break;
	}
	exit: free(param);
	scif_close(epd);
}

void * server_thread(void * arg) {
	//Detach itself, so no issue about join.
	pthread_detach(pthread_self());

	//Load client end point id.
	scif_epd_t epd = (scif_epd_t) (uint64_t) arg;

	//Receive size of RMA window.
	size_t cache_size;
	scif_recv(epd, &cache_size, sizeof(cache_size), SCIF_RECV_BLOCK);

	//If cache size > 0, it is a file session: open and read a file.
	if (cache_size)
		file_session(epd, cache_size);
	//Else it will be a session like ls and so on.
	else
		directory_session(epd);

	return NULL;
}
int main(int argc, char **argv) {
	//Set port and backlog to default value.
	uint16_t port = 80;
	int backlog = 32;

	//Process parameters.
	int _dircnt_ = 0;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-p"))
			port = atoll(argv[++i]);
		else if (!strcmp(argv[i], "-t"))
			backlog = atoll(argv[++i]);
		else {
			_dircnt_++;
		}
	}

	//Listen to port
	scif_epd_t server_epd = scif_listen_s(port, backlog);

	//Accept connections and start thread for the connection.
	scif_epd_t client_epd;
	while ((client_epd = scif_accept_block_s(server_epd))) {
		pthread_t pid;
		pthread_create(&pid, NULL, server_thread, (void *) client_epd);
	}
	return 0;
}
