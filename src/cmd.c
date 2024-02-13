// SPDX-License-Identifier: BSD-3-Clause

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#include "cmd.h"
#include "utils.h"

#define READ		0
#define WRITE		1


void redirect_input(word_t *in)
{
	// get filename
	char *filename = get_word(in);

	// open the file
	int fd = open(filename, O_RDONLY);

	// check if the file was opened successfully
	DIE(fd == -1, "error opening file\n");

	// we don't need the filename anymore, so we can free the memory
	free(filename);

	// redirect stdin to the file
	int rc = dup2(fd, STDIN_FILENO);

	// check if the redirection was successful
	DIE(rc == -1, "dup2 failed\n");

	// close the file
	rc = close(fd);

	fflush(stdin);
}

static void redirect_output(word_t *out, int io_flags)
{
	// get filename
	char *filename = get_word(out);

	// identify the mode in which we have to open the file
	int fd;

	if (io_flags == IO_OUT_APPEND) {
		// open the file in append mode
		fd = open(filename, O_WRONLY | O_CREAT | O_APPEND, 0644);
	} else {
		// open file in regular mode (no append)
		fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	}

	// check if the file was opened successfully
	DIE(fd == -1, "error opening file\n");

	// we don't need the filename anymore, so we can free the memory
	free(filename);

	// redirect stdout to the file
	int rc = dup2(fd, STDOUT_FILENO);

	// check if the redirection was successful
	DIE(rc == -1, "dup2 failed\n");

	// close the file
	rc = close(fd);
	fflush(stdout);
}

static void redirect_error(word_t *err, int io_flags)
{
	// get filename
	char *filename = get_word(err);

	// identify the mode in which we have to open the file
	int fd;

	if (io_flags == IO_ERR_APPEND) {
		// open the file in append mode
		fd = open(filename, O_WRONLY | O_CREAT | O_APPEND, 0644);
	} else {
		// open file in regular mode (no append)
		fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	}

	// check if the file was opened successfully
	DIE(fd == -1, "error opening file\n");

	// we don't need the filename anymore, so we can free the memory
	free(filename);

	// redirect stderr to the file
	int rc = dup2(fd, STDERR_FILENO);

	// check if the redirection was successful
	DIE(rc == -1, "dup2 failed\n");

	// close the file
	rc = close(fd);
	fflush(stderr);
}

