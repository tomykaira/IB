#include "ib.h"
#include "pmiclient.h"

resource_t	res;
int connect_qp(resource_t *res, int ib_port, int gid_idx, int myrank);

#define SIZE	437		/* < 450 if SEND_INLINE set */
#define TIME	10

char	buf[SIZE];

int
gid_by_hostname()
{
    char hostname[256];
    if (gethostname(hostname, 256) == -1) {
	perror("gethostname");
	return -1;
    }
    return (uint8_t)hostname[2];
}

int
main()
{
    int		ib_port = 1;
    int		gid_idx = 1;
    int		rc;
    int		rank, nprocs;
    struct ibv_sge	sge_list;
    struct ibv_wc	wc;
    struct ibv_send_wr	*sr;
    int		i;
    float	time;
    unsigned long long	start, end;
    int		count;
    int		ntries = 0;

    mypmiInit(&rank, &nprocs);
    fprintf(stderr, "[%d] nprocs(%d)\n", rank, nprocs);
    rc = resource_create(&res, ib_port, rank);
    gid_idx = gid_by_hostname();
    rc = connect_qp(&res, ib_port, gid_idx, rank);
    create_sge(&res, buf, SIZE, &sge_list);
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
	do {
	    count = 0;
	    start = getCPUCounter();
	    post_ibreceive(&res, &sge_list, 1);
	    while (poll_cq(&res, &wc, 1, RCQ_FLG) == 0) {
		count++;
	    }
	    end = getCPUCounter();
	    time = ((float)(end - start))/((float)MHZ);
	    fprintf(stderr, "[%d] %d clock %f usec (%d times sleep)\n",
		    rank, (int)(end - start), time, count);
	    fprintf(stderr, "[%d] %d byte has received (opcode=%d)\n", rank, wc.byte_len, wc.opcode);
	    printf("buf %d %d %d %d ... %d %d\n", buf[0], buf[1], buf[2], buf[3], buf[SIZE - 2], buf[SIZE - 1]);
	} while (ntries++ < TIME);
    } else {
	do {
	    printf("[%d] ntries(%d)\n", rank, ntries);
	    count = 0;
	    start = getCPUCounter();
	    post_ibsend(&res, IBV_WR_SEND, &sge_list, sr, 1);
	    while ((rc = poll_cq(&res, &wc, 1, SCQ_FLG)) == 0) {
		count++;
	    }
	    end = getCPUCounter();
	    time = ((float)(end - start))/((float)MHZ);
	    fprintf(stderr, "[%d] %d clock %f usec (%d times sleep)\n",
		    rank, (int)(end - start), time, count);
	    fprintf(stderr, "[%d] %d byte opcode(%d) id(%ld) rc(%d)\n",
		    rank, wc.byte_len, wc.opcode, wc.wr_id, rc);
	} while (ntries++ < TIME);
    }
    mypmiBarrier();
    resource_destroy(&res);
    return 0;
}
