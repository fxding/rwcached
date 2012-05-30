
#include "rwcached.h"
#include "anet.h"
#include "xmem.h"
#include "db.h"
#include "repl.h"
#include "utils.h"

#include <time.h>
#include <sys/time.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdlib.h>

serverContext server;

static int shutdown = 0;

static void setShutDownFlag(const int signo){
	xlog(LOG_DEBUG, "siganl : 收到 quit 信号量\n");
	if(SIGQUIT == signo){
		shutdown = 1;
	}
}

void daemonize(void) {
	    int fd;

	if (fork() != 0) exit(0); /* parent exits */
	setsid(); /* create a new session */

	/* Every output goes to /dev/null. If Redis is daemonized but
	 * the 'logfile' is set to 'stdout' in the configuration file
	 * it will not log at all. */
	if ((fd = open("/dev/null", O_RDWR, 0)) != -1) {
		dup2(fd, STDIN_FILENO);
		dup2(fd, STDOUT_FILENO);
		dup2(fd, STDERR_FILENO);
		if (fd > STDERR_FILENO) close(fd);
    }
}


void xlog(int level, const char *fmt, ...){
	const char *levelmap[] = {"DEBUG", "INFO", "WARN", "ERROR", "FATAL"};
	va_list ap;
	FILE *fp;
	char buf[64];
	char msg[MAX_LOGMSG_LEN];
	if (level < server.loglevel) return;

	time_t now = time(NULL);

	fp = (server.logfile == NULL)? stdout:fopen(server.logfile, "a");
	if(!fp) return;

	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);

	strftime(buf, sizeof(buf), "%d %b %H:%M:%S", localtime(&now));
	fprintf(fp, "[%d-%s] %s %s\n", (int)getpid(), levelmap[level], buf, msg);
	fflush(fp);

	if(server.logfile) fclose(fp);
}

void server_cron(){
	
	static int loops = 0;

	if(shutdown){
		shutdown = 0;
		xlog(LOG_INFO, "server: 收到 quit 信号, 服务关闭....\n");
		exit(0);
	}

	server.systemtime = time(NULL);
	// 内存数据保存
	if (server.bgsavepid == -1){
		if (server.dirty > server.dirty_to_save){
			xlog(LOG_INFO, "server bgsave .........dirty=%lu\n", server.dirty);
			db_bgsave(server.dbfilename);
		}
	} else {
		int childstat;
		pid_t pid;
		// 无阻塞检测子进程状态
		if ((pid = wait3(&childstat, WNOHANG, NULL)) != -1){
			if (pid == server.bgsavepid){
				db_bgsaveDoneHandler(childstat);
			}
			// todo
			// enable hash resize
			dict_enableResize(server.db);
		}
	}
	// 复制处理
	loops = (++loops) % 100;
	if(loops == 0) repl_cron();
}



int server_drive(){

	eventloop *el = server.el;
	if (server.masterhost != NULL)
		repl_slaveof(server.masterhost, server.masterport);
	for(;;){
		int j;
		//
		// todo 添加每次循环要做的事
		server_cron();
		//
		int numevents = 0;
		if ( (numevents = multi_poll(el, &server.timeout)) < 0) continue;

		for(j=0; j<numevents; j++){
			fileevent *fe = &el->fevents[el->fired[j].fd];
			int mask = el->fired[j].mask;
			int fd = el->fired[j].fd;

			if (mask & MULTI_READABLE){
				fe->rfileProc(el, fd, fe->clientdata, mask);
			} else if (mask & MULTI_WRITABLE){
				fe->wfileProc(el, fd, fe->clientdata, mask);
			}
		}

	}

}

void server_config(){

	server.logfile = NULL;
	server.loglevel = LOG_DEBUG;
	server.sfd = -1;
	server.db = NULL;
	server.el = NULL;
	server.freecltlist = NULL;
	server.timeout.tv_sec = 0;
	server.timeout.tv_usec = 0;
	server.systemtime = time(NULL);
	server.dirty = 0;
	server.dirty_to_save = 100;
	server.dirty_before_save = 0;
	server.bgsavepid = -1;
	
	server.daemonize = 0;
	server.memlimited = 64*1024*1024;
	server.slabfactor = 1.25;
	server.slabbase = 48;
	server.servicethreads = 1;

	server.dbfilename = "dump.db";
	server.port = 11234;
	strcpy(server.ip, "127.0.0.1");
	server.master = NULL;
	server.masterhost = NULL;
	server.masterport = 0;
	server.repl_dbkvcount = 0;
	server.repl_dbfd = -1;
	server.repl_state = -1;
	server.repl_transfer_s = -1;
	server.repl_transfer_fd = -1;
	server.repl_transfer_tmpfile = NULL;
	server.repl_dbsize = -1;

	server.slaves = listCreate();
}

int server_init(){
	char err[MAX_LOGMSG_LEN];	
	// 预分配内存初始化
	if(xmem_init(server.memlimited, server.slabbase, 
				server.slabfactor)){
		xlog(LOG_WARN, "malloc: 预分配内存失败 %u MB \n", server.memlimited/(1024*1024));
		return -__LINE__;
	}
	// 数据库创建
	server.db = sys_malloc(sizeof(dict));
	if(server.db == NULL){
		xlog(LOG_WARN, "malloc: 字典空间分配失败\n");
		return -__LINE__;
	}

	if(dict_init(server.db, 32) < 0){
		xlog(LOG_WARN, "dict init: 字典初始化失败\n");
	}
	// 服务器 描述符创建
	server.sfd = anetTcpServer(err, server.port, server.ip);
	if (server.sfd == ANET_ERR){
		xlog(LOG_ERR, "server %s, %s\n", "套接字创建失败!", err);
		return -__LINE__;
	}
	// 创建多连接管理
	if ( (server.el=multi_createEventLoop()) == NULL){
		xlog(LOG_ERR, "server %s\n", "多连接管理创建失败!");
		return -__LINE__;
	}
	// 创建服务器连接接收处理函数
	if (multi_createFileEvent(server.el, server.sfd, MULTI_READABLE,
				cc_acceptClient, NULL) < 0){
		xlog(LOG_ERR, "server %s\n", "连接接收处理函数设置失败!");
		return -__LINE__;
	}
	if (server.daemonize) daemonize();
	
	shutdown = 0;
	signal(SIGHUP, SIG_IGN);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGQUIT, setShutDownFlag);
	
	return 0;
}

int main(int argc, char *argv[]){
	const char *short_opts = "dhp:m:M:l:f:b:t:P:";
	
	server_config();
	
	if(util_getOptions(argc, argv, short_opts) < 0)
		return -1;

	if (server_init() < 0) return -1;

	server_drive();

	return 0;
}