static void redirect_both_out_and_err(simple_command_t *s)
{
	// get filename for stdout
	char *filename_out = get_word(s->out);

	// open the output file in append mode
	int fd_out = open(filename_out, O_WRONLY | O_CREAT | O_APPEND, 0644);
	// check if the file was opened successfully
	DIE(fd_out == -1, "failed to open output file\n");

	// we don't need the filename anymore, so we can free the memory
	free(filename_out);


	// get filename for stderr
	char *filename_err = get_word(s->err);

	// open the error file
	int fd_err = open(filename_err, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	// check if the file was opened successfully
	DIE(fd_err == -1, "failed to open error file\n");

	// we don't need the filename anymore, so we can free the memory
	free(filename_err);

	// redirect stdout and stderr to the files
	int rc_out = dup2(fd_out, STDOUT_FILENO);
	// check if the output redirection was successful
	DIE(rc_out == -1, "dup2 failed\n");

	int rc_err = dup2(fd_err, STDERR_FILENO);
	// check if the error redirection was successful
	DIE(rc_err == -1, "dup2 failed\n");

	// close files and free memory
	close(fd_out);
	close(fd_err);

	fflush(stdout);
	fflush(stderr);
}


static void perform_redirections(simple_command_t *s)
{
	if (s->in != NULL)
		redirect_input(s->in);

	if (s->out != NULL) {
		if (s->err == NULL) {
			// only redirect output
			redirect_output(s->out, s->io_flags);
			return;
		}

		//if we are here, it means that we have to redirect both stdout and stderr
		redirect_both_out_and_err(s);
		return;
	}

	if (s->err != NULL) {
		// we only need to redirect error
		redirect_error(s->err, s->io_flags);
		return;
	}
}

/**
 * Internal change-directory command.
 */
static bool shell_cd(word_t *dir)
{
	// get the directory name
	char *dir_name = get_word(dir);

	// execute the cd command
	int return_value = chdir(dir_name);

	free(dir_name);
	return return_value;
}

static bool complete_cd_command(simple_command_t *s)
{
	perform_redirections(s);

	return shell_cd(s->params);
}

/**
 * Internal exit/quit command.
 */
static int shell_exit(void)
{
	return SHELL_EXIT;
}

static int execute_external_command(simple_command_t *s)
{
	pid_t pid = fork();

	// check if the fork failed
	if (pid == -1)
		return 0;

	//  check if we are in the child process
	if (pid == 0) {
		//2c. Perform redirections in child
		perform_redirections(s);

		// 3c. Load executable in child
		int argc;
		char **argv = get_argv(s, &argc);

		if (execvp(argv[0], argv) == -1) {
			// execution failed
			exit(0);
		}

		// free memory
		for (int i = 0; i < argc; i++)
			free(argv[i]);

		free(argv);

	} else {
		// we are in the parent process

		// 2. Wait for child
		int status;

		waitpid(pid, &status, 0);

		// 3. Return exit status
		return WEXITSTATUS(status);
	}
	return 0;
}

static int assign_environment_variable(simple_command_t *s)
{
	// check if the next part is "=" and if there is another next part after that
	if ((strcmp(s->verb->next_part->string, "=") != 0) || (s->verb->next_part->next_part == NULL))
		return 0; // invalid command

	// now that we are sure the syntax is correct, we can assign the variable
	const char *var_name = s->verb->string;
	char *var_value = get_word(s->verb->next_part->next_part);

	int return_value = setenv(var_name, var_value, 1);

	free(var_value);
	return return_value;
}


/**
 * Parse a simple command (internal, environment variable assignment,
 * external command).
 */
static int parse_simple(simple_command_t *s, int level, command_t *father)
{
	/* TODO: Sanity checks. */
	if (!s)
		return 0;

	// check if the command is builtin
	if (strcmp(s->verb->string, "exit") == 0)
		return shell_exit();

	if (strcmp(s->verb->string, "quit") == 0)
		return shell_exit();

	if (strcmp(s->verb->string, "cd") == 0)
		return complete_cd_command(s);

	// check if the command is an environment variable assignment
	if (s->verb->next_part != NULL) {
		return assign_environment_variable(s);
	}

	/* TODO: If external command:
	 *   1. Fork new process
	 *     2c. Perform redirections in child
	 *     3c. Load executable in child
	 *   2. Wait for child
	 *   3. Return exit status
	 */

	// if we reached this point, it means that this is an external command
	return execute_external_command(s);
}

/**
 * Process two commands in parallel, by creating two children.
 */
static bool run_in_parallel(command_t *cmd1, command_t *cmd2, int level,
		command_t *father)
{
	// the logic is similar to the one used in the run_on_pipe function,
	// but the commands are executed independently, not in pipe

	// fork the first process
	pid_t first_pid = fork();

	// check if the fork failed
	if (first_pid == -1)
		return false;

	//  check if we are in the child process
	if (first_pid == 0) {
		// execute the first command using the dedicated function
		exit(parse_command(cmd1, level + 1, father));
	}

	// fork the second process
	pid_t second_pid = fork();

	// check if the fork failed
	if (second_pid == -1)
		return false;

	//  check if we are in the child process
	if (second_pid == 0) {
		// execute the second command using the dedicated function
		exit(parse_command(cmd2, level + 1, father));
	}

	// wait for the two processes to finish
	int first_status, second_status;

	waitpid(first_pid, &first_status, 0);
	waitpid(second_pid, &second_status, 0);

	// both processes must have terminated with success in order
	// for the parallel command to be successful

	if (WEXITSTATUS(first_status) != 0)
		return false;

	if (WEXITSTATUS(second_status) != 0)
		return false;

	// if we reached this point, it means that both processes
	// terminated with success
	return true;
}

/**
 * Run commands by creating an anonymous pipe (cmd1 | cmd2).
 */
static bool run_on_pipe(command_t *cmd1, command_t *cmd2, int level,
		command_t *father)
{
	// inspiration from this video: https://youtu.be/Mqb2dVRe0uo?feature=shared

	// declare filedescriptors array
	// pipefds[0] = the reading end of the pipe
	// pipefds[1] = the writing end of the pipe
	int pipefds[2];

	// check if the pipe was created successfully
	if (pipe(pipefds) == -1)
		return false; // pipe failed

	/*
	* the output of the first command will no loger be displayed at stdout,
	* but it will be written to the pipe. The second command will read its input
	* from the pipe, instead of stdin and this input will be the output from the
	* first command
	*/

	// create the two processes

	// fork the first process
	pid_t first_pid = fork();

	// check if the fork failed
	if (first_pid == -1)
		return false;

	//  check if we are in the child process
	if (first_pid == 0) {
		/*
		* we need to redirect the standard output of the first command
		* to the writing end of the pipe
		*/
		int rc = dup2(pipefds[1], STDOUT_FILENO);
		// check if the redirection was successful
		DIE(rc == -1, "dup2 failed\n");

		/*
		* dup2 has created a new filedescriptor for STDOUT, so now
		* we have two filedescriptors => we can close the old one
		*/
		close(pipefds[1]);

		// we do not use the reading end of the pipe, so we can close it
		close(pipefds[0]);

		// execute the first command using the dedicated function
		exit(parse_command(cmd1, level + 1, father));
	}

	// fork the second process
	pid_t second_pid = fork();

	// check if the fork failed
	if (second_pid == -1)
		return false;

	//  check if we are in the child process
	if (second_pid == 0) {
		// the second command will read its input from the pipe, instead of stdin
		int rc = dup2(pipefds[0], STDIN_FILENO);
		// check if the redirection was successful
		DIE(rc == -1, "dup2 failed\n");

		/*
		* dup2 has created a new filedescriptor for stdin, so now
		* we have two filedescriptors => we can close the old one
		*/
		close(pipefds[0]);

		// we do not use the writing end of the pipe, so we can close it
		close(pipefds[1]);

		// execute the second command using the dedicated function
		exit(parse_command(cmd2, level + 1, father));
	}

	/*
	* the parent process still has the two filedescriptors open, so we
	* need to close them so that the second command knows when to stop
	* expecting input
	*/
	close(pipefds[0]);
	close(pipefds[1]);

	// wait for the two processes to finish
	int status;

	waitpid(first_pid, NULL, 0);
	waitpid(second_pid, &status, 0);

	return WEXITSTATUS(status);
}

/**
 * Parse and execute a command.
 */
int parse_command(command_t *c, int level, command_t *father)
{
	int exit_code;

	if (c->op == OP_NONE) {
		return parse_simple(c->scmd, level, father);
	}

	switch (c->op) {
	case OP_SEQUENTIAL:
		parse_command(c->cmd1, level + 1, c);
	 	exit_code = parse_command(c->cmd2, level + 1, c);
		break;

	case OP_PARALLEL:
		exit_code = run_in_parallel(c->cmd1, c->cmd2, level + 1, c);
		break;

	case OP_CONDITIONAL_NZERO:
		exit_code = parse_command(c->cmd1, level + 1, c);
		if (exit_code != 0)
			exit_code = parse_command(c->cmd2, level + 1, c);

		break;

	case OP_CONDITIONAL_ZERO:
		exit_code = parse_command(c->cmd1, level + 1, c);
		if (exit_code == 0)
			exit_code = parse_command(c->cmd2, level + 1, c);
		break;

	case OP_PIPE:
		exit_code = run_on_pipe(c->cmd1, c->cmd2, level + 1, c);
		break;

	default:
		return SHELL_EXIT;
	}

	return exit_code;
}
