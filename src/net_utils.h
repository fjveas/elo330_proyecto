#ifndef __NET_UTILS_H__
#define __NET_UTILS_H__

#include <stdint.h>

#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/time.h>

#define LISTEN_BACKLOG  16
#define STATION_BUFSZ   192

#define max(x, y) ({ \
	__typeof__(x) _x = (x); \
	__typeof__(x) _y = (y); \
	(_x > _y) ? _x : _y; \
})

int test_connection(int sockfd);
void *resolve_host(const char *hostname, struct sockaddr_in *addr);
int tcp_server_socket(uint16_t port, int backlog);
int tcp_client_socket(const char *hostname, uint16_t port);

ssize_t read_line(int fd, void *buffer, size_t n);

#endif /* __NET_UTILS_H__ */
