#include "dict.h"

/*
 *	初始设置 hash table 的大小
 *	将会设置为一个 大于或等于 size 的 2 的倍数
 * */
int dict_init(dict *d, size_t size){
	size_t s = DB_MIN_SIZE;
	// hash 表初始大小计算
	while(1){
		if (s >= size) break;
		s *= 2;
	}
	// dict 初始化
	d->rehashidx = -1;
	d->ht[0].table = sys_calloc(s, sizeof(kvpair*));
	d->ht[0].size = s;
	d->ht[0].sizemask = d->ht[0].size - 1;
	d->ht[0].used = 0;

	d->ht[1].table = NULL;
	d->ht[1].size = 0;
	d->ht[1].sizemask = 0;
	d->ht[1].used = 0;

	d->resizeenable = DICT_RESIZE_ENABLE;
	d->resizeratio = DICT_RESIZE_RATIO;

	return 0;
}

static inline int pair_keySet(kvpair* kv, void *k, size_t klen){
	if(kv){
		kv->key = xmalloc(klen+1);
		if (kv->key){
			memcpy(kv->key, k, klen);
			kv->klen = klen;
			((char*)kv->key)[klen] = '\0';
			return 0;
		}
	}
	return -__LINE__;
}

static inline int pair_valSet(kvpair *kv, void *v, size_t vlen){
	if(kv){
		kv->value = xmalloc(vlen+1);
		if(kv->value){
			memcpy(kv->value, v, vlen);
			kv->vlen = vlen;
			((char*)kv->value)[vlen] = '\0';
			return 0;
		}
	}
	return -__LINE__;
}

static inline int pair_valFree(kvpair *kv){
	if(kv){
		xfree(kv->value);
		kv->value = NULL;
		kv->vlen = 0;
		return 0;
	}
	return -__LINE__;
}

static inline int pair_kvFree(kvpair *kv){
	if(kv){
		xfree(kv->value);
		xfree(kv->key);
		xfree(kv);
		kv = NULL;
		return 0;
	}
	return -__LINE__;
}

/*
 *	kv 中 k 的比较, 内存比较
 * */
int dict_keyComp(void *k1, size_t klen1, void *k2, size_t klen2){
	// len 不相等
	if( klen1 != klen2) return -1;
	// len 相等
	return memcmp(k1, k2, klen1);
}

int dict_expandIfNeed(dict *d){
	if(d->rehashidx >= 0){
		dict_rehash(d, 1);
		return 0;
	}

	if(d->resizeenable && (d->ht[0].used/d->ht[0].size >= d->resizeratio))
		dict_expand(d);
	return 0;
}

/*
 *	通过给定的 hash 值检测
 *	当 kidx 不为 null 时, 设置 kidx 所在 bucket index, 为 NULL 则不设置.
 *	根据 返回值是否为 NULL,, 可判断是否存在,
 *	当返回值 为 NULL 时, kidx 为 k 的可插入位置,
 *	当返回值不为 NULL 时, kidx 为 k 的存在位置.
 * */
kvpair *dict_keyExistChech(dict *d, void *k, size_t klen, int *kidx){
	uint32_t hv;
	int i, idx;
	kvpair *kv = NULL;
	// 扩展哈希表
	dict_expandIfNeed(d);
	// hash 值计算
	hv = hash(k, klen);
	// dict 查询: ht[0], ht[1]
	for (i = 0; i <= 1; i++) {

		idx = hv & d->ht[i].sizemask;
		printf("************************hv = %u, idx = %d***********************\n", hv, idx);
		if(kidx != NULL) *kidx = idx;		// 记录 key 所在 bucket index
		kv = d->ht[i].table[idx];
		while(kv){
			if(dict_keyComp(k,klen,kv->key, kv->klen) == 0){
				return kv;
			}
			kv = kv->next;
		}
		// ht 为空
		if(d->rehashidx < 0) return NULL;
	}
	return NULL;
}

