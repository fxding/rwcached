/* 
 * 分配出去的空间, 每个空间都有一个 len(数据长度), free(剩余空间)
 * struct { 
 * 	uint32_t len; 
 * 	uint32_t free; 
 * 	void 	 data[];
 * 	}
 */



#include "xmem.h"
#include "slabs.h"

#include <stdint.h>


static void *membase = NULL; // 预分配内存起始地址

static void *memcur = NULL; // 当前分配位置

static void *memend = NULL;

// 指定 size 内存分配, 不初始化
// 1. 先到 freelist 中查找
// 2. 分配一个新的 slab, 从中分配
// 3. 分配失败!
void* xmalloc(size_t size){
	uint32_t slabi; 
	void *ptr = NULL;

	// 获得请求 size 对应的slab位置
	slabi = getSlabIndex(size);
	// 查看 slab 中 freelist 是否存在可用空间
	ptr = getFromFreelist(slab[slabi]);
	if (ptr != NULL) return ptr;
	// freelist 未找到, slabi 中分配
	ptr = getFromSlab(slab[slabi]);
	// 返回
	return ptr;
}

// 空间申请并初始化
void *xcalloc(size_t size){
	void *ptr;
	// 分配一块内存
	ptr = xmalloc(size);
	// 初始化
	if (ptr) memset(ptr, 0, size);
	// 返回
	return ptr;
}

// 空间扩展
/*
 * 1. 空间变小: 可以在更小 size 的 slab 分配, 不可以
 * 2. 空间不变: 不处理
 * 3. 空间变大: 可以在更大 size 的 slab 分配, 不可以
 **/
void *xrealloc(void* ptr, size_t size){

	return NULL;
}






