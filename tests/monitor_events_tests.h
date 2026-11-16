#pragma once
#include "../src/monitor_events.h"
namespace monitor_test {
inline int queries=0;
inline HMONITOR selected=reinterpret_cast<HMONITOR>(1);
inline double rate=60;
inline bool precise=true;
inline csp::MonitorRefresh query(HWND){++queries;csp::MonitorRefresh result;result.monitor=selected;result.hz=rate;result.precise=precise;return result;}
inline HMONITOR locate(HWND){return selected;}
inline void pump(unsigned duration=90){
    const auto end=GetTickCount64()+duration;
    do {MSG msg{};while(PeekMessage(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessage(&msg);}Sleep(1);}while(GetTickCount64()<end);
}
template<class Predicate> inline void until(Predicate ready,unsigned timeout=3000){
    const auto end=GetTickCount64()+timeout;
    while(!ready() && GetTickCount64()<end)pump(10);
}
}
inline void monitor_events_tests(){
    using namespace monitor_test;
    HWND parent=CreateWindowEx(0,L"STATIC",L"",WS_POPUP,0,0,100,100,nullptr,nullptr,GetModuleHandle(nullptr),nullptr);
    HWND other=CreateWindowEx(0,L"STATIC",L"",WS_POPUP,200,0,100,100,nullptr,nullptr,GetModuleHandle(nullptr),nullptr);
    HWND child=CreateWindowEx(0,L"STATIC",L"",WS_CHILD,0,0,80,80,parent,nullptr,GetModuleHandle(nullptr),nullptr);
    check(parent && other && child,"monitor event test windows created");
    struct Windows{HWND parent,other,child;~Windows(){DestroyWindow(child);DestroyWindow(other);DestroyWindow(parent);}} windows{parent,other,child};
    queries=0;precise=true;selected=reinterpret_cast<HMONITOR>(1);rate=60000.0/1001;
    csp::MonitorEvents events(query,locate);
    check(!events.bind(child,true) && events.has_receiver(),"hook failure reported for caller's polling fallback");
    check(queries==1,"initial monitor lookup");
    for(int i=0;i<10;++i)events.location_changed();pump();
    check(queries==1,"same-monitor movement skips display configuration query");
    for(int i=0;i<20;++i)events.display_changed();until([&]{return queries>=2;});
    check(queries==2,"display notification burst coalesces into one query");
    selected=reinterpret_cast<HMONITOR>(2);rate=144;
    for(int i=0;i<20;++i)events.location_changed();until([&]{return queries>=3;});
    check(queries==3 && events.current().hz==144,"cross-monitor movement updates shared result once");
    rate=120;events.display_changed();until([&]{return queries>=4;});
    check(queries==4 && events.current().hz==120,"same monitor mode change forces refresh");
    events.attach_dialog(other);const int attached=queries;
    check(events.current().hz==120,"dialog reads shared monitor result");
    pump();check(queries==attached,"idle listener performs no periodic query");
    events.detach_dialog(other);events.display_changed();events.stop();pump();
    check(queries==attached,"stop cancels a pending display query");
    check(!events.bind(child,true),"listener reopens after close");
    SetParent(child,other);selected=reinterpret_cast<HMONITOR>(3);rate=75;events.location_changed();until([&]{return events.current().hz==75;});
    check(events.current().hz==75,"reparented panel uses new monitor");
    events.stop();
    precise=false;rate=0;const int beforeFailure=queries;
    events.bind(child,true);until([&]{return queries>=beforeFailure+4;});
    check(queries==beforeFailure+4,"temporary failure retries at most three times");
    const int exhausted=queries;pump(200);
    check(queries==exhausted,"retry exhaustion returns to idle without polling");
    events.display_changed();until([&]{return queries>exhausted;});const int beforeRecovery=queries;
    precise=true;rate=60000.0/1001;until([&]{return events.current().precise;});
    check(queries==beforeRecovery+1 && events.current().precise,"delayed retry recovers fractional display rate");
    const int recovered=queries;pump(550);
    check(queries==recovered,"successful retry cancels further wakeups");
    precise=false;events.display_changed();until([&]{return queries>recovered;});const int beforeClose=queries;
    events.stop();pump(600);
    check(queries==beforeClose,"closing cancels pending monitor retry");
    precise=true;
    // Exercise real process-scoped location hooks with real ancestor movement.
    csp::MonitorEvents hooked(query,locate);selected=reinterpret_cast<HMONITOR>(4);
    const bool listening=hooked.bind(child);check(listening,"native location event hook registered");
    pump();const int baseline=queries;selected=reinterpret_cast<HMONITOR>(5);rate=100;
    SetWindowPos(other,nullptr,220,10,100,100,SWP_NOZORDER|SWP_NOACTIVATE);
    NotifyWinEvent(EVENT_OBJECT_LOCATIONCHANGE,other,OBJID_WINDOW,CHILDID_SELF);until([&]{return queries>baseline;});
    check(queries==baseline+1 && hooked.current().hz==100,"ancestor location event reaches panel listener");
    SetParent(child,parent);pump();const int rebound=queries;
    selected=reinterpret_cast<HMONITOR>(6);rate=165;
    NotifyWinEvent(EVENT_OBJECT_LOCATIONCHANGE,other,OBJID_WINDOW,CHILDID_SELF);pump();
    check(queries==rebound,"old ancestor is ignored after reparenting");
    NotifyWinEvent(EVENT_OBJECT_LOCATIONCHANGE,parent,OBJID_WINDOW,CHILDID_SELF);
    until([&]{return queries>rebound;});
    check(queries==rebound+1 && hooked.current().hz==165,"new ancestor is observed after reparenting");
    const int beforeVisibility=queries;
    NotifyWinEvent(EVENT_OBJECT_HIDE,parent,OBJID_WINDOW,CHILDID_SELF);
    MSG visibility{};bool delivered=false;const auto visibilityEnd=GetTickCount64()+1000;
    while(GetTickCount64()<visibilityEnd){
        if(PeekMessage(&visibility,child,csp::MonitorEvents::visibility_message,csp::MonitorEvents::visibility_message,PM_REMOVE)){delivered=true;break;}
        MSG pending{};if(PeekMessage(&pending,nullptr,0,0,PM_REMOVE)){if(pending.hwnd==child && pending.message==csp::MonitorEvents::visibility_message){delivered=true;break;}TranslateMessage(&pending);DispatchMessage(&pending);}Sleep(1);
    }
    check(delivered && queries==beforeVisibility,"ancestor visibility notifies refresh control without querying display");
    hooked.stop();const int stopped=queries;
    NotifyWinEvent(EVENT_OBJECT_LOCATIONCHANGE,other,OBJID_WINDOW,CHILDID_SELF);pump();
    check(queries==stopped,"unregistered hook cannot query destroyed listener state");
}
