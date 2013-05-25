#include "ib.h"

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

resource_t  res;

int open_server(int port);
int connect_peer(char *hostaddr, int port);
int read_safe(int fd, char **data);
int write_safe(int fd, char *data, int len);

int gid_by_hostname();
int connect_qp(resource_t *res, int fd, int ib_port, int gid_idx, int server);

#define SIZE  128
#define RDMA_MIN_SIZE 4096
#define RDMA_MAX_SIZE RDMA_MIN_SIZE*4 /* size は alignment が大事かもしれない */
#define STEP RDMA_MIN_SIZE

#define TCP_PORT 8352

#define TIMES 10000
#define SIZE  16000

char  buf[SIZE];

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

void
sync(int server, int sfd)
{
  char send[] = "SYNC";
  char *recv = NULL;
  if (server) {
    read_safe(sfd, &recv);
    TEST_Z(strcmp(send, recv));
    write_safe(sfd, send, strlen(send));
  } else {
    write_safe(sfd, send, strlen(send));
    read_safe(sfd, &recv);
    TEST_Z(strcmp(send, recv));
  }
  if (recv)
    free(recv);
}

static void report(const char * type, const int server, const double elapsed)
{
  printf("%s (%s) =>\n", type, server ? "server" : "client");
  printf("\tsize\t%d\n", SIZE);
  printf("\ttimes\t%d\n", TIMES);
  printf("\tinterval\t%lf [sec]\n", elapsed);
  printf("\tperformance\t%lf [req/sec]\n\n", (double)TIMES/elapsed);
}

static void bench_tcp(int server, int sfd)
{
  const char test = "TCP";
  const char data[SIZE] = "";
  const char ack[] = "OK\r\n";
  struct timeval begin, end;
  double elapsed;
  int i;

  memset(data, 'O', SIZE);

  gettimeofday(&begin, NULL);
  if (server) {
    for (int i = 0; i < TIMES; ++i) {
      const char *recv = NULL;

      int length = read_safe(sfd, &recv)
      if (length == -1) {
        fprintf(stderr, "Error in bench_tcp (server, read)\n");
        return;
      }
      if (length != SIZE) {
        fprintf(stderr, "Error in bench_tcp (server, data lost)\n");
        return;
      }
      if (write_safe(sfd, ack, strlen(ack)) == -1) {
        fprintf(stderr, "Error in bench_tcp (server, send)\n");
        return;
      }

      if (recv)
        free(recv);
    }
  } else {
    for (int i = 0; i < TIMES; ++i) {
      const char *recv = NULL;

      if (write_safe(sfd, data, SIZE) == -1) {
        fprintf(stderr, "Error in bench_tcp (client, send)\n");
        return;
      }
      if (read_safe(sfd, &recv) == -1) {
        fprintf(stderr, "Error in bench_tcp (client, read)\n");
        return;
      }

      if (recv)
        free(recv);
    }
  }
  gettimeofday(&end, NULL);
  elapsed = get_interval(begin, end);

  report("TCP", server, elapsed);
}

static void bench_ib_send_recv(int server, resource_t *res)
{
  const char data[SIZE] = "";
  const char ack[] = "OK\r\n";
  struct timeval begin, end;
  struct ibv_sge  sge_data, sge_msg;
  struct ibv_wc  wc;
  struct ibv_send_wr  *sr, *bad_wr = NULL;
  double elapsed;
  int i;

  memset(data, 'O', SIZE);

  create_sge(&res, data, SIZE, &sge_data);
  create_sge(&res, ack, strlen(ack), &sge_msg);
  memset(&wc, 0, sizeof(struct ibv_wc));
  sr = malloc(sizeof(*sr));
  memset(sr, 0, sizeof(*sr));

  gettimeofday(&begin, NULL);
  for (int i = 0; i < TIMES; ++i) {
    if (server) {
      post_ibreceive(&res, &sge_data, 1);
	    while (poll_cq(&res, &wc, 1, RCQ_FLG) == 0) {
	    }
      post_ibsend(&res, IBV_WR_SEND, &sge_msg, sr, 1)
	    while ((rc = poll_cq(&res, &wc, 1, SCQ_FLG)) == 0) {
	    }
    } else {
      post_ibsend(&res, IBV_WR_SEND, &sge_data, sr, 1)
	    while ((rc = poll_cq(&res, &wc, 1, SCQ_FLG)) == 0) {
	    }
      post_ibreceive(&res, &sge_msg, 1);
	    while (poll_cq(&res, &wc, 1, RCQ_FLG) == 0) {
	    }
    }
  }
  gettimeofday(&end, NULL);
  elapsed = get_interval(begin, end);

  report("ib_send_recv", server, elapsed);
}

