#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your information in the following struct.
 ********************************************************/
team_t team = {
    /* Your student ID */
    "20201617",
    /* Your full name*/
    "Sangyeon Lee",
    /* Your email address */
    "sangyeone0163@gmail.com",
};

/* Basic constants and macros */
#define WSIZE               4                   /* Word and header/footer size (bytes) */
#define DSIZE               8                   /* Double word size (bytes) */
#define CHUNKSIZE           (1 << 10)           /* Extend heap by this amount (bytes) */
#define SEGLIST_CNT         20

#define MAX(x, y)           ((x) > (y) ? (x) : (y))
#define ALIGNMENT           8
#define ALIGN(size)         (((size) + (ALIGNMENT - 1)) & ~0x7)

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc)   ((size) | (alloc))

/* Read and write a word at address p (4 bytes) */
#define GET(p)              (*(unsigned int *)(p))
#define PUT(p, val)         (*(unsigned int *)(p) = (val))

/* Read the size and allocated fields from address p */
#define GET_SIZE(p)         (GET(p) & ~0x7)
#define GET_ALLOC(p)        (GET(p) & 0x1)

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp)            ((char *)(bp) - WSIZE)
#define FTRP(bp)            ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp)       ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp)       ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

/* In a free block, we use the first two words of payload to hold
 * pointers to the previous and next free blocks, respectively. */
#define PREV_PTR(bp)        ((char *)(bp))
#define NEXT_PTR(bp)        ((char *)(bp) + WSIZE)
#define PREV(bp)            (*(char **)(PREV_PTR(bp)))
#define NEXT(bp)            (*(char **)(NEXT_PTR(bp)))

/* Global variables */
static char **seg_free_lists; /* Array of pointers to segregated free lists */

/* Function prototypes for internal helper routines */
static void insert_node(void *bp, size_t size);
static void delete_node(void *bp);
static void *extend_heap(size_t words);
static void *coalesce(void *bp);
static void *find_fit(size_t asize);
static void *place(void *bp, size_t asize);
static inline int get_list_index(size_t size);

/*================================================================
 * get_list_index
 *
 * Determine the index of the segregated free list
 *================================================================*/
static inline int get_list_index(size_t size) {
    int idx = 0;
    while (idx < (SEGLIST_CNT - 1) && (size > 1)) {
        size >>= 1;
        idx++;
    }
    return idx;
}

/*================================================================
 * insert_node
 *
 *  Insert a free block `bp` of given `size` into the appropriate
 *  segregated free list (sorted in descending order by block size).
 *  We first determine which list index to use, then traverse that
 *  list until we find the correct spot so that larger‐blocks appear
 *  earlier (head) and smaller‐blocks appear later (tail).
 *================================================================*/
static void insert_node(void *bp, size_t size) {
    int idx = 0;
    size_t size_class = size;

    idx = get_list_index(size_class);

    /* Head of the chosen list */
    char *curr = seg_free_lists[idx];
    char *prev = NULL;

    /* Traverse until we find a block equal or bigger than `size` */
    while (curr && GET_SIZE(HDRP(curr)) < size) {
        prev = curr;
        curr = NEXT(curr);
    }

    /* Insert bp between prev and curr */
    PREV(bp) = prev;
    NEXT(bp) = curr;
    if (prev) {
        NEXT(prev) = bp;
    } else {
        /* Inserting at head of this list */
        seg_free_lists[idx] = bp;
    }
    
    if (curr) {
        PREV(curr) = bp;
    }
}

/*================================================================
 * delete_node
 *
 *  Remove block `bp` from its segregated free list. We recompute
 *  the index from the block’s size, then update prev/next pointers.
 *================================================================*/
static void delete_node(void *bp) {
    size_t size = GET_SIZE(HDRP(bp));
    int idx = 0;
    size_t size_class = size;

    idx = get_list_index(size_class);

    char *prev = PREV(bp);
    char *next = NEXT(bp);

    if (prev) {
        NEXT(prev) = next;
    } else {
        /* bp was head of this list */
        seg_free_lists[idx] = next;
    }

    if (next) {
        PREV(next) = prev;
    }
}

