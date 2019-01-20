/*
 * mm_alloc.c
 *
 * Stub implementations of the mm_* routines.
 */

#include "mm_alloc.h"
#include <stdlib.h>

#define BLOCKSIZE sizeof(block_t)

block_t* first = NULL;
block_t* last = NULL;

void *mm_malloc(size_t size) {
    if (!size) {
        return NULL;
    }
    if (!first) {
        block_t* tmp_alloc = sbrk(BLOCKSIZE + size);
        if (tmp_alloc == (void*) -1) {
            perror("Error allocate new block with sbrk.");
            return NULL;
        }
        tmp_alloc->size = size;
        tmp_alloc->free = 0;
        tmp_alloc->pre = NULL;
        tmp_alloc->next = NULL;
        first = tmp_alloc;
        last = tmp_alloc;
        memset(tmp_alloc->pointer, 0, size);
        return tmp_alloc->pointer;
    } else {
        block_t* tmp = first;
        while(tmp) {
            if (tmp->free && tmp->size >= size) {
                if (tmp->size - size > BLOCKSIZE) {
                    tmp->free = 0;
                    tmp->size = size;
                    // void pointer calculate by bytes for pointer addition and subtraction.
                    block_t* nextblock = memset(tmp->pointer, 0, size) + size;
                    nextblock->size = tmp->size - size - BLOCKSIZE;
                    nextblock->free = 1;
                    nextblock->pre = tmp;
                    nextblock->next = tmp->next;
                    if (nextblock->next) {
                        nextblock->next->pre = nextblock;
                    }
                    tmp->next = nextblock;
                    return tmp->pointer;
                } else {
                    tmp->free = 0;
                    tmp->size = size;
                    memset(tmp->pointer, 0, size) + size;
                    return tmp->pointer;
                }
            }
            tmp = tmp->next;
        }
        block_t* tmp_alloc = sbrk(BLOCKSIZE + size);
        if (tmp_alloc == (void*) -1) {
            perror("Error allocate new block with sbrk.");
            return NULL;
        }
        tmp_alloc->size = size;
        tmp_alloc->free = 0;
        tmp_alloc->pre = last;
        tmp_alloc->next = NULL;
        last->next = tmp_alloc;
        last = tmp_alloc;
        memset(tmp_alloc->pointer, 0, size);
        return tmp_alloc->pointer;
    }

    return NULL;
}

void *mm_realloc(void *ptr, size_t size) {
    if (ptr) {
        if (!size) {
            mm_free(ptr);
            return NULL;
        } else {
            void* malloc = mm_malloc(size);
            if (!malloc) {
                return NULL;
            }
            block_t* meta_malloc = (block_t*) (malloc - BLOCKSIZE);
            block_t* meta_ptr = (block_t*) (ptr - BLOCKSIZE);
            if (meta_malloc->size >= meta_ptr->size) {
                memcpy(meta_malloc->pointer, ptr, meta_ptr->size);
            } else {
                memcpy(meta_malloc->pointer, ptr, meta_malloc->size);
            }
            mm_free(ptr);
            return meta_malloc->pointer;
        }
    } else {
        if (!size) {
            return NULL;
        }
        return mm_malloc(size);
    }
    return NULL;
}

block_t* merge(block_t* merge_pre, block_t* merge_next) {
    merge_pre->next = merge_next->next;
    if (merge_pre->next) {
        merge_pre->next->pre = merge_pre;
    }
    merge_pre->size = merge_pre->size + merge_next->size;
    if (last == merge_next) {
        last = merge_pre;
    }
    return merge_pre;
}

void mm_free(void *ptr) {
    if (!ptr) {
        return;
    }
    block_t* freeblock = ptr - BLOCKSIZE;
    freeblock->free = 1;
    if (freeblock->pre != NULL && freeblock->pre->free) {
        freeblock = merge(freeblock->pre, freeblock);
    }
    if (freeblock->next != NULL && freeblock->next->free) {
        freeblock = merge(freeblock, freeblock->next);
    }
}
