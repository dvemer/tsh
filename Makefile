tsh:
	cc term.c acompl.c parse.c list.c sh.c -I. -o sh -Wall -g3 -DTERMNC
