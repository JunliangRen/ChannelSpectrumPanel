#pragma once
#include <stdexcept>
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
