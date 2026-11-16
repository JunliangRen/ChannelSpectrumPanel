#pragma once
#include <ddraw.h>
#include <mmsystem.h>
#include <atomic>
#include "frame_timer.h"
// Optional nonblocking scanline polling. Timer thread only posts; UI thread owns DDraw and painting.
class ScanlineSync {
public:
    static constexpr UINT message=WM_APP+101;
    ~ScanlineSync(){stop();}
    void stop(){if(timer_){timeKillEvent(timer_);timer_=0;}if(period_){timeEndPeriod(1);period_=false;}
        if(draw_){draw_->Release();draw_=nullptr;}if(module_){FreeLibrary(module_);module_=nullptr;}pending_=false;}
    bool start(HWND window){
        stop();window_=window;module_=LoadLibraryExW(L"ddraw.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!module_)return false;
        using Create=HRESULT(WINAPI*)(GUID*,void**,REFIID,IUnknown*);
        auto create=reinterpret_cast<Create>(GetProcAddress(module_,"DirectDrawCreateEx"));
        if(!create || FAILED(create(nullptr,reinterpret_cast<void**>(&draw_),IID_IDirectDraw7,nullptr)) ||
            FAILED(draw_->SetCooperativeLevel(window,DDSCL_NORMAL))){stop();return false;}
        period_=timeBeginPeriod(1)==TIMERR_NOERROR;
        timer_=timeSetEvent(2,1,callback,reinterpret_cast<DWORD_PTR>(this),TIME_PERIODIC|TIME_CALLBACK_FUNCTION|TIME_KILL_SYNCHRONOUS);
        LARGE_INTEGER now{},frequency{};QueryPerformanceCounter(&now);QueryPerformanceFrequency(&frequency);frequency_=double(frequency.QuadPart);last_=double(now.QuadPart);deadline_.reset(last_,0);if(!timer_){stop();return false;}return true;
    }
    bool ready(double interval){
        pending_=false;if(!draw_)return false;LARGE_INTEGER stamp{};QueryPerformanceCounter(&stamp);const double now=double(stamp.QuadPart);
        const double period=frequency_*interval/1000;
        if(deadline_.period!=period)deadline_.reset(last_,period);
        if(now<deadline_.next)return false;const double elapsed=(now-last_)*1000/frequency_;
        DWORD scan=0;auto result=draw_->GetScanLine(&scan);
        RECT rect{};GetWindowRect(window_,&rect);MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfo(MonitorFromWindow(window_,MONITOR_DEFAULTTONEAREST),&monitor);
        int top=rect.top-monitor.rcMonitor.top-100,bottom=rect.bottom-monitor.rcMonitor.top;
        // Time-bounded fallback if a driver/monitor cannot supply a useful safe region.
        if((monitor.dwFlags&MONITORINFOF_PRIMARY) && SUCCEEDED(result) && int(scan)>=top && int(scan)<=bottom && elapsed<2*interval)return false;
        last_=now;deadline_.due(now);return true;
    }
private:
    static void CALLBACK callback(UINT,UINT,DWORD_PTR user,DWORD_PTR,DWORD_PTR){auto* self=reinterpret_cast<ScanlineSync*>(user);if(!self->pending_.exchange(true))if(!PostMessage(self->window_,message,0,0))self->pending_=false;}
    HWND window_=nullptr;HMODULE module_=nullptr;IDirectDraw7* draw_=nullptr;MMRESULT timer_=0;
    std::atomic<bool> pending_{false};
    bool period_=false;double last_=0,frequency_=1;
    csp::FrameDeadline deadline_;
};
