/* Native 3DS heap sizing. Older libctru does not cap its application heap
 * when a CIA requests 178 MB mode. Keep it inside the 96 MB virtual window.
 * Derived from libctru allocateHeaps.c (devkitPro contributors, zlib license).
 */
#include <3ds/svc.h>
#include <3ds/os.h>
#include <3ds/result.h>
#include <3ds/allocator/mappable.h>
extern u32 __ctru_heap, __ctru_linear_heap;
extern u32 __ctru_heap_size, __ctru_linear_heap_size;
extern char *fake_heap_start, *fake_heap_end;
void __system_allocateHeaps(void) {
    Handle limit=0;
    s64 maximum=0,used=0;
    ResourceLimitType kind=RESLIMIT_COMMIT;
    if(R_FAILED(svcGetResourceLimit(&limit,CUR_PROCESS_HANDLE)))svcBreak(USERBREAK_PANIC);
    Result a=svcGetResourceLimitLimitValues(&maximum,limit,&kind,1);
    Result b=svcGetResourceLimitCurrentValues(&used,limit,&kind,1);
    svcCloseHandle(limit);
    if(R_FAILED(a)||R_FAILED(b)||maximum<=used)svcBreak(USERBREAK_PANIC);
    u32 available=(u32)(maximum-used)&~0xfff;
    u32 linear=available/2&~0xfff;
    if(linear>32*1024*1024)linear=32*1024*1024;
    u32 heap=available-linear;
    const u32 heap_cap=OS_HEAP_AREA_END-OS_HEAP_AREA_BEGIN;
    if(heap>heap_cap)heap=heap_cap;
    linear=available-heap;
    if(linear>64*1024*1024)linear=64*1024*1024;
    __ctru_heap_size=heap;__ctru_linear_heap_size=linear;
    if(R_FAILED(svcControlMemory(&__ctru_heap,OS_HEAP_AREA_BEGIN,0,heap,MEMOP_ALLOC,MEMPERM_READ|MEMPERM_WRITE)))svcBreak(USERBREAK_PANIC);
    if(R_FAILED(svcControlMemory(&__ctru_linear_heap,0,0,linear,MEMOP_ALLOC_LINEAR,MEMPERM_READ|MEMPERM_WRITE)))svcBreak(USERBREAK_PANIC);
    mappableInit(OS_MAP_AREA_BEGIN,OS_MAP_AREA_END);
    fake_heap_start=(char*)__ctru_heap;fake_heap_end=fake_heap_start+heap;
}
