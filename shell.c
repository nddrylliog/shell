#include <sys/wait.h>
#include <sys/fcntl.h>
#include <sys/types.h>
#include <sys/param.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <signal.h>


static int error;
static int inout[2];
static int flags;

enum PROCESS_FLAGS {
    P_BG =  (1 << 0),
    P_OR =  (1 << 1),
    P_AND = (1 << 2),
};

typedef int (*builtin_cmd)(int, char **);

struct builtin {
	const char *name;
	builtin_cmd func;
};
#define	BIN(n)	{ #n, (int (*)()) builtin_ ## n }


int
builtin_cd(int argc, char **argv)
{
        if (argc < 2) {
            warn("Usage: cd <directory>");
            return (1);
        }
        int code = chdir(argv[1]);
        if (code != 0) {
            fprintf(stderr, "cd: %s\n", strerror(errno));
        }
	return code;
}

int
builtin_exit(void)
{
        exit(0);
}

int
builtin_status(void)
{
        printf("%d\n", error);
	return (0);
}

static struct builtin builtins[] = {
	BIN(cd),
	BIN(exit),
	BIN(status),
	{ NULL, NULL }
};

/*
 * run_builtin handles setting up the handlers for your builtin commands
 *
 * It accepts the array of strings, and tries to match the first argument
 * with the builtin mappings defined in the builtins[].
 * Returns 0 if it did not manage to find builtin command, or 1 if args
 * contained a builtin command
 */
static int
run_builtin(char **args)
{
	int argc;
	struct builtin *b;

	for (b = builtins; b->name != NULL; b++) {
		if (strcmp(b->name, args[0]) == 0) {
			for (argc = 0; args[argc] != NULL; argc++)
				/* NOTHING */;
			error = b->func(argc, args);
			return (1);
		}
	}

	return (0);
}

/*
 * run_command handles 
 */
static int
run_command(char **args)
{
        // DEBUG start
        // char **nargs = args;
        // int argc = 0;
        // while (*nargs) {
        //     argc++;
        //     nargs++;
        // }
        // fprintf(stderr, "Launching %s with %d arguments\n", args[0], argc);
        // DEBUG end
        //

        if (flags & P_AND) {
            // only execute if previous command was successful
            if (error != 0) {
                flags = 0;
                return (0);
            }
        } else if (flags & P_OR) {
            // only execute if previous command was unsuccessful
            if (error == 0) {
                flags = 0;
                return (0);
            }
        }

        // try built-in first
        if (run_builtin(args) == 1) {
            return (1);
        }

        // execute external command
        pid_t child_pid;
        if ((child_pid = fork()) == 0) {
            /* Child process code */    
            if (inout[0] != STDIN_FILENO) {
                dup2(inout[0], 0); 
                if (errno) {
                    fprintf(stderr, "error while dup2-ing: %s\n", strerror(errno));
                }
                close(inout[0]);
            }

            if (inout[1] != STDOUT_FILENO) {
                dup2(inout[1], 1); 
                if (errno) {
                    fprintf(stderr, "error while dup2-ing: %s\n", strerror(errno));
                }
                close(inout[1]);
            }

            // fprintf(stderr, "Launching '%s'\n", args[0]);
            if (execvp(args[0], args) == -1) {
                fprintf(stderr, "Process %d failed with error: %s", child_pid, strerror(errno));
            }
        } else if (child_pid > 0) {
            /* Parent process code */
            if (!(flags & P_BG)) {
                fprintf(stderr, "flags = %d, waiting.\n", flags);
                waitpid(child_pid, &error, 0);
            }
        } else {
            fprintf(stderr, "Failed to fork! Cannot launch command.\n");
        }

        flags = 0;

        return (1);
}

/*
 * Takes a pointer to a string pointer and advances this string pointer
 * to the word delimiter after the next word.
 * Returns a pointer to the beginning of the next word in the string,
 * or NULL if there is no more word available.
 */
static char *
parseword(char **pp)
{
	char *p = *pp;
	char *word;

	for (; isspace(*p); p++)
		/* NOTHING */;

	word = p;

        // Japanese schoolgirl is NOT AMUSED.
	// for (; fprintf(stderr, "Testing char %c aka %d\n", *p, *p), strchr("\t&> <;|\n", *p) == NULL; p++)
	for (; strchr("\t&> <;|\n", *p) == NULL; p++)
		/* NOTHING */;

	*pp = p;

	return (p != word ? word : NULL);
}

static void
process(char *line)
{
	int ch, ch2;
	char *p, *word;
	char *args[100], **narg;
	int pip[2];

	p = line;
        if (*p == '#') {
            // comment line, don't execute
            return;
        }

newcmd:
	inout[0] = STDIN_FILENO;
	inout[1] = STDOUT_FILENO;
        flags = 0;

newcmd2:
	narg = args;
	*narg = NULL;

	for (; *p != 0; p++) {
		word = parseword(&p);

		ch = *p;
		*p = 0;

		// printf("parseword: '%s', '%c', '%s'\n", word, ch, p + 1);

		if (word != NULL) {
			*narg++ = word;
			*narg = NULL;
		}

nextch:
		switch (ch) {
		case ' ':
		case '\t': p++; ch = *p; goto nextch;
		case '<': {
                        p++;
                        char *path = parseword(&p);
                        *p = 0;

                        inout[0] = open(path, O_RDONLY);

                        p++; ch = *p;
                        goto nextch;
                }
		case '>': {
                        p++;
                        char* path = parseword(&p);
                        *p = 0;

                        inout[1] = open(path, O_CREAT | O_WRONLY, 00644);

                        p++; ch = *p;
                        goto nextch;
                }
                case '|': {
                        p++; 
                        ch2 = *p;
                        if (ch2 == '|') {
                            p++;
                            run_command(args);
                            flags |= P_OR;
                            goto newcmd;
                        } else {
                            warn("piping!");
                        }
                        break;
                }
                case '&': {
                        p++;
                        ch2 = *p;
                        if (ch2 == '&') {
                            p++;
                            run_command(args);
                            flags |= P_AND;
                            goto newcmd;
                        } else {
                            flags |= P_BG;
                            run_command(args);
                            goto newcmd;
                        }
                        break;
                }
		case ';':
                        p++;
                        run_command(args);
                        goto newcmd;
		case '\n':
                        run_command(args);
			break;
		case '\0':
                        run_command(args);
                        goto newcmd;
		default:
                        p--; // will be handled by next iteration
                        break;
		}
	}
}

static void siginthandler()
{
        ;
}

int
main(void)
{
	char cwd[MAXPATHLEN+1];
	char line[1000];
	char *res;
        
        signal(SIGINT, siginthandler);
	
        for (;;) {
		getcwd(cwd, sizeof(cwd));
		printf("%s %% ", cwd);

		res = fgets(line, sizeof(line), stdin);
		if (res == NULL)
			break;

		process(line);
	}

	return (error);
}
