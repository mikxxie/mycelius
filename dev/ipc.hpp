#pragma once
extern "C" {
#include "dev/ipc.h"
}

namespace mycelius {

class Message {
public:
    uint32_t type;
    uint32_t reserved;
    uint64_t value;

    Message() : type(0), reserved(0), value(0) {}
    Message(uint32_t t, uint64_t v) : type(t), reserved(0), value(v) {}
};

enum class MessageType : unsigned {
    None = MYCELIUS_MSG_NONE,
    ArpReply = MYCELIUS_MSG_ARP_REPLY,
    BlkRequest = MYCELIUS_MSG_BLK_REQUEST,
    NetRx = MYCELIUS_MSG_NET_RX,
};

inline int send(u32 to, const Message &msg) {
    return mycelius_send(to, &msg);
}

inline int recv(u32 to, u32 *from, MessageType *type, u64 *value) {
    u32 t;
    int r = mycelius_recv(to, from, &t, value);
    if (r == 0 && type) *type = static_cast<MessageType>(t);
    return r;
}

inline int exit(int code) {
    mycelius_exit(code);
    return code; /* unreachable */
}

inline int write(int fd, const void *buf, size_t count) {
    return mycelius_write(fd, buf, count);
}

} // namespace mycelius
