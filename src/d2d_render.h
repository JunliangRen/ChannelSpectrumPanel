#pragma once
#include <d2d1.h>
#include <dwrite.h>
#include <dxgi.h>
#pragma comment(lib,"dwrite.lib")
#include <wrl/client.h>
#include "render.h"
#include "rendered_fps.h"
#pragma comment(lib,"d2d1.lib")
namespace csp {
using Microsoft::WRL::ComPtr;
inline D2D1_COLOR_F d2d_color(uint32_t rgb){return D2D1::ColorF(rgb,1.f);}
inline bool d2d_supported(const Settings& s) noexcept {
    return !s.transparent && !s.segmented &&
        (s.style==Style::Simple || s.style==Style::Horizontal || s.style==Style::SimpleAntialias ||
         s.style==Style::HorizontalAntialias || s.style==Style::SeparateLines || s.style==Style::SimpleLine ||
         s.style==Style::Vertical || s.style==Style::GradientBars);
}
inline D2D1_MATRIX_3X2_F d2d_transform(unsigned rotation,int w,int h) noexcept {
    if(rotation==1)return {0,1,-1,0,float(w),0};
    if(rotation==2)return {-1,0,0,-1,float(w),float(h)};
    if(rotation==3)return {0,-1,1,0,0,float(h)};
    return D2D1::Matrix3x2F::Identity();
}
// Cache colors independently of geometry: resizing and animated bar tops only move endpoints.
class D2DRamp {
public:
    void clear() noexcept {for(auto& brush:brushes_)brush.Reset();solid_.Reset();valid_=false;}
    HRESULT prepare(ID2D1RenderTarget* target,bool multi,const std::array<uint32_t,8>& colors,uint32_t top,uint32_t bottom){
        if(valid_ && multi==multi_ && (multi?colors==colors_:(top==top_ && bottom==bottom_)))return S_OK;
        clear();
        for(unsigned i=0;i<(multi?7u:1u);++i){
            const D2D1_GRADIENT_STOP stops[]={{0,d2d_color(multi?colors[7-i]:top)},{1,d2d_color(multi?colors[6-i]:bottom)}};
            ComPtr<ID2D1GradientStopCollection> collection;
            auto hr=target->CreateGradientStopCollection(stops,2,D2D1_GAMMA_2_2,D2D1_EXTEND_MODE_CLAMP,collection.GetAddressOf());
            if(FAILED(hr)){clear();return hr;}
            hr=target->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({0,0},{0,1}),collection.Get(),brushes_[i].GetAddressOf());
            if(FAILED(hr)){clear();return hr;}
        }
        auto hr=target->CreateSolidColorBrush(d2d_color(top),solid_.GetAddressOf());
        if(FAILED(hr)){clear();return hr;}
        multi_=multi;colors_=colors;top_=top;bottom_=bottom;valid_=true;return S_OK;
    }
    void draw(ID2D1RenderTarget* target,int left,int top,int right,int bottom,int origin,int length,bool inclusiveEnd=false){
        for(int i=0;i<(multi_?7:1);++i){
            const int start=origin+(multi_?length*i/7:0),end=origin+(multi_?length*(i+1)/7:length);
            int clippedTop=std::max(top,start),clippedBottom=std::min(bottom,end);
            if(clippedTop>=clippedBottom)continue;
            // Paint endpoint rows explicitly: D2D's filtered/clamped gradient endpoints
            // otherwise bleed into adjacent colors, especially for one-pixel segments.
            if(clippedTop==start){
                solid_->SetColor(d2d_color(multi_?colors_[7-i]:top_));
                target->FillRectangle(D2D1::RectF(float(left),float(start),float(right),float(start+1)),solid_.Get());
                ++clippedTop;
            }
            if(inclusiveEnd && clippedTop<clippedBottom && clippedBottom==end){
                solid_->SetColor(d2d_color(bottom_));
                target->FillRectangle(D2D1::RectF(float(left),float(end-1),float(right),float(end)),solid_.Get());
                --clippedBottom;
            }
            if(clippedTop>=clippedBottom)continue;
            // GDI samples at integer rows. D2D samples pixel centers; shift both endpoints.
            brushes_[i]->SetStartPoint({0,start+.5f});
            brushes_[i]->SetEndPoint({0,start+float(std::max(1,end-start-(inclusiveEnd?1:0)))+.5f});
            target->FillRectangle(D2D1::RectF(float(left),float(clippedTop),float(right),float(clippedBottom)),brushes_[i].Get());
        }
    }
private:
    std::array<ComPtr<ID2D1LinearGradientBrush>,7> brushes_;
    ComPtr<ID2D1SolidColorBrush> solid_;
    std::array<uint32_t,8> colors_{};
    uint32_t top_=0,bottom_=0;bool multi_=false,valid_=false;
};
// Target-owned resources; all access stays on the window's UI thread.
class D2DScene {
public:
    void clear() noexcept {solid_.Reset();for(auto& b:blur_)b.Reset();foreground_ramp_.clear();background_ramp_.clear();bar_ramp_.clear();valid_=false;}
    HRESULT draw(ID2D1RenderTarget* target,int w,int h,const std::vector<Bar>& bars,std::span<const Tick> ticks,const Settings& s){
        if(!solid_){auto hr=target->CreateSolidColorBrush(d2d_color(0),solid_.GetAddressOf());if(FAILED(hr))return hr;}
        if((s.style==Style::SimpleAntialias || s.style==Style::HorizontalAntialias) &&
            (!valid_ || s.background!=background_ || s.foreground!=fg_ || s.foreground2!=fg2_)){
            for(auto& b:blur_)b.Reset();valid_=false;
            for(unsigned i=0;i<24;++i){
                const D2D1_GRADIENT_STOP stops[]={{0,d2d_color(s.background)},{1,d2d_color(horizontal_color(s,i))}};
                ComPtr<ID2D1GradientStopCollection> collection;
                auto hr=target->CreateGradientStopCollection(stops,2,D2D1_GAMMA_1_0,D2D1_EXTEND_MODE_CLAMP,collection.GetAddressOf());
                if(FAILED(hr))return hr;
                hr=target->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({0,0},{0,1}),collection.Get(),blur_[i].GetAddressOf());
                if(FAILED(hr))return hr;
            }
            background_=s.background;fg_=s.foreground;fg2_=s.foreground2;valid_=true;
        }
        if(s.background_palette){auto hr=background_ramp_.prepare(target,true,s.background_colors,0,0);if(FAILED(hr))return hr;}
        if(s.style==Style::Vertical){auto hr=foreground_ramp_.prepare(target,s.foreground_palette,s.foreground_colors,horizontal_color(s,23),horizontal_color(s,0));if(FAILED(hr))return hr;}
        if(s.style==Style::GradientBars){auto hr=bar_ramp_.prepare(target,false,s.foreground_colors,s.foreground,s.foreground2);if(FAILED(hr))return hr;}
        target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
        target->Clear(d2d_color(s.background));
        if(s.background_palette)background_ramp_.draw(target,0,0,w,h,0,h);
        auto line=[&](int x1,int y1,int x2,int y2,uint32_t color){solid_->SetColor(d2d_color(color));target->DrawLine({x1+.5f,y1+.5f},{x2+.5f,y2+.5f},solid_.Get(),1);};
        if(s.style==Style::SimpleLine){
            std::array<const Bar*,18> first{},last{};
            for(const auto& b:bars)if(b.channel<18){
                if(last[b.channel])line(last[b.channel]->left,last[b.channel]->top,b.left,b.top,s.foreground);
                else first[b.channel]=&b;
                last[b.channel]=&b;
            }
            std::array<unsigned,18> order{};unsigned count=0;
            for(unsigned c=0;c<18;++c)if(first[c])order[count++]=c;
            auto left=[&](unsigned c){return std::min(first[c]->left,last[c]->left);};
            std::sort(order.begin(),order.begin()+count,[&](auto a,auto b){return left(a)<left(b);});
            for(unsigned i=1;i<count;++i){auto a=order[i-1],b=order[i];
                auto* end=first[a]->left>last[a]->left?first[a]:last[a];
                auto* begin=first[b]->left<last[b]->left?first[b]:last[b];
                line(end->left,end->top,begin->left,begin->top,s.foreground);
            }
        }else for(const auto& b:bars){
            const int top=std::clamp(b.top,0,h),left=std::max(0,b.left),right=std::min(w,b.right);
            if(top>=h || right<=left)continue;
            if(s.style==Style::Vertical){foreground_ramp_.draw(target,left,top,right,h,0,h);continue;}
            if(s.style==Style::GradientBars){bar_ramp_.draw(target,left,top,right,h,top,h-top,true);continue;}
            const unsigned index=(s.style==Style::Horizontal || s.style==Style::HorizontalAntialias)?level_color_index(b.level):23;
            const auto color=(s.style==Style::Horizontal || s.style==Style::HorizontalAntialias)?horizontal_color(s,index):s.foreground;
            solid_->SetColor(d2d_color(color));
            target->FillRectangle(D2D1::RectF(float(left),float(top),float(right),float(h)),solid_.Get());
            const int blur=h*int(s.blur)/100;
            if(blur>0 && (s.style==Style::SimpleAntialias || s.style==Style::HorizontalAntialias)){
                blur_[index]->SetStartPoint({0,float(top)});blur_[index]->SetEndPoint({0,float(top+blur)});
                target->FillRectangle(D2D1::RectF(float(left),float(top),float(right),float(std::min(h,top+blur))),blur_[index].Get());
            }
        }
        if(s.show_peaks){solid_->SetColor(d2d_color(s.peak_color));for(const auto& b:bars)if(b.peak>=0 && b.peak<h)
            target->FillRectangle(D2D1::RectF(float(std::max(0,b.left)),float(b.peak),float(std::min(w,b.right)),float(b.peak+1)),solid_.Get());}
        for(const auto& t:ticks)line(t.x1,t.y1,t.x2,t.y2,s.tick_color);
        return S_OK;
    }
