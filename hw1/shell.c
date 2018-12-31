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
};


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

    if (fundex >= 0) {
      cmd_table[fundex].fun(tokens);
    } else {
      /* REPLACE this to run commands as programs. */
      pid_t child = fork();
      if (child > 0) {
        int status;
        wait(&status);
      } else if (child == 0) {
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
        if (redir) {
          len = len - 2;
        }
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


    if (shell_is_interactive)
      /* Please only print shell prompts when standard input is not a tty */
      fprintf(stdout, "%d: ", ++line_num);

    /* Clean up memory */
    tokens_destroy(tokens);
  }

  return 0;
}
