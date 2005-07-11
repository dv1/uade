/* UADE
 *
 * Copyright 2005 Heikki Orsila <heikki.orsila@iki.fi>
 *
 * This source code module is dual licensed under GPL and Public Domain.
 * Hence you may use _this_ module (not another code module) however you
 * want in your projects.
 */

#ifndef _UADEIPC_H_
#define _UADEIPC_H_

#include <stdint.h>

#define UADE_MAX_MESSAGE_SIZE (4096)

enum uade_msgtype {
  UADE_MSG_FIRST = 0,
  UADE_COMMAND_CONFIG,
  UADE_COMMAND_SCORE,
  UADE_COMMAND_PLAYER,
  UADE_COMMAND_MODULE,
  UADE_COMMAND_READ,
  UADE_COMMAND_REBOOT,
  UADE_COMMAND_SET_SUBSONG,
  UADE_COMMAND_IGNORE_CHECK,
  UADE_COMMAND_SONG_END_NOT_POSSIBLE,
  UADE_COMMAND_SET_NTSC,
  UADE_COMMAND_FILTER_ON,
  UADE_COMMAND_CHANGE_SUBSONG,
  UADE_COMMAND_ACTIVATE_DEBUGGER,
  UADE_COMMAND_TOKEN,
  UADE_REPLY_MSG,
  UADE_REPLY_CANT_PLAY,
  UADE_REPLY_CAN_PLAY,
  UADE_REPLY_SONG_END,
  UADE_REPLY_CRASH,
  UADE_REPLY_SUBSONG_INFO,
  UADE_REPLY_PLAYERNAME,
  UADE_REPLY_MODULENAME,
  UADE_REPLY_FORMATNAME,
  UADE_REPLY_DATA,
  UADE_MSG_LAST
};

struct uade_msg {
  uint32_t msgtype;
  uint32_t size;
  uint8_t data[];
} __attribute__((packed));

#endif
