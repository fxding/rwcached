#include "rwcached.h"
#include "repl.h"
#include "adlist.h"
#include "network.h"
#include "protocol.h"
#include "db.h"
#include "xmem.h"
#include "dict.h"
#include "anet.h"

#include <time.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>

extern serverContext server;

void sendDBToSlave(eventloop *el, int fd, void *data, int mask){
	clientContext *slave = data;
	char buf[SENDBUF_LEN];
	int buflen = 0;

	if (slave->repl_dboff == 0){ // 如果是第一次发送则下发送头部信息
		response_header res;
		res.version = PROTOCOL_VERSION;
		res.opcode  = PKT_CMD_SYNC;
		res.status  = htons(RESPONSE_SUCCESS);
		// todo 发送头部信息
		res.bodylen = htonl((uint32_t) slave->repl_dbsize);
		res.kvcount = htonl((uint32_t) server.repl_dbkvcount);
		if(write(fd, res.bytes, sizeof(response_header)) !=
				sizeof(response_header)){
			cc_freeClient(slave);
			return;
		}
		xlog(LOG_DEBUG, "send db: 向 fd=%d 发送头部信息, slave fd=%d\n", fd, slave->fd);
	}
	// 定位文件位置到上次发送的地方
	lseek(slave->repl_dbfd, slave->repl_dboff, SEEK_SET);
	// 读取 db 文件数据
	buflen = read(slave->repl_dbfd, buf, SENDBUF_LEN);
	if (buflen < 0){
		xlog(LOG_WARN, "send db: 发送 db 发生错误 %s\n",
				buflen==0?"":strerror(errno));
		cc_freeClient(slave);
		return;
	}
	int nbytes=0;
	// 向 slave 写 db 数据
	if ((nbytes = write(fd, buf, buflen))==-1){
		xlog(LOG_WARN, "send db: 发送 db 错误 %s\n", strerror(errno));
		cc_freeClient(slave);
		return;
	}
	xlog(LOG_DEBUG, "send db: 向 fd=%d 发送 db 信息, slave fd=%d\n", fd, slave->fd);
	// 更新发送位置
	slave->repl_dboff += nbytes;
	// 检测是否发送完成
	if (slave->repl_dboff == slave->repl_dbsize){
		close(slave->repl_dbfd);
		slave->repl_dbfd = -1;
		multi_deleteFileEvent(server.el, fd, MULTI_WRITABLE);
		slave->repl_state = REPL_ONLINE;
		if (multi_createFileEvent(server.el, fd, MULTI_WRITABLE,
					writeDataToClient, slave) < 0){
			cc_freeClient(slave);
			return;
		}
		xlog(LOG_INFO, "send db: 发送 db 文件完成\n");
	}
}

void updateSlavesWaitingBgsave(int bgsaveerr){
	listNode *ln;
	int startbgsave = 0;
	listIter li;

	listRewind(server.slaves, &li);
	while((ln=listNext(&li))){
		clientContext *slave = ln->value;
		if (slave->repl_state == REPL_WAIT_BGSAVE_START){
			startbgsave = 1;
			slave->repl_state = REPL_WAIT_BGSAVE_END;
		} else if (slave->repl_state == REPL_WAIT_BGSAVE_END) {
			if (bgsaveerr != 0){
				cc_freeClient(slave);
				xlog(LOG_WARN, "replication: 数据同步失败, db 保存子进程返回错误\n");
				continue;
			}
			struct stat buf;
			if ( ((slave->repl_dbfd=open(server.dbfilename, O_RDONLY)) == -1) ||
				 (fstat(slave->repl_dbfd, &buf)==-1))
			{
				cc_freeClient(slave);
				xlog(LOG_WARN, "replication: db 文件打开失败\n");
				continue;
			}
			slave->repl_dboff  = 0;
			slave->repl_dbsize = buf.st_size;
			slave->repl_state  = REPL_SEND_DB;

			if (multi_deleteFileEvent(server.el, slave->fd, MULTI_WRITABLE) < 0)
			{
				cc_freeClient(slave);
				continue;
			}
			// 添加发送 db 数据事件函数
			if (multi_createFileEvent(server.el, slave->fd, MULTI_WRITABLE,
						sendDBToSlave, slave) < 0){
				cc_freeClient(slave);
				continue;
			}
		}

	}

	if (startbgsave){
		if (db_bgsave(server.dbfilename) != 0){
			listIter li;
			listRewind(server.slaves, &li);
			xlog(LOG_WARN, "bgsave: db 保存失败\n");
			while((ln=listNext(&li))){
				clientContext *slave = ln->value;
				if (slave->repl_state == REPL_WAIT_BGSAVE_END){
					cc_freeClient(slave);
				}
			}
		}
	}
}

