#!/usr/bin/env zsh
for file in *.trace;
	for r in 5 10 20 40 50 64 100 256 300 400 512
		python vmsim.py -n 16 -a aging -r $r ${file} > rate/${file}_${n}_${r}.txt