#pragma once
#include <cstdint>
#include <cwchar>
#include <iterator>

namespace csp {
inline constexpr int fps_font_pixels=16, fps_box_width=96, fps_box_height=25;
inline constexpr bool fps_fits(int width,int height) noexcept {
    return width>=fps_box_width && height>=2+fps_box_height;
}
class RenderedFps {
public:
    void reset() noexcept {
        frames_=sample_frames_=start_ms_=0;
        wcscpy_s(label_,L"\u2014 FPS");
    }
    void presented(uint64_t now_ms) noexcept {
        if(++frames_==1){start_ms_=now_ms;sample_frames_=frames_;return;}
        const auto elapsed=now_ms-start_ms_;
        if(elapsed<500)return;
        std::swprintf(label_,std::size(label_),L"%.1f FPS",1000.0*(frames_-sample_frames_)/elapsed);
        start_ms_=now_ms;sample_frames_=frames_;
    }
    const wchar_t* label() const noexcept {return label_;}
    uint64_t frames() const noexcept {return frames_;}
private:
    uint64_t frames_=0,sample_frames_=0,start_ms_=0;
    wchar_t label_[32]=L"\u2014 FPS";
};
}
