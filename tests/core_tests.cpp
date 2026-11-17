#define CSP_SAVE_TEST_DIAGNOSTICS
#include "../src/spectrum.h"
#include "../src/render.h"
#include "../src/config.h"
#include "../src/original_config.h"
#include <fstream>
#include <float.h>
#include <iostream>
#include <limits>
#include <stdexcept>
int checks=0;
void check(bool ok,const char* name) {++checks;if(!ok)throw std::runtime_error(name);}
void check_close(double a,double b,const char* name){check(std::abs(a-b)<1e-9,name);}
#include "stability_tests.h"
#include "performance_tests.h"
#include "recovery_tests.h"
#include "async_save_tests.h"
#include "save_conflict_tests.h"
#include "reservation_stress_tests.h"
#include "settings_layout_tests.h"
#include "fps_tests.h"
#include "scanline_tests.h"
#include "monitor_events_tests.h"
#include "settings_options_tests.h"
#include "d2d_tests.h"
#include "localization_tests.h"
#include "../src/installation_guard.h"
int main(int argc,char** argv) {
 try {
    if(argc>1 && std::string(argv[1])=="--save-stress"){
        for(int i=0;i<500;++i){save_conflict_tests();if(i%25==0)std::cout<<"Save iteration "<<i<<std::endl;}
        std::cout<<"PASS: 500 save-conflict iterations, "<<checks<<" checks; retries access="<<csp::preset_retry_access<<", sharing="<<csp::preset_retry_sharing<<", lock="<<csp::preset_retry_lock<<".\n";return 0;
    }
    settings_options_tests();
    check(csp::legacy_components_absent([](const wchar_t*){return false;}),"clean installation accepts the unified DLL");
    for(const wchar_t* legacy:{L"foo_uie_channel_spectrum_x86.dll",L"foo_uie_channel_spectrum_x64.dll",L"foo_uie_channel_spectrum_arm64ec.dll"})
        check(!csp::legacy_components_absent([&](const wchar_t* name){return wcscmp(name,legacy)==0;}),"each loaded legacy DLL rejects mixed installation");
    localization_tests();
    d2d_tests();
    monitor_events_tests();
    fps_tests();
    scanline_tests();
    settings_layout_tests();
    stability_tests();
    performance_tests();
    recovery_tests();
    async_save_tests();
    save_conflict_tests();
    reservation_stress_tests();
    // The assembly helper must preserve the caller's rounding mode and stack
    // across repeated calls, and return the same pixels in either ABI.
    unsigned int savedControl=0;
    _controlfp_s(&savedControl,0,0);
    csp::MappingInput mappingInput{173,5,1024,890,1};
    csp::MappingOutput mappingReference{};
    csp::csp_mapping_coordinates(&mappingInput,&mappingReference);
    for (unsigned int rounding : {_RC_NEAR,_RC_DOWN,_RC_UP,_RC_CHOP}) {
        unsigned int before=0,after=0;
        _controlfp_s(&before,rounding,_MCW_RC);
        for(int repeat=0;repeat<64;++repeat) {
            csp::MappingOutput output{};
            csp::csp_mapping_coordinates(&mappingInput,&output);
            check(output.previous==mappingReference.previous && output.current==mappingReference.current && output.wide==mappingReference.wide,"mapping independent of caller rounding mode");
        }
        _controlfp_s(&after,0,0);
        check(before==after,"mapping restores floating-point control state");
    }
    unsigned int restoredControl=0;
    _controlfp_s(&restoredControl,savedControl,_MCW_RC);
    csp::Settings s;
    const double k=.2309*std::log2(1025.0);
    check_close(csp::amplitude(k,1024,s),1,"old FFT unity reference");
    s.log_amplitude=true;check_close(csp::amplitude(k,1024,s),1,"old logarithmic reference");
    s.new_fft=true;s.log_amplitude=false;
    check_close(csp::amplitude(.01,1024,s),3.0/7,"new FFT -40dB point");
    s.log_amplitude=true;check_close(csp::amplitude(.01,1024,s),.5,"new logarithmic -40dB point");
    check_close(csp::amplitude(0,1024,s),0,"silence");
    check_close(csp::amplitude(std::numeric_limits<double>::infinity(),1024,s),0,"infinity rejected");
    s={};s.hold=100;
    csp::Peak p;p.update(20,1000,s,400);p.update(200,1099,s,400);
    check(p.position==20*256,"hold before boundary");
    p.update(200,1100,s,400);check(p.position==20*256+36,"hold exact boundary");
    for(int n=1;n<10;++n)p.update(200,1100+n*14,s,400);
    check(p.position==20*256+36*55,"ten-frame triangular descent");
    p.update(0,1300,s,400);check(p.position==0 && p.velocity==36,"new peak resets velocity");
    p.update(0,1390,s,400);p.update(200,1489,s,400);check(p.position==0,"equal peak renews hold");
    p={};p.update(20,0xfffffff0,s,400);p.update(200,0x30,s,400);
    check(p.position==20*256,"GetTickCount wrap hold");
    p.update(200,0x54,s,400);check(p.position==20*256+36,"GetTickCount wrap expiration");
    csp::Engine engine;s={};s.new_fft=true;s.linear_frequency=true;
    std::vector<double> data(1024*2,0);data[2]=1;
    engine.update(data,1024,2,0,1,800,200,0,s);
    bool left=false,right=false;int leftx=-1;
    for(auto b:engine.bars())if(b.top<200){if(b.left<400){left=true;leftx=b.left;}else right=true;}
    check(left && !right,"left channel isolation");check(leftx>=398,"low frequency beside center");
    data.assign(data.size(),0);data[1023*2+1]=1;engine.reset();
    engine.update(data,1024,2,0,1,800,200,0,s);
    int rightx=-1;for(auto b:engine.bars())if(b.top<200)rightx=b.left;
    check(rightx>=798,"high frequency at outer edge");
    data.assign(data.size(),0);data[100*2]=.01;data[101*2]=1;
    engine.reset();engine.update(data,1024,2,0,1,40,200,0,s);
    int maxheight=0;for(auto b:engine.bars())maxheight=std::max(maxheight,200-b.top);
    check(maxheight==200,"shared-bin group uses max not average");
    data.assign(data.size(),0);for(unsigned i=1;i<1024;++i)data[i*2]=data[i*2+1]=double(i%73)/73;
    s.linear_frequency=false;
    for(int width:{1,2,3,17,800,1780}) {
        engine.reset();engine.update(data,1024,2,0,1,width,200,0,s);
        for(auto b:engine.bars())check(b.left>=0 && b.right<=width && b.right>=b.left,"geometry within bounds");
        auto& b=engine.bars();
        for(size_t i=0;i<b.size()/2;++i){auto a=b[i],z=b[i+b.size()/2];check(a.top==z.top,"equal channels have equal heights");}
    }
    engine.update({},1024,2,0,1,800,200,0,s);check(engine.bars().empty(),"truncated spectrum clears display");
    data.assign(data.size(),0); data[2]=1; s={}; s.new_fft=true;
    engine.reset();engine.update(data,1024,2,0,1,800,200,0,s);
    data.assign(data.size(),0);
    engine.update(data,1024,2,1,0,800,200,14,s);
    bool stale=false;for(auto b:engine.bars())if(b.peak<200)stale=true;
    check(!stale,"channel layout change resets peak history");
    // Pixel-level style checks: no fill, channel boundary separation, and peak overlay.
    std::vector<csp::Bar> shape={{3,4,2,0},{1,2,2,0},{4,5,6,4},{6,7,6,4}};
    shape[2].channel=shape[3].channel=1;
    std::vector<uint32_t> raster(8*8);
    csp::render(raster.data(),8,8,shape,csp::Style::SimpleLine);
    check(raster[2*8+2]==0xe4e4e4,"contour connects same-channel points");
    check(raster[5*8+2]==0,"contour does not fill below line");
    check(raster[4*8+3]==0xe4e4e4,"original contour bridges neighbouring partitions without filling");
    check(raster[3]==0xf5f5f5,"contour peak dot overlay retained");
    csp::render(raster.data(),8,8,shape,csp::Style::SeparateLines);
    check(raster[7*8+3]==0xe4e4e4 && raster[7*8+2]==0,"separate lines are solid with black gaps");
    for(auto style:{csp::Style::SimpleLine,csp::Style::SeparateLines}) {
        s={};s.style=style;s.new_fft=true;s.width=8;s.spacing=0;
        check(s.effective_width()==1 && s.pitch()==2,"line styles use thin strokes and spacing");
        data.assign(data.size(),0);data[2]=1;
        engine.reset();engine.update(data,1024,2,0,1,800,200,0,s);
        bool litLeft=false,litRight=false;
        for(auto b:engine.bars())if(b.top<200){if(b.channel==0)litLeft=true;else litRight=true;}
        check(litLeft && !litRight,"line styles retain channel isolation");
        data.assign(data.size(),0);engine.update(data,1024,2,0,1,800,200,14,s);
        bool peak=false;for(auto b:engine.bars())if(b.peak<200)peak=true;
        check(peak,"line styles retain falling peaks after signal ends");
        for(int width:{2,3,17,800,1780}) {
            engine.reset();engine.update(data,1024,2,0,1,width,200,0,s);
            std::vector<uint32_t> guard(size_t(width)*200+2,0xabcdef);
            csp::render(guard.data()+1,width,200,engine.bars(),style);
            check(guard.front()==0xabcdef && guard.back()==0xabcdef,"line renderer preserves buffer guards");
        }
    }
    // Settings persistence: backward compatibility, roundtrip, and truncated/unknown rejection.
    s={};s.style=csp::Style::Horizontal;s.rotation=3;s.padding_left=13;s.foreground=0x123456;
    s.transparent=true;s.scanline_sync=true;s.foreground_palette=true;s.foreground_colors[3]=0x112233;
    s.order[0]=1;s.order[1]=0;
    auto words=csp::encode(s);csp::Settings decoded;
    check(csp::decode(words,decoded) && decoded==s,"all settings roundtrip");
    for(size_t n=1;n<words.size();++n)check(!csp::decode(std::span(words.data(),n),decoded),"truncated configuration rejected");
    words[0]=99;check(!csp::decode(words,decoded),"unknown configuration rejected");
    words=csp::encode(s);words.resize(11);words[0]=1;
    check(csp::decode(words,decoded) && decoded.style==csp::Style::GradientBars && decoded.fft==s.fft,"v1 settings migrate");
    words=csp::encode(s);words.resize(12);words[0]=2;words[11]=1;
    check(csp::decode(words,decoded) && decoded.style==csp::Style::SimpleLine,"v2 settings migrate");
    // Distinct input channels: max merge and explicit selection.
    s={};s.new_fft=true;s.channel_mode=1;data.assign(1024*3,0);data[12*3+2]=1;
    engine.reset();engine.update(data,1024,3,0,1,120,80,0,s);
    bool found=false;for(auto b:engine.bars())if(b.top==0)found=true;
    check(found,"merge includes third input channel");
    s.channel_mode=2;s.selected=1;engine.update(data,1024,3,0,1,120,80,14,s);
    found=false;for(auto b:engine.bars())if(b.top<80)found=true;check(!found,"selection excludes other channels");
    s.selected=2;engine.update(data,1024,3,0,1,120,80,28,s);
    found=false;for(auto b:engine.bars())if(b.top==0)found=true;check(found,"selection chooses requested channel");
    s={};s.new_fft=true;s.all_channels=true;s.padding_left=7;s.padding_right=9;s.channel_gap=5;
    s.order[0]=2;s.order[2]=0;
    engine.update(data,1024,3,0,1,120,80,42,s);
    found=false;for(auto b:engine.bars()){check(b.left>=7 && b.right<=111,"padding bounds");if(b.top==0 && b.channel==0)found=true;}
    check(found,"custom channel order changes first display source");
    // Direction, reverse and every style across tiny and regular dimensions.
    for(unsigned style=0;style<8;++style)for(unsigned dir=0;dir<3;++dir)for(bool reverse:{false,true}){
        s={};s.style=static_cast<csp::Style>(style);s.direction=dir;s.reverse_frequency=reverse;s.all_channels=true;s.segmented=true;
        for(int width:{1,2,3,17,121}){engine.reset();engine.update(data,1024,3,0,1,width,31,0,s);
            std::vector<uint32_t> storage(size_t(width)*31+2,0xabcdef);
            csp::render(storage.data()+1,width,31,engine.bars(),s);
            check(storage.front()==0xabcdef && storage.back()==0xabcdef,"all modes preserve buffer guards");
            for(auto b:engine.bars())check(b.left>=0 && b.right<=width && b.left<=b.right,"all modes geometry");
        }
    }
    std::array<uint32_t,6> source={1,2,3,4,5,6},target{},restored{};
    for(unsigned rot=0;rot<4;++rot){csp::rotate_pixels(source.data(),3,2,target.data(),rot);
        csp::rotate_pixels(target.data(),(rot&1)?2:3,(rot&1)?3:2,restored.data(),(4-rot)%4);
        check(source==restored,"rotation exact inverse");}
    s={};s.style=csp::Style::Simple;s.show_peaks=false;s.foreground=0x123456;s.background=0x654321;
    csp::render(raster.data(),8,8,shape,s);check(raster[3]==s.background,"peak disable works");
    check(raster[7*8+3]==s.foreground,"RGB foreground is used");
    std::fill(raster.begin(),raster.end(),0x999999);csp::render(raster.data(),8,8,{},s,true);
    check(std::all_of(raster.begin(),raster.end(),[](auto p){return p==0x999999;}),"transparent renderer preserves parent pixels");
    data.assign(2048,0);
    s={};s.style=csp::Style::Vertical;s.all_channels=true;s.foreground=0x112233;s.hold=240;
    s.foreground_palette=true;s.background_palette=true;s.foreground_colors[0]=0x123456;s.active_mask=5;s.blur=25;
    std::vector<uint32_t> original;
    check(csp::encode_original_csp(s,original) && original.size()==75 && original[0]==0x07172737,"original file layout size and magic");
    check(original[1]==0x332211,"original COLORREF byte order");
    check(csp::decode_original_csp(original,decoded) && decoded==s,"original configuration field roundtrip");
    auto saved=decoded;original[0]=0;check(!csp::decode_original_csp(original,decoded) && decoded==saved,"bad original magic leaves settings unchanged");
    s={};s.style=csp::Style::GradientBars;check(!csp::encode_original_csp(s,original),"added styles cannot silently export to original format");
    s={};s.frequency_ticks=true;s.all_channels=true;s.active_mask=1;
    engine.update(data,1024,2,0,1,800,200,0,s,48000);
    check(engine.ticks().size()==12,"frequency ticks below Nyquist at 2kHz spacing");
    s.active_mask=0;engine.update(data,1024,2,0,1,800,200,14,s);
    check(engine.bars().empty() && engine.ticks().empty(),"no active channels clears panel");
    s={};s.style=csp::Style::SimpleAntialias;s.blur=50;s.show_peaks=false;s.foreground=0xffffff;s.background=0;
    std::vector<csp::Bar> aa={{1,2,0,0}};std::vector<uint32_t> aaPixels(8*8);
    csp::render(aaPixels.data(),8,8,aa,s);
    check(aaPixels[1]==0 && aaPixels[4*8+1]==0xffffff,"blur span uses height percentage");
    s={};s.style=csp::Style::Horizontal;s.all_channels=true;s.active_mask=0x3ffff;
    check(csp::encode_original_csp(s,original),"original preset export supported");
    check(!csp::decode_original_csp({},decoded),"empty original file rejected");
    for(size_t n=1;n<original.size();++n){decoded=s;check(csp::decode_original_csp(std::span(original.data(),n),decoded),"old DWORD-aligned preset prefix accepted");}
    decoded=s;decoded.hold=987;
    check(csp::decode_original_csp(std::span(original.data(),10),decoded) && decoded.hold==987,"old preset preserves absent tail fields");
    original[5]=99;check(!csp::decode_original_csp(original,decoded),"unknown original draw mode rejected");
    // Original x86 routine executed under Unicorn, with only containers/Win32 mocked.
    std::ifstream fixture("tests/fixtures/original-coordinates.txt");
    check(bool(fixture),"original coordinate fixture present");
    int fw,fh,fb,fc,fp,fs,fl,fd,fr,frc,fg,ff,fa,scale;
    unsigned differences=0,compared=0,cases=0;
    std::ofstream diff(argc>3?argv[3]:"build/oracle-differences.txt");
    while(fixture>>fw>>fh>>fb>>fc>>fp>>fs>>fl>>fd>>fr>>frc>>fg>>ff>>fa>>scale){
        const int extent=(fw-(fc-1)*fg)/fc/fp*fp,shift=(fw-(fc-1)*fg-fc*extent)/2;
        for(int c=0;c<fc;++c){
            const bool inc=fd?fd==1:((c+fr)&1)!=0;
            auto actual=csp::frequency_descriptors(extent,fb,fp,fs,inc,fl,fh,c>0);
            int start=shift+(frc?fc-1-c:c)*(extent+fg);
            for(int k=0;k<fb;++k){
                csp::Descriptor expected{};fixture>>expected.left>>expected.draw>>expected.right>>expected.baseline;
                auto got=actual[k];got.left+=start;got.right+=start;
#if defined(_M_ARM64EC) || defined(_M_ARM64) || defined(CSP_PORTABLE_MAPPING)
                if(k){
                    if(std::abs(got.left-expected.left)>fp || std::abs(got.right-expected.right)>fp || got.baseline!=expected.baseline)
                        std::cerr<<"ARM fixture mismatch: case "<<cases<<" channel "<<c<<" bin "<<k<<" pitch "<<fp<<" got "<<got.left<<","<<got.right<<","<<got.baseline<<" expected "<<expected.left<<","<<expected.right<<","<<expected.baseline<<"\n";
                    check(std::abs(got.left-expected.left)<=fp && std::abs(got.right-expected.right)<=fp && got.baseline==expected.baseline,"ARM descriptor deviation stays within one quantized bar pitch");
                }
#endif
                if(k){++compared;if(got!=expected){++differences;diff<<cases<<' '<<c<<' '<<k<<": "<<got.left<<' '<<got.draw<<' '<<got.right<<" != "<<expected.left<<' '<<expected.draw<<' '<<expected.right<<'\n';}}
            }
        }
        int rows;fixture>>rows;
        s={};s.new_fft=ff;s.log_amplitude=fa;s.range=scale;
        for(int row=0;row<rows;++row){int x1,y1,x2,y2;fixture>>x1>>y1>>x2>>y2;
            check(std::abs(int(fh-fh*csp::amplitude_tick(row/2,fb,s))-y1)<=1,"original amplitude tick location");}
        ++cases;
    }
    std::cout<<"Original descriptor comparison: "<<differences<<" differences / "<<compared<<" rows across "<<cases<<" cases.\n";
#if defined(_M_ARM64EC) || defined(_M_ARM64) || defined(CSP_PORTABLE_MAPPING)
    // ARM double arithmetic does not reproduce x87 extended precision pixel ties.
    // Keep the difference report for native validation, not a false exactness claim.
#else
    check(differences==0,"original descriptor fixture exact match");
#endif
    check(cases==13,"all oracle cases read");
    check(csp::speaker_input(9,0x603,4)==2 && csp::speaker_input(2,0x603,4)==-1,"sparse side speaker layout");
    s={};s.new_fft=true;s.channel_mode=2;s.selected=9;data.assign(1024*4,0);data[12*4+2]=1;
    engine.update(data,1024,4,0,1,120,80,0,s,48000,0x603);
    found=false;for(auto b:engine.bars())if(b.top==0)found=true;check(found,"speaker selection maps side-left to compact input");
    s.selected=2;engine.update(data,1024,4,0,1,120,80,14,s,48000,0x603);
    found=false;for(auto b:engine.bars())if(b.top<80)found=true;check(!found,"absent centre speaker remains silent");
    s={};s.amplitude_ticks=true;s.new_fft=true;s.long_amplitude=100;
    data.assign(2048,0);engine.update(data,1024,2,0,1,800,200,0,s);
    check(engine.ticks().size()==10,"seven visible new-mode tick levels; full lines once and short ticks on both sides");
    // End-to-end direction checks: caller preprocessing -> descriptors -> emitted bars.
    // Original caller VA 100035FC..1000361C swaps forced Left/Right on Reverse.
    // Rows: Mirror, Right, Left. Columns: reverse off/on; values are low-to-high direction.
    constexpr bool stereoDirections[3][2][2]={{{false,true},{true,false}},{{true,true},{false,false}},{{false,false},{true,true}}};
    constexpr bool monoDirections[3][2]={{true,false},{true,false},{false,true}};
    for(unsigned dir=0;dir<3;++dir)for(unsigned rev=0;rev<2;++rev)
    for(bool swap:{false,true})for(bool linear:{false,true})
    for(auto style:{csp::Style::Simple,csp::Style::SimpleLine})for(unsigned channels:{1u,2u}) {
        s={};s.new_fft=true;s.all_channels=true;s.direction=dir;s.reverse_frequency=rev!=0;
        s.reverse_channels=swap;s.linear_frequency=linear;s.style=style;
        int positions[2][2]={{-1,-1},{-1,-1}};
        for(unsigned tone=0;tone<2;++tone){
            std::vector<double> input(1024*channels,0);
            for(unsigned ch=0;ch<channels;++ch)input[(tone?900:8)*channels+ch]=1;
            engine.reset();engine.update(input,1024,channels,0,channels==1?0:1,800,200,0,s);
            for(auto bar:engine.bars())if(bar.top==0){
                check(bar.channel<channels,"direction probe channel identity");
                check(positions[tone][bar.channel]==-1,"single tone emits one active group per channel");
                positions[tone][bar.channel]=bar.left;
                unsigned partition=swap?channels-1-bar.channel:bar.channel;
                check(bar.left>=int(partition*800/channels) && bar.left<int((partition+1)*800/channels),"channel reversal changes partition, not frequency direction");
            }
        }
        for(unsigned ch=0;ch<channels;++ch){
            check(positions[0][ch]>=0 && positions[1][ch]>=0,"both frequency probes emitted");
            bool expected=channels==1?monoDirections[dir][rev]:stereoDirections[dir][rev][ch];
            check(expected?positions[0][ch]<positions[1][ch]:positions[0][ch]>positions[1][ch],"Engine frequency direction matches original caller and builder");
        }
    }
    // Native GDI gradient behavior and palette scope, including zero blur.
    s={};s.show_peaks=false;s.style=csp::Style::Simple;s.foreground=0xffffff;s.foreground2=0;s.background=0;
    std::vector<csp::Bar> probe{{0,3,0,0,0,1}};std::vector<uint32_t> gdiProbe(30);
    s.foreground_palette=true;s.foreground_colors.fill(0xff0000);
    csp::render(gdiProbe.data(),3,10,probe,s);
    check(gdiProbe.front()==0xffffff && gdiProbe.back()==0xffffff,"Simple ignores Vertical palette");
    s.style=csp::Style::SimpleAntialias;s.blur=0;csp::render(gdiProbe.data(),3,10,probe,s);
    check(gdiProbe.front()==0xffffff,"zero blur keeps bright bar top");
    s.style=csp::Style::Vertical;s.foreground_palette=false;csp::render(gdiProbe.data(),3,10,probe,s);
    check(gdiProbe.front()==0xffffff && gdiProbe.back()>0 && gdiProbe.back()<0xffffff,"GDI gradient excludes bottom endpoint");
    // Offline synthetic snapshot through the same Engine and renderer as the plugin.
    const int w=1780,h=320;engine.reset();s={};s.new_fft=true;
    if(argc>2)s.style=static_cast<csp::Style>(std::clamp(std::atoi(argv[2]),0,7));
    for(unsigned frame=0;frame<100;++frame) {
        for(unsigned bin=0;bin<1024;++bin)for(unsigned ch=0;ch<2;++ch) {
            const double envelope=std::exp(-double(bin)/170);
            const double ripple=.04+.65*std::pow(std::abs(std::sin(bin*.29+frame*.06+ch*.8)),12);
            data[bin*2+ch]=.16*envelope*ripple;
        }
        engine.update(data,1024,2,0,1,w,h,frame*14,s);
    }
    std::vector<uint32_t> pixels(size_t(w)*h);csp::render(pixels.data(),w,h,engine.bars(),s.style);
    if(argc>1) {
        BITMAPFILEHEADER file{};file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);
        file.bfSize=file.bfOffBits+DWORD(pixels.size()*4);
        BITMAPINFOHEADER info{};info.biSize=sizeof(info);info.biWidth=w;info.biHeight=-h;info.biPlanes=1;info.biBitCount=32;
        std::ofstream out(argv[1],std::ios::binary);out.write(reinterpret_cast<char*>(&file),sizeof(file));
        out.write(reinterpret_cast<char*>(&info),sizeof(info));out.write(reinterpret_cast<char*>(pixels.data()),pixels.size()*4);
        if(!out)throw std::runtime_error("preview write failed");
    }
    std::cout<<"PASS: "<<checks<<" checks. Synthetic renderer snapshot; not a foobar2000 integration test.\n";
    return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
