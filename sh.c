#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <signal.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>
#include "list.h"
#include "sh.h"
#include "common.h"
#include "acompl.h"

#define CMD_LEN 4096

extern char **environ;
/* prompt string */
static char prompt[50];
/* jobs list */
struct list_head tasks = {&tasks, &tasks};
static int pipes_num;
static int tasks_num;
/* input command */
static char *cmd = NULL;
static int cursor_pos;
/* bg/fg job flag */
static int bck;
static int will_wait;
/* pipes array */
static int *pipes;
/* shell history variables */
static struct list_head history = {&history, &history};
static struct history_ent *curr_cmd;
static int history_sz = 0;
int builtins_num;

static void try_chdir(void);
static void dump_history(void);
static void do_exit(void);
static void list_jobs(void);

/* terminal parameters */
struct termios tattr;

struct builtin_ent builtins[] = {{"cd", try_chdir}, {"help", NULL},
				 {"history", dump_history}, {"jobs", list_jobs},
				 {"exit", do_exit},
				 };

static struct task *get_task(int pid)
{
	struct list_head *pos;

	list_for_each(pos, &tasks) {
		struct task *task;

		task = get_elem(pos, struct task, next);

		if (task->pid == pid)
			return task;
	}

	return NULL;
}

static void hndl_chld1(int code, siginfo_t *si, void *arg)
{
	struct task *task;
	int status;
	int pid;

	pid = wait(&status);
	task = get_task(pid);

	if (task->flag & BG)
		printf("[%i] %s ready\n", pid, task->name);

	if (task->in_pipe) {
#ifdef DEBUG
		fprintf(stderr, "%s closing in %i\n", task->name, *task->in_pipe);
#endif
		if (close(*task->in_pipe))
			fprintf(stderr, "error %s\n", strerror(errno));
	}

	if (task->out_pipe) {
#ifdef DEBUG
		fprintf(stderr, "%s closing out %i\n", task->name, *task->out_pipe);
#endif
		if (close(*task->out_pipe))
			fprintf(stderr, "error %s\n", strerror(errno));
	}

	delete_item(&task->next);
	free(task);
	tasks_num--;

	if (tasks_num == 0)
		free(pipes);
}

static void sighup_jobs(void)
{
	struct list_head *pos;

	list_for_each(pos, &tasks) {
		struct task *task;

		task = get_elem(pos, struct task, next);

		if (!(task->flag & DSWN))
			kill(task->pid, SIGHUP);
	}
}

/* SIGHUP handler */
static void hndl_sighup(int code)
{
	printf("sighup caught!\n");
	sighup_jobs();
	getchar();
	exit(1);
}

#ifdef	DEBUG
/* only for development/debug purposes */
static void hndl_ssegv(int code)
{
	printf("%s\n", __func__);
	getchar();
}
#endif

static void list_jobs(void)
{
	struct list_head *pos;
	int i;

	i = 0;
	printf("jobs:\n");

	list_for_each(pos, &tasks) {
		struct task *task;

		task = get_elem(pos, struct task, next);
		printf("[%i] %s\n", ++i, task->name);
	}
}

/*
 * Scans 'path' directory for binary 'name'.
 */
static char *look_for_binary(char *path, char *name)
{
	size_t path_len;
	size_t name_len;
	char *full_path;
	int need_slash;

	path_len = strlen(path);
	name_len = strlen(name);
	need_slash = (path[path_len - 1] != '/');
	/* first 1 is '/' at the end of path, second 1 is '\0' */
	full_path = malloc(path_len + name_len + 1 + need_slash);
	ASSERT_ERR("malloc failed\n", (full_path == NULL));

	strcpy(full_path, path);

	if (need_slash)
		strcat(full_path, "/");

	strcat(full_path, name);

	if (access(full_path, X_OK)) {
		free(full_path);
		full_path = NULL;
	}

	return full_path;
}

/*
 * Looking for binary 'name' in each 'PATH' places.
 */
