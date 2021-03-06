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
int connect_qp(resource_t *res, int fd, int ib_port, int gid_idx, int server);

#define SIZE  128
#define RDMA_MIN_SIZE 4096
#define RDMA_MAX_SIZE RDMA_MIN_SIZE*4 /* size は alignment が大事かもしれない */
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

	if (argc == 3) {
		// host port
		DEBUG { printf("Starting as Client to %s\n", argv[1]); }
		server = 0;
		sfd = connect_peer(argv[1], atoi(argv[2]));
	} else {
		// port
		struct sockaddr_in client;
		uint len;

		DEBUG { printf("Starting as Server\n"); }
		server = 1;
		server_sock = open_server(atoi(argv[1]));
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
		char send[] = "CHECK";
		read_safe(sfd, &recv);
		TEST_Z(strcmp("CHECK", recv));
		free(recv);
		write_safe(sfd, send, strlen(send));
	} else {
		char send[] = "CHECK";
		char *recv;
		write_safe(sfd, send, strlen(send));
		read_safe(sfd, &recv);
		TEST_Z(strcmp("CHECK", recv));
		free(recv);
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
		char *recv;
		read_safe(sfd, &recv);
		TEST_Z(strcmp("START", recv));
		free(recv);
		write_safe(sfd, send, strlen(send));
	} else {
		char send[] = "START";
		char *recv;
		write_safe(sfd, send, strlen(send));
		read_safe(sfd, &recv);
		TEST_Z(strcmp("START", recv));
		free(recv);
	}
	printf("[%d] START\n", server);

	if (server) {
		struct ibv_mr *mr;
		for (int size = RDMA_MIN_SIZE; size < RDMA_MAX_SIZE; size += STEP) {
			char *content = calloc(size, sizeof(char));

			TEST_NZ( mr = ibv_reg_mr(res.pd, content, size, IBV_ACCESS_REMOTE_READ) );

			for (int i = 0; i < size; i += 6) {
				strncpy(content + i, "Hello!", 6);
			}

			INT_TO_BE(buf, mr->rkey);
			INT_TO_BE(buf + 4, (((intptr_t)mr->addr) >> 32));
			INT_TO_BE(buf + 8, (((intptr_t)mr->addr) & 0xffffffff));
			TEST_Z( post_ibsend(&res, IBV_WR_SEND, &sge_list, sr, 1) );
			while ((rc = poll_cq(&res, &wc, 1, SCQ_FLG)) == 0) {
			}
			DEBUG { printf("[%d] memory region is sent. key(%x) addr(%lx) rc(%d)\n", server, mr->rkey, (intptr_t)mr->addr, rc); }

			/* wait for done */
			TEST_Z( post_ibreceive(&res, &sge_list, 1) );
			while (poll_cq(&res, &wc, 1, RCQ_FLG) == 0) {
			}
			DEBUG { printf("[%d] %d byte has content (opcode=%d)\n", server, wc.byte_len, wc.opcode); }
			DEBUG { printf("[%d] Content message: %s\n", server, buf); }

			ibv_dereg_mr(mr);
			free(content);
		}
	} else {
		struct ibv_mr *mr;
		struct ibv_sge sge;
		struct ibv_send_wr wr, *bad_wr;
		uint32_t peer_key;
		uint64_t peer_addr;

		for (int size = RDMA_MIN_SIZE; size < RDMA_MAX_SIZE; size += STEP) {
			char *received = malloc(size);

			/* receive_peer_mr */
			TEST_Z(post_ibreceive(&res, &sge_list, 1));
			while (poll_cq(&res, &wc, 1, RCQ_FLG) == 0) {
			}
			DEBUG { printf("[%d] receive remote addr: %d byte has received (opcode=%d)\n", server, wc.byte_len, wc.opcode); }
			peer_key  = BE_TO_INT(buf);
			peer_addr = BE_TO_INT(buf + 4);
			peer_addr = (peer_addr << 32) | BE_TO_INT(buf + 8);
			DEBUG { printf("[%d] remote key %x, remote addr %lx\n", server, peer_key, peer_addr); }

			TEST_NZ(mr = ibv_reg_mr(res.pd, received, size, IBV_ACCESS_LOCAL_WRITE));
			memset(&wr, 0, sizeof(wr));

			sge.addr = (intptr_t)received;
			sge.length = size;
			sge.lkey = mr->lkey;

			wr.sg_list = &sge;
			wr.num_sge = 1;
			wr.opcode = IBV_WR_RDMA_READ;

			wr.wr.rdma.remote_addr = peer_addr;
			wr.wr.rdma.rkey = peer_key;

			DEBUG { printf("[%d] Queue post_send RDMA\n", server); }
			start = getCPUCounter();
			TEST_Z(ibv_post_send(res.qp, &wr, &bad_wr));

			while ((rc = poll_cq(&res, &wc, 1, SCQ_FLG)) == 0) {
			}
			end = getCPUCounter();

			ibv_dereg_mr(mr);

			received[10] = '\0';
			printf("[%d] Received %s(%d)\n", server, received);

			/* notify done */
			sprintf(buf, "Done.");
			TEST_Z(post_ibsend(&res, IBV_WR_SEND, &sge_list, sr, 1));
			while ((rc = poll_cq(&res, &wc, 1, SCQ_FLG)) == 0) {
			}
			DEBUG { printf("[%d] Complete post_send Done rc(%d)\n", server, rc); }
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
