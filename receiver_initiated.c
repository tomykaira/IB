#include "ib.h"

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <assert.h>

resource_t  res;

int open_server(int port);
int connect_peer(char *hostaddr, int port);
int read_safe(int fd, char **data);
int write_safe(int fd, char *data, int len);

int gid_by_hostname();
int connect_qp(resource_t *res, int fd, int ib_port, int gid_idx, int server);
void wait_complete(resource_t *res, int cq_flag);

#define SIZE  128
#define RDMA_MIN_SIZE 4096
#define RDMA_MAX_SIZE RDMA_MIN_SIZE*4 /* size は alignment が大事かもしれない */
#define STEP RDMA_MIN_SIZE

#define TCP_PORT 8352

#define POLL(x) while (x == 0) { sched_yield(); }

const int request_size = 256;

void display_received(char * received, int length)
{
  printf("*** ");
  for (int i = 0; i < 6; ++i) {
    printf("%d ", received[i]);
  }

  printf("... ");

  for (int i = length-5; i < length; ++i) {
    printf("%d ", received[i]);
  }

  printf("\n");
}

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

static void extract_receive_request(char *request,
                                    char *key,
                                    size_t *keylen,
                                    uint32_t *ptr_key,
                                    uint64_t *ptr_addr)
{
  size_t len;
  extract_mr(request, ptr_key, ptr_addr);
  *keylen = len = (size_t)request[12];
  memcpy(key, request + 13, len);
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

static void act_as_sender(resource_t *res)
{
  struct ibv_sge  sge, sge_buf;
  struct ibv_send_wr *sr, wr, *bad_wr = NULL;
  char *request = calloc(request_size, 1);
  char *buf     = calloc(SIZE, 1);
  char *data    = calloc(RDMA_MIN_SIZE, 1);
  struct ibv_mr *request_mr, *data_mr;

  sprintf(data, "Hello! From sender.");

  create_sge(res, buf, SIZE, &sge_buf);
  sr = calloc(1, sizeof(*sr));

  TEST_NZ(request_mr = ibv_reg_mr(res->pd, request, request_size, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE));

  format_mr(request_mr, buf);

  TEST_Z( post_ibsend(res, IBV_WR_SEND, &sge_buf, sr, 1, 1) );

  TEST_NZ(data_mr = ibv_reg_mr(res->pd, data, RDMA_MIN_SIZE, IBV_ACCESS_LOCAL_WRITE));
  prepare_rdma_write_wr(data_mr, &sge, &wr);

  wait_complete(res, SCQ_FLG);

  while (request[request_size-2] == 0) {
    uint32_t peer_key;
    uint64_t peer_addr;
    char key[242];
    size_t key_len;

    DEBUG {printf("waiting request\n");}

    /* polling */
    POLL(request[request_size-1]);
    request[request_size-1] = 0;

    extract_receive_request(request, key, &key_len, &peer_key, &peer_addr);

    TEST_Z(strncmp(key, "test", key_len));

    wr.wr.rdma.remote_addr = peer_addr;
    wr.wr.rdma.rkey = peer_key;

    data[RDMA_MIN_SIZE-1] = 255; /* check bit */

    /* issue mem copy */
    TEST_Z(ibv_post_send(res->qp, &wr, &bad_wr));
    /* this can be postponed */
    wait_complete(res, SCQ_FLG);
  }

  ibv_dereg_mr(request_mr);
  ibv_dereg_mr(data_mr);

  free(request);
  free(data);
  free(buf);
}

static void act_as_receiver(resource_t *res)
{
  struct ibv_sge  sge, sge_buf;
  struct ibv_send_wr *sr, wr, *bad_wr = NULL;
  char *request = calloc(request_size, 1);
  char *buf     = calloc(SIZE, 1);
  char *data    = calloc(RDMA_MIN_SIZE, 1);
  struct ibv_mr *request_mr, *data_mr;
  uint32_t sender_key;
  uint64_t sender_addr;

  create_sge(res, buf, SIZE, &sge_buf);
  sr = calloc(1, sizeof(*sr));

  TEST_NZ(request_mr = ibv_reg_mr(res->pd, request, request_size, IBV_ACCESS_LOCAL_WRITE));
  TEST_NZ(data_mr = ibv_reg_mr(res->pd, data, RDMA_MIN_SIZE, IBV_ACCESS_LOCAL_WRITE |  IBV_ACCESS_REMOTE_WRITE));

  /* get sender's request buffer */
  TEST_Z(post_ibreceive(res, &sge_buf, 1));
  wait_complete(res, RCQ_FLG);

  extract_mr(buf, &sender_key, &sender_addr);

  /* prepare request send work */
  prepare_rdma_write_wr(request_mr, &sge, &wr);

  wr.wr.rdma.remote_addr = sender_addr;
  wr.wr.rdma.rkey = sender_key;

  for (int i = 0; i < 10; ++i) {
    format_receive_request("test", 4, data_mr, request);
    TEST_Z(ibv_post_send(res->qp, &wr, &bad_wr));

    /* this can be postponed or interleaved with data poll */
    wait_complete(res, SCQ_FLG);

    DEBUG {printf("waiting response\n");}

    /* poll last byte */
    POLL(data[RDMA_MIN_SIZE-1]);
    data[RDMA_MIN_SIZE-1] = 0;
    printf("Received: %s\n", data);
  }
  /* quit request */
  format_receive_request("test", 4, data_mr, request);
  request[request_size - 2] = 1;
  TEST_Z(ibv_post_send(res->qp, &wr, &bad_wr));
  wait_complete(res, SCQ_FLG);

  ibv_dereg_mr(request_mr);
  ibv_dereg_mr(data_mr);

  free(request);
  free(data);
  free(buf);
}

int
main(int argc, char *argv[])
{
  const int ib_port = 1;
  int server = 0;
  int server_sock = -1;
  int sfd = -1;

  if (argc == 3) {
    // host port
    DEBUG { printf("Starting as Client to %s\n", argv[1]); }
    server = 0;
    sfd = connect_peer(argv[1], atoi(argv[2]));
  } else {
    // port
    struct sockaddr_in client;
    uint len;

    DEBUG { printf("Starting as Server\n"); }
    server = 1;
    server_sock = open_server(atoi(argv[1]));
    if (server_sock == -1) {
      fprintf(stderr, "Failed to start server\n");
      goto end;
    }
    len = sizeof(client);
    sfd = accept(server_sock, (struct sockaddr *)&client, &len);
  }

  if (sfd < 0) {
    fprintf(stderr, "Failed to establish connection (sfd = %d)\n", sfd);
    goto end;
  }

  if (server) {
    char *recv;
    char send[] = "CHECK";
    read_safe(sfd, &recv);
    TEST_Z(strcmp("CHECK", recv));
    free(recv);
    write_safe(sfd, send, strlen(send));
  } else {
    char send[] = "CHECK";
    char *recv;
    write_safe(sfd, send, strlen(send));
    read_safe(sfd, &recv);
    TEST_Z(strcmp("CHECK", recv));
    free(recv);
  }

  TEST_Z( resource_create(&res, ib_port, server) );
  TEST_Z( connect_qp(&res, sfd, ib_port, gid_by_hostname(), server) );

  printf("[%d] START\n", server);

  if (server) {
    act_as_sender(&res);
  } else {
    act_as_receiver(&res);
  }

 end:
  if (sfd >= 0) {
    close(sfd);
    sfd = -1;
  }
  if (server_sock >= 0) {
    close(server_sock);
    server_sock = -1;
  }
  resource_destroy(&res);
  return 0;
}
