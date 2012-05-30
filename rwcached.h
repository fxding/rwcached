#ifndef __RWCACHED_H__
#define __RWCACHED_H__


#include <string.h>
#include <malloc.h>
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <sys/types.h>
#include <unistd.h>

#include "dict.h"
#include "multi.h"
#include "network.h"
#include "adlist.h"
#include "utils.h"

/* 内存管理 */
#define SLABCLS_MAX_SIZE 250
#define SLAB_BUCKET_SIZE (1024*1024)  // 1 MB
#define SLABCLS_BASE_SIZE 60


/* 数据存储 dict */
#define DB_MIN_SIZE 4
#define DB_MAX_SIZE 4294967295UL
#define DICT_RESIZE_ENABLE 1
#define DICT_RESIZE_RATIO 3
#define DICT_VALUE_MAX_SIZE (1024*16)
/* hash 函数相关定义 */

#define hash(data, len) SuperFastHash(data, len)


/* 网络 */
#define ADDR_MAX_LEN 32

/* 日志级别*/
#define LOG_DEBUG 0 // 调试信息
#define LOG_INFO  1 // 流水信息
#define LOG_WARN  2 // 警告信息
#define LOG_ERR 3 // 错误信息
#define LOG_FATAL 4 // 致命错误信息
#define MAX_LOGMSG_LEN 1024
/* slave-->master */



typedef struct serverContext{
	int sfd;
	
	uint32_t dirty;			/* 系统当前的脏数据数量 */
	uint32_t dirty_to_save;		/* 设置的当有多少个脏数据是开始备份 */
	uint32_t dirty_before_save; /* 开始备份前脏数据数量, 保存以备数据保存时出错 */
	time_t	systemtime;
	// 配置信息
	int		daemonize;		/* 以守护程序启动 */
	size_t	memlimited;		/* 预分配内存最大值 MB*/
	double	slabfactor;		/* slab 增长因子 */
	size_t	slabbase;		/* 最小 slab 大小 */
	int		servicethreads; /* 开启的服务线程数 */

	// 主从相关
	list	*slaves;        /* 当做为 master 时, slave 列表*/
	pid_t	bgsavepid;		/* 保存内存数据的子进程 pid */
	//int		master_sock;	/* 当作为 slave 时, master 的连接套接字 */
	int		repl_dbfd;		/* 内存 db 文件的描述符 */
	char	*dbfilename;	/* db 文件的名字, 将和进程号组成 db 文件名*/
	uint32_t repl_dbkvcount;
	// as slave
	char	*masterhost;	/* 当作为 slave 时, master 的 ip */
	int		masterport;		/* master 端口 */
	clientContext *master;	/* master 连接 */
	int		repl_state;		/* 与 master 的连接状态 */
	int		repl_transfer_s;	/* 与 master 的传输描述符 */
	int		repl_transfer_fd;	/* 传输的 db 文件描述符*/
	char*	repl_transfer_tmpfile;	/* 同步传输 db 时, 创建的 tmp 文件名*/
	uint32_t repl_dbsize;	/* 同步传输时, db 文件的大小 */

	int		port;				/* 作为服务程序时, 监听的端口 */
	char	ip[ADDR_MAX_LEN];	/* 本机 ip */

	struct dict *db;		/* 保存数据的 数据字典 */
	// 多连接相关
	struct timeval	timeout;	/* 事件循环时, 超时时间 */
	eventloop		*el;		/* 事件管理结构 */
	clientContext	*freecltlist;	/* 释放后的连接,上下文结构, 保存供新建立连接使用 */
	// 日志相关
	char	*logfile;	/* 日志文件名称 */
	int		loglevel;	/* 日志记录级别 */
}serverContext;

extern  serverContext server;


void xlog(int level, const char *fmt, ...);


#endif // __RWCACHED_H__