/*================================================================
 * mm_init
 *
 *  Initialize the heap and segregated free lists. We allocate
 *  space for SEGLIST_CNT pointers (each 4 bytes), then set up
 *  a small prologue block (8 bytes) and an epilogue header. After
 *  that, we extend the heap by CHUNKSIZE bytes to create the
 *  first free block.
 *================================================================*/
int mm_init(void) {
    void *heap_start;

    /* Allocate space for SEGLIST_CNT pointers + 4 words of prologue */
    if ((heap_start = mem_sbrk((SEGLIST_CNT + 4) * sizeof(char*))) == (void *)-1) {
        return -1;
    }

    /* seg_free_lists points to the start of that region */
    seg_free_lists = (char **)heap_start;

    /* Initialize all list heads to NULL */
    for (int i = 0; i < SEGLIST_CNT; i++) {
        seg_free_lists[i] = NULL;
    }

    /* The first address after those SEGLIST_CNT pointers is our "heap_listp" */
    char *heap_listp = (char *)heap_start + SEGLIST_CNT * sizeof(char*);

    /* Alignment padding */
    PUT(heap_listp, 0);                            /* (0) Alignment padding */

    /* Prologue header and footer (each 4 bytes, total payload = 8) */
    PUT(heap_listp + WSIZE, PACK(DSIZE, 1));       /* (1) Prologue header: size=8, alloc=1 */
    PUT(heap_listp + 2 * WSIZE, PACK(DSIZE, 1));   /* (2) Prologue footer: size=8, alloc=1 */

    /* Epilogue header */
    PUT(heap_listp + 3 * WSIZE, PACK(0, 1));       /* (3) Epilogue header: size=0, alloc=1 */

    /* Extend the empty heap with a free block of CHUNKSIZE bytes */
    if (extend_heap(CHUNKSIZE / WSIZE) == NULL) {
        return -1;
    }

    return 0;
}

/*================================================================
 * extend_heap
 *
 *  Extend the heap by `words * WSIZE` bytes. We always allocate
 *  an even number of words to maintain alignment. We set up the
 *  new free block’s header/footer, plus a new epilogue header.
 *  Finally, we coalesce with the previous block (if it was free),
 *  then insert the resulting block into the segregated lists.
 *================================================================*/
static void *extend_heap(size_t words) {
    char *bp;
    size_t size;

    /* Allocate an even number of words to maintain alignment (round up) */
    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    if ((long)(bp = mem_sbrk(size)) == -1) {
        return NULL;
    }

    /* Initialize free block header/footer */
    PUT(HDRP(bp), PACK(size, 0));       /* Free block header */
    PUT(FTRP(bp), PACK(size, 0));       /* Free block footer */

    /* New epilogue header */
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));

    /* Coalesce with previous if possible */
    char *coalesced_bp = coalesce(bp);

    /* Insert coalesced block into segregated list */
    insert_node(coalesced_bp, GET_SIZE(HDRP(coalesced_bp)));

    return coalesced_bp;
}

/*================================================================
 * coalesce
 *
 *  Boundary‐tag coalescing. If the previous or next blocks are free,
 *  remove them from their free lists, merge sizes, write new header/
 *  footer, and return the pointer to the coalesced block. This
 *  function does NOT insert the result back, caller must insert.
 *================================================================*/
static void *coalesce(void *bp) {
    /* Compute prev/next block pointers just once */
    char *prev_bp = PREV_BLKP(bp);
    char *next_bp = NEXT_BLKP(bp);

    /* Check their allocation status */
    size_t prev_alloc = GET_ALLOC(HDRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp)); /* Current block's size */

    /* Case 1: both previous and next blocks are allocated -> no coalescing */
    if (prev_alloc && next_alloc) {
        return bp;
    }

    /* Case 2: prev allocated, next free -> merge with next only */
    if (prev_alloc && !next_alloc) {
        /* Remove next block from its free list */
        delete_node(next_bp);

        size += GET_SIZE(HDRP(next_bp));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));

        return bp;
    }

    /* Case 3: prev free, next allocated -> merge with prev only */
    if (!prev_alloc && next_alloc) {
        /* Remove previous block from its free list */
        delete_node(prev_bp);

        size += GET_SIZE(HDRP(prev_bp));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        
        return prev_bp;
    }

    /* Case 4: both prev and next are free -> merge all three */
    if (!prev_alloc && !next_alloc) {
        /* Remove both adjacent free blocks */
        delete_node(prev_bp);
        delete_node(next_bp);

        size += GET_SIZE(HDRP(prev_bp)) + GET_SIZE(HDRP(next_bp));
        PUT(HDRP(prev_bp), PACK(size, 0));
        PUT(FTRP(next_bp), PACK(size, 0));
        
        return prev_bp;
    }

    return bp; /* Should never reach here */
}

