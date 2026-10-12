#!/bin/bash
filename=$1

grep "Flushes.*bits-per-key" ${filename} | awk 'BEGIN{x=0;i=0}{x+=$(NF-12);i++}END{print "level 0:\t"x/i}'
grep "level 3.*bits-per-key" ${filename} | awk 'BEGIN{x=0;i=0}{x+=$(NF-6);i++}END{print "level 3:\t"x/i}'
grep "level 4.*bits-per-key" ${filename} | awk 'BEGIN{x=0;i=0}{x+=$(NF-6);i++}END{print "level 4:\t"x/i}'
grep "level 5.*bits-per-key" ${filename} | awk 'BEGIN{x=0;i=0}{x+=$(NF-6);i++}END{print "level 5:\t"x/i}'
grep "level 6.*bits-per-key" ${filename} | awk 'BEGIN{x=0;i=0}{x+=$(NF-6);i++}END{print "level 6:\t"x/i}'
