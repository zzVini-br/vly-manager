#ifndef VLY_APP_COMMANDS_H
#define VLY_APP_COMMANDS_H

/* Each command receives its own argv, with argv[0] being the command name,
 * and returns the process exit status. */

int cmd_list(int argc, char **argv);
int cmd_kill(int argc, char **argv);
int cmd_kill_tree(int argc, char **argv);

#endif /* VLY_APP_COMMANDS_H */
