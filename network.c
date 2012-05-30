
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <unistd.h>


#include "network.h"
#include "anet.h"
#include "rwcached.h"
#include "xmem.h"
#include "protocol.h"
#include "adlist.h"
#include "repl.h"
#include "db.h"

/* 定义包的解包打包函数 */

extern serverContext server;

int net_getHeader(clientContext *cc){
    // 获得头部性息
    memcpy(cc->reqheader.bytes, cc->recvbuf, sizeof(request_header));
    // 字节序转换
    cc->reqheader.bodylen = ntohl(cc->reqheader.bodylen);
    cc->reqheader.reserve = ntohs(cc->reqheader.reserve);
    cc->reqheader.kvcount = ntohl(cc->reqheader.kvcount);

    return 0;
}


void cc_freeClient(clientContext *cc){
    xlog(LOG_DEBUG, "freeclient: %s\n", "释放连接");
    multi_deleteFileEvent(server.el, cc->fd, MULTI_READABLE);
    multi_deleteFileEvent(server.el, cc->fd, MULTI_WRITABLE);
    if (cc->fd >= 0) close(cc->fd);
    cc->fd = -1;
	listRelease(cc->reply);
	if(cc->flag & RWCACHED_SLAVE){
		listNode *node = listSearchKey(server.slaves, cc);
		if(node) listDelNode(server.slaves, node);
	}
    // todo add to freelist; // lock
    cc->next = server.freecltlist;
    server.freecltlist = cc;
}


void net_addUpdateToSlaves(clientContext *cc){
	//int n = listLength(server.slaves);
	object *o, *tailvalue;
	listNode *ln;
	listIter li;

	int pktbytes = cc->reqheader.bodylen+sizeof(request_header);
	listRewind(server.slaves, &li);
	while((ln=listNext(&li))){
		clientContext *slave = ln->value;
		if (slave->repl_state == REPL_WAIT_BGSAVE_START) continue;
		// 获得reply list 尾节点
		if (listLast(slave->reply) != NULL){
			tailvalue = listNodeValue(listLast(slave->reply));
		} else tailvalue = NULL;
		// 优先考虑将发送数据放到发送缓冲中
		if (pktbytes <= SENDBUF_LEN-slave->bytestosend){
			memcpy(slave->sendbuf+slave->bytestosend, cc->recvbuf, pktbytes);
			slave->bytestosend += pktbytes;
		// 缓冲空间不足则放入缓冲链表中
		} else if (tailvalue==NULL || (SENDBUF_LEN-tailvalue->len)<pktbytes ){
			// 新建一个 object 保存信息
			o = sys_malloc(sizeof(object));
			if (o==NULL){
				xlog(LOG_WARN, "malloc: 内存分配失败 %s\n", strerror(errno));
				listDelNode(server.slaves, ln);
				cc_freeClient(slave);
				continue;
			}
			o->ptr = sys_malloc(SENDBUF_LEN);
			if(o->ptr == NULL){
				xlog(LOG_WARN, "malloc: 内存分配失败 %s\n", strerror(errno));
				listDelNode(server.slaves, ln);
				cc_freeClient(slave);
				continue;
			}
			o->refcount = 1;
			memcpy(o->ptr, cc->recvbuf, pktbytes);
			o->len = pktbytes;
			// 添加到list 中
			listAddNodeTail(slave->reply, o);
		} else {
			// 将数据添加到尾节点
			memcpy(tailvalue->ptr+tailvalue->len, cc->recvbuf, pktbytes);
			tailvalue->len += pktbytes;
		}
		if (multi_createFileEvent(server.el, slave->fd, MULTI_WRITABLE,
					writeDataToClient, slave) < 0){
			listDelNode(server.slaves, ln);
			cc_freeClient(slave);
		}
	}
}

void net_syncToSlaves(clientContext *cc){
	if (cc == NULL) return;
	char cmd = cc->reqheader.opcode;
	switch(cmd){
		case PKT_CMD_ADD:
		case PKT_CMD_SET:
		case PKT_CMD_REPLACE:
		case PKT_CMD_DELETE:
			net_addUpdateToSlaves(cc);
			break;
		default:
			xlog(LOG_DEBUG, "sync : 不需要同步的数据\n");
	}
}

