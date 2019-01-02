#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>

#include "tokenizer.h"

/* Convenience macro to silence compiler warnings about unused function parameters. */
#define unused __attribute__((unused))

/* Whether the shell is connected to an actual terminal or not. */
bool shell_is_interactive;

/* File descriptor for the shell input */
int shell_terminal;

/* Terminal mode settings for the shell */
struct termios shell_tmodes;

/* Process group id for the shell */
pid_t shell_pgid;

/* Count for the backGround process */
int background_process_count = 0;

int cmd_wait(struct tokens *tokens);
int cmd_cd(struct tokens *tokens);
int cmd_pwd(struct tokens *tokens);
int cmd_exit(struct tokens *tokens);
int cmd_help(struct tokens *tokens);

/* Built-in command functions take token array (see parse.h) and return int */
typedef int cmd_fun_t(struct tokens *tokens);

/* Built-in command struct and lookup table */
typedef struct fun_desc {
  cmd_fun_t *fun;
  char *cmd;
  char *doc;
} fun_desc_t;

fun_desc_t cmd_table[] = {
  {cmd_help, "?", "show this help menu"},
  {cmd_exit, "exit", "exit the command shell"},
  {cmd_cd, "cd", "change the directory"},
  {cmd_pwd, "pwd", "show the current directory"},
  {cmd_wait, "wait", "waits until all background jobs have terminated before returning to the prompt"},
};

/* Wait until all background jobs have terminated. */
int cmd_wait(unused struct tokens *tokens) {
    for (size_t i = 0; i < background_process_count; ++i) {
        waitpid(-1, NULL, 0);
    }
    background_process_count = 0;
    printf("Finish wait.\n");
    return 1;
}

/* Print the current directory. */
int cmd_pwd(unused struct tokens *tokens) {
  char buff[1024];
  if (getcwd(buff, sizeof(buff)) != NULL) {
    printf("%s\n", buff);
  } else {
    perror("cmd_pwd error");
  }
  return 1;
}

/* Change the current directory */
int cmd_cd(unused struct tokens *tokens) {
  if (chdir(tokens_get_token(tokens, 1)) == -1) {
    perror("cmd_cd error");
  }
  return 1;
}

/* Prints a helpful description for the given command */
int cmd_help(unused struct tokens *tokens) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    printf("%s - %s\n", cmd_table[i].cmd, cmd_table[i].doc);
  return 1;
}

/* Exits this shell */
int cmd_exit(unused struct tokens *tokens) {
  exit(0);
}

/* Looks up the built-in command, if it exists. */
int lookup(char cmd[]) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    if (cmd && (strcmp(cmd_table[i].cmd, cmd) == 0))
      return i;
  return -1;
}

/* Intialization procedures for this shell */
void init_shell() {
  /* Our shell is connected to standard input. */
  shell_terminal = STDIN_FILENO;

  /* Check if we are running interactively */
  shell_is_interactive = isatty(shell_terminal);

  if (shell_is_interactive) {
    /* If the shell is not currently in the foreground, we must pause the shell until it becomes a
     * foreground process. We use SIGTTIN to pause the shell. When the shell gets moved to the
     * foreground, we'll receive a SIGCONT. */
    while (tcgetpgrp(shell_terminal) != (shell_pgid = getpgrp()))
      kill(-shell_pgid, SIGTTIN);

    /* Saves the shell's process id */
    shell_pgid = getpid();

    /* Take control of the terminal */
    tcsetpgrp(shell_terminal, shell_pgid);

    /* Save the current termios to a variable, so it can be restored later. */
    tcgetattr(shell_terminal, &shell_tmodes);
  }
}

