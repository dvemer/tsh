CFLAGS = -Wall -g3 -DTERMNC 
tsh:
	gcc term.c acompl.c parse.c list.c sh.c -I. -o sh $(CFLAGS)
