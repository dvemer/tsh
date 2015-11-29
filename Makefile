tsh:
	cc parse.c ../list.c sh.c -I. -I../ -o sh -DNODEBUG -Wall
