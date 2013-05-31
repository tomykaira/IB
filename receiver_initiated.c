#include "ib.h"

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <assert.h>

int connect_qp(resource_t *res, int fd, int ib_port, int gid_idx, int server);
void wait_complete(resource_t *res, int cq_flag);
int clear_cq(resource_t *res, int cq_flag);

extern int TIMES;
extern int SIZE;

#define BUF_SIZE  128
#define RDMA_OFFSET 1

#define POLL(x) while (x == 0) { sched_yield(); }

double get_interval(struct timeval bt, struct timeval et);
void report(const char * type, const int server, const double elapsed);

const int request_size = 256;

static void format_mr(struct ibv_mr *mr, char *output)
{
  INT_TO_BE(output, (unsigned int)mr->rkey);
  INT_TO_BE(output + 4, (((uintptr_t)mr->addr) >> 32));
  INT_TO_BE(output + 8, (((uintptr_t)mr->addr) & 0xffffffff));
}

static void extract_mr(char *input, uint32_t *ptr_key, uint64_t *ptr_addr)
{
  *ptr_key  = BE_TO_INT(input);
  *ptr_addr = ((uint64_t)BE_TO_INT(input + 4) << 32) | BE_TO_INT(input + 8);
}

/* addr(8), rkey(4), len(1), key(242), check(1) -> 256 */
static void format_receive_request(char * key,
                                   size_t keylen,
                                   struct ibv_mr *mr,
                                   char *output)
{
  assert(keylen <= 242);
  memset(output, 0, 256);
  format_mr(mr, output);
  output[12] = (uint8_t)keylen;
  memcpy(output + 13, key, keylen);
  output[255] = 1;
}

static void update_receive_request(char *key, size_t keylen, char *output)
{
  output[12] = (uint8_t)keylen;
  memcpy(output + 13, key, keylen);
}

/* never store or free `key`, it is valid while request is valid */
static void extract_receive_request(char *request,
                                    char **key,
                                    size_t *keylen,
                                    uint32_t *ptr_key,
                                    uint64_t *ptr_addr)
{
  size_t len;
  extract_mr(request, ptr_key, ptr_addr);
  *keylen = len = (size_t)request[12];
  *key = request + 13;
}

void prepare_rdma_write_wr(struct ibv_mr *mr, struct ibv_sge *sge, struct ibv_send_wr *wr)
{
  memset(wr,  0, sizeof(*wr));
  memset(sge, 0, sizeof(*sge));

  sge->addr   = (intptr_t)mr->addr;
  sge->length = mr->length;
  sge->lkey   = mr->lkey;

  wr->sg_list = sge;
  wr->num_sge = 1;
  wr->opcode  = IBV_WR_RDMA_WRITE;
}

