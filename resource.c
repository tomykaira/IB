#include "ib.h"

int
resource_create(resource_t *res, int ib_port, int myrank)
{
    struct ibv_device		**dev_list = NULL;
    struct ibv_qp_init_attr	qp_init_attr;
    struct ibv_device		*ib_dev = NULL;
    char	*dev_name = NULL;
    size_t	size;
    int		i;
    int		mr_flags = 0;
    int		cq_size = 0;
    int		dev_numm;
    int		rc = 0;

    /* Init structure */
    memset(res, 0, sizeof(resource_t));
    /* Get the device list */
    dev_list = ibv_get_device_list(&dev_numm);
    if(!dev_list) {
	fprintf(stderr, "[%d] failed to get IB devices list\n", myrank);
	return 1;
    }
    // if no device
    if(!dev_numm) {
	fprintf(stderr, "[%d] No IB device is found\n", myrank);
	rc = 1;
	goto err_exit;
    }
    DEBUG { printf("[%d] found %d IB device(s)\n", myrank, dev_numm); }
    /* Open the requested device */
    for(i = 0; i < dev_numm; i ++){
	dev_name = strdup(ibv_get_device_name(dev_list[i]));
	DEBUG { printf("[%d] IB device name: %s\n", myrank, dev_name); }
	ib_dev = dev_list[i];
	break;
    }
    if (!ib_dev){
	fprintf(stderr, "[%d] IB device %s wasn't found\n", myrank, dev_name);
	rc = 1;
	goto err_exit;
    }
    res->ib_ctx = ibv_open_device(ib_dev);
    DEBUG { printf("[%d] IB context = %lx\n", myrank, (uintptr_t)res->ib_ctx); }
    if(!res->ib_ctx){
	fprintf(stderr, "[%d] failed to open device %s\n", myrank, dev_name);
	rc = 1;
	goto err_exit;
    }
    // free device list
    ibv_free_device_list(dev_list);
    dev_list = NULL;
    ib_dev = NULL;
    // query prot properties
    if(ibv_query_port(res->ib_ctx, ib_port, &res->port_attr)){
	fprintf(stderr, "[%d] ibv_query_port on port %u failed\n", myrank, ib_port);
	rc = 1;
	goto err_exit;
    }

    /* Create a PD */
    res->pd = ibv_alloc_pd(res->ib_ctx);
    if (!res->pd){
	fprintf(stderr, "[%d] ibv_alloc_pd failed\n", myrank);
	rc = 1;
	goto err_exit;
    }

    /* Create send/recv CQ
     *  inputs:
     *		device handle
     *		CQ capacity
     *  Output:
     *		CQ handle
     */
    res->scq = ibv_create_cq(res->ib_ctx, MAX_CQ_CAPACITY, NULL, NULL, 0);
    res->rcq = ibv_create_cq(res->ib_ctx, MAX_CQ_CAPACITY, NULL, NULL, 0);
    if (!res->scq){
	fprintf(stderr, "[%d] failed to create SCQ with %u entries\n", myrank, cq_size);
	rc = 1;
	goto err_exit;
    }
    if (!res->rcq){
	fprintf(stderr, "[%d] failed to create SCQ with %u entries\n", myrank, cq_size);
	rc = 1;
	goto err_exit;
    }

    /* Allocate fix buffer */
    size = MAX_FIX_BUF_SIZE;
    res->buf_size = size;
    res->buf = (char *)malloc(size * sizeof(char));
    if (!res->buf ){
	fprintf(stderr, "[%d] failed to malloc %Zu bytes to memory buffer\n", myrank, size);
	rc = 1;
	goto err_exit;
    }
    memset(res->buf, 0 , size);

    /* Memory Region
     *	inputs:
     *		device handle
     *		PD
     *		Virtual Addr(addr of MR)
     *		Access Ctrl: LocalWrite, RemoteRead, RemoteWrite, RemoteAtomicOp, MemWindowBinding
     *	outputs:
     *		MR handle
     *		L_Key
     *		R_Key
     */
    res->mr_list = malloc(sizeof(struct ibv_mr*) * MAX_MR_NUM);
    res->mr_size = 1;

    mr_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ |
		IBV_ACCESS_REMOTE_WRITE ;
    res->mr_list[0] = ibv_reg_mr(res->pd, res->buf, size, mr_flags);
    if (!res->mr_list[0]){
	fprintf(stderr, "[%d] ibv_reg_mr failed with mr_flags=0x%x\n", myrank, mr_flags);
	rc = 1;
	goto err_exit;
    }
    DEBUG { printf("[%d] fixed MR was registered with addr=%p, lkey=0x%x, rkey=0x%x, flags=0x%x\n", myrank, res->buf, res->mr_list[0]->lkey, res->mr_list[0]->rkey, mr_flags); }

    /* Create QP */
    // inputs:
    //	PD
    //	CQs for SQ,RQ
    //	capacity of SQ,RQ
    // Outputs:
    //	QP handle
    memset(&qp_init_attr, 0, sizeof(qp_init_attr));
    qp_init_attr.qp_type = IBV_QPT_RC;
    qp_init_attr.sq_sig_all = 1;
    qp_init_attr.send_cq = res->scq;
    qp_init_attr.recv_cq = res->rcq;
    // max SR/RR num in SQ/RQ
    qp_init_attr.cap.max_send_wr = MAX_SQ_CAPACITY ;
    qp_init_attr.cap.max_recv_wr = MAX_RQ_CAPACITY;
    // max SGE num
    qp_init_attr.cap.max_send_sge = MAX_SGE_CAPACITY;
    qp_init_attr.cap.max_recv_sge = MAX_SGE_CAPACITY;
    qp_init_attr.cap.max_inline_data = 256;

    res->qp = ibv_create_qp(res->pd, &qp_init_attr);
    if (!res->qp){
	fprintf(stderr, "failed to create QP\n");
	rc = 1;
	goto err_exit;
    }
    DEBUG { printf("[%d] QP was created, QP number=0x%x\n", myrank, res->qp->qp_num); }

    /* EXIT */
err_exit:
    if(rc){
	/* Error encountered, cleanup */
	if(res->qp){
	    ibv_destroy_qp(res->qp);
	    res->qp = NULL;
	}
	if(res->mr_list && res->mr_size > 0){
	    int i;
	    for(i=0; i<res->mr_size; i++){
		ibv_dereg_mr(res->mr_list[i]);
		res->mr_list[i] = NULL;
	    }
	    free(res->mr_list);
	}
	if(res->buf){
	    free(res->buf);
	    res->buf = NULL;
	}
	if(res->scq){
	    ibv_destroy_cq(res->scq);
	    res->scq = NULL;
	}
	if(res->rcq){
	    ibv_destroy_cq(res->rcq);
	    res->rcq = NULL;
	}
	if(res->comp_ch){
	    ibv_destroy_comp_channel(res->comp_ch);
	    res->comp_ch = NULL;
	}
	if(res->pd){
	    ibv_dealloc_pd(res->pd);
	    res->pd = NULL;
	}
	if (res->ib_ctx) {
	    ibv_close_device(res->ib_ctx);
	    res->ib_ctx = NULL;
	}
	if (dev_list) {
	    ibv_free_device_list(dev_list);
	    dev_list = NULL;
	}
    }
    return rc;
}

