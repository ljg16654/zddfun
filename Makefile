.PHONY: target

CFLAGS:=-lgmp -Wall -std=gnu99 -O2 -ltcmalloc

target: nono

sud: sud.c cbt.c darray.c

tri: tri.c cbt.c darray.c

nono: nono.c cbt.c darray.c
