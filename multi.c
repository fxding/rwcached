#include <unistd.h>
#include <errno.h>
#include <sys/time.h>

#include "rwcached.h"
#include "xmem.h"
/**
 *	创建多描述符管理结构
 * */
int multi_create(eventloop *el){

	el->mlt = sys_malloc(sizeof(multi));
	if (el->mlt == NULL) return -__LINE__;
	el->mlt->epfd = epoll_create(100);
	if (el->mlt->epfd == -1) return -__LINE__;
	return 0;
}
/*
 * 释放多描述符管理结构
 * */
void multi_free(eventloop *el){
	close(el->mlt->epfd);
	sys_free(el->mlt);
	el->mlt = NULL;
}
/*
 * 向多连接管理结构中添加一个 fd 事件
 * @el 事件管理结构
 * @fd 待添加的描述符
 * @mask 描述符事件
 * */
int multi_add(eventloop *el, int fd, int mask){
	struct epoll_event ee;

	int op = el->fevents[fd].mask == MULTI_NONE?
		EPOLL_CTL_ADD : EPOLL_CTL_MOD;

	ee.events = 0;
	mask |= el->fevents[fd].mask;
	if (mask & MULTI_READABLE) ee.events |= EPOLLIN;
	if (mask & MULTI_WRITABLE) ee.events |= EPOLLOUT;
	//ee.data.u64 = 0;
	ee.data.fd = fd;
	if (epoll_ctl(el->mlt->epfd, op, fd, &ee) == -1)
	{
		perror("epoll_ctl: conn_sock");
	//	return -__LINE__;
	}

	return 0;
}
/*
 * 删除描述符中的事件
 * @el 事件管理结构
 * @fd 待删除事件描述符
 * @delmask 待删除事件标志
 *
 * */
int multi_delete(eventloop *el, int fd, int delmask){
	struct epoll_event ee;
	int mask = el->fevents[fd].mask & (~delmask);

	ee.events = 0;
	if (mask & MULTI_READABLE) ee.events |= EPOLLIN;
	if (mask & MULTI_WRITABLE) ee.events |= EPOLLOUT;
	ee.data.u64 = 0;
	ee.data.fd = fd;
	if (mask != MULTI_NONE){
		epoll_ctl(el->mlt->epfd, EPOLL_CTL_MOD, fd, &ee);
	} else {
		epoll_ctl(el->mlt->epfd, EPOLL_CTL_DEL, fd, &ee);
	}

	return 0;
}
/*
 * 描述符事件轮询
 * @el 事件管理结构
 * @tvp timeout 事件
 *
 * */
int multi_poll(eventloop *el, struct timeval *tvp){

	int retval, numevents = 0;
	// 将时间转换为毫秒
	// -1 阻塞等待
	// 0 马上返回
	retval = epoll_wait(el->mlt->epfd, el->mlt->events, MULTI_MAX_EVENTS,
			tvp?(tvp->tv_sec*1000+tvp->tv_usec/1000) : -1);

	if(retval > 0){
		int j;

		numevents = retval;
		for(j=0; j<numevents; j++){
			int mask = 0;
			struct epoll_event *ee = el->mlt->events+j;

			if (ee->events & EPOLLIN) mask |= MULTI_READABLE;
			if (ee->events & EPOLLOUT) mask |= MULTI_WRITABLE;
			el->fired[j].fd = ee->data.fd;
			el->fired[j].mask = mask;
		}
	}
	return numevents;
}

eventloop *multi_createEventLoop(){
	eventloop *el = sys_malloc(sizeof(eventloop));
	if (!el) return NULL;
	// 初始换 eventloop
	memset(el, 0, sizeof(eventloop));
	// 创建多描述符管理结构
	if (multi_create(el) < 0){
		multi_free(el);
		return NULL;
	}
	return el;
}

/*
 * 创建一个描述符事件并添加到事件管理结构中
 * @el 事件管理结构
 * @fd 待添加描述符
 * @mask 添加描述符事件标志
 * @proc 描述符事件处理函数
 * */
int multi_createFileEvent(eventloop *el, int fd, int mask, multiFileProc *proc, void *clientdata){
	if (fd > MULTI_MAX_EVENTS) return -__LINE__;
	fileevent *fe = &el->fevents[fd];

	if (multi_add(el, fd, mask) < 0) return -__LINE__;

	fe->mask |= mask;
	if(mask & MULTI_READABLE) fe->rfileProc = proc;
	if (mask & MULTI_WRITABLE) fe->wfileProc = proc;
	fe->clientdata = clientdata;

	return 0;
}
/**
 * 删除描述符的事件
 * @el 事件管理结构
 * @fd 待删除事件描述符
 * @mask 待删除事件标志
 * */
int multi_deleteFileEvent(eventloop *el, int fd, int mask){
	if (fd > MULTI_MAX_EVENTS) return -__LINE__;
	fileevent *fe = &el->fevents[fd];

	if (fe->mask == MULTI_NONE) return 0;

	fe->mask &= ~mask;

	multi_delete(el, fd, mask);
	return 0;
}