void readDataFromClient(eventloop *el, int fd, void* data, int mask){
    clientContext *cc = (clientContext*)data;
    int nbytes = 0;

    nbytes = recv(fd, cc->recvbuf+cc->bytesrecved,
            RECVBUF_LEN-cc->bytesrecved, 0);
    if (nbytes == -1){
        if (errno==EAGAIN || errno==EINTR){
            nbytes = 0;
        } else {
            xlog(LOG_DEBUG, "server recv: %s\n", "数据接收失败");
            cc_freeClient(cc);
            return;
        }
    } else if (nbytes == 0){
        cc_freeClient(cc);
        return;
    }
    // 设置接收到的字节数
    cc->bytesrecved += nbytes;
    for(;;){
        int packetlen = 0;
        // 接收数据过短, 包头信息不全
        if (cc->bytesrecved < sizeof(request_header)) break;
        // 获得包头信息
        net_getHeader(cc);
        packetlen = sizeof(request_header)+cc->reqheader.bodylen;
        if (packetlen > RECVBUF_LEN){
            // 数据包过大
            cc_freeClient(cc);
            break;
        }
        if (cc->bytesrecved < packetlen){
            // 数据包信息不足, 继续接收
            break;
        }
        // 处理数据
        if(net_process(cc) < 0){
            cc_freeClient(cc);
            break;
        }
		// 同步 slaves
		//net_setHeader(cc);
		net_syncToSlaves(cc);
        // 处理完的数据
        cc->bytesrecved -= packetlen;
        if (cc->bytesrecved == 0) break;
        memmove(cc->recvbuf, cc->recvbuf+packetlen, cc->bytesrecved);
    }
    return;
}


void writeDataToClient(eventloop *el, int fd, void *data, int mask){
    int nbytes = 0;
	object *o;

    clientContext *cc = (clientContext*)data;
	while(cc->bytestosend>0 || listLength(cc->reply)){
		if (cc->bytestosend>0){
			if (cc->flag & RWCACHED_MASTER){
				// 如果该链接为 master , 由于是同步数据
				// 不向 master 发送数据到客户端失败.
				nbytes = cc->bytestosend;
			} else {
				nbytes = write(fd, cc->sendbuf, cc->bytestosend);
				xlog(LOG_DEBUG, "send buf: 发送缓冲数据 %d bytes\n", nbytes);
			}
			// 数据发送后处理
			if (nbytes <= 0) break;
			if (nbytes < cc->bytestosend){
				cc->bytestosend -= nbytes;
				memmove(cc->sendbuf, cc->sendbuf+nbytes, cc->bytestosend);
			} else{
				cc->bytestosend = 0;
			}
		} else {
			// 获得发送列表中第一个发送缓冲对象
			o = listNodeValue(listFirst(cc->reply));
			if (o->len==0){
				listDelNode(cc->reply, listFirst(cc->reply));
				continue;
			}
			if (cc->flag & RWCACHED_MASTER){
				nbytes = o->len;
			} else {
				// 发送缓冲对对象中的信息
				nbytes = write(fd, ((char*)o->ptr)+cc->sentlen,
						o->len-cc->sentlen);
				xlog(LOG_DEBUG, "send reply: 发送缓冲列表数据 %d bytes\n", nbytes);
				if (nbytes <= 0) break;
			}
			cc->sentlen += nbytes;
			if (cc->sentlen == o->len){
				listDelNode(cc->reply, listFirst(cc->reply));
				cc->sentlen = 0;
				xlog(LOG_DEBUG, "send reply: 发送缓冲列表数据完成\n");
			}
		}
	}

	if (nbytes < 0){
		if (errno==EAGAIN || errno==EINTR){
			nbytes = 0;
		} else {
			xlog(LOG_WARN, "send data: 发送数据到客户端失败 %s\n", strerror(errno));
			cc_freeClient(cc);

		}
	}
	if (cc->bytestosend==0 && listLength(cc->reply)==0){
		cc->sentlen = 0;
		multi_deleteFileEvent(server.el, fd, MULTI_WRITABLE);
		if (cc->flag & CLOSE_AFTER_REPLY){
			cc_freeClient(cc);
		}
	}
}

