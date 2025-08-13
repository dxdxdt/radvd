#include "config.h"
#include "includes.h"
#include "radvd.h"
#include <stdbool.h>

/* Note: these are applicable to receiving sockopts only */
#if defined IPV6_HOPLIMIT && !defined IPV6_RECVHOPLIMIT
#define IPV6_RECVHOPLIMIT IPV6_HOPLIMIT
#endif

#if defined IPV6_PKTINFO && !defined IPV6_RECVPKTINFO
#define IPV6_RECVPKTINFO IPV6_PKTINFO
#endif

static bool do_sio_recvall (const int s) {
	int err;
	DWORD dwIoControlValue = RCVALL_ON;
	DWORD dwBytesRet = 0;

	err = WSAIoctl(
		s,
		SIO_RCVALL,
		&dwIoControlValue,
		sizeof(dwIoControlValue),
		NULL,
		0,
		&dwBytesRet,
		NULL,
		NULL);
	if (err != 0) {
		pwinerror("WSAIoctl()", WSAGetLastError());
		return false;
	}

	return true;
}

static bool do_bind (
		const int s,
		const unsigned long scope_id,
		size_t *nr_ifaces)
{
	const struct sockaddr_in6 *sa = NULL;
	struct addrinfo hints = {
		.ai_family = AF_INET6,
		.ai_socktype = SOCK_RAW,
		.ai_protocol = IPPROTO_ICMPV6,
		.ai_flags = AI_PASSIVE
	};
	struct addrinfo *ai = NULL;
	char hostname[256] = {0};
	int err;
	bool ret = false;

	// POSIX.1 says that if such truncation occurs, then it is unspecified
	// whether the returned buffer includes a terminating null byte.
	gethostname(hostname, sizeof(hostname) - 1);
	dlog(LOG_DEBUG, 3, "hostname: %s", hostname);
	err = getaddrinfo(hostname, NULL, &hints, &ai);
	if (err != 0) {
		flog(LOG_ERR, "getaddrinfo(): %s", gai_strerror(err));
		goto END;
	}

	if (nr_ifaces != NULL) {
		// count interfaces
		*nr_ifaces = 0;
		for (const struct addrinfo *p = ai; p != NULL; p = p->ai_next) {
			sa = (const struct sockaddr_in6*)p->ai_addr;
			if (sa->sin6_scope_id > 0) {
				*nr_ifaces += 1;
			}
		}
	}

	for (const struct addrinfo *p = ai; p != NULL; p = p->ai_next) {
		char addr_str[INET6_ADDRSTRLEN];
		addr_str[0] = 0;
		sa = (const struct sockaddr_in6*)p->ai_addr;
		inet_ntop(AF_INET6, &sa->sin6_addr, addr_str, sizeof(addr_str));
		if ((scope_id == 0 && sa->sin6_scope_id != 0) ||
			(scope_id != 0 && sa->sin6_scope_id == scope_id))
		{
			flog(LOG_INFO, "Binding to: %s%%%lu", addr_str, sa->sin6_scope_id);
			break;
		}
		dlog(LOG_DEBUG, 4, "Skipping: %s%%%lu", addr_str, sa->sin6_scope_id);
		sa = NULL;
	}

	if (sa == NULL) {
		flog(LOG_ERR, "No suitable interface found. scope_id: %lu", scope_id);
		goto END;
	}

	err = bind(s, (const struct sockaddr*)sa, sizeof(*sa));
	if (err < 0) {
		pwinerror("bind()", WSAGetLastError());
		goto END;
	}
	ret = true;

END:
	if (ai != NULL) {
		freeaddrinfo(ai);
	}

	return ret;
}

int open_icmpv6_socket(void) {
	int sock;
	int err;
	size_t nb_ifaces;

/*
 * SIO_RCVALL works with this kind of socket, but technically this is an
 * undocumented feature.
 *
 * https://learn.microsoft.com/en-us/windows/win32/winsock/sio-rcvall#remarks
 *
 * > The SIO_RCVALL IOCTL enables a socket to receive all IPv4 or IPv6 packets
 * > on a network interface. The socket handle passed to the WSAIoctl or
 * > WSPIoctl function must be one of the following:
 * >
 * > - An IPv4 socket that was created with the address family set to AF_INET,
 * >   the socket type set to SOCK_RAW, and the protocol set to IPPROTO_IP.
 * > - An IPv6 socket that was created with the address family set to AF_INET6,
 * >   the socket type set to SOCK_RAW, and the protocol set to IPPROTO_IPV6.
 */
	sock = socket(AF_INET6, SOCK_RAW, IPPROTO_ICMPV6);
	if (sock < 0) {
		flog(LOG_ERR, "can't create socket(AF_INET6): %s", strerror(errno));
		return -1;
	}

	err = setsockopt(sock, IPPROTO_IPV6, IPV6_RECVPKTINFO, (const char*)(int[]){1}, sizeof(int));
	if (err < 0) {
		flog(LOG_ERR, "setsockopt(IPV6_RECVPKTINFO): %s", strerror(errno));
		return -1;
	}

	if (false) { // not supported by WS. The macro is only defined in Mingw
		err = setsockopt(sock, IPPROTO_IPV6, IPV6_CHECKSUM, (const char*)(int[]){2}, sizeof(int));
		if (err < 0) {
			flog(LOG_ERR, "setsockopt(IPV6_CHECKSUM): %s", strerror(errno));
			return -1;
		}
	}

	err = setsockopt(sock, IPPROTO_IPV6, IPV6_UNICAST_HOPS, (const char*)(int[]){255}, sizeof(int));
	if (err < 0) {
		flog(LOG_ERR, "setsockopt(IPV6_UNICAST_HOPS): %s", strerror(errno));
		return -1;
	}

	err = setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, (const char*)(int[]){255}, sizeof(int));
	if (err < 0) {
		flog(LOG_ERR, "setsockopt(IPV6_MULTICAST_HOPS): %s", strerror(errno));
		return -1;
	}

	err = setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, (const char*)(int[]){0}, sizeof(int));
	if (err < 0) {
		flog(LOG_ERR, "setsockopt(IPV6_MULTICAST_LOOP): %s", strerror(errno));
		return -1;
	}

#ifdef IPV6_RECVHOPLIMIT
	err = setsockopt(sock, IPPROTO_IPV6, IPV6_RECVHOPLIMIT, (const char*)(int[]){1}, sizeof(int));
	if (err < 0) {
		flog(LOG_ERR, "setsockopt(IPV6_RECVHOPLIMIT): %s", strerror(errno));
		return -1;
	}
#endif

	const char *env_scopeid = getenv("RADVDUMP_SCOPE_ID");
	if (env_scopeid == NULL) {
		env_scopeid = "";
	}
	const unsigned long scopeid = (unsigned long)atol(env_scopeid);
	if (!do_bind(sock, scopeid, &nb_ifaces) ||
		!do_sio_recvall(sock))
	{
		return -1;
	}

	if (nb_ifaces > 1 && scopeid == 0) {
/*
 * TODO: Unlike POSIX/BSD sockets, Winsock raw sockets must be bound to an
 * interface before they can be used
 */
		flog(LOG_WARNING, "*** Multiple link-local addresses detected! ***");
		flog(LOG_WARNING, "You may use RADVDUMP_SCOPE_ID env var to specify an interface.");
	}

	return sock;
}
