#pragma once
#include <windows.h>
#include <stdexcept>
#include "spectrum.h"
namespace csp {
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
        old=SelectObject(dc,bitmap);width=w;height=h;
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
        fill(background,h,s.background_palette,s.background_colors,s.background,s.background);
        fill(foreground,h,s.foreground_palette,s.foreground_colors,horizontal_color(s,23),horizontal_color(s,0));
        for(unsigned n=0;n<24;++n)fill(antialias[n],blur,false,s.foreground_colors,s.background,horizontal_color(s,n));
        valid=true;
    }
};
inline void original_contour(uint32_t* pixels,int w,int h,const std::vector<Bar>& bars,uint32_t color){
    if(bars.empty())return;
    static thread_local GdiSurface surface;surface.resize(w,h);
    std::copy_n(pixels,size_t(w)*h,surface.pixels);
    HPEN pen=CreatePen(PS_SOLID,1,RGB((color>>16)&255,(color>>8)&255,color&255));
    if(!pen)throw std::runtime_error("CreatePen failed");
    HGDIOBJ old=SelectObject(surface.dc,pen);
    std::vector<std::vector<POINT>> channels;
    for(const auto& b:bars){if(channels.size()<=b.channel)channels.resize(b.channel+1);channels[b.channel].push_back({b.left,b.top});}
    // Boundary points connect adjacent physical channel partitions, preserving each channel's samples.
    std::vector<size_t> physical;for(size_t n=0;n<channels.size();++n)if(!channels[n].empty())physical.push_back(n);
    auto leftmost=[&](size_t n){return std::min(channels[n].front().x,channels[n].back().x);};
    std::sort(physical.begin(),physical.end(),[&](auto a,auto b){return leftmost(a)<leftmost(b);});
    for(auto& points:channels)if(points.size()>1)Polyline(surface.dc,points.data(),int(points.size()));
    for(size_t i=1;i<physical.size();++i){
        const auto& a=channels[physical[i-1]];const auto& b=channels[physical[i]];
        POINT link[]={a.front().x>a.back().x?a.front():a.back(),b.front().x<b.back().x?b.front():b.back()};
        Polyline(surface.dc,link,2);
    }
    GdiFlush();SelectObject(surface.dc,old);DeleteObject(pen);
    std::copy_n(surface.pixels,size_t(w)*h,pixels);
    for(size_t i=0;i<size_t(w)*h;++i)pixels[i]&=0xffffff;
}
}
