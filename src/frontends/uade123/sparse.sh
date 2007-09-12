#!/bin/sh

for f in *.c ; do
    sparse -I../common -I../../include "$f"
done
