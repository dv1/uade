CC = gcc
CFLAGS = -Wall -O2 -fpic -shared -g `xmms-config --cflags` -DVERSION=\"1.50\"
CLIBS = `xmms-config --libs`
MODULES = plugin.o

uade2.so:	$(MODULES)
	$(CC) $(CFLAGS) -o $@ $(MODULES) $(CLIBS)

plugin.o:	plugin.c
	$(CC) $(CFLAGS) -c $<

install:	uade2.so
	mkdir -p ~/.xmms/Plugins/Input
	install uade2.so ~/.xmms/Plugins/Input/
