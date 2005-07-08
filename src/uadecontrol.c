
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

#include <uadecontrol.h>
#include <strlrep.h>
#include <unixatomic.h>

#define INPUT_BUF_SIZE (4096)

unsigned int uade_inputbytes = 0;
static char uade_inputbuffer[INPUT_BUF_SIZE];

int uade_input_fd = 0; /* stdin */
int uade_output_fd = 1; /* stdout */


static int uade_url_to_fd(const char *url, int flags, mode_t mode);
static int uade_valid_message(struct uade_control *uc);


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


int uade_receive_command(struct uade_control *uc, size_t maxbytes)
{
  size_t fullsize;

  assert(sizeof(*uc) == 8);

  if (uade_inputbytes < sizeof(*uc)) {
    if ((get_more(sizeof(*uc)) == 0))
      return 0;
  }
  uade_copy_from_inputbuffer(uc, sizeof(*uc));

  if (!uade_valid_message(uc))
    return 0;

  fullsize = uc->size + sizeof(*uc);
  if (fullsize > maxbytes) {
    fprintf(stderr, "too big a command: %u\n", fullsize);
    return 0;
  }
  if (uade_inputbytes < uc->size) {
    if ((get_more(uc->size) == 0))
      return 0;
  }
  uade_copy_from_inputbuffer(&uc->data, uc->size);
  return 1;
}


int uade_receive_string_command(char *s, enum uade_command com,
				size_t maxlen)
{
  const size_t COMLEN = 4096;
  uint8_t commandbuf[COMLEN];
  struct uade_control *uc = (struct uade_control *) commandbuf;

  if ((uade_receive_command(uc, COMLEN) == 0))
    return 0;
  if (uc->command != com)
    return -1;
  if (uc->size == 0)
    return -1;
  if (uc->size != (strlen(uc->data) + 1))
    return -1;
  strlcpy(s, uc->data, maxlen);
  return 1;
}


int uade_send_command(struct uade_control *uc)
{
  if (!uade_valid_message(uc))
    return 0;
  if (atomic_write(uade_output_fd, uc, sizeof(*uc) + uc->size) < 0)
    return 0;
  return 1;
}


int uade_send_string(enum uade_command com, const char *str)
{
  struct uade_control uc = {.command = com, .size = strlen(str) + 1};
  if ((sizeof(uc) + uc.size) > INPUT_BUF_SIZE)
    return 0;
  if (atomic_write(uade_output_fd, &uc, sizeof(uc)) < 0)
    return 0;
  if (atomic_write(uade_output_fd, str, uc.size) < 0)
    return 0;
  return 1;
}


void uade_set_input_source(const char *input_source)
{
  if ((uade_input_fd = uade_url_to_fd(input_source, O_RDONLY, 0)) < 0) {
    fprintf(stderr, "can not open input file %s: %s\n", input_source, strerror(errno));
    exit(-1);
  }
}


void uade_set_output_destination(const char *output_destination)
{
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


static int uade_valid_message(struct uade_control *uc)
{
  if (uc->command <= UADE_COMMAND_FIRST || uc->command >= UADE_COMMAND_LAST) {
    fprintf(stderr, "unknown command: %d\n", uc->command);
    return 0;
  }
  if ((sizeof(*uc) + uc->size) > INPUT_BUF_SIZE) {
    fprintf(stderr, "too long a message\n");
    return 0;
  }
  return 1;
}
