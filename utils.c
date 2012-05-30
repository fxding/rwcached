
#include "rwcached.h"
#include "utils.h"
#include "xmem.h"

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>

void util_programUsage(char *program_name){
	printf("使用方式: %s -options <args>......\n", program_name);
	printf("-p <num>		指定程序监听端口, 默认 11234.\n"
		   "-d			作为守护程序运行.\n"
		   "-m <num>		最大预分配内存空间, 单位 MB, 默认 64 MB.\n"
		   "-l <num>		指定日志文件级别, DEBUG-1, INFO-2, WARN-3, ERR-4, FATAL-5.\n"
		   "-h			输出帮助信息, 然后退出.\n"
		   "-f <factor>		指定预分配内存使用时, 内存槽的增长率, 默认 1.25.\n"
		   "-b <bytes>		指定存储的 key-value 存储的最小空间大小, 单位 byte, 默认 48 bytes.\n"
		   "-t <num>		指定系统启用的服务线程数, 默认 1.\n");
	printf("-M <IP>			设置作为 slave 时, master 的 ip.\n"
		   "-P <num>		设置 master 端口, -S 与 -P, 同时使用才会有效.\n");
	return;
}




int util_getOptions(int argc, char *argv[], const char *short_opts){

	char c = '\0';
	int isslave = 0;

	//if(argc == 1){
	//	util_programUsage(argv[0]);
	//	exit(1);
	//}

	while((c=getopt(argc, argv, short_opts)) != -1){
		switch(c){
			case 'p':
					server.port = atoi(optarg);
					break;
			case 'd':
					server.daemonize = 1;
					break;
			case 'm':
					server.memlimited = ((size_t)atoi(optarg))*1024*1024;
					break;
			case 'l':
					server.loglevel = atoi(optarg);
					if(server.loglevel < 0 || server.loglevel > LOG_FATAL){
						fprintf(stderr, "日志级别设置错误\n");
						return -__LINE__;
					}
					break;
			case 'h':
					util_programUsage(argv[0]);
					return -__LINE__;
			case 'f':
					server.slabfactor = atof(optarg);
					if(server.slabfactor <= 1.0){
						fprintf(stderr, "增长因子不能小于 1\n");
						return -__LINE__;
					}
					break;
			case 'b':
					server.slabbase	= atoi(optarg);
					if(server.slabbase == 0){
						fprintf(stderr, "内存最小槽大小必须大于 0\n");
						return -__LINE__;
					}
					break;
			case 't':
					server.servicethreads = atoi(optarg);
					if(server.servicethreads <= 0){
						fprintf(stderr, "服务线程数必须大于 0\n");
						return -__LINE__;
					} else if (server.servicethreads > 64){
						fprintf(stderr, "[WARNING] 设置过多的服务线程并没有好处, "
								"推荐设置服务线程数和机器 cpu 数量一致.\n");
					}
					break;
			case 'M':
					server.masterhost = sys_malloc(ADDR_MAX_LEN);
					memcpy(server.masterhost, optarg, strlen(optarg));
					isslave = 1;
					break;
			case 'P': 
					server.masterport = atoi(optarg);
					isslave = 1;  
					break;
			default:
					util_programUsage(argv[1]);
					return -__LINE__;
		}
	}
	if (isslave){
		if (server.masterhost && server.masterport) return 0;	
		fprintf(stderr, "[WARNING] -M 与 -P 选项必须同时使用才有效\n");
		return -__LINE__;
	}
	return 0;
}
