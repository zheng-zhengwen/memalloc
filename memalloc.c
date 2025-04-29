#include <unistd.h>    // 用于系统调用 sbrk
#include <string.h>    // memset, memcpy
#include <pthread.h>   // 线程锁
#include <stdio.h>     // 调试用 printf

// 强制内存对齐，16字节可以适应多数平台的 SIMD 要求
typedef char ALIGN[16];

/**
 * 内存块头部结构体（Header）：
 * 我们在每个分配给用户的内存块前面加一个 header，
 * 用于存储该块的元数据，如大小、是否空闲、链表指针等。
 *
 * 为什么用 union？
 * - 使用 ALIGN 强制结构体按 16 字节对齐，提高性能并防止未对齐访问
 */
union header {
    struct {
        size_t size;              // 实际数据大小（不含 header）
        unsigned is_free;         // 当前块是否已释放（1=是，0=否）
        union header *next;       // 链表结构：指向下一个内存块
    } s;
    ALIGN stub;                   // 对齐填充
};

typedef union header header_t;

/*** 全局内存块链表（简单的单链表） ***/
header_t *head = NULL, *tail = NULL;
pthread_mutex_t global_malloc_lock;  // 简单线程安全支持（加锁保护）

/**
 * 思路：查找是否存在一个足够大的空闲块可复用
 * 优点：避免重复向系统申请内存，降低内存碎片
 */
header_t *get_free_block(size_t size)
{
    header_t *curr = head;
    while(curr) {
        if (curr->s.is_free && curr->s.size >= size)
            return curr;  // 找到可用块
        curr = curr->s.next;
    }
    return NULL;  // 没有找到
}

/**
 * free() 思路：
 * 1. 释放逻辑是标记而非立即删除
 * 2. 如果该块是堆的末尾，还可以 shrink 内存，归还给操作系统
 * 3. 注意 sbrk 不是线程安全，所以这个 free 只是伪线程安全
 */
void free(void *block)
{
    header_t *header, *tmp;
    void *programbreak;

    if (!block)
        return;

    pthread_mutex_lock(&global_malloc_lock);

    header = (header_t*)block - 1;
    programbreak = sbrk(0);

    // 判断是否是最后一个块，且在堆顶位置（才能 shrink）
    if ((char*)block + header->s.size == programbreak) {
        if (head == tail) {
            head = tail = NULL;
        } else {
            tmp = head;
            while (tmp && tmp->s.next != tail)
                tmp = tmp->s.next;
            if (tmp) {
                tmp->s.next = NULL;
                tail = tmp;
            }
        }
        // 向 OS 归还内存
        sbrk(0 - header->s.size - sizeof(header_t));

        /**
         * ⚠️ 线程安全警告：
         * 如果另一个线程恰好在我们判断堆顶和释放之间调用了 sbrk，
         * 会造成释放错别人的内存的风险。
         */
        pthread_mutex_unlock(&global_malloc_lock);
        return;
    }

    // 非堆尾，只做标记
    header->s.is_free = 1;
    pthread_mutex_unlock(&global_malloc_lock);
}

/**
 * malloc() 思路：
 * 1. 优先复用已有的空闲块（避免频繁向系统申请）
 * 2. 如果没有空闲块，调用 sbrk 向系统申请新的内存空间
 * 3. 使用链表管理所有分配的块
 */
void *malloc(size_t size)
{
    size_t total_size;
    void *block;
    header_t *header;

    if (!size)
        return NULL;

    pthread_mutex_lock(&global_malloc_lock);

    // 尝试复用空闲块
    header = get_free_block(size);
    if (header) {
        header->s.is_free = 0;
        pthread_mutex_unlock(&global_malloc_lock);
        return (void*)(header + 1);  // 返回用户数据部分
    }

    // 没有空闲块，向 OS 申请
    total_size = sizeof(header_t) + size;
    block = sbrk(total_size);
    if (block == (void*) -1) {
        pthread_mutex_unlock(&global_malloc_lock);
        return NULL;
    }

    // 初始化新块的 header
    header = block;
    header->s.size = size;
    header->s.is_free = 0;
    header->s.next = NULL;

    // 链表更新
    if (!head)
        head = header;
    if (tail)
        tail->s.next = header;
    tail = header;

    pthread_mutex_unlock(&global_malloc_lock);
    return (void*)(header + 1);  // 返回 header 后面的地址
}

/**
 * calloc() 思路：
 * 1. 分配一块大小为 num * nsize 的内存
 * 2. 使用 memset 清零
 * 3. 注意整数溢出检查
 */
void *calloc(size_t num, size_t nsize)
{
    size_t size;
    void *block;

    if (!num || !nsize)
        return NULL;

    size = num * nsize;

    // 溢出检测：乘积反除判断是否正确
    if (nsize != size / num)
        return NULL;

    block = malloc(size);
    if (!block)
        return NULL;

    memset(block, 0, size);  // 清零
    return block;
}

/**
 * realloc() 思路：
 * 1. 如果原块够大，直接返回原指针
 * 2. 如果不够，申请新块 -> 拷贝数据 -> 释放旧块
 */
void *realloc(void *block, size_t size)
{
    header_t *header;
    void *ret;

    if (!block || !size)
        return malloc(size);  // 行为如 malloc

    header = (header_t*)block - 1;

    // 空间够用，直接返回
    if (header->s.size >= size)
        return block;

    // 申请新块并拷贝旧数据
    ret = malloc(size);
    if (ret) {
        memcpy(ret, block, header->s.size);
        free(block);
    }
    return ret;
}

/**
 * 调试函数：打印当前内存块链表状态
 * 可以用它来验证你的 malloc/free 是否工作正常
 */
void print_mem_list()
{
    header_t *curr = head;
    printf("head = %p, tail = %p \n", (void*)head, (void*)tail);
    while(curr) {
        printf("addr = %p, size = %zu, is_free=%u, next=%p\n",
            (void*)curr, curr->s.size, curr->s.is_free, (void*)curr->s.next);
        curr = curr->s.next;
    }
}