/* 创建一个 slave 添加到 server 的 slave 例表中 */
int cc_createSlave(clientContext *cc){
	clientContext *slave;
	//listNode *ln;


	//ln = listSearchKey(server.slaves, cc);

	if(cc->flag & RWCACHED_SLAVE){
		xlog(LOG_WARN, "add salve: 一个已经存在的 slave\n");
		return 0;
	}

	if (server.bgsavepid != -1){ // 存在子进程正在保存 db
		listNode *ln;
		listIter li;

		listRewind(server.slaves, &li);
		while((ln=listNext(&li))){ // 寻找一个正在等待 db 保存的 slave
			slave = ln->value;
			if(slave->repl_state == REPL_WAIT_BGSAVE_END) break;
		}
		if (ln){
			// 复制 slave 发送缓冲列表到新的 slave
			listRelease(cc->reply);
			cc->reply = listDup(slave->reply);
			cc->repl_state = REPL_WAIT_BGSAVE_END;
			xlog(LOG_INFO, "add slave: 等待 db 保存完成\n");
		} else {
			cc->repl_state = REPL_WAIT_BGSAVE_START;
			xlog(LOG_INFO, "add slave: 等待 db 保存开始\n");
		}
	} else {
		xlog(LOG_INFO, "add save: 开始保存 db\n");
		if (db_bgsave(server.dbfilename) < 0){
			xlog(LOG_WARN, "db save: 保存 db 子进程失败\n");
			return -__LINE__;
		}
		cc->repl_state = REPL_WAIT_BGSAVE_END;
	}
	xlog(LOG_DEBUG, "create slave: 创建一个 slave fd=%d\n", cc->fd);
	cc->bytestosend = 0;
	//if (cc->reply) listRelease(cc->reply);
	//cc->reply = listCreate();
	listSetFreeMethod(cc->reply, objFree);
	listSetDupMethod(cc->reply, objDup);
	cc->sentlen = 0;
	cc->repl_dbfd = 0;
	cc->repl_dboff = 0;
	cc->repl_dbsize = 0;
	cc->flag |= RWCACHED_SLAVE;
	listAddNodeTail(server.slaves, cc);
	return 0;
}



clientContext *cc_createClient(int fd){
    // todo freelist
    clientContext *cc;
    // 从空闲的客户端上下文
    if (server.freecltlist != NULL){ // lock
        xlog(LOG_DEBUG,"%s\n", " 从freelist 中分配 clientContext");
        cc = server.freecltlist;
        server.freecltlist = server.freecltlist->next;
    } else {
        // 从内存中分配
        xlog(LOG_DEBUG,"%s\n", " 从 内存 中分配 clientContext");
        cc = sys_malloc(sizeof(clientContext));
        if (cc == NULL){
            xlog(LOG_ERR, "malloc: %s\n", "客户端创建内存分配失败!");
            return NULL;
        }
    }
    // todo 设置客户端上下文信息
    memset(cc, 0, sizeof(clientContext));
    cc->fd = fd;
	cc->bytestosend = 0;
	cc->bytesrecved = 0;
	cc->flag = 0;
	cc->reply = listCreate();
	listSetFreeMethod(cc->reply, objFree);
	listSetDupMethod(cc->reply, objDup);
	cc->sentlen = 0;
	cc->repl_dbfd = -1;
	cc->repl_dboff = 0;
	cc->repl_dbsize = 0;
	cc->repl_state = REPL_NONE;

    anetNonBlock(NULL, cc->fd);
    anetTcpNoDelay(NULL, cc->fd);
    if (multi_createFileEvent(server.el, fd, MULTI_READABLE,
            readDataFromClient, cc) < 0){
        cc_freeClient(cc);
        xlog(LOG_WARN, "file event: %s\n", "连接事件添加失败!");
        return NULL;
    }
    return cc;
}

void cc_acceptClient(eventloop *el, int fd, void *data, int mask){
    int cport, cfd;
    char cip[128];

    cfd = anetTcpAccept("acceptClient", fd, cip, &cport);
    if (cfd == ANET_ERR){
        xlog(LOG_ERR, "server accept: %s\n", "客户端接收失败!");
        return;
    }
	xlog(LOG_DEBUG, "accept ; 收到一个 tcp 连接 ip=%s,port=%d\n", cip, cport);
    cc_createClient(cfd);
    return;
}

static void net_setReplayHead(clientContext *cc, response_header *res){

    res = (response_header*)cc->sendbuf;
    res->version = PROTOCOL_VERSION;
    res->opcode  = cc->reqheader.opcode;
    res->kvcount = htonl(res->kvcount);
    res->bodylen = htonl(res->bodylen);
    res->status  = htons(res->status);
}