int repl_slaveof(char *host, int port){

	// 如果 host 为空则表示清除 master 关系
	if(host == NULL){
		if(server.masterhost) sys_free(server.masterhost);
		server.masterhost = NULL;
		server.masterport = -1;
		if (server.master)cc_freeClient(server.master);
		if (server.repl_state == REPL_TRANSFER){
			multi_deleteFileEvent(server.el, server.repl_transfer_s, MULTI_READABLE);
			close(server.repl_transfer_s);
			close(server.repl_transfer_fd);
			unlink(server.repl_transfer_tmpfile);
			server.repl_state = REPL_NONE;
		} else if (server.repl_state == REPL_CONNECTING){
			multi_deleteFileEvent(server.el, server.repl_transfer_s, MULTI_WRITABLE|MULTI_READABLE);
			close(server.repl_transfer_s);
			server.repl_state = REPL_NONE;
		}
	// host 不为空, 则根据 host 连接 master
	} else if (host!=NULL && port!=0){
		server.masterhost = sys_malloc(ADDR_MAX_LEN);
		memcpy(server.masterhost, host, ADDR_MAX_LEN);
		server.masterport = port;
		if(server.master) cc_freeClient(server.master);
		listNode *ln;
		// 若存在 slave 则和slave断开连接
		while(listLength(server.slaves)) {
			ln = listFirst(server.slaves);
			cc_freeClient((clientContext*)ln->value);
		}
		// 若正在和某个 master 传输 db 数据, 则终止传输
		// 删除 db 文件
		if (server.repl_state == REPL_TRANSFER){
			multi_deleteFileEvent(server.el, server.repl_transfer_s, MULTI_READABLE);
			close(server.repl_transfer_s);
			close(server.repl_transfer_fd);
			unlink(server.repl_transfer_tmpfile);
			server.repl_state = REPL_NONE;
		}
		// 置 server 状态为需要与 master 建立连接状态
		server.repl_state = REPL_CONNECT;
		xlog(LOG_INFO, "slave connect: slave 创建 master ip=%s, port=%d\n",
				server.masterhost, server.masterport);
	} else {
		// 参数错误
		return -1;
	}
	return 0;
}

/* 同步读取 master 发送的 db 数据 */
void repl_syncReadDB(eventloop* el, int fd, void *data, int mask){
	clientContext *cc = (clientContext*)data;
	size_t nbytes;
	int off = 0;

	// 读取 master 传输的 db 数据
	nbytes = recv(fd, cc->recvbuf+cc->bytesrecved, RECVBUF_LEN-cc->bytesrecved, 0);
	if (nbytes == -1){
		if (errno==EAGAIN || errno==EINTR){
			 nbytes = 0;
		} else {
		     xlog(LOG_DEBUG, "server recv: %s\n", "数据接收失败");
			 cc_freeClient(cc);
			 goto werr;
		}
	} else if (nbytes == 0){
		cc_freeClient(cc);
	    goto werr;
	}
	// 设置接收到的字节数
	cc->bytesrecved += nbytes;
	// 第一次获得传送 db 的大小
	if (server.repl_dbsize == (uint32_t)-1){
		if(cc->bytesrecved < sizeof(request_header)) return;
		net_getHeader(cc);
		off = sizeof(request_header);
		server.repl_dbsize = cc->reqheader.bodylen;
		server.repl_dbkvcount   = cc->reqheader.kvcount;
		xlog(LOG_INFO, "db recv: 待同步 db 文件信息 kvcount=%d, dbsize=%d\n",
				server.repl_dbkvcount, server.repl_dbsize);
		memmove(cc->recvbuf, cc->recvbuf+sizeof(request_header),
				cc->bytesrecved-sizeof(request_header));
		cc->bytesrecved -= sizeof(request_header);
	}
	// 计算需要写到 db 文件中的数据大小, 避免将非 db 中的数据写到db文件中
	int nwrite = cc->bytesrecved<server.repl_dbsize?cc->bytesrecved:server.repl_dbsize;

	if (write(server.repl_transfer_fd, cc->recvbuf, nwrite) != nwrite){
		xlog(LOG_WARN, "sync db; 同步写 db 文件错误 %s\n", strerror(errno));
		cc_freeClient(cc);
		goto werr;
	}
	cc->bytesrecved    -= nwrite;
	memmove(cc->recvbuf, cc->recvbuf+nwrite, cc->bytesrecved);
	server.repl_dbsize -= nwrite;
	if(server.repl_dbsize == 0){ // db 文件同步完成
		if(rename(server.repl_transfer_tmpfile, server.dbfilename) < 0){
			xlog(LOG_WARN, "rename dbfile: 重命名 db 文件错误 %s\n", strerror(errno));
			cc_freeClient(cc);
			goto werr;
		}
		// empty dict
		dict_clear(server.db);

		multi_deleteFileEvent(server.el, cc->fd, MULTI_READABLE);
		// 加载 db 文件
		if (db_load(server.dbfilename) < 0){
			xlog(LOG_WARN, "db load: db 文件加载失败\n");
			cc_freeClient(cc);
			goto werr;
		}
		sys_free(server.repl_transfer_tmpfile);
		close(server.repl_transfer_fd);
		server.master = cc;
		server.master->flag |= RWCACHED_MASTER;
		server.repl_state = REPL_CONNECTED;
		if (multi_createFileEvent(server.el, cc->fd, MULTI_READABLE,
				readDataFromClient, cc) < 0){
			xlog(LOG_WARN, "add event: 创建同步读取事件失败\n");
			goto werr;
		}
		xlog(LOG_INFO, "master <-->slave 建立同步成功\n");
	}

	return;
werr:
	multi_deleteFileEvent(server.el, server.repl_transfer_s, MULTI_READABLE);
	close(server.repl_transfer_s);
	close(server.repl_transfer_fd);
	unlink(server.repl_transfer_tmpfile);
	sys_free(server.repl_transfer_tmpfile);
	server.repl_state = REPL_CONNECT;
	return;
}

