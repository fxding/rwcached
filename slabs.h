/*
 * 预分配内存管理文件
 * 内存空间将按照 size 的不同组织成为不同的 slab
 * 每种 slab 保持一种 size 大小的 trunk 
 * 分配时, 以固定大小分配, 最佳适应方式
 *
 * */

// 链表节点定义
typedef struct listnode {
	struct listnode *next;
	void	data[];
};

typedef struct slab{
	uint32_t slabsize;		// slab 大小
	listnode *next;			// 空闲节点链表
	
	void **slablist;		// 指向分配的内存空间列表
	void *slabcur;			// 当前分配位置

	
};

typedef struct slabclass{
	uint32_t classcount;		// slabclass 的种类
	slab **slabs;
} 


typedef struct premem{
	uint32_t size;		//	预分配内存大小

	void *membase;		// 其实位置
	void *memcur;		// 当前位置
	void *memend;		// 结束为止
	
	// 分块类别
	slabclass *slabcls;
};
