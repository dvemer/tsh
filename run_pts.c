#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <termios.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <time.h>

int main(int argc,char *argv[])
{
        int pts_file_fd;
	int input_file_fd;
	int tsh_pid;
        char c;
	char pts_path[64];
	struct timespec ts;

	if (setuid(0) == -1) {
		printf("failed to get root priv: %s\n", strerror(errno));
		exit(1);
	}

	tsh_pid = atoi(argv[1]);
	snprintf(pts_path, sizeof pts_path, "/proc/%i/fd/0", tsh_pid);
        pts_file_fd = open(pts_path, O_WRONLY);

        if (pts_file_fd == -1) {
                printf("failed to open: %s\n", strerror(errno));
		exit(1);
	}

	input_file_fd = open(argv[2], O_RDONLY);

	if (input_file_fd == -1) {
                printf("failed to open: %s\n", strerror(errno));
		exit(1);
	}

	ts.tv_sec = 0;
	ts.tv_nsec = 300000000;

	while (read(input_file_fd, &c, sizeof c) == sizeof c) {
	        printf("write %02X...", c);
		printf("result: %i\n", ioctl(pts_file_fd, 0x5412, &c));
		nanosleep(&ts, NULL);
	}

	printf("done!\n");

        close(pts_file_fd);
        close(input_file_fd);
        return 0;
}

