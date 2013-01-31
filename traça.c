#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <sys/ptrace.h>
#include <sys/user.h>
#include <asm/unistd_32.h>


 /*
  * NAME
  * 	nurse - treat a program with care
  *
  * SYNOPSIS
  * 	nurse [OPTIONS] COMMAND
  * 	nurse [OPTIONS] -- COMMAND [ARGS]
  *
  * DESCRIPTION
  * 	Runs COMMAND with restrictions on its usage of resources and syscalls.
  * 	With no OPTIONS, the program runs without restrictions.  There are two
  * 	kinds of options: to put limits on resources (specified via the command
  * 	line) and to put limits on the system calls (specified via a config
  * 	file).
  *
  * RESOURCE LIMITS
  * 	By default, there are no resource limits.  Each limit, soft and hard
  * 	may be set individually putting "LIMITNAME soft hard" as an option on
  * 	the command line.
  *
  * SYSCALL MANAGEMENT
  * 	By default, there are no limitations on system calls.  In blacklist
  * 	mode, the user specifies a list of forbidden system calls; the rest of
  * 	the calls being run without restrictions.  In whitelist mode, the user
  * 	specifies a list of allowed system calls; the rest of the calls being
  * 	forbidden.  In whitelist mode, the specified system calls can be forced
  * 	to run a maximum number of times, and their arguments can be forced to
  * 	match certain conditions (numeric or regexp).  So far, syscall
  * 	management is done via a fixed config file.
  *
  * TODO
  * 	* Command-line arguments for all the hard-coded options.
  * 	* Options to redirect input and output of the child process
  * 	* Kill the child safely (always)
  *
  * SEE ALSO
  * 	strace(1), ulimit(1)
  */

//#define PLIMIT_CONFIG_FILE "ptropt.ptropt"
#define PLIMIT_CONFIG_FILE "/etc/nurse.conf"

/*
 * data structures
 */

struct one_limit {
	int resource;
	char *name;
	bool active;
	struct rlimit l;
};

#define REGISTER(x) {RLIMIT_ ## x, #x, false, {0,0}}
static struct one_limit global_resource_limits[] = {
	REGISTER(AS),
	REGISTER(CORE),
	REGISTER(CPU),
	REGISTER(DATA),
	REGISTER(FSIZE),
	REGISTER(LOCKS),
	REGISTER(MEMLOCK),
//	REGISTER(MSGQUEUE),
//	REGISTER(NICE),
	REGISTER(NOFILE),
	REGISTER(NPROC),
	REGISTER(RSS),
//	REGISTER(RTPRIO),
//	REGISTER(SIGPENDING),
	REGISTER(STACK),
//	REGISTER(OFILE),
	{-1, "", 0, {0,0}}
};

typedef struct {
	int argc;
	char **argv; //argv[0] = executable file name

	struct one_limit *lim;
	bool do_trace;
	int status;

	pid_t pid;
	bool in_call;
	int counter;

	bool report_exit_fail;
	int report_exit_status;
} traced_program;


/*
 * misc
 */

#define NHEAD "==NURSE== "

#define global_debugging_stream stderr

#ifndef LENGTH
#define LENGTH(a) (sizeof(a)/sizeof((a)[0]))
#endif

pid_t atexit_hack_pid = 0;

static void debug(const char *fmt, ...)
{
	FILE *f = global_debugging_stream;
	if (!f) return;

	va_list argp;
	va_start(argp, fmt);
	fprintf(f, NHEAD);
	vfprintf(f, fmt, argp);
	fflush(f);
	va_end(argp);
}


/*
 * resources stuff
 */

// set the limits of the current process
static void set_resource_limits(traced_program *p)
{
	struct one_limit *l = p->lim;;
	while (l->resource >= 0)
	{
		if (l->active) {
			debug("setting %s = %ld %ld\n",
					l->name, (long)l->l.rlim_cur,
					(long)l->l.rlim_max);
			int r = setrlimit(l->resource, &l->l);
			if (r) {
				debug("error \"%s\"\n", strerror(errno));
				exit(EXIT_FAILURE);
			}
			//else debug("done\n");
		}
		l += 1;
	}
}


/*
 * tracing stuff
 */

struct one_call {
	int syscall;
	char *name;
	bool active;
	int option;
	int maxcalls;
	int numcalls;
	char *s;
	// ... whatever else
};


