/*
 * net_utils.c:
 *
 */

#include "common.h"
#include "linked_list.h"
#include "net_utils.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <pthread.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/socket.h>

int test_connection(int sockfd)
{
	ssize_t ret;
	char c;
	int old_errno = errno;

	errno = 0;
	ret = recv(sockfd, &c, 1, MSG_DONTWAIT | MSG_PEEK);
	if (ret <= 0 && errno != EAGAIN)
		return -1;

	errno = old_errno;
	return 0;
}

void *resolve_host(const char *hostname, struct sockaddr_in *addr)
{
	struct hostent *hp = gethostbyname(hostname);

	if (hp == NULL) {
		addr->sin_addr.s_addr = inet_addr(hostname);
		hp = gethostbyaddr(&(addr->sin_addr.s_addr),
			sizeof(addr->sin_addr.s_addr), AF_INET);
	}

	return hp;
}

int tcp_server_socket(uint16_t port, int backlog)
{
	int sock;
	struct sockaddr_in addr;

	/* Create the socket. */
	sock = socket(AF_INET, SOCK_STREAM, 0);

	/* Create the address of the server. */
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = htonl(INADDR_ANY); /* Use the wildcard address.*/

	const int reuse = 1;
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);

	/* Bind the socket to the address. */
	if (bind(sock, (struct sockaddr *)&addr, sizeof addr)) {
		print_error("cannot bind the socket.\n");
		return -1;
	}

	/* Listen for connections.  */
	listen(sock, backlog);

	return sock;
}

int tcp_client_socket(const char *hostname, uint16_t port)
{
	int sock, len, flags;
	struct hostent *hp;
	struct sockaddr_in addr;
	struct timeval timeout = {.tv_sec = 5, .tv_usec = 0};
	fd_set working_fds;

	/* Look up our host's network address.*/
	if ((hp = resolve_host(hostname, &addr)) == NULL) {
		print_error("Can't find host %s\n", hostname);
		return -2;
	}

	/* Create a socket in the INET domain.*/
	sock = socket(AF_INET, SOCK_STREAM, 0);

	/* Set socket in non-blocking mode */
	flags = fcntl(sock, F_GETFL, 0);
	fcntl(sock, F_SETFL, flags | O_NONBLOCK);

	/* Create the address of the server. */
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	memcpy(&addr.sin_addr, hp->h_addr_list[0], hp->h_length);
	len = sizeof(struct sockaddr_in);

	/* Connect to the server. */
	connect(sock, (struct sockaddr *)&addr, len);

	/* Wait for timeout */
	FD_ZERO(&working_fds);
	FD_SET(sock, &working_fds);
	if (select(sock + 1, NULL, &working_fds, NULL, &timeout) == 1) {
		int so_error;
		socklen_t so_len = sizeof so_error;

		getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &so_len);
		if (so_error == 0) {
			flags = fcntl(sock, F_GETFL);
			fcntl(sock, F_SETFL, flags & ~O_NONBLOCK);
			return sock;
		}
	}

	/* Timed out */
	return -1;
}

/*
 * Read characters from 'fd' until a newline is encountered. If a newline
 * character is not encountered in the first (n - 1) bytes, then the excess
 * characters are discarded. The returned string placed in 'buf' is
 * null-terminated and includes the newline character if it was read in the
 * first (n - 1) bytes. The function return value is the number of bytes
 * placed in buffer (which includes the newline character if encountered,
 * but excludes the terminating null byte).
 *
 * Taken from "The Linux Programming Interface", Michael Kerrisk, 2010.
 */
ssize_t read_line(int fd, void *buffer, size_t n)
{
	ssize_t numRead;                    /* # of bytes fetched by last read() */
	size_t totRead;                     /* Total bytes read so far */
	char *buf;
	char ch;

	if (n <= 0 || buffer == NULL) {
		errno = EINVAL;
		return -1;
	}

	buf = buffer;                       /* No pointer arithmetic on "void *" */

	totRead = 0;
	for (;;) {
		numRead = read(fd, &ch, 1);

		if (numRead == -1) {
			if (errno == EINTR)         /* Interrupted --> restart read() */
				continue;
			else
				return -1;              /* Some other error */
		} else if (numRead == 0) {      /* EOF */
			if (totRead == 0)           /* No bytes read; return 0 */
				return 0;
			else                        /* Some bytes read; add '\0' */
				break;
		} else {                        /* 'numRead' must be 1 if we get here */
			if (totRead < n - 1) {      /* Discard > (n - 1) bytes */
				totRead++;
				*buf++ = ch;
			}
			if (ch == '\n')
				break;
		}
	}

	*buf = '\0';
	return totRead;
}
