#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

int main(int c, char *v[])
{
	if (c != 5) {
		fprintf(stderr, "usage:\n\t%s mem cpu files faultP\n", *v);
		exit(EXIT_FAILURE);
	}
	size_t n_mem = 1024*1024*atol(v[1]);
	unsigned long n_cpu = atoll(v[2]);
	unsigned int n_files = atoi(v[3]);
	int faultPi = atoi(v[4]);

	if (n_mem) {
		printf("malloc(%zd)\n", n_mem);
		char *t = malloc(n_mem);
		if (!t) {
			printf("malloc fail: %s\n", strerror(errno));
			return 1;
		}
		else
		{
			for (unsigned long i = 0; i < n_mem; i++)
				t[i] = i;//rand();
			free(t);
		}
	}

	if (n_cpu > 1000) {
		printf("n_cpu = %ld\n", n_cpu);
		unsigned long MMM = n_cpu / 10;
		for (unsigned long i = 0; i < n_cpu; i++)
			if ((i % MMM) == 0)
				printf("eo %ld \n", i / MMM);
	}

	if (n_files) {
		FILE *f[n_files];
		for (unsigned int i = 0; i < n_files; i++) {
			char tmpname[0x100];
			snprintf(tmpname, 0x100, "/tmp/cosa_lletja_%d", i);
			f[i] = fopen(tmpname, "w");
			if (!f[i]) {
				printf("fopen fail (%s): %s\n",
						tmpname, strerror(errno));
				return 1;
			}
		}
		for (unsigned int i = 0; i < n_files; i++ )
			fclose(f[i]);
	}

	if (faultPi) {
		if (faultPi == -1) {
			int *p = NULL;
			int q = *p;
			return q;
		}
		return faultPi;
	}

	return 0;
}
