// run the specified program with limited resources, as in SETRLIMIT(2)


#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>

struct one_limit {
	int resource;
	char *name;
	bool active;
	struct rlimit l;
};

#define REGISTER(x) {RLIMIT_ ## x, #x, false, {0,0}}
static struct one_limit t[] = {
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

static void usagerr()
{
	struct one_limit *l = t;
	fprintf(stderr, "usage:\n\tsetrlim [RLIMIT_1 v m]* -- prog [args]\n\t");
	while (l->resource >= 0) {
		fprintf(stderr, "%s%s", l->name, (l-t+1)%9 ? " ":"\n\t");
		l += 1;
	}
	fprintf(stderr, "\n");
	exit(EXIT_FAILURE);
}

static void fill_one(char *n, int v, int m)
{
	struct one_limit *l = t;
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

static void fill_all(int c, char *v[])
{
	int i = 1;
	while (i+2 < c) {
		fill_one(v[i], atoi(v[i+1]), atoi(v[i+2]));
		i += 3;
	}
}

static void set_lims(void)
{
	struct one_limit *l = t;
	while (l->resource >= 0)
	{
		if (l->active) {
			fprintf(stderr, "setting %s = %ld %ld...\t",
					l->name, (long)l->l.rlim_cur,
					(long)l->l.rlim_max);
			int r = setrlimit(l->resource, &l->l);
			if (r) {
				fprintf(stderr, "error \"%s\"\n",
						strerror(errno));
				exit(EXIT_FAILURE);
			}
			else
				fprintf(stderr, "done\n");
		}
		l += 1;
	}
}

// if there is a "--", run program + arguments
// otherwise, run program without arguments
static void call_prog(int c, char *v[])
{
	if (c < 2) usagerr();
	int i = 0;
	while (v[i] && 0 != strcmp("--", v[i]))
		i += 1;

	char *f, **a;
	if (i+1 < c && 0 == strcmp("--", v[i]))
	{
		f = v[i+1];
		a = v + i + 1;
	} else {
		f = v[c-1];
		a = NULL;
	}
	fprintf(stderr, "running \"%s\"\n", f);
	if (a)
		for (i = 0; a[i]; i++)
			fprintf(stderr, "\targ[%d] = \"%s\"\n", i, a[i]);
	execve(f, a, NULL);
	fprintf(stderr, "ERROR %d (%s)\n", errno, strerror(errno));
}

// usage: setrlim RLIMIT_NAME1 v m RLIMIT_NAME2 v m ... -- prog [args]
int main(int c, char *v[])
{
	fill_all(c, v);
	set_lims();
	//call_proc(v[c-1]);
	call_prog(c, v);

	return EXIT_SUCCESS;
}
