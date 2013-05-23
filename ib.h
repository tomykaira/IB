#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <infiniband/verbs.h>
#include <sys/types.h>

#ifndef DEBUG
#if 0
#define DEBUG
#else
#define DEBUG	if(0)
#endif
#endif

#define COMBUF_SIZE	1024

static inline unsigned long long
getCPUCounter(void)
{
    unsigned int lo, hi;
    __asm__ __volatile__ (      // serialize
	"xorl %%eax,%%eax \n        cpuid"
	::: "%rax", "%rbx", "%rcx", "%rdx");
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
    return (unsigned long long)hi << 32 | lo;
}

/*exchange data struct for connectting QPs*/
struct qp_con_data_t{
    uint64_t addr;	/*Buffer address*/
    uint32_t rkey;	/*Remote key*/
    uint32_t qp_num;	/*QP number*/
    uint16_t lid;	/*LID of the IB port*/
    uint8_t gid[16];
};

typedef struct resources {
    struct ibv_port_attr	port_attr;	/* IB port attributes */
    struct qp_con_data_t	remote_props;
    struct ibv_context		*ib_ctx;	/*device handle*/
    struct ibv_pd		*pd;
    struct ibv_cq		*scq;
    struct ibv_cq		*rcq;
    struct ibv_qp		*qp;
    struct ibv_mr		**mr_list;
    int				mr_size;
    struct ibv_comp_channel	*comp_ch;
    char			*buf; /*for RDMA and send ops*/
    int				buf_size;
} resource_t;

#define RESOURCE_LIB 1
#define MAX_FIX_BUF_SIZE 64
#define MAX_SQ_CAPACITY 20
#define MAX_RQ_CAPACITY 20
#define MAX_SGE_CAPACITY 20
#define MAX_CQ_CAPACITY 20
#define MAX_MR_NUM 10

#define MAX_TRIES 1
#define SCQ_FLG 1
#define RCQ_FLG 2

#define MHZ  2932.583

#define TEST_NZ(x) do { if ( (x)) die("error: " #x " failed (returned non-zero)." ); } while (0)
#define TEST_Z(x)  do { if (!(x)) die("error: " #x " failed (returned zero/null)."); } while (0)

#define BE_TO_INT(x) ((((x)[0] & 0xFF) << 24) | (((x)[1] & 0xFF) << 16) | (((x)[2] & 0xFF) << 8) | ((x)[3] & 0xFF))

static void
INT_TO_BE(char *x, unsigned int y)
{
	x[0] = ((y >> 24) & 0xff);
	x[1] = ((y >> 16) & 0xff);
	x[2] = ((y >> 8) & 0xff);
	x[3] = (y & 0xff);
}

extern int	resource_create(resource_t *res, int ib_port, int myrank);
extern int	poll_cq(resource_t *res, struct ibv_wc *wc, int num_wr, int cq_flg);
extern int	create_sge(resource_t *res, char *buf, int size, struct ibv_sge *sge);
extern int	post_ibreceive(resource_t *res, struct ibv_sge *sge_list, int sge_size);
extern int	post_ibsend(resource_t *res, int opcode, struct ibv_sge *sge_list,
			    struct ibv_send_wr *sr, int sge_size);
extern int resource_destroy(resource_t *res);