int do_add(dict *d, void *key, size_t klen, void *value, size_t vlen){

    if (dict_add(d, key, klen, value, vlen) != 0){
        xlog(LOG_DEBUG, "dict add :%s, key=%s\n", "数据添加失败", (char*)key);
        return -__LINE__;
    }
	server.dirty++;
    return 0;
}

kvpair *do_get(dict *d, void *key, size_t klen){
    kvpair *kv = NULL;
    kv = dict_get(d, key, klen);

    if ( kv == NULL){
        xlog(LOG_DEBUG, "dict get: %s, key=%s\n", "数据查询失败", (char*)key);
        return NULL;
    }

    return kv;
}

int do_set( dict *d, void *key, size_t klen, void *value, size_t vlen){
    if (dict_set(d, key, klen, value, vlen) != 0){
        xlog(LOG_DEBUG, "dict set: %s, key=%s\n", "数据设置失败", (char*)key);
        return -__LINE__;
    }
	server.dirty++;
    return 0;
}

int do_replace( dict *d, void *key, size_t klen, void *value, size_t vlen){
    if(dict_replace(d, key, klen, value, vlen) != 0){
        xlog(LOG_DEBUG, "dict replace: %s, key=%s\n", "数据覆盖失败", (char*)key);
        return -__LINE__;
    }
	server.dirty++;
    return 0;
}

int do_delete(dict *d, void *key, size_t klen){
    if (dict_delete(d, key, klen) != 0){
        xlog(LOG_DEBUG, "dict delete: %s, key=%s\n", "数据删除失败", (char*)key);
        return -__LINE__;
    }
	server.dirty++;
    return 0;
}


int net_appendToBuf(clientContext *cc, void *data, size_t len){
    if (!data || !cc ) return -__LINE__;
	if (len > (SENDBUF_LEN-cc->bytestosend)) return -__LINE__;
	memcpy(cc->sendbuf+cc->bytestosend, data, len);
	cc->bytestosend += len;

    return 0;
}

