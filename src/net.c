#include "net.h"

#include <stdlib.h>
#include <string.h>

#include "uncurl/status.h"

#if defined(__WINDOWS__)
	#include <winsock2.h>
	#include <ws2tcpip.h>
	#define SHUT_RDWR 2
	#define poll WSAPoll
	#define socket_error() WSAGetLastError()
	#define SOCKET_WOULD_BLOCK WSAEWOULDBLOCK
	#define SOCKET_IN_PROGRESS WSAEWOULDBLOCK
	#define SOCKET_BAD_FD WSAENOTSOCK
	typedef int32_t socklen_t;
#elif defined(__UNIXY__)
	#include <fcntl.h>
	#include <unistd.h>
	#include <sys/socket.h>
	#include <arpa/inet.h>
	#include <netinet/tcp.h>
	#include <poll.h>
	#include <errno.h>
	#include <netdb.h>
	#define socket_error() errno
	#define SOCKET_WOULD_BLOCK EAGAIN
	#define SOCKET_IN_PROGRESS EINPROGRESS
	#define SOCKET_BAD_FD EBADF
	#define INVALID_SOCKET -1
	#define closesocket close
	typedef int32_t SOCKET;
#endif

#define net_set_sockopt(s, level, opt_name, opt) \
	setsockopt(s, level, opt_name, (const char *) &opt, sizeof(opt))

struct net_context {
	struct net_opts opts;
	SOCKET s;
};

static int32_t net_set_nonblocking(SOCKET s)
{
	#if defined(__WINDOWS__)
		u_long mode = 1;
		return ioctlsocket(s, FIONBIO, &mode);
	#elif defined(__UNIXY__)
		return fcntl(s, F_SETFL, O_NONBLOCK);
	#endif
}

static int32_t net_get_error(SOCKET s)
{
	int32_t opt;

	socklen_t size = sizeof(int32_t);
	int32_t e = getsockopt(s, SOL_SOCKET, SO_ERROR, (char *) &opt, &size);

	return e ? e : opt;
}

int32_t net_error()
{
	return socket_error();
}

int32_t net_would_block()
{
	return SOCKET_WOULD_BLOCK;
}

int32_t net_in_progress()
{
	return SOCKET_IN_PROGRESS;
}

int32_t net_bad_fd()
{
	return SOCKET_BAD_FD;
}

void net_close(struct net_context *nc)
{
	if (!nc) return;

	if (nc->s != INVALID_SOCKET) {
		shutdown(nc->s, SHUT_RDWR);
		closesocket(nc->s);
	}

	free(nc);
}

static void net_set_options(SOCKET s, struct net_opts *opts)
{
	net_set_sockopt(s, SOL_SOCKET, SO_RCVBUF, opts->read_buf);
	net_set_sockopt(s, SOL_SOCKET, SO_SNDBUF, opts->write_buf);
	net_set_sockopt(s, SOL_SOCKET, SO_KEEPALIVE, opts->keepalive);
	net_set_sockopt(s, IPPROTO_TCP, TCP_NODELAY, opts->tcp_nodelay);
}

void net_default_opts(struct net_opts *opts)
{
	opts->read_timeout = 5000;
	opts->connect_timeout = 5000;
	opts->read_buf = 64 * 1024;
	opts->write_buf = 64 * 1024;
	opts->keepalive = 1;
	opts->tcp_nodelay = 1;
}

int32_t net_poll(struct net_context *nc, int32_t net_event, int32_t timeout_ms)
{
	int32_t e;
	struct pollfd fd;

	memset(&fd, 0, sizeof(struct pollfd));

	fd.fd = nc->s;
	fd.events = (net_event == NET_POLLIN) ? POLLIN : (net_event == NET_POLLOUT) ? POLLOUT : 0;

	e = poll(&fd, 1, timeout_ms);

	return (e == 0) ? UNCURL_NET_ERR_TIMEOUT : (e < 0) ? UNCURL_NET_ERR_POLL : UNCURL_OK;
}