/*
 *	添加一个 kv 对到 dict 中
 *	该 k 必须是 dict 中不存在的
 *
 * */
int dict_add(dict *d, void *k, size_t klen, void *v, size_t vlen){
	int idx;
	kvpair *kv = NULL;
	dictht *ht = NULL;

	// 检查是否存在该 key
	if( (kv=dict_keyExistChech(d, k, klen, &idx)) != NULL) return -__LINE__;
	// insert table
	kv = xmalloc(sizeof(kvpair));
	if (d->rehashidx < 0) ht = &d->ht[0];
	else	{			  ht = &d->ht[1];// printf("***********insert ht 1 ***********\n"); }
	}
	kv->next = ht->table[idx];
	ht->table[idx] = kv;
	// 设置 kv 值
	if(pair_keySet(kv, k, klen) < 0) return -__LINE__;
	if(pair_valSet(kv, v, vlen) < 0) return -__LINE__;
	ht->used++;
	return 0;
}

/*
 *	替换一个 k 对应的 v 值
 *	该 k 必须是 dict 中存在的
 * */
int dict_replace(dict *d,  void *k, size_t klen, void *v, size_t vlen){
	kvpair *kv = NULL;

	// 存在检测
	if ( (kv=dict_keyExistChech(d,k,klen, NULL)) == NULL) return -1;
	// 替换
	if(pair_valFree(kv) < 0) return -__LINE__;
	if(pair_valSet(kv, v, vlen) < 0) return -__LINE__;

	return 0;
}

/*
 *	设置一个 kv 对
 *	如果 k 存在则修改 v 值, 如果 k 不存在则添加
 *
 * */
int dict_set(dict *d, void *k, size_t klen, void *v, size_t vlen)
{
	// 替换
	if(dict_replace(d, k, klen, v, vlen) == 0) return 0;
	// 添加
	if(dict_add(d, k, klen, v, vlen) == 0) return 0;

	return -__LINE__;
}

kvpair* dict_get(dict *d, void *k, size_t klen){

	return dict_keyExistChech(d, k, klen, NULL);
}

/*
 *	删除一个存在的 kv
 *
 * */
int dict_delete(dict *d, void *k, size_t klen){
	kvpair *kv = NULL, *prekv = NULL;
	uint32_t hv;
	int idx, i;

	hv = hash(k, klen);
	for (i = 0; i <= 1; i++) {
		// index
		idx = d->ht[i].sizemask & hv;
		kv = d->ht[i].table[idx];
		prekv = NULL;

		while(kv){

			if (dict_keyComp(k, klen, kv->key, kv->klen) == 0){
				// delete
				if (prekv){
					prekv->next = kv->next;
				} else { // first node
					d->ht[i].table[idx] = kv->next;
				}
				if(pair_kvFree(kv) < 0) return -__LINE__;
				d->ht[i].used--;
				return 0;
			}
			prekv = kv;
			kv = kv->next;
		}
		if (d->rehashidx < 0) break;
	}
	return __LINE__;
}

/*
 *	dict hash 表 resize
 *	将 hash 表扩展到 size 大小, 总是将扩大后的 ht 设置到 ht[1]
 * */
int dict_resize(dict *d, size_t size){
	dictht nht;
	// 表空间申请
	nht.table		= sys_calloc(size, sizeof(kvpair*));
	if (nht.table == NULL) return -__LINE__;
	nht.size		= size;
	nht.sizemask	= size-1;
	nht.used		= 0;
	// 总是设置 ht[1]; = 复制操作
	d->ht[1]		= nht;

	return 0;
}

/*
 *	将 dict 扩大
 * */
int dict_expand(dict *d){
	size_t size;

	// 扩大 size 计算
	size = (d->ht[0].size)*2;
	// 大于最大 db 设置的虽大 size 则不进行扩展
	if(size > DB_MAX_SIZE) return __LINE__;
	// resize
	if (dict_resize(d, size) < 0) return -__LINE__;
	// 设置 rehash 状态
	d->rehashidx = 0;

	return 0;
}

