#include "ib.h"

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

resource_t  res;

int open_server(int port);
int connect_peer(char *hostaddr, int port);
int read_safe(int fd, char **data);
int write_safe(int fd, char *data, int len);

int gid_by_hostname();
int connect_qp(resource_t *res, int fd, int ib_port, int gid_idx, int myrank);

#define SIZE  128
#define RDMA_MIN_SIZE 4096
#define RDMA_MAX_SIZE 1024*1024	/* size は alignment が大事かもしれない */
#define STEP RDMA_MIN_SIZE

#define TCP_PORT 8352

char  buf[SIZE];

void display_received(char * received, int length)
{
	printf("*** ");
	for (int i = 0; i < 6; ++i) {
		printf("%d ", received[i]);
	}

	printf("... ");

	for (int i = length-5; i < length; ++i) {
		printf("%d ", received[i]);
	}

	printf("\n");
}

int
main(int argc, char *argv[])
{
	int    ib_port = 1;
	int    rc;
	struct ibv_sge  sge_list;
	struct ibv_wc  wc;
	struct ibv_send_wr  *sr;
	unsigned long long start, end;
	float time;

	int server = 0;
	int server_sock = -1;
	int sfd = -1;

	if (argc == 2) {
		DEBUG { printf("Starting as Client to %s\n", argv[1]); }
		server = 0;
		sfd = connect_peer(argv[1], TCP_PORT);
	} else {
		struct sockaddr_in client;
		uint len;

		DEBUG { printf("Starting as Server\n"); }
		server = 1;
		server_sock = open_server(TCP_PORT);
		if (server_sock == -1) {
			fprintf(stderr, "Failed to start server\n");
			goto end;
		}
		len = sizeof(client);
		sfd = accept(server_sock, (struct sockaddr *)&client, &len);
	}

	if (sfd < 0) {
		fprintf(stderr, "Failed to establish connection (sfd = %d)\n", sfd);
		goto end;
	}

	if (server) {
		char *recv;
		read_safe(sfd, &recv);
		TEST_Z(strcmp("CHECK", recv));
		free(recv);
	} else {
		char send[] = "CHECK";
		write_safe(sfd, send, strlen(send));
	}

	TEST_Z( resource_create(&res, ib_port, server) );
	TEST_Z( connect_qp(&res, sfd, ib_port, gid_by_hostname(), server) );
	TEST_Z( create_sge(&res, buf, SIZE, &sge_list) );
	memset(&wc, 0, sizeof(struct ibv_wc));
	sr = malloc(sizeof(*sr));
	memset(sr, 0, sizeof(*sr));
	memset(buf, 0, SIZE);

	if (server) {
		char send[] = "START";
		write_safe(sfd, send, strlen(send));
	} else {
		char *recv;
		read_safe(sfd, &recv);
		TEST_Z(strcmp("START", recv));
		free(recv);
	}
	fprintf(stderr, "[%d] START\n", server);

	if (server) {
		struct ibv_mr *mr;
		for (int size = RDMA_MIN_SIZE; size < RDMA_MAX_SIZE; size += STEP) {
			char *received = calloc(size, sizeof(char));

			mr = ibv_reg_mr(res.pd, received, size, IBV_ACCESS_REMOTE_WRITE |  IBV_ACCESS_LOCAL_WRITE);

			INT_TO_BE(buf, mr->rkey);
			INT_TO_BE(buf + 4, (((intptr_t)mr->addr) >> 32));
			INT_TO_BE(buf + 8, (((intptr_t)mr->addr) & 0xffffffff));
			if (post_ibsend(&res, IBV_WR_SEND, &sge_list, sr, 1)) {
				fprintf(stderr, "[%d] failed to post SR\n", server);
				goto end;
			}
			while ((rc = poll_cq(&res, &wc, 1, SCQ_FLG)) == 0) {
			}
			/* printf("[%d] memory region is sent. key(%x) addr(%lx) rc(%d)\n", server, mr->rkey, (intptr_t)mr->addr, rc); */

			/* wait for done */
			post_ibreceive(&res, &sge_list, 1);
			while (poll_cq(&res, &wc, 1, RCQ_FLG) == 0) {
			}
			/* printf("[%d] %d byte has received (opcode=%d)\n", server, wc.byte_len, wc.opcode); */
			/* printf("[%d] Received message: %s\n", server, buf); */
			/* display_received(received, size); */

			ibv_dereg_mr(mr);
			free(received);
		}
	} else {
		struct ibv_mr *mr;
		struct ibv_sge sge;
		struct ibv_send_wr wr, *bad_wr;
		uint32_t peer_key;
		uint64_t peer_addr;

		for (int size = RDMA_MIN_SIZE; size < RDMA_MAX_SIZE; size += STEP) {
			char *content = malloc(size);

			/* receive_peer_mr */
			post_ibreceive(&res, &sge_list, 1);
			while (poll_cq(&res, &wc, 1, RCQ_FLG) == 0) {
			}
			/* printf("[%d] receive remote addr: %d byte has received (opcode=%d)\n", server, wc.byte_len, wc.opcode); */
			peer_key  = BE_TO_INT(buf);
			peer_addr = BE_TO_INT(buf + 4);
			peer_addr = (peer_addr << 32) | BE_TO_INT(buf + 8);
			/* printf("[%d] remote key %x, remote addr %lx\n", server, peer_key, peer_addr); */

			mr = ibv_reg_mr(res.pd, content, size, IBV_ACCESS_LOCAL_WRITE);
			for (int i = 0; i < size; i += 6) {
				strncpy(content + i, "Hello!", 6);
			}
			memset(&wr, 0, sizeof(wr));

			sge.addr = (intptr_t)content;
			sge.length = size;
			sge.lkey = mr->lkey;

			wr.sg_list = &sge;
			wr.num_sge = 1;
			wr.opcode = IBV_WR_RDMA_WRITE;

			wr.wr.rdma.remote_addr = peer_addr;
			wr.wr.rdma.rkey = peer_key;

			/* printf("[%d] Queue post_send RDMA\n", server); */
			start = getCPUCounter();
			ibv_post_send(res.qp, &wr, &bad_wr);

			while ((rc = poll_cq(&res, &wc, 1, SCQ_FLG)) == 0) {
			}
			end = getCPUCounter();

			ibv_dereg_mr(mr);
			free(content);

			printf("[%d] Complete post_send %d bytes RDMA rc(%d)\n", server, size, rc);
			time = ((float)(end - start))/((float)MHZ);
			printf("    %d clock %f usec\n", (int)(end - start), time);

			/* notify done */
			sprintf(buf, "Done.");
			if (post_ibsend(&res, IBV_WR_SEND, &sge_list, sr, 1)) {
				fprintf(stderr, "[%d] failed to post SR\n", server);
				goto end;
			}
			while ((rc = poll_cq(&res, &wc, 1, SCQ_FLG)) == 0) {
			}
			/* printf("[%d] Complete post_send Done rc(%d)\n", server, rc); */
		}
	}

 end:
	if (sfd >= 0) {
		close(sfd);
		sfd = -1;
	}
	if (server_sock >= 0) {
		close(server_sock);
		server_sock = -1;
	}
	resource_destroy(&res);
	return 0;
}
