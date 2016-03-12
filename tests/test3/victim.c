#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

int main(void)
{
	fprintf(stderr, "%i %i\n", getpid(), getpgrp());
	return 0;
}
