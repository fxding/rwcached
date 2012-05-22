#ifndef __DICT_H__
#define __DICT_H__

#include "rwcached.h"
/*
 * key 字典表结构定义, 操作定义
 *
 * */

/*
 * key-value 对定义
 *
 * */

typedef struct kvpair{
	void *key;
	uint32_t klen;
	void *value;
	uint32_t vlen;
	struct kvpair *next;
}kvpair;

/*
 * hash 存储结构定义
 * */
typedef struct dictht{
	kvpair **table;
	size_t size;
	size_t sizemask;
	size_t used;
} dictht;


typedef struct dict{
	dictht ht[2];	// hash 表结构, 2 个 hash 表用于 hash 表的 rehash
	int rehashidx;	// 当前使用正在进行 rehash 的 table 索引
	int resizeenable;
	int resizeratio;
}dict;

#if 0
#define pair_keySet(kv, k, klen) do{\
	if(kv){\
		(kv)->key = xmalloc(klen);\
		memcpy((kv)->key, (k), (klen));\
		(kv)->klen = (klen);\
	}\
}while(0)

#define pair_valSet(kv, v, vlen) do{\
	if(kv){\
		(kv)->value = xmalloc((vlen)+1);\
		memcpy((kv)->value, (v), (vlen));\
		(kv)->vlen = (vlen);\
	((char*)(kv)->value)[vlen] = '\0';\
	}\
}while(0)

#define pair_valFree(kv) do{\
	if(kv){\
		xfree((kv)->value);\
		(kv)->value = NULL;\
		(kv)->vlen = 0;\
	}\
}while(0)

#define pair_kvFree(kv) do{\
	if(kv){\
		xfree((kv)->value);\
		xfree((kv)->key);\
		xfree(kv);\
		(kv) = NULL;\
	}\
}while(0)
#endif

int dict_init(dict *d, size_t size);
int dict_add(dict *d, void *k, size_t klen, void *v, size_t vlen);
int dict_set(dict *d, void *k, size_t klen, void *v, size_t vlen);
kvpair* dict_get(dict *d, void *k, size_t klen);
int dict_delete(dict *d, void *k, size_t klen);
int dict_replace(dict *d, void *k,size_t klen, void *v, size_t vlen);
int dict_expand(dict *d);
int dict_resize(dict *d, size_t s);
int dict_rehash(dict *d, int n);
int dict_keyComp(void *k1,size_t klen1, void *k2, size_t klen2);

#endif // __DICT_H__