int32_t net_getip4(char *host, char *ip4, uint32_t ip4_len)
{
	int32_t e;
	int32_t r = UNCURL_ERR_DEFAULT;
	struct addrinfo hints;
	struct addrinfo *servinfo = NULL;

	//set to request only IP4, TCP
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	e = getaddrinfo(host, NULL, &hints, &servinfo);
	if (e != 0) {r = UNCURL_NET_ERR_RESOLVE; goto net_getip4_failure;}

	//attempt to convert the first returned address into string
	struct sockaddr_in *addr = (struct sockaddr_in *) servinfo->ai_addr;
	const char *dst = inet_ntop(AF_INET, &addr->sin_addr, ip4, ip4_len);
	if (!dst) {r = UNCURL_NET_ERR_NTOP; goto net_getip4_failure;}

	r = UNCURL_OK;

	net_getip4_failure:

	if (servinfo) freeaddrinfo(servinfo);

	return r;
}

int32_t net_connect(struct net_context **nc_in, char *ip4, uint16_t port, struct net_opts *opts)
{
	int32_t e;
	int32_t r = UNCURL_ERR_DEFAULT;

	struct net_context *nc = *nc_in = calloc(1, sizeof(struct net_context));

	//set options
	memcpy(&nc->opts, opts, sizeof(struct net_opts));

	nc->s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (nc->s == INVALID_SOCKET) {r = UNCURL_NET_ERR_SOCKET; goto net_connect_failure;}

	//set options
	net_set_options(nc->s, &nc->opts);

	//put socket in nonblocking mode, allows us to implement connection timeout
	e = net_set_nonblocking(nc->s);
	if (e != 0) {r = UNCURL_NET_ERR_BLOCKMODE; goto net_connect_failure;}

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(struct sockaddr_in));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	inet_pton(AF_INET, ip4, &addr.sin_addr);

	//initiate the socket connection
	e = connect(nc->s, (struct sockaddr *) &addr, sizeof(struct sockaddr_in));

	//initial socket state must be 'in progress' for nonblocking connect
	if (net_error() != net_in_progress()) {r = UNCURL_NET_ERR_CONNECT; goto net_connect_failure;}

	//wait for socket to be ready to write
	e = net_poll(nc, NET_POLLOUT, nc->opts.connect_timeout);
	if (e != UNCURL_OK) {r = e; goto net_connect_failure;}

	//if the socket is clear of errors, we made a successful connection
	if (net_get_error(nc->s) != 0) {r = UNCURL_NET_ERR_CONNECT_FINAL; goto net_connect_failure;}

	//success
	return UNCURL_OK;

	//cleanup on failure
	net_connect_failure:

	net_close(nc);
	*nc_in = NULL;

	return r;
}

int32_t net_write(void *ctx, char *buf, uint32_t buf_size)
{
	struct net_context *nc = (struct net_context *) ctx;

	int32_t n;
	uint32_t total = 0;

	while (total < buf_size) {
		n = send(nc->s, buf + total, buf_size - total, 0);
		if (n <= 0) return UNCURL_NET_ERR_WRITE;
		total += n;
	}

	return UNCURL_OK;
}

int32_t net_read(void *ctx, char *buf, uint32_t buf_size)
{
	struct net_context *nc = (struct net_context *) ctx;

	int32_t e;
	int32_t n;
	uint32_t total = 0;

	while (total < buf_size) {
		e = net_poll(nc, NET_POLLIN, nc->opts.read_timeout);
		if (e != UNCURL_OK) return e;

		n = recv(nc->s, buf + total, buf_size - total, 0);
		if (n <= 0) {
			if (net_error() == net_would_block()) continue;
			if (n == 0) return UNCURL_NET_ERR_CLOSED;
			return UNCURL_NET_ERR_READ;
		}

		total += n;
	}

	return UNCURL_OK;
}

int32_t net_get_fd(struct net_context *nc)
{
	return (int32_t) nc->s;
}

void net_get_opts(struct net_context *nc, struct net_opts *opts)
{
	memcpy(opts, &nc->opts, sizeof(struct net_opts));
}
