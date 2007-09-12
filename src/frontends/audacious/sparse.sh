#!/bin/sh

INCLUDES="-I../common -I../../include $(pkg-config gtk+ --cflags)"

for f in *.c ; do
    sparse $INCLUDES "$f"
done
