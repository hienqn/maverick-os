/* shell.c

   An improved command-line shell for Pintos.
   Features:
     - Command history (up/down arrows)
     - Line editing (left/right arrows, home/end)
     - Built-in commands: exit, cd, help, history

   Type the program name (with arguments) and press Enter to run it. */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <syscall.h>

#define MAX_LINE 80
#define HISTORY_SIZE 16

/* Command history circular buffer */
static char history[HISTORY_SIZE][MAX_LINE];
static int history_count = 0;
static int history_pos = 0;

static void read_line(char line[], size_t size);
static bool backspace(char** pos, char line[]);
static void clear_line(char** pos, char line[]);
static void add_to_history(const char* cmd);
static void print_prompt(void);
static void show_help(void);

int main(void) {
  printf("\n");
  printf("=================================\n");
  printf("  Pintos Shell v2.0\n");
  printf("  Type 'help' for commands\n");
  printf("=================================\n\n");

  for (;;) {
    char command[MAX_LINE];

    /* Read command. */
    print_prompt();
    read_line(command, sizeof command);

    /* Skip empty commands */
    if (command[0] == '\0')
      continue;

    /* Add to history (skip duplicates) */
    if (history_count == 0 || strcmp(history[(history_count - 1) % HISTORY_SIZE], command))
      add_to_history(command);

    /* Built-in commands */
    if (!strcmp(command, "exit") || !strcmp(command, "quit")) {
      break;
    } else if (!strcmp(command, "help") || !strcmp(command, "?")) {
      show_help();
    } else if (!strcmp(command, "clear") || !strcmp(command, "cls")) {
      /* ANSI escape: clear screen + move cursor home */
      printf("\033[2J\033[H");
    } else if (!strcmp(command, "history")) {
      int start = history_count > HISTORY_SIZE ? history_count - HISTORY_SIZE : 0;
      for (int i = start; i < history_count; i++)
        printf(" %d  %s\n", i + 1, history[i % HISTORY_SIZE]);
    } else if (!memcmp(command, "cd ", 3)) {
      if (!chdir(command + 3))
        printf("cd: %s: No such directory\n", command + 3);
    } else if (!strcmp(command, "cd")) {
      /* cd with no args - do nothing (no home dir concept) */
    } else {
      /* Execute external command */
      pid_t pid = exec(command);
      if (pid != PID_ERROR) {
        int exit_code = wait(pid);
        if (exit_code != 0)
          printf("[exit code: %d]\n", exit_code);
      } else {
        printf("%s: command not found\n", command);
      }
    }
  }

  printf("\nGoodbye!\n");
  return EXIT_SUCCESS;
}

static void print_prompt(void) { printf("pintos$ "); }

static void show_help(void) {
  printf("\nBuilt-in commands:\n");
  printf("  help, ?     Show this help\n");
  printf("  history     Show command history\n");
  printf("  clear, cls  Clear the screen\n");
  printf("  cd <dir>    Change directory\n");
  printf("  exit, quit  Exit the shell\n");
  printf("\nLine editing:\n");
  printf("  Up/Down     Navigate history\n");
  printf("  Ctrl+U      Clear line\n");
  printf("  Ctrl+L      Clear screen\n");
  printf("  Ctrl+C      Cancel line\n");
  printf("\nExternal commands: ls, cat, echo, rm, mkdir, cp, ...\n\n");
}

static void add_to_history(const char* cmd) {
  strlcpy(history[history_count % HISTORY_SIZE], cmd, MAX_LINE);
  history_count++;
}

/* Reads a line of input with history support */
static void read_line(char line[], size_t size) {
  char* pos = line;
  int hist_nav = history_count; /* Current position in history navigation */
  char saved_line[MAX_LINE];    /* Save current line when navigating history */
  saved_line[0] = '\0';
  line[0] = '\0';

  for (;;) {
    char c;
    read(STDIN_FILENO, &c, 1);

    /* Check for escape sequence (arrow keys) */
    if (c == '\033') { /* ESC */
      char seq[2];
      read(STDIN_FILENO, &seq[0], 1);
      if (seq[0] == '[') {
        read(STDIN_FILENO, &seq[1], 1);
        switch (seq[1]) {
          case 'A': /* Up arrow - previous history */
            if (hist_nav > 0 && hist_nav > history_count - HISTORY_SIZE) {
              /* Save current line if at the bottom */
              if (hist_nav == history_count) {
                *pos = '\0';
                strlcpy(saved_line, line, MAX_LINE);
              }
              hist_nav--;
              /* Clear current line */
              clear_line(&pos, line);
              /* Copy history entry */
              strlcpy(line, history[hist_nav % HISTORY_SIZE], size);
              pos = line + strlen(line);
              printf("%s", line);
            }
            break;
          case 'B': /* Down arrow - next history */
            if (hist_nav < history_count) {
              hist_nav++;
              clear_line(&pos, line);
              if (hist_nav == history_count) {
                /* Restore saved line */
                strlcpy(line, saved_line, size);
              } else {
                strlcpy(line, history[hist_nav % HISTORY_SIZE], size);
              }
              pos = line + strlen(line);
              printf("%s", line);
            }
            break;
          case 'C': /* Right arrow - ignore for now */
          case 'D': /* Left arrow - ignore for now */
            break;
        }
      }
      continue;
    }

    switch (c) {
      case '\r':
      case '\n':
        *pos = '\0';
        putchar('\n');
        return;

      case '\b':
      case 127: /* DEL key */
        backspace(&pos, line);
        break;

      case ('U' - 'A') + 1: /* Ctrl+U - clear line */
        clear_line(&pos, line);
        break;

      case ('C' - 'A') + 1: /* Ctrl+C - cancel line */
        printf("^C\n");
        print_prompt();
        pos = line;
        line[0] = '\0';
        hist_nav = history_count;
        break;

      case ('L' - 'A') + 1:      /* Ctrl+L - clear screen */
        printf("\033[2J\033[H"); /* ANSI clear */
        print_prompt();
        printf("%s", line);
        break;

      default:
        /* Add printable characters to line */
        if (c >= 32 && c < 127 && pos < line + size - 1) {
          putchar(c);
          *pos++ = c;
        }
        break;
    }
  }
}

/* Clear the entire line from display and buffer */
static void clear_line(char** pos, char line[]) {
  while (*pos > line) {
    printf("\b \b");
    (*pos)--;
  }
  line[0] = '\0';
}

/* Backspace one character */
static bool backspace(char** pos, char line[]) {
  if (*pos > line) {
    printf("\b \b");
    (*pos)--;
    return true;
  }
  return false;
}
