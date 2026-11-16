#pragma once
#include "spectrum.h"
namespace csp {
inline std::vector<uint32_t> encode(const Settings& s){
    std::vector<uint32_t> v={6,s.fft,s.interval,s.fall,s.hold,s.range,s.width,s.spacing,
        s.new_fft,s.log_amplitude,s.linear_frequency,unsigned(s.style),
        s.direction,s.rotation,s.channel_mode,s.selected,s.padding_left,s.padding_right,s.channel_gap,
        s.blur,s.edge,s.reverse_frequency,s.reverse_channels,s.show_peaks,s.segmented,
        s.frequency_ticks,s.amplitude_ticks,s.all_channels,s.short_frequency,s.long_frequency,
        s.short_amplitude,s.long_amplitude,s.foreground,s.foreground2,s.background,s.peak_color,s.tick_color};
    v.insert(v.end(),s.order.begin(),s.order.end());
    v.insert(v.end(),{s.transparent,s.fast_transparent,s.scanline_sync,s.foreground_palette,s.background_palette});
    v.insert(v.end(),s.foreground_colors.begin(),s.foreground_colors.end());
    v.insert(v.end(),s.background_colors.begin(),s.background_colors.end());v.push_back(s.active_mask);v.push_back(s.fps_milli?s.target_fps()*1000:0);v.push_back(s.follow_monitor);v.push_back(s.direct2d);return v;
}
inline bool decode(std::span<const uint32_t> v,Settings& result){
    if(v.empty()){result={};return true;}
    if(v[0]<1 || v[0]>6 || v.size()<(v[0]==1?11u:v[0]==2?12u:v[0]==3?77u:v[0]==4?78u:v[0]==5?79u:80u))return false;
    Settings s={v[1],v[2],v[3],v[4],v[5],v[6],v[7],v[8]!=0,v[9]!=0,v[10]!=0};
    if(v[0]>=2)s.style=static_cast<Style>(v[11]);
    if(v[0]>=3){
        s.direction=v[12];s.rotation=v[13];s.channel_mode=v[14];s.selected=v[15];
        s.padding_left=v[16];s.padding_right=v[17];s.channel_gap=v[18];s.blur=v[19];s.edge=v[20];
        s.reverse_frequency=v[21]!=0;s.reverse_channels=v[22]!=0;s.show_peaks=v[23]!=0;s.segmented=v[24]!=0;
        s.frequency_ticks=v[25]!=0;s.amplitude_ticks=v[26]!=0;s.all_channels=v[27]!=0;
        s.short_frequency=v[28];s.long_frequency=v[29];s.short_amplitude=v[30];s.long_amplitude=v[31];
        s.foreground=v[32];s.foreground2=v[33];s.background=v[34];s.peak_color=v[35];s.tick_color=v[36];
        std::copy_n(v.begin()+37,18,s.order.begin());
        s.transparent=v[55]!=0;s.fast_transparent=v[56]!=0;s.scanline_sync=v[57]!=0;
        s.foreground_palette=v[58]!=0;s.background_palette=v[59]!=0;
        std::copy_n(v.begin()+60,8,s.foreground_colors.begin());std::copy_n(v.begin()+68,8,s.background_colors.begin());s.active_mask=v[76];
    }
    if(v[0]>=4)s.fps_milli=v[77];
    if(v[0]>=5)s.follow_monitor=v[78]!=0;
    if(v[0]>=6)s.direct2d=v[79]!=0;
    s.sanitize();result=s;return true;
}
}
