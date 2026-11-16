#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cfenv>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace csp {
inline unsigned parse_target_fps(std::wstring_view text) noexcept {
    while(!text.empty() && text.front()==L' ')text.remove_prefix(1);
    while(!text.empty() && text.back()==L' ')text.remove_suffix(1);
    if(text.empty() || text.size()>3)return 0;
    unsigned value=0;
    for(wchar_t ch:text){if(ch<L'0' || ch>L'9')return 0;value=value*10+unsigned(ch-L'0');}
    return value>=10 && value<=200?value:0;
}
enum class Style : unsigned { GradientBars, SimpleLine, SeparateLines, Simple, Horizontal, Vertical, SimpleAntialias, HorizontalAntialias };
struct Settings {
    unsigned fft = 2048, interval = 14, fall = 10, hold = 0, range = 100;
    unsigned width = 1, spacing = 0;
    bool new_fft = false, log_amplitude = false, linear_frequency = false;
    Style style = Style::GradientBars;
    unsigned direction=0, rotation=0, channel_mode=0, selected=0;
    unsigned padding_left=0,padding_right=0,channel_gap=0,blur=25,edge=0;
    bool reverse_frequency=false,reverse_channels=false,show_peaks=true,segmented=false;
    bool frequency_ticks=false,amplitude_ticks=false,all_channels=false;
    unsigned short_frequency=3,long_frequency=7,short_amplitude=3,long_amplitude=7;
    uint32_t foreground=0xe4e4e4,foreground2=0x202020,background=0,peak_color=0xf5f5f5,tick_color=0x808080;
    std::array<unsigned,18> order={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17};
    uint32_t active_mask=0x3ffff;
    bool transparent=false,fast_transparent=false,scanline_sync=false;
    bool foreground_palette=false,background_palette=false;
    std::array<uint32_t,8> foreground_colors={0x202020,0x3c3c3c,0x585858,0x747474,0x909090,0xacacac,0xc8c8c8,0xe4e4e4};
    std::array<uint32_t,8> background_colors={0,0,0,0,0,0,0,0};
    // Keep the format-4 storage unit for compatibility; effective FPS is integer-only.
    // Zero derives rounded FPS from the legacy animation interval.
    unsigned fps_milli=0;
    bool follow_monitor=false;
    bool direct2d=false;
    unsigned target_fps() const {return fps_milli?(std::clamp(fps_milli,10000u,200000u)+500)/1000:unsigned(std::lround(1000.0/std::clamp(interval,5u,100u)));}
    double refresh_ms() const {return 1000.0/target_fps();}
    bool operator==(const Settings&) const = default;
    void sanitize() {
        if (unsigned(style)>7) style=Style::GradientBars;
        if (fft < 512 || fft > 8192 || (fft & (fft - 1))) fft = 2048;
        direction=std::min(direction,2u);rotation%=4;channel_mode=std::min(channel_mode,2u);selected=std::min(selected,17u);
        padding_left=std::min(padding_left,4096u);padding_right=std::min(padding_right,4096u);channel_gap=std::min(channel_gap,4096u);
        blur=std::min(blur,100u);active_mask&=0x3ffff;edge=std::min(edge,2u);
        short_frequency=std::min(short_frequency,100u);long_frequency=std::min(long_frequency,100u);
        short_amplitude=std::min(short_amplitude,100u);long_amplitude=std::min(long_amplitude,100u);
        foreground&=0xffffff;foreground2&=0xffffff;background&=0xffffff;peak_color&=0xffffff;tick_color&=0xffffff;
        std::array<bool,18> seen{};bool valid=true;for(auto n:order){if(n>=18 || seen[n]){valid=false;break;}seen[n]=true;}
        if(!valid)for(unsigned i=0;i<18;++i)order[i]=i;
        interval = std::clamp(interval, 5u, 100u);
        if(fps_milli)fps_milli=target_fps()*1000;
        fall = std::clamp(fall, 1u, 99u);
        hold = std::min(hold, 10000u);
        range = std::clamp(range, 1u, 1000u);
        width = std::clamp(width, 1u, 8u);
        spacing = std::min(spacing, 8u);
    }
    unsigned effective_width() const { return style==Style::SimpleLine || style==Style::SeparateLines ? 1 : width; }
    unsigned pitch() const { return style==Style::SimpleLine ? 2 : effective_width()+ (style==Style::SeparateLines ? std::max(1u,spacing) : spacing)+(segmented && style!=Style::SimpleLine ? 1 : 0); }
};
// Caller VA 100035FC..1000361C swaps forced directions on Reverse.
// VA 1000384B flips Mirror for a single display; forced directions bypass that flip.
inline bool frequency_increasing(unsigned side,unsigned displays,const Settings& s) {
    return s.direction ? ((s.direction==1) != s.reverse_frequency)
                       : bool((side&1) ^ s.reverse_frequency ^ (displays==1));
}
// Static-analysis VA 100041DD..10004546. Host FFT flags must match this mode.
inline double amplitude(double a, unsigned bins, const Settings& s) {
    if (!std::isfinite(a) || a <= 0 || bins < 2) return 0;
    const double gain = 100.0 / s.range;
    if (s.new_fft)
        return gain * std::max(0.0, 1 + (s.log_amplitude ? .25 : 2.0/7) * std::log10(std::max(a, 1.0/131072)));
    const double k = .2309 * std::log2(bins + 1.0);
    return gain * (s.log_amplitude ? std::log1p(2*a)/std::log1p(2*k) : a/k);
}
// VA 1000C13C..1000C39F: eight 10 dB ticks, not uniformly spaced pixels.
inline double amplitude_tick(unsigned index,unsigned bins,const Settings& s) {
    if(s.new_fft) return (100.0/s.range)*(1.0-double(index)/(s.log_amplitude?8:7));
    const double a=.2309*std::log2(1+bins*std::pow(10.0,-.5*index));
    return amplitude(a,bins,s);
}
// VA 10001580: speaker bit number to compact interleaved sample index.
inline int speaker_input(unsigned speaker,uint32_t mask,unsigned channels) {
    if(speaker>=18 || !(mask&(1u<<speaker)))return -1;
    unsigned input=std::popcount(mask&((1u<<speaker)-1));
    return input<channels?int(input):-1;
}
struct Peak {
    int64_t position = 0, velocity = 0;
    uint32_t touched = 0;
    bool initialized = false;
    uint64_t applied_steps=0;
    void update(int y, uint32_t now, const Settings& s, int bottom) {
        const int64_t target = int64_t(y)*256;
        const int64_t acceleration = int64_t(128.0*s.fall*s.interval/(500-s.interval));
        if (!initialized || position >= target) {
            position = target; velocity = acceleration; touched = now; initialized = true; applied_steps=0;
        } else if (uint32_t(now-touched) >= s.hold) {
            const uint32_t age=now-touched;
            const uint64_t steps=s.hold?1+(age-s.hold)/s.interval:age/s.interval;
            const uint64_t count=steps>applied_steps?steps-applied_steps:0;
            // Closed-form legacy steps: independent of render frequency, no catch-up loop.
            const double next=double(position)+double(count)*double(velocity)+double(acceleration)*double(count)*double(count?count-1:0)/2;
            position=int64_t(std::min(next,double(bottom)*256));
            velocity+=int64_t(count)*acceleration;applied_steps=steps;
            // Off-screen saturation prevents unbounded state after prolonged silence.
            position = std::min(position, int64_t(bottom)*256);
        }
    }
};
struct Tick {int x1,y1,x2,y2;};
struct Bar { int left, right, top, peak; unsigned channel=0; double level=0; };
// Four-DWORD descriptors from VA 1000B370, consumed in bin order.
// A change of quantized boundary completes the PREVIOUS bin's accumulated group.
struct Descriptor { int left,draw,right,baseline; bool operator==(const Descriptor&) const=default; };
struct MappingInput {int bin,anchor,bins,extent,rounded_previous;};
struct MappingOutput {int previous,current,wide;};
// ARM has no x87 extended precision. Keep the same mapping formula in double.
inline void portable_mapping_coordinates(const MappingInput* input,MappingOutput* output){
    const int rounding=std::fegetround();std::fesetround(FE_TONEAREST);
    const double d=input->bins-1.;
    const auto q=[&](int k){return 1024.*std::log1p(1024.*k/d)/std::log(1025.);};
    const double anchor=q(input->anchor),t=std::max(double(input->anchor),input->extent*double(input->anchor)/d);
    const auto interpolate=[&](int k){return t+(q(k)-anchor)*(input->extent-t)/(1024.-anchor);};
    const double previous=interpolate(input->bin-1),current=interpolate(input->bin);
    *output={int(previous),int(current),std::abs(current-previous)>1.};
    if(rounding!=-1)std::fesetround(rounding);
}
#if defined(_M_ARM64EC) || defined(_M_ARM64) || defined(CSP_PORTABLE_MAPPING)
inline void csp_mapping_coordinates(const MappingInput* input,MappingOutput* output){portable_mapping_coordinates(input,output);}
#else
extern "C" void csp_mapping_coordinates(const MappingInput*,MappingOutput*);
#endif
inline std::vector<Descriptor> frequency_descriptors(int extent,unsigned bins,int pitch,int spacing,bool increasing,bool linear,int baseline,bool rounded_previous=false) {
    std::vector<Descriptor> out;out.reserve(bins);
    if(extent<=0 || bins<2 || pitch<1)return out;
    const double d=bins-1.;unsigned anchor=0;
    for(unsigned k=0;k<bins;++k){
        double previous=extent*double(k?k-1:0)/d,current=extent*double(k)/d;
        if(k && !linear){
            MappingInput input{int(k),int(anchor),int(bins),extent,int(rounded_previous)};
            MappingOutput output{};csp_mapping_coordinates(&input,&output);
            previous=output.previous;current=output.current;
            if(output.wide){
                anchor=k;previous=std::max(double(k-1),extent*double(k-1)/d);
                current=std::max(double(k),extent*double(k)/d);
            }
        }
        const int start=int(previous)/pitch*pitch,end=std::max(int(current),start+pitch);
        Descriptor item=increasing?Descriptor{start,0,end,baseline}:Descriptor{extent-end,0,extent-start,baseline};
        if(!out.empty()){
            auto& prev=out.back();
            if(increasing && prev.left!=item.left)prev={prev.left+spacing,1,item.left,baseline};
            if(!increasing && prev.right!=item.right)prev={item.right+spacing,1,prev.right,baseline};
        }
        if(k==bins-1){item.left+=spacing;item.draw=1;}
        out.push_back(item);
    }
    return out;
}
class Engine {
public:
    void reset() { width_ = height_ = -1; descriptors_.clear(); peaks_ = {}; bars_.clear(); ticks_.clear(); }
    const std::vector<Bar>& bars() const { return bars_; }
    const std::vector<Tick>& ticks() const {return ticks_;}
    // Input is bin-major interleaved spectrum, never PCM. -1 denotes a missing channel.
    void update(std::span<const double> spectrum, unsigned bins, unsigned channels,
                int leftChannel, int rightChannel, int width, int height,
                uint32_t now, const Settings& settings, unsigned sample_rate=48000,uint32_t channel_mask=0,bool advance_peaks=true) {
        Settings s = settings; s.sanitize();
        if (width < 2 || height < 1 || bins < 2 || channels < 1 ||
            bins > spectrum.size()/channels) { reset(); return; }
        // Absent layout: retain positional behavior. A supplied layout uses speaker identities.
        if(!channel_mask)channel_mask=(1u<<std::min(channels,18u))-1;
        ordered_.clear();auto& ordered=ordered_;
        for(unsigned n:s.order)if(s.active_mask&(1u<<n)){
            int input=speaker_input(n,channel_mask,channels);
            if(input>=0)ordered.push_back(unsigned(input));
        }
        const unsigned displays=s.channel_mode==0 ? (s.all_channels ? unsigned(ordered.size()) : 2u) : 1u;
        if(!displays){reset();return;}
        if (channels != channels_ || leftChannel != left_ || rightChannel != right_ || displays!=displays_ || channel_mask!=channel_mask_) reset();
        channel_mask_=channel_mask;channels_=channels;left_=leftChannel;right_=rightChannel;displays_=displays;
        if(width!=width_ || height!=height_ || bins!=bins_ || !(s==settings_)){
            // A failed rebuild must not leave cached dimensions pointing at partial vectors.
            try {build(width,height,bins,s);} catch(...) {reset();throw;}
        }
        bars_.clear();ticks_.clear();if(extent_<=0)return;
        for(unsigned side=0;side<displays;++side) {
            values_.resize(peaks_[side].size());std::fill(values_.begin(),values_.end(),0.0);auto& values=values_;
            auto accumulate=[&](int channel){
                if(channel<0 || unsigned(channel)>=channels)return;
                for(unsigned k=1;k<bins;++k){
                    double a=spectrum[size_t(k)*channels+channel];
                    if(std::isfinite(a) && a>0)values[k]=std::max(values[k],a);
                }
            };
            if(s.channel_mode==1)for(unsigned c:ordered)accumulate(int(c));
            else if(s.channel_mode==2)accumulate(speaker_input(s.selected,channel_mask,channels));
            else if(s.all_channels)accumulate(int(ordered[side]));
            else accumulate(side?rightChannel:leftChannel);
            const unsigned physical=s.reverse_channels ? displays-1-side : side;
            const int start=int(s.padding_left)+shift_+int(physical)*(extent_+int(s.channel_gap));
            const bool increasing=frequency_increasing(side,displays,s);
            if(s.frequency_ticks && sample_rate){
                // 0x100211A0: thresholds max(0,trunc(2000*n*M/(Fs/2))-1), long every fifth tick.
                for(unsigned n=0;n*2000.0<sample_rate*.5;++n){
                    unsigned bin=unsigned(std::clamp(int(n*2000.0*bins/(sample_rate*.5))-1,0,int(bins)-1));
                    const auto& desc=descriptors_[side][bin];
                    int x=start+(desc.left+desc.right-1)/2;
                    int length=int(n%5?s.short_frequency:s.long_frequency);if(!length)continue;
                    if(length==100)length=height;
                    ticks_.push_back({x,height-1,x,std::max(0,height-length)});
                }
            }
            double pending=0;
            for(size_t j=1;j<values.size();++j){
                pending=std::max(pending,values[j]);
                const auto& desc=descriptors_[side][j];
                if(!desc.draw)continue;
                double q=amplitude(pending,bins,s);pending=0;
                int y=height-int(std::min(double(height),height*q));
                if(s.segmented && s.style!=Style::SimpleLine)y&=~1;
                auto& peak=peaks_[side][j];
                if(advance_peaks || !peak.initialized)peak.update(y,now,s,height);
                int a=start+desc.left,b=start+desc.right;
                if(s.style==Style::SimpleLine){if(!increasing)a=b;b=a+1;}
                else if(s.style==Style::SeparateLines){if(!increasing)a=b-1;b=a+1;}
                int py=int(peak.position/256);if(s.segmented && s.style!=Style::SimpleLine)py&=~1;
                bars_.push_back({std::clamp(a,0,width),std::clamp(b,0,width),y,py,side,q});
            }
        }
        if(s.amplitude_ticks)for(unsigned n=0;n<8;++n){
            const int y=int(height-height*amplitude_tick(n,bins,s));
            const int left=int(s.padding_left),right=width-int(s.padding_right);
            const unsigned length=(n&1)?s.short_amplitude:s.long_amplitude;
            if(y<0 || y>=height || right<=left || !length)continue;
            if(length==100)ticks_.push_back({left,y,right-1,y});
            else {
                ticks_.push_back({left,y,std::min(right-1,left+int(length)-1),y});
                ticks_.push_back({std::max(left,right-int(length)),y,right-1,y});
            }
        }
    }

private:
    void build(int width,int height,unsigned bins,const Settings& s) {
        width_=width; height_=height; bins_=bins; settings_=s;
        const int pitch=int(s.pitch());
        const int usable=width-int(s.padding_left)-int(s.padding_right)-int(displays_-1)*int(s.channel_gap);
        const int extent=std::max(0,usable/int(displays_)/pitch*pitch);
        extent_=extent;shift_=std::max(0,(usable-int(displays_)*extent)/2);
        peaks_.resize(displays_);descriptors_.resize(displays_);
        for(unsigned side=0;side<displays_;++side){
            peaks_[side].assign(bins,{});
            const bool increasing=frequency_increasing(side,displays_,s);
            const int spacing=s.style==Style::SimpleLine?0:int(s.pitch()-s.effective_width());
            descriptors_[side]=frequency_descriptors(extent,bins,pitch,spacing,increasing,s.linear_frequency,height,side>0);
        }
    }
    int width_=-1,height_=-1;
    unsigned bins_=0,channels_=0;
    uint32_t channel_mask_=0;
    int left_=-1,right_=-1;
    Settings settings_;
    std::vector<std::vector<Descriptor>> descriptors_;
    std::vector<std::vector<Peak>> peaks_;
    unsigned displays_=2;
    int extent_=0,shift_=0;
    std::vector<unsigned> ordered_;
    std::vector<double> values_;
    std::vector<Bar> bars_;
    std::vector<Tick> ticks_;
};
}
