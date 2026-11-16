#pragma once
#include <windows.h>
#include "spectrum.h"
#include "gdi_render.h"
namespace csp {
struct RenderCache {GradientCache gradients;ContourCache contour;};
// DIB rasterizer: connected one-pixel contour, no area fill or antialiasing.
inline void line(uint32_t* pixels,int width,int height,int x0,int y0,int x1,int y1,uint32_t color=0xe4e4e4) {
    const int dx=std::abs(x1-x0),sx=x0<x1?1:-1,dy=-std::abs(y1-y0),sy=y0<y1?1:-1;
    int error=dx+dy;
    for (;;) {
        if(x0>=0 && x0<width && y0>=0 && y0<height)pixels[size_t(y0)*width+x0]=color;
        if(x0==x1 && y0==y1)break;
        const int e2=2*error;
        if(e2>=dy){error+=dy;x0+=sx;}if(e2<=dx){error+=dx;y0+=sy;}
    }
}
inline uint32_t mix(uint32_t a,uint32_t b,double t) {
    t=std::clamp(t,0.0,1.0);uint32_t out=0;
    for(unsigned shift:{0u,8u,16u})out|=uint32_t(((a>>shift)&255)*(1-t)+((b>>shift)&255)*t)<<shift;
    return out;
}
inline uint32_t palette(const std::array<uint32_t,8>& colors,double value){
    double p=std::clamp(value,0.0,1.0)*7;unsigned i=std::min(6u,unsigned(p));return mix(colors[i],colors[i+1],p-i);
}
inline void render(uint32_t* pixels,int width,int height,const std::vector<Bar>& bars,const Settings& s,bool preserve_background=false,std::span<const Tick> ticks={},RenderCache* cache=nullptr) {
    if(width<=0 || height<=0)return;
    RenderCache local;auto& state=cache?*cache:local;
    auto& gradients=state.gradients;gradients.update(height,s);
    if(!preserve_background)for(int y=0;y<height;++y)std::fill_n(pixels+size_t(y)*width,width,gradients.background[y]);
    if(s.style==Style::SimpleLine) {
        original_contour(pixels,width,height,bars,s.foreground,state.contour);
    } else for(const auto& b:bars) {
        const int top=std::clamp(b.top,0,height);
        for(int y=top;y<height;++y){
            if(s.segmented && (y&1))continue;
            uint32_t color=s.foreground;
            if(s.style==Style::GradientBars)color=mix(s.foreground,s.foreground2,double(y-top)/std::max(1,height-1-top));
            else if(s.style==Style::Vertical)color=gradients.foreground[y];
            else if(s.style==Style::Horizontal || s.style==Style::HorizontalAntialias)
                color=horizontal_color(s,level_color_index(b.level));
            if((s.style==Style::SimpleAntialias || s.style==Style::HorizontalAntialias) && y-top<gradients.blur){
                unsigned index=s.style==Style::SimpleAntialias?23:level_color_index(b.level);
                color=gradients.antialias[index][y-top];
            }
            for(int x=std::max(0,b.left);x<std::min(width,b.right);++x)pixels[size_t(y)*width+x]=color;
        }
    }
    if(s.show_peaks)for(const auto& b:bars)if(b.peak>=0 && b.peak<height)
        for(int x=std::max(0,b.left);x<std::min(width,b.right);++x)pixels[size_t(b.peak)*width+x]=s.peak_color;
    for(const auto& tick:ticks)line(pixels,width,height,tick.x1,tick.y1,tick.x2,tick.y2,s.tick_color);
}
inline void render(uint32_t* pixels,int width,int height,const std::vector<Bar>& bars,Style style=Style::GradientBars) {
    Settings s;s.style=style;render(pixels,width,height,bars,s);
}
inline void rotate_pixels(const uint32_t* source,int w,int h,uint32_t* target,unsigned rotation) {
    if(rotation==0){if(source!=target)std::copy_n(source,size_t(w)*h,target);return;}
    const int outputWidth=(rotation&1)?h:w;
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){
        int dx=x,dy=y;
        if(rotation==1){dx=h-1-y;dy=x;}
        if(rotation==2){dx=w-1-x;dy=h-1-y;}
        if(rotation==3){dx=y;dy=w-1-x;}
        target[size_t(dy)*outputWidth+dx]=source[size_t(y)*w+x];
    }
}
// Zero rotation renders directly into the destination DIB; other rotations use scratch.
inline uint32_t* render_target(uint32_t* destination,int w,int h,unsigned rotation,std::vector<uint32_t>& scratch){
    if(!rotation)return destination;
    scratch.resize(size_t(w)*h);return scratch.data();
}
}
