#ifndef __SLABS_H__
#define __SLABS_H__
/*
 * 预分配内存管理文件
 * 内存空间将按照 size 的不同组织成为不同的 slab
 * 每种 slab 保持一种 size 大小的 trunk
 * 分配时, 以固定大小分配, 最佳适应方式
 *
 * */
#include "rwcached.h"



// 链表节点定义
struct node{
	struct node *next;
	char	data[];
};
typedef struct node listnode;

typedef struct{
	size_t chunksize;		// slab 中一个 chunk 的大小
	uint32_t chunkperbucket;	// 每个 bucket 中 chunk 数
	listnode *free;		// 空闲节点链表

	void **buckets;			// 指向分配的内存块列表
	uint32_t bucketcur;		// 已分配 bucket 数
	uint32_t bucketfree;	// slab 中的 bucket 数
	// slab pointer
	//uint32_t bucketcur;			// 当前内存的可分配 slab 位置
	//uint32_t bucketend;			// 当前内存块的结束 slab 位置
	// chunk pointer
	//uint32_t chunkcount;	// 一个 bucket 中的 chunk 数
	void *chunkcur;			// 当前 bucket 中 chunk 可分配位置
	uint32_t chunkfree;		// 当前 bucket 中 可用 chunk 数
}slab;

typedef struct{
	uint32_t size;		//	预分配内存大小

	void *membase;		// 其实位置
	void *memcur;		// 当前位置
	void *memend;		// 结束为止

	// 分块类别
	slab *slabcls;			// 数组
	uint32_t slabclscount;	// 类别个数

}premem_t;

int slab_init(size_t presize, size_t basesize, double factor);

int slab_getSlabIndex(size_t size);

void* slab_getFromFreelist(int slabi);

void* slab_getFromSlab(int slabi);

int slab_chunkFree(void* ptr);


#endif // __SLABS_H__