static char *get_full_path(char *name)
{
	char *path_var;
	char *path;
	char *full_path;
	char *path_var_copy;

	path_var = getenv("PATH");

	if (path_var == NULL)
		return NULL;

	path_var_copy = strdup(path_var);
	path = strtok(path_var_copy, ":");

	while (path != NULL) {
		full_path = look_for_binary(path, name);

		if (full_path != NULL) {
			free(path_var_copy);
			return full_path;
		}

		path = strtok(NULL, ":");
	};

	free(path_var_copy);
	return NULL;
}

static void init_prompt(void)
{
	char *usr;

	usr = getenv("USER");

	if (usr == NULL)
		usr = "vivek";
	else {
		/* enough space for 'usr>' */
		if (strlen(usr) > (sizeof prompt) - 2)
			usr = "vivek";
	}

	strcpy(prompt, usr);
	strcat(prompt, ">");
}


#ifdef DEBUG
static void dump_tasks(void)
{
	struct list_head *pos;

	printf("tasks:\n");
	list_for_each(pos, &tasks) {
		struct task *task;

		task = get_elem(pos, struct task, next);
		printf("%s\n", task->name);
	}
}
#endif

/*
 * Parses command line for arguments and pipes.
 */
static void parse_cmd(void)
{
	pipes_num = 0;
	tasks.prev = &tasks;
	tasks.next = &tasks;
	parse(cmd, &tasks, &pipes_num, &bck);
#ifdef DEBUG
	printf("back:%i\n", bck);
	printf("pipes:%i\n", pipes_num);
#endif
	tasks_num = pipes_num + 1;
#ifdef DEBUG
	dump_tasks();
#endif
}

static void create_pipes(void)
{
	int i;
	int *tmp_pipes;

	pipes = malloc(pipes_num * (sizeof (int) * 2));
	ASSERT_ERR("malloc failed\n", (pipes == NULL));

	tmp_pipes = pipes;

	for(i = 0;i < pipes_num;i++, tmp_pipes += 2) {
		pipe(tmp_pipes);
#ifdef DEBUG
		fprintf(stderr, "pipe: in %i out %i\n",
			tmp_pipes[0], tmp_pipes[1]);
#endif
	}
}

static void assign_pipes(struct task *task)
{
	int idx;

	task->in_pipe = NULL;
	task->out_pipe = NULL;

	if (pipes_num == 0)
		return;

	idx = task->idx;

	if (idx == 0)
		task->out_pipe = &pipes[1];
	else {
		if (idx == pipes_num)
			task->in_pipe = &pipes[pipes_num * 2 - 2];
		else {
			task->out_pipe = &pipes[1 + (idx * 2)];
			task->in_pipe = &pipes[idx * 2 - 2];
		}
	}
}

static void close_pipes(int fd1, int fd2)
{
	int i;
	int *p;

	p = pipes;

	for(i = 0;i < (pipes_num * 2);i++) {
		if ((p[i] == fd1) || (p[i] == fd2))
			continue;
		close(p[i]);
	}
}

static void run_task(struct task *task, int *lead_pid, int idx)
{
	pid_t pid;
	char *full_path;

	task->idx = idx;
	assign_pipes(task);

	full_path = get_full_path(task->name);

	if (full_path == NULL) {
		fprintf(stderr, "Can't find %s\n", task->name);
		will_wait = 0;
		return;
	}

	if (task->flag & FG)
		will_wait = 1;
	else
		will_wait = 0;

	if ((pid = fork())) {
		if (*lead_pid == -1)
			*lead_pid = pid;

		task->pid = pid;
		free(full_path);
	} else {
		setpgid(0, 0);
		signal(SIGINT, SIG_DFL);
		signal(SIGCHLD, SIG_DFL);
		signal(SIGQUIT, SIG_DFL);
		signal(SIGTSTP, SIG_DFL);
		signal(SIGSEGV, SIG_DFL);

		if ((idx == 0) && (bck == 0)) {
#ifdef DEBUG
			fprintf(stderr, "set child to fg!\n");
#endif
			tcsetpgrp(0, getpid());
		}

		if (pipes_num) {
			if (idx == 0) {
#ifdef DEBUG
				fprintf(stderr, "first %s idx %i; write to %i, closing %i\n",
					task->name, idx, pipes[1], pipes[0]);
#endif
				dup2(pipes[1], 1);
				close_pipes(pipes[1], -1);
			} else {
				if (idx == pipes_num) {
					int last_in;

					last_in = pipes[pipes_num * 2 - 2];
#ifdef DEBUG
					fprintf(stderr, "last %s idx %i; read from %i, pipes: %i\n",
						task->name, idx, last_in, pipes_num);
#endif
					dup2(last_in, 0);
					close_pipes(last_in, -1);
				} else {
					int dup_in;
					int dup_out;

					dup_in = pipes[idx * 2 - 2];
					dup_out = pipes[1 + (idx * 2)];
#ifdef DEBUG
					fprintf(stderr, "middle %s idx %i; out to %i, in to %i\n",
						task->name, idx, dup_out, dup_in);
#endif
					dup2(dup_out, 1);
					dup2(dup_in, 0);
					close_pipes(dup_out, dup_in);
				}
			}
		}
#ifdef DEBUG
		fprintf(stderr, "execing %s %s\n", __func__, full_path);
		{
			int i;

			printf("arguments:\n");
			i = 0;

			while (task->args[i]) {
				printf("[%i] %s\n", i, task->args[i]);
				i++;
			};
		}
#endif
		execve(full_path, task->args, environ);
		exit(1);
	}
}

