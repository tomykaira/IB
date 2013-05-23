#include <errno.h>
#include "ib.h"

int write_safe(int fd, char *data, int len);
int read_safe(int fd, char **data);

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
connect_qp(resource_t *res, int fd, int ib_port, int gid_idx, int server)
{
    int		rc = 0;
    union ibv_gid	my_gid;
    uint32_t	remote_qp_num;
    uint16_t	remote_lid;
    uint8_t	remote_gid[16];
    char	send[24], *recv;

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
	printf("[%d] SENDING INFO TO PEER qp_num(%d) lid(%d)\n", server, res->qp->qp_num, res->port_attr.lid);
    }

    /* format: QP_NUM(4) LID(4) GID(16) */
    INT_TO_BE(send, res->qp->qp_num);
    INT_TO_BE(send + 4, res->port_attr.lid);
    memcpy(send + 8, &my_gid, 16);

    if (server) {
	rc = read_safe(fd, &recv);
	if (rc != 24) {
	    fprintf(stderr, "[%d] QP data length expected: 24, got: %d\n", server, rc);
	    return -1;
	}
	rc = write_safe(fd, send, 24);
	if (rc == -1) {
	    fprintf(stderr, "[%d] Failed to send QP data\n", server);
	    return rc;
	}
    } else {
	rc = write_safe(fd, send, 24);
	if (rc == -1) {
	    fprintf(stderr, "[%d] Failed to send QP data\n", server);
	    return rc;
	}
	rc = read_safe(fd, &recv);
	if (rc != 24) {
	    fprintf(stderr, "[%d] QP data length expected: 24, got: %d\n", server, rc);
	    return -1;
	}
    }

    remote_qp_num = BE_TO_INT(recv);
    remote_lid    = BE_TO_INT(recv);
    memcpy(remote_gid, recv + 8, 16);
    free(recv);

    DEBUG {
	uint8_t *p;
	printf("[%d] remote_qp_num(%d) remote_lid(%d)\n", server, remote_qp_num, remote_lid);
	p = (uint8_t*) &my_gid;
	printf("[%d] Local GID = %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n", server, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]);
	p = remote_gid;
	printf("[%d] Remote GID = %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n", server, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]);
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
