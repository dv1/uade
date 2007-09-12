#!/bin/sh

INCLUDES="-I. -Iinclude"
CFLAGS="-DREGPARAM= "

for f in *.c ; do
    sparse $INCLUDES $CFLAGS "$f"
done
