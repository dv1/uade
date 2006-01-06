/* UADE
 *
 * Copyright 2005 Heikki Orsila <heikki.orsila@iki.fi>
 *
 * This source code module is dual licensed under GPL and Public Domain.
 * Hence you may use _this_ module (not another code module) in any way you
 * want in your projects.
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/poll.h>
#include <errno.h>
#include <assert.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <netinet/in.h>

#include <uadeipc.h>
#include <strlrep.h>
#include <unixatomic.h>


struct uade_ipc {
  unsigned int inputbytes;
  char inputbuffer[UADE_MAX_MESSAGE_SIZE];
  int input_fd; /* stdin */
  int output_fd; /* stdout */
  enum uade_control_state state;
};


static pid_t server_pid;
static struct uade_ipc ipcc[2];


static int url_to_fd(const char *url, int flags, mode_t mode);
static int valid_message(struct uade_msg *uc);


static struct uade_ipc *get_peer(void)
{
  return &ipcc[(getpid() == server_pid) ? 0 : 1];
}


void uade_check_fix_string(struct uade_msg *um, size_t maxlen)
{
  uint8_t *s = (uint8_t *) um->data;
  size_t safelen;
  if (um->size == 0) {
    s[0] = 0;
    fprintf(stderr, "zero string detected\n");
  }
  safelen = 0;
  while (s[safelen] != 0 && safelen < maxlen)
    safelen++;
  if (safelen == maxlen) {
    safelen--;
    fprintf(stderr, "too long a string\n");
    s[safelen] = 0;
  }
  if (um->size != (safelen + 1)) {
    fprintf(stderr, "string size does not match\n");
    um->size = safelen + 1;
    s[safelen] = 0;
  }
}


static ssize_t get_more(size_t bytes, struct uade_ipc *ipc)
{
  if (ipc->inputbytes < bytes) {
    ssize_t s = atomic_read(ipc->input_fd, &ipc->inputbuffer[ipc->inputbytes], bytes - ipc->inputbytes);
    if (s <= 0)
      return -1;
    ipc->inputbytes += s;
  }
  return 0;
}


static void copy_from_inputbuffer(void *dst, int bytes, struct uade_ipc *ipc)
{
  if (ipc->inputbytes < bytes) {
    fprintf(stderr, "not enough bytes in input buffer\n");
    exit(-1);
  }
  memcpy(dst, ipc->inputbuffer, bytes);
  memmove(ipc->inputbuffer, &ipc->inputbuffer[bytes], ipc->inputbytes - bytes);
  ipc->inputbytes -= bytes;
}


int uade_receive_message(struct uade_msg *um, size_t maxbytes)
{
  size_t fullsize;
  struct uade_ipc *ipc = get_peer();

  if (ipc->state == UADE_INITIAL_STATE) {
    ipc->state = UADE_R_STATE;
  } else if (ipc->state == UADE_S_STATE) {
    fprintf(stderr, "protocol error: receiving in S state is forbidden\n");
    return -1;
  }

  if (ipc->inputbytes < sizeof(*um)) {
    if (get_more(sizeof(*um), ipc))
      return 0;
  }

  copy_from_inputbuffer(um, sizeof(*um), ipc);

  um->msgtype = ntohl(um->msgtype);
  um->size = ntohl(um->size);

  if (!valid_message(um))
    return -1;

  fullsize = um->size + sizeof(*um);
  if (fullsize > maxbytes) {
    fprintf(stderr, "too big a command: %zu\n", fullsize);
    return -1;
  }
  if (ipc->inputbytes < um->size) {
    if (get_more(um->size, ipc))
      return -1;
  }
  copy_from_inputbuffer(&um->data, um->size, ipc);

  if (um->msgtype == UADE_COMMAND_TOKEN)
    ipc->state = UADE_S_STATE;

  return 1;
}


int uade_receive_short_message(enum uade_msgtype msgtype)
{
  struct uade_msg um;
  struct uade_ipc *ipc = get_peer();

  if (ipc->state == UADE_INITIAL_STATE) {
    ipc->state = UADE_R_STATE;
  } else if (ipc->state == UADE_S_STATE) {
    fprintf(stderr, "protocol error: receiving (%d) in S state is forbidden\n", msgtype);
    return -1;
  }

  if (uade_receive_message(&um, sizeof(um)) <= 0) {
    fprintf(stderr, "can not receive short message: %d\n", msgtype);
    return -1;
  }
  return (um.msgtype == msgtype) ? 0 : -1;
}


