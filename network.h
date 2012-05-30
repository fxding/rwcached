#ifndef __NETWORK_H__
#define __NETWORK_H__

#include "multi.h"
#include "protocol.h"
#include "adlist.h"

#define RECVBUF_LEN 1024
#define SENDBUF_LEN 1024


#define RWCACHED_MASTER 0x001
#define RWCACHED_SLAVE  0x002
#define CLOSE_AFTER_REPLY 0x004

#define REPL_NONE 0
#define REPL_WAIT_BGSAVE_START 1
#define REPL_WAIT_BGSAVE_END 2
#define REPL_SEND_DB 3
#define REPL_ONLINE 4


typedef struct clientContext{
	int fd;					/* 连接描述符 */
	int bytesrecved;		/* 接收到的字节数 */
	int bytestosend;		/* 待发送字节数 */
	int flag;				/* 客户端标志 */
	char recvbuf[RECVBUF_LEN];	/* 接收到的数据的缓冲区 */
	char sendbuf[SENDBUF_LEN];	/* 待发送数据的缓冲区 */

	/////////////////////////////
	// 当该链接为一个同步链接是使用
	list *reply;				/* 客户端回复缓冲链表 */
	int sentlen;				/* 已发送数据 */
	int repl_dbfd;				/* 内存文件描述度*/
	int repl_dboff;				/* 内存文件发送或读取偏移量*/
	off_t repl_dbsize;			/* 内存文件大小*/
	int repl_state;
	/////////////////////////////
	request_header reqheader;		/* 请求头部信息 */
	int pktlen;					/*用于判断一个包头大小, 接受一个完整的数据包*/

	////////////////////////////
	struct clientContext *next; /* 用于将空闲连接放入空闲连接表 */
}clientContext;



/* 对接收到的完整数据包进行解包并掉用相关的处理函数 */
int net_process(clientContext *cc);
/* 对回复客户端的数据进行 pack, 并拷贝到写缓冲中*/
//int net_addReplay();
/* 将客户端 fd 添加到多路复用管理中 */
//int net_addClienWritable();

int net_getHeader(clientContext *cc);
// 从客户端读取数据
void readDataFromClient(eventloop *el, int fd, void *data, int mask);

void writeDataToClient(eventloop *el, int fd, void *data, int mask);

clientContext * cc_createClient(int fd);

void cc_freeClient(clientContext *cc);

void cc_acceptClient(eventloop *el, int fd, void *data, int mask);

#endif // __NETWORK_H__
