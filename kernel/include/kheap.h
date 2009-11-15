#ifndef KHEAP_H
#define KHEAP_H

#include "os.h"


#define HEAP_START_ADDRESS 0xC0000000


void heap_install();
void* k_malloc( uint32_t size, uint32_t alignment );
void k_free( void* mem );


/*#include "ordered_array.h"
#define KHEAP_START         0x40000000 // 1GB
#define KHEAP_INITIAL_SIZE  0x00300000
#define KHEAP_MAX           0x4FFFF000

#define HEAP_INDEX_SIZE     0x20000
#define HEAP_MAGIC          0x123890AB
#define HEAP_MIN_SIZE       0x70000

extern uint32_t placement_address;

typedef struct
{
    uint32_t magic;         // Magic number, used for error checking and identification.
    uint8_t is_hole;       // 1: hole, 0: block.
    uint32_t size;          // size of the block, including the end footer.
} header_t;

typedef struct
{
    uint32_t magic;         // Magic number, same as in header_t.
    header_t *header;    // Pointer to the block header.
} footer_t;

typedef struct
{
    ordered_array_t index;
    uint32_t start_address; // The start of allocated space.
    uint32_t end_address;   // The end of allocated space. May be expanded up to max_address.
    uint32_t max_address;   // The maximum address the heap can be expanded to.
    uint8_t supervisor;    // Should extra pages requested by us be mapped as supervisor-only?
    uint8_t readonly;      // Should extra pages requested by us be mapped as read-only?
} heap_t;

heap_t* create_heap(uint32_t start, uint32_t end, uint32_t max, uint8_t supervisor, uint8_t readonly);

void* alloc(uint32_t size, uint8_t page_align, heap_t *heap);
void  free(void* p, heap_t* heap);
void  kfree(void* p);*/

#endif
