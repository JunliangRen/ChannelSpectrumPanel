#pragma once
#include "config.h"
namespace csp {
inline constexpr unsigned offsets[]={0x8,0x28,0x2c,0x34,0x40,0x30,0x38,0x50,0x44,0xc,0x3c,0x48,0x4c,0x58,0x5c,0x64,0x60,0x68,0x6c,0x70,0x74,0x78,0x7c,0x10,0x14,0x18,0x1c,0x20,0x24,0x80,0x84,0x88,0x8c,0x90,0xb8,0x98,0x9c,0xa0,0xa4,0xa8,0xac,0xb0,0xb4,0x94,0xbc,0xc0,0xc8,0xcc,0xd0,0xd4,0xd8,0xdc,0xe0,0xe4,0xe8,0xec,0xf0,0xf4,0xf8,0xfc,0x100,0x104,0x108,0x10c,0x114,0x110,0x118,0x11c,0x120,0x124,0x128,0x12c,0x130,0xc4};
inline bool encode_original_csp(const Settings& s,std::vector<uint32_t>& words);
inline bool decode_original_csp(std::span<const uint32_t> words,Settings& result){

    if(words.empty() || words.size()>std::size(offsets)+1 || words[0]!=0x07172737)return false;
    if(words.size()==1)return true;
    // VA 1001D7B0: EOF at a DWORD boundary leaves later fields at their current values.
    Settings base=result;std::vector<uint32_t> prior;
    if(!encode_original_csp(base,prior)){base.style=Style::Simple;encode_original_csp(base,prior);}
    std::array<uint32_t,77> fields{};
    for(size_t i=0;i<std::size(offsets);++i)fields[offsets[i]/4]=prior[i+1];
    for(size_t i=0;i+1<words.size();++i)fields[offsets[i]/4]=words[i+1];
    auto get=[&](unsigned offset){return fields[offset/4];};
    auto rgb=[](uint32_t color){return ((color&255)<<16)|(color&0xff00)|((color>>16)&255);};
    if(get(0x40)>5 || get(0x68)>3 || get(0x114)>2 || get(0x5c)>2)return false;
    Settings s;
    constexpr Style styles[]={Style::Simple,Style::Horizontal,Style::Vertical,Style::SimpleAntialias,Style::HorizontalAntialias,Style::SimpleLine};
    s.style=styles[get(0x40)];s.foreground=rgb(get(8));s.foreground2=rgb(get(0xc));s.background=rgb(get(0x28));
    s.peak_color=rgb(get(0x2c));s.tick_color=rgb(get(0xb8));s.edge=get(0x30);s.width=get(0x38);
    s.show_peaks=get(0x34)!=0;s.interval=get(0x3c);s.log_amplitude=get(0x44)!=0;s.fft=get(0x48);s.fall=get(0x4c);
    s.transparent=(get(0x50)&1)!=0;s.fast_transparent=(get(0x50)&2)!=0;s.segmented=get(0x58)!=0;
    s.channel_mode=get(0x5c);s.selected=get(0x64);s.reverse_frequency=get(0x60)!=0;s.rotation=get(0x68);
    s.reverse_channels=get(0x6c)!=0;s.range=get(0x70);s.spacing=get(0x74);s.frequency_ticks=get(0x7c)!=0;
    s.linear_frequency=get(0x8c)!=0;s.amplitude_ticks=get(0x90)!=0;s.hold=get(0xbc);s.scanline_sync=get(0xc0)!=0;
    s.direction=get(0x114);s.padding_left=get(0x118);s.padding_right=get(0x11c);s.channel_gap=get(0x120);
    s.short_frequency=get(0x124);s.long_frequency=get(0x128);s.short_amplitude=get(0x12c);s.long_amplitude=get(0x130);
    s.new_fft=get(0xc4)!=0;s.all_channels=true;
    for(unsigned i=0;i<18;++i)s.order[i]=get(0xc8+4*i);
    s.active_mask=get(0x110);s.blur=get(0x78);s.foreground_palette=get(0x80)!=0;s.background_palette=get(0x94)!=0;
    constexpr unsigned fgOffsets[]={0x84,0x88,0x10,0x14,0x18,0x1c,0x20,0x24};
    for(unsigned i=0;i<8;++i){s.foreground_colors[7-i]=rgb(get(fgOffsets[i]));s.background_colors[7-i]=rgb(get(0x98+4*i));}
    s.sanitize();result=s;return true;
}
inline bool encode_original_csp(const Settings& s,std::vector<uint32_t>& words){
    unsigned mode;
    switch(s.style){case Style::Simple:mode=0;break;case Style::Horizontal:mode=1;break;case Style::Vertical:mode=2;break;
    case Style::SimpleAntialias:mode=3;break;case Style::HorizontalAntialias:mode=4;break;case Style::SimpleLine:mode=5;break;
    default:return false;}
    std::array<uint32_t,77> fields{};auto put=[&](unsigned offset,uint32_t value){fields[offset/4]=value;};
    auto color=[](uint32_t rgb){return ((rgb&255)<<16)|(rgb&0xff00)|((rgb>>16)&255);};
    put(8,color(s.foreground));put(0xc,color(s.foreground2));put(0x28,color(s.background));put(0x2c,color(s.peak_color));
    put(0x30,s.edge);put(0x34,s.show_peaks);put(0x38,s.width);put(0x3c,s.interval);put(0x40,mode);put(0x44,s.log_amplitude);
    put(0x48,s.fft);put(0x4c,s.fall);put(0x50,unsigned(s.transparent)|(unsigned(s.fast_transparent)<<1));put(0x58,s.segmented);
    put(0x5c,s.channel_mode);put(0x60,s.reverse_frequency);put(0x64,s.selected);put(0x68,s.rotation);put(0x6c,s.reverse_channels);
    put(0x70,s.range);put(0x74,s.spacing);put(0x78,s.blur);put(0x7c,s.frequency_ticks);put(0x80,s.foreground_palette);
    put(0x8c,s.linear_frequency);put(0x90,s.amplitude_ticks);put(0x94,s.background_palette);put(0xb8,color(s.tick_color));
    put(0xbc,s.hold);put(0xc0,s.scanline_sync);put(0xc4,s.new_fft);put(0x110,s.active_mask);put(0x114,s.direction);
    put(0x118,s.padding_left);put(0x11c,s.padding_right);put(0x120,s.channel_gap);
    put(0x124,s.short_frequency);put(0x128,s.long_frequency);put(0x12c,s.short_amplitude);put(0x130,s.long_amplitude);
    constexpr unsigned fgOffsets[]={0x84,0x88,0x10,0x14,0x18,0x1c,0x20,0x24};
    for(unsigned i=0;i<8;++i){put(fgOffsets[i],color(s.foreground_colors[7-i]));put(0x98+4*i,color(s.background_colors[7-i]));}
    for(unsigned i=0;i<18;++i)put(0xc8+4*i,s.order[i]);
    words={0x07172737};for(auto offset:offsets)words.push_back(fields[offset/4]);return true;
}

}