#define REG_SYSC(x) [x] = {x, #x, false, 0, 0, 0, NULL}
static struct one_call global_call_list[] = {
#include "reg_sysc.c"
	{-1, NULL, false, 0, 0, 0, NULL}
};
static const int NUMCALLS = LENGTH(global_call_list);

static char *call_name(int syscall)
{
	int i = 0;
	struct one_call *c = global_call_list;
	while (i < NUMCALLS) {
		if (c[i].syscall == syscall)
			return c[i].name;
		i += 1;
	}
	return NULL;
}

/*
 * signal stuff
 */

struct one_signal {
	int signal;
	char *name;
	char *diag;
	// ... whatever else
};

#define REG_SYGN(x,d) {x, #x, d}
static struct one_signal global_signal_list[] = {
	REG_SYGN(SIGHUP, "hang up"),
	REG_SYGN(SIGINT, "keyboard interrupt"),
	REG_SYGN(SIGQUIT, "keyboard quit"),
	REG_SYGN(SIGILL, "illegal instruction"),
	REG_SYGN(SIGTRAP, "breakpoint"),
	REG_SYGN(SIGABRT, "abort"),
	REG_SYGN(SIGFPE, "floating point exception"),
	REG_SYGN(SIGKILL, "killed"),
	REG_SYGN(SIGUSR1, "user-defined signal 1"),
	REG_SYGN(SIGUSR2, "user-defined signal 2"),
	REG_SYGN(SIGSEGV, "segmentation fault"),
	REG_SYGN(SIGPIPE, "broken pipe"),
	REG_SYGN(SIGALRM, "alarm"),
	REG_SYGN(SIGTERM, "terminated"),
	REG_SYGN(SIGCHLD, "child stopped"),
	REG_SYGN(SIGSTOP, "stopped"),
	REG_SYGN(SIGCONT, "continue"),
	REG_SYGN(SIGBUS, "bus error"),
	REG_SYGN(SIGXCPU, "cpu time limit exceeded"),
	REG_SYGN(SIGXFSZ, "file size limit exceeded"),
	{-1, NULL, NULL}
};
static const int NUMSIGNALS = LENGTH(global_signal_list);

static char *signal_string(int signal)
{
	int i = 0;
	struct one_signal *c = global_signal_list;
	while (i < NUMCALLS) {
		if (c[i].signal == signal)
			return c[i].diag;
		i += 1;
	}
	return "unrecognized signal";
}

/*
 * main program
 */

static void usagerr()
{
	struct one_limit *t = global_resource_limits;
	struct one_limit *l = t;
	fprintf(stderr, "usage:\n\tnurse [RLIM soft hard]* -- exe [arg]*\n\t");
	while (l->resource >= 0) {
		fprintf(stderr, "%s%s", l->name, (l-t+1)%12 ? " ":"\n\t");
		l += 1;
	}
	fprintf(stderr, "\n");
	fprintf(stderr, "\tenv: %s %s\n", "PLIMIT_CONFIG_FILE",
			"NURSE_HACK_REPORT_EXIT_FAIL");
	exit(EXIT_FAILURE);
}

static void fill_one_limit(traced_program *p, char *n, int v, int m)
{
	struct one_limit *l = p->lim;
	while (l->resource >= 0)
	{
		if (0 == strcmp(n, l->name)) {
			//fprintf(stderr, "got %s = %d %d\n", l->name, v, m);
			l->active = true;
			l->l.rlim_cur = v;
			l->l.rlim_max = m;
		}
		l += 1;
	}
}

static int find_barbar(int c, char *v[])
{
	int r;
	for (r = 0; r < c; r++)
		if (0 == strcmp(v[r], "--"))
			return r+1;
	return -1;

}

// "smart" parameter
static char *plimit_config_file(void)
{
	char *r = getenv("PLIMIT_CONFIG_FILE");
	return r ? r : PLIMIT_CONFIG_FILE;
}

