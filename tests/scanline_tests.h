#pragma once
#include "../src/scanline.h"
inline void scanline_tests(){
    struct Device : ScanlineDevice {
        int created=0,released=0;bool live=false,fail=false;
        bool initialize(HWND) override{++created;live=true;return !fail;}
        void reset() noexcept override{if(live){++released;live=false;}}
        HRESULT scanline(DWORD&) noexcept override{return E_FAIL;}
    } device;
    HWND window=CreateWindowExW(0,L"STATIC",L"",WS_POPUP,0,0,20,20,nullptr,nullptr,GetModuleHandle(nullptr),nullptr);
    check(window!=nullptr,"scanline test window");
    {
        ScanlineSync sync(device);check(sync.start(window),"scanline timer starts");
        MSG old{};const auto until=GetTickCount64()+2000;
        while(!PeekMessage(&old,window,ScanlineSync::message,ScanlineSync::message,PM_REMOVE) && GetTickCount64()<until)Sleep(1);
        check(old.message==ScanlineSync::message,"scanline timer queues generation token");
        sync.pause();check(device.live && device.created==1 && device.released==0,"pause retains device");
        check(!sync.ready(old.wParam,1),"paused timer rejects queued callback");
        check(sync.start(window),"cached scanline restart");
        check(!sync.ready(old.wParam,1),"restart rejects old generation");
        for(int i=0;i<12;++i){sync.pause();check(sync.start(window),"repeated cached restart");}
        check(device.created==1,"same window initializes device only once");
        sync.stop();check(!device.live && device.released==1,"full stop releases device");
        device.fail=true;check(!sync.start(window),"injected initialization failure falls back");
        check(!device.live,"partial initialization is cleaned up");
        check(!sync.start(window) && device.created==2,"failure cached across repeated host events");
        sync.stop();device.fail=false;check(sync.start(window) && device.created==3,"explicit stop allows retry");
        HWND other=CreateWindowExW(0,L"STATIC",L"",WS_POPUP,0,0,20,20,nullptr,nullptr,GetModuleHandle(nullptr),nullptr);
        check(other!=nullptr,"replacement window created");
        check(sync.start(other) && device.created==4,"new HWND rebuilds cooperative device");
        sync.stop();DestroyWindow(other);
    }
    check(device.released==4,"all successful and failed devices released exactly once");
    MSG message{};while(PeekMessage(&message,window,ScanlineSync::message,ScanlineSync::message,PM_REMOVE)){}
    DestroyWindow(window);
    // A blocked driver must not block starting/stopping or panel destruction.
    struct Probe {
        HANDLE entered=CreateEventW(nullptr,TRUE,FALSE,nullptr),release=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        std::atomic<int> destroyed{0},queries{0};std::atomic<bool> wrong_thread{false};
        int outcome=0;
        ~Probe(){CloseHandle(entered);CloseHandle(release);}
    };
    struct SlowDevice : ScanlineDevice {
        std::shared_ptr<Probe> probe;DWORD owner=0;
        explicit SlowDevice(std::shared_ptr<Probe> p):probe(std::move(p)){}
        ~SlowDevice(){if(owner && owner!=GetCurrentThreadId())probe->wrong_thread=true;++probe->destroyed;}
        bool initialize(HWND window) override {
            owner=GetCurrentThreadId();if(window)probe->wrong_thread=true;
            SetEvent(probe->entered);WaitForSingleObject(probe->release,5000);
            if(probe->outcome==2)throw 1;
            return probe->outcome==0;
        }
        void reset() noexcept override{if(owner && owner!=GetCurrentThreadId())probe->wrong_thread=true;}
        HRESULT scanline(DWORD& line) noexcept override {
            if(owner!=GetCurrentThreadId())probe->wrong_thread=true;
            ++probe->queries;line=123;return S_OK;
        }
    };
    for(int attempt=0;attempt<12;++attempt){
        AsyncScanlineDevice rejected([]{return std::make_unique<ScanlineDevice>();},
            +[](void*,unsigned,unsigned(__stdcall*)(void*),void*,unsigned,unsigned*)->uintptr_t{return 0;});
        check(!rejected.initialize(nullptr),"thread launch failure refunds slot");
    }
    for(int outcome=0;outcome<3;++outcome){
        auto probe=std::make_shared<Probe>();probe->outcome=outcome;
        {
            AsyncScanlineDevice async([probe]{return std::make_unique<SlowDevice>(probe);});
            HWND panel=CreateWindowExW(0,L"STATIC",L"",WS_POPUP,0,0,20,20,nullptr,nullptr,GetModuleHandle(nullptr),nullptr);
            ScanlineSync sync(async);
            check(sync.start(panel),"async driver start returns before driver gate opens");
            check(WaitForSingleObject(probe->entered,2000)==WAIT_OBJECT_0,"driver entered worker");
            async.set_active(true);DWORD line=0;
            check(FAILED(async.scanline(line)),"pending driver uses regular cadence");
            MSG tick{};const auto deadline=GetTickCount64()+2000;bool refreshed=false;
            while(!refreshed && GetTickCount64()<deadline){
                if(PeekMessage(&tick,panel,ScanlineSync::message,ScanlineSync::message,PM_REMOVE))
                    refreshed=sync.ready(tick.wParam,15);
                else Sleep(1);
            }
            check(refreshed,"regular cadence continues while initialization is blocked");
            sync.stop();DestroyWindow(panel);
            check(probe->destroyed==0,"cancel does not join blocked initialization");
        }
        SetEvent(probe->release);
        const auto end=GetTickCount64()+2000;
        while(probe->destroyed==0 && GetTickCount64()<end)Sleep(1);
        check(probe->destroyed==1 && !probe->wrong_thread,"cancelled success failure exception clean up on worker");
        check(probe->queries==0,"cancelled worker never queries or touches a panel");
    }
    for(int outcome=1;outcome<=2;++outcome){
        auto probe=std::make_shared<Probe>();probe->outcome=outcome;SetEvent(probe->release);
        AsyncScanlineDevice async([probe]{return std::make_unique<SlowDevice>(probe);});
        check(async.initialize(nullptr),"failed driver is attempted asynchronously");async.set_active(true);
        const auto end=GetTickCount64()+2000;
        while(probe->destroyed==0 && GetTickCount64()<end)Sleep(1);
        DWORD line=0;
        check(probe->destroyed==1 && FAILED(async.scanline(line)),"driver failure retains timer fallback");
        async.set_active(false);async.set_active(true);
        check(FAILED(async.scanline(line)) && probe->destroyed==1,"resume does not retry a failed driver");
        check(!probe->wrong_thread,"failed driver released by owning worker");
    }
    // Production ScanlineSync already holds this timer-resolution lease. Use
    // the same resolution when checking a sample whose lifetime is only 4 ms.
    struct SampleTimerPeriod {
        bool active=timeBeginPeriod(1)==TIMERR_NOERROR;
        ~SampleTimerPeriod(){if(active)timeEndPeriod(1);}
    } sampleTimerPeriod;
    check(sampleTimerPeriod.active,"sample tests acquire production timer resolution");
    for(int cycle=0;cycle<8;++cycle){
        auto probe=std::make_shared<Probe>();SetEvent(probe->release);
        AsyncScanlineDevice async([probe]{return std::make_unique<SlowDevice>(probe);});
        check(async.initialize(nullptr),"reopen starts independent worker");async.set_active(true);
        DWORD line=0;auto end=GetTickCount64()+2000;bool valid=false;
        while(!(valid=SUCCEEDED(async.scanline(line))) && GetTickCount64()<end)Sleep(1);
        check(valid && line==123,"worker publishes scanline atomically");
        async.set_active(false);Sleep(10);
        check(FAILED(async.scanline(line)),"paused sample expires");
        async.set_active(true);end=GetTickCount64()+2000;valid=false;
        while(!(valid=SUCCEEDED(async.scanline(line))) && GetTickCount64()<end)Sleep(1);
        check(valid,"resume refreshes scanline sample");async.reset();
        end=GetTickCount64()+2000;while(probe->destroyed==0 && GetTickCount64()<end)Sleep(1);
        check(probe->destroyed==1 && !probe->wrong_thread,"reopen closes all driver resources on owner thread");
    }
}
