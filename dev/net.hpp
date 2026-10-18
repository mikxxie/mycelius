#pragma once
extern "C" {
#include "dev/net.h"
}

namespace mycelius {

inline int net_send(const uint8_t *dst_mac, uint16_t eth_type,
                    const uint8_t *payload, uint16_t len) {
    return mycelius_net_send(dst_mac, eth_type, payload, len);
}

inline int net_arp_request(const uint8_t *target_ip) {
    return mycelius_net_arp_request(target_ip);
}

inline int net_icmp_echo(const uint8_t *dst_ip, uint16_t id,
                         uint16_t seq, const uint8_t *data,
                         uint16_t data_len) {
    return mycelius_net_icmp_echo(dst_ip, id, seq, data, data_len);
}

inline int net_get_ip(uint8_t ip[4]) {
    return mycelius_net_get_ip(ip);
}

inline int net_get_mac(uint8_t mac[6]) {
    return mycelius_net_get_mac(mac);
}

} // namespace mycelius
