#ifndef __UTILS_H__
#define __UTILS_H__

#include "rwcached.h"

#include <getopt.h>

/* 输出程序参数设置方式 */
void util_programUsage(char *program_name);

/* 获得程序启动设置参数 */
int util_getOptions(int argc, char *argv[], const char *short_opts);


#endif // __UTILS_H__