static int bench_rdma_ib(int server, int sfd)
{
  const char data[SIZE] = "";
  const char buf[128];
  struct timeval begin, end;
  struct ibv_sge  sge, sge_buf;
  struct ibv_wc  wc;
  struct ibv_send_wr  wr, *bad_wr = NULL;
  double elapsed;
  int i;
  struct ibv_mr *mr;

  memset(data, 'O', SIZE);

  create_sge(&res, buf, 128, &sge_buf);
  memset(&wc, 0, sizeof(struct ibv_wc));
  sr = malloc(sizeof(*sr));
  memset(sr, 0, sizeof(*sr));

  gettimeofday(&begin, NULL);
  for (int i = 0; i < TIMES; ++i) {
    if (server) {
      uint32_t peer_key;
      uint64_t peer_addr;

      /* receive_peer_mr */
      TEST_Z(post_ibreceive(&res, &sge_buf, 1));

      TEST_NZ(mr = ibv_reg_mr(res.pd, data, SIZE, IBV_ACCESS_LOCAL_WRITE));
      memset(&wr, 0, sizeof(wr));

      while (poll_cq(&res, &wc, 1, RCQ_FLG) == 0) {
      }
      peer_key  = BE_TO_INT(buf);
      peer_addr = BE_TO_INT(buf + 4);
      peer_addr = (peer_addr << 32) | BE_TO_INT(buf + 8);

      sge.addr = (intptr_t)data;
      sge.length = SIZE;
      sge.lkey = mr->lkey;

      wr.sg_list = &sge;
      wr.num_sge = 1;
      wr.opcode = IBV_WR_RDMA_READ;

      wr.wr.rdma.remote_addr = peer_addr;
      wr.wr.rdma.rkey = peer_key;

      TEST_Z(ibv_post_send(res.qp, &wr, &bad_wr));
      while ((rc = poll_cq(&res, &wc, 1, SCQ_FLG)) == 0) {
      }

      ibv_dereg_mr(mr);

      /* notify done */
      sprintf(buf, "Done.");
      TEST_Z(post_ibsend(&res, IBV_WR_SEND, &sge_buf, sr, 1));
      while ((rc = poll_cq(&res, &wc, 1, SCQ_FLG)) == 0) {
      }
    } else {
      TEST_NZ( mr = ibv_reg_mr(res.pd, data, SIZE, IBV_ACCESS_REMOTE_READ) );

      INT_TO_BE(buf, mr->rkey);
      INT_TO_BE(buf + 4, (((intptr_t)mr->addr) >> 32));
      INT_TO_BE(buf + 8, (((intptr_t)mr->addr) & 0xffffffff));
      TEST_Z( post_ibsend(&res, IBV_WR_SEND, &sge_buf, sr, 1) );
      while ((rc = poll_cq(&res, &wc, 1, SCQ_FLG)) == 0) {
      }

      /* wait for done */
      TEST_Z( post_ibreceive(&res, &sge_buf, 1) );
      while (poll_cq(&res, &wc, 1, RCQ_FLG) == 0) {
      }

      ibv_dereg_mr(mr);
    }
  }
  gettimeofday(&end, NULL);
  elapsed = get_interval(begin, end);

  report("rdma_ib", server, elapsed);
}

