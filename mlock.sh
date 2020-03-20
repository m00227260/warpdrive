#!/bin/bash

size=81920000
block=8192

for i in {1..10}
do
	let "size*=$i"
	echo $i $size
	sudo ./test/test_bind_api -b $block -s $size -o perf -c 50 -v
done
