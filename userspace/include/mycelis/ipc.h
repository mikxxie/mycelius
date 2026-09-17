#pragma once
#include <stdint.h>
enum mycelius_message_type { MYCELIS_EVENT_RX = 1, MYCELIS_BLOCK_REQUEST = 2 };
struct mycelius_message { uint32_t type; uint32_t reserved; uint64_t value; };
int mycelius_send(uint32_t destination, const struct mycelius_message *message);
int mycelius_receive(struct mycelius_message *message);