static int bench_rdma_reuse(int server, int sfd)
{
  const char data[SIZE] = "";
  const char buf[128];
  struct timeval begin, end;
  struct ibv_sge  sge;
  struct ibv_wc  wc;
  struct ibv_send_wr  wr, *bad_wr = NULL;
  double elapsed;
  int i;
  struct ibv_mr *mr;

  memset(data, 'O', SIZE);

  create_sge(&res, buf, 128, &sge);
  memset(&wc, 0, sizeof(struct ibv_wc));
  sr = malloc(sizeof(*sr));
  memset(sr, 0, sizeof(*sr));

  if (server) {
    uint32_t peer_key;
    uint64_t peer_addr;

    TEST_NZ(mr = ibv_reg_mr(res.pd, data, SIZE, IBV_ACCESS_LOCAL_WRITE));
    memset(&wr, 0, sizeof(wr));

    /* receive_peer_mr */
    TEST_Z(post_ibreceive(&res, &sge, 1));
    while (poll_cq(&res, &wc, 1, RCQ_FLG) == 0) {
    }
    peer_key  = BE_TO_INT(buf);
    peer_addr = BE_TO_INT(buf + 4);
    peer_addr = (peer_addr << 32) | BE_TO_INT(buf + 8);

    sge.addr = (intptr_t)received;
    sge.length = size;
    sge.lkey = mr->lkey;

    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.opcode = IBV_WR_RDMA_READ;

    wr.wr.rdma.remote_addr = peer_addr;
    wr.wr.rdma.rkey = peer_key;
  } else {
    TEST_NZ( mr = ibv_reg_mr(res.pd, data, SIZE, IBV_ACCESS_REMOTE_READ) );

    INT_TO_BE(buf, mr->rkey);
    INT_TO_BE(buf + 4, (((intptr_t)mr->addr) >> 32));
    INT_TO_BE(buf + 8, (((intptr_t)mr->addr) & 0xffffffff));

    TEST_Z( post_ibsend(&res, IBV_WR_SEND, &sge, sr, 1) );
    while ((rc = poll_cq(&res, &wc, 1, SCQ_FLG)) == 0) {
    }
  }

  gettimeofday(&begin, NULL);
  for (int i = 0; i < TIMES; ++i) {
    char *other = malloc(sizeof(data));
    if (server) {

      TEST_Z( post_ibreceive(&res, &sge, 1) );
      while (poll_cq(&res, &wc, 1, RCQ_FLG) == 0) {
      }

      /* issue mem copy */
      TEST_Z(ibv_post_send(res.qp, &wr, &bad_wr));
      while ((rc = poll_cq(&res, &wc, 1, SCQ_FLG)) == 0) {
      }

      memcpy(other, data, SIZE);

      /* notify done */
      sprintf(buf, "Done.");
      TEST_Z(post_ibsend(&res, IBV_WR_SEND, &sge, sr, 1));
      while ((rc = poll_cq(&res, &wc, 1, SCQ_FLG)) == 0) {
      }
    } else {
      memcpy(data, other, SIZE);

      sprintf(buf, "DoIt");
      TEST_Z( post_ibsend(&res, IBV_WR_SEND, &sge, sr, 1) );
      while ((rc = poll_cq(&res, &wc, 1, SCQ_FLG)) == 0) {
      }

      /* wait for done */
      TEST_Z( post_ibreceive(&res, &sge, 1) );
      while (poll_cq(&res, &wc, 1, RCQ_FLG) == 0) {
      }
    }
    free(other);
  }
  gettimeofday(&end, NULL);
  elapsed = get_interval(begin, end);

  ibv_dereg_mr(mr);

  report("rdma_ib", server, elapsed);
}


int
main(int argc, char *argv[])
{
  int    ib_port = 1;
  int    rc;
  unsigned long long start, end;
  float time;

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

  sync(server, sfd);

  TEST_Z( resource_create(&res, ib_port, server) );
  TEST_Z( connect_qp(&res, sfd, ib_port, gid_by_hostname(), server) );

  sync(server, sfd);
  printf("[%d] START\n", server);

  bench_tcp(server, sfd);
  sync(server, sfd);

  bench_ib_send_recv(server, &res);
  sync(server, sfd);

  bench_rdma_ib(server, &res);
  sync(server, sfd);

  bench_rdma_reuse(server, &res);
  sync(server, sfd);

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
