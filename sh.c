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
#include <sys/ucontext.h>
#include <fcntl.h>
#include <stdarg.h>
#include "list.h"
#include "sh.h"
#include "common.h"
#include "acompl.h"
#include "term.h"

#define CMD_ALLOC_LEN 1024

extern char **environ;
/* prompt string */
static char prompt[50];
/* jobs list */
static int tasks_num;
/* input command */
static char *cmd = NULL;
static int cmd_len;
static int cursor_pos;
/* bg/fg job flag */
static int bck;
/* shell history variables */
static struct list_head history = {&history, &history};
static struct history_ent *curr_cmd;
static int history_sz = 0;
int builtins_num;
static int jobs_idxs[MAX_JOBS];
static struct job *jobs_ptrs[MAX_JOBS];

static struct job *curr_job;
struct list_head jobs = {&jobs, &jobs};
static void try_chdir(char *cmd);
static void dump_history(char *cmd);
static void do_exit(char *cmd);
static void do_bg(char *cmd);
static void do_fg(char *cmd);
static void list_jobs(char *cmd);

/* terminal parameters */
struct termios tattr;

struct builtin_ent builtins[] = {{"cd", try_chdir}, {"help", NULL},
				 {"history", dump_history}, {"jobs", list_jobs},
				 {"exit", do_exit}, {"bg", do_bg}, {"fg", do_fg}
				 };

#ifdef	DEBUG
#define	DEBUG_FILE	"debug.txt"
static FILE *debug_file;
static void init_debug(void)
{
	debug_file = FILE(DEBUG_FILE, "w+");
}
static void dbgprintf(const char *format, ...)
{
	va_list list;

	va_start(list, format);
	vfprintf(debug_file, format, list);
	va_end(list);
}
#else
static void init_debug(void)
{
}
static void dbgprintf(const char *format, ...)
{
}
#endif

static struct task *get_task_in_job(struct job *job, int pid)
{
	struct list_head *pos;

	list_for_each(pos, &job->tasks) {
		struct task *task;

		task = get_elem(pos, struct task, next);

		if (task->pid == pid)
			return task;
	}

	return NULL;
}

static struct task *get_task(int pid)
{
	struct list_head *pos;

	list_for_each(pos, &jobs) {
		struct job *job;
		struct task *result;

		job = get_elem(pos, struct job, next);
		result = get_task_in_job(job, pid);

		if (result != NULL) {
			return result;
		}
	}

	return NULL;
}

static void hndl_chld1(int code, siginfo_t *si, void *arg)
{
	int pid;
	int status;
	struct task *task;
	struct job *job;

	pid = waitpid(-1, &status, WUNTRACED | WCONTINUED);
	dbgprintf("task %i done!\n", pid);
	task = get_task(pid);

	if (task == NULL) {
		printf("pid %i not found in jobs...\n", pid);
	} else {
		job = task->job;
		if (WIFEXITED(status)) {
			dbgprintf("%i exited\n", pid);
			delete_item(&task->next);
			free(task);
			job->tasks_num--;

			if (job->tasks_num == 0) {
				/* job is done */
				dbgprintf("freeing job %i\n", job->idx);
				if (job->bckg == 1)
					printf("job %i done\n", job->idx);

				jobs_idxs[job->idx] = 0;
				jobs_ptrs[job->idx] = NULL;
				delete_item(&job->next);
				free(job);
			}
		}
		if (WIFSTOPPED(status))
			printf("job %i task %i stopped\n", job->idx, task->pid);
	}

	tasks_num--;
	return;
}

static void sighup_jobs(void)
{
	struct list_head *pos;

	list_for_each(pos, &jobs) {
		struct job *job;

		job = get_elem(pos, struct job, next);

		if (job->dswnd == 0)
			killpg(job->pgid, SIGHUP);
	}
}

/* SIGHUP handler */
static void hndl_sighup(int code)
{
	dbgprintf("sighup caught!\n");
	sighup_jobs();
	getchar();
	exit(1);
}

