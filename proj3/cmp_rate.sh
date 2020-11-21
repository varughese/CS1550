#!/usr/bin/env zsh
for file in *.trace;
	for n in 8 16 32 64;
		for alg in 'aging';
			python vmsim.py -n $n -a ${alg} -r 256 ${file} > algo/${file}_${alg}_${n}.txt