private:
    D2DRamp foreground_ramp_,background_ramp_,bar_ramp_;
    ComPtr<ID2D1SolidColorBrush> solid_;
    std::array<ComPtr<ID2D1LinearGradientBrush>,24> blur_;
    uint32_t background_=0,fg_=0,fg2_=0;bool valid_=false;
};
// Injectable target operations keep failure tests on the real renderer path.
// The default implementation always requests hardware; tests use software HWND targets.
struct D2DTargetOperations {
    virtual ~D2DTargetOperations()=default;
    virtual HRESULT create(ID2D1Factory* factory,HWND window,int w,int h,ID2D1HwndRenderTarget** target) const noexcept {
        const auto properties=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_HARDWARE,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE),96,96);
        return factory->CreateHwndRenderTarget(properties,D2D1::HwndRenderTargetProperties(window,D2D1::SizeU(w,h),D2D1_PRESENT_OPTIONS_IMMEDIATELY),target);
    }
    virtual HRESULT resize(ID2D1HwndRenderTarget* target,D2D1_SIZE_U size) const noexcept {return target->Resize(size);}
    virtual ULONGLONG overlay_time_ms() const noexcept {return GetTickCount64();}
    virtual HRESULT create_fps_format(IDWriteTextFormat** out) const noexcept {
        ComPtr<IDWriteFactory> factory;
        auto hr=DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(factory.GetAddressOf()));
        if(FAILED(hr))return hr;
        return factory->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,float(fps_font_pixels),L"",out);
    }
    virtual HRESULT end_draw(ID2D1HwndRenderTarget* target) const noexcept {return target->EndDraw();}
    static const D2DTargetOperations& system() noexcept {static const D2DTargetOperations operations;return operations;}
};
class D2DRenderer {
public:
    // A custom operations object must outlive this renderer; the production default is static.
    explicit D2DRenderer(const D2DTargetOperations& operations=D2DTargetOperations::system()) noexcept:operations_(operations){}
    D2DRenderer(const D2DRenderer&)=delete;
    D2DRenderer& operator=(const D2DRenderer&)=delete;
    bool active() const noexcept {return target_!=nullptr;}
    template<class Fallback> bool draw_or_fallback(HWND window,int w,int h,const std::vector<Bar>& bars,std::span<const Tick> ticks,const Settings& s,Fallback&& fallback,const wchar_t* fps=nullptr){
        if(draw(window,w,h,bars,ticks,s,fps))return true;
        fallback();return false;
    }
    void retry_overlay() noexcept {overlay_retry_at_=0;}
    void reset() noexcept {retry_overlay();overlay_brush_.Reset();scene_.clear();target_.Reset();factory_.Reset();failed_=false;}
    bool failed() const noexcept {return failed_;}
    bool draw(HWND window,int w,int h,const std::vector<Bar>& bars,std::span<const Tick> ticks,const Settings& s,const wchar_t* fps=nullptr) noexcept {
        if(!s.direct2d || !d2d_supported(s) || failed_ || w<1 || h<1)return false;
        try {
            if(!factory_ && FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,factory_.GetAddressOf())))return fail();
            if(!target_){
                auto hr=operations_.create(factory_.Get(),window,w,h,target_.GetAddressOf());
                if(FAILED(hr))return fail();
            }else {auto size=target_->GetPixelSize();if((size.width!=unsigned(w) || size.height!=unsigned(h)) && FAILED(operations_.resize(target_.Get(),D2D1::SizeU(w,h))))return fail();}
            bool overlay_ready=false;
            if(fps && fps_fits(w,h) && (!overlay_retry_at_ || operations_.overlay_time_ms()>=overlay_retry_at_)){
                const HRESULT hr=prepare_fps();
                if(FAILED(hr)){
                    if(hr==D2DERR_RECREATE_TARGET || hr==DXGI_ERROR_DEVICE_REMOVED ||
                        hr==DXGI_ERROR_DEVICE_RESET || hr==DXGI_ERROR_DEVICE_HUNG)return fail();
                    // Keep painting the spectrum. Retry optional text on a later frame.
                    overlay_retry_at_=operations_.overlay_time_ms()+1000;
                }else{
                    overlay_retry_at_=0;
                    overlay_ready=true;
                }
            }
            // Logical coordinates remain physical pixels, matching the existing Engine.
            target_->SetTransform(d2d_transform(s.rotation,w,h));target_->BeginDraw();
            auto hr=scene_.draw(target_.Get(),s.rotation&1?h:w,s.rotation&1?w:h,bars,ticks,s);
            if(SUCCEEDED(hr) && overlay_ready)draw_fps(w,h,fps);
            auto end=operations_.end_draw(target_.Get());if(FAILED(hr) || FAILED(end))return fail();return true;
        }catch(...){return fail();}
    }
