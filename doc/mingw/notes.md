# Radvd Windows port notes

## TODO
### UAC manifest
Mingw supports `windres`, which can be used to embed manifest in the resource.

## Caveats and challenges
### if_indextoname
https://learn.microsoft.com/en-us/windows/win32/api/netioapi/nf-netioapi-if_indextoname#remarks

> The if_indextoname function maps an interface index into its corresponding
> name. This function is designed as part of basic socket extensions for IPv6 as
> described by the IETF in RFC 2553.
...
> The if_indextoname function is implemented for portability of applications
> with Unix environments,
...

And Windows doesn't do predictable interface names either. Options:

- Use LUID only?
- Use GUID?

### SIO_RCVALL on ICMPV6 raw socket is an undocumented feature
Using `SIO_RCVALL` on `socket(AF_INET6, SOCK_RAW, IPPROTO_ICMPV6)` is
technically an undocumented feature.

https://learn.microsoft.com/en-us/windows/win32/winsock/sio-rcvall#remarks

> The SIO_RCVALL IOCTL enables a socket to receive all IPv4 or IPv6 packets on a
> network interface. The socket handle passed to the WSAIoctl or WSPIoctl
> function must be one of the following:
>
> - An IPv4 socket that was created with the address family set to AF_INET, the
>   socket type set to SOCK_RAW, and the protocol set to IPPROTO_IP.
> - An IPv6 socket that was created with the address family set to AF_INET6, the
>   socket type set to SOCK_RAW, and the protocol set to IPPROTO_IPV6.

The `SOCK_RAW, IPPROTO_ICMPV6` pair is not one of the above yet it works.

### Raw socket limitations
Unlike BSD/POSIX raw sockets, Winsock raw sockets have to be bound to an
interface before they can be used(with or without SIO_RCVALL). radvd is built
upon the former assumption so the entire codebase needs rework to support
Windows.

https://learn.microsoft.com/en-us/previous-versions/windows/desktop/legacy/ms741687(v=vs.85)#return-value

> WSAEINVAL The socket has not been bound (with bind, for example).

Nowhere in the documentation is it mentioned that raw sockets must be bound.

> One common use of raw sockets are troubleshooting applications that need to
> examine IP packets and headers in detail. For example, a raw socket can be
> used with the SIO_RCVALL IOCTL to enable a socket to receive all IPv4 or IPv6
> packets passing through a network interface. For more information, see the
> SIO_RCVALL reference.

Raw sockets without the use of `SIO_RCVALL` is useless. It always has to be used
in conjunction with `SOCK_RAW`.

Another problem, performance-wise, is the lack of the ICMP filter. Due to these
limitations, implementing radvd as a userspace process is unfeasible.
