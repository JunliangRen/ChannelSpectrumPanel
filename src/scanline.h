#pragma once
#include <ddraw.h>
#include <mmsystem.h>
#include <atomic>
#include <memory>
#include <functional>
#include <chrono>
#include <process.h>
#include "frame_timer.h"
#pragma comment(lib,"dxguid.lib")
// DirectDraw is created, queried and released on a single owning thread.
class ScanlineDevice {
public:
    virtual ~ScanlineDevice(){reset();}
    virtual bool initialize(HWND window){
        reset();module_=LoadLibraryExW(L"ddraw.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!module_)return false;
        using Create=HRESULT(WINAPI*)(GUID*,void**,REFIID,IUnknown*);
        auto create=reinterpret_cast<Create>(GetProcAddress(module_,"DirectDrawCreateEx"));
        if(!create || FAILED(create(nullptr,reinterpret_cast<void**>(&draw_),IID_IDirectDraw7,nullptr)) ||
            FAILED(draw_->SetCooperativeLevel(window,DDSCL_NORMAL))){reset();return false;}
        return true;
    }
    virtual void set_active(bool) noexcept {}
    virtual void reset() noexcept {if(draw_){draw_->Release();draw_=nullptr;}if(module_){FreeLibrary(module_);module_=nullptr;}}
    virtual HRESULT scanline(DWORD& scan) noexcept {return draw_?draw_->GetScanLine(&scan):E_FAIL;}
private:HMODULE module_=nullptr;IDirectDraw7* draw_=nullptr;
};
// The worker never receives a panel pointer or HWND. Cancellation does not wait
// for a driver call. Its shared state and module reference outlive the panel.
class AsyncScanlineDevice final : public ScanlineDevice {
    struct State {
        HANDLE wake=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        std::atomic<bool> cancelled{false},active{false};
        std::atomic<unsigned long long> sample{0};
        std::function<std::unique_ptr<ScanlineDevice>()> factory;
        ~State(){if(wake)CloseHandle(wake);}
    };
    // Shared across all panels in this module, including cancelled workers.
    // The slot remains occupied until all driver calls and cleanup have returned.
    inline static std::atomic<unsigned> workers_{0};
    struct Slot {
        bool acquired=false;
        Slot() noexcept {
            auto count=workers_.load();
            while(count<4){if(workers_.compare_exchange_weak(count,count+1)){acquired=true;break;}}
        }
        ~Slot(){if(acquired)--workers_;}
    };
    struct Job {Slot slot;std::shared_ptr<State> state;};
public:
    using Launch=decltype(&_beginthreadex);
    using Factory=std::function<std::unique_ptr<ScanlineDevice>()>;
    explicit AsyncScanlineDevice(Factory factory=[] {return std::make_unique<ScanlineDevice>();}, Launch launch=_beginthreadex):factory_(std::move(factory)),launch_(launch){}
    ~AsyncScanlineDevice(){reset();}
    AsyncScanlineDevice(const AsyncScanlineDevice&)=delete;
    AsyncScanlineDevice& operator=(const AsyncScanlineDevice&)=delete;
    bool initialize(HWND) override {
        reset();
        try {
            auto job=std::make_unique<Job>();if(!job->slot.acquired)return false;
            auto state=std::make_shared<State>();if(!state->wake)return false;
            state->factory=factory_;job->state=state;
            // The v143 UCRT pins the entry-point module before starting the
            // thread and releases it after CRT cleanup on normal return.
            auto* context=job.release();
            const auto thread=launch_(nullptr,0,worker,context,0,nullptr);
            if(!thread){delete context;return false;}
            CloseHandle(reinterpret_cast<HANDLE>(thread));state_=std::move(state);return true;
        }catch(...){return false;}
    }
    void set_active(bool active) noexcept override {
        if(state_){state_->active=active;state_->sample=0;SetEvent(state_->wake);}
    }
    void reset() noexcept override {
        if(state_){state_->cancelled=true;SetEvent(state_->wake);state_.reset();}
    }
    HRESULT scanline(DWORD& scan) noexcept override {
        if(!state_)return E_FAIL;
        if(!state_->active)return E_FAIL;
        const auto sample=state_->sample.load();SetEvent(state_->wake);
        // Reject old samples (including those left over after pause/resume).
        if(!sample || DWORD(milliseconds()-DWORD(sample>>32))>4)return E_FAIL;
        scan=DWORD(sample)-1;return S_OK;
    }
private:
    static DWORD milliseconds() noexcept {
        return DWORD(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }
    static unsigned __stdcall worker(void* context) noexcept {
        std::unique_ptr<Job> job(static_cast<Job*>(context));
        {
            auto state=std::move(job->state);
            try {
                auto device=state->factory();
                // DDSCL_NORMAL explicitly allows NULL: no panel-window lifetime
                // or cross-thread SetCooperativeLevel dependency.
                const bool initialized=!state->cancelled && device && device->initialize(nullptr);
                if(initialized){
                    while(!state->cancelled){
                        if(WaitForSingleObject(state->wake,INFINITE)!=WAIT_OBJECT_0)break;
                        if(state->cancelled)break;
                        if(!state->active)continue;
                        DWORD scan=0;const auto result=device->scanline(scan);
                        state->sample=SUCCEEDED(result)?(static_cast<unsigned long long>(milliseconds())<<32)|(scan+1ULL):0;
                    }
                }
                if(device)device->reset();
            }catch(...){/* Keep the regular cadence active. */}
        }
        // Return through the UCRT trampoline: do not call ExitThread or
        // FreeLibraryAndExitThread and bypass its module/CRT cleanup.
        return 0;
    }
    Factory factory_;Launch launch_;std::shared_ptr<State> state_;
};
// Multimedia timer cancellation is synchronous; driver cancellation is not.

class ScanlineSync {
public:
    static constexpr UINT message=WM_TIMER;
    ScanlineSync():device_(system_device_){}
    explicit ScanlineSync(ScanlineDevice& device):device_(device){}
    ~ScanlineSync(){stop();}
    ScanlineSync(const ScanlineSync&)=delete;
    ScanlineSync& operator=(const ScanlineSync&)=delete;
    void pause() noexcept {
        if(timer_){timeKillEvent(timer_);timer_=0;}
        if(period_){timeEndPeriod(1);period_=false;}
        generation_=csp::next_refresh_token();pending_=false;device_.set_active(false);
    }
    void stop() noexcept {pause();device_.reset();initialized_=failed_=false;window_=nullptr;}
    bool start(HWND window){
        pause();
        if(window_!=window){stop();window_=window;}
        // A driver failure must not trigger another costly attempt on every host event.
        if(failed_)return false;
        if(!initialized_){
            if(!device_.initialize(window)){device_.reset();failed_=true;return false;}
            initialized_=true;
        }
        device_.set_active(true);
        period_=timeBeginPeriod(1)==TIMERR_NOERROR;
        LARGE_INTEGER now{},frequency{};
        if(!QueryPerformanceCounter(&now) || !QueryPerformanceFrequency(&frequency)){pause();return false;}
        frequency_=double(frequency.QuadPart);last_=double(now.QuadPart);deadline_.reset(last_,0);
        timer_=timeSetEvent(2,1,callback,reinterpret_cast<DWORD_PTR>(this),TIME_PERIODIC|TIME_CALLBACK_FUNCTION|TIME_KILL_SYNCHRONOUS);
        if(!timer_){pause();return false;}return true;
    }
    bool ready(WPARAM generation,double interval){
        if(!timer_ || generation!=generation_ || !initialized_)return false;
        pending_=false;LARGE_INTEGER stamp{};QueryPerformanceCounter(&stamp);const double now=double(stamp.QuadPart);
        const double period=frequency_*interval/1000;
        if(deadline_.period!=period)deadline_.reset(last_,period);
        DWORD scan=0;auto result=device_.scanline(scan);
        if(now<deadline_.next)return false;const double elapsed=(now-last_)*1000/frequency_;
        RECT rect{};GetWindowRect(window_,&rect);MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfo(MonitorFromWindow(window_,MONITOR_DEFAULTTONEAREST),&monitor);
        int top=rect.top-monitor.rcMonitor.top-100,bottom=rect.bottom-monitor.rcMonitor.top;
        // Time-bounded fallback if a driver/monitor cannot supply a useful safe region.
        if((monitor.dwFlags&MONITORINFOF_PRIMARY) && SUCCEEDED(result) && int(scan)>=top && int(scan)<=bottom && elapsed<2*interval)return false;
        last_=now;deadline_.due(now);return true;
    }
private:
    static void CALLBACK callback(UINT,UINT,DWORD_PTR user,DWORD_PTR,DWORD_PTR){auto* self=reinterpret_cast<ScanlineSync*>(user);if(!self->pending_.exchange(true))if(!PostMessage(self->window_,message,self->generation_,0))self->pending_=false;}
    AsyncScanlineDevice system_device_;ScanlineDevice& device_;
    HWND window_=nullptr;MMRESULT timer_=0;WPARAM generation_=0;
    bool initialized_=false,failed_=false;
    std::atomic<bool> pending_{false};
    bool period_=false;double last_=0,frequency_=1;
    csp::FrameDeadline deadline_;
};
