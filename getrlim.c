#include <stdio.h>
#include <sys/time.h>
#include <sys/resource.h>

#define plim(x) do{\
	struct rlimit l[1];\
	getrlimit(x,l);\
	printf(#x " = %ld, %ld\n", (long)l->rlim_cur, (long)l->rlim_max);\
} while(0)

int main(void)
{
	plim(RLIMIT_AS);
	plim(RLIMIT_CORE);
	plim(RLIMIT_CPU);
	plim(RLIMIT_DATA);
	plim(RLIMIT_FSIZE);
	plim(RLIMIT_LOCKS);
	plim(RLIMIT_MEMLOCK);
//	plim(RLIMIT_MSGQUEUE);
//	plim(RLIMIT_NICE);
	plim(RLIMIT_NOFILE);
	plim(RLIMIT_NPROC);
	plim(RLIMIT_RSS);
//	plim(RLIMIT_RTPRIO);
	//plim(RLIMIT_RTTIME);
//	plim(RLIMIT_SIGPENDING);
	plim(RLIMIT_STACK);
//	plim(RLIMIT_OFILE);

	return 0;
}
