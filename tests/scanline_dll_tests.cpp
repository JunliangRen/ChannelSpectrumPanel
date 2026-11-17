#include <windows.h>
#include <iostream>
#include <stdexcept>
#include <array>
#include "scanline_dll_protocol.h"
static unsigned checks=0;
// On a failed assertion a worker may still reference the stack-owned gates.
// Exit the isolated test process without unwinding those objects under it.
void check(bool condition,const char* message){++checks;if(!condition){std::cerr<<"FAIL: "<<message<<std::endl;ExitProcess(1);}}
struct Events : Probe {
    Events(int value=0){mode=value;ui_thread=GetCurrentThreadId();entered=CreateEventW(nullptr,TRUE,FALSE,nullptr);release=CreateEventW(nullptr,TRUE,FALSE,nullptr);destroyed=CreateEventW(nullptr,TRUE,FALSE,nullptr);initialized=CreateEventW(nullptr,TRUE,FALSE,nullptr);}
    ~Events(){CloseHandle(entered);CloseHandle(release);CloseHandle(destroyed);CloseHandle(initialized);}
};
using Start=void* (__cdecl*)(Probe*);
using Cancel=void (__cdecl*)(void*);
constexpr auto dll=L"scanline_dll_fixture.dll";
bool unloaded(){
    const auto end=GetTickCount64()+5000;
    while(GetModuleHandleW(dll) && GetTickCount64()<end)Sleep(1);
    return GetModuleHandleW(dll)==nullptr;
}
void lifecycle(int mode){
    Events probe(mode);HMODULE module=LoadLibraryW(dll);check(module!=nullptr,"load fixture DLL");
    auto start=reinterpret_cast<Start>(GetProcAddress(module,"csp_start"));
    auto cancel=reinterpret_cast<Cancel>(GetProcAddress(module,"csp_cancel"));check(start && cancel,"fixture exports");
    auto* device=start(&probe);check(device!=nullptr,"CRT worker starts in DLL");
    if(mode!=4)check(WaitForSingleObject(probe.entered,3000)==WAIT_OBJECT_0,"worker blocked in init or query");
    if(mode==4)check(WaitForSingleObject(probe.initialized,3000)==WAIT_OBJECT_0,"release fixture initialized before cancel");
    cancel(device);
    if(mode==4)check(WaitForSingleObject(probe.entered,3000)==WAIT_OBJECT_0,"worker blocked in release");
    check(FreeLibrary(module)!=FALSE,"host releases DLL reference");
    check(GetModuleHandleW(dll)!=nullptr,"CRT retains DLL while cancelled driver call is blocked");
    SetEvent(probe.release);
    check(unloaded(),"DLL actually unloads after worker returns through CRT");
    check(probe.released==1 && probe.wrong_thread==0,"single release on original background thread");
}
void overlap(){
    HMODULE module=LoadLibraryW(dll);check(module!=nullptr,"load overlap fixture");
    auto start=reinterpret_cast<Start>(GetProcAddress(module,"csp_start"));
    auto cancel=reinterpret_cast<Cancel>(GetProcAddress(module,"csp_cancel"));check(start && cancel,"overlap exports");
    std::array<Events,4> blocked;Events replacement;
    for(auto& probe:blocked){
        auto* device=start(&probe);check(device!=nullptr,"bounded parallel slot starts");
        check(WaitForSingleObject(probe.entered,3000)==WAIT_OBJECT_0,"parallel worker enters gate");cancel(device);
    }
    for(int i=0;i<24;++i)check(!start(&replacement),"cancelled but blocked workers retain all slots");
    check(WaitForSingleObject(replacement.entered,0)==WAIT_TIMEOUT,"denied attempts do not launch drivers");
    SetEvent(blocked[0].release);check(WaitForSingleObject(blocked[0].destroyed,3000)==WAIT_OBJECT_0,"one blocked worker cleaned up");
    void* next=nullptr;const auto end=GetTickCount64()+3000;
    while(!(next=start(&replacement)) && GetTickCount64()<end)Sleep(1);
    check(next!=nullptr,"completed cleanup makes a slot available again");
    check(WaitForSingleObject(replacement.entered,3000)==WAIT_OBJECT_0,"replacement enters gate");cancel(next);
    check(FreeLibrary(module)!=FALSE,"host unloads with multiple cancelled workers outstanding");
    check(GetModuleHandleW(dll)!=nullptr,"outstanding workers retain DLL");
    for(auto& p:blocked)SetEvent(p.release);SetEvent(replacement.release);
    check(unloaded(),"last worker releases final DLL reference");
    for(auto& p:blocked)check(p.released==1 && p.wrong_thread==0,"overlap resource cleanup");
    check(replacement.released==1 && replacement.wrong_thread==0,"replacement cleanup");
}
int main(){
    try {
        for(int cycle=0;cycle<8;++cycle){for(int mode=0;mode<5;++mode)lifecycle(mode);overlap();}
        std::cout<<"PASS: "<<checks<<" worker overlap and actual DLL unload checks.\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
