#ifndef __MULTI_H__
#define __MULTI_H__


#include <sys/epoll.h>


#define MULTI_NONE 0
#define MULTI_READABLE 1
#define MULTI_WRITABLE 2

#define MULTI_MAX_EVENTS 1024

struct eventloop;
/*
 * 描述符事件处理函数
 * @ el 多描述符管理
 * @ fd 事件描述符
 * @ clientdata 发生事件的 client 上下文
 * @ mask 事件标志
 *
 * */
typedef void multiFileProc(struct eventloop *el, int fd, void *clientdata, int mask);


typedef struct{
	int epfd;	// epoll 描述符
	struct epoll_event events[MULTI_MAX_EVENTS]; // 描述符事件结构
}multi;

typedef struct fileevent{
	int mask; // 事件标志
	multiFileProc *rfileProc; // 可读事件处理函数
	multiFileProc *wfileProc; // 可写事件处理函数
	void *clientdata;		// client 上下文
}fileevent;

typedef struct firedevent{
	int fd; // 有事件描述符
	int mask; // 发生事件
}firedfile;

typedef struct eventloop{
	multi *mlt;  // 多描述符管理
	firedfile fired[MULTI_MAX_EVENTS]; // 发生事件描述符信息, 用 fd 索引
	fileevent fevents[MULTI_MAX_EVENTS]; // 发生事件 fd 的处理信息
}eventloop;





int multi_create(eventloop *el);

int multi_add(eventloop *el, int fd, int mask);

int multi_delete(eventloop *el, int fd, int mask);

int multi_poll(eventloop *el, struct timeval *tvp);

void multi_free(eventloop *el);

eventloop * multi_createEventLoop();

int multi_createFileEvent(eventloop *el, int fd, int mask,
		multiFileProc *proc, void *clientdata);

int multi_deleteFileEvent(eventloop *el, int fd, int mask);




#endif // __MULTI_H__
