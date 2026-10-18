#pragma once
#include <stdint.h>
#include <stddef.h>

/* Mycelius network API - public header for userland network clients. */

/* Ethernet frame constants */
#define MYCELIUS_ETH_HDR_LEN   14
#define MYCELIUS_ETH_ADDR_LEN  6
#define MYCELIUS_ETH_TYPE_ARP  0x0806
#define MYCELIUS_ETH_TYPE_IPV4 0x0800

/* ARP op codes */
#define MYCELIUS_ARP_OP_REQUEST 1
#define MYCELIUS_ARP_OP_REPLY   2

/* IP protocol numbers */
#define MYCELIUS_IP_PROTO_ICMP 1

/* ICMP types */
#define MYCELIUS_ICMP_ECHO_REQUEST 8
#define MYCELIUS_ICMP_ECHO_REPLY   0

/* Default Mycelius IP address assigned by QEMU user networking (slirp).
 * Host (QEMU) is at 10.0.2.2. Guest is typically 10.0.2.15. */
#define MYCELIUS_IP_DEFAULT { 10, 0, 2, 15 }

struct mycelius_eth_addr {
    uint8_t addr[MYCELIUS_ETH_ADDR_LEN];
};

struct mycelius_ip_addr {
    uint8_t addr[4];
};

struct mycelius_arp_entry {
    uint8_t ip[4];
    uint8_t mac[MYCELIUS_ETH_ADDR_LEN];
};

/* Send an Ethernet frame. Returns 0 on success, -1 on failure. */
int mycelius_net_send(const uint8_t *dst_mac, uint16_t eth_type,
                      const uint8_t *payload, uint16_t len);

/* Send an ARP request for the given IP. Returns 0 on success. */
int mycelius_net_arp_request(const uint8_t *target_ip);

/* Send an ICMP echo request to dst_ip. Returns 0 on success. */
int mycelius_net_icmp_echo(const uint8_t *dst_ip, uint16_t id,
                           uint16_t seq, const uint8_t *data,
                           uint16_t data_len);

/* Get our own IP address. Returns 0 on success. */
int mycelius_net_get_ip(uint8_t ip[4]);

/* Get our own MAC address. Returns 0 on success. */
int mycelius_net_get_mac(uint8_t mac[MYCELIUS_ETH_ADDR_LEN]);