static int repl_sendSyncRequst(struct timeval *tv, int fd){
	request_header sync;
	sync.version = PROTOCOL_VERSION;
	sync.opcode  = PKT_CMD_SYNC;
	sync.reserve = htons(0);
	sync.kvcount = htonl(0);
	sync.bodylen = htonl(0);

	fd_set wfds;
	int retval;
	FD_ZERO(&wfds);
	FD_SET(fd, &wfds);

	int nbytes = sizeof(request_header);
	int nwrite = 0;
	while(nbytes > 0){
		retval = select(fd+1, NULL, &wfds, NULL, tv);
		if(retval == -1){
			xlog(LOG_WARN, "write sync: 发送 sync 命令失败\n");
			return -__LINE__;
		} else if(retval){
			nwrite=write(fd, sync.bytes+nwrite, nbytes);
			if (nwrite > 0) nbytes -= nwrite;
		}
	}
	return 0;
}


// 当此函数被调用是, 说明已经和 master 建立了连接
void repl_syncWithMaster(eventloop* el, int fd, void *data, int mask){
	// 创建一个客户端连接
	clientContext *cc = cc_createClient(fd);
	if (cc == NULL) goto werr;
	// 连接上 master 删除, 读写事件
	multi_deleteFileEvent(server.el, fd, MULTI_WRITABLE|MULTI_READABLE);

	if(server.repl_state == REPL_NONE) {
		close(fd);
		return;
	}
	//todo
	//向 master 发送同步命令
	//select 阻塞等待 master 相应
	struct timeval tv;
	tv.tv_sec  = 1;
	tv.tv_usec = 0;
	if (repl_sendSyncRequst(&tv, fd) < 0) goto werr;

	int maxtries = 3;
	char tmpfilename[256];
	int dbfd;
	// 创建 db 保存的临时文件
	while(maxtries--){
		snprintf(tmpfilename, 256,"tmp-%d-%d.db", (int)getpid(), (int)time(NULL));
		dbfd = open(tmpfilename, O_CREAT|O_WRONLY|O_EXCL, 0644);
		if(dbfd != -1) break;
	}

	if (dbfd == -1){
		xlog(LOG_WARN, "create tmpfile: 创建 tmpfile 失败 %s\n", strerror(errno));
		goto werr;
	}
	// 创建 master 发送的 db 数据读取函数
	if (multi_createFileEvent(server.el, fd, MULTI_READABLE,
				repl_syncReadDB, cc) < 0){
		xlog(LOG_WARN, "add event: 创建读取 db 事件失败");
		goto werr;
	}
	// 更新 server 信息
	server.repl_state = REPL_TRANSFER;
	server.repl_transfer_fd = dbfd;
	server.repl_transfer_tmpfile = sys_malloc(256);
	memcpy(server.repl_transfer_tmpfile, tmpfilename, 256);
	return;
werr:
	server.repl_state = REPL_CONNECT;
	close(fd);
	return;
}

void  repl_cron(){
	// 检测 与 master 的连接状态, 若需要连接,
	// 则连接到 master, 进行数据的同步
	if(server.repl_state == REPL_CONNECT){
		xlog(LOG_INFO, "slave connect: 正在连接到 master\n");
		int fd;
		// 非阻塞连接到 master
		fd = anetTcpNonBlockConnect(NULL, server.masterhost, server.masterport);
		if(fd == -1){
			xlog(LOG_WARN, "slave connect; 不能连接到 master %s\n", strerror(errno));
		}
		// 检测是否建立连接
		if (multi_createFileEvent(server.el, fd, MULTI_WRITABLE|MULTI_READABLE,
					repl_syncWithMaster, NULL) < 0)
		{
			close(fd);
			xlog(LOG_WARN, "slave connect: 创建 slave 同步事件错误\n");
		}
		// 设置 server 状态为正在于 master 建立连接
		server.repl_transfer_s = fd;
		server.repl_state  = REPL_CONNECTING;
		server.repl_dbsize = -1;
	}
}
