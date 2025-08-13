#include "config.h"
#include "includes.h"
#include "radvd.h"
#include <mswsock.h>


static LPFN_WSARECVMSG get_wsarecvmsg_f (const int s) {
	LPFN_WSARECVMSG ret = NULL;
	GUID guidWSARecvMsg = WSAID_WSARECVMSG;
	DWORD dwBytesRecvd = 0;

	WSAIoctl(
		s,
		SIO_GET_EXTENSION_FUNCTION_POINTER,
		&guidWSARecvMsg,
		sizeof(guidWSARecvMsg),
		&ret,
		sizeof(ret),
		&dwBytesRecvd,
		NULL,
		NULL);

	return ret;
}

int recv_rs_ra(
		int sock,
		unsigned char *msg,
		struct sockaddr_in6 *addr,
		struct in6_pktinfo **pkt_info,
		int *hoplimit,
		unsigned char *unused0)
{
	static char chdr[
		WSA_CMSG_SPACE(sizeof(struct in6_pktinfo)) +
		WSA_CMSG_SPACE(sizeof(int))];
	LPFN_WSARECVMSG WSARecvMsg = get_wsarecvmsg_f(sock);
	WSABUF msgbuf = {
		.buf = (char*)msg,
		.len = MSG_SIZE_RECV
	};
	WSAMSG mhdr = {
		.name = (LPSOCKADDR)addr,
		.namelen = sizeof(*addr),
		.dwBufferCount = 1,
		.lpBuffers = &msgbuf,
		.Control = {
			.buf = chdr,
			.len = sizeof(chdr)
		}
	};
	int fr;
	DWORD len = 0;

	fr = WSARecvMsg(sock, &mhdr, &len, NULL, NULL);
	if (fr != 0) {
		pwinerror("WSARecvMsg()", WSAGetLastError());
		return -1;
	}

	*hoplimit = 255;

	for (
			WSACMSGHDR *cmsg = WSA_CMSG_FIRSTHDR(&mhdr);
			cmsg != NULL;
			cmsg = WSA_CMSG_NXTHDR(&mhdr, cmsg))
	{
		if (cmsg->cmsg_level != IPPROTO_IPV6)
			continue;

		switch (cmsg->cmsg_type) {
#ifdef IPV6_HOPLIMIT
		case IPV6_HOPLIMIT:
			if ((cmsg->cmsg_len == WSA_CMSG_LEN(sizeof(int))) && (*(int *)WSA_CMSG_DATA(cmsg) >= 0) &&
			    (*(int *)WSA_CMSG_DATA(cmsg) < 256)) {
				*hoplimit = *(int *)WSA_CMSG_DATA(cmsg);
			} else {
				flog(LOG_ERR, "received a bogus IPV6_HOPLIMIT from the kernel! len=%d, data=%d",
				     (int)cmsg->cmsg_len, *(int *)WSA_CMSG_DATA(cmsg));
				return -1;
			}
			break;
#endif /* IPV6_HOPLIMIT */
		case IPV6_PKTINFO:
			if ((cmsg->cmsg_len == WSA_CMSG_LEN(sizeof(struct in6_pktinfo))) &&
			    ((struct in6_pktinfo *)WSA_CMSG_DATA(cmsg))->ipi6_ifindex) {
				*pkt_info = (struct in6_pktinfo *)WSA_CMSG_DATA(cmsg);
			} else {
				flog(LOG_ERR, "received a bogus IPV6_PKTINFO from the kernel! len=%d, index=%d",
				     (int)cmsg->cmsg_len, ((struct in6_pktinfo *)WSA_CMSG_DATA(cmsg))->ipi6_ifindex);
				return -1;
			}
			break;
		}
	}

	char if_namebuf[IF_NAMESIZE] = {""};
	char *if_name = 0;
	if (pkt_info && *pkt_info) {
		if_name = if_indextoname((*pkt_info)->ipi6_ifindex, if_namebuf);
	}
	if (!if_name) {
		if_name = "unknown interface";
	}
	dlog(LOG_DEBUG, 5, "%s recvmsg len=%ld", if_name, len);

	return len;
}
