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
#include "list.h"
#include "sh.h"

#define CMD_LEN 512

static char *cmd_args[MAX_ARGS];

char prompt[50];
extern char **environ;

struct job {
	int pid;
	int flag;
};

struct list_head tasks = {&tasks, &tasks};
static int pipes_num;
static int tasks_num;

struct job jobs[MAX_JOBS];

char cmd[CMD_LEN];
int bck;

struct job *get_job(int pid);
static int *pipes;

struct task *get_task(int pid)
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

void hndl_chld1(int code, siginfo_t *si, void *arg)
{
	struct job *job;
	struct task *task;
	int status;

	job = get_job(si->si_pid);

	if (job == NULL) {
		printf("shiiiit!\n");
		exit(1);
	}

	job->flag = 0;
	wait(&status);
	task = get_task(si->si_pid);
#ifdef DEBUG
	fprintf(stderr, "%i done!\n", pid);
#endif

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

	tasks_num--;

	if (tasks_num == 0)
		free(pipes);
}

void sighup_jobs(void)
{
	int i;

	for(i = 0;i < MAX_JOBS;i++)
		if (jobs[i].flag & DSWN)
			kill(jobs[i].pid, SIGHUP);
}

/* SIGHUP handler */
void hndl_sighup(int code)
{
	printf("sighup caught!\n");
	sighup_jobs();
	getchar();
	exit(1);
}

void hndl_chld2(int code)
{
	//printf("chld2!\n");
}

void kill_all(void)
{
	int i;

	for(i = 0;i < MAX_JOBS;i++)
		if (jobs[i].flag)
			kill(jobs[i].pid, SIGKILL);
}

void list_jobs(void)
{
	int i;

	printf("jobs:\n");

	for(i = 0;i < MAX_JOBS;i++) {
		if (jobs[i].flag)
			printf("[%i] %i %s\n", i, jobs[i].pid, (jobs[i].flag & FG) ? "fg" : "bg");
	}
}

void add_job(int pid, int bck)
{
	int i;

	for(i = 0;i < MAX_JOBS;i++) {
		if (jobs[i].flag == 0) {
			jobs[i].pid = pid;
			if (bck)
				jobs[i].flag = BG;
			else
				jobs[i].flag = FG;
			return;
		}
	}
	printf("job list is full!\n");
	kill_all();
	getchar();
	exit(1);
}

struct job *get_job(int pid)
{
	int i;

	for(i = 0;i < MAX_JOBS;i++) {
		if (jobs[i].flag) {
			if (jobs[i].pid == pid)
				return &jobs[i];
		}
	}

	return NULL;
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

	if (full_path == NULL)
		return NULL;

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

	path_var = getenv("PATH");

	if (path_var == NULL)
		return NULL;

	path = strtok(path_var, ":");

	while (path != NULL) {
		full_path = look_for_binary(path, name);

		if (full_path != NULL)
			return full_path;

		path = strtok(NULL, ":");
	};

	return NULL;
}

static int exec_bin(struct task *task)
{
	char *full_path;

	full_path = get_full_path(task->name);

	if (full_path == NULL) {
		fprintf(stderr, "path failed: %s\n", task->name);
		return -1;
	}

	cmd_args[0] = &cmd[0];
#ifdef DEBUG
	fprintf(stderr, "execing %s %s\n", __func__, full_path);
#endif
	execve(full_path, task->args, environ);

	return 0;
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

	if (pipes == NULL) {
		printf("malloc failed!\n");
		exit(1);
	}

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

	for(i = 0;i < (pipes_num*2);i++) {
		if ((p[i] == fd1) || (p[i] == fd2))
			continue;
		close(p[i]);
	}
}

