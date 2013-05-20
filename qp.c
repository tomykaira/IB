#include "ib.h"
#include "../spawn/pmiclient.h"

static int modify_qp_to_init(struct ibv_qp *qp, int ib_port)
{
	struct ibv_qp_attr attr;
	int flags;
	int rc;

	memset(&attr, 0, sizeof(attr));
	attr.qp_state = IBV_QPS_INIT;
	attr.port_num = ib_port;
	attr.pkey_index = 0;
	attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ
			     | IBV_ACCESS_REMOTE_WRITE;
	flags = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS;
	rc = ibv_modify_qp(qp, &attr, flags);
	if (rc) {
	 fprintf(stderr, "failed to modify QP state to INIT\n");
	}
	return rc;
}

int
modify_qp_to_rtr(struct ibv_qp *qp, uint32_t remote_qpn, uint16_t dlid,
		 uint8_t *dgid, int ib_port, int gid_idx)
{
    struct ibv_qp_attr	attr;
    int		flags;
    int		rc;

    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_RTR;
    attr.path_mtu = IBV_MTU_1024;
    attr.dest_qp_num = remote_qpn;
    attr.rq_psn = 0;
    attr.max_dest_rd_atomic = 1;
    attr.min_rnr_timer = 12; /* Ready NAK timer, usually 12 is default value */
    attr.ah_attr.is_global = 0;
    attr.ah_attr.dlid = dlid;
    attr.ah_attr.sl = 0;
    attr.ah_attr.src_path_bits = 0;
    attr.ah_attr.port_num = ib_port;

    if (gid_idx >= 0){
	attr.ah_attr.is_global = 1;
	attr.ah_attr.port_num = 1;
	memcpy(&attr.ah_attr.grh.dgid, dgid, 16);
	attr.ah_attr.grh.flow_label = 0;
	attr.ah_attr.grh.hop_limit = 1;
	attr.ah_attr.grh.sgid_index = gid_idx;
	attr.ah_attr.grh.traffic_class = 0;
    }

    flags = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN
	| IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;
    rc = ibv_modify_qp(qp, &attr, flags);
    if (rc){
	fprintf(stderr, "failed to modify QP state to RTR\n");
    }
    return rc;
}

int
modify_qp_to_rts(struct ibv_qp *qp)
{
    struct ibv_qp_attr	attr;
    int		flags;
    int		rc;

    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_RTS;
    attr.timeout = 20; /* timeout */
    attr.retry_cnt = 7;
    attr.rnr_retry = 7;
    attr.sq_psn = 0;
    attr.max_rd_atomic = 1;

    flags = IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT
	| IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC;
    rc = ibv_modify_qp(qp, &attr, flags);
    if (rc){
	fprintf(stderr, "failed to modify QP state to RTS\n");
    }
    return rc;
}


int
connect_qp(resource_t *res, int ib_port, int gid_idx, int myrank)
{
    int		rc = 0;
    char	temp_char;
    union ibv_gid	my_gid;
    uint32_t	remote_qp_num;
    uint16_t	remote_lid;
    uint8_t	remote_gid[16];
    char	key[512];
    int		peer;

    /* Init QP */
    if(gid_idx >= 0) {
	rc = ibv_query_gid(res->ib_ctx, ib_port, gid_idx, &my_gid);
	if (rc){
	    fprintf(stderr, "could not get gid for port %d, index %d\n", ib_port, gid_idx);
	    return rc;
	}
    } else {
	memset(&my_gid, 0, sizeof my_gid);
    }
    /* Exchange info between peer */
    DEBUG {
	fprintf(stdout, "[%d] SENDING INFO TO PEER qp_num(%d) lid(%d)\n", myrank, res->qp->qp_num, res->port_attr.lid);
    }
    sprintf(key, "RANK%d_QP_NUM", myrank);
    mypmiPutInt(key, res->qp->qp_num);
    sprintf(key, "RANK%d_LID", myrank);
    mypmiPutInt(key, res->port_attr.lid);
    sprintf(key, "RANK%d_GID", myrank);
    mypmiPutByte(key, (char*) &my_gid, 16);
    /* * */
    mypmiBarrier();
    /* * */
    peer = (myrank + 1) % 2;
    sprintf(key, "RANK%d_QP_NUM", peer);
    remote_qp_num = mypmiGetInt(key);
    sprintf(key, "RANK%d_LID", peer);
    remote_lid = mypmiGetInt(key);
    sprintf(key, "RANK%d_GID", peer);
    mypmiGetByte(key, remote_gid, 16);

    DEBUG {
	uint8_t *p;
	fprintf(stderr, "[%d] remote_qp_num(%d) remote_lid(%d)\n", remote_qp_num, remote_lid);
	p = (uint8_t*) &my_gid;
	fprintf(stdout, "[%d] Local GID = %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n", myrank, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]);
	p = remote_gid;
	fprintf(stdout, "[%d] Remote GID = %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n", myrank, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]);
    }

    /* Init QP  */
    rc = modify_qp_to_init(res->qp, ib_port);
    if (rc){
	fprintf(stderr, "change QP state to INIT failed\n");
	goto connect_qp_exit;
    }

    /* Modify QP TO RTR status */
    rc = modify_qp_to_rtr(res->qp, remote_qp_num, remote_lid, remote_gid,
			  ib_port, gid_idx);
    if (rc){
	fprintf(stderr, "failed to modify QP state to RTR\n");
	goto connect_qp_exit;
    }

    /* Modify QP TO RTS status */
    rc = modify_qp_to_rts(res->qp);
    if (rc){
	fprintf(stderr, "failed to modify QP state to RTR\n");
	goto connect_qp_exit;
    }
    /* EXIT */
connect_qp_exit:
    return rc;
}
