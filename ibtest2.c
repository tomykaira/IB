#include "ib.h"
#include "../spawn/pmiclient.h"

resource_t	res;
int connect_qp(resource_t *res, int ib_port, int gid_idx, int myrank);
int create_sge2(resource_t *res, char *buf, int size, int count, struct ibv_sge *sge);
int post_ibreceive2(resource_t *res, struct ibv_sge *sge_list, int count);

//#define SIZE	128
#define SIZE	4
// #define SIZE	1024
#define TIME	10

char	buf[COMBUF_SIZE*10];

int
main()
{
    int		ib_port = 1;
    int		gid_idx = 1;
    int		rc, ncount;
    int		rank, nprocs;
    struct ibv_sge	sge_list[TIME];
    struct ibv_wc	wc;
    struct ibv_send_wr	*sr;
    int		i;
    float	time;
    unsigned long long	start, end;
    int		count;

    mypmiInit(&rank, &nprocs);
    fprintf(stderr, "[%d] nprocs(%d)\n", rank, nprocs);
    rc = resource_create(&res, ib_port, rank);
    gid_idx = rank;
    rc = connect_qp(&res, ib_port, gid_idx, rank);
    create_sge2(&res, buf, SIZE, TIME, sge_list);
    memset(&wc, 0, sizeof(struct ibv_wc));
    sr = malloc(sizeof(*sr));
    memset(sr, 0, sizeof(*sr));
    mypmiBarrier();
    fprintf(stderr, "[%d] START\n", rank);
    if (rank == 0) {
	memset(buf, 0, SIZE);
    } else {
	for (i = 0; i < SIZE; i++) buf[i] = i;
    }
    mypmiBarrier();
    if (rank == 0) {
	ncount = 0;
	post_ibreceive2(&res, sge_list, TIME);
	for (i = 0; i < TIME; i++) {
	    count = 0;
	    start = getCPUCounter();
	    while ((rc = poll_cq(&res, &wc, 1, RCQ_FLG)) == 0) {
		ncount += rc;
		count++;
	    }
	    end = getCPUCounter();
	    if (buf[COMBUF_SIZE*i] != (i + 1)) {
		fprintf(stderr, "[%d] is really recived ? expected value(%d) received value(%d)\n", rank, i, buf[COMBUF_SIZE*i]);
	    }
	    time = ((float)(end - start))/((float)MHZ);
	    fprintf(stderr, "[%d] step %d rc(%d) id(%ld) %d clock %f usec (%d times sleep)\n",
		    rank, i, rc, wc.wr_id, (int)(end - start), time, count);
	    fprintf(stderr, "[%d] %d byte has received (opcode=%d)\n", rank, wc.byte_len, wc.opcode);
	}
    } else {
	for (i = 0; i < TIME; i++) {
	    count = 0;
	    buf[0] = i + 1;
	    start = getCPUCounter();
	    post_ibsend(&res, IBV_WR_SEND, sge_list, sr, 1);
	    while ((rc = poll_cq(&res, &wc, 1, SCQ_FLG)) == 0) {
		count++;
	    }
	    end = getCPUCounter();
	    time = ((float)(end - start))/((float)MHZ);
	    fprintf(stderr, "[%d] step %i %d clock %f usec (%d times sleep)\n",
		    rank, i, (int)(end - start), time, count);
	    fprintf(stderr, "[%d] %d byte opcode(%d) id(%ld) rc(%d)\n",
		    rank, wc.byte_len, wc.opcode, wc.wr_id, rc);
	}
    }
    mypmiBarrier();
    resource_destroy(&res);
    return 0;
}
