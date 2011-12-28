/* enpipe - pipe shoving
	enpipe [-o file] command-line
	pietro gagliardi - 27 dec 2011 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/wait.h>

/* a command line argument consisting entirely of this is replaced with the input filename */
#define ENPIPE_INFILE_MARK "-"

char *argv0, *outfile = NULL;
char tempfile[] = "/tmp/enpipestdinXXXXXX";

/* fatal:  panic */
void fatal(const char *fmt, ...)
{
	va_list arg;
	char *se;

	se = strerror(errno); /* save it ASAP */
	fprintf(stderr, "%s: ", argv0);
	va_start(arg, fmt);
	vfprintf(stderr, fmt, arg);
	va_end(arg);
	fprintf(stderr, ": %s\n", se);
	exit(EXIT_FAILURE);
}

/* filltempfile:  fill temp file with stdin */
void filltempfile(void)
{
	int outfid;
	char buf[BUFSIZ];
	int n;
	
	if ((outfid = mkstemp(tempfile)) == -1)
		fatal("could not generate temporary filename for stdin and open it");
	for (;;) {
		n = read(0, buf, BUFSIZ);
		if (n == -1)
			fatal("reading from standard input failed");
		if (n == 0)
			break;
		if (write(outfid, buf, n) != n)
			fatal("writing stdin to output file failed");
	}
	close(outfid);
}

#define CMDLINE_MEMEXTMSG "memory exhausted allocating room for command line"
#define combine(s) {char *k;						\
	combined_len += strlen(s);					\
	k = (char *) realloc(combined, combined_len);		\
	if (k == NULL)								\
		fatal(CMDLINE_MEMEXTMSG);				\
	combined = k;								\
	strcat(combined, s);}

/* fillcmdline:  return command line for execv */
char **fillcmdline(int argc, char *argv[])
{
	static char *pline[4]; /* sh, -c, line, NULL */
		/* making it static is okay; we're only calling this function once */
	char *combined = NULL;
	size_t combined_len;
	int inserted = 0;

	pline[0] = "sh";
	pline[1] = "-c"; /* this requires a SINGLE ARGUMENT to contain the entire line, so we have to build it into a string */
	combined = malloc(combined_len = 1); /* so initialize it */
	if (combined == NULL)
		fatal(CMDLINE_MEMEXTMSG);
	*combined = '\0';
	for (; argc; argc--, argv++) {
		if (strcmp(*argv, ENPIPE_INFILE_MARK) == 0) { /* insert filename */
			combine(tempfile);
			inserted = 1;
		} else
			combine(*argv);
		combine(" ");
	}
	if (!inserted) /* add if omitted */
		combine(tempfile);
	pline[2] = combined;
	pline[3] = NULL; /* and terminate */
	return pline;
}

/* copyoutfile:  copy output file into the pipeline */
void copyoutfile(void)
{
	int fid;
	char buf[BUFSIZ];
	int n;

	/* ERRORS IN THIS FUNCTION ARE NOT FATAL!
		the program probably failed; let's make its error status available anyway */
	fid = open(outfile, O_RDONLY);
	if (fid == -1) {
		fprintf(stderr, "%s: can't open %s for copying program output\n", argv0, outfile);
		return;
	}
	for (;;) {
		n = read(fid, buf, BUFSIZ);
		if (n == -1) {
			fprintf(stderr, "%s: reading from %s failed", argv0, outfile);
			break;
		}
		if (n == 0)
			break;
		if (write(1, buf, n) != n) {
			fprintf(stderr, "%s: writing %s to stdout failed", argv0, outfile);
			break;
		}
	}
	close(fid);
}		

/* usage:  report a syntax error and quit */
void usage(void)
{
	fprintf(stderr, "usage: %s [-o outfile] command-line\n", argv0);
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	char **pline;
	int exitstatus;
	int opt;
	extern char *optarg; /* getopt stuff */
	extern int opterr, optind;

	argv0 = argv[0];
	opterr = 0; /* disable library diagonstics */
	setenv("POSIXLY_CORRECT", "1", 1); /* force the GNU implementation of getopt() to act like standard getopt() and not parse the entire command line - we want to be able to pass command line options to the program */
	while ((opt = getopt(argc, argv, ":ino:s:")) != -1)
		switch (opt) {
		case 'i':
		case 'n':
			/* TODO */
			break;
		case 'o':
			outfile = optarg;
			break;
		case 's': /* this might be removed because mkstemps() is nonstandard */
			/* TODO */
			break;
		default:
			usage();
		}
	for (; optind; optind--) { /* excise other arguments */
		argc--;
		argv++;
	}
	if (argc <= 0) /* should something different happen here? */
		usage();
	filltempfile();
	pline = fillcmdline(argc, argv);
	switch (fork()) {
	case -1:
		fatal("fork to run program failed");
	case 0:
		execv("/bin/sh", pline);
		fatal("execv to run program failed");
	}
	if (wait(&exitstatus) == -1)
		fatal("error while waiting for child process to run"); /* TODO what do I do? */
	if (outfile != NULL)
		copyoutfile();
	exit(exitstatus);
}
