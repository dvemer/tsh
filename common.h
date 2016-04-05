#ifndef	COMMON_H
#define	COMMON_H
#include <sys/time.h>
#include <string.h>
#include <errno.h>
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

static inline int string_is_empty(const char *s)
{
	int i;

	i = 0;

	while (s[i] != '\0') {
		if (s[i] != ' ')
			return 0;
		i++;
	};

	return 1;
}

#define PRINT_ERR_EXIT(args...) { fprintf(stderr, "[%s]:%s:%i %s", __FILE__, __func__, __LINE__, strerror(errno)); \
	fprintf(stderr, args); getchar() ;exit(1);}
#define	ASSERT_ERR(str, cond) { if (cond) { PRINT_ERR_EXIT(str);} }
#endif
