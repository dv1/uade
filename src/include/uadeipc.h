#ifndef _UADEIPC_H_
#define _UADEIPC_H_

#include <stdint.h>

#define UADE_COMMAND_SCORE (1)
#define UADE_COMMAND_PLAYER (2)
#define UADE_COMMAND_MODULE (3)
#define UADE_COMMAND_PLAY (4)

struct uade_control {
  uint32_t command;
  uint32_t size;
  uint8_t data;
} __attribute__((packed));


#endif
