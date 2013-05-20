#include "ib.h"

int
poll_cq(resource_t *res, struct ibv_wc *wc, int num_wr, int cq_flg)
{
    int			ntries;
    int			poll_result = 0;
    int			rc = 0;
    int			i = 0;

    /* poll the completion for a while before giving up of doing it .. */
    if(cq_flg == SCQ_FLG && res->scq != NULL) {
	for (ntries = 0; ntries < MAX_TRIES; ntries++) {
	    rc = ibv_poll_cq(res->scq, num_wr, wc);
	    if (rc < 0) break;
	    poll_result += rc;
	    if (poll_result >= num_wr) break;
	}
    } else if (cq_flg == RCQ_FLG && res->rcq != NULL){
	for (ntries = 0; ntries < MAX_TRIES; ntries++) {
	    rc = ibv_poll_cq(res->rcq, num_wr, wc);
	    if (rc < 0) break;
	    poll_result += rc;
	    if (poll_result >= num_wr) break;
	}
    }
    if (poll_result <= 0) return poll_result;
    DEBUG {
	for(i = 0; i < num_wr; i++){
	    fprintf(stderr, "status: %d, vendor syndrome: 0x%d, %d byte, op: 0x%d, id=%d\n",
		    wc[i].status, wc[i].vendor_err, wc[i].byte_len, wc[i].opcode, wc[i].wr_id);
	}
    }
    return poll_result;
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
    struct ibv_recv_wr	*rr;
    struct ibv_recv_wr	*bad_wr;
    int			rc;

    /* Create RR list */
    rr = malloc(sizeof(*rr));
    memset(rr, 0, sizeof(*rr));
    rr->next = NULL;
    rr->wr_id = 1;
    rr->sg_list = sge_list;
    rr->num_sge = sge_size;
    rc = ibv_post_recv(res->qp, rr, &bad_wr);
    if (rc) {
	fprintf(stderr, "failed to post RR(%d),bad_wr.wr_id=%d, errmsg=%s\n", rc, bad_wr->wr_id, strerror(rc));
    }
    return rc;
}

int
post_ibreceive2(resource_t *res, struct ibv_sge *sge_list, int count)
{
    struct ibv_recv_wr	rr;
    struct ibv_recv_wr	*bad_wr;
    int			i;
    int			rc;

    /* Create RR list */
    for (i = 0; i < count; i++) {
	memset(&rr, 0, sizeof(rr));
	rr.next = NULL;
	rr.wr_id = i;
	rr.sg_list = &sge_list[i]; /* If the smae list, then a few micro seconds delay !!! */
	rr.num_sge = 1;
	rc = ibv_post_recv(res->qp, &rr, &bad_wr);
	if (rc) {
	    fprintf(stderr, "failed to post RR(%d),bad_wr.wr_id=%d, errmsg=%s\n", rc, bad_wr->wr_id, strerror(rc));
	    return rc;
	}
    }
    return rc;
}

int
post_ibsend(resource_t *res, int opcode, struct ibv_sge *sge_list, struct ibv_send_wr *sr,
	    int sge_size)
{
    int rc;
    struct ibv_send_wr	*bad_wr = NULL;

    /* Create a SR */
    sr->next = NULL;
    sr->wr_id = 1;
    sr->sg_list = sge_list;
    sr->num_sge = sge_size;
    sr->opcode = opcode;
    sr->send_flags = 0;
    sr->send_flags = IBV_SEND_SIGNALED | IBV_SEND_INLINE;
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