/*
 *	将 ht[0] 中的 kv rehash 到 ht[1] 中
 *	每次进行 n 个 bucket 的 rehash
 *
 * */
int dict_rehash(dict *d, int n){
	while(n-- > 0){
		kvpair *kv = NULL, *nextkv;

		if(d->ht[0].used == 0){
			// 释放 ht[0]
			sys_free(d->ht[0].table);
			// 将扩展后的 ht[1] 赋给 ht[0]
			d->ht[0] = d->ht[1];
			d->rehashidx = -1;
			memset(&d->ht[1], 0, sizeof(dictht));
			//printf("rehash complete*********************************\n");
			return 0;
		}
		// 寻找 rehash bucket
		while(d->ht[0].table[d->rehashidx] == NULL) d->rehashidx++;
		kv = d->ht[0].table[d->rehashidx];
		// rehash bucket
		while(kv){
			int idx;

			nextkv = kv->next;
			printf("[rehash %d] kv-%x k-%s, v-%s, next-%x\n", d->rehashidx, kv,  kv->key, kv->value, nextkv);
			// idx
			idx = hash(kv->key, kv->klen) & d->ht[1].sizemask;
			// 插入
			kv->next = d->ht[1].table[idx];
			d->ht[1].table[idx] = kv;
			d->ht[1].used++;
			d->ht[0].used--;
			// next
			kv = nextkv;
		}
		d->ht[0].table[d->rehashidx] = NULL;
		d->rehashidx++;
	}
	return 0;
}

static void dict_print(dict* d){
	int i = 0, j = 0;
	kvpair **t = NULL;
	kvpair *kv;
	while(j<2){
		if(d->ht[j].table){
			t = d->ht[j].table;
			printf("table %d, size %d, used %d\n", j, d->ht[j].size, d->ht[j].used);
			for (i = 0; i < d->ht[j].size; i++) {
				kv = t[i];
				while(kv)
				{
					printf("kv: [%s, %d, %s, %d]\t", kv->key, kv->klen, kv->value, kv->vlen);
					printf("adddr :[kv-%x, k-%x, v-%x]\n", kv, kv->key, kv->value);
					kv = kv->next;
				}
			}
		}
		j++;
	}
}

// test
//#ifdef TEST_DICT

int main(){
	dict d;
	kvpair *kv = NULL;
	xmem_init(1024*1024*10, 1.25);
	dict_init(&d, 1);

	char op[100], key[1024], value[1024];
	int ret = 0;
	while(1){
		ret = -1;
		printf("intput : op  <key or value> :");
		scanf("%s", op);
		if(strcmp(op, "add") == 0){
			scanf("%s%s", key, value);
			printf("add [%s, %s]\n", key, value);
			ret = dict_add(&d, key, strlen(key), value, strlen(value));
		} else if (strcmp("get", op) == 0){
			scanf("%s", key);
			printf("get [%s]\n", key);
			kv = dict_get(&d, key, strlen(key));
			if(kv){
				printf("get: key %s, value %s\n", kv->key, kv->value);
				ret = 0;
			}
		} else if (strcmp("delete", op) == 0){
			scanf("%s", key);
			printf("delete [%s]\n", key);
			ret = dict_delete(&d, key, strlen(key));
		} else if (strcmp("set", op) == 0){
			scanf("%s%s", key, value);
			printf("set [%s, %s]\n", key, value);
			ret = dict_set(&d, key, strlen(key), value, strlen(value));
		} else if (strcmp("replace", op) == 0) {
			scanf("%s%s", key, value);
			printf("replace [%s, %s]\n", key, value);
			ret = dict_replace(&d,  key, strlen(key), value, strlen(value));
		} else if (strcmp("q", op) == 0){
			break;
		} else if (strcmp("p", op) == 0){
			dict_print(&d);
		} else {
			printf("intput error !\n");
		}
		if (!ret)printf("OK\n");
		else printf("ERROR\n");

	}
	return 0;
}

//#endif