#ifdef	DEBUG
/* only for development/debug purposes */
static void sigsegv_action(int code, siginfo_t *si, void *ctx)
{
	ucontext_t *ctx_ptr;

	ctx_ptr = ctx;
	printf("segmentation fault!\n");
	printf("Context:\n");
#ifdef __FreeBSD__
	printf("RIP: %08lX\n", ctx_ptr->uc_mcontext.mc_rip);
#else
	printf("RIP: %08llX\n", ctx_ptr->uc_mcontext.gregs[16]);
#endif
	getchar();
}
#endif

static void list_jobs(char *cmd)
{
	struct list_head *pos;

	printf("jobs:\n");

	list_for_each(pos, &jobs) {
		struct job *job;

		job = get_elem(pos, struct job, next);
		printf("[%i] %s\n", job->idx, job->name);
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

static void print_prompt(void)
{
	write(1, prompt, strlen(prompt));
}

#ifdef DEBUG
static void dump_tasks(void)
{
	struct list_head *pos;

	printf("tasks:\n");
	list_for_each(pos, &curr_job->tasks) {
		struct task *task;

		task = get_elem(pos, struct task, next);
		printf("%s\n", task->name);
	}
}
#endif


static int get_next_idx(void)
{
	int i;

	for(i = 0;i < MAX_JOBS;i++) {
		if (jobs_idxs[i] == 0) {
			jobs_idxs[i] = 1;
			return i;
		}
	}

	return -1;
}

/*
 * Parses command line for arguments and pipes.
 */
static void parse_cmd(void)
{
	curr_job = parse(cmd, &bck);

	if (curr_job != NULL) {
		curr_job->idx = get_next_idx();
		jobs_ptrs[curr_job->idx] = curr_job;
		list_add_tail(&curr_job->next, &jobs);
		dbgprintf("back:%i\n", bck);
#ifdef DEBUG
		dump_tasks();
#endif
	}
}

static void set_default_sig(void)
{
	signal(SIGINT, SIG_DFL);
	signal(SIGCHLD, SIG_DFL);
	signal(SIGQUIT, SIG_DFL);
	signal(SIGTSTP, SIG_DFL);
	signal(SIGSEGV, SIG_DFL);
}

static int left_pipe[2];
static int right_pipe[2];
static int job_gid;

static int run_task(struct task *task)
{
	char *full_path;
	pid_t pid;
	dbgprintf("new run task %i\n", task->idx);

	if (strchr(task->name, '/') != NULL) {
		/* name contains '/', don't scan $PATH */
		full_path = task->name;
	} else {
		full_path = get_full_path(task->name);

		if (full_path == NULL) {
			fprintf(stderr, "Can't find %s\n", task->name);
			return -1;
		}
	}

	tasks_num++;

	if (task->idx == -1) {
		/* only one task */
		pid = fork();

		if (pid == -1)
			goto err;

		if (pid == 0) {
			setpgid(0, 0);

			if (curr_job->bckg == 0)
				tcsetpgrp(0, getpid());
			set_default_sig();
			execve(full_path, task->args, environ);
		} else
			task->pid = pid;
		return 0;
	}

	if (task->idx == 0) {
		/* create left pipe */
		pipe(left_pipe);
		pid = fork();
		dbgprintf("task %i is %i\n", task->idx, pid);

		if (pid == -1) 
			goto err;

		if (pid) {
			job_gid = pid;
			curr_job->pgid = pid;
			task->pid = pid;
			return 0;
		} else {
			/* child */
			setpgid(0, 0);
			set_default_sig();
			if (curr_job->bckg == 0)
				tcsetpgrp(0, getpid());
			dup2(left_pipe[1], 1);
			close(left_pipe[0]);
			execve(full_path, task->args, environ);
		}
	} else {
		/* create right pipe */
		if (task->is_last == 0)
			pipe(right_pipe);
		pid = fork();
		dbgprintf ("task %i is %i\n", task->idx, pid);

		if (pid == -1) {
			fprintf(stderr, "Failed to fork %s\n", strerror(errno));
			exit(1);
		}

		if (pid) {
			close(left_pipe[0]);
			close(left_pipe[1]);
			task->pid = pid;
		} else {
			setpgid(0, job_gid);
			tcsetpgrp(0, getpid());
			if (task->is_last == 0)
				dup2(right_pipe[1], 1);

			dup2(left_pipe[0], 0);
			close(left_pipe[1]);
			execve(full_path, task->args, environ);
		}

		memcpy(left_pipe, right_pipe, sizeof (left_pipe));
		return 0;
	}
err:
	fprintf(stderr, "Failed to fork %s\n", strerror(errno));
	exit(1);
}

/* Processes global list of commands. */
static void exec_cmd(void)
{
	struct list_head *pos;

	tasks_num = 0;

	list_for_each(pos, &curr_job->tasks) {
		struct task *task;

		task = get_elem(pos, struct task, next);
		dbgprintf("task %i\n", task->idx);

		if (run_task(task) == -1)
			return;
	}
}

static void try_chdir(char *cmd)
{
	char *space_delim;

	/* parse 'cmd', get new directory from string */
	space_delim = strchr(cmd, ' ');

	if (space_delim == NULL)
		return;

	/* skip rest of spaces */
	while (*space_delim == ' ') space_delim++;

	chdir(space_delim);
	return;
}

static void do_exit(char *cmd)
{
	exit(1);
}

static void do_bg(char *cmd)
{
	int job_idx;

	sscanf(cmd, "bg %i", &job_idx);

	if (job_idx < 0 || job_idx >= MAX_JOBS)
		goto err;

	if (jobs_ptrs[job_idx] == NULL)
		goto err;

	/* send SIGCONT */

err:
	fprintf(stderr, "invalid job index\n");
	return;
}

static void do_fg(char *cmd)
{
	int job_idx;

	sscanf(cmd, "fg %i", &job_idx);
}

static void try_dwn(char *cmd)
{
	int idx;

	sscanf(cmd, "disown %i", &idx);

	if (jobs_ptrs[idx] == NULL)
		return;

	jobs_ptrs[idx]->dswnd = 1;
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
	memset(cmd, 0, cmd_len);
}

static void set_up_cmd(void)
{
	struct list_head *prev;

	if (curr_cmd->idx == 0)
		return;

	prev = curr_cmd->next.prev;
	curr_cmd = get_elem(prev, struct history_ent, next);
	dbgprintf("   NEW UP CURR:#%s#\n", curr_cmd->cmd_string);
}

static void set_down_cmd(void)
{
	struct list_head *next;

	if (curr_cmd->idx == (history_sz - 1))
		return;

	next = curr_cmd->next.next;
	curr_cmd = get_elem(next, struct history_ent, next);
	dbgprintf("   NEW DOWN CURR:#%s#\n", curr_cmd->cmd_string);
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

#ifdef	DEBUG_INPUT
#define	INPUT_FILE	"input.bin"
static int input_fd;
static void log_input(unsigned char value)
{
	write(input_fd, &value, sizeof value);
}

static void init_debug_input(void)
{
	input_fd = creat(INPUT_FILE, 0664);

	if (input_fd == -1) {
		fprintf(stderr, "Failed to open input file: %s\n", strerror(errno));
		getchar();
		exit(1);
	}
}

static void deinit_debug_input(void)
{
	close(input_fd);
}
#else
static void log_input(unsigned char value __attribute__ ((unused)))
{
}

static void init_debug_input(void)
{
}

static void deinit_debug_input(void)
{
}
#endif

static int handle_ansi(void)
{
	unsigned char second_byte;

	read(0, &second_byte, sizeof second_byte);
	log_input(second_byte);

	if (second_byte == '[') {
		unsigned char cntrl_byte;

		read(0, &cntrl_byte, sizeof cntrl_byte);
		log_input(cntrl_byte);

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

#define	HIST_FILE	"tsh_hist"
static void load_history(void)
{
	FILE *hist_file;
	char *hist_string;
	int ch;
	char *err_str;
	int pos;
	int max_hist_strlen;

	hist_file = fopen(HIST_FILE, "r");

	if (hist_file == NULL) {
		err_str = strerror(errno);
		goto err;
	}

	pos = 0;
	max_hist_strlen = 50;
	hist_string = calloc(max_hist_strlen, 1);

	if (hist_string == NULL) {
		err_str = "calloc fail";
		goto err;
	}

	while ((ch = fgetc(hist_file)) != EOF) {
		if (ch == '\n') {
			add_to_history(hist_string);
			dbgprintf("add to history: %s\n", hist_string);
			memset(hist_string, 0, max_hist_strlen);
			pos = 0;
			continue;
		}

		if (pos == max_hist_strlen) {
			hist_string = realloc(hist_string, max_hist_strlen += 10);

			if (hist_string == NULL) {
				err_str = "realloc fail";
				goto err;
			}
		}

		hist_string[pos++] = ch;
	};

	return;
err:
	fprintf (stderr, "Failed to load history file: %s\n", err_str);
	return;
}

static void dump_history(char *cmd)
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

/*
 * erase word: find nearest space to the
 * left from current cursor position and
 * delete all char from it to cursor
 * position.
*/
static int nearest_space(void)
{
	int i;
	int flag;

	i = cursor_pos;
	flag = 0;

	while (i > -1) {
		if (cmd[i] == ' ') {
			if (flag != 0)
				return i;
		} else {
			flag = 1;
		}

		i--;
	};

	if (i == -1)
		i = 0;

	return i;
}
static void erase_word(void)
{
	size_t new_len;
	int nearest_idx;

	nearest_idx = nearest_space();
	new_len = strlen(cmd) - cursor_pos + 1;
	memmove(&cmd[nearest_idx], &cmd[cursor_pos], new_len);
	t_erase_line();
	print_prompt();
	write(1, cmd, strlen(cmd));
}

static void realloc_cmd(int new_cursor_pos)
{
	if (new_cursor_pos == cmd_len) {
		cmd_len += CMD_ALLOC_LEN;
		cmd = realloc(cmd, cmd_len);
	}
}

static void insert_ch(char c)
{
	realloc_cmd(cursor_pos + 1);

	if (cursor_pos < strlen(cmd)) {
		/* insert char to command string */
		memmove(&cmd[cursor_pos + 1], &cmd[cursor_pos],
		strlen(cmd) - cursor_pos + 1);
		cmd[cursor_pos] = c;
		write(1, &cmd[cursor_pos], strlen(cmd) - cursor_pos + 1);
		cursor_pos = strlen(cmd);
	} else {
		/* cursor at last position */
		cmd[cursor_pos++] = c;
		write(1, &c, sizeof c);
	}
}

static void handle_backspace(void)
{
	if (cursor_pos < strlen(cmd)) {
		size_t len;
		int old_pos;

		old_pos = cursor_pos;
		len = strlen(cmd);
		t_erase_line();
		t_move_cur_back(len + strlen(prompt));
		memmove(&cmd[cursor_pos - 1], &cmd[cursor_pos],
			len - cursor_pos + 1);
		print_prompt();
		write(1, cmd, strlen(cmd));
		t_move_cur_back(len - old_pos);
		cursor_pos--;
	} else {
		/* simple case, just print backspace */
		do_backspace();
		cursor_pos--;
		cmd[cursor_pos] = '\0';
	}
}


static int read_cmd(void)
{
	unsigned char c;
	int tab_cnt;

	memset(cmd, 0, cmd_len);
	tab_cnt = 0;
	cursor_pos = 0;

	while (1) {
		read (0, &c, sizeof c);
		log_input(c);

		/* tab */
		if (c == 0x9) {
			tab_cnt++;

			if (tab_cnt == 2) {
				/* run autocompletion */
				print_ac(cmd);
				//printf("\n");
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
			//erase_word();
			continue;
		}

		/* backspace */
		if (c == 0x08) {
			if (cursor_pos)
				handle_backspace();
			continue;
		}

		/* esc char - some ANSI code */
		if (c == 0x1B) {
			handle_ansi();
			continue;
		}

		/* enter key */
		if (c == 0xA) {
			printf("\n");
			break;
		}

		insert_ch(c);
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

	memset(cmd, 0, cmd_len);
	n = read(0, cmd, cmd_len);

	if (n < -1) {
		fprintf(stderr, "read:%i %s\n", n, strerror(errno));
		return -1;
	}

	if (cmd[cmd_len - 1] != '\0') {
		fprintf(stderr, "Too long cmd, max is %i\n", cmd_len);
		return -1;
	}

	return 0;
}
#endif /* TERMNC */

static void alloc_cmd(void)
{
	/* default cmd len is 4096 */
	cmd = malloc(CMD_ALLOC_LEN);
	cmd_len = CMD_ALLOC_LEN;
	ASSERT_ERR ("malloc failed\n", (cmd == NULL));
}

static int process_builtins(char *cmd)
{
	int i;

	i = 0;

	for(i = 0;i < ARR_SZ(builtins);i++) {
		if (strcmp(cmd, builtins[i].name) == 0)
			if (builtins[i].handler != NULL) {
				builtins[i].handler(cmd);
				return 0;
			}
	}

	return -1;
}

#ifdef	DEBUG
static int set_sigsegv(void)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof sa);
	sa.sa_sigaction = sigsegv_action;
	sa.sa_flags |= (SA_SIGINFO | SA_RESTART);
	return sigaction(SIGSEGV, &sa, NULL);
}
#else
static int set_sigsegv(void)
{
	return 0;
}
#endif

static void set_signals(void)
{
	const char *sig = "SIGINT";
	struct sigaction sa;

	if (signal(SIGINT, SIG_IGN) == SIG_ERR)
		goto err;

	sig = "SIGTSTP";
	if (signal(SIGTSTP, SIG_IGN) == SIG_ERR)
		goto err;

	sig = "SIGSEGV";
	if (set_sigsegv() == -1)
		goto err;

	sig = "SIGHUP";
	if (signal(SIGHUP, hndl_sighup) == SIG_ERR)
		goto err;

	sig = "SIGCHLD";
	memset(&sa, 0, sizeof sa);
	sa.sa_sigaction = hndl_chld1;
	sa.sa_flags |= (SA_SIGINFO);
	if (sigaction(SIGCHLD, &sa, NULL) == -1)
		goto err;

	return;
err:
	fprintf(stderr, "Failed to set %s handler: %s\n", sig, strerror(errno));
	getchar();
	return;
}

#ifdef	DEBUG_OUTPUT
#define	OUTPUT_FILE	"output.txt"
static void dup_debug_output(void)
{
	int output_fd;

	output_fd = creat(OUTPUT_FILE, 0664);

	if (output_fd == -1) {
		fprintf(stderr, "Failed to open output file: %s\n", strerror(errno));
		getchar();
		exit(1);
	}

	setvbuf(stdout, NULL, _IONBF, 0);
	dup2(output_fd, 1);
}
#else
static void dup_debug_output(void)
{
}
#endif

int main(void)
{
	builtins_num = ARR_SZ(builtins);
	dup_debug_output();
	init_debug_input();
	init_autoc();
	alloc_cmd();
	init_termc();
	init_prompt();
	load_history();
	tcsetpgrp(0, getpid());
	set_signals();
	init_debug();

	while (1) {
		char *p;

		print_prompt();

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

		if (process_builtins(cmd) == 0)
			continue;

		/* parse arguments if needed*/
		parse_cmd();
		/* now exec list of commands */
		exec_cmd();

		/* run in background */
		if (tasks_num && bck == 0) {
			while (1) {
				if (tasks_num == 0) {
					tcsetpgrp(0, getpid());
					dbgprintf("wake!\n");
					break;
				} else
					pause();
			};
		}

	};
}
