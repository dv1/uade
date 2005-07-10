
#include <uadeformats.h>

void *uade_read_uadeformats(char *filename)
{
  FILE *f = fopen(filename, "r");
  if (f == NULL)
    return NULL;

}


int uade_get_playername(const char *pre, void *formats)
{
  assert(0);
}
