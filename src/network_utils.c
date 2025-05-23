#include "network_utils.h"

void gai_error(int code, char *msg) { /* getaddrinfo-style error */
  fprintf(stderr, "%s: %s\n", msg, gai_strerror(code));
  exit(0);
}

/*
 * open_clientfd - Open connection to server at <hostname, port> and
 *     return a socket descriptor ready for reading and writing. This
 *     function is reentrant and protocol-independent.
 *
 *     On error, returns -1 and sets errno.
 */
int open_clientfd(char *hostname, char *port) {
  int clientfd, rc;
  struct addrinfo hints, *listp, *p;

  /* Get a list of potential server addresses */
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_socktype = SOCK_STREAM; /* Open a connection */
  hints.ai_flags = AI_NUMERICSERV; /* ... using a numeric port arg. */
  hints.ai_flags |= AI_ADDRCONFIG; /* Recommended for connections */
  if ((rc = getaddrinfo(hostname, port, &hints, &listp)) != 0)
    gai_error(rc, "getaddrinfo error");

  /* Walk the list for one that we can successfully connect to */
  for (p = listp; p; p = p->ai_next) {
    /* Create a socket descriptor */
    if ((clientfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0)
      continue; /* Socket failed, try the next */

    /* Connect to the server */
    if (connect(clientfd, p->ai_addr, p->ai_addrlen) != -1)
      break;         /* Success */
    close(clientfd); /* Connect failed, try another */
  }

  /* Clean up */
  freeaddrinfo(listp);
  if (!p) /* All connects failed */
    return -1;
  else /* The last connect succeeded */
    return clientfd;
}

/* Open and return a listening socket on the given port. This function is
 * reentrant and protocol-independent.
 *
 * On error, returns -1 and sets errno.
 */
int open_listenfd(char *port) {
  struct addrinfo hints, *listp, *p;
  int listenfd, rc, optval = 1;

  /* Get a list of potential server addresses */
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_socktype = SOCK_STREAM;             /* Accept connections */
  hints.ai_flags = AI_PASSIVE | AI_ADDRCONFIG; /* ... on any IP address */
  hints.ai_flags |= AI_NUMERICSERV;            /* ... using port number */
  if ((rc = getaddrinfo(NULL, port, &hints, &listp)) != 0)
    gai_error(rc, "getaddrinfo error");

  /* Walk the list for one that we can bind to */
  for (p = listp; p; p = p->ai_next) {
    /* Create a socket descriptor */
    if ((listenfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0)
      continue; /* Socket failed, try the next */

    /* Eliminates "Address already in use" error from bind */
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval,
               sizeof(int));

    /* Bind the descriptor to the address */
    if (bind(listenfd, p->ai_addr, p->ai_addrlen) == 0)
      break;         /* Success */
    close(listenfd); /* Bind failed, try the next */
  }

  /* Clean up */
  freeaddrinfo(listp);
  if (!p) /* No address worked */
    return -1;

  /* Make it a listening socket ready to accept connection requests */
  if (listen(listenfd, LISTENQ) < 0) {
    close(listenfd);
    return -1;
  }
  return listenfd;
}

/*
 * rio_readn - Robustly read n bytes (unbuffered)
 */
ssize_t rio_readn(int fd, void *usrbuf, size_t n) {
  size_t nleft = n;
  ssize_t nread;
  char *bufp = usrbuf;

  while (nleft > 0) {
    if ((nread = read(fd, bufp, nleft)) < 0) {
      if (errno == EINTR) /* Interrupted by sig handler return */
        nread = 0;        /* and call read() again */
      else
        return -1; /* errno set by read() */
    } else if (nread == 0)
      break; /* EOF */
    nleft -= (size_t)nread;
    bufp += nread;
  }
  return (ssize_t)(n - nleft); /* return >= 0 */
}

/*
 * rio_writen - Robustly write n bytes (unbuffered)
 */
ssize_t rio_writen(int fd, char *usrbuf, size_t n) {
  size_t nleft = n;
  ssize_t nwritten;
  char *bufp = usrbuf;

  while (nleft > 0) {
    if ((nwritten = write(fd, bufp, nleft)) <= 0) {
      if (errno == EINTR) /* Interrupted by sig handler return */
        nwritten = 0;     /* and call write() again */
      else
        return -1; /* errno set by write() */
    }
    nleft -= (size_t)nwritten;
    bufp += nwritten;
  }
  return (ssize_t)n;
}

/*
 * rio_read - This is a wrapper for the Unix read() function that
 *    transfers min(n, rio_cnt) bytes from an internal buffer to a user
 *    buffer, where n is the number of bytes requested by the user and
 *    rio_cnt is the number of unread bytes in the internal buffer. On
 *    entry, rio_read() refills the internal buffer via a call to
 *    read() if the internal buffer is empty.
 */
static ssize_t rio_read(rio_t *rp, char *usrbuf, size_t n) {
  ssize_t cnt;

  while (rp->rio_cnt <= 0) { /* Refill if buf is empty */
    rp->rio_cnt = read(rp->rio_fd, rp->rio_buf,
                       sizeof(rp->rio_buf));
    if (rp->rio_cnt < 0) {
      if (errno != EINTR) /* Interrupted by sig handler return */
        return -1;
    } else if (rp->rio_cnt == 0) /* EOF */
      return 0;
    else
      rp->rio_bufptr = rp->rio_buf; /* Reset buffer ptr */
  }

  /* Copy min(n, rp->rio_cnt) bytes from internal buf to user buf */
  cnt = (ssize_t)n;
  if (rp->rio_cnt < n)
    cnt = rp->rio_cnt;
  memcpy(usrbuf, rp->rio_bufptr, cnt);
  rp->rio_bufptr += cnt;
  rp->rio_cnt -= cnt;
  return cnt;
}

/*
 * rio_readinitb - Associate a descriptor with a read buffer and reset buffer
 */
void rio_readinitb(rio_t *rp, int fd) {
  rp->rio_fd = fd;
  rp->rio_cnt = 0;
  rp->rio_bufptr = rp->rio_buf;
}

/*
 * rio_readnb - Robustly read n bytes (buffered)
 */
ssize_t rio_readnb(rio_t *rp, void *usrbuf, size_t n) {
  size_t nleft = n;
  ssize_t nread;
  char *bufp = usrbuf;

  while (nleft > 0) {
    if ((nread = rio_read(rp, bufp, nleft)) < 0)
      return -1; /* errno set by read() */
    else if (nread == 0)
      break; /* EOF */
    nleft -= (size_t)nread;
    bufp += nread;
  }
  return (ssize_t)(n - (size_t)nleft); /* return >= 0 */
}

/*
 * rio_readlineb - Robustly read a text line (buffered)
 */
ssize_t rio_readlineb(rio_t *rp, void *usrbuf, size_t maxlen) {
  ssize_t n, rc;
  char c, *bufp = usrbuf;

  for (n = 1; n < maxlen; n++) {
    if ((rc = rio_read(rp, &c, 1)) == 1) {
      *bufp++ = c;
      if (c == '\n') {
        n++;
        break;
      }
    } else if (rc == 0) {
      if (n == 1)
        return 0; /* EOF, no data read */
      else
        break; /* EOF, some data was read */
    } else
      return -1; /* Error */
  }
  *bufp = 0;
  return n - 1;
}
