#include "kernel.h"
/* Minimal Ethernet/ARP/IPv4/ICMP stack over the legacy virtio-net transport.
 * Synchronous and bounded, matching the bring-up style of the blk driver. */

#define ETH_HDR_LEN 14
#define ETH_TYPE_ARP 0x0806
#define ETH_TYPE_IPV4 0x0800
#define IP_PROTO_ICMP 1
#define ARP_OP_REQUEST 1
#define ARP_OP_REPLY 2
#define ICMP_ECHO_REQUEST 8
#define ICMP_ECHO_REPLY 0
#define ARP_CACHE_SIZE 4

static const u8 net_ip[4] = { 10, 0, 2, 15 };
static const u8 eth_bcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
static u8 net_mac[6];
static int net_up;
static int net_ready;

static u8 rx_buf[10 + 1514] __attribute__((aligned(16)));
static u8 tx_buf[ETH_HDR_LEN + 1500] __attribute__((aligned(16)));

struct arp_entry { u8 ip[4]; u8 mac[6]; };
static struct arp_entry arp_cache[ARP_CACHE_SIZE];

static u16 ping_id;
static u16 ping_seq;
static int ping_got_reply;

static void copy_bytes(void *dst, const void *src, u32 n) {
    u8 *d = dst; const u8 *s = src;
    while (n--) *d++ = *s++;
}

static int mac_equal(const u8 *a, const u8 *b) {
    for (u32 i = 0; i < 6; ++i) if (a[i] != b[i]) return 0;
    return 1;
}

static int ip_equal(const u8 *a, const u8 *b) {
    for (u32 i = 0; i < 4; ++i) if (a[i] != b[i]) return 0;
    return 1;
}

static u16 csum(const u8 *p, u32 len) {
    u32 sum = 0;
    while (len > 1) { sum += ((u32)p[0] << 8) | p[1]; p += 2; len -= 2; }
    if (len) sum += (u32)p[0] << 8;
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return (u16)~sum;
}

static void arp_learn(const u8 ip[4], const u8 mac[6]) {
    for (u32 i = 0; i < ARP_CACHE_SIZE; ++i)
        if (ip_equal(arp_cache[i].ip, ip)) { copy_bytes(arp_cache[i].mac, mac, 6); return; }
    for (u32 i = 0; i < ARP_CACHE_SIZE; ++i)
        if (!arp_cache[i].ip[0]) { copy_bytes(arp_cache[i].ip, ip, 4); copy_bytes(arp_cache[i].mac, mac, 6); return; }
    copy_bytes(arp_cache[0].ip, ip, 4); copy_bytes(arp_cache[0].mac, mac, 6);
}

static int arp_lookup(const u8 ip[4], u8 mac[6]) {
    for (u32 i = 0; i < ARP_CACHE_SIZE; ++i)
        if (ip_equal(arp_cache[i].ip, ip)) { copy_bytes(mac, arp_cache[i].mac, 6); return 0; }
    return -1;
}

static void eth_tx(const u8 dst[6], u16 type, const u8 *payload, u16 len) {
    u8 *f = tx_buf;
    copy_bytes(f, dst, 6);
    copy_bytes(f + 6, net_mac, 6);
    f[12] = (u8)(type >> 8); f[13] = (u8)type;
    copy_bytes(f + ETH_HDR_LEN, payload, len);
    virtio_net_tx(f, ETH_HDR_LEN + len);
}

static void arp_send(u16 op, const u8 dst_mac[6], const u8 sender_ip[4], const u8 target_ip[4],
                    const u8 target_mac[6]) {
    u8 p[28];
    p[0] = 0; p[1] = 1; /* ethernet */
    p[2] = 0x08; p[3] = 0x00; /* ipv4 */
    p[4] = 6; p[5] = 4;
    p[6] = (u8)(op >> 8); p[7] = (u8)op;
    copy_bytes(p + 8, net_mac, 6);      /* sha */
    copy_bytes(p + 14, sender_ip, 4);   /* spa */
    copy_bytes(p + 18, target_mac, 6);  /* tha */
    copy_bytes(p + 24, target_ip, 4);   /* tpa */
    eth_tx(dst_mac, ETH_TYPE_ARP, p, 28);
}

