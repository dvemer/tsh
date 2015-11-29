#define FG 0x1
#define BG 0x2
#define DSWN 0x4 /* disown: don't send SIGHUP */

#define MAX_JOBS 100
#define MAX_ARGS 40
#define MAX_CMD_NAME 50
#define PATH_LEN 100

/* entry to execute */
struct task {
	char *name;
	char *args[MAX_ARGS];
	int *in_pipe;
	int *out_pipe;
	int pid;
	int idx;
	struct list_head next; /* for pipe convair */
};

void parse(char *s, struct list_head *tasks, int *pipes_num, int *bck);
