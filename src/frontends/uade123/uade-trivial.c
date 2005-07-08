#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

#include <uadeipc.h>
#include <strlrep.h>

static char configname[PATH_MAX];
static char modulename[PATH_MAX];
static char playername[PATH_MAX];
static char scorename[PATH_MAX];
static char uadename[PATH_MAX];


static int get_string_arg(char *dst, size_t maxlen, const char *arg, int *i,
			  char *argv[], int *argc)
{
  if (strcmp(argv[*i], arg) == 0) {
    if ((*i + 1) >= *argc) {
      fprintf(stderr, "missing parameter for %s\n", argv[*i]);
      exit(-1);
    }
    strlcpy(scorename, argv[*i + 1], sizeof(scorename));
    *i += 2;
    return 1;
  }
  return 0;
}


int main(int argc, char *argv[])
{
  int i;
  for (i = 1; i < argc; i++) {
    if (get_string_arg(configname, sizeof(configname), "-c", &i, argv, &argc))
      continue;
    if (get_string_arg(modulename, sizeof(modulename), "-m", &i, argv, &argc))
      continue;
    if (get_string_arg(playername, sizeof(playername), "-p", &i, argv, &argc))
      continue;
    if (get_string_arg(scorename, sizeof(scorename), "-s", &i, argv, &argc))
      continue;
    if (get_string_arg(uadename, sizeof(uadename), "-u", &i, argv, &argc))
      continue;
    fprintf(stderr, "unknown arg: %s\n", argv[i]);
    exit(-1);
  }

#define CHECK_EXISTENCE(x) do { if ((x)[0] == 0) { fprintf(stderr, "must have %s\n", (x)); exit(-1); } } while (0)

  CHECK_EXISTENCE(configname);
  CHECK_EXISTENCE(modulename);
  CHECK_EXISTENCE(playername);
  CHECK_EXISTENCE(scorename);
  CHECK_EXISTENCE(uadename);

  return 0;
}
