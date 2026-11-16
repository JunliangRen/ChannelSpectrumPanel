#pragma once
#include "../src/d2d_render.h"
#include <wincodec.h>
#include "d2d_failure_tests.h"
#include "d2d_gradient_tests.h"
#pragma comment(lib,"windowscodecs.lib")
inline void d2d_tests(){
    // Exercise the ARM formula on every host, with the existing x87 mapping as oracle.
    for(int bins:{512,1024,4096})for(int extent:{160,890,1780})for(int rounded:{0,1}){
        for(int bin=1;bin<bins;bin+=17){
            csp::MappingInput input{bin,bin/4,bins,extent,rounded};csp::MappingOutput portable{},reference{};
            csp::portable_mapping_coordinates(&input,&portable);csp::csp_mapping_coordinates(&input,&reference);
            check(std::abs(portable.previous-reference.previous)<=1 && std::abs(portable.current-reference.current)<=1,"portable ARM mapping stays within one pixel of x87 coordinates");
        }
    }
    csp::Settings s;s.direct2d=true;s.style=csp::Style::Simple;s.foreground=0xffffff;s.background=0;s.peak_color=0xff0000;
    auto words=csp::encode(s);csp::Settings decoded;
    check(csp::decode(words,decoded) && decoded==s,"Direct2D preference round trips without changing animation parameters");
    words.pop_back();check(!csp::decode(words,decoded),"truncated backend preference rejected");
    words[0]=5;check(csp::decode(words,decoded) && !decoded.direct2d,"old format 5 keeps GDI default");
    std::vector<uint32_t> original;check(csp::encode_original_csp(s,original) && csp::decode_original_csp(original,decoded) && !decoded.direct2d,"original presets retain GDI default");
    for(auto style:{csp::Style::Simple,csp::Style::Horizontal,csp::Style::SimpleAntialias,csp::Style::HorizontalAntialias,csp::Style::SimpleLine,csp::Style::SeparateLines}){s.style=style;check(csp::d2d_supported(s),"priority style supported by Direct2D");}
    for(auto style:{csp::Style::Vertical,csp::Style::GradientBars}){s.style=style;check(csp::d2d_supported(s),"gradient styles supported by Direct2D");}
    s.style=csp::Style::Simple;
    for(int feature=0;feature<2;++feature){auto other=s;if(feature==0)other.transparent=true;if(feature==1)other.segmented=true;check(!csp::d2d_supported(other),"unsupported effects route to GDI without changing settings");}
    HRESULT initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    check(SUCCEEDED(initialized) || initialized==RPC_E_CHANGED_MODE,"COM available for offscreen Direct2D test");
    struct Uninitialize {HRESULT hr;~Uninitialize(){if(SUCCEEDED(hr))CoUninitialize();}} uninitialize{initialized};
    csp::ComPtr<IWICImagingFactory> wic;
    check(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(wic.GetAddressOf()))),"WIC factory creates");
    csp::ComPtr<IWICBitmap> bitmap;check(SUCCEEDED(wic->CreateBitmap(64,32,GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,bitmap.GetAddressOf())),"offscreen bitmap creates");
    csp::ComPtr<ID2D1Factory> factory;check(SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,factory.GetAddressOf())),"Direct2D factory creates");
    csp::ComPtr<ID2D1RenderTarget> target;
    check(SUCCEEDED(factory->CreateWicBitmapRenderTarget(bitmap.Get(),D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE),target.GetAddressOf())),"software Direct2D target creates for deterministic primitive tests");
    csp::D2DScene scene;std::vector<csp::Bar> bars{{5,6,12,5,0,1},{20,21,18,8,0,1},{40,41,9,3,1,1}};
    std::vector<uint32_t> pixels(64*32);
    for(auto style:{csp::Style::Simple,csp::Style::HorizontalAntialias,csp::Style::SimpleLine,csp::Style::Simple}){
        s.style=style;target->BeginDraw();auto hr=scene.draw(target.Get(),64,32,bars,{},s);auto end=target->EndDraw();
        check(SUCCEEDED(hr) && SUCCEEDED(end),"native Direct2D priority style completes");
        check(SUCCEEDED(bitmap->CopyPixels(nullptr,64*4,UINT(pixels.size()*4),reinterpret_cast<BYTE*>(pixels.data()))),"read native Direct2D output");
        check((pixels[0]&0xffffff)==0,"black background preserved");
        check((pixels[5*64+5]&0xffffff)==0xff0000,"one-pixel peak retains exact position and color");
        if(style==csp::Style::Simple){check((pixels[15*64+5]&0xffffff)==0xffffff,"one-pixel column rendered");check((pixels[15*64+6]&0xffffff)==0,"adjacent column stays black");}
        if(style==csp::Style::SimpleLine){bool visible=false;for(auto pixel:pixels)visible|=(pixel&0xffffff)==0xffffff;check(visible,"Simple-line draws visible contour");}
    }
    s.style=csp::Style::Simple;s.show_peaks=false;
    for(unsigned rotation=0;rotation<4;++rotation){
        const int lw=rotation&1?32:64,lh=rotation&1?64:32;
        std::vector<csp::Bar> marker{{3,4,lh-1,0,0,1}};
        target->SetTransform(csp::d2d_transform(rotation,64,32));target->BeginDraw();
        auto hr=scene.draw(target.Get(),lw,lh,marker,{},s);auto end=target->EndDraw();
        check(SUCCEEDED(hr) && SUCCEEDED(end),"rotated native Direct2D scene completes");
        check(SUCCEEDED(bitmap->CopyPixels(nullptr,64*4,UINT(pixels.size()*4),reinterpret_cast<BYTE*>(pixels.data()))),"read rotated Direct2D output");
        std::vector<uint32_t> logical(lw*lh),expected(64*32);logical[(lh-1)*lw+3]=0xffffff;
        csp::rotate_pixels(logical.data(),lw,lh,expected.data(),rotation);
        bool same=true;for(size_t i=0;i<pixels.size();++i)same&=(pixels[i]&0xffffff)==expected[i];
        check(same,"Direct2D rotation preserves physical-pixel placement in all four orientations");
    }
    scene.clear();scene.clear();target.Reset();
    csp::D2DRenderer renderer;
    s.direct2d=false;check(!renderer.draw(nullptr,64,32,bars,{},s) && !renderer.failed(),"GDI mode does not initialize Direct2D");
    s.direct2d=true;check(!renderer.draw(nullptr,64,32,bars,{},s) && renderer.failed(),"invalid target triggers recoverable GDI fallback");
    check(!renderer.draw(nullptr,64,32,bars,{},s),"failed backend does not retry every frame");
    renderer.reset();check(!renderer.failed(),"Apply/window recreation resets failure latch");
    d2d_gradient_tests(wic.Get(),factory.Get());
    d2d_failure_test::run();
    std::cout<<"Direct2D: offscreen software primitive tests passed; hardware/player validation pending.\n";
}
