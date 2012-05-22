#include "rwcached.h"

static premem_t *mem = NULL;

typedef struct{
	size_t size;
	char  data[];
}chunk;

#define CHUNK_PREFIX_SIZE sizeof(chunk)

/*
 *	@presize 为预分配内存大小
 *	@basesize 为内存块类型分配最小大小
 *	@factor 为块类型大小增长因子
 * */
int slab_init(size_t presize, size_t basesize, double factor){
	// 创建 mem 上下文
	mem = sys_malloc(sizeof(premem_t));
	// 分配内存
	mem->membase = sys_malloc(presize);
	if (mem->membase == NULL) return -__LINE__;
	// 设置内存指针
	mem->memcur = mem->membase;
	mem->memend = ((void*)mem->membase) + presize;
	mem->size = presize;

	// slab class 设置
	int nslabcls  = SLABCLS_MAX_SIZE;
	mem->slabcls = sys_malloc(sizeof(slab)*nslabcls);
	memset(mem->slabcls, 0, sizeof(slab)*nslabcls);
	mem->slabclscount = nslabcls;


	int i = 0;
	// index 0 存放 kvpair
	mem->slabcls[0].chunksize = sizeof(kvpair)+CHUNK_PREFIX_SIZE;
	mem->slabcls[0].chunkperbucket = (SLAB_BUCKET_SIZE)/(mem->slabcls[0].chunksize);
	mem->slabcls[0].bucketcur = -1;
	size_t size = basesize;
	for(i=1; i<nslabcls-1 && size<=SLAB_BUCKET_SIZE/factor; i++){
		mem->slabcls[i].chunksize = size+CHUNK_PREFIX_SIZE;
		mem->slabcls[i].chunkperbucket = (SLAB_BUCKET_SIZE)/(mem->slabcls[i].chunksize);
		mem->slabcls[i].bucketcur = -1;
		size = size*factor;
		printf("chunksize:%d, chunkperbucket %d\n", mem->slabcls[i].chunksize-CHUNK_PREFIX_SIZE, mem->slabcls[i].chunkperbucket);
	}
	// 最后一个块类型设置为 SLAB_BUCKET_SIZE
	mem->slabcls[i].chunksize = SLAB_BUCKET_SIZE;
	mem->slabcls[i].chunkperbucket = SLAB_BUCKET_SIZE/SLAB_BUCKET_SIZE;
	mem->slabcls[i].bucketcur = -1;
	return 0;
}


/*
 * 从预分配内存中分配个 bucket
 *
 * */
static int slab_newBucket(int idx){

	slab *s = &mem->slabcls[idx];
	if(s->bucketfree <= 0)
	// slab 空间用完, resize
	{
		// bucket 数量计算
		uint32_t size = s->bucketcur + s->bucketfree;
		size = (size == 0)? 16 : 2*size;
		// bucket 分配
		void* newbuckets = realloc(s->buckets, size*sizeof(void*));
		if (newbuckets == NULL) return -1;
		// bucket 设置
		s->buckets = newbuckets;
		s->bucketfree = size - s->bucketcur;
	}
	//new bucket
	if (mem->memcur == NULL) return -1;
	if ( (mem->memcur+( (s->chunksize)*s->chunkperbucket))
			> mem->memend){
		fprintf(stderr, "[error] 预分配内存不足!\n");
		return -1;
	}
	s->buckets[s->bucketcur] = mem->memcur;
	mem->memcur += (s->chunksize)*s->chunkperbucket;
	printf("memcur %x new bucket %d ---> start[%x]\n",mem->memcur, s->bucketcur, s->buckets[s->bucketcur]);
	return 0;
}

// 通过 size 获得 slabclass id
int slab_getSlabIndex(size_t size){
	int i = 0;

	if (0 == size) return 0;
	// 查找合适的 slabclass
	while((size+CHUNK_PREFIX_SIZE) > mem->slabcls[i].chunksize){
		i++;
		if (i >= mem->slabclscount) return -1;
	}
	// todo 二分查找

	return i;
}

// 从 freelist 中分配
void *slab_getFromFreelist(int idx){

	void *ptr = NULL;
	slab *s = &mem->slabcls[idx];

	// freelist 为空
	if (s->free == NULL) return NULL;
	// 分配第一个节点
	ptr = s->free;
	s->free = s->free->next;
	// chunk
	((chunk*)ptr)->size = s->chunksize;
	ptr = ptr + sizeof(chunk);
	return ptr;
}

// 从 slab 中分配
void *slab_getFromSlab(int idx){
	void *ptr;
	slab *s = &mem->slabcls[idx];

	if (s->chunkfree <= 0){
		s->bucketcur++;
		// 分配新的 bucket 块
		if (slab_newBucket(idx) < 0) return NULL;
		s->chunkcur = s->buckets[s->bucketcur];
		s->chunkfree = s->chunkperbucket;
	}
	// 分配一个可用块
	ptr = s->chunkcur;
	// 移动到下一可用块
	// caddr_t 按字节计算
	s->chunkcur = ((void*)s->chunkcur)+s->chunksize;
	s->chunkfree--;
	// chunk
	((chunk*)ptr)->size = s->chunksize;
	ptr = ptr + sizeof(chunk);
	return ptr;
}

// 获得 chunk 大小
static size_t slab_getChunkSize(void* ptr){
	chunk *ch = (void*)(ptr-sizeof(chunk));
	return ch->size;
}
// 添加 chunk 到 freelist
static int slab_addFreeChunk(int idx, void *ptr){
	slab *s = &mem->slabcls[idx];
	// 减去头部信息
	ptr = (void*)(ptr - sizeof(chunk));
	// 添加到 freelist
	((listnode*)ptr)->next = s->free;
	s->free = ((listnode*)ptr);
	return 0;
}

// 将释放的 chunk 加入 slab 空闲列表
int slab_chunkFree(void* ptr){
	size_t size = 0;
	// 获得 chunk 大小
	size = slab_getChunkSize(ptr);
	if (size <= 0) return -1;
	// 获得 size 对应的 slab
	int idx = 0;
	size -= CHUNK_PREFIX_SIZE;
	idx = slab_getSlabIndex(size-CHUNK_PREFIX_SIZE);
	// 添加到 freelist
	slab_addFreeChunk(idx, ptr);

	return 0;
}