/* Processes global list of commands. */
static void exec_cmd(void)
{
	struct list_head *pos;
	pid_t lead_pid;
	int idx;
	
	lead_pid = -1;
	idx = 0;
	create_pipes();

	list_for_each(pos, &tasks) {
		struct task *task;

		task = get_elem(pos, struct task, next);
		run_task(task, &lead_pid, idx);
		idx++;
	}
}

static void try_chdir(void)
{
	char *space_delim;

	/* parse 'cmd', get new directory from string */
	space_delim = strchr(cmd, ' ');

	if (space_delim == NULL)
		return;

	/* skip rest of spaces */
	while (*space_delim == ' ') space_delim++;

	if (chdir(space_delim))
		printf("chdir: %s\n", space_delim);
		
	return;
}

static void do_exit(void)
{
	exit(1);
}

struct termios ts;
struct sigaction sa;

static void try_dwn(char *cmd)
{
	int pid;
	struct task *task;

	sscanf(cmd, "dwn %i", &pid);
	task = get_task(pid);

	if (task != NULL)
		task->flag |= DSWN;
}

static void try_export(char *cmd)
{
	/* NI */
}


#ifdef TERMNC
static void restore_term(void)
{
	struct termios tattr;

	tcgetattr(0, &tattr);
	tattr.c_lflag &= (ICANON | ECHO) ; /* Clear ICANON and ECHO. */

	if (tcsetattr(0, TCSANOW, &tattr)) {
		printf("Failed to set terminal setting!\n");
		exit(1);
	}
}

static void init_termc(void)
{
	if (tcgetattr(0, &tattr)) {
		printf("Failed to get terminal setting!\n");
		exit(1);
	}

	tattr.c_lflag &= ~(ICANON | ECHO) ; /* Clear ICANON and ECHO. */
	tattr.c_cc[VMIN] = 1;
	tattr.c_cc[VTIME] = 0;

	if (tcsetattr(0, TCSANOW, &tattr)) {
		printf("Failed to set terminal setting!\n");
		exit(1);
	}
}

static void do_backspace(void)
{
	char bck[] = {'\b',' ','\b'} ;

	write(1, bck, sizeof bck);
}

#define	CURSOR_UP	1
#define	CURSOR_DOWN	2

static void move_cur_end(void)
{
	int tmp_cursor_pos;
	char s[] = {0x1b, '[', '1', 'C'};

	tmp_cursor_pos = cursor_pos;

	while (tmp_cursor_pos < strlen(cmd)) {
		write(1, s, sizeof s);
		tmp_cursor_pos++;
	}
}

static void erase_cmd(void)
{
	ssize_t cmd_len;
	int i;

	cmd_len = strlen(cmd);
	move_cur_end();

	for(i=0;i < cmd_len;i++)
		do_backspace();
}
/* clear current input string */
static void flush_cmd(void)
{
	erase_cmd();
	memset(cmd, 0, CMD_LEN);
}

