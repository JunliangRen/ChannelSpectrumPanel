#pragma once
#include <windows.h>
#include <mmsystem.h>
#include <atomic>
#include <cmath>
#include <cstdint>
#pragma comment(lib,"winmm.lib")
namespace csp {
inline uint32_t frame_clock_ms() noexcept {
    static const double ticks_per_ms=[] {LARGE_INTEGER f{};QueryPerformanceFrequency(&f);return double(f.QuadPart)/1000;}();
    LARGE_INTEGER now{};QueryPerformanceCounter(&now);
    return static_cast<uint32_t>(static_cast<uint64_t>(double(now.QuadPart)/ticks_per_ms));
}
// Absolute deadlines avoid accumulating callback/work duration into the next frame.
struct FrameDeadline {
    double next=0,period=1;
    void reset(double now,double step) noexcept {period=step;next=now+step;}
    bool due(double now) noexcept {
        if(now<next)return false;
        next+=(std::floor((now-next)/period)+1)*period;
        return true;
    }
};
class FrameTimer {
public:
    static constexpr UINT message=WM_APP+102;
    FrameTimer()=default;
    FrameTimer(const FrameTimer&)=delete;
    FrameTimer& operator=(const FrameTimer&)=delete;
    ~FrameTimer(){stop();}
    bool start(HWND window,double fps) noexcept {
        stop();window_=window;
        LARGE_INTEGER frequency{},now{};
        if(!std::isfinite(fps) || fps<1 || fps>1000 || !QueryPerformanceFrequency(&frequency) || !QueryPerformanceCounter(&now))return false;
        deadline_.reset(double(now.QuadPart),double(frequency.QuadPart)/fps);
        period_=timeBeginPeriod(1)==TIMERR_NOERROR;
        timer_=timeSetEvent(1,1,callback,reinterpret_cast<DWORD_PTR>(this),TIME_PERIODIC|TIME_CALLBACK_FUNCTION|TIME_KILL_SYNCHRONOUS);
        if(!timer_){stop();return false;}return true;
    }
    void stop() noexcept {
        if(timer_){timeKillEvent(timer_);timer_=0;}
        if(period_){timeEndPeriod(1);period_=false;}
        ++generation_;pending_=false;
    }
    bool consume(WPARAM token) noexcept {
        if(!timer_ || token!=generation_)return false;
        pending_=false;return true;
    }
private:
    static void CALLBACK callback(UINT,UINT,DWORD_PTR user,DWORD_PTR,DWORD_PTR) noexcept {
        auto* self=reinterpret_cast<FrameTimer*>(user);
        if(self->callback_busy_.test_and_set(std::memory_order_acquire))return;
        LARGE_INTEGER now{};
        if(QueryPerformanceCounter(&now) && self->deadline_.due(double(now.QuadPart)) && !self->pending_.exchange(true)){
            if(!PostMessage(self->window_,message,self->generation_,0))self->pending_=false;
        }
        self->callback_busy_.clear(std::memory_order_release);
    }
    HWND window_=nullptr;
    MMRESULT timer_=0;
    bool period_=false;
    // Changed only after synchronous timer cancellation has drained callbacks.
    WPARAM generation_=0;
    std::atomic<bool> pending_{false};
    std::atomic_flag callback_busy_=ATOMIC_FLAG_INIT;
    FrameDeadline deadline_;
};
}
