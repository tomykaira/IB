#include "ib.h"

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <fcntl.h>


resource_t  res;

int open_server(int port);
int connect_peer(char *hostaddr, int port);
int read_safe(int fd, char **data);
int write_safe(int fd, char *data, int len);

int gid_by_hostname();
int connect_qp(resource_t *res, int fd, int ib_port, int gid_idx, int server);

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
  int count = 0;

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
		start = getCPUCounter();
		post_ibreceive(&res, &sge_list, 1);
		while (poll_cq(&res, &wc, 1, RCQ_FLG) == 0) {
			count++;
			if (count >= 100000) {
				fprintf(stderr, "[%d] poll_cq timed out\n", server);
				break;
			}
		}
		end = getCPUCounter();
		time = ((float)(end - start))/((float)MHZ);
		printf("[%d] %d clock %f usec (%d times sleep)\n", server, (int)(end - start), time, count);
		printf("[%d] %d byte has received (opcode=%d)\n", server, wc.byte_len, wc.opcode);
		printf("buf %d %d %d %d ... %d %d\n", buf[0], buf[1], buf[2], buf[3], buf[SIZE - 2], buf[SIZE - 1]);
	} else {
		start = getCPUCounter();
		post_ibsend(&res, IBV_WR_SEND, &sge_list, sr, 1);
		while ((rc = poll_cq(&res, &wc, 1, SCQ_FLG)) == 0) {
			count++;
			if (count >= 100000) {
				fprintf(stderr, "[%d] poll_cq timed out\n", server);
				break;
			}
		}
		end = getCPUCounter();
		time = ((float)(end - start))/((float)MHZ);
		printf("[%d] %d clock %f usec\n", server, (int)(end - start), time, count);
		printf("[%d] %d byte opcode(%d) id(%ld) rc(%d)\n", server, wc.byte_len, wc.opcode, wc.wr_id, rc);
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
