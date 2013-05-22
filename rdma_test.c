#include "ib.h"
#include "pmiclient.h"

resource_t  res;
int connect_qp(resource_t *res, int ib_port, int gid_idx, int myrank);

#define MHZ  2932.583
#define SIZE  128
#define RDMA_SIZE 1024
#define BE_TO_INT(x) ((((x)[0] & 0xFF) << 24) | (((x)[1] & 0xFF) << 16) | (((x)[2] & 0xFF) << 8) | ((x)[3] & 0xFF))

void INT_TO_BE(char *x, int y)
{
	x[0] = ((y >> 24) & 0xff);
	x[1] = ((y >> 16) & 0xff);
	x[2] = ((y >> 8) & 0xff);
	x[3] = (y & 0xff);
}

char  buf[SIZE];

void display_received(char * received)
{
	printf("*** ");
	for (int i = 0; i < 6; ++i) {
		printf("%d ", received[i]);
	}

	printf("... ");

	for (int i = RDMA_SIZE-5; i < RDMA_SIZE; ++i) {
		printf("%d ", received[i]);
	}

	printf("\n");
}

int
main()
{
	int    ib_port = 1;
	int    gid_idx = 1;
	int    rc;
	int    rank, nprocs;
	struct ibv_sge  sge_list;
	struct ibv_wc  wc;
	struct ibv_send_wr  *sr;

	mypmiInit(&rank, &nprocs);
	fprintf(stderr, "[%d] nprocs(%d)\n", rank, nprocs);
	rc = resource_create(&res, ib_port, rank);
	gid_idx = rank;
	rc = connect_qp(&res, ib_port, gid_idx, rank);
	create_sge(&res, buf, SIZE, &sge_list);
	memset(&wc, 0, sizeof(struct ibv_wc));
	sr = malloc(sizeof(*sr));
	memset(sr, 0, sizeof(*sr));
	mypmiBarrier();
	fprintf(stderr, "[%d] START\n", rank);
	memset(buf, 0, SIZE);

	mypmiBarrier();

	if (rank == 0) {
		struct ibv_mr *mr;
		char *received = calloc(RDMA_SIZE, sizeof(char));

		display_received(received);

		mr = ibv_reg_mr(res.pd, received, RDMA_SIZE, IBV_ACCESS_REMOTE_WRITE |  IBV_ACCESS_LOCAL_WRITE);

		INT_TO_BE(buf, mr->rkey);
		INT_TO_BE(buf + 4, (((intptr_t)mr->addr) >> 32));
		INT_TO_BE(buf + 8, (((intptr_t)mr->addr) & 0xffffffff));
		if (post_ibsend(&res, IBV_WR_SEND, &sge_list, sr, 1)) {
			fprintf(stderr, "[%d] failed to post SR\n", rank);
			goto end;
		}
		while ((rc = poll_cq(&res, &wc, 1, SCQ_FLG)) == 0) {
		}
		printf("[%d] memory region is sent. rc(%d)\n", rank, rc);

		/* wait for done */
		post_ibreceive(&res, &sge_list, 1);
		while (poll_cq(&res, &wc, 1, RCQ_FLG) == 0) {
		}
		printf("[%d] %d byte has received (opcode=%d)\n", rank, wc.byte_len, wc.opcode);
		printf("[%d] Received message: %s\n", rank, buf);
		display_received(received);
	} else {
		struct ibv_mr *mr;
		struct ibv_sge sge;
		struct ibv_send_wr wr, *bad_wr;
		char *buffer = malloc(RDMA_SIZE);
		uint32_t peer_key;
		uint64_t peer_addr;

		/* receive_peer_mr */
		post_ibreceive(&res, &sge_list, 1);
		while (poll_cq(&res, &wc, 1, RCQ_FLG) == 0) {
		}
		printf("[%d] receive remote addr: %d byte has received (opcode=%d)\n", rank, wc.byte_len, wc.opcode);
		peer_key  = BE_TO_INT(buf);
		peer_addr = BE_TO_INT(buf + 4);
		peer_addr = (peer_addr << 32) | BE_TO_INT(buf + 8);
		printf("[%d] remote key %d, remote addr %ld\n", rank, peer_key, peer_addr);

		mr = ibv_reg_mr(res.pd, buffer, RDMA_SIZE, IBV_ACCESS_LOCAL_WRITE);
		for (int i = 0; i < RDMA_SIZE; i += 6) {
			strncpy(buffer + i, "Hello!", 6);
		}
		memset(&wr, 0, sizeof(wr));

		sge.addr = (intptr_t)buffer;
		sge.length = RDMA_SIZE;
		sge.lkey = mr->lkey;

		wr.sg_list = &sge;
		wr.num_sge = 1;
		wr.opcode = IBV_WR_RDMA_WRITE;

		wr.wr.rdma.remote_addr = peer_addr;
		wr.wr.rdma.rkey = peer_key;

		printf("[%d] Queue post_send RDMA\n", rank);
		ibv_post_send(res.qp, &wr, &bad_wr);

		while ((rc = poll_cq(&res, &wc, 1, SCQ_FLG)) == 0) {
		}
		printf("[%d] Complete post_send RDMA rc(%d)\n", rank, rc);

		/* notify done */
		sprintf(buf, "Done.");
		if (post_ibsend(&res, IBV_WR_SEND, &sge_list, sr, 1)) {
			fprintf(stderr, "[%d] failed to post SR\n", rank);
			goto end;
		}
		while ((rc = poll_cq(&res, &wc, 1, SCQ_FLG)) == 0) {
		}
		printf("[%d] Complete post_send Done rc(%d)\n", rank, rc);
	}

	mypmiBarrier();

 end:
	resource_destroy(&res);
	return 0;
}
