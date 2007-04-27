#include <stdlib.h>
#include <stdio.h>

#include <ao/ao.h>

#include <uadeconstants.h>

#include "audio.h"
#include "uade123.h"


static ao_device *libao_device = NULL;


void audio_close(void)
{
  if (libao_device) {

    /* Work-around libao/alsa, sleep 10ms to drain audio stream. */
    if (uade_output_file_name[0] == 0)
      usleep(10000);

    ao_close(libao_device);
  }
}


/* buffer_time is given in milliseconds */

int audio_init(int frequency, int buffer_time)
{
  int driver;
  ao_sample_format format;

  if (uade_no_output)
    return 1;

  ao_initialize();

  format.bits = UADE_BYTES_PER_SAMPLE * 8;
  format.channels = UADE_CHANNELS;
  format.rate = frequency;
  format.byte_format = AO_FMT_NATIVE;

  if (uade_output_file_name[0]) {
    driver = ao_driver_id(uade_output_file_format[0] ? uade_output_file_format : "wav");
    if (driver < 0) {
      fprintf(stderr, "Invalid libao driver\n");
      return 0;
    }
    libao_device = ao_open_file(driver, uade_output_file_name, 1, &format, NULL);
  } else {
    driver = ao_default_driver_id();
    if (buffer_time > 0) {
      char val[32];
      snprintf(val, sizeof val, "%d", buffer_time);
      libao_device = ao_open_live(driver, &format, & (ao_option) {.key = "buffer_time", .value = val});
    } else {
      libao_device = ao_open_live(driver, &format, NULL);
    }
  }
  if (libao_device == NULL) {
    fprintf(stderr, "Error opening device: errno %d\n", errno);
    return 0;
  }
  return 1;
}


int audio_play(unsigned char *samples, int bytes)
{
  if (uade_no_output)
    return bytes;
  /* ao_play returns 0 on failure */
  return ao_play(libao_device, (char *) samples, bytes);
}