/*================================================================
 * find_fit
 *
 *  Search for a free block of at least asize bytes. 
 *  1. Compute the starting list index by repeatedly halving `asize`. 
 *  2. From that index onward, scan each free list:
 *     - Traverse the chain of free blocks (which are sorted by size descending).
 *     - If a block’s size ≥ asize, return it immediately.
 *  3. If no block is big enough, return NULL.
 *================================================================*/
static void *find_fit(size_t asize) {
    /* 1) Determine starting list index */
    int idx = 0;
    size_t size_class = asize;

    idx = get_list_index(size_class);

    /* 2) Scan from list[idx] up to the last list */
    for (int i = idx; i < SEGLIST_CNT; i++) {
        for (char *bp = seg_free_lists[i]; bp != NULL; bp = NEXT(bp)) {
            if (GET_SIZE(HDRP(bp)) >= asize) {
                /* Found a large enough block */
                return bp;
            }
        }
    }

    /* No fit found */
    return NULL;
}

/*================================================================
 * place
 *
 *  Given a free block `bp` that is large enough to accommodate an
 *  allocation of `asize` bytes, remove `bp` from its free list and
 *  decide how to split (if at all). There are three cases:
 *
 *  Case 1) If the leftover space after allocating `asize` bytes is < 16
 *     bytes, do not split: mark the entire block allocated and return `bp`.
 *
 *  Case 2) If the leftover space is between 16 and 340 bytes (inclusive),
 *     create a small “remainder” free block at the beginning of `bp`,
 *     and allocate the tail end of `bp` for `asize` bytes:
 *       - Write a free header/footer of size = remainder at `bp`
 *       - Write an allocated header/footer of size = asize at NEXT_BLKP(bp)
 *       - Insert the small free block (at `bp`, size=remainder) back into its free list
 *       - Return a pointer to the allocated chunk (NEXT_BLKP(bp))
 *
 *  Case 3) Otherwise (remainder > 340 bytes), split normally:
 *       - Write an allocated header/footer of size = asize at `bp`
 *       - Create a free block at NEXT_BLKP(bp) with size = remainder
 *       - Insert the new free block into its free list
 *       - Return `bp`
 *
 *  In all cases, the returned pointer points to the start of the
 *  allocated payload (not the free-list metadata).
 *================================================================*/
static void *place(void *bp, size_t asize) {
    size_t ptr_size = GET_SIZE(HDRP(bp));
    size_t remainder = ptr_size - asize;

    /* Remove this block from free list */
    delete_node(bp);

    /* Case 1: remainder < minimum split size (16 bytes) */
    if (remainder < 2 * DSIZE) {
        /* Mark entire block allocated */
        PUT(HDRP(bp), PACK(ptr_size, 1));
        PUT(FTRP(bp), PACK(ptr_size, 1));
        return bp;
    }
    /* Case 2: small remainder (16 ≤ remainder ≤ 340) */
    else if (remainder <= 340) {
        /* Create a free block at the front of size = remainder */
        PUT(HDRP(bp), PACK(remainder, 0));
        PUT(FTRP(bp), PACK(remainder, 0));
        /* Allocate tail end of the block for asize bytes */
        char *alloc_bp = NEXT_BLKP(bp);
        PUT(HDRP(alloc_bp), PACK(asize, 1));
        PUT(FTRP(alloc_bp), PACK(asize, 1));
        /* Insert the small free block (at `bp`) back into free list */
        insert_node(bp, remainder);
        return alloc_bp;
    }
    /* Case 3: normal split (remainder > 340) */
    else {
        /* Allocate first asize bytes */
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        /* Create a free block for the leftover */
        char *next_bp = NEXT_BLKP(bp);
        PUT(HDRP(next_bp), PACK(remainder, 0));
        PUT(FTRP(next_bp), PACK(remainder, 0));
        /* Insert the leftover free block into free list */
        insert_node(next_bp, remainder);
        return bp;
    }
}


