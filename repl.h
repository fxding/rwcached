#ifndef __REPL_H__
#define __REPL_H__

#include "rwcached.h"
#include "multi.h"


#define REPL_CONNECT 1
#define REPL_CONNECTING 2
#define REPL_TRANSFER 3
#define REPL_CONNECTED 4


void sendDBToSlave(eventloop *el, int fd, void *data, int mask);

void updateSlavesWaitingBgsave(int bgsaveerr);

void repl_cron();

void repl_syncWithMaster(eventloop *el, int fd, void *data, int mask);

void repl_syncReadDB(eventloop *el, int fd, void *data, int mask);

int repl_slaveof(char *host, int port);


#endif // __REPL_H__
