#ifndef	COMMON_H
#define	COMMON_H
#include <sys/time.h>
#define	ARR_SZ(x) (sizeof (x) / sizeof (x[0]))
static inline void print_ts(const char *s)
{
	struct timeval tv;
	struct timezone tz;

	printf("DBG: %s", s);
	if (gettimeofday(&tv, &tz))
		printf(" gettimeofday failed!\n");
	else
		printf(" %lu:%lu\n", tv.tv_sec, tv.tv_usec);
}

#define PRINT_ERR_EXIT(args...) {fprintf(stderr, "[%s]:%s:%i ", __FILE__, __func__, __LINE__); fprintf(stderr, args); exit(1);}
#define	ASSERT_ERR(str, cond) { if (cond) { PRINT_ERR_EXIT(str);} }
#endif