static void register_traced_conf(traced_program *p)
{
	assert(p->do_trace);
	//FILE *f = fopen(PLIMIT_CONFIG_FILE, "r");
	FILE *f = fopen(plimit_config_file(), "r");
	if (!f) {
		p->do_trace = false;
		return;
	}
	debug("opened syscall file \"%s\"\n", plimit_config_file());


	// by default, disable all the calls
	for (int i = 0; i < NUMCALLS; i++) {
		int sci = global_call_list[i].syscall;
		if (sci && sci != -1) {
			assert(sci = i);
			global_call_list[i].maxcalls = 0;
			global_call_list[i].numcalls = 0;
		}
	}

	// read config file, and allow the specified calls
	char cline[0x100];
	while (fgets(cline, 0x100, f)) {
		char *pos = strchr(cline, ' ');
		if (!pos) continue;
		*pos = '\0';
		int number, i = 0;
		if (!pos[1]) continue;
		if (1 != sscanf(pos+1, "%d", &number)) continue;
		while (i < NUMCALLS-1) {
			if (global_call_list[i].name &&
				0 == strcmp(global_call_list[i].name, cline))
			{
				global_call_list[i].maxcalls = number;
				//fprintf(stderr, "%s: %d\n", cline, number);
			}
			i += 1;
		}
	}
	fclose(f);
	//exit(7);
}

// this ``smart parameter'' makes the nurse fail when the child does
static float NURSE_HACK_REPORT_EXIT_FAIL(void)
{
	char *r = getenv("NURSE_HACK_REPORT_EXIT_FAIL");
	return r ? atof(r) : 0;
}

static void read_args(traced_program *p, int c, char *v[])
{
	int b = find_barbar(c, v);
	if (b == -1 && c > 1) b = c - 1;
	else if (b < 1 || b > c - 1) usagerr();

	p->argc = c - b;
	p->argv = v + b;

	p->lim = global_resource_limits;

	int i = 1;
	while (i + 2 < b) {
		fill_one_limit(p, v[i], atoi(v[i+1]), atoi(v[i+2]));
		i += 3;
	}

	// TODO: treat ptrace-related arguments
	p->do_trace = false;
	p->do_trace = true;
	if (p->do_trace)
		register_traced_conf(p);

	p->report_exit_fail = false;
	if (NURSE_HACK_REPORT_EXIT_FAIL() > 0.5)
	{
		p->report_exit_fail = true;
		p->report_exit_status = 0;
	}
}

static void proceed_child(traced_program *p)
{
	set_resource_limits(p);
	if (p->do_trace)
		ptrace(PTRACE_TRACEME, 0, 0, 0);
	for (int i = 0; i < p->argc; i++)
		debug("argv[%d] = \"%s\"\n", i, p->argv[i]);
	execve(p->argv[0], p->argv, NULL); // only returns if it fails

	perror(NHEAD "execve");
	exit(EXIT_FAILURE);
}

#define NTHBYTE(l,i) (((l) >> (8*(i)))&0xff)

// copies a string from the child
static void strnchild(traced_program *p, char *to, char *from, long n)
{
	//assert(sizeof(long)==4);
	size_t wordsize = sizeof(long);
	long i = -1, r;
	do {
		i += 1;
		if (0 == i % wordsize) {
			errno = 0;
			r = ptrace(PTRACE_PEEKDATA, p->pid, from + i, NULL);
			if (r == -1 && errno) {
				debug("peek error \"%s\"\n", strerror(errno));
				exit(EXIT_FAILURE);
			}
		}
		to[i] = NTHBYTE(r, i % wordsize);
	} while(i < n && to[i]);
	if (i == n)
		debug("MEGA WARNING LARGE STRING MAY BREAK STUFF!!!\n");
}

static void stringshot(char *y, char *x, long n, int max)
{
	int cx = 0, i = 0;
	for (; i < n && cx < max - 1; i++)
	{
		int c = x[i];
		if (isalnum(c) || c == ' ' || ispunct(c))
			y[cx++] = c;
		else if (c == '\n') { y[cx++] = '\\'; y[cx++] = 'n'; }
		else if (c == '\t') { y[cx++] = '\\'; y[cx++] = 't'; }
		else if (c == '\0') { y[cx++] = '\\'; y[cx++] = '0'; }
		else y[cx++] = '.';
	}
	y[cx] = '\0';
}

