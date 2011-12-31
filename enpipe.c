/* enpipe - pipe shoving
	enpipe [-in] [-o outfile] command-line
	pietro gagliardi - 27 dec 2011 */

/* TODO: 'passstdin' should probably be named something a bit more clear */

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
/* 	passstdin is 1 to pass standard input through rather than send to file */
char **fillcmdline(int argc, char *argv[], int passstdin)
{
	static char *pline[4]; /* sh, -c, line, NULL */
		/* making it static is okay; we're only calling this function once */
	char *combined = NULL;
	size_t combined_len;
	int inserted = passstdin;

	pline[0] = "sh";
	pline[1] = "-c"; /* this requires a SINGLE ARGUMENT to contain the entire line, so we have to build it into a string */
	combined = malloc(combined_len = 1); /* so initialize it */
	if (combined == NULL)
		fatal(CMDLINE_MEMEXTMSG);
	*combined = '\0';
	for (; argc; argc--, argv++) {
		if (!passstdin && strcmp(*argv, ENPIPE_INFILE_MARK) == 0) { /* insert filename */
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

/* zapstdout:  redirect stdout to /dev/null */
/* 	(merely closing it caused TeX to write a ] at the beginning of the output DVI file, which is wrong) */
void zapstdout(void)
{
	int nf, dupstatus;

	nf = open("/dev/null", O_RDWR); /* TODO what should it be? */
	if (nf == -1)
		fatal("could not open /dev/null to redirect stdout");
	dupstatus = dup2(nf, 1);
	if (dupstatus == -1)
		fatal("could not reroute /dev/null to stdout");
	else if (dupstatus != 1) /* TODO is this necessary? */
		fatal("dup2 was told to reroute /dev/null to stdout, but didn't (it went to file descriptor %d instead)", dupstatus);
	close(nf); /* we don't need this, nor care if it fails */
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
		char *e;

		e = strerror(errno);
		fprintf(stderr, "%s: can't open %s for copying program output: %s\n", argv0, outfile, e);
		return;
	}
	for (;;) {
		n = read(fid, buf, BUFSIZ);
		if (n == -1) {
			char *e;

			e = strerror(errno);
			fprintf(stderr, "%s: reading from %s failed: %s\n", argv0, outfile, e);
			break;
		}
		if (n == 0)
			break;
		if (write(1, buf, n) != n) {
			char *e;

			e = strerror(errno);
			fprintf(stderr, "%s: writing %s to stdout failed: %s\n", argv0, outfile, e);
			break;
		}
	}
	close(fid);
}		

/* usage:  report a syntax error and quit */
void usage(void)
{
	fprintf(stderr, "usage: %s [-in] [-o outfile] command-line\n", argv0);
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	char **pline;
	int exitstatus;
	int passstdin = 0;
	int opt;
	extern char *optarg; /* getopt stuff */
	extern int opterr, optind;

	argv0 = argv[0];
	opterr = 0; /* disable library diagonstics */
	setenv("POSIXLY_CORRECT", "1", 1); /* force the GNU implementation of getopt() to act like standard getopt() and not parse the entire command line - we want to be able to pass command line options to the program */
	while ((opt = getopt(argc, argv, ":ino:")) != -1)
		switch (opt) {
		case 'i':
		case 'n':
			passstdin = 1;
			break;
		case 'o':
			outfile = optarg;
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
	if (!passstdin)
		filltempfile();
	pline = fillcmdline(argc, argv, passstdin);
	switch (fork()) {
	case -1:
		fatal("fork to run program failed");
	case 0:
		if (outfile != NULL) /* we don't want stdout mixing with the outfile */
			zapstdout();
		execv("/bin/sh", pline);
		fatal("execv to run program failed");
	}
	if (wait(&exitstatus) == -1)
		fatal("error while waiting for child process to run"); /* TODO what do I do? */
	if (outfile != NULL)
		copyoutfile();
	exit(exitstatus);
}

/* other things:
	- originally I wanted -s suffix to add a suffix to the filename
		in case a program needed it, but a) why would it
		b) mkstemps() is not POSIX
*/