void act_as_sender(resource_t *res)
{
  struct timeval begin, end;
  struct ibv_sge  sge, sge_buf;
  struct ibv_send_wr *sr, wr, *bad_wr = NULL;
  int data_size = SIZE + RDMA_OFFSET;
  char *request = calloc(request_size, 1);
  char *buf     = calloc(BUF_SIZE, 1);
  char *data    = calloc(data_size, 1);
  char *copy    = calloc(SIZE, 1);
  struct ibv_mr *request_mr, *data_mr;
  int queue_counter = 0;

  memset(copy, 'O', SIZE);

  create_sge(res, buf, BUF_SIZE, &sge_buf);
  sr = calloc(1, sizeof(*sr));

  TEST_NZ(request_mr = ibv_reg_mr(res->pd, request, request_size, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE));

  format_mr(request_mr, buf);

  TEST_Z( post_ibsend(res, IBV_WR_SEND, &sge_buf, sr, 1, 1) );

  TEST_NZ(data_mr = ibv_reg_mr(res->pd, data, data_size, IBV_ACCESS_LOCAL_WRITE));
  prepare_rdma_write_wr(data_mr, &sge, &wr);

  wait_complete(res, SCQ_FLG);

  gettimeofday(&begin, NULL);
  while (request[request_size-2] == 0) {
    uint32_t peer_key;
    uint64_t peer_addr;
    char *key;
    size_t key_len;

    DEBUG {printf("waiting request\n");}

    /* polling */
    POLL(request[request_size-1]);
    request[request_size-1] = 0;

    extract_receive_request(request, &key, &key_len, &peer_key, &peer_addr);

    TEST_Z(strncmp(key, "test", key_len));

    wr.wr.rdma.remote_addr = peer_addr;
    wr.wr.rdma.rkey = peer_key;

    /* memcpy(data, copy, SIZE); */
    data[data_size-1] = 255; /* check bit */

    /* issue mem copy */
    TEST_Z(ibv_post_send(res->qp, &wr, &bad_wr));
    ++queue_counter;
    if (queue_counter >= MAX_CQ_CAPACITY) {
      queue_counter = clear_cq(res, SCQ_FLG);
    }
  }
  gettimeofday(&end, NULL);
  report("rec_init", 1, get_interval(begin, end));

  ibv_dereg_mr(request_mr);
  ibv_dereg_mr(data_mr);

  free(request);
  free(data);
  free(buf);
  free(copy);
  free(sr);
}

void act_as_receiver(resource_t *res)
{
  struct timeval begin, end;
  struct ibv_sge  sge, sge_buf;
  struct ibv_send_wr *sr, wr, *bad_wr = NULL;
  int data_size = SIZE + RDMA_OFFSET;
  char *request = calloc(request_size, 1);
  char *buf     = calloc(BUF_SIZE, 1);
  char *data    = calloc(data_size, 1);
  char *copy    = calloc(data_size, 1);
  struct ibv_mr *request_mr, *data_mr;
  uint32_t sender_key;
  uint64_t sender_addr;
  int queue_counter = 0;

  create_sge(res, buf, BUF_SIZE, &sge_buf);
  sr = calloc(1, sizeof(*sr));

  TEST_NZ(request_mr = ibv_reg_mr(res->pd, request, request_size, IBV_ACCESS_LOCAL_WRITE));
  TEST_NZ(data_mr = ibv_reg_mr(res->pd, data, data_size, IBV_ACCESS_LOCAL_WRITE |  IBV_ACCESS_REMOTE_WRITE));

  /* get sender's request buffer */
  TEST_Z(post_ibreceive(res, &sge_buf, 1));
  wait_complete(res, RCQ_FLG);

  extract_mr(buf, &sender_key, &sender_addr);

  /* prepare request send work */
  prepare_rdma_write_wr(request_mr, &sge, &wr);

  wr.wr.rdma.remote_addr = sender_addr;
  wr.wr.rdma.rkey = sender_key;

  format_receive_request("", 0, data_mr, request);

  gettimeofday(&begin, NULL);
  for (int i = 0; i < TIMES; ++i) {
    update_receive_request("test", 4, request);
    TEST_Z(ibv_post_send(res->qp, &wr, &bad_wr));

    DEBUG {printf("waiting response\n");}

    /* poll last byte */
    POLL(data[data_size-1]);
    data[data_size-1] = 0;
    /* memcpy(copy, data, SIZE); */
    DEBUG { printf("Received: %s\n", data); }

    ++queue_counter;
    if (queue_counter >= MAX_CQ_CAPACITY) {
      queue_counter = clear_cq(res, SCQ_FLG);
    }
  }
  /* quit request */
  format_receive_request("test", 4, data_mr, request);
  request[request_size - 2] = 1;
  TEST_Z(ibv_post_send(res->qp, &wr, &bad_wr));
  wait_complete(res, SCQ_FLG);

  gettimeofday(&end, NULL);
  report("rec_init", 0, get_interval(begin, end));

  ibv_dereg_mr(request_mr);
  ibv_dereg_mr(data_mr);

  free(request);
  free(data);
  free(buf);
  free(copy);
  free(sr);
}
