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
        char *p;
        int cmd_flag;
	int argc;
	struct task *new_task;

        t = command;
        p = t;
        cmd_flag = 0;
	argc = 0;
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

void parse(char *s, struct list_head *tasks, int *pipes_num, int *bck)
{
        int i;
        char *t;
        char *p;

        i = 0;
        t = &s[0];
        p = t;
	*bck = check_bck(s);

        while (1) {

                if ((*t == '|') || (*t == '\0')) {
                        char tmp;
			struct task *new_task;

                        tmp = *t;
                        *t = '\0';
                        new_task = parse_command(p);

			if (*bck)
				new_task->flag = BG;
			else
				new_task->flag = FG;

			list_add_tail(&new_task->next, tasks);

                        if (tmp == '|') {
				(*pipes_num)++;

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

	return;
}