/*================================================================
 * mm_malloc
 *
 *  If size == 0, return NULL. Otherwise, compute adjusted block size
 *  `asize` = max(2*DSIZE, aligned(size + DSIZE)). Then search for a fit
 *  in the segregated lists. If found, call place() and return pointer.
 *  If no fit, extend the heap by max(asize, CHUNKSIZE) and then place.
 *================================================================*/
void *mm_malloc(size_t size) {
    /* Ignore the size == 0 request */
    if (size == 0) {
        return NULL;
    }

    /* Compute asize = aligned(size + DSIZE) with minimum of 2*DSIZE */
    size_t asize = (size <= DSIZE) ? (2 * DSIZE) : ALIGN(size + DSIZE);

    /* Search for a fit in the segregated free lists */
    char *bp = find_fit(asize);
    if (bp == NULL) {
        /* No fit found, extend heap by max(asize, CHUNKSIZE) */
        size_t extend_bytes = MAX(asize, CHUNKSIZE);
        //size_t extend_bytes = (asize < 512 ? 512 : asize);
        if ((bp = extend_heap(extend_bytes / WSIZE)) == NULL) return NULL;
    }

    /* place the block and return the (possibly split) block pointer */
    bp = place(bp, asize);
    return bp;
}

/*================================================================
 * mm_free
 *
 *  Free a block: set its header/footer to “free,” coalesce, then
 *  insert the coalesced block into the segregated free lists.
 *================================================================*/
void mm_free(void *bp) {
    if (bp == NULL) return;

    size_t size = GET_SIZE(HDRP(bp));

    /* Mark header/footer as free */
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));

    /* Coalesce with neighbors and insert into free list */
    char *merged_bp = coalesce(bp);

    /* Insert the coalesced block into the segregated free list */
    insert_node(merged_bp, GET_SIZE(HDRP(merged_bp)));
}

/*================================================================
 * mm_realloc
 *
 *  If size == 0, free the block and return NULL. If bp == NULL,
 *  equivalent to malloc(size). Otherwise, compute new_size = aligned(size+DSIZE).
 *  Cases:
 *   1) If new_size ≤ old_size: return same bp.
 *   2) If next block is free and (old_size + next_size) ≥ new_size:
 *      remove next block from free list, merge sizes, update header/footer,
 *      return bp.
 *   3) Else, malloc new region, copy payload, free old block.
 *================================================================*/
void *mm_realloc(void *bp, size_t size) {
    /* size == 0 -> free and return NULL */
    if (size == 0) {
        mm_free(bp);
        return NULL;
    }

    /* bp == NULL -> equivalent to malloc */
    if (bp == NULL) {
        return mm_malloc(size);
    }

    /* Compute old block size and adjusted new size (including overhead & alignment) */
    size_t old_size = GET_SIZE(HDRP(bp));
    size_t new_size = (size <= DSIZE) ? (2 * DSIZE) : ALIGN(size + DSIZE);

    /* Case 1: current block already large enough */
    if (new_size <= old_size) {
        return bp;
    }

    /* Case 2: check if next block is free and big enough when combined */
    char *next_bp = NEXT_BLKP(bp);
    size_t next_alloc = GET_ALLOC(HDRP(next_bp));
    size_t next_size = next_alloc ? 0 : GET_SIZE(HDRP(next_bp));
    size_t total_size = old_size + next_size;

    if (!next_alloc && total_size >= new_size) {
        /* Remove next block from free list */
        delete_node(next_bp);

        /* Update header/footer of merged block */
        PUT(HDRP(bp), PACK(total_size, 1));
        PUT(FTRP(bp), PACK(total_size, 1));
        return bp;
    }

    /* Case 3: fallback to malloc + copy + free */
    void *new_bp = mm_malloc(size);
    if (!new_bp) {
        return NULL;
    }

    /* Copy payload (only up to the smaller of old payload and requested size) */
    size_t copy_bytes = old_size - DSIZE;
    if (size < copy_bytes) {
        copy_bytes = size;
    }
    memcpy(new_bp, bp, copy_bytes);
    mm_free(bp);
    return new_bp;
}
