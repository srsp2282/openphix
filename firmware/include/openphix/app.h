/* Application entry points. */
#ifndef OPENPHIX_APP_H
#define OPENPHIX_APP_H

/* The interactive application: boots, shows the menu, runs until the port
 * reports HAL_KEY_QUIT. Returns 0. */
int app_main(void);

/* Developer tools built into the core, run from the simulator with
 * `--tool <name> [args]`: dumps of the decoded resources, used to verify the
 * C parsers against tools/hixtool. Returns a process exit code. */
int app_tool(int argc, char **argv);

#endif
