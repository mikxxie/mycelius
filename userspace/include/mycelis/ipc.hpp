#pragma once
extern "C" {
#include "ipc.h"
}
namespace mycelius { using Message = mycelius_message; enum class MessageType : unsigned { Rx = MYCELIS_EVENT_RX, BlockRequest = MYCELIS_BLOCK_REQUEST }; }
