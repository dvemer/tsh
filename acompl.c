/* auto completion */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include "sh.h"
#include "common.h"

#define	ALLOC_SZ	50
static const char **bins_names_ptrs;
static int columns;
extern struct builtin_ent builtins[];
extern int builtins_num;
static int ac_ent_num;

static void scan_directory(const char *path)
{
	DIR *dir;
	struct dirent *ent;

	dir = opendir(path);

	if (dir == NULL)
		return;

	while ((ent = readdir(dir)) != NULL) {
	};
}

static void sort_builtins(void)
{
	int i;
	int j;

	for(j = 0;j < builtins_num - 1;j++) {
		for(i = 0;i < builtins_num - j - 1;i++) {
			int res;

			res = strcmp(bins_names_ptrs[i], bins_names_ptrs[i + 1]);

			if (res > 0) {
				const char *tmp;

				tmp = bins_names_ptrs[i];
				bins_names_ptrs[i] = bins_names_ptrs[i + 1];
				bins_names_ptrs[i + 1] = tmp;
			}
		}
	}
}

static void init_builtins(void)
{
	size_t sz;
	int i;

	sz = builtins_num * (sizeof *bins_names_ptrs);
	bins_names_ptrs = malloc(sz);
	ASSERT_ERR("malloc failed\n", (bins_names_ptrs == NULL));
	ac_ent_num = builtins_num;

	for(i = 0;i < builtins_num;i++)
		bins_names_ptrs[i] = builtins[i].name;

	sort_builtins();
}

void init_autoc(void)
{
	init_builtins();
}
/*
 * main entry to auto completion:
 * prints all possible commands according template.
 */
void print_ac(const char *str_template)
{
	int i;

	printf("\n");
	for(i = 0;i < ac_ent_num;i++) {
		if (strstr(bins_names_ptrs[i], str_template) ==
		    bins_names_ptrs[i])
			printf("%s\n", bins_names_ptrs[i]);
	}
}
