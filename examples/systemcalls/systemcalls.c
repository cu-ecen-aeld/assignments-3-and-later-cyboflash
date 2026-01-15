#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

#include "systemcalls.h"


/**
 * @param cmd the command to execute with system()
 * @return true if the command in @param cmd was executed
 *   successfully using the system() call, false if an error occurred,
 *   either in invocation of the system() call, or if a non-zero return
 *   value was returned by the command issued in @param cmd.
*/
bool do_system(const char *cmd)
{

/*
 * TODO  add your code here
 *  Call the system() function with the command set in the cmd
 *   and return a boolean true if the system() call completed with success
 *   or false() if it returned a failure
*/
    int ret = system(cmd);
    if (ret == -1) {
        return false;
    }
    return WIFEXITED(ret) && WEXITSTATUS(ret) == 0;
}

/**
* @param count -The numbers of variables passed to the function. The variables are command to execute.
*   followed by arguments to pass to the command
*   Since exec() does not perform path expansion, the command to execute needs
*   to be an absolute path.
* @param ... - A list of 1 or more arguments after the @param count argument.
*   The first is always the full path to the command to execute with execv()
*   The remaining arguments are a list of arguments to pass to the command in execv()
* @return true if the command @param ... with arguments @param arguments were executed successfully
*   using the execv() call, false if an error occurred, either in invocation of the
*   fork, waitpid, or execv() command, or if a non-zero return value was returned
*   by the command issued in @param arguments with the specified arguments.
*/

bool do_exec(int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;

/*
 * TODO:
 *   Execute a system command by calling fork, execv(),
 *   and wait instead of system (see LSP page 161).
 *   Use the command[0] as the full path to the command to execute
 *   (first argument to execv), and use the remaining arguments
 *   as second argument to the execv() command.
 *
*/

    va_end(args);

    pid_t pid = fork();

    if (pid < 0) {
        // parent process code
        return false;
    }
    else if (0 == pid) {
        // child process code
        execv(command[0], command);
        exit(EXIT_FAILURE);
    } 
    else {
        // parent process code
        int status;
        if (-1 == waitpid(pid, &status, 0)) {
            return false;
        }
        else if (WIFEXITED(status)) {
            return 0 == WEXITSTATUS(status);
        }
    }

    return false;
}
/**
* @param outputfile - The full path to the file to write with command output.
*   This file will be closed at completion of the function call.
* All other parameters, see do_exec above
*/
bool do_exec_redirect(const char *outputfile, int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;

    va_end(args);

    pid_t pid = fork();

    if (pid < 0) {
        return false;
    }
    else if (0 == pid) {
        int fd = open(outputfile, O_WRONLY|O_CREAT|O_TRUNC, 0644);
        if (fd < 0) {
            exit(EXIT_FAILURE);
        }
        // redirect stdout to a file
        if (dup2(fd, STDOUT_FILENO) < 0) {
            exit(EXIT_FAILURE);
        }
        close(fd);
        execv(command[0], command);
        exit(EXIT_FAILURE);
    }
    else {
        int status;
        if (-1 == waitpid(pid, &status, 0)) {
            return false;
        }
        else if (WIFEXITED(status)) {
            return WEXITSTATUS(status) == 0;
        }
    }

    return false;
}
