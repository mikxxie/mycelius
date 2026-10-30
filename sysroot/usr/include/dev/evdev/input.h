#ifndef _DEV_EVDEV_INPUT_H_
#define _DEV_EVDEV_INPUT_H_

#include <sys/time.h>
#include <sys/types.h>

#define EV_KEY      0x01
#define EV_ABS      0x03

#define ABS_X       0x00
#define ABS_Y       0x01

struct input_event {
    struct timeval time;
    uint16_t type;
    uint16_t code;
    int32_t value;
};

#endif
