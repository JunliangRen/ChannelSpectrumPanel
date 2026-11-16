#pragma once
#include <stdexcept>
#include "spectrum.h"
namespace csp {
// Logging is best effort: an allocation or logger failure must never escape WndProc.
template<class Sink> void report_once(bool& reported,const char* message,Sink&& sink) noexcept {
    if(reported)return;
    reported=true;
    try {sink(message);}catch(...){}
}
template<class Start,class Log> void start_fallback_timer(Start&& start,Log&& log){
    if(!start())throw std::runtime_error("Could not start spectrum refresh timer.");
    try{log();}catch(...){} // Logging cannot prevent an already-established fallback.
}
// Monitor tracking is optional: its failure must not prevent manual-rate drawing.
template<class Start,class Log> bool start_monitor_tracking(bool listening,Start&& start,Log&& log) noexcept {
    if(listening)return true;
    try{if(start())return true;}catch(...){}
    try{log();}catch(...){}
    return false;
}
inline bool refresh_active(bool visible,bool playing,bool paused) noexcept {return visible && playing && !paused;}
// Style affects bin layout as well as painting. A paused panel has no timer
// to rebuild it, so its next paint must consume the retained spectrum.
inline void set_drawing_style(Settings& settings,Style style,bool& frame_dirty) noexcept {
    if(settings.style!=style){settings.style=style;frame_dirty=true;}
}
struct SnapshotState {
    bool valid=false,new_fft=false;
    void capture(bool mode) noexcept {new_fft=mode;valid=true;}
    void invalidate() noexcept {valid=false;}
    bool mode_for_redraw(bool requested) const noexcept {return valid?new_fft:requested;}
};
class StopRepaint {
public:
    bool update(bool playing) noexcept {
        if(playing){stopped_=false;return false;}
        if(stopped_)return false;
        stopped_=true;return true;
    }
private:bool stopped_=false;
};
}
