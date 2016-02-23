#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "term.h"

static void vt100_erase_line(void)
{
	char erase_line_ctrl[] = {0x1b, '[', '2', 'K'};

	write(1, erase_line_ctrl, sizeof erase_line_ctrl);
}

static void vt100_move_cur_back(int steps)
{
	char cur_back_ctrl[13] = {0};
	size_t len;

	cur_back_ctrl[0] = 0x1b;
	cur_back_ctrl[1] = '[';
	sprintf(&cur_back_ctrl[2], "%i", steps);
	len = strlen(cur_back_ctrl);
	cur_back_ctrl[len] = 'D';
	write(1, cur_back_ctrl, strlen(cur_back_ctrl));
}

static void vt100_erase_end(void)
{
	char erase_end_ctrl[] = {0x1b, '[', 'K'};

	write(1, erase_end_ctrl, sizeof erase_end_ctrl);
}

void t_erase_line(void)
{
	vt100_erase_line();
}

void t_move_cur_back(int steps)
{
	vt100_move_cur_back(steps);
}

void t_erase_end(void)
{
	vt100_erase_end();
}
