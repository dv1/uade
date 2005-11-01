#include <stdlib.h>
#include <stdio.h>

#include <ao/ao.h>

#include "audio.h"
#include "uade123.h"

int uade_bytes_per_sample;
int uade_sample_bytes_per_second;


static ao_device *libao_device = NULL;


void audio_close(void)
{
  if (libao_device)
    ao_close(libao_device);
}


int audio_init(void)
{
  int driver;
  ao_sample_format format;

  if (uade_no_output) {
    uade_bytes_per_sample = 2;
    uade_sample_bytes_per_second = uade_bytes_per_sample * 2 * 44100;
    return 1;
  }

  ao_initialize();

  format.bits = 16;
  format.channels = 2;
  format.rate = 44100;
  format.byte_format = AO_FMT_NATIVE;

  uade_bytes_per_sample = format.bits / 8;
  uade_sample_bytes_per_second = uade_bytes_per_sample * format.channels * format.rate;

  if (uade_output_file_name[0]) {
    driver = ao_driver_id(uade_output_file_format[0] ? uade_output_file_format : "wav");
    if (driver < 0) {
      fprintf(stderr, "Invalid libao driver\n");
      return 0;
    }
    libao_device = ao_open_file(driver, uade_output_file_name, 1, &format, NULL);
  } else {
    driver = ao_default_driver_id();
    libao_device = ao_open_live(driver, &format, NULL);
  }
  if (libao_device == NULL) {
    fprintf(stderr, "Error opening device: errno %d\n", errno);
    return 0;
  }
  return 1;
}


int audio_play(char *samples, int bytes)
{
  if (uade_no_output)
    return bytes;
  /* ao_play returns 0 on failure */
  return ao_play(libao_device, samples, bytes);
}
