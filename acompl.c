/* auto completion */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "sh.h"
#include "common.h"

#define	ALLOC_SZ	50
static char **bins_names_ptrs;
static int columns;
extern struct builtin_ent builtins[];
extern int builtins_num;
static int ac_ent_num;
static int ac_ent_total;
static int ac_print_max;

static char *get_full_name(const char *path, const char *name)
{
	char *full_path;

	full_path = malloc(strlen(path) + strlen(name) + 2);
	ASSERT_ERR("malloc failed\n", (full_path == NULL));
	strcpy(full_path, path);
	strcat(full_path, "/");
	strcat(full_path, name);
	return full_path;
}

static void add_new_binary(char *binary_name)
{
	if (ac_ent_num == ac_ent_total) {
		ac_ent_total += ALLOC_SZ;
		bins_names_ptrs = realloc(bins_names_ptrs, ac_ent_total * (sizeof *bins_names_ptrs));
		ASSERT_ERR("realloc failed\n", (bins_names_ptrs == NULL));
	}

	bins_names_ptrs[ac_ent_num] = malloc(strlen(binary_name) + 1);
	ASSERT_ERR("malloc failed\n", (bins_names_ptrs[ac_ent_num] == NULL));
	strcpy(bins_names_ptrs[ac_ent_num], binary_name);
	ac_ent_num++;
}

static void scan_directory(const char *path)
{
	DIR *dir;
	struct dirent *ent;

	dir = opendir(path);

	if (dir == NULL)
		return;

	while ((ent = readdir(dir)) != NULL) {
		char *full_path;
		struct stat stat_info;

		full_path = get_full_name(path, ent->d_name);
		if (stat(full_path, &stat_info) == 0) {
			/* file is executable */
			if (!(stat_info.st_mode & S_IXUSR))
				continue;

			if (strcmp(ent->d_name, ".") == 0)
				continue;

			if (strcmp(ent->d_name, "..") == 0)
				continue;

			add_new_binary(ent->d_name);
		}
	};
}

static char *scan_path_var(void)
{
	char *path_var;
	char *path;
	char *path_var_copy;

	path_var = getenv("PATH");

	if (path_var == NULL)
		return NULL;

	path_var_copy = strdup(path_var);
	path = strtok(path_var_copy, ":");

	while (path != NULL) {
		scan_directory(path);
		path = strtok(NULL, ":");
	};

	free(path_var_copy);
	return NULL;
}

static void sort_binaries(void)
{
	int i;
	int j;

	for(j = 0;j < ac_ent_num - 1;j++) {
		for(i = 0;i < ac_ent_num - j - 1;i++) {
			int res;

			res = strcmp(bins_names_ptrs[i], bins_names_ptrs[i + 1]);

			if (res > 0) {
				char *tmp;

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
	ac_ent_total = ac_ent_num;

	for(i = 0;i < builtins_num;i++)
		bins_names_ptrs[i] = builtins[i].name;
}

static void init_binaries(void)
{
	scan_path_var();
}

void init_autoc(void)
{
	ac_print_max = 50;
	init_builtins();
	init_binaries();
	sort_binaries();
}

static void print_command_ac(const char *str_template)
{
	int i;

	printf("\n");
	for(i = 0;i < ac_ent_num;i++) {
		if (strstr(bins_names_ptrs[i], str_template) ==
		    bins_names_ptrs[i])
			printf("%s\n", bins_names_ptrs[i]);
	}
}

static void print_dir_with_tmpl(const char *path, const char *template)
{
	DIR *dir;
	struct dirent *ent;

	dir = opendir(path);

	if (dir == NULL)
		return;

	printf("\n");
	while ((ent = readdir(dir)) != NULL) {
		if ((template == NULL) || strstr(ent->d_name, template) == &ent->d_name[0])
			printf("%s\n", ent->d_name);
	};
}

static void print_file_path_ac(char *path)
{
	char *last_dir;
	char *file_template;
	size_t file_name_len;
	struct stat stat_info;

	if (strcmp(path, ".") == 0) {
		print_dir_with_tmpl(".", NULL);
		return;
	}

	last_dir = strrchr(path, '/');

	if (last_dir != NULL) {
		file_name_len = strlen(last_dir + 1);
		file_template = malloc(file_name_len);
		ASSERT_ERR("malloc failed!\n", (file_template == NULL));
		strcpy(file_template, last_dir + 1);
		*last_dir = '\0';
		if (stat(path, &stat_info) == 0) {
			/* path exists */
			print_dir_with_tmpl(path, file_template);
		}
	} else {
		file_name_len = strlen(path);
		file_template = path;
		print_dir_with_tmpl(".", file_template);
	}

	return;
}

/*
 * Command is no empty string, so print files.
 * Process last word as part of path.
 */
static void print_file_ac(const char *command)
{
	char *copy;
	char *tmp;
	char *prev;
	char *first;

	copy = strdup(command);
	first = tmp = strtok(copy, " ");

        while (tmp != NULL) {
		prev = tmp;
                tmp = strtok(NULL, " ");
        };

	if (strcmp(first, prev) == 0) {
		/* string is empty */
		print_file_path_ac(".");
	} else {
		print_file_path_ac(prev);
	}

	free(copy);
	return;
}

/*
 * main entry to auto completion:
 * prints all possible commands according template.
 */
void print_ac(const char *str_template)
{
	if (string_is_empty(str_template))
		print_command_ac(str_template);
	else
		print_file_ac(str_template);
}