static void handle_arp(const u8 *p, u16 len) {
    if (len < 28) return;
    u16 op = ((u16)p[6] << 8) | p[7];
    arp_learn(p + 14, p + 8); /* learn sender: spa -> sha */
    if (op != ARP_OP_REQUEST || !ip_equal(p + 24, net_ip)) return;
    arp_send(ARP_OP_REPLY, p + 8, net_ip, p + 14, p + 8);
}

static void handle_ipv4(const u8 *p, u16 len, const u8 src_mac[6]) {
    if (len < 20 || (p[0] >> 4) != 4) return;
    u16 total = ((u16)p[2] << 8) | p[3];
    if (total < 20 || total > len) return;
    if (!ip_equal(p + 16, net_ip)) return;
    if (p[9] != IP_PROTO_ICMP) return;
    const u8 *icmp = p + 20;
    u16 icmp_len = total - 20;
    if (icmp_len < 8 || csum(icmp, icmp_len) != 0) return;
    if (icmp[0] == ICMP_ECHO_REPLY) {
        u16 id = ((u16)icmp[4] << 8) | icmp[5];
        u16 seq = ((u16)icmp[6] << 8) | icmp[7];
        if (id == ping_id && seq == ping_seq) ping_got_reply = 1;
        return;
    }
    if (icmp[0] != ICMP_ECHO_REQUEST) return;
    u8 reply[20 + 1500];
    copy_bytes(reply + 20, icmp, icmp_len);
    reply[20] = ICMP_ECHO_REPLY; reply[21] = 0;
    reply[22] = 0; reply[23] = 0;
    u16 c = csum(reply + 20, icmp_len);
    reply[22] = (u8)(c >> 8); reply[23] = (u8)c;
    reply[0] = 0x45; reply[1] = 0;
    reply[2] = (u8)((20 + icmp_len) >> 8); reply[3] = (u8)(20 + icmp_len);
    reply[4] = 0; reply[5] = 1;
    reply[6] = 0; reply[7] = 0;
    reply[8] = 64; reply[9] = IP_PROTO_ICMP;
    reply[10] = 0; reply[11] = 0;
    copy_bytes(reply + 12, net_ip, 4);
    copy_bytes(reply + 16, p + 12, 4);
    u16 ipc = csum(reply, 20);
    reply[10] = (u8)(ipc >> 8); reply[11] = (u8)ipc;
    eth_tx(src_mac, ETH_TYPE_IPV4, reply, 20 + icmp_len);
}

static void net_rx_frame(const u8 *f, u16 len) {
    if (len < ETH_HDR_LEN) return;
    if (!mac_equal(f, net_mac) && !mac_equal(f, eth_bcast)) return;
    u16 type = ((u16)f[12] << 8) | f[13];
    if (type == ETH_TYPE_ARP) handle_arp(f + ETH_HDR_LEN, len - ETH_HDR_LEN);
    else if (type == ETH_TYPE_IPV4) handle_ipv4(f + ETH_HDR_LEN, len - ETH_HDR_LEN, f + 6);
}

void net_poll(void) {
    if (!net_up || !net_ready) return;
    for (u32 i = 0; i < 8; ++i) {
        int n = virtio_net_rx(rx_buf, sizeof rx_buf);
        if (n < 0) return;
        if (n < 10 + ETH_HDR_LEN) continue;
        net_rx_frame(rx_buf + 10, (u16)(n - 10));
    }
}