static void run_task(struct task *task, int *lead_pid, int idx)
{
	pid_t pid;

	task->idx = idx;
	assign_pipes(task);

	if ((pid = fork())) {
		add_job(pid, bck);

		if (*lead_pid == -1)
			*lead_pid = pid;

		task->pid = pid;
	} else {
			
		setpgid(0, 0);
		/*if (*lead_pid == -1)
			setpgid(0, 0);
		else
			setpgid(0, *lead_pid);*/

		signal(SIGINT, SIG_DFL);
		signal(SIGCHLD, SIG_DFL);
		signal(SIGQUIT, SIG_DFL);
		signal(SIGTSTP, SIG_DFL);

		if ((idx == 0) && (bck == 0)) {
#ifdef DEBUG
			fprintf(stderr, "set child to fg!\n");
#endif
			tcsetpgrp(0, getpid());
		}

		if (pipes_num) {
			if (idx == 0) {
#ifdef DEBUG
				fprintf(stderr, "first %s idx %i; write to %i, closing %i\n", task->name, idx, pipes[1], pipes[0]);
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

		exec_bin(task);
		fprintf(stderr, "exec %s failed!\n", task->name);

		exit(0);
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

static void try_chdir(char *cmd)
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

struct termios ts;
struct sigaction sa;

static void try_dwn(char *cmd)
{
	int pid;
	struct job *job;

	sscanf(cmd, "dwn %i", &pid);
	job = get_job(pid);

	if (job != NULL)
		job->flag |= DSWN;
}

static void try_export(char *cmd)
{
	
}


#ifdef TERMNC
static int read_cmd(void)
{
	int result;

	result = 0;
	memset(cmd, 0, sizeof cmd);

	while(result < MAX_CMD_NAME) {
		char c;

		if (read(0, &c, sizeof c) != sizeof c)
			break;
#ifdef DEBUG
		printf("%02X\n", c);
#endif
		cmd[result] = c;

		if (c == 0xA)
			break;
		result++;
	};

	return result;
}
#else
static int read_cmd(void)
{
	int n;

	memset(cmd, 0, sizeof cmd);
	n = read(0, cmd, sizeof cmd);
#ifdef DEBUG
	printf("read:%i %s\n", n, strerror(errno));
#endif
	return n;
}
#endif /* TERMNC */

#ifdef TERMNC
void init_termc(void)
{
	struct termios ti;

	tcgetattr(0, &ti);
	ti.c_iflag = (BRKINT | ICRNL | IXON | IXANY | IMAXBEL | IUTF8);
	ti.c_oflag = (OPOST | ONLCR);
	ti.c_cflag = (CBAUD | CSIZE | CREAD | HUPCL);
	//ti.c_lflag = (ISIG | ECHOE | ECHOK | ECHOCTL | ECHOKE | IEXTEN);
	ti.c_lflag = (ISIG | ECHO | ECHOCTL | ECHOKE | IEXTEN);
	printf("%08X\n", ti.c_lflag);
	//ti.c_lflag = 0x8a31;
	ti.c_cc[7] = 0xFF;
	ti.c_cc[11] = 0xFF;
	ti.c_cc[16] = 0xFF;

	if (tcsetattr(0, TCSANOW, &ti)) {
		printf("Failed to set terminal setting!\n");
		exit(1);
	}
}
#else
void init_termnc(void)
{
	struct termios ti;

	tcgetattr(0, &ti);
	ti.c_iflag = 0x6d02;
	ti.c_oflag = 0x5;
	ti.c_cflag = 0x4bf;
	ti.c_lflag = 0x8a3b;
	ti.c_cc[7] = 0xFF;
	ti.c_cc[11] = 0xFF;
	ti.c_cc[16] = 0xFF;

	if (tcsetattr(0, TCSANOW, &ti)) {
		printf("Failed to set terminal setting!\n");
		exit(1);
	}
	if (tcsetattr(1, TCSANOW, &ti)) {
		printf("Failed to set terminal setting!\n");
		exit(1);
	}
	if (tcsetattr(2, TCSANOW, &ti)) {
		printf("Failed to set terminal setting!\n");
		exit(1);
	}
}
#endif /* TERMNC */

int main(void)
{
#ifdef TERMNC
	init_termc();
#else
	//init_termnc();
#endif
	//setsid();
	init_prompt();
	//setpgrp();
	//setpgid(0, 0);
	signal(SIGINT, SIG_IGN);
	signal(SIGTSTP, SIG_IGN);
	sa.sa_sigaction = hndl_chld1;
	sa.sa_flags |= SA_SIGINFO;
	//sa.sa_flags &= ~SA_NOCLDSTOP;
	sigaction(SIGCHLD, &sa, NULL);
	//signal(SIGCHLD, hndl_chld1);
	signal(SIGHUP, hndl_sighup);
	tcsetpgrp(0, getpid());

	while (1) {
		int n;
		char *p;

		write(1, prompt, strlen(prompt));
		n = read_cmd();
#ifdef TERMNC
		/* 'nl' handling */
		if (cmd[0] == 0xA) {
			//write(1, "\n", 1);
			continue;
		}
#else
		if (n == 1)
			continue;
#endif

		/* remove nl */
		p = strchr(cmd, '\n');

		if (p != NULL)
			*p = '\0';
		
		/* check embedded commands */
		if (strcmp(cmd, "lst") == 0) {
			/* print all jobs */
			list_jobs();
			continue;
		}

		if (strstr(cmd, "cd ") == &cmd[0]) {
			/* change current directory */
			try_chdir(cmd);
			continue;
		}

		if (strstr(cmd, "dwn ") == &cmd[0]) {
			/* remove job from 'SIGHUP' send list */
			try_dwn(cmd);
			continue;
		}

		if (strstr(cmd, "export ") == &cmd[0]) {
			/* set new environment variable */
			try_export(cmd);
			continue;
		}

		if (strcmp(cmd, "exit") == 0)
			exit(1);

		if (strstr(cmd, "mv ") == &cmd[0]) {
			char type;
			int job_pid;
			struct job *job_to_move;

			/* mv b/f pid */
			sscanf(cmd, "mv %c %i\n", &type, &job_pid);
			job_to_move = get_job(job_pid);

			if (job_to_move != NULL) {
				int status;

				if (type == 'f')
					job_to_move->flag = FG;

				if (type == 'b')
					job_to_move->flag = BG;
				else
					continue;

				/* try to move job */
				printf("moving %i to %c\n", job_to_move->pid, type);
				tcsetpgrp(0, job_to_move->pid);
				waitpid(-1, &status, WUNTRACED | WCONTINUED);
				tcsetpgrp(0, getpid());
			}

			continue;
		}

		/* parse arguments if needed*/
		parse_cmd();
		/* now exec list of commands */
		exec_cmd();

		if (bck == 0) {
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
