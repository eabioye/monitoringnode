#ifndef TIME_UTIL_H
#define TIME_UTIL_H

#include <time.h>

void init_time(void);
struct tm get_time_now(int *milliseconds);


#endif