static int parse_ip(const char *s, u8 ip[4]) {
    for (u32 i = 0; i < 4; ++i) {
        u32 v = 0, digits = 0;
        while (*s >= '0' && *s <= '9' && digits < 3) { v = v * 10 + (u32)(*s - '0'); ++s; ++digits; }
        if (!digits || v > 255) return -1;
        ip[i] = (u8)v;
        if (i < 3) { if (*s != '.') return -1; ++s; }
    }
    return *s ? -1 : 0;
}

int net_ping(const char *s) {
    u8 ip[4], mac[6];
    if (!net_ready) { uart_puts("net: virtio-net transport not attached\r\n"); return -1; }
    if (!net_up) { uart_puts("net: interface down\r\n"); return -1; }
    if (!s || parse_ip(s, ip)) { uart_puts("net: usage: net ping <ip>\r\n"); return -1; }
    if (arp_lookup(ip, mac) != 0) {
        arp_send(ARP_OP_REQUEST, eth_bcast, net_ip, ip, eth_bcast);
        for (u32 i = 0; i < 60 && arp_lookup(ip, mac); ++i) net_poll();
        if (arp_lookup(ip, mac)) { uart_puts("net: arp timeout\r\n"); return -1; }
    }
    ping_id = 0x4d4e; /* "MN" */
    ping_seq = 1;
    ping_got_reply = 0;
    u8 payload[32];
    for (u32 i = 0; i < sizeof payload; ++i) payload[i] = (u8)i;
    u8 pkt[20 + 8 + sizeof payload];
    pkt[20] = ICMP_ECHO_REQUEST; pkt[21] = 0; pkt[22] = 0; pkt[23] = 0;
    pkt[24] = (u8)(ping_id >> 8); pkt[25] = (u8)ping_id;
    pkt[26] = (u8)(ping_seq >> 8); pkt[27] = (u8)ping_seq;
    copy_bytes(pkt + 28, payload, sizeof payload);
    u16 c = csum(pkt + 20, 8 + sizeof payload);
    pkt[22] = (u8)(c >> 8); pkt[23] = (u8)c;
    pkt[0] = 0x45; pkt[1] = 0;
    pkt[2] = (u8)((sizeof pkt) >> 8); pkt[3] = (u8)sizeof pkt;
    pkt[4] = 0; pkt[5] = 2; pkt[6] = 0; pkt[7] = 0;
    pkt[8] = 64; pkt[9] = IP_PROTO_ICMP;
    pkt[10] = 0; pkt[11] = 0;
    copy_bytes(pkt + 12, net_ip, 4);
    copy_bytes(pkt + 16, ip, 4);
    u16 ipc = csum(pkt, 20);
    pkt[10] = (u8)(ipc >> 8); pkt[11] = (u8)ipc;
    eth_tx(mac, ETH_TYPE_IPV4, pkt, sizeof pkt);
    for (u32 i = 0; i < 120; ++i) {
        net_poll();
        if (ping_got_reply) { uart_puts("net: reply from "); uart_puts(s); uart_puts("\r\n"); return 0; }
    }
    uart_puts("net: no reply from "); uart_puts(s); uart_puts("\r\n");
    return -1;
}

void net_init(void) {
    if (virtio_net_mac(net_mac) != 0) return;
    net_ready = 1;
}

void net_set_up(int up) {
    net_up = up != 0;
    uart_puts(net_up ? "net0: administratively up\r\n" : "net0: administratively down\r\n");
}

void net_status(void) {
    if (!net_ready) { uart_puts("net0: transport not attached\r\n"); return; }
    uart_puts("net0: ");
    uart_puts(net_up ? "UP" : "DOWN");
    uart_puts(" mac=");
    for (u32 i = 0; i < 6; ++i) {
        char out[2] = { 0, 0 };
        u32 d = (net_mac[i] >> 4) & 15;
        out[0] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        uart_puts(out);
        d = net_mac[i] & 15;
        out[0] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        uart_puts(out);
        if (i < 5) uart_puts(":");
    }
    uart_puts(" ip=10.0.2.15\r\n");
}