int uade_receive_string(char *s, enum uade_msgtype com,
				size_t maxlen)
{
  const size_t COMLEN = 4096;
  uint8_t commandbuf[COMLEN];
  struct uade_msg *um = (struct uade_msg *) commandbuf;
  int ret;
  struct uade_ipc *ipc = get_peer();

  if (ipc->state == UADE_INITIAL_STATE) {
    ipc->state = UADE_R_STATE;
  } else if (ipc->state == UADE_S_STATE) {
    fprintf(stderr, "protocol error: receiving in S state is forbidden\n");
    return -1;
  }

  ret = uade_receive_message(um, COMLEN);
  if (ret <= 0)
    return ret;
  if (um->msgtype != com)
    return -1;
  if (um->size == 0)
    return -1;
  if (um->size != (strlen((char *) um->data) + 1))
    return -1;
  strlcpy(s, (char *) um->data, maxlen);
  return 1;
}


int uade_send_message(struct uade_msg *um)
{
  uint32_t size = um->size;
  struct uade_ipc *ipc = get_peer();

  if (ipc->state == UADE_INITIAL_STATE) {
    ipc->state = UADE_S_STATE;
  } else if (ipc->state == UADE_R_STATE) {
    fprintf(stderr, "protocol error: sending in R state is forbidden\n");
    return -1;
  }

  if (!valid_message(um))
    return -1;
  if (um->msgtype == UADE_COMMAND_TOKEN)
    ipc->state = UADE_R_STATE;
  um->msgtype = htonl(um->msgtype);
  um->size = htonl(um->size);
  if (atomic_write(ipc->output_fd, um, sizeof(*um) + size) < 0)
    return -1;

  return 0;
}


int uade_send_short_message(enum uade_msgtype msgtype)
{
  if (uade_send_message(& (struct uade_msg) {.msgtype = msgtype})) {
    fprintf(stderr, "can not send short message: %d\n", msgtype);
    return -1;
  }
  return 0;
}


int uade_send_string(enum uade_msgtype com, const char *str)
{
  uint32_t size = strlen(str) + 1;
  struct uade_msg um = {.msgtype = ntohl(com), .size = ntohl(size)};
  struct uade_ipc *ipc = get_peer();

  if (ipc->state == UADE_INITIAL_STATE) {
    ipc->state = UADE_S_STATE;
  } else if (ipc->state == UADE_R_STATE) {
    fprintf(stderr, "protocol error: sending in R state is forbidden\n");
    return -1;
  }

  if ((sizeof(um) + size) > UADE_MAX_MESSAGE_SIZE)
    return -1;
  if (atomic_write(ipc->output_fd, &um, sizeof(um)) < 0)
    return -1;
  if (atomic_write(ipc->output_fd, str, size) < 0)
    return -1;

  return 0;
}


static void set_input_source(const char *input_source, struct uade_ipc *ipc)
{
  if ((ipc->input_fd = url_to_fd(input_source, O_RDONLY, 0)) < 0) {
    fprintf(stderr, "can not open input file %s: %s\n", input_source, strerror(errno));
    exit(-1);
  }
}


static void set_output_destination(const char *output_destination, struct uade_ipc *ipc)
{
  if ((ipc->output_fd = url_to_fd(output_destination, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) < 0) {
    fprintf(stderr, "can not open output file %s: %s\n", output_destination, strerror(errno));
    exit(-1);
  }
}


void uade_set_peer(int peer_is_client, const char *input, const char *output)
{
  struct uade_ipc *ipc;
  assert(peer_is_client == 0 || peer_is_client == 1);
  assert(input != NULL);
  assert(output != NULL);

  if (peer_is_client == 0)
    server_pid = getpid();

  ipc = get_peer();
  *ipc = (struct uade_ipc) {.state = UADE_INITIAL_STATE};

  set_input_source(input, ipc);
  set_output_destination(output, ipc);
}


static int url_to_fd(const char *url, int flags, mode_t mode)
{
  int fd;
  if (strncmp(url, "fd://", 5) == 0) {
    char *endptr;
    if (url[5] == 0)
      return -1;
    fd = strtol(&url[5], &endptr, 10);
    if (*endptr != 0)
      return -1;
  } else {
    if (flags & O_WRONLY) {
      fd = open(url, flags, mode);
    } else {
      fd = open(url, flags);
    }
  }
  if (fd < 0)
    fd = -1;
  return fd;
}


static int valid_message(struct uade_msg *um)
{
  if (um->msgtype <= UADE_MSG_FIRST || um->msgtype >= UADE_MSG_LAST) {
    fprintf(stderr, "unknown command: %d\n", um->msgtype);
    return 0;
  }
  if ((sizeof(*um) + um->size) > UADE_MAX_MESSAGE_SIZE) {
    fprintf(stderr, "too long a message\n");
    return 0;
  }
  return 1;
}
