#!/bin/sh
#
# This script looks to see if a directory contains .h files
#
for dir in $@; do
	for hfile in $dir/*.h; do
		if [ -f $hfile ]; then echo $dir; fi
		break
	done
done
exit 0
