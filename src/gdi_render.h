#pragma once
#include <windows.h>
#include <stdexcept>
#include "spectrum.h"
#include "rendered_fps.h"
namespace csp {
// EndPaint must run even if allocation or rendering throws.
class PaintSession {
public:
    explicit PaintSession(HWND window):window_(window),dc(BeginPaint(window,&paint_)){}
    ~PaintSession(){EndPaint(window_,&paint_);}
    PaintSession(const PaintSession&)=delete;PaintSession& operator=(const PaintSession&)=delete;
private:
    HWND window_;PAINTSTRUCT paint_{};
public:
    HDC dc;
};
class GdiFpsOverlay {
public:
    GdiFpsOverlay()=default;
    GdiFpsOverlay(const GdiFpsOverlay&)=delete;
    GdiFpsOverlay& operator=(const GdiFpsOverlay&)=delete;
    ~GdiFpsOverlay(){reset();}
    void reset() noexcept {
        if(font_ && font_!=GetStockObject(DEFAULT_GUI_FONT))DeleteObject(font_);
        font_=nullptr;
    }
    void draw(HDC dc,int w,int h,const wchar_t* label){
        if(!fps_fits(w,h) || !SaveDC(dc))return;
        RECT box{w-fps_box_width,2,w-2,2+fps_box_height};
        if(!font_){
            font_=CreateFontW(-fps_font_pixels,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
                DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"Segoe UI");
            if(!font_)font_=static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        }
        SelectObject(dc,font_);
        SetBkColor(dc,RGB(32,32,32));SetTextColor(dc,RGB(255,255,255));
        SetTextAlign(dc,TA_RIGHT|TA_TOP);
        ExtTextOutW(dc,box.right-3,box.top+1,ETO_CLIPPED|ETO_OPAQUE,&box,label,UINT(wcslen(label)),nullptr);
        RestoreDC(dc,-1);
    }
private:HFONT font_=nullptr;
};
class SelectedPen {
public:
    SelectedPen(HDC dc,COLORREF color):dc_(dc),pen_(CreatePen(PS_SOLID,1,color)){
        if(!pen_)throw std::runtime_error("CreatePen failed");
        old_=SelectObject(dc_,pen_);
        if(!old_ || old_==HGDI_ERROR){DeleteObject(pen_);throw std::runtime_error("SelectObject pen failed");}
    }
    ~SelectedPen(){SelectObject(dc_,old_);DeleteObject(pen_);}
    SelectedPen(const SelectedPen&)=delete;SelectedPen& operator=(const SelectedPen&)=delete;
private:HDC dc_;HPEN pen_;HGDIOBJ old_;
};
inline unsigned level_color_index(double level){
    if(!(level>0))return 0;
    if(level>=1)return 23;
    return unsigned(level*24);
}
// Memory DC only: use the same GDI GradientFill / Polyline raster rules as the x86 panel.
class GdiSurface {
public:
    GdiSurface()=default;GdiSurface(const GdiSurface&)=delete;GdiSurface& operator=(const GdiSurface&)=delete;
    ~GdiSurface(){clear();}
    void resize(int w,int h){
        if(w==width && h==height)return;clear();
        dc=CreateCompatibleDC(nullptr);if(!dc)throw std::runtime_error("CreateCompatibleDC failed");
        BITMAPINFO bi{};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=w;
        bi.bmiHeader.biHeight=-h;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;
        bitmap=CreateDIBSection(dc,&bi,DIB_RGB_COLORS,reinterpret_cast<void**>(&pixels),nullptr,0);
        if(!bitmap){clear();throw std::runtime_error("CreateDIBSection failed");}
        old=SelectObject(dc,bitmap);
        if(!old || old==HGDI_ERROR){old=nullptr;clear();throw std::runtime_error("SelectObject bitmap failed");}
        width=w;height=h;
    }
    void gradient(int top,int bottom,uint32_t first,uint32_t last){
        if(bottom<=top)return;
        auto vertex=[](int x,int y,uint32_t color){return TRIVERTEX{LONG(x),LONG(y),COLOR16(((color>>16)&255)<<8),COLOR16(((color>>8)&255)<<8),COLOR16((color&255)<<8),0};};
        TRIVERTEX vertices[]={vertex(0,top,first),vertex(width,bottom,last)};
        GRADIENT_RECT rect{0,1};
        if(!GradientFill(dc,vertices,2,&rect,1,GRADIENT_FILL_RECT_V))throw std::runtime_error("GradientFill failed");
    }
    void clear(){if(dc && old)SelectObject(dc,old);if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);
        dc=nullptr;bitmap=nullptr;old=nullptr;pixels=nullptr;width=height=0;}
    HDC dc=nullptr;uint32_t* pixels=nullptr;int width=0,height=0;
private:HBITMAP bitmap=nullptr;HGDIOBJ old=nullptr;
};
inline uint32_t horizontal_color(const Settings& s,unsigned index){
    uint32_t result=0;index=std::min(index,23u);
    for(unsigned shift:{0u,8u,16u}){
        int a=(s.foreground2>>shift)&255,b=(s.foreground>>shift)&255;
        result|=uint32_t(std::clamp(int(a+int(index)*(b-a)*(1.0/23)),0,255))<<shift;
    }
    return result;
}
struct GradientCache {
    int height=0,blur=0;Settings settings{};bool valid=false;GdiSurface surface;
    std::vector<uint32_t> foreground,background;std::array<std::vector<uint32_t>,24> antialias;
    void update(int h,const Settings& s){
        if(valid && h==height && s==settings)return;
        height=h;settings=s;valid=false;blur=h*int(s.blur)/100;
        auto fill=[&](std::vector<uint32_t>& out,int length,bool multi,const std::array<uint32_t,8>& colors,uint32_t top,uint32_t bottom){
            out.clear();if(length<1)return;surface.resize(1,length);
            if(multi)for(int i=0;i<7;++i)surface.gradient(length*i/7,length*(i+1)/7,colors[7-i],colors[6-i]);
            else surface.gradient(0,length,top,bottom);
            GdiFlush();out.assign(surface.pixels,surface.pixels+length);
            for(auto& p:out)p&=0xffffff;
        };
        if(s.background_palette)fill(background,h,true,s.background_colors,s.background,s.background);
        else background.assign(h,s.background);
        if(s.style==Style::Vertical)fill(foreground,h,s.foreground_palette,s.foreground_colors,horizontal_color(s,23),horizontal_color(s,0));
        else foreground.clear();
        for(unsigned n=0;n<24;++n){
            if(s.style==Style::HorizontalAntialias || (s.style==Style::SimpleAntialias && n==23))
                fill(antialias[n],blur,false,s.foreground_colors,s.background,horizontal_color(s,n));
            else antialias[n].clear();
        }
        valid=true;
    }
};
struct ContourCache {
    GdiSurface surface;
    std::array<std::vector<POINT>,18> channels;
    std::vector<size_t> physical;
};
inline void original_contour(uint32_t* pixels,int w,int h,const std::vector<Bar>& bars,uint32_t color,ContourCache& cache){
    if(bars.empty())return;
    auto& surface=cache.surface;surface.resize(w,h);
    std::copy_n(pixels,size_t(w)*h,surface.pixels);
    SelectedPen pen(surface.dc,RGB((color>>16)&255,(color>>8)&255,color&255));
    auto& channels=cache.channels;for(auto& points:channels)points.clear();
    for(const auto& b:bars)if(b.channel<channels.size())channels[b.channel].push_back({b.left,b.top});
    // Boundary points connect adjacent physical channel partitions, preserving each channel's samples.
    auto& physical=cache.physical;physical.clear();for(size_t n=0;n<channels.size();++n)if(!channels[n].empty())physical.push_back(n);
    auto leftmost=[&](size_t n){return std::min(channels[n].front().x,channels[n].back().x);};
    std::sort(physical.begin(),physical.end(),[&](auto a,auto b){return leftmost(a)<leftmost(b);});
    for(auto& points:channels)if(points.size()>1)Polyline(surface.dc,points.data(),int(points.size()));
    for(size_t i=1;i<physical.size();++i){
        const auto& a=channels[physical[i-1]];const auto& b=channels[physical[i]];
        POINT link[]={a.front().x>a.back().x?a.front():a.back(),b.front().x<b.back().x?b.front():b.back()};
        Polyline(surface.dc,link,2);
    }
    GdiFlush();
    std::copy_n(surface.pixels,size_t(w)*h,pixels);
    for(size_t i=0;i<size_t(w)*h;++i)pixels[i]&=0xffffff;
}
}
