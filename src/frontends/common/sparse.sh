#!/bin/sh

for f in *.c ; do
    sparse -I. -I../../include "$f"
done
