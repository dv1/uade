#!/bin/sh

oldpwd=$(pwd)

find . -mindepth 1 -type d |while read path ; do
    if test -x "$path/sparse.sh" ; then
	cd "$oldpwd/$path"
	echo "Testing $path"
	./sparse.sh
	cd "$oldpwd"
    fi
done
