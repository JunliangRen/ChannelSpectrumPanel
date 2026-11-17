#pragma once
#include "../src/frame_timer.h"
#include "../src/monitor_refresh.h"
inline void fps_tests(){
    csp::MonitorRefresh screenA,screenB;screenA.hz=csp::refresh_rational(60000,1001);screenB.hz=144;
    check_close(csp::effective_refresh(true,60,screenA),60000.0/1001,"monitor mode keeps fractional Hz");
    check_close(csp::effective_refresh(false,60,screenA),60,"manual mode remains integer");
    check(csp::refresh_changed(csp::effective_refresh(true,60,screenA),csp::effective_refresh(true,60,screenB)),"different monitor rates trigger retiming");
    check(!csp::refresh_changed(screenB.hz,144),"unchanged monitor rate does not restart timer");
    screenB.hz=240;check_close(csp::effective_refresh(true,60,screenB),240,"auto mode supports displays above manual FPS limit");
    screenB.hz=0;check_close(csp::effective_refresh(true,71,screenB),71,"unavailable monitor falls back to manual FPS");
    check(csp::refresh_rational(60000,0)==0 && csp::refresh_rational(0,1)==0,"invalid refresh rationals rejected");
    csp::Settings autoSettings;autoSettings.follow_monitor=true;autoSettings.fps_milli=60000;
    auto autoWords=csp::encode(autoSettings);csp::Settings autoLoaded;
    check(csp::decode(autoWords,autoLoaded) && autoLoaded==autoSettings,"format 5 retains auto mode and manual fallback");
    autoWords.resize(78);check(!csp::decode(autoWords,autoLoaded),"truncated auto flag rejected");
    autoWords[0]=4;check(csp::decode(autoWords,autoLoaded) && !autoLoaded.follow_monitor && autoLoaded.target_fps()==60,"format 4 remains manual");
    csp::Settings legacy;legacy.interval=14;
    auto words=csp::encode(legacy);words[0]=3;words.resize(77);
    csp::Settings loaded;check(csp::decode(words,loaded) && loaded.fps_milli==0 && loaded.interval==14,"v3 animation reference retained");
    check_close(loaded.target_fps(),71,"legacy milliseconds rounded to integer FPS");
    for(unsigned fps:{30000u,60000u,90000u,120000u,144000u,71000u,200000u}){
        loaded.fps_milli=fps;auto v=csp::encode(loaded);csp::Settings result;
        check(csp::decode(v,result) && result==loaded,"v4 FPS and animation basis round trip");
        v.pop_back();check(!csp::decode(v,result),"truncated v4 rejected");
    }
    for(unsigned interval=5;interval<=100;++interval){
        auto v=words;v[2]=interval;csp::Settings result;
        check(csp::decode(v,result) && result.target_fps()==unsigned(std::lround(1000.0/interval)) && result.interval==interval,"all legacy intervals round without changing animation basis");
    }
    for(unsigned value:{71429u,71500u,59940u}){
        auto v=csp::encode(loaded);v[0]=4;v.resize(78);v[77]=value;csp::Settings result;
        check(csp::decode(v,result) && result.fps_milli==((value+500)/1000)*1000,"previous fractional format-4 FPS rounds half up");
    }
    for(const wchar_t* invalid:{L"60.5",L"60.0",L"1e2",L"nan",L"",L"9",L"201",L"-60",L"+60"})check(csp::parse_target_fps(invalid)==0,"FPS rejects fractional or invalid input");
    check(csp::parse_target_fps(L"60")==60 && csp::parse_target_fps(L" 144 ")==144,"FPS accepts whole numbers");
    loaded.style=csp::Style::Simple;loaded.fps_milli=60000;
    std::vector<uint32_t> original;check(csp::encode_original_csp(loaded,original),"FPS settings export to legacy");
    check(csp::decode_original_csp(original,legacy) && legacy.interval==17 && legacy.fps_milli==0,"legacy export rounds target interval");
    for(unsigned hold:{0u,100u}){
        csp::Settings settings;settings.hold=hold;
        csp::Peak reference;reference.update(0,0,settings,1000000);reference.update(1000000,500,settings,1000000);
        for(unsigned fps:{30u,60u,90u,120u,144u,200u}){
            settings.fps_milli=fps*1000;csp::Peak peak;peak.update(0,0,settings,1000000);
            for(unsigned n=1;n*1000/fps<500;++n)peak.update(1000000,n*1000/fps,settings,1000000);
            peak.update(1000000,500,settings,1000000);
            check(peak.position==reference.position && peak.velocity==reference.velocity,"peak motion independent of target FPS and sampling cadence");
            peak.update(1000000,500,settings,1000000);
            check(peak.position==reference.position,"duplicate timestamps do not advance peaks");
        }
    }
    csp::FrameDeadline schedule;schedule.reset(0,1000.0/60);
    check(!schedule.due(16) && schedule.due(17),"fractional frame deadline");
    for(int i=2;i<=600;++i)check(schedule.due(i*1000.0/60+0.1),"deadline stays on original timeline");
    check(std::abs(schedule.next-601*1000.0/60)<1e-6,"no accumulated timer drift");
    check(schedule.due(20000) && !schedule.due(20000),"missed deadlines skip without catch-up bursts");
    // A hidden built-in window exercises real callback cancellation and queue coalescing.
    HWND window=CreateWindowEx(0,L"STATIC",L"",0,0,0,0,0,HWND_MESSAGE,nullptr,GetModuleHandle(nullptr),nullptr);
    check(window!=nullptr,"timer test window created");
    struct WindowCleanup{HWND w;~WindowCleanup(){DestroyWindow(w);}} cleanup{window};
    csp::FrameTimer timer;
    check(timer.start(window,200),"high-resolution timer starts");
    MSG message{};bool received=false;
    const auto timeout=GetTickCount64()+2000;
    while(GetTickCount64()<timeout){
        if(PeekMessage(&message,window,csp::FrameTimer::message,csp::FrameTimer::message,PM_REMOVE)){received=true;break;}
        Sleep(1);
    }
    check(received,"timer posts a UI update");
    Sleep(30);
    MSG duplicate{};check(!PeekMessage(&duplicate,window,csp::FrameTimer::message,csp::FrameTimer::message,PM_REMOVE),"pending update prevents queue backlog");
    const auto stale=message.wParam;
    check(message.message==WM_TIMER && message.lParam==0 && stale>=0x10000,"modal-compatible timer ID never carries callback pointer");
    check(timer.consume(stale),"current timer message accepted");
    timer.stop();check(!timer.consume(stale),"stopped timer message ignored");
    check(timer.start(window,60),"timer can restart");
    check(!timer.consume(stale),"previous generation ignored after restart");
    timer.stop();
    while(PeekMessage(&message,window,csp::FrameTimer::message,csp::FrameTimer::message,PM_REMOVE)){}
    Sleep(30);
    check(!PeekMessage(&message,window,csp::FrameTimer::message,csp::FrameTimer::message,PM_REMOVE),"synchronous stop prevents later callbacks");
}
