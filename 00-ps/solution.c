#include <solution.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdbool.h>
#include <ctype.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>

#define SIZE_MAX 4096
#define LENGTH_MAX 4096

bool IsPid(char *name)
{
	for( int i = 0; name[i] != '\0'; i ++ ) {
		if( !isdigit(name[i]) ) return false;
	}
	return true;
}

int ReadVars(char *filename, char **mas, int *num)
{
	char buffer[LENGTH_MAX] = {0};
    int fd = open(filename, O_RDONLY);
	if ( fd < 0 ) return false;

    ssize_t size_read = read(fd, buffer, LENGTH_MAX * sizeof(char));
	if ( size_read < 0 ) return false;

    int arg_num = 0;
	int prev_pos = 0;
    for( int pos = 0; pos < size_read; pos ++) {
        if (*(buffer + pos) == '\0') {
            mas[arg_num] = (char*) calloc( 1, pos + 1 - prev_pos );
			strncpy(mas[arg_num], buffer + prev_pos, pos + 1 - prev_pos);
			prev_pos = pos + 1;
            arg_num++;
        }
    }
	mas[arg_num-1] = NULL;
	*num = arg_num;

	return true;
}

void ps(void)
{
	DIR* p_proc;
	char sym_path_exe[SIZE_MAX] = {0}, path_exe[SIZE_MAX] = {0}, path_cmdline[SIZE_MAX] = {0}, path_environ[SIZE_MAX] = {0};

    if ((p_proc = opendir("/proc/")) == NULL) {
        report_error("/proc/", errno);
        return;
    }
	struct dirent* p_dirent;

	char **argv = (char **)calloc(sizeof(char *), LENGTH_MAX * sizeof(char *));
    char **environ = (char **)calloc(sizeof(char *), LENGTH_MAX * sizeof(char *));

	while ((p_dirent = readdir(p_proc)) != NULL) {
		int argc, num_env;

		if ( !IsPid(p_dirent->d_name) ) {
			continue;
		}

		sprintf(sym_path_exe, "/proc/%s/exe", p_dirent->d_name);
		if ( readlink(sym_path_exe, path_exe, SIZE_MAX) == -1 ) {
            report_error(sym_path_exe, errno);
            continue;
        }

		sprintf(path_cmdline, "/proc/%s/cmdline", p_dirent->d_name);
		if ( !ReadVars(path_cmdline, argv, &argc) ) {
			report_error(path_cmdline, errno);
			continue;
		}

		sprintf(path_environ, "/proc/%s/environ", p_dirent->d_name);
		if ( !ReadVars(path_environ, environ, &num_env) ) {
			report_error(path_environ, errno);
			continue;
		}

		report_process(atoi(p_dirent->d_name), path_exe, argv, environ);

		for (int i = 0; i < argc; i ++) free(argv[i]);
		for (int i = 0; i < num_env; i ++) free(environ[i]);
	}
	closedir(p_proc);

	free(argv);
    free(environ);
}