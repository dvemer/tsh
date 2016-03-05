#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "list.h"
#include "sh.h"
#include "common.h"

/* check last symbol for '&' and replace it */
static int check_bck(char *s)
{
	ssize_t len;

	len = strlen(s) - 1;

	while (len > -1) {
		if (s[len] == '&') {
			/* trim command */
			s[len] = '\0';
			return 1;
		}

		if (s[len] != ' ')
			break;
		len--;
	}

	return 0;
}

static void add_job_task(struct job *job)
{
}

static void add_task_item(struct task **task_ptr, char *str)
{
	if (*task_ptr == NULL) {
		struct task *tmp;

		tmp = malloc(sizeof *tmp);
		ASSERT_ERR("malloc failed\n", (tmp == NULL));
		memset(tmp, 0, sizeof *tmp);
		tmp->name = calloc(1, strlen(str) + 1);
		ASSERT_ERR("calloc failed\n", (tmp->name == NULL));
		strcpy(tmp->name, str);
		tmp->argc = 1;
		tmp->args[0] = tmp->name;
#ifdef	DEBUG
		printf("set name:%s\n", str);
#endif
		*task_ptr = tmp;
	} else {
		struct task *tmp;

		tmp = *task_ptr;
		ASSERT_ERR("too many args\n", (tmp->argc == MAX_ARGS));
		tmp->args[tmp->argc] = str;
#ifdef	DEBUG
		printf("set arg:%s\n", str);
#endif
		tmp->argc++;
	}
}

static struct task *parse_command(char *command)
{
        char *t;
	struct task *new_task;

        t = command;
	new_task = NULL;

        while (*t != '\0') {
                if (*t != ' ') {
                        char *space;

                        space = strchr(t, ' ');

                        if (space != NULL) {
                                *space = '\0';
#ifdef	DEBUG
                                printf("cmd elem:#%s#\n", t);
#endif
				add_task_item(&new_task, t);
                        } else {
#ifdef	DEBUG
                                printf("last:#%s#\n", t);
#endif
				add_task_item(&new_task, t);
                                break;
                        }
                        t = space;
			t++;
                        continue;
                }
                t++;
        }

	return new_task;
}

static struct job *create_job(char *s)
{
	struct job *new_job;

	new_job = calloc(sizeof *new_job, 1);

	if (new_job == NULL)
		return NULL;

	new_job->name = calloc(strlen(s) + 1, 1);

	if (new_job == NULL)
		return NULL;

	strcpy(new_job->name, s);
	new_job->tasks.prev = &new_job->tasks;
	new_job->tasks.next = &new_job->tasks;
	return new_job;
}

struct job *parse(char *s, int *bck)
{
        char *t;
        char *p;
	struct job *new_job;
	int idx;
	struct task *new_task;
	int pipes_num;

	pipes_num = 0;
	new_job = create_job(s);

	if (new_job == NULL) {
		fprintf(stderr, "Failed to alloc memory!\n");
		exit(1);
	}

        t = &s[0];
        p = t;
	*bck = check_bck(s);
	new_job->bckg = *bck;
	idx = 0;

        while (1) {

                if ((*t == '|') || (*t == '\0')) {
                        char tmp;

                        tmp = *t;
                        *t = '\0';
                        new_task = parse_command(p);
			new_task->idx = idx;
			new_task->is_last = 0;
			idx++;

			if (*bck)
				new_task->flag = BG;
			else
				new_task->flag = FG;

			list_add_tail(&new_task->next, &new_job->tasks);

                        if (tmp == '|') {
				pipes_num++;

				/* dirty hack:disable background for chains */
				*bck = 0;

                                t++;
                                p = t;
                                continue;
                        } else
                                break;
                }

		/* NI */
                if (0 && (*t == '>')) {
                        char *end;

                        end = strchr(t, ';');

                        if (end != NULL)
                                *end = '\0';
                }

                t++;
        };

	if (pipes_num == 0) {
		struct task *task;

		task = get_elem(new_job->tasks.next, struct task, next);
		task->idx = -1;
	}

	if (new_task)
		new_task->is_last = 1;

	new_job->tasks_num = pipes_num + 1;

	return new_job;
}
