#pragma once
#include <stdint.h>
#include <stddef.h>

/* Mycelius network API - public header for userland network clients.
 * BSD-style socket interface over the virtio-net transport. */

/* Ethernet frame constants */
#define ETH_HDR_LEN   14
#define ETH_ADDR_LEN  6
#define ETH_TYPE_ARP  0x0806
#define ETH_TYPE_IPV4 0x0800

/* ARP op codes */
#define ARP_OP_REQUEST 1
#define ARP_OP_REPLY   2

/* IP protocol numbers */
#define IP_PROTO_ICMP 1

/* ICMP types */
#define ICMP_ECHO_REQUEST 8
#define ICMP_ECHO_REPLY   0

/* Default Mycelius IP address assigned by QEMU user networking (slirp).
 * Host (QEMU) is at 10.0.2.2. Guest is typically 10.0.2.15. */
#define MYCELIUS_IP_DEFAULT { 10, 0, 2, 15 }

struct mycelius_eth_addr {
    u8 addr[ETH_ADDR_LEN];
};

struct mycelius_ip_addr {
    u8 addr[4];
};

struct mycelius_arp_entry {
    u8 ip[4];
    u8 mac[ETH_ADDR_LEN];
};

/* Socket API - BSD-style socket operations over virtio-net */
struct sockaddr {
    u16 sa_family;
    u8  sa_data[14];
};

struct sockaddr_in {
    u16 sin_family;
    u16 sin_port;
    u8  sin_addr[4];
    u8  sin_zero[8];
};

int mycelius_net_socket(int domain, int type, int proto);
int mycelius_net_bind(int fd, const struct sockaddr *addr, u32 addrlen);
int mycelius_net_listen(int fd, int backlog);
int mycelius_net_accept(int fd, struct sockaddr *addr, u32 *addrlen);
int mycelius_net_connect(int fd, const struct sockaddr *addr, u32 addrlen);
int mycelius_net_send(int fd, const void *buf, u32 len, int flags);
int mycelius_net_recv(int fd, void *buf, u32 len, int flags);

/* Convenience functions - ARP and ICMP helpers */
int mycelius_net_arp_request(const u8 *target_ip);
int mycelius_net_icmp_echo(const u8 *dst_ip, u16 id, u16 seq,
                           const u8 *data, u16 data_len);

/* Get our own IP and MAC addresses. Returns 0 on success. */
int mycelius_net_get_ip(u8 ip[4]);
int mycelius_net_get_mac(u8 mac[ETH_ADDR_LEN]);