private:
    HRESULT prepare_fps() noexcept {
        if(!text_format_){
            ComPtr<IDWriteTextFormat> format;
            auto hr=operations_.create_fps_format(format.GetAddressOf());
            if(FAILED(hr))return hr;
            hr=format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            if(FAILED(hr))return hr;
            hr=format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            if(FAILED(hr))return hr;
            text_format_=std::move(format);
        }
        if(!overlay_brush_){
            auto hr=target_->CreateSolidColorBrush(d2d_color(0),overlay_brush_.GetAddressOf());
            if(FAILED(hr))return hr;
        }
        return S_OK;
    }
    void draw_fps(int w,int h,const wchar_t* label) noexcept {
        target_->SetTransform(D2D1::Matrix3x2F::Identity());
        const auto box=D2D1::RectF(float(w-fps_box_width),2.f,float(w-2),float(2+fps_box_height));
        overlay_brush_->SetColor(d2d_color(0x202020));target_->FillRectangle(box,overlay_brush_.Get());
        overlay_brush_->SetColor(d2d_color(0xffffff));
        auto text=box;text.right-=3;text.top+=1;
        target_->DrawText(label,UINT32(wcslen(label)),text_format_.Get(),text,overlay_brush_.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    ULONGLONG overlay_retry_at_=0;
    ComPtr<IDWriteTextFormat> text_format_;
    ComPtr<ID2D1SolidColorBrush> overlay_brush_;
    bool fail() noexcept {reset();failed_=true;return false;} // Retry on Apply or window recreation, never every frame.
    const D2DTargetOperations& operations_;
    ComPtr<ID2D1Factory> factory_;ComPtr<ID2D1HwndRenderTarget> target_;D2DScene scene_;bool failed_=false;
};
}