int net_process(clientContext *cc){
    int kvcount          = cc->reqheader.kvcount;
    int pos              = sizeof(request_header);
    response_header *res = (response_header*)cc->sendbuf;
    res->status = RESPONSE_SUCCESS;
    res->bodylen= 0;
	res->kvcount= 0;
    cc->bytestosend = sizeof(response_header);
	kvpair *kv = NULL;

    // 包数据异常, 释放连接
    if(kvcount < 0){
        xlog(LOG_WARN, "packet: %s\n", "数据包数据错误");
        cc_freeClient(cc);
        return -__LINE__;
    }
	if (cc->reqheader.opcode == PKT_CMD_SYNC){
		cc_createSlave(cc);
		return 0;
	}
    while(kvcount){
        // 报数据处理
        size_t klen, vlen; void *key, *value;

        //klen  = (size_t)(cc->recvbuf+pos); // 键长度
		memcpy(&klen, cc->recvbuf+pos, sizeof(uint32_t));
		klen = ntohl(klen);
        pos  += sizeof(uint32_t);            // 位置后移
        key   = (void*)cc->recvbuf+pos;            // 键值
        pos  += klen;                        // 位置后移
        if(klen > DICT_VALUE_MAX_SIZE){
			xlog(LOG_WARN, "packet :%s", "数据包过大");
            res->status = RESPONSE_VALUE_2BIG;
            break;
        }
        uint8_t cmd = cc->reqheader.opcode;
        if (cmd==PKT_CMD_ADD||cmd==PKT_CMD_SET||cmd==PKT_CMD_REPLACE){
           // vlen  = (size_t)(cc->recvbuf+pos);        // 值长度
            memcpy(&vlen, cc->recvbuf+pos, sizeof(uint32_t));
			vlen  = ntohl(vlen);
            pos  += sizeof(uint32_t);                // 位置后移
            value = (void*) cc->recvbuf+pos;                // 值
            pos  += vlen;                            // 位置后移
            if (vlen > DICT_VALUE_MAX_SIZE){        // 值长度过大
				xlog(LOG_WARN, "packet :%s", "数据包过大");
                res->status = RESPONSE_VALUE_2BIG;
                break;
            }
        }
        int ret = -1;
        if (cmd == PKT_CMD_GET){
            kv = do_get(server.db, key, klen);
            if(kv != NULL){
                size_t tklen = htonl(kv->klen);
                size_t tvlen = htonl(kv->vlen);
                // 将查询到的键值加入到发送缓冲中去
                if (   (net_appendToBuf(cc, &tklen, sizeof(uint32_t)) < 0)
                    || (net_appendToBuf(cc, kv->key, kv->klen) < 0)
                    || (net_appendToBuf(cc, &tvlen, sizeof(uint32_t)) < 0)
                    || (net_appendToBuf(cc, kv->value, kv->vlen) < 0))
                {
                    res->status  = RESPONSE_REPLY_PKT_2BIG;
					xlog(LOG_WARN, "packet :%s", "回包过大");
                    break;
                }
                res->bodylen += 2*sizeof(uint32_t)+kv->klen+kv->vlen;
				res->kvcount++;
            } else;
        } else if (cmd == PKT_CMD_ADD){
            ret = do_add(server.db, key, klen, value, vlen);
            if (ret==0){
                // 返回操作成功的值
                size_t tmp = klen;
                tmp = htonl(klen);
                if (   (net_appendToBuf(cc, &tmp, sizeof(uint32_t)) < 0)
                     ||(net_appendToBuf(cc, key, klen) < 0))
                {
                    res->status = RESPONSE_REPLY_PKT_2BIG;
					xlog(LOG_WARN, "packet :%s", "回包过大");
                    break;
                }
                res->bodylen += sizeof(uint32_t)+klen;
				res->kvcount++;
            } else {};
        } else if (cmd== PKT_CMD_SET){
            ret = do_set(server.db, key, klen, value, vlen);
            if (ret==0){
                // 返回操作成功的值
                size_t tmp = klen;
                tmp = htonl(klen);
                if (   (net_appendToBuf(cc, &tmp, sizeof(uint32_t)) < 0)
                     ||(net_appendToBuf(cc, key, klen) < 0))
                {
                    res->status = RESPONSE_REPLY_PKT_2BIG;
					xlog(LOG_WARN, "packet :%s", "回包过大");
                    break;
                }
                res->bodylen += sizeof(uint32_t)+klen;
				res->kvcount++;
            } else{};
        } else if(cmd == PKT_CMD_REPLACE){
            ret = do_replace(server.db, key, klen, value, vlen);
            if (ret==0){
                // 返回操作成功的值
                size_t tmp = klen;
                tmp = htonl(klen);
                if (   (net_appendToBuf(cc, &tmp, sizeof(uint32_t)) < 0)
                     ||(net_appendToBuf(cc, key, klen) < 0))
                {
                    res->status = RESPONSE_REPLY_PKT_2BIG;
					xlog(LOG_WARN, "packet :%s", "回包过大");
                    break;
                }
                res->bodylen += sizeof(uint32_t)+klen;
				res->kvcount++;
			} else{};
        } else if(cmd == PKT_CMD_DELETE){
            ret = do_delete(server.db, key, klen);
            if (ret==0){
                // 返回操作成功的值
                size_t tmp = klen;
                tmp = htonl(klen);
                if (   (net_appendToBuf(cc, &tmp, sizeof(uint32_t)) < 0)
                     ||(net_appendToBuf(cc, key, klen) < 0))
                {
                    res->status = RESPONSE_REPLY_PKT_2BIG;
					xlog(LOG_WARN, "packet :%s", "回包过大");
                    break;
                }
                res->bodylen += sizeof(uint32_t)+klen;
				res->kvcount++;
			} else{};
        } else {
            res->status = RESPONSE_UNKNOW_CMD;
			xlog(LOG_WARN, "packet :%s", "未知命令");
            res->bodylen = 0;
            cc->flag = CLOSE_AFTER_REPLY;
            break;
        }
        kvcount--;
        ret = -1;
    }
    cc->bytestosend = res->bodylen+sizeof(response_header);
    net_setReplayHead(cc, res);
    // 添加 client 可写事件
    if (multi_createFileEvent(server.el, cc->fd, MULTI_WRITABLE,
               writeDataToClient, cc) < 0){
        xlog(LOG_WARN, "file event :%s\n", "连接写事件添加失败");
        cc_freeClient(cc);
        return -__LINE__;
    }
    return 0;
}
