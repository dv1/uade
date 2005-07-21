#include <stdlib.h>
#include <stdio.h>

#include <ao/ao.h>

#include "audio.h"
#include "uade123.h"


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

  ao_initialize();

  format.bits = 16;
  format.channels = 2;
  format.rate = 44100;
  format.byte_format = AO_FMT_NATIVE;

  bytes_per_sample = format.bits / 8;
  sample_bytes_per_second = bytes_per_sample * format.channels * format.rate;

  if (output_file_name[0]) {
    driver = ao_driver_id(output_file_format[0] ? output_file_format : "wav");
    if (driver < 0) {
      fprintf(stderr, "illegal libao driver\n");
      return 0;
    }
    libao_device = ao_open_file(driver, output_file_name, 1, &format, NULL);
  } else {
    driver = ao_default_driver_id();
    libao_device = ao_open_live(driver, &format, NULL);
  }
  if (libao_device == NULL) {
    fprintf(stderr, "error opening device: errno %d\n", errno);
    return 0;
  }
  return 1;
}


int audio_play(char *samples, int bytes)
{
  /* ao_play returns 0 on failure */
  return ao_play(libao_device, samples, bytes);
}
