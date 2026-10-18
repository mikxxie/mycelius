#pragma once
extern "C" {
#include "dev/syscall.h"
}

namespace mycelius {

inline int sys_exit(int code) {
    mycelius_exit(code);
    return code;
}

inline int sys_send(u32 from, u32 to, u32 type, u64 value) {
    return mycelius_send(from, to, type, value);
}

inline int sys_recv(u32 to, u32 *from, u32 *type, u64 *value) {
    return mycelius_recv(to, from, type, value);
}

} // namespace mycelius