int main(unused int argc, unused char *argv[]) {
  init_shell();

  static char line[4096];
  int line_num = 0;

  /* Please only print shell prompts when standard input is not a tty */
  if (shell_is_interactive)
    fprintf(stdout, "%d: ", line_num);

  while (fgets(line, 4096, stdin)) {
    /* Split our line into words. */
    struct tokens *tokens = tokenize(line);

    /* Find which built-in function to run. */
    int fundex = lookup(tokens_get_token(tokens, 0));

    int background = 0;

    if (tokens_get_length(tokens) > 1) {
        if (strcmp("&", tokens_get_token(tokens, tokens_get_length(tokens) - 1)) == 0) {
            background = 1;
            background_process_count++;
        }
    }

    // Ignore all signals here for the background process.
    signal(SIGINT,SIG_IGN);
    signal(SIGQUIT,SIG_IGN);
    signal(SIGTSTP,SIG_IGN);
    signal(SIGTTIN,SIG_IGN);
    signal(SIGTTOU,SIG_IGN);
    signal(SIGCHLD,SIG_IGN);
    signal(SIGCONT,SIG_IGN);
    signal(SIGKILL,SIG_IGN);

    if (fundex >= 0) {
      cmd_table[fundex].fun(tokens);
    } else {
      /* REPLACE this to run commands as programs. */
      pid_t child = fork();
      if (child > 0) {
        // In the parent process.
        int status;
        if (!background) {
            wait(&status);
        }
      } else if (child == 0) {
        // In the child process.
        pid_t cpid = getpid();
        // Ensure that each program you start is in its own process group.
        setpgid(0, cpid);
        // Its process group should be placed in the foreground.
        if (!background) {
            tcsetpgrp(shell_terminal, cpid);
        }
        // Catch all signals.
        signal(SIGINT,SIG_DFL);
        signal(SIGQUIT,SIG_DFL);
        signal(SIGTSTP,SIG_DFL);
        signal(SIGTTIN,SIG_DFL);
        signal(SIGTTOU,SIG_DFL);
        signal(SIGCHLD,SIG_DFL);
        signal(SIGCONT,SIG_DFL);
        signal(SIGKILL,SIG_DFL);
        size_t len = tokens_get_length(tokens);
        int redir = 1;
        if (len > 2) {
          if (strcmp(">", tokens_get_token(tokens, len - 2)) == 0) {
            int file_des = open(tokens_get_token(tokens, len - 1), O_RDWR | O_CREAT, S_IRWXU);
            dup2(file_des, 1);
            close(file_des);
            redir = 1;
          } else if (strcmp("<", tokens_get_token(tokens, len - 2)) == 0) {
            int file_des = open(tokens_get_token(tokens, len - 1), O_RDWR | O_CREAT, S_IRWXU);
            dup2(file_des, 0);
            close(file_des);
            redir = 1;
          } else {
            redir = 0;
          }
        } else {
          redir = 0;
        }
        int len_minus = 0;
        if (background) {
            len_minus = 1;
        }
        if (redir) {
            len_minus = 2;
        }
        len -= len_minus;
        char *args[len + 1];
        for (size_t i = 0; i < len; ++i) {
          args[i] = tokens_get_token(tokens, i);
        }
        args[len] = NULL;

        // Add support for path resolution.
        if (execv(tokens_get_token(tokens, 0), args) == -1) {
          char *variable_path = getenv("PATH");
          char *token_path = strtok(variable_path, ":");
          char path[1024];
          while (token_path) {
            strcpy(path, token_path);
            strcat(path, "/");
            strcat(path, tokens_get_token(tokens, 0));
            if (execv(path, args) == -1) {
              token_path = strtok(NULL, ":");
            } else {
              break;
            }
          }
        }
      } else if (child < 0) {
          perror("fork error");
          return 1;
      }
    }
    // Restore the shell to the foreground.
    tcsetpgrp(shell_terminal, shell_pgid);
    // Restore the shell to handle the signal.
    signal(SIGINT,SIG_DFL);
    signal(SIGQUIT,SIG_DFL);
    signal(SIGTSTP,SIG_DFL);
    signal(SIGTTIN,SIG_DFL);
    signal(SIGTTOU,SIG_DFL);
    signal(SIGCHLD,SIG_DFL);
    signal(SIGCONT,SIG_DFL);
    signal(SIGKILL,SIG_DFL);

    fflush(stdout);
    fflush(stderr);
    fflush(stdin);
//    shell_is_interactive = isatty(shell_terminal);
    if (shell_is_interactive)
      /* Please only print shell prompts when standard input is not a tty */
      fprintf(stdout, "%d: ", ++line_num);

    /* Clean up memory */
    tokens_destroy(tokens);
  }

  return 0;
}