int
resource_destroy(resource_t *res)
{
    int rc = 0;

    // Delete QP
    if (res->qp && ibv_destroy_qp(res->qp)){
	fprintf(stderr, "failed to destroy QP\n");
	rc = 1;
    }
    // Deregister MR
    if(res->mr_list && res->mr_size > 0){
	int i;
	for(i=0; i<res->mr_size; i++){
	    ibv_dereg_mr(res->mr_list[i]);
	}
	free(res->mr_list);
    }
    if(res->buf){
	free(res->buf);
    }
    // Delete CQ
    if (res->scq && ibv_destroy_cq(res->scq)){
	fprintf(stderr, "failed to destroy SCQ\n");
	rc = 1;
    }
    if (res->rcq && ibv_destroy_cq(res->rcq)){
	fprintf(stderr, "failed to destroy RCQ\n");
	rc = 1;
    }
    if(res->comp_ch && ibv_destroy_comp_channel(res->comp_ch)){
	fprintf(stderr, "failed to destroy Complete CH\n");
	rc = 1;
    }
    // Deallocate PD
    if (res->pd && ibv_dealloc_pd(res->pd)){
	fprintf(stderr, "failed to deallocate PD\n");
	rc = 1;
    }
    if (res->ib_ctx && ibv_close_device(res->ib_ctx)){
	fprintf(stderr, "failed to close device context\n");
	rc = 1;
    }
    return rc;
}