// TODO: split this function into smaller parts
static void trace_child(traced_program *p)
{
	p->counter = 0;
	p->in_call = false;

	/*
	 * continue to stop, wait and release until
	 * the child is finished; wait_val != 1407
	 * Low=0177L and High=05 (SIGTRAP)
	 */
	while (p->status == 1407 ) {
		p->counter++;
		if (ptrace(PTRACE_SYSCALL, p->pid, 0, 0) != 0)
			perror(NHEAD "ptrace");
		wait(&p->status);
		p->in_call = !p->in_call;

		struct user_regs_struct r;
		if (ptrace(PTRACE_GETREGS, p->pid, 0, &r) != 0)
			perror(NHEAD "ptracer (TODO:pair)");
		else {
			int nbuff=0x100;
			char buff[nbuff];
			int syscall = r.orig_eax;
			// TODO: duely process each syscall (white/black list)
			if (!syscall) exit(69);
			if (syscall == __NR_exit) continue;
			long ebx = r.ebx;
			long ecx = r.ecx;
			long edx = r.edx;
			struct one_call *call = global_call_list + syscall;
			char *callname = call->name;
			if (callname && p->in_call) {
				call->numcalls += 1;
				debug("syscall %s(%d) [b,c,d]=[%lx,%lx,%lx] (%d of %d)\n",
						callname+5, syscall,
						ebx, ecx, edx,
						call->numcalls, call->maxcalls
						);
				if (syscall == __NR_open)
				{
					strnchild(p, buff, (char *)ebx, nbuff);
					debug("ebx = %lx \"%s\"\n", ebx, buff);
				}
				if (syscall == __NR_write)
				{
					strnchild(p, buff, (char *)ecx, nbuff);
					char buff2[nbuff];
					stringshot(buff2, buff, edx, 40);
					debug("ebx = %lx \"%s\"\n", ecx, buff2);
				}
				if (call->maxcalls >= 0 &&
					call->numcalls > call->maxcalls) {
					ptrace(PTRACE_KILL, p->pid, 0, 0);
					debug("DIAG too much %d-calls (%s), "
							"killing...\n",
							syscall, 5+
							call_name(syscall)
							);
				}
				//if (syscall == __NR_fork)
				//{
				//	ptrace(PTRACE_KILL, p->pid, 0, 0);
				//	debug("invalid syscall, killing...");
				//}
			}
			if (callname && !p->in_call) {
				debug("syscall %s(%d) returned %lx\n",
						callname+5, syscall,
						r.eax);
			}
		}
	}

	debug("counter (syscalls) = %d\n", p->counter);
}

static void proceed_parent(traced_program *p)
{
	int status;
	wait(&status);

	if (p->do_trace)
	{
		p->status = status;
		trace_child(p);
		status = p->status;
	}

	debug("done [%#x = %d]\n", status, status);
	if (WIFEXITED(status))
		debug("DIAG exit status %d\n",
				WEXITSTATUS(status));
	if (WIFSIGNALED(status))
		debug("DIAG signaled with signal number %d (%s)\n",
				WTERMSIG(status),
				signal_string(WTERMSIG(status)) );
	if (WIFSTOPPED(status))
		debug("DIAG stopped with signal %d (%s)\n",
				WSTOPSIG(status),
				signal_string(WSTOPSIG(status)) );

	if (p->report_exit_fail) {
		p->report_exit_status =
			(WIFEXITED(status) && 0==WEXITSTATUS(status)) ?
			EXIT_SUCCESS : EXIT_FAILURE;
	}
}

static void run_limited_program(traced_program *p)
{
	switch (p->pid = fork()) {
        case -1:
                perror(NHEAD "fork");
		exit(EXIT_FAILURE);
        case 0:
		atexit_hack_pid = p->pid;
		proceed_child(p); // does not return
		fprintf(stderr, "IMPOSSIBLE CONDITION!\n");
		exit(EXIT_FAILURE);
	default:
		proceed_parent(p);
	}
}

// TODO: remove this hack (it does not work well)
static void bye(void)
{
	if (atexit_hack_pid)
		kill(atexit_hack_pid, 9);
}

static void check_consistency(void)
{
	for (int i = 0; i < NUMCALLS; i++)
	{
		int sci = global_call_list[i].syscall;
		if (sci && sci != -1 && sci != i)
		{
			fprintf(stderr, "bad bad bad %d!=%d\n", sci, i);
			exit(38);
		}
	}

	if(atexit(bye)) {
		fprintf(stderr,"ou nou!\n");
		exit(39);
	}
}

// usage: LIMIT soft hard ... -- executable arg1 arg2 ...
int main(int c, char *v[])
{
	check_consistency();

	traced_program p[1];
	read_args(p, c, v);
	run_limited_program(p);

	return p->report_exit_fail ? p->report_exit_status : EXIT_SUCCESS;
}
