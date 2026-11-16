// Test executable only: deterministic allocation failures without exhausting the host.
#include <cstdlib>
#include <new>
#include <cstddef>
thread_local int allocationCountdown=-1;
thread_local std::size_t allocationCount=0;
void reset_allocation_count(){allocationCount=0;}
std::size_t get_allocation_count(){return allocationCount;}
void fail_allocation_after(int successfulAllocations){allocationCountdown=successfulAllocations;}
void* operator new(std::size_t size){
    ++allocationCount;
    if(allocationCountdown==0)throw std::bad_alloc();
    if(allocationCountdown>0)--allocationCountdown;
    if(void* p=std::malloc(size?size:1))return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size){return ::operator new(size);}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}
void operator delete[](void* p,std::size_t) noexcept{std::free(p);}
