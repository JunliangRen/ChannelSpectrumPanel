#include <foobar2000/SDK/foobar2000.h>
#include <columns_ui-sdk/ui_extension.h>
#include <windowsx.h>
#include <bit>
#include "spectrum.h"
#include "render.h"
#include "d2d_render.h"
#include "settings_ui.h"
#include "scanline.h"
#include "frame_timer.h"
#include "runtime_state.h"
#include <uxtheme.h>

DECLARE_COMPONENT_VERSION("Channel Spectrum Mirror", "1.0",
    "Columns UI stereo spectrum for x86, x64 and ARM64EC. Independent reconstruction inspired by Channel Spectrum panel 0.17.2.\n"
    "Mirrored channels, original amplitude curves and fixed-point falling peaks. Experimental first version.");
#if defined(_M_ARM64EC)
VALIDATE_COMPONENT_FILENAME("foo_uie_channel_spectrum_arm64ec.dll");
#elif defined(_WIN64)
VALIDATE_COMPONENT_FILENAME("foo_uie_channel_spectrum_x64.dll");
#else
VALIDATE_COMPONENT_FILENAME("foo_uie_channel_spectrum_x86.dll");
#endif

namespace {
constexpr GUID panel_guid={0x0c45a893,0x4607,0x4914,{0x8e,0x64,0x21,0x89,0xb3,0xa2,0x79,0x70}};
constexpr wchar_t window_class[]=L"ChannelSpectrumMirror.1";

class Panel : public uie::window, private play_callback_impl_base {
public:
    Panel():play_callback_impl_base(flag_on_playback_starting|flag_on_playback_new_track|flag_on_playback_stop|flag_on_playback_seek|flag_on_playback_pause){}
    const GUID& get_extension_guid() const override {return panel_guid;}
    void get_name(pfc::string_base& out) const override {out="Channel Spectrum Mirror";}
    void get_category(pfc::string_base& out) const override {out="Visualisations";}
    unsigned get_type() const override {return uie::type_panel;}
    bool is_available(const uie::window_host_ptr&) const override {return true;}
    HWND get_wnd() const override {return window_;}
    bool have_config_popup() const override {return true;}
    bool show_config_popup(HWND parent) override {
        DialogState dialog{settings_,[this](const csp::Settings& next){
            d2d_.reset();settings_=next;engine_.reset();frame_dirty_=true;stream_.release();last_time_=-1;reported_=false;
            if(window_){
                LONG_PTR ex=GetWindowLongPtr(window_,GWL_EXSTYLE)&~(WS_EX_CLIENTEDGE|WS_EX_STATICEDGE);
                if(next.edge==1)ex|=WS_EX_CLIENTEDGE;else if(next.edge==2)ex|=WS_EX_STATICEDGE;
                SetWindowLongPtr(window_,GWL_EXSTYLE,ex);SetWindowPos(window_,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
                background_cache_.clear();start_timer();InvalidateRect(window_,nullptr,FALSE);
            }
        }};
        dialog.monitor_events=monitor_events_;
        if(!window_){dialog.monitor_events=std::make_shared<csp::MonitorEvents>();dialog.monitor_fallback=!dialog.monitor_events->bind(parent);}
        INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_TAB_CLASSES|ICC_LISTVIEW_CLASSES|ICC_BAR_CLASSES|ICC_UPDOWN_CLASS};
        InitCommonControlsEx(&controls);
        DialogBoxParam(core_api::get_my_instance(),MAKEINTRESOURCE(IDD_SETTINGS),parent,settings_proc,reinterpret_cast<LPARAM>(&dialog));
        return dialog.changed;
    }
    void get_config(stream_writer* w,abort_callback& abort) const override {
        for(uint32_t word:csp::encode(settings_))w->write_lendian_t(word,abort);
    }
    void set_config(stream_reader* r,t_size size,abort_callback& abort) override {
        if(size>4096 || size%4)throw exception_io_data();
        std::vector<uint32_t> words(size/4);for(auto& word:words)r->read_lendian_t(word,abort);
        if(!csp::decode(words,settings_))throw exception_io_data();
    }
    HWND create_or_transfer_window(HWND parent,const uie::window_host_ptr& host,
        const ui_helpers::window_position_t& pos) override {
        if(window_) {
            ShowWindow(window_,SW_HIDE);SetParent(window_,parent);monitor_events_->location_changed();
            if(host_.is_valid())host_->relinquish_ownership(window_);
            host_=host;SetWindowPos(window_,nullptr,pos.x,pos.y,pos.cx,pos.cy,SWP_NOZORDER|SWP_NOACTIVATE);
        } else {
            WNDCLASSEX wc{sizeof(wc)};wc.lpfnWndProc=window_proc;wc.hInstance=core_api::get_my_instance();
            wc.lpszClassName=window_class;wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
            if(!RegisterClassEx(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return nullptr;
            host_=host;
            window_=CreateWindowEx(settings_.edge==1?WS_EX_CLIENTEDGE:settings_.edge==2?WS_EX_STATICEDGE:0,window_class,L"Channel Spectrum Mirror",WS_CHILD|WS_CLIPSIBLINGS|WS_CLIPCHILDREN,
                pos.x,pos.y,pos.cx,pos.cy,parent,nullptr,core_api::get_my_instance(),this);
            if(!window_)host_.release();
        }
        return window_;
    }
    void destroy_window() override {if(window_)DestroyWindow(window_);host_.release();}
private:
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
    void stop_refresh() noexcept {refresh_running_=false;if(window_)KillTimer(window_,1);sync_.stop();frame_timer_.stop();}
    void on_playback_pause(bool paused) noexcept override {
        if(paused){stop_refresh();animation_clock_valid_=false;}
        queue_playback_refresh();
    }
    void start_timer(){
        stop_refresh();
        KillTimer(window_,3);
        const bool listening=monitor_events_->bind(window_);
        monitor_events_->refresh(false);
        const bool tracked=csp::start_monitor_tracking(listening,
            [&]{return SetTimer(window_,3,5000,nullptr)!=0;},
            [&]{console::print("Channel Spectrum Mirror: display monitoring unavailable; using manual FPS.");});
        monitor_events_->set_polling_active(!listening && tracked);
        active_fps_=csp::effective_refresh(settings_.follow_monitor && tracked,settings_.target_fps(),monitor_events_->current());
        auto playback=playback_control::get();
        if(!csp::refresh_active(IsWindowVisible(window_)!=FALSE,playback->is_playing(),playback->is_paused())){animation_clock_valid_=false;return;}
        if(settings_.scanline_sync && sync_.start(window_)){refresh_running_=true;return;}
        if(frame_timer_.start(window_,active_fps_)){refresh_running_=true;return;}
        csp::start_fallback_timer(
            [&]{return SetTimer(window_,1,UINT(std::ceil(1000.0/active_fps_)),nullptr)!=0;},
            [&]{if(settings_.scanline_sync)console::print("Channel Spectrum Mirror: scanline sync unavailable; using regular timer.");});
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
        if(!IsWindowVisible(window_)) {stop_refresh();animation_clock_valid_=false;stream_.release();last_time_=-1;return;}
        auto playback=playback_control::get();
        const bool playing=playback->is_playing();
        const bool clearStopped=stop_repaint_.update(playing);
        if(!playing) {stop_refresh();animation_clock_valid_=false;
            if(clearStopped){stream_.release();engine_.reset();snapshot_.invalidate();frame_dirty_=false;last_time_=-1;InvalidateRect(window_,nullptr,FALSE);}
            return;
        }
        if(playback->is_paused()){
            stop_refresh();animation_clock_valid_=false;
            if(snapshot_.valid && frame_dirty_)update_cached_frame(false);
            return;
        }
        if(!stream_.is_valid())visualisation_manager::get()->create_stream(stream_,settings_.new_fft?1:0);
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
        auto* self=reinterpret_cast<Panel*>(GetWindowLongPtr(w,GWLP_USERDATA));
        if(msg==WM_NCCREATE) {
            self=static_cast<Panel*>(reinterpret_cast<CREATESTRUCT*>(lp)->lpCreateParams);
            self->window_=w;SetWindowLongPtr(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
        }
        if(!self)return DefWindowProc(w,msg,wp,lp);
        try {
            switch(msg) {
            case WM_CREATE:self->start_timer();return 0;
            case csp::MonitorEvents::visibility_message:
            case playback_refresh_message:self->start_timer();return 0;
            case csp::FrameTimer::message:if(self->frame_timer_.consume(wp))self->tick();return 0;
            case ScanlineSync::message:if(self->settings_.scanline_sync && self->sync_.ready(1000.0/self->active_fps_))self->tick();return 0;
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
                if(wp==1)self->tick();
                else if(wp==3){
                    self->monitor_events_->refresh(true);
                    auto playback=playback_control::get();
                    if(!self->refresh_running_ && csp::refresh_active(IsWindowVisible(w)!=FALSE,playback->is_playing(),playback->is_paused()))self->start_timer();
                }
                return 0;
            case WM_SHOWWINDOW:
                if(!wp) {self->stop_refresh();self->animation_clock_valid_=false;KillTimer(w,1);KillTimer(w,3);self->stream_.release();self->last_time_=-1;}
                else {self->frame_dirty_=true;self->start_timer();}
                break;
            case WM_CONTEXTMENU: {
                HMENU menu=CreatePopupMenu();if(!menu)return 0;
                AppendMenu(menu,MF_STRING,1,L"Configure...");
                struct Mode {const wchar_t* label;csp::Style style;};
                constexpr Mode modes[]={
                    {L"Simple",csp::Style::Simple},{L"Horizontal",csp::Style::Horizontal},
                    {L"Vertical",csp::Style::Vertical},{L"Simple-antialiasing",csp::Style::SimpleAntialias},
                    {L"Horizontal-antialiasing",csp::Style::HorizontalAntialias},{L"Simple-line",csp::Style::SimpleLine}
                };
                if(HMENU colors=CreatePopupMenu()){
                    for(unsigned i=0;i<std::size(modes);++i)
                        AppendMenu(colors,MF_STRING|(self->settings_.style==modes[i].style?MF_CHECKED:0),10+i,modes[i].label);
                    // The parent owns the submenu only after a successful append.
                    if(!AppendMenu(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(colors),L"Color Mode"))DestroyMenu(colors);
                }
                POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
                if(p.x==-1 && p.y==-1){p={12,12};ClientToScreen(w,&p);}
                const auto command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,w,nullptr);
                DestroyMenu(menu);
                if(command==1)self->show_config_popup(w);
                else if(command>=10 && command<10+std::size(modes)){
                    csp::set_drawing_style(self->settings_,modes[command-10].style,self->frame_dirty_);
                    InvalidateRect(w,nullptr,FALSE);
                }
                return 0;
            }
            case WM_DESTROY:
                self->d2d_.reset();self->monitor_events_->stop();
                self->stop_refresh();self->animation_clock_valid_=false;KillTimer(w,1);KillTimer(w,3);self->stream_.release();self->clear_bitmap();self->render_cache_.gradients.surface.clear();self->render_cache_.contour.surface.clear();self->render_cache_.gradients.valid=false;self->engine_.reset();self->snapshot_.invalidate();self->last_time_=-1;return 0;
            case WM_NCDESTROY:
                SetWindowLongPtr(w,GWLP_USERDATA,0);self->window_=nullptr;break;
            }
        } catch(const std::exception& e) {
            csp::report_once(self->reported_,e.what(),[](const char* message){console::print(message);});
        } catch(...) {self->reported_=true;}
        return DefWindowProc(w,msg,wp,lp);
    }
    HWND window_=nullptr;
    uie::window_host_ptr host_;
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
static uie::window_factory<Panel> factory;
}
