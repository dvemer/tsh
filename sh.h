#ifndef	SH_H
#define	SH_H

#include "list.h"
#include <time.h>
#define FG 0x1
#define BG 0x2
#define DSWN 0x4 /* disown: don't send SIGHUP */

#define MAX_JOBS 100
#define MAX_ARGS 40
#define MAX_CMD_NAME 50
#define PATH_LEN 100

#define	CMD_UP 1
#define	CMD_DOWN 2

struct builtin_ent {
	const char *name;
	void (*handler)(char *cmd);
};

struct history_ent {
	int idx;/* for simple checking for first/last entry */
	struct list_head next;
	char cmd_string[];
};

/* entry to execute */
struct task {
	char *name;
	char *args[MAX_ARGS];
	int argc;
	int *in_pipe;
	int *out_pipe;
	int pid;
	int idx;
	int flag;
	int is_last;
	struct list_head next; /* for pipe convair */
};

struct job {
	char *name;
	int bckg;
	int idx;
	int tasks_num;
	struct list_head tasks;
	struct list_head next;
};

struct job *parse(char *s, int *bck);
#endif
