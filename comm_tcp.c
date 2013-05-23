#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>

int write_safe(int fd, char *data, int len)
{
  int wrote_len = 0;
  while (wrote_len < len) {
    int ret = write(fd, (void*) data + wrote_len, len - wrote_len);

    if (ret == -1) {
      if (errno == EINTR || errno == EAGAIN)
        continue;
      perror("write_safe");
      return -1;
    }

    wrote_len += ret;
  }
  return 0;
}

int read_safe(int fd, char **data)
{
  int received_len = 0, current_size = BUFSIZE;
  char *buf = malloc(BUFSIZE);

  while (1) {
    int max = current_size - received_len;
    assert(max > 0);
    int ret = read(fd, buf + received_len, max);

    if (ret == -1) {
      if (errno == EINTR)
        continue;
      perror("read_safe");
      return -1;
    }

    received_len += ret;
    if (ret == max) {
      if (current_size <= received_len) {
        buf = realloc(buf, current_size*2);
        current_size *= 2;
      }
    }

    break;
  }

  *data = buf;
  return received_len;
}

struct addrinfo *resolve_host(char *host, int port)
{
  struct addrinfo hints = { 0 }, *result;
  int s;
  char port_str[10];
  sprintf(port_str, "%d", port);

  hints.ai_family   = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags    = 0;
  hints.ai_protocol = 0;

  s = getaddrinfo(host, port_str, &hints, &result);

  if (s != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
    return NULL;
  }

  return result;
}

int connect_peer(char *hostaddr, int port)
{
  struct addrinfo *host, *rp;
  int sfd;

  host = resolve_host(hostaddr, port);

  if (!host) {
    return -1;
  }

  for (rp = host; rp != NULL; rp = rp->ai_next) {
    sfd = socket(rp->ai_family, rp->ai_socktype,
                 rp->ai_protocol);
    if (sfd == -1)
      continue;

    if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1)
      break;                  /* Success */

    close(sfd);
  }

  if (rp == NULL) {
    fprintf(stderr, "no server found\n");
    exit(EXIT_FAILURE);
  }

  freeaddrinfo(host);

  return sfd;
}

int open_server(int port)
{
  int sock0 = socket(AF_INET, SOCK_STREAM, 0);

  if (sock0 == -1) {
    perror("socket (server)");
    return -1;
  }

  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = INADDR_ANY;
  if (bind(sock0, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
    perror("bind (sever)");
    return -1;
  }

  if (listen(sock0, 5) == -1) {
    perror("listen (server)");
    return -1;
  }

  return sock0;
}
