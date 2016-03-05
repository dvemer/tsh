CFLAGS = -Wall -g3 -DTERMNC -DDEBUG_OUTPUT
tsh:
	gcc term.c acompl.c parse.c list.c sh.c -I. -o tsh $(CFLAGS)
