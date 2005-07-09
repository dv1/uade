
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

#include <uadecontrol.h>
#include <strlrep.h>
#include <unixatomic.h>

unsigned int uade_inputbytes = 0;
static char uade_inputbuffer[UADE_MAX_MESSAGE_SIZE];

int uade_input_fd = 0; /* stdin */
int uade_output_fd = 1; /* stdout */


static int uade_url_to_fd(const char *url, int flags, mode_t mode);
static int uade_valid_message(struct uade_msg *uc);


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


static int get_more(unsigned int bytes)
{
  if (uade_inputbytes < bytes) {
    ssize_t s;
    if ((s = atomic_read(uade_input_fd, &uade_inputbuffer[uade_inputbytes], bytes - uade_inputbytes)) < 0) {
      fprintf(stderr, "no more input\n");
      return 0;
    }
    uade_inputbytes += s;
  }
  return 1;
}


static void uade_copy_from_inputbuffer(void *dst, int bytes)
{
  if (uade_inputbytes < bytes) {
    fprintf(stderr, "not enough bytes in input buffer\n");
    exit(-1);
  }
  memcpy(dst, uade_inputbuffer, bytes);
  memmove(uade_inputbuffer, &uade_inputbuffer[bytes], uade_inputbytes - bytes);
  uade_inputbytes -= bytes;
}


int uade_receive_message(struct uade_msg *uc, size_t maxbytes)
{
  size_t fullsize;

  assert(sizeof(*uc) == 8);

  if (uade_inputbytes < sizeof(*uc)) {
    if ((get_more(sizeof(*uc)) == 0))
      return 0;
  }
  uade_copy_from_inputbuffer(uc, sizeof(*uc));

  uc->msgtype = ntohl(uc->msgtype);
  uc->size = ntohl(uc->size);

  if (!uade_valid_message(uc))
    return -1;

  fullsize = uc->size + sizeof(*uc);
  if (fullsize > maxbytes) {
    fprintf(stderr, "too big a command: %u\n", fullsize);
    return -1;
  }
  if (uade_inputbytes < uc->size) {
    if ((get_more(uc->size) == 0))
      return 0;
  }
  uade_copy_from_inputbuffer(&uc->data, uc->size);
  return 1;
}


int uade_receive_string(char *s, enum uade_msgtype com,
				size_t maxlen)
{
  const size_t COMLEN = 4096;
  uint8_t commandbuf[COMLEN];
  struct uade_msg *uc = (struct uade_msg *) commandbuf;
  int ret;
  ret = uade_receive_message(uc, COMLEN);
  if (ret <= 0)
    return ret;
  if (uc->msgtype != com)
    return -1;
  if (uc->size == 0)
    return -1;
  if (uc->size != (strlen(uc->data) + 1))
    return -1;
  strlcpy(s, uc->data, maxlen);
  return 1;
}


int uade_send_message(struct uade_msg *uc)
{
  uint32_t size = uc->size;
  if (!uade_valid_message(uc))
    return -1;
  uc->msgtype = htonl(uc->msgtype);
  uc->size = htonl(uc->size);
  if (atomic_write(uade_output_fd, uc, sizeof(*uc) + size) < 0)
    return -1;
  return 1;
}


int uade_send_string(enum uade_msgtype com, const char *str)
{
  uint32_t size = strlen(str) + 1;
  struct uade_msg uc = {.msgtype = ntohl(com), .size = ntohl(size)};
  if ((sizeof(uc) + size) > UADE_MAX_MESSAGE_SIZE)
    return -1;
  if (atomic_write(uade_output_fd, &uc, sizeof(uc)) < 0)
    return -1;
  if (atomic_write(uade_output_fd, str, size) < 0)
    return -1;
  return 1;
}


void uade_set_input_source(const char *input_source)
{
  fprintf(stderr, "using source: %s\n", input_source);
  if ((uade_input_fd = uade_url_to_fd(input_source, O_RDONLY, 0)) < 0) {
    fprintf(stderr, "can not open input file %s: %s\n", input_source, strerror(errno));
    exit(-1);
  }
}


void uade_set_output_destination(const char *output_destination)
{
  fprintf(stderr, "using destination: %s\n", output_destination);
  if ((uade_output_fd = uade_url_to_fd(output_destination, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) < 0) {
    fprintf(stderr, "can not open output file %s: %s\n", output_destination, strerror(errno));
    exit(-1);
  }
}


static int uade_url_to_fd(const char *url, int flags, mode_t mode)
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


static int uade_valid_message(struct uade_msg *uc)
{
  if (uc->msgtype <= UADE_MSG_FIRST || uc->msgtype >= UADE_MSG_LAST) {
    fprintf(stderr, "unknown command: %d\n", uc->msgtype);
    return 0;
  }
  if ((sizeof(*uc) + uc->size) > UADE_MAX_MESSAGE_SIZE) {
    fprintf(stderr, "too long a message\n");
    return 0;
  }
  return 1;
}
