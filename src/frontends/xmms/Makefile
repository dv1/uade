CC = gcc
CFLAGS = -Wall -O2 -fpic -shared -g `xmms-config --cflags` -DVERSION=\"1.50\" -I../common -I../../include

CLIBS = `xmms-config --libs`
MODULES = plugin.o amifilemagic.o eagleplayer.o strlrep.o uadecontrol.o \
	uadeipc.o unixatomic.o

uade2.so:	$(MODULES)
	$(CC) $(CFLAGS) -o $@ $(MODULES) $(CLIBS)

amifilemagic.o:	../common/amifilemagic.c ../common/amifilemagic.h
	$(CC) $(CFLAGS) -c $<

eagleplayer.o:	../common/eagleplayer.c ../common/eagleplayer.h
	$(CC) $(CFLAGS) -c $<

plugin.o:	plugin.c ../common/eagleplayer.h ../common/uadecontrol.h ../../include/uadeipc.h ../../include/uadeconstants.h
	$(CC) $(CFLAGS) -c $<

strlrep.o:	../../strlrep.c ../../include/strlrep.h
	$(CC) $(CFLAGS) -c $<

uadecontrol.o:	../common/uadecontrol.c ../common/uadecontrol.h
	$(CC) $(CFLAGS) -c $<

uadeipc.o:	../../uadeipc.c ../../include/uadeipc.h ../../include/uademsg.h
	$(CC) $(CFLAGS) -c $<

unixatomic.o:	../../unixatomic.c ../../include/unixatomic.h
	$(CC) $(CFLAGS) -c $<
