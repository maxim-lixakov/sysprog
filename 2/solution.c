#include "parser.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

struct ExecutionResult {
    int exitCode;
    int forceExitCode;
};

static struct ExecutionResult execute_command_line(const struct command_line *line) {
    assert(line != NULL);

    int exitCode = 0;
    int pipefd[2], last_fd = -1;
    struct ExecutionResult execResult = {-1, -1};

    const struct expr *e = line->head;
    while (e != NULL) {
        if (e->type == EXPR_TYPE_COMMAND) {
            if (e->next && e->next->type == EXPR_TYPE_PIPE) {
                if (pipe(pipefd) == -1) {
                    perror("pipe");
                    exit(EXIT_FAILURE);
                }
            }

            pid_t pid = fork();
            if (pid == -1) {
                perror("fork");
                exit(EXIT_FAILURE);
            } else if (pid == 0) {
                if (last_fd != -1) {
                    dup2(last_fd, STDIN_FILENO);
                    close(last_fd);
                }

                if (e->next && e->next->type == EXPR_TYPE_PIPE) {
                    close(pipefd[0]);
                    dup2(pipefd[1], STDOUT_FILENO);
                    close(pipefd[1]);
                } else {
                    if (line->out_type == OUTPUT_TYPE_FILE_NEW) {
                        int out_fd = open(line->out_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                        dup2(out_fd, STDOUT_FILENO);
                        close(out_fd);
                    } else if (line->out_type == OUTPUT_TYPE_FILE_APPEND) {
                        int out_fd = open(line->out_file, O_WRONLY | O_CREAT | O_APPEND, 0644);
                        dup2(out_fd, STDOUT_FILENO);
                        close(out_fd);
                    }
                }

                char *args[e->cmd.arg_count + 2];
                args[0] = e->cmd.exe;
                for (uint32_t i = 0; i < e->cmd.arg_count; ++i) {
                    args[i + 1] = e->cmd.args[i];
                }
                args[e->cmd.arg_count + 1] = NULL;

                execvp(args[0], args);
                perror("execvp");
                exit(EXIT_FAILURE);
            } else {
                if (last_fd != -1) {
                    close(last_fd);
                }
                if (e->next && e->next->type == EXPR_TYPE_PIPE) {
                    last_fd = pipefd[0];
                    close(pipefd[1]);
                } else {
                    last_fd = -1;
                }
                waitpid(pid, &exitCode, 0);
                execResult.exitCode = WEXITSTATUS(exitCode);
            }
        }
        e = e->next;
    }

    if (last_fd != -1) {
        close(last_fd);
    }

    return execResult;
}

int main(void) {
    struct parser *p = parser_new();
    char buf[1024];
    int rc;

    while ((rc = read(STDIN_FILENO, buf, sizeof(buf))) > 0) {
        parser_feed(p, buf, rc);
        struct command_line *line = NULL;
        while (true) {
            enum parser_error err = parser_pop_next(p, &line);
            if (err == PARSER_ERR_NONE && line == NULL) break;
            if (err != PARSER_ERR_NONE) {
                printf("Error: %d\n", (int)err);
                continue;
            }
            struct ExecutionResult execResult = execute_command_line(line);
            command_line_delete(line);
            if (execResult.forceExitCode != -1) {
                parser_delete(p);
                return execResult.forceExitCode;
            }
        }
    }
    parser_delete(p);
    return 0;
}
