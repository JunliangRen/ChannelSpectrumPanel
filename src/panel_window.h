#pragma once
#include <foobar2000/SDK/foobar2000.h>
#include <windowsx.h>
#include <bit>
#include "spectrum.h"
#include "render.h"
#include "d2d_render.h"
#include "settings_thread.h"
#include "scanline.h"
#include "frame_timer.h"
#include "runtime_state.h"
#include "panel_config.h"
#include <uxtheme.h>


namespace {
cfg_uint interface_language({0xf1469e93,0x8e1d,0x482c,{0xa5,0xb6,0x93,0x58,0xab,0xa0,0x23,0xa1}},0);
constexpr wchar_t window_class[]=L"ChannelSpectrumMirror.1";

class SpectrumWindow : private play_callback_impl_base {
public:
    SpectrumWindow():play_callback_impl_base(flag_on_playback_starting|flag_on_playback_new_track|flag_on_playback_stop|flag_on_playback_seek|flag_on_playback_pause){}
    ~SpectrumWindow(){destroy_window();}
    SpectrumWindow(const SpectrumWindow&)=delete;
    SpectrumWindow& operator=(const SpectrumWindow&)=delete;
    HWND get_wnd() const noexcept {return window_;}
    std::function<bool()> editing;
    std::function<service_ptr_t<service_base>()> retain;
    const csp::Settings& settings() const noexcept {return settings_;}
    void apply_settings(const csp::Settings& next){
        stop_refresh();
        if(!next.scanline_sync)sync_.stop();
        d2d_.reset();settings_=next;engine_.reset();frame_dirty_=true;stream_.release();last_time_=-1;reported_=false;
        if(window_){
            LONG_PTR ex=GetWindowLongPtr(window_,GWL_EXSTYLE)&~(WS_EX_CLIENTEDGE|WS_EX_STATICEDGE);
            if(next.edge==1)ex|=WS_EX_CLIENTEDGE;else if(next.edge==2)ex|=WS_EX_STATICEDGE;
            SetWindowLongPtr(window_,GWL_EXSTYLE,ex);SetWindowPos(window_,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
            background_cache_.clear();start_timer();InvalidateRect(window_,nullptr,FALSE);
        }
    }
    void host_visibility(bool visible){
        host_visible_=visible;
        if(!window_)return;
        if(!visible){stop_refresh();animation_clock_valid_=false;stream_.release();last_time_=-1;}
        else {frame_dirty_=true;start_timer();InvalidateRect(window_,nullptr,FALSE);}
    }
    void theme_changed(){background_cache_.clear();if(window_)InvalidateRect(window_,nullptr,FALSE);}
    bool show_config_popup(HWND parent) {
        if(settings_open_)return false;
        settings_open_=true;
        struct ConfigGuard{bool& open;~ConfigGuard(){open=false;}} configGuard{settings_open_};

        DialogState dialog{settings_,[this](const csp::Settings& next){apply_settings(next);}};
        dialog.language=unsigned(interface_language.get_value())<3?unsigned(interface_language.get_value()):0;
        dialog.save_language=[](unsigned language){interface_language=language;};
        SettingsThread::run(core_api::get_my_instance(),parent,dialog,{},window_?window_:parent);
        return dialog.changed;
    }
    void get_config(stream_writer* w,abort_callback& abort) const {
        const auto bytes=csp::encode_panel_config(settings_);w->write(bytes.data(),bytes.size(),abort);
    }
    void set_config(stream_reader* r,t_size size,abort_callback& abort) {
        if(size>4096 || size%4)throw exception_io_data();
        std::vector<uint8_t> bytes(size);if(size)r->read_object(bytes.data(),size,abort);
        csp::Settings next;if(!csp::decode_panel_config(bytes.data(),size,next))throw exception_io_data();apply_settings(next);
    }
    HWND create_window(HWND parent,int x,int y,int width,int height){
        WNDCLASSEX wc{sizeof(wc)};wc.lpfnWndProc=window_proc;wc.hInstance=core_api::get_my_instance();
        wc.lpszClassName=window_class;wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
        if(!RegisterClassEx(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return nullptr;
        return CreateWindowEx(settings_.edge==1?WS_EX_CLIENTEDGE:settings_.edge==2?WS_EX_STATICEDGE:0,window_class,L"Channel Spectrum Panel",WS_CHILD|WS_CLIPSIBLINGS|WS_CLIPCHILDREN,
            x,y,width,height,parent,nullptr,core_api::get_my_instance(),this);
    }
    void transfer_window(HWND parent){
        ShowWindow(window_,SW_HIDE);SetParent(window_,parent);monitor_events_->location_changed();
    }
    void destroy_window() noexcept {if(window_)DestroyWindow(window_);}
    void build_menu(HMENU menu,unsigned base){
        AppendMenu(menu,MF_STRING,base+1,L"Configure...");
        if(HMENU colors=CreatePopupMenu()){
            for(unsigned i=0;i<std::size(modes);++i)
                AppendMenu(colors,MF_STRING|(settings_.style==modes[i].style?MF_CHECKED:0),base+10+i,modes[i].label);
            if(!AppendMenu(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(colors),L"Color Mode"))DestroyMenu(colors);
        }
    }
    void menu_command(unsigned command){
        if(!window_)return;
        if(command==1)show_config_popup(window_);
        else if(command>=10 && command<10+std::size(modes)){
            csp::set_drawing_style(settings_,modes[command-10].style,frame_dirty_);
            if(window_)InvalidateRect(window_,nullptr,FALSE);
        }
    }
private:
    struct Mode {const wchar_t* label;csp::Style style;};
    static constexpr Mode modes[]={
        {L"Simple",csp::Style::Simple},{L"Horizontal",csp::Style::Horizontal},
        {L"Vertical",csp::Style::Vertical},{L"Simple-antialiasing",csp::Style::SimpleAntialias},
        {L"Horizontal-antialiasing",csp::Style::HorizontalAntialias},{L"Simple-line",csp::Style::SimpleLine}
    };
    bool ensure_stream() noexcept {
        if(!window_ || destroying_ || !host_visible_ || !IsWindowVisible(window_))return false;
        if(stream_.is_valid())return true;
        const HWND expected=window_;
        try {
            visualisation_stream::ptr candidate;
            visualisation_manager::get()->create_stream(candidate,settings_.new_fft?1:0);
            if(window_!=expected || destroying_ || !host_visible_ || !IsWindowVisible(window_) || !candidate.is_valid())return false;
            stream_=candidate;
            return true;
        }catch(...){return false;}
    }
    void invalidate_snapshot() noexcept {
        snapshot_.invalidate();frame_dirty_=false;last_time_=-1;engine_.reset();
        if(window_)InvalidateRect(window_,nullptr,FALSE);
    }
    void on_playback_starting(play_control::t_track_command,bool) noexcept override {invalidate_snapshot();queue_playback_refresh();}
    void on_playback_new_track(metadb_handle_ptr) noexcept override {invalidate_snapshot();queue_playback_refresh();}
    void on_playback_stop(play_control::t_stop_reason) noexcept override {stop_refresh();animation_clock_valid_=false;invalidate_snapshot();}
    void on_playback_seek(double) noexcept override {invalidate_snapshot();}
    static constexpr UINT playback_refresh_message=WM_APP+103;
    void queue_playback_refresh() noexcept {if(window_)PostMessage(window_,playback_refresh_message,0,0);}
    void stop_refresh() noexcept {refresh_running_=false;if(window_)KillTimer(window_,1);sync_.pause();frame_timer_.stop();}
    void on_playback_pause(bool paused) noexcept override {
        if(paused){stop_refresh();animation_clock_valid_=false;}
        queue_playback_refresh();
    }
    void start_timer(){
        if(!window_ || destroying_)return;
        KillTimer(window_,3);
        const bool listening=monitor_events_->bind(window_);
        monitor_events_->refresh(false);
        const bool tracked=csp::start_monitor_tracking(listening,
            [&]{return SetTimer(window_,3,5000,nullptr)!=0;},
            [&]{console::print("Channel Spectrum Panel: display monitoring unavailable; using manual FPS.");});
        monitor_events_->set_polling_active(!listening && tracked);
        const double next_fps=csp::effective_refresh(settings_.follow_monitor && tracked,settings_.target_fps(),monitor_events_->current());
        auto playback=playback_control::get();
        const bool visible=host_visible_ && IsWindowVisible(window_)!=FALSE;
        if(visible)ensure_stream(); // Register before playback/first timer tick, without reading audio.
        if(!csp::refresh_active(visible,playback->is_playing(),playback->is_paused())){
            stop_refresh();animation_clock_valid_=false;
            // Ancestor visibility events can stop the timer before tick() gets
            // a chance to release the stream. Do not retain a hidden client.
            if(!visible){stream_.release();last_time_=-1;}
            return;
        }
        if(refresh_running_ && !csp::refresh_changed(active_fps_,next_fps)){
            return;
        }
        stop_refresh();active_fps_=next_fps;
        if(settings_.scanline_sync && sync_.start(window_)){refresh_running_=true;return;}
        if(frame_timer_.start(window_,active_fps_)){refresh_running_=true;return;}
        csp::start_fallback_timer(
            [&]{return SetTimer(window_,1,UINT(std::ceil(1000.0/active_fps_)),nullptr)!=0;},
            [&]{if(settings_.scanline_sync)console::print("Channel Spectrum Panel: scanline sync unavailable; using regular timer.");});
        refresh_running_=true;
    }
    void clear_bitmap() {
        if(dc_) {SelectObject(dc_,old_bitmap_);DeleteObject(bitmap_);DeleteDC(dc_);}
        background_cache_.clear();
        dc_=nullptr;bitmap_=nullptr;old_bitmap_=nullptr;pixels_=nullptr;bitmap_w_=bitmap_h_=0;
    }
    void paint() {

        csp::PaintSession paintSession(window_);const HDC target=paintSession.dc;
        if(snapshot_.valid && frame_dirty_)update_cached_frame(false);
        RECT rc;GetClientRect(window_,&rc);
        const int w=rc.right,h=rc.bottom;
        if(w>0 && h>0) {
            d2d_.draw_or_fallback(window_,w,h,engine_.bars(),engine_.ticks(),settings_,[&]{
            if(w!=bitmap_w_ || h!=bitmap_h_) {
                clear_bitmap();
                BITMAPINFO bi{};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=w;
                bi.bmiHeader.biHeight=-h;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;
                HDC next=CreateCompatibleDC(target);
                auto bitmap=CreateDIBSection(target,&bi,DIB_RGB_COLORS,reinterpret_cast<void**>(&pixels_),nullptr,0);
                if(next && bitmap) {
                    dc_=next;bitmap_=bitmap;old_bitmap_=SelectObject(dc_,bitmap_);
                    if(!old_bitmap_ || old_bitmap_==HGDI_ERROR){old_bitmap_=nullptr;clear_bitmap();}
                    else {bitmap_w_=w;bitmap_h_=h;}
                } else {if(next)DeleteDC(next);if(bitmap)DeleteObject(bitmap);pixels_=nullptr;}
            }
            if(pixels_) {
                const int lw=(settings_.rotation&1)?h:w,lh=(settings_.rotation&1)?w:h;
                auto* drawing=csp::render_target(pixels_,w,h,settings_.rotation,rotated_);
                if(settings_.transparent){
                    RECT location;GetWindowRect(window_,&location);
                    if(!settings_.fast_transparent || background_cache_.size()!=size_t(w)*h || !EqualRect(&location,&background_location_)){
                        std::fill_n(pixels_,size_t(w)*h,settings_.background);
                        DrawThemeParentBackground(window_,dc_,nullptr);GdiFlush();
                        background_cache_.assign(pixels_,pixels_+size_t(w)*h);background_location_=location;
                    }
                    csp::rotate_pixels(background_cache_.data(),w,h,drawing,(4-settings_.rotation)%4);
                }
                csp::render(drawing,lw,lh,engine_.bars(),settings_,settings_.transparent,engine_.ticks(),&render_cache_);
                if(settings_.rotation)csp::rotate_pixels(drawing,lw,lh,pixels_,settings_.rotation);
                BitBlt(target,0,0,w,h,dc_,0,0,SRCCOPY);
            } else FillRect(target,&rc,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            });
        }

    }
    void tick() {

        if(!host_visible_ || !IsWindowVisible(window_)) {stop_refresh();animation_clock_valid_=false;stream_.release();last_time_=-1;return;}
        auto playback=playback_control::get();
        const bool playing=playback->is_playing();
        const bool clearStopped=stop_repaint_.update(playing);
        if(!playing) {stop_refresh();animation_clock_valid_=false;
            if(clearStopped){engine_.reset();snapshot_.invalidate();frame_dirty_=false;last_time_=-1;InvalidateRect(window_,nullptr,FALSE);}
            return;
        }
        if(playback->is_paused()){
            stop_refresh();animation_clock_valid_=false;
            if(snapshot_.valid && frame_dirty_)update_cached_frame(false);
            return;
        }
        if(!ensure_stream())return;
        double time=0;
        if(!stream_->get_absolute_time(time) || !stream_->get_spectrum_absolute(chunk_,time,settings_.fft))return;

        if(last_time_>=0 && (time<last_time_ || time-last_time_>1))engine_.reset();
        last_time_=time;
        const unsigned channels=chunk_.get_channels();const auto bins=chunk_.get_sample_count();
        if(!channels || bins<2 || bins>8192 || bins>chunk_.get_data_size()/channels){
            engine_.reset();snapshot_.invalidate();frame_dirty_=false;last_time_=-1;InvalidateRect(window_,nullptr,FALSE);return;
        }
        // Match float32 consumption in the original while respecting the x64 SDK's audio_sample ABI.
        samples_.resize(bins*channels);
        for(size_t i=0;i<samples_.size();++i)samples_[i]=static_cast<float>(chunk_.get_data()[i]);
        frame_bins_=unsigned(bins);frame_channels_=channels;frame_mask_=chunk_.get_channel_config();frame_rate_=chunk_.get_sample_rate();
        snapshot_.capture(settings_.new_fft);const uint32_t wall=csp::frame_clock_ms();
        if(animation_clock_valid_)frame_time_+=wall-animation_wall_;
        animation_wall_=wall;animation_clock_valid_=true;
        update_cached_frame(true);
    }
    void update_cached_frame(bool animate){
        const auto channels=frame_channels_;const auto bins=frame_bins_;
        const auto mask=frame_mask_;
        auto channel=[&](unsigned bit)->int {
            if(!(mask&bit))return -1;
            return int(std::popcount(mask&(bit-1)));
        };
        int left=channel(audio_chunk::channel_front_left),right=channel(audio_chunk::channel_front_right);
        if(channels==1)left=right=0;
        else if(!mask) {left=0;right=1;}
        RECT rc;GetClientRect(window_,&rc);
        auto displaySettings=settings_;displaySettings.new_fft=snapshot_.mode_for_redraw(settings_.new_fft);
        engine_.update(samples_,unsigned(bins),channels,left,right,(settings_.rotation&1)?rc.bottom:rc.right,(settings_.rotation&1)?rc.right:rc.bottom,frame_time_,displaySettings,frame_rate_,mask,animate);
        frame_dirty_=false;
        InvalidateRect(window_,nullptr,FALSE);
    }
    static LRESULT CALLBACK window_proc(HWND w,UINT msg,WPARAM wp,LPARAM lp) noexcept {
        auto* self=reinterpret_cast<SpectrumWindow*>(GetWindowLongPtr(w,GWLP_USERDATA));
        if(msg==WM_NCCREATE) {
            self=static_cast<SpectrumWindow*>(reinterpret_cast<CREATESTRUCT*>(lp)->lpCreateParams);
            self->window_=w;self->destroying_=false;SetWindowLongPtr(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
        }
        if(!self)return DefWindowProc(w,msg,wp,lp);
        // Keep the service alive through exception reporting after nested modal UI.
        service_ptr_t<service_base> keepAlive;
        try {
            switch(msg) {
            case WM_CREATE:
                self->start_timer();return 0;
            case csp::MonitorEvents::visibility_message:
            case playback_refresh_message:self->start_timer();return 0;
            case WM_THEMECHANGED:case WM_SYSCOLORCHANGE:self->background_cache_.clear();InvalidateRect(w,nullptr,FALSE);break;
            case WM_ERASEBKGND:return 1;
            case WM_SIZE:self->frame_dirty_=true;InvalidateRect(w,nullptr,FALSE);return 0;
            case WM_PAINT:self->paint();return 0;
            case csp::MonitorEvents::changed_message:
                if(self->settings_.follow_monitor && IsWindowVisible(w)){
                    const double next=csp::effective_refresh(self->monitor_events_->tracking_available(),self->settings_.target_fps(),self->monitor_events_->current());
                    if(csp::refresh_changed(self->active_fps_,next))self->start_timer();
                }
                return 0;
            case WM_WINDOWPOSCHANGED:self->monitor_events_->location_changed();break;
            case WM_TIMER:

                if(self->frame_timer_.consume(wp))self->tick();
                else if(self->settings_.scanline_sync && self->sync_.ready(wp,1000.0/self->active_fps_))self->tick();
                else if(wp==1)self->tick();
                else if(wp==3){
                    self->monitor_events_->refresh(true);
                    auto playback=playback_control::get();
                    if(!self->refresh_running_ && csp::refresh_active(self->host_visible_ && IsWindowVisible(w)!=FALSE,playback->is_playing(),playback->is_paused()))self->start_timer();
                }
                return 0;
            case WM_SHOWWINDOW:
                if(!wp) {self->stop_refresh();self->animation_clock_valid_=false;KillTimer(w,1);KillTimer(w,3);self->stream_.release();self->last_time_=-1;}
                else {self->frame_dirty_=true;self->start_timer();}
                break;
            case WM_CONTEXTMENU: {
                keepAlive=self->retain?self->retain():service_ptr_t<service_base>();
                if(self->editing && self->editing())break;
                HMENU menu=CreatePopupMenu();if(!menu)return 0;
                self->build_menu(menu,0);
                POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
                if(p.x==-1 && p.y==-1){p={12,12};ClientToScreen(w,&p);}

                const auto command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,w,nullptr);

                DestroyMenu(menu);
                if(IsWindow(w) && self->window_==w)self->menu_command(command);
                return 0;
            }
            case WM_DESTROY:
                self->destroying_=true;
                self->d2d_.reset();self->monitor_events_->stop();
                self->stop_refresh();self->sync_.stop();self->animation_clock_valid_=false;KillTimer(w,1);KillTimer(w,3);self->stream_.release();self->clear_bitmap();self->render_cache_.gradients.surface.clear();self->render_cache_.contour.surface.clear();self->render_cache_.gradients.valid=false;self->engine_.reset();self->snapshot_.invalidate();self->last_time_=-1;return 0;
            case WM_NCDESTROY:
                SetWindowLongPtr(w,GWLP_USERDATA,0);self->window_=nullptr;break;
            }
        } catch(const std::exception& e) {
            csp::report_once(self->reported_,e.what(),[](const char* message){console::print(message);});
        } catch(...) {self->reported_=true;}
        return DefWindowProc(w,msg,wp,lp);
    }
    HWND window_=nullptr;
    bool host_visible_=true,destroying_=false,settings_open_=false;
    visualisation_stream::ptr stream_;
    audio_chunk_impl chunk_;
    std::vector<double> samples_;
    std::vector<uint32_t> rotated_,background_cache_;
    RECT background_location_{};
    ScanlineSync sync_;
    csp::FrameTimer frame_timer_;
    std::shared_ptr<csp::MonitorEvents> monitor_events_=std::make_shared<csp::MonitorEvents>();
    bool refresh_running_=false;
    double active_fps_=71;
    uint32_t animation_wall_=0;
    bool animation_clock_valid_=false;
    csp::Settings settings_;
    csp::Engine engine_;
    csp::RenderCache render_cache_;
    csp::D2DRenderer d2d_;
    csp::StopRepaint stop_repaint_;
    HDC dc_=nullptr;
    HBITMAP bitmap_=nullptr;
    HGDIOBJ old_bitmap_=nullptr;
    uint32_t* pixels_=nullptr;
    int bitmap_w_=0,bitmap_h_=0;
    csp::SnapshotState snapshot_;
    bool frame_dirty_=false;
    uint32_t frame_time_=0,frame_mask_=0;
    unsigned frame_bins_=0,frame_channels_=0,frame_rate_=0;
    double last_time_=-1;
    bool reported_=false;
};
}
