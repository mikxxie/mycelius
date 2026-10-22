#pragma once
extern "C" {
#include "dev/net.h"
}

namespace mycelius {

inline int net_send(const u8 *dst_mac, u16 eth_type,
                    const u8 *payload, u16 len) {
    return mycelius_net_send(dst_mac, eth_type, payload, len);
}

inline int net_arp_request(const u8 *target_ip) {
    return mycelius_net_arp_request(target_ip);
}

inline int net_icmp_echo(const u8 *dst_ip, u16 id,
                         u16 seq, const u8 *data,
                         u16 data_len) {
    return mycelius_net_icmp_echo(dst_ip, id, seq, data, data_len);
}

inline int net_get_ip(u8 ip[4]) {
    return mycelius_net_get_ip(ip);
}

inline int net_get_mac(u8 mac[ETH_ADDR_LEN]) {
    return mycelius_net_get_mac(mac);
}

} // namespace mycelius
