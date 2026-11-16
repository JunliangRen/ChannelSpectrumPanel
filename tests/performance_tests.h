#pragma once
#include "../src/runtime_state.h"
inline void performance_tests(){
    bool reported=false;int calls=0;
    csp::report_once(reported,"failure",[&](const char*){++calls;throw std::bad_alloc();});
    csp::report_once(reported,"again",[&](const char*){++calls;});
    check(reported && calls==1,"throwing error sink is contained and reported once");
    csp::StopRepaint stop;int repaints=0;
    for(int i=0;i<1000;++i)repaints+=stop.update(false);
    check(repaints==1,"stopped polling requests only one repaint");
    check(!stop.update(true) && stop.update(false) && !stop.update(false),"play-stop transition rearms one repaint");
    std::vector<uint32_t> destination(64*32,0x123456),scratch;
    check(csp::render_target(destination.data(),64,32,0,scratch)==destination.data() && scratch.empty(),"zero rotation uses destination without allocating scratch");
    auto before=destination;csp::rotate_pixels(destination.data(),64,32,destination.data(),0);
    check(destination==before,"zero rotation alias is a no-op");
    auto* rotatedTarget=csp::render_target(destination.data(),64,32,1,scratch);
    check(rotatedTarget==scratch.data() && scratch.size()==destination.size(),"nonzero rotation uses scratch");
    // Warm two distinct panels and forbid C++ allocations during repeated frames.
    // Covers engine temporaries, gradients, and contour points for all eight styles.
    for(unsigned style=0;style<8;++style){
        csp::Engine a,b;csp::RenderCache ca,cb;csp::Settings sa,sb;
        sa.style=static_cast<csp::Style>(style);sa.new_fft=true;
        sb=sa;sb.foreground=0xff4400;sb.blur=45;sb.background_palette=true;
        std::vector<double> samples(2048,.01);
        std::vector<uint32_t> pa(160*80),pb(200*100);
        auto frame=[&](unsigned now){
            a.update(samples,1024,2,0,1,160,80,now,sa);
            b.update(samples,1024,2,0,1,200,100,now,sb);
            csp::render(pa.data(),160,80,a.bars(),sa,false,a.ticks(),&ca);
            csp::render(pb.data(),200,100,b.bars(),sb,false,b.ticks(),&cb);
        };
        frame(0);frame(14);const auto first=pa,second=pb;
        bool allocated=false;
        fail_allocation_after(0);
        try{for(unsigned i=2;i<12;++i)frame(i*14);}catch(const std::bad_alloc&){allocated=true;}
        catch(...){fail_allocation_after(-1);throw;}
        fail_allocation_after(-1);
        check(!allocated,"warmed alternating panels do not allocate C++ frame buffers");
        check(pa==first && pb==second,"independent caches retain stable pixels");
        if(style!=unsigned(csp::Style::Vertical))check(ca.gradients.foreground.empty(),"non-Vertical styles skip foreground gradient");
        if(style!=unsigned(csp::Style::SimpleAntialias) && style!=unsigned(csp::Style::HorizontalAntialias)){
            bool empty=true;for(const auto& colors:ca.gradients.antialias)empty &= colors.empty();
            check(empty,"non-AA styles skip antialias gradients");
        }
        check(ca.gradients.height==80 && cb.gradients.height==100,"alternating panels retain independent cache sizes");
    }
}
