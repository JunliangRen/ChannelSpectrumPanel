#pragma once
#include "../src/preset_save.h"
inline void recovery_tests(){
    int fallbackAttempts=0,monitorWarnings=0;
    auto monitorFallback=[&]{++fallbackAttempts;return false;};
    auto warning=[&]{++monitorWarnings;throw std::bad_alloc();};
    check(csp::start_monitor_tracking(true,monitorFallback,warning) && fallbackAttempts==0,"working listener does not create polling timer");
    check(!csp::start_monitor_tracking(false,monitorFallback,warning) && fallbackAttempts==1 && monitorWarnings==1,"monitor timer failure remains nonfatal even when logging throws");
    bool drawingStarted=false;
    csp::start_monitor_tracking(false,[]{throw std::bad_alloc();return false;},warning);
    csp::start_fallback_timer([&]{drawingStarted=true;return true;},[]{});
    check(drawingStarted,"monitor setup failure cannot interrupt refresh setup");
    check(csp::start_monitor_tracking(false,[]{return true;},warning),"monitor fallback timer can restore tracking");
    check(!csp::refresh_active(true,false,false) && !csp::refresh_active(true,true,true) && !csp::refresh_active(false,true,false),"idle paused and hidden states disable high-frequency refresh");
    check(csp::refresh_active(true,true,false),"visible playback enables refresh after resume");
    bool started=false;int logs=0;
    csp::start_fallback_timer([&]{started=true;return true;},[&]{++logs;if(!started)throw 1;throw std::bad_alloc();});
    check(started && logs==1,"fallback timer starts even when logging fails");
    bool failed=false;logs=0;
    try{csp::start_fallback_timer([]{return false;},[&]{++logs;});}catch(const std::runtime_error&){failed=true;}
    check(failed && logs==0,"timer creation failure is reported before success log");
    csp::Settings s;s.new_fft=true;s.linear_frequency=true;s.hold=0;
    csp::Engine engine;std::vector<double> samples(2048,.1);
    engine.update(samples,1024,2,0,1,160,80,0,s);
    std::fill(samples.begin(),samples.end(),0.001);
    engine.update(samples,1024,2,0,1,160,80,14,s);
    const auto frozen=engine.bars();
    for(unsigned time=28;time<280;time+=14)engine.update(samples,1024,2,0,1,160,80,time,s,48000,0,false);
    bool same=frozen.size()==engine.bars().size();
    for(size_t i=0;i<frozen.size();++i)same &= frozen[i].peak==engine.bars()[i].peak;
    check(same,"paused relayout does not advance held peak animation");
    engine.update(samples,1024,2,0,1,200,100,14,s,48000,0,false);
    check(!engine.bars().empty(),"paused cached spectrum rebuilds after resize");
    bool visible=false;for(const auto& b:engine.bars())visible |= b.top<100;
    check(visible,"paused resized spectrum remains visible");
    engine.reset();s.rotation=1;
    engine.update(samples,1024,2,0,1,100,200,14,s,48000,0,false);
    check(!engine.bars().empty(),"paused settings change rebuilds from retained spectrum");

    // Use the same style-change operation as the context menu. No playback
    // tick runs between selection and rebuilding the cached paused snapshot.
    csp::Settings styleSettings;styleSettings.style=csp::Style::Simple;
    styleSettings.width=5;styleSettings.spacing=3;
    csp::Engine pausedEngine;csp::SnapshotState snapshot;snapshot.capture(styleSettings.new_fft);
    bool dirty=false;
    pausedEngine.update(samples,1024,2,0,1,200,100,14,styleSettings);
    const auto wideBars=pausedEngine.bars();
    for(auto style:{csp::Style::SimpleLine,csp::Style::Horizontal,csp::Style::SimpleLine,csp::Style::Simple}){
        csp::set_drawing_style(styleSettings,style,dirty);
        check(dirty && snapshot.valid,"context style change requests rebuild without discarding paused snapshot");
        if(snapshot.valid && dirty){
            pausedEngine.update(samples,1024,2,0,1,200,100,14,styleSettings,48000,0,false);dirty=false;
        }
        csp::Engine reference;reference.update(samples,1024,2,0,1,200,100,14,styleSettings,48000,0,false);
        const auto& actual=pausedEngine.bars();const auto& expected=reference.bars();
        bool matches=actual.size()==expected.size();
        for(size_t i=0;matches && i<actual.size();++i)
            matches=actual[i].left==expected[i].left && actual[i].right==expected[i].right && actual[i].top==expected[i].top && actual[i].peak==expected[i].peak;
        check(matches && !actual.empty(),"paused menu style rebuild matches fresh target-style geometry and peaks");
        if(style==csp::Style::SimpleLine){
            bool changed=actual.size()!=wideBars.size();
            for(size_t i=0;!changed && i<actual.size();++i)changed=actual[i].left!=wideBars[i].left || actual[i].right!=wideBars[i].right;
            check(changed,"line style replaces old wide-bar geometry while paused");
        }
        const auto still=actual;
        pausedEngine.update(samples,1024,2,0,1,200,100,5000,styleSettings,48000,0,false);
        bool frozenPeaks=still.size()==pausedEngine.bars().size();
        for(size_t i=0;frozenPeaks && i<still.size();++i)frozenPeaks=still[i].peak==pausedEngine.bars()[i].peak;
        check(frozenPeaks,"paused redraw after style switch does not advance peak animation");
        csp::set_drawing_style(styleSettings,style,dirty);check(!dirty,"reselecting current style does not rebuild frozen peaks");
        dirty=true;csp::set_drawing_style(styleSettings,style,dirty);check(dirty,"reselecting current style preserves an existing resize invalidation");dirty=false;
    }

    struct FakeOps {
        int fail,stage=0;bool replaced=false;
        bool write(HANDLE,std::span<const unsigned char>){return ++stage!=fail;}
        bool flush(HANDLE){return ++stage!=fail;}
        bool close(HANDLE){return ++stage!=fail;}
        bool replace(const wchar_t*,const wchar_t*){if(++stage==fail)return false;replaced=true;return true;}
    };
    const std::vector<unsigned char> bytes{'C','S','M','X',1,2,3,4};
    for(int stage=1;stage<=4;++stage){
        FakeOps ops{stage};HANDLE file=reinterpret_cast<HANDLE>(1);failed=false;
        try{csp::finish_preset_save(file,L"temp",L"target",bytes,ops);}catch(const std::runtime_error&){failed=true;}
        check(failed && !ops.replaced && ops.stage==stage,"write/flush/close/replace failure prevents commit");
        check((file==INVALID_HANDLE_VALUE)==(stage>=3),"temporary handle ownership tracks close attempt");
    }
    FakeOps success{0};HANDLE file=reinterpret_cast<HANDLE>(1);
    csp::finish_preset_save(file,L"temp",L"target",bytes,success);
    check(success.replaced && file==INVALID_HANDLE_VALUE,"replacement occurs only after successful close");
    // Real same-directory replacement; no dialogs or player windows.
    auto dir=std::filesystem::path("build")/("preset-io-"+std::to_string(GetCurrentProcessId()));
    check(CreateDirectoryW(dir.c_str(),nullptr)!=FALSE,"create isolated preset IO test directory");
    auto target=dir/L"test.csm";
    auto read=[&]{std::ifstream in(target,std::ios::binary);return std::vector<unsigned char>(std::istreambuf_iterator<char>(in),{});};
    csp::save_preset_atomic(target,bytes);check(read()==bytes,"new preset saved completely");
    const std::vector<unsigned char> replacement{9,8,7,6};
    HANDLE locked=CreateFileW(target.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    check(locked!=INVALID_HANDLE_VALUE,"lock destination against replacement");
    failed=false;
    try{csp::save_preset_atomic(target,replacement);}catch(const std::runtime_error&){failed=true;}
    CloseHandle(locked);
    check(failed && read()==bytes,"failed replacement preserves original preset bytes");
    size_t count=0;for(const auto& entry:std::filesystem::directory_iterator(dir)){(void)entry;++count;}
    check(count==1,"failed save cleans up its temporary file");
    csp::save_preset_atomic(target,replacement);check(read()==replacement,"existing preset replaced after successful write");
    check(DeleteFileW(target.c_str()) && RemoveDirectoryW(dir.c_str()),"preset IO test cleans its files");
}
