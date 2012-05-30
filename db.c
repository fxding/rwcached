
#include "rwcached.h"
#include "db.h"
#include "repl.h"



#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <stdlib.h>

int db_save(char *filename){
	FILE *fp;
	char tmpfile[256];

	snprintf(tmpfile, 256, "tmp-%d.db", (int)getpid());
	fp = fopen(tmpfile, "w");
	if(fp == NULL){
		xlog(LOG_DEBUG, "dbsave: db 文件创建失败: %s\n", strerror(errno));
		return -__LINE__;
	}
//	if(fwrite("RWCACHED0001", 12, 1, fp)==0) goto werr;

	// 内存数据保存
	dict *d = server.db;
	int i = 0, j = 0;
	kvpair *kv, **t;
	uint32_t klen, vlen;
	char *ptr;
	while(i<2){
		if(d->ht[i].table){
			t = d->ht[i].table;
			for(j=0; j<	d->ht[i].size; j++){
				kv = t[j];
				while(kv){
					klen = htonl(kv->klen);
					vlen = htonl(kv->vlen);
					if (fwrite(&klen, sizeof(uint32_t), 1, fp)==0) goto werr;
					ptr = (char*)kv->key;
					if (fwrite(ptr, kv->klen, 1, fp)==0) goto werr;
					if (fwrite(&vlen, sizeof(uint32_t), 1, fp)==0) goto werr;
					ptr = (char*)kv->value;
					if (fwrite(ptr, kv->vlen, 1, fp)==0) goto werr;
					kv = kv->next;
				}
			}
		}
		i++;
	}
	fflush(fp);
	fsync(fileno(fp));
	fclose(fp);
	if(rename(tmpfile, filename) == -1){
		xlog(LOG_ERR, "bgsave: db 文件重命名失败 %s\n", strerror(errno));
		unlink(tmpfile);
		return -__LINE__;
	}
	xlog(LOG_INFO, "bgsave: db 文件保存成功\n");
	server.dirty = 0;
	// test
	// db_load(filename);
	return 0;
werr:
	fclose(fp);
	unlink(tmpfile);
	xlog(LOG_WARN, "bgsave: db 文件保存失败, %s\n", strerror(errno));
	return -__LINE__;
}

int db_load(char *dbfile){
	FILE *fp;

	fp = fopen(dbfile, "r");
	if(fp == NULL){
		errno = ENOENT;
		return -__LINE__;
	}
	size_t klen=0, vlen=0;
	char key[1024], value[1024];

	dict *d = server.db;
	uint32_t dbkvCount = server.repl_dbkvcount;
	while(dbkvCount > 0){
		if(fread(&klen, sizeof(uint32_t), 1, fp)==0) goto eoferr;
		klen = ntohl(klen);
		if(fread(key, klen, 1, fp)==0) goto eoferr;
		if(fread(&vlen, sizeof(uint32_t), 1, fp)==0) goto eoferr;
		vlen = ntohl(vlen);
		if(fread(value, vlen, 1, fp)==0) goto eoferr;
		dict_add(d, key, klen, value, vlen);
		dbkvCount--;
	}
	fclose(fp);
	return 0;

eoferr:
	xlog(LOG_ERR, "load db: db 加载失败退出\n");
	exit(-1);
}


int db_bgsave(char *filename){
	if (filename == NULL){
		xlog(LOG_DEBUG, "bgsave :%s\n", "保存文件名为空");
		return -__LINE__;
	}
	if (server.bgsavepid != -1){
		xlog(LOG_DEBUG, "bgsave :%s\n", "保存进程正在工作");
		return -__LINE__;
	}
	server.dirty_before_save = server.dirty;
	server.repl_dbkvcount = server.db->ht[0].used+server.db->ht[1].used;
	pid_t childpid = -1;
	if ( (childpid=fork()) == 0){
		/* child */
		if (server.sfd > 0) close(server.sfd);
		if (db_save(filename) == 0){
			_exit(0);
		} else {
			_exit(1);
		}
	} else {
		/* parent */
		if (childpid == -1){
			xlog(LOG_ERR, "bgsave: 内存保存进程创建失败 %s\n", strerror(errno));
			return -__LINE__;
		}
		server.bgsavepid = childpid;
		// todo
		// disable hash resize
		dict_disableResize(server.db);
	}
	return 0;
}

void db_bgsaveDoneHandler(int childstat){
	int exitcode = WEXITSTATUS(childstat);
	int bysignal = WIFSIGNALED(childstat);

	if (bysignal==0 && exitcode==0){
		xlog(LOG_INFO, "bgsave: 子进程 db 保存成功\n");
		server.dirty = server.dirty-server.dirty_before_save;
	} else if (bysignal==0 && exitcode!=0){
		xlog(LOG_WARN, "bgsave: 子进程 db 保存失败\n");
	} else {
		xlog(LOG_WARN, "bgsave: 子进程 db 保存被信号终止 %d\n", WTERMSIG(childstat));
		char tmpfile[256];
		snprintf(tmpfile, 256, "tmp-%d.db", (int)server.bgsavepid);
		unlink(tmpfile);
	}
	server.bgsavepid = -1;
	// 更新 slave 状态信息
	updateSlavesWaitingBgsave(exitcode==0?0:-1);
}
