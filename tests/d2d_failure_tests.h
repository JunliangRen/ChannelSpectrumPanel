#pragma once
#include "../src/d2d_render.h"
#include <dxgi.h>
namespace d2d_failure_test {
struct Operations final:csp::D2DTargetOperations {
    mutable int creates=0,resizes=0,ends=0;
    HRESULT create_result=S_OK,resize_result=S_OK,end_result=S_OK;
    mutable csp::ComPtr<ID2D1HwndRenderTarget> probe;
    HRESULT create(ID2D1Factory* factory,HWND window,int w,int h,ID2D1HwndRenderTarget** target) const noexcept override {
        ++creates;
        const auto properties=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE),96,96);
        auto hr=factory->CreateHwndRenderTarget(properties,D2D1::HwndRenderTargetProperties(window,D2D1::SizeU(w,h),D2D1_PRESENT_OPTIONS_IMMEDIATELY),target);
        if(FAILED(hr))return hr;
        probe=*target;return create_result;
    }
    HRESULT resize(ID2D1HwndRenderTarget* target,D2D1_SIZE_U size) const noexcept override {
        ++resizes;return FAILED(resize_result)?resize_result:target->Resize(size);
    }
    HRESULT end_draw(ID2D1HwndRenderTarget* target) const noexcept override {
        ++ends;auto hr=target->EndDraw();return FAILED(hr)?hr:end_result;
    }
    ULONG release_probe(){auto* target=probe.Detach();return target?target->Release():0;}
};
struct Window {
    HWND handle=CreateWindowEx(0,L"STATIC",L"Direct2D lifecycle test",WS_POPUP,0,0,96,64,nullptr,nullptr,GetModuleHandle(nullptr),nullptr);
    Window(){check(handle!=nullptr,"native Direct2D test window creates");}
    ~Window(){if(handle)DestroyWindow(handle);}
};
inline void run(){
    csp::Settings settings;settings.direct2d=true;settings.style=csp::Style::Simple;settings.foreground=0xffffff;settings.peak_color=0xff0000;
    const auto original=settings;
    const std::vector<csp::Bar> bars{{5,6,12,5,0,1},{30,31,8,3,1,1}};
    Window window;
    for(int stage=0;stage<3;++stage)for(HRESULT error:{D2DERR_RECREATE_TARGET,DXGI_ERROR_DEVICE_REMOVED,E_OUTOFMEMORY}){
        Operations operations;csp::D2DRenderer renderer(operations);
        check(renderer.draw(window.handle,64,32,bars,{},settings),"software HWND target draws before injected failure");
        int fallbacks=0;csp::GdiSurface gdi,presented;presented.resize(80,40);
        auto fallback=[&]{++fallbacks;gdi.resize(80,40);csp::render(gdi.pixels,80,40,bars,settings);
            // A memory presentation target works even on a locked/disconnected desktop.
            const BOOL copied=BitBlt(presented.dc,0,0,80,40,gdi.dc,0,0,SRCCOPY);GdiFlush();
            check(copied!=FALSE,"GDI fallback copies the completed frame to an offscreen presentation target");
            check(presented.pixels[15*80+5]==0xffffff && presented.pixels[5*80+5]==0xff0000,"fallback retains columns and peak pixels");};
        const int beforeEnds=operations.ends;
        if(stage==0){renderer.reset();check(operations.release_probe()==0,"reset releases target and brush references before recreation");operations.create_result=error;}
        if(stage==1)operations.resize_result=error;
        if(stage==2)operations.end_result=error;
        const int attempts=operations.creates;
        check(!renderer.draw_or_fallback(window.handle,80,40,bars,{},settings,fallback) && fallbacks==1,"failure falls back exactly once in the current paint");
        check(renderer.failed() && !renderer.active(),"failure drops the target and latches GDI mode");
        check(operations.release_probe()==0,"failed target has no outstanding renderer or brush references");
        if(stage==1)check(operations.ends==beforeEnds,"Resize failure never enters drawing on the invalid target");
        if(stage==2)check(operations.ends==beforeEnds+1,"EndDraw failure occurs after the real drawing transaction closes");
        const int calls=operations.creates+operations.resizes+operations.ends;
        for(int i=0;i<4;++i)check(!renderer.draw_or_fallback(window.handle,80,40,bars,{},settings,fallback),"latched failure keeps using GDI");
        check(operations.creates+operations.resizes+operations.ends==calls,"failure latch avoids per-frame GPU recreation attempts");
        renderer.reset();renderer.reset();operations.create_result=operations.resize_result=operations.end_result=S_OK;
        check(renderer.draw_or_fallback(window.handle,80,40,bars,{},settings,fallback) && renderer.active(),"Apply-style reset restores Direct2D after injected failure");
        check(operations.creates==attempts+(stage==0?2:1),"recovery creates exactly one replacement target");
        renderer.reset();check(operations.release_probe()==0,"recovered target is fully released");
        check(settings==original,"failure and recovery never rewrite presets or animation settings");
    }
    // Retain one renderer across native window lifetimes, as the panel object does.
    Operations operations;csp::D2DRenderer renderer(operations);
    DWORD baselineGdi=0,baselineUser=0;
    for(int cycle=0;cycle<25;++cycle){
        Window next;
        check(renderer.draw(next.handle,64,32,bars,{},settings),"reopened native window obtains a fresh target");
        check(renderer.draw(next.handle,80,40,bars,{},settings),"target resizes successfully before close");
        if(cycle%2){operations.end_result=D2DERR_RECREATE_TARGET;check(!renderer.draw(next.handle,80,40,bars,{},settings),"device loss immediately before close falls back");operations.end_result=S_OK;}
        // WM_DESTROY uses this same reset operation before the HWND is released.
        renderer.reset();check(!renderer.active() && !renderer.failed(),"window teardown clears target and failure latch");
        check(operations.release_probe()==0,"window teardown releases every renderer reference to its target");
        check(DestroyWindow(next.handle)!=FALSE,"native test window closes");next.handle=nullptr;
        if(cycle==2){baselineGdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);baselineUser=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);}
    }
    check(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<=baselineGdi,"repeated window lifetimes do not grow GDI object count");
    check(GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS)<=baselineUser,"repeated window lifetimes do not grow USER object count");
    // Destruction must release resources even without an explicit window-reset call.
    Operations destruction;
    {csp::D2DRenderer scoped(destruction);check(scoped.draw(window.handle,64,32,bars,{},settings),"destructor test creates a target");}
    check(destruction.release_probe()==0,"renderer destructor releases target and target-owned brushes");
    std::cout<<"Direct2D: injected Create/Resize/EndDraw failures, GDI recovery and 25 native window lifetimes passed (software HWND targets).\n";
}
}
