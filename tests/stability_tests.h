#pragma once
#include "../src/preset.h"
void fail_allocation_after(int successfulAllocations);
inline void stability_tests(){
    csp::Settings s;s.new_fft=true;s.hold=10000;s.linear_frequency=true;
    csp::Engine engine,fresh;
    std::vector<double> loud(1024*2,1),silence(1024*2,0);
    engine.update(loud,1024,2,0,1,160,80,0,s);
    engine.update({},1024,2,0,1,160,80,1,s);
    check(engine.bars().empty(),"invalid frame clears visible output");
    engine.update(silence,1024,2,0,1,160,80,2,s);
    for(const auto& b:engine.bars())check(b.peak==80,"invalid frame discards held peaks before recovery");
    // Fail each early allocation during a resize, then retry the same dimensions.
    // A cached partial rebuild used to skip rebuilding and index incomplete vectors.
    for(int allocation=0;allocation<32;++allocation){
        engine.reset();engine.update(silence,1024,2,0,1,160,80,0,s);
        fail_allocation_after(allocation);
        try {engine.update(silence,1024,2,0,1,180,90,1,s);}
        catch(const std::bad_alloc&){}
        catch(...) {fail_allocation_after(-1);throw;}
        fail_allocation_after(-1);
        engine.update(silence,1024,2,0,1,180,90,2,s);
        fresh.reset();fresh.update(silence,1024,2,0,1,180,90,2,s);
        const auto& a=engine.bars();const auto& b=fresh.bars();
        check(a.size()==b.size(),"allocation failure recovery rebuilds complete descriptors");
        bool same=true;for(size_t i=0;i<a.size();++i)
            same &= a[i].left==b[i].left && a[i].right==b[i].right && a[i].top==b[i].top && a[i].peak==b[i].peak;
        check(same,"allocation failure retry matches fresh engine");
    }
    // Channel layout changes must not carry old channel peaks into a new layout.
    s.all_channels=true;
    engine.update(loud,1024,2,0,1,160,80,3,s,48000,3);
    engine.update(silence,1024,2,0,1,160,80,4,s,48000,12);
    for(const auto& b:engine.bars())check(b.peak==80,"speaker layout change resets old peak history");
    engine.update(loud,1024,2,0,1,160,80,5,s);
    engine.reset();engine.update(silence,1024,2,0,1,160,80,6,s);
    for(const auto& b:engine.bars())check(b.peak==80,"explicit stop/hide reset discards held peaks");

    check(csp::level_color_index(std::numeric_limits<double>::max())==23,"huge level saturates color before integer conversion");
    check(csp::level_color_index(std::numeric_limits<double>::infinity())==23,"infinite level saturates color");
    check(csp::level_color_index(std::numeric_limits<double>::quiet_NaN())==0,"NaN level selects safe color");
    check(csp::level_color_index(-1)==0,"negative level selects safe color");
    check(csp::level_color_index(.5)==12,"normal color scale unchanged");
    s={};s.style=csp::Style::Horizontal;s.show_peaks=false;
    std::vector<uint32_t> pixels(16*16);
    std::vector<csp::Bar> bars{{0,16,0,16,0,std::numeric_limits<double>::max()}};
    csp::render(pixels.data(),16,16,bars,s);
    check(pixels[0]==s.foreground,"extreme amplitude renders brightest horizontal color");
    const auto first=pixels;
    auto other=s;other.foreground=0xff0000;other.background=0x123456;
    csp::render(pixels.data(),16,16,bars,other);
    csp::render(pixels.data(),16,16,bars,s);
    check(pixels==first,"shared renderer cache does not bleed colors between panels");

    // Actual memory DCs, without a player or a visible window.
    csp::GdiSurface surface;surface.resize(16,16);
    const auto originalPen=GetCurrentObject(surface.dc,OBJ_PEN);
    const DWORD resources=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    for(int n=0;n<64;++n){
        try {csp::SelectedPen pen(surface.dc,RGB(1,2,3));throw std::runtime_error("injected drawing failure");}
        catch(const std::runtime_error&){}
        check(GetCurrentObject(surface.dc,OBJ_PEN)==originalPen,"exception restores previous GDI pen");
    }
    GdiFlush();check(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==resources,"exception path does not leak GDI objects");
    for(int n=0;n<64;++n){csp::GdiSurface temporary;temporary.resize(16+n,8);temporary.resize(8,16+n);}
    GdiFlush();check(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==resources,"repeated DIB resize/destruction does not leak GDI objects");

    csp::Settings expected;expected.hold=8765;expected.foreground=0x123456;
    const auto words=csp::encode(expected);
    std::vector<unsigned char> bytes{'C','S','M','X'};
    for(auto word:words)for(int i=0;i<4;++i)bytes.push_back(static_cast<unsigned char>(word>>(8*i)));
    csp::Settings target;target.background=0x654321;const auto unchanged=target;
    for(size_t length=0;length<bytes.size();++length){
        check(!csp::decode_preset(std::span(bytes).first(length),target),"truncated CSMX rejected");
        check(target==unchanged,"failed preset import is atomic");
    }
    check(csp::decode_preset(bytes,target) && target==expected,"complete CSMX imports correctly");
    csp::Settings host;
    check(csp::decode({},host) && host==csp::Settings{},"empty host config still initializes defaults");
    const std::vector<unsigned char> legacyHeader{0x37,0x27,0x17,0x07};
    check(csp::decode_preset(legacyHeader,target) && target==expected,"original header-only prefix retains current settings");
}