static void set_up_cmd(void)
{
	struct list_head *prev;

	if (curr_cmd->idx == 0)
		return;

	prev = curr_cmd->next.prev;
	curr_cmd = get_elem(prev, struct history_ent, next);
#ifdef	DEBUG
	printf("   NEW UP CURR:#%s#\n", curr_cmd->cmd_string);
#endif
}

static void set_down_cmd(void)
{
	struct list_head *next;

	if (curr_cmd->idx == (history_sz - 1))
		return;

	next = curr_cmd->next.next;
	curr_cmd = get_elem(next, struct history_ent, next);
#ifdef	DEBUG
	printf("   NEW DOWN CURR:#%s#\n", curr_cmd->cmd_string);
#endif
}

static void cmd_up_down(int action)
{
	if (history_sz == 0)
		return;

	if ((action == CMD_DOWN) && (curr_cmd->idx == 0))
		set_down_cmd();

	if ((action == CMD_UP) && (curr_cmd->idx == history_sz - 1)) {
		if (strcmp(cmd, curr_cmd->cmd_string) == 0)
			set_up_cmd();
	}

	/* flush current input in terminal */
	flush_cmd();
	/* print next command */
	write(1, curr_cmd->cmd_string, strlen(curr_cmd->cmd_string));
	strcpy(cmd, curr_cmd->cmd_string);
	cursor_pos = strlen(cmd);

	if (action == CMD_UP)
		set_up_cmd();
	else
		set_down_cmd();
}

static void cursor_right(void)
{
	char s[] = {0x1b, '[', '1', 'C'};

	if (cursor_pos < strlen(cmd)) {
		write(1, s, sizeof s);
		cursor_pos++;
	}
}

static void cursor_left(void)
{
	char s[] = {0x1b, '[', '1', 'D'};

	if (cursor_pos) {
		write(1, s, sizeof s);
		cursor_pos--;
	}
}

static int handle_ansi(void)
{
	unsigned char second_byte;

	read(0, &second_byte, sizeof second_byte);

	if (second_byte == '[') {
		unsigned char cntrl_byte;

		read(0, &cntrl_byte, sizeof cntrl_byte);

		/* cursor up */
		switch (cntrl_byte) {
			case 0x41:
				cmd_up_down(CMD_UP);
				return 0;
			case 0x42:
				cmd_up_down(CMD_DOWN);
				return 0;
			case 0x43:
				/* cursor right */
				cursor_right();
				return 0;
			case 0x44:
				/* cursor left */
				cursor_left();
				return 0;
			default:
				return 0;
		}
	}

	return 0;
}

static void add_to_history(const char *cmd)
{
	struct history_ent *new_ent;
	size_t sz;

	sz = (sizeof *new_ent) + strlen(cmd) + 1;
	new_ent = malloc(sz);
	ASSERT_ERR("malloc failed\n", (new_ent == NULL));
	strcpy(new_ent->cmd_string, cmd);
	new_ent->idx = history_sz;
	list_add_tail(&new_ent->next, &history);
	history_sz++;
	curr_cmd = new_ent;
}

static void dump_history(void)
{
	struct list_head *pos;
	int i;

	i = 0;

	list_for_each(pos, &history) {
		struct history_ent *ent;

		ent = get_elem(pos, struct history_ent, next);
		printf("[%i] %s\n", i, ent->cmd_string);
		i++;
	}
}

static void erase_word(void)
{
	size_t new_len;

	/* erase previous input */
	erase_cmd();
	new_len = strlen(cmd) + 1 - cursor_pos;
	memmove(cmd, &cmd[cursor_pos], new_len);
	write(1, cmd, new_len);
	cursor_pos = strlen(cmd);
}

