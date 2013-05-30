#include "ib.h"

#define COUNT_MAX 1000000

void
wait_complete(resource_t *res, int cq_flag)
{
	struct ibv_wc  wc;
	int count = 0;

	while (poll_cq(res, &wc, 1, cq_flag) == 0) {
		count++;
		if (count > COUNT_MAX) {
			printf("status: %s, vendor syndrome: 0x%d, %d byte, op: 0x%d, id=%ld\n",
			       ibv_wc_status_str(wc.status), wc.vendor_err, wc.byte_len, wc.opcode, wc.wr_id);
			exit(1);
		}
	}
	if (wc.status != 0)
		printf("status: %s, vendor syndrome: 0x%d, %d byte, op: 0x%d, id=%ld\n",
		       ibv_wc_status_str(wc.status), wc.vendor_err, wc.byte_len, wc.opcode, wc.wr_id);
}

int
poll_cq(resource_t *res, struct ibv_wc *wc, int count, int cq_flg)
{
    int   rc = 0;
    struct ibv_cq *target = NULL;

    /* poll the completion for a while before giving up of doing it .. */
    if(cq_flg == SCQ_FLG && res->scq != NULL) {
        target = res->scq;
    } else if (cq_flg == RCQ_FLG && res->rcq != NULL){
        target = res->rcq;
    }

    rc = ibv_poll_cq(target, count, wc); /* wc will overwritten */
    return rc;
}

static struct ibv_wc *dummy_wc = NULL;

int
clear_cq(resource_t *res, int cq_flag)
{
    int   rc = 0;
    struct ibv_cq *target = NULL;
    int found = 0;

    if (!dummy_wc) {
	dummy_wc = calloc(MAX_CQ_CAPACITY, sizeof(struct ibv_wc));
    }

    if(cq_flag == SCQ_FLG && res->scq != NULL) {
        target = res->scq;
    } else if (cq_flag == RCQ_FLG && res->rcq != NULL){
        target = res->rcq;
    }

    rc = ibv_poll_cq(target, MAX_CQ_CAPACITY, dummy_wc);
    if (rc < 0) {
	    fprintf(stderr, "ibv_poll_cq failed");
	    return rc;
    }
    return rc < MAX_CQ_CAPACITY ? MAX_CQ_CAPACITY - rc : 0;
}

int
create_sge(resource_t *res, char *buf, int size, struct ibv_sge *sge)
{
    struct ibv_mr	*mr;

    mr = ibv_reg_mr(res->pd, buf, size,
		    IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE);
    if(mr == NULL) {
	fprintf(stderr, "failed to register MR\n");
	return 1;
    }
    res->mr_list[res->mr_size++] = mr;
    memset(sge, 0, sizeof(*sge));
    sge->addr = (uintptr_t)buf;
    sge->length = size;
    sge->lkey = mr->lkey;
    return 0;
}

int
create_sge2(resource_t *res, char *buf, int size, int count, struct ibv_sge *sge)
{
    struct ibv_mr	*mr;
    int			i;

    mr = ibv_reg_mr(res->pd, buf, COMBUF_SIZE*count,
		    IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE);
    if(mr == NULL) {
	fprintf(stderr, "failed to register MR\n");
	return 1;
    }
    res->mr_list[res->mr_size++] = mr;
    memset(sge, 0, sizeof(*sge)*count);
    for (i = 0; i < count; i++) {
	sge[i].addr = (uintptr_t) (buf + COMBUF_SIZE*i);
	sge[i].length = size;
	sge[i].lkey = mr->lkey;
    }
    return 0;
}

int
post_ibreceive(resource_t *res, struct ibv_sge *sge_list, int sge_size)
{
    struct ibv_recv_wr	rr;
    struct ibv_recv_wr	*bad_wr;
    int			rc;

    /* Create RR list */
    memset(&rr, 0, sizeof(rr));
    rr.next = NULL;
    rr.wr_id = 1;
    rr.sg_list = sge_list;
    rr.num_sge = sge_size;
    rc = ibv_post_recv(res->qp, &rr, &bad_wr);
    if (rc) {
	fprintf(stderr, "failed to post RR(%d),bad_wr.wr_id=%ld, errmsg=%s\n", rc, bad_wr->wr_id, strerror(rc));
    }
    return rc;
}

int
post_ibreceive2(resource_t *res, struct ibv_sge *sge_list, int count)
{
    struct ibv_recv_wr	rr;
    struct ibv_recv_wr	*bad_wr;
    int			i;
    int			rc = 0;

    /* Create RR list */
    for (i = 0; i < count; i++) {
	memset(&rr, 0, sizeof(rr));
	rr.next = NULL;
	rr.wr_id = i;
	rr.sg_list = &sge_list[i]; /* If the smae list, then a few micro seconds delay !!! */
	rr.num_sge = 1;
	rc = ibv_post_recv(res->qp, &rr, &bad_wr);
	if (rc) {
	    fprintf(stderr, "failed to post RR(%d),bad_wr.wr_id=%ld, errmsg=%s\n", rc, bad_wr->wr_id, strerror(rc));
	    return rc;
	}
    }
    return rc;
}

int
post_ibsend(resource_t *res, int opcode, struct ibv_sge *sge_list, struct ibv_send_wr *sr,
	    int sge_size, int inlinep)
{
    int rc;
    struct ibv_send_wr	*bad_wr = NULL;

    /* Create a SR */
    sr->next = NULL;
    sr->wr_id = 1;
    sr->sg_list = sge_list;
    sr->num_sge = sge_size;
    sr->opcode = opcode;

    if (inlinep)
	sr->send_flags = IBV_SEND_SIGNALED | IBV_SEND_INLINE;
    else
	sr->send_flags = IBV_SEND_SIGNALED;
/*
    sr->send_flags = IBV_SEND_SIGNALED | IBV_SEND_INLINE;
    sr->send_flags = IBV_SEND_SIGNALED;
*/
    if (opcode != IBV_WR_SEND) {
	// set addr and key if is RDMA op
	sr->wr.rdma.remote_addr = res->remote_props.addr;
	sr->wr.rdma.rkey = res->remote_props.rkey;
    }
    /* Post SR to SQ */
    rc = ibv_post_send(res->qp, sr, &bad_wr);
    if (rc) {
	fprintf(stderr, "failed to post SR\n");
    }
    return rc;
}
