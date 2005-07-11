#include <alsa/asoundlib.h>
int main(void) { return (!(SND_LIB_MAJOR==1 && SND_LIB_MINOR==0)); }
