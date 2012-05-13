#include "slabs.h"



static void* membase = NULL;
// 当前分配为止
static void* memcur = NULL;
// 预分配内存结束位置
static void* memend = NULL;
// 保存每个 size 类的起始地址
static void* premem = NULL;

// 通过 size 获得 slabclass id
int getSlabindex(size_t size){
	int i = 0;

	if (0 == size) return 0;
	// 查找合适的 slabclass
	while(size > premem->slabclass[i]->size){
		i++;
		if (i >= premem->slabclasscount) return -1;
	}

	return i;
}

// 从 freelist 中分配
void *getFromFreelist(int slabid){

	void *ptr = NULL;
	// freelist 为空
	if (slabclass[slabid].freelist == NULL) return NULL;
	ptr = freelist->next


}
