#include <windows.h>
#include "../src/scanline.h"
#include "scanline_dll_protocol.h"
class GatedDevice final : public ScanlineDevice {
    Probe* p_;DWORD thread_=0;bool live_=false;
    void gate(){SetEvent(p_->entered);WaitForSingleObject(p_->release,INFINITE);}
public:
    explicit GatedDevice(Probe* p):p_(p){}
    ~GatedDevice(){reset();}
    bool initialize(HWND window) override {
        thread_=GetCurrentThreadId();live_=true;SetEvent(p_->initialized);
        if(window || thread_==p_->ui_thread)InterlockedIncrement(&p_->wrong_thread);
        if(p_->mode<=2)gate();
        if(p_->mode==2)throw 1;
        return p_->mode!=1;
    }
    HRESULT scanline(DWORD& line) noexcept override {
        if(GetCurrentThreadId()!=thread_)InterlockedIncrement(&p_->wrong_thread);
        if(p_->mode==3)gate();line=123;return S_OK;
    }
    void reset() noexcept override {
        if(!live_)return;live_=false;
        if(GetCurrentThreadId()!=thread_)InterlockedIncrement(&p_->wrong_thread);
        if(p_->mode==4)gate();
        InterlockedIncrement(&p_->released);SetEvent(p_->destroyed);
    }
};
extern "C" __declspec(dllexport) void* csp_start(Probe* probe){
    try {
        auto device=std::make_unique<AsyncScanlineDevice>([probe]{return std::make_unique<GatedDevice>(probe);});
        if(!device->initialize(nullptr))return nullptr;
        device->set_active(true);return device.release();
    }catch(...){return nullptr;}
}
extern "C" __declspec(dllexport) void csp_cancel(void* device){delete static_cast<AsyncScanlineDevice*>(device);}