static int read_cmd(void)
{
	char c;
	int tab_cnt;

	memset(cmd, 0, CMD_LEN);
	tab_cnt = 0;
	cursor_pos = 0;

	while (1) {
		read (0, &c, sizeof c);

		/* tab */
		if (c == 0x9) {
			tab_cnt++;

			if (tab_cnt == 2) {
				/* run autocompletion */
				print_ac(cmd);
				printf("\n");
				write(1, prompt, strlen(prompt));
				write(1, cmd, strlen(cmd));
			}	
			continue;
		}

		tab_cnt = 0;

		/* kill character: erase whole line */
		if (c == tattr.c_cc[VKILL]) {
			flush_cmd();
			cursor_pos = 0;
			continue;
		}

		/* word erase: erase word */
		if (c == tattr.c_cc[VWERASE]) {
			erase_word();
			continue;
		}

		/* backspace */
		if (c == 0x8) {
			if (cursor_pos) {
				do_backspace();
				cmd[cursor_pos] = '\0';
				cursor_pos--;
			}
			continue;
		}


		/* esc char - some ANSI code */
		if (c == 0x1B) {
			handle_ansi();
			continue;
		}

		write(1, &c, sizeof c);
		cmd[cursor_pos++] = c;

		if (c == 0xA) {
			break;
		}


	}

	return 0;
}
#else
static void restore_term(void)
{
}

static void init_termnc(void)
{
}

static int read_cmd(void)
{
	int n;

	memset(cmd, 0, CMD_LEN);
	n = read(0, cmd, CMD_LEN);

	if (n < -1) {
		fprintf(stderr, "read:%i %s\n", n, strerror(errno));
		return -1;
	}

	if (cmd[CMD_LEN - 1] != '\0') {
		fprintf(stderr, "Too long cmd, max is %i\n", CMD_LEN);
		return -1;
	}

	return 0;
}
#endif /* TERMNC */

static void alloc_cmd(void)
{
	/* default cmd len is 4096 */
	cmd = malloc(CMD_LEN);
	ASSERT_ERR ("malloc failed\n", (cmd == NULL));
}

static int process_builtins(void)
{
	int i;

	i = 0;

	for(i = 0;i < ARR_SZ(builtins);i++) {
		if (strstr(cmd, builtins[i].name) == &cmd[0])
			if (builtins[i].handler != NULL) {
				builtins[i].handler();
				return 0;
			}
	}

	return -1;
}

int main(void)
{
	builtins_num = ARR_SZ(builtins);
	init_autoc();
	alloc_cmd();
	init_termc();
	init_prompt();
	signal(SIGINT, SIG_IGN);
	signal(SIGTSTP, SIG_IGN);
#ifdef	DEBUG
	signal(SIGSEGV, hndl_ssegv);
#endif
	sa.sa_sigaction = hndl_chld1;
	sa.sa_flags |= (SA_SIGINFO | SA_RESTART);
	sigaction(SIGCHLD, &sa, NULL);
	signal(SIGHUP, hndl_sighup);
	tcsetpgrp(0, getpid());

	while (1) {
		char *p;

		write(1, prompt, strlen(prompt));
		if (read_cmd() < 0)
			continue;

		/* remove nl */
		p = strchr(cmd, '\n');

		if (p != NULL)
			*p = '\0';

		/* skip empty command */
		if (strlen(cmd) == 0)
			continue;

		add_to_history(cmd);
		if (process_builtins() == 0)
			continue;

		if (strstr(cmd, "mv ") == &cmd[0]) {
			char type;
			int task_pid;
			struct task *task_to_move;

			/* mv b/f pid */
			sscanf(cmd, "mv %c %i\n", &type, &task_pid);
			task_to_move = get_task(task_pid);

			if (task_to_move != NULL) {
				int status;

				if (type == 'f')
					task_to_move->flag = FG;

				if (type == 'b')
					task_to_move->flag = BG;
				else
					continue;

				/* try to move task */
				printf("moving %i to %c\n", task_to_move->pid, type);
				tcsetpgrp(0, task_to_move->pid);
				waitpid(-1, &status, WUNTRACED | WCONTINUED);
				tcsetpgrp(0, getpid());
			}

			continue;
		}

		/* parse arguments if needed*/
		parse_cmd();
		/* now exec list of commands */
		exec_cmd();

		if (will_wait == 1) {
			while (1) {
				pause();

				if (tasks_num == 0) {
					tcsetpgrp(0, getpid());
#ifdef DEBUG
					printf("wake!\n");
#endif
					break;
				}
			};
		}

	};
}
