#!/bin/bash

docker run -i --rm fizruk/stella compile < example.stella > example.c &&
# bin/stellac compile < example.stella > example.c &&
gcc -std=c11 -g -DSTELLA_DEBUG -DSTELLA_GC_STATS -DSTELLA_RUNTIME_STATS example.c stella/runtime.c stella/gc.c -o example