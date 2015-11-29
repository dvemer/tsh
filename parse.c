#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "list.h"
#include "sh.h"

/* check last symbol for '&' and replace it */
static int check_bck(char *s)
{
	size_t len;
	int i;

	len = strlen(s);

	if (len == 1)
		return 0;

	i = len - 1;

	/* skip all spaces at the end of line */
	while (s[i] == ' ') i--;

	if (s[i] == '&') {
		s[i] = ' ';
		return 1;
	}

	return 0;
}

void parse(char *s, struct list_head *tasks, int *pipes_num, int *bck)
{
	char *p;
	char *p1;
	int prev_space;
	int prev_pipe;
	int name;
	struct task *task;
	int argcnt;

	*bck = check_bck(s);
#ifdef DEBUG
	printf("************************\n");
	printf("%s\n", s);
	printf("************************\n");
#endif
	p = &s[0];
	p1 = p;
	prev_space = 1;
	/* expression cannot begin with pipe */
	prev_pipe = 1;
	name = 0;
	argcnt = 1;
	task = NULL;

	while(1) {
		if (*p1 == '\0') {
			if (name == 0) {
#ifdef DEBUG
				printf("end name: ");
#endif
				if (task == NULL)
					task = malloc (sizeof *task);

				task->name = p;
				task->args[0] = p;
				name = 1;
			} else {
#ifdef DEBUG
				printf("end arg: ");
#endif
				task->args[argcnt] = p;
				argcnt++;
			}
			list_add_tail(&task->next, tasks);
#ifdef DEBUG
			printf("end:%s#\n", task->name);
#endif
			break;
		}

		if ((*p1 == ' ') || (*p1 == '|')) {
			/* space found */
			if (*p1 == ' ') {
				if (prev_space == 1) {
					/* previous symbol was space, ignore */
					p1++;
					p = p1;
					continue;
				} else
					prev_space = 1;
			}

			/* pipe delimiter */
			if (*p1 == '|') {
				if (prev_pipe == 1) {
					/* previous symbols was pipe, fail */
					printf("syntax error!\n");
					exit(1);
				}

				/* ok, remember pipe symbol */
				prev_pipe = 1;
			} else {
				/* space delimiter */
				prev_pipe = 0;
			}


			/* split line */
			*p1 = '\0';
			/* print lexem */
			if (strlen(p) != 0) {
				if (name == 0) {
					if (task == NULL) {
						task = malloc(sizeof *task);

						if (task == NULL) {
							printf("malloc failed!\n");
							exit(1);
						}

						task->name = p;
						task->args[0] = p;
					}
#ifdef DEBUG
					printf("name: ");
#endif
					name = 1;
				} else {
#ifdef DEBUG
					printf("arg: ");
#endif
					task->args[argcnt] = p;
					argcnt++;
				}
#ifdef DEBUG
				printf("%s", p);
				printf("%c\n", '#');
#endif
			} else {
#ifdef DEBUG
				printf("pipe!\n");
#endif
				list_add_tail(&task->next, tasks);
				name = 0;
				argcnt = 1;
				task = NULL;
				(*pipes_num)++;
			}

			p1++;
			p = p1;

			continue;
		}

		/* symbol found, reset flags */
		prev_space = 0;
		prev_pipe = 0;

		p1++;
	};
}
