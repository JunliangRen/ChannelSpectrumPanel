#pragma once
#include <foobar2000/SDK/foobar2000.h>
#include <windowsx.h>
#include <bit>
#include <memory>
#include <type_traits>
#include "spectrum.h"
#include "render.h"
#include "rendered_fps.h"
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

constexpr wchar_t fullscreen_class[]=L"ChannelSpectrumPanel.Fullscreen";

struct MenuDeleter {
    void operator()(HMENU menu) const noexcept {DestroyMenu(menu);}
};
using OwnedMenu=std::unique_ptr<std::remove_pointer_t<HMENU>,MenuDeleter>;

// Panel lifecycle and class registration are confined to the host UI thread.
// USER32 refuses to unregister a class while another instance still has an HWND.
class PanelWindowClass {
public:
    bool ensure(const wchar_t* name,WNDPROC proc){
        if(atom_)return true;
        WNDCLASSEX wc{sizeof(wc)};
        wc.style=CS_DBLCLKS;
        wc.lpfnWndProc=proc;
        wc.hInstance=core_api::get_my_instance();
        wc.lpszClassName=name;
        wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
        atom_=RegisterClassEx(&wc);
        module_=wc.hInstance;
        return atom_!=0;
    }
    void release_if_unused() noexcept {
        if(atom_ && UnregisterClass(MAKEINTATOM(atom_),module_))atom_=0;
    }
private:
    ATOM atom_=0;
    HINSTANCE module_=nullptr;
};
PanelWindowClass embedded_registration,fullscreen_registration;

class SpectrumWindow : private play_callback_impl_base {
public:
    explicit SpectrumWindow(decltype(&EnumDisplayMonitors) enumerate=&EnumDisplayMonitors,csp::MonitorEvents::Query query_rate=csp::query_monitor_refresh,
        decltype(&SetWindowPos) position_popup=&SetWindowPos,csp::MonitorEvents::Query panel_query=csp::query_monitor_refresh,
        const csp::D2DTargetOperations& render_ops=csp::D2DTargetOperations::system()):play_callback_impl_base(flag_on_playback_starting|flag_on_playback_new_track|flag_on_playback_stop|flag_on_playback_seek|flag_on_playback_pause),enumerate_displays_(enumerate),query_rate_(query_rate),position_popup_(position_popup),
        monitor_events_(std::make_shared<csp::MonitorEvents>(panel_query)),d2d_(render_ops){}
    ~SpectrumWindow(){destroy_window();}
    SpectrumWindow(const SpectrumWindow&)=delete;
    SpectrumWindow& operator=(const SpectrumWindow&)=delete;
    const wchar_t* fps_label() const noexcept {return show_fps_?fps_.label():nullptr;}
    uint64_t rendered_frames() const noexcept {return fps_.frames();}
    double active_fps() const noexcept {return active_fps_;}
    HWND fullscreen_window() const noexcept {return fullscreen_;}
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
            background_cache_.clear();start_timer();InvalidateRect(output(),nullptr,FALSE);
        }
    }
    void host_visibility(bool visible){
        host_visible_=visible;
        if(!window_)return;
        if(!visible){leave_fullscreen(false);stop_refresh();animation_clock_valid_=false;stream_.release();last_time_=-1;}
        else {frame_dirty_=true;start_timer();InvalidateRect(output(),nullptr,FALSE);}
    }
    void theme_changed(){background_cache_.clear();if(window_)InvalidateRect(output(),nullptr,FALSE);}
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
        if(!embedded_registration.ensure(window_class,window_proc))return nullptr;
        HWND panel=CreateWindowEx(settings_.edge==1?WS_EX_CLIENTEDGE:settings_.edge==2?WS_EX_STATICEDGE:0,window_class,L"Channel Spectrum Panel",WS_CHILD|WS_CLIPSIBLINGS|WS_CLIPCHILDREN,
            x,y,width,height,parent,nullptr,core_api::get_my_instance(),this);
        if(!panel)embedded_registration.release_if_unused();
        return panel;
    }
    void transfer_window(HWND parent){
        ShowWindow(window_,SW_HIDE);SetParent(window_,parent);monitor_events_->location_changed();
    }
    void destroy_window() noexcept {
        if(window_)DestroyWindow(window_);
        // Also runs on service destruction after an externally destroyed HWND.
        fullscreen_registration.release_if_unused();
        embedded_registration.release_if_unused();
    }
    void build_menu(HMENU menu,unsigned base){
        const auto language=unsigned(interface_language.get_value());
        AppendMenu(menu,MF_STRING,base+1,csp::translate(language,L"Configure..."));
        if(!editing || !editing())AppendMenu(menu,MF_STRING,base+2,csp::translate(language,L"Fullscreen"));
        append_fps_command(menu,base,language);
        if(OwnedMenu colors{CreatePopupMenu()}){
            for(unsigned i=0;i<std::size(modes);++i)
                AppendMenu(colors.get(),MF_STRING|(settings_.style==modes[i].style?MF_CHECKED:0),base+10+i,csp::translate(language,modes[i].label));
            if(AppendMenu(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(colors.get()),csp::translate(language,L"Color Mode")))colors.release();
        }
    }
    void menu_command(unsigned command){
        if(!window_)return;
        if(command==1)show_config_popup(window_);
        else if(command==2)enter_fullscreen();
        else if(command==3){show_fps_=!show_fps_;fps_.reset();d2d_.retry_overlay();InvalidateRect(output(),nullptr,FALSE);}
        else if(command>=10 && command<10+std::size(modes)){
            csp::set_drawing_style(settings_,modes[command-10].style,frame_dirty_);
            if(window_)InvalidateRect(output(),nullptr,FALSE);
        }
    }
protected:
    virtual bool present_gdi(HDC target,HDC source,int width,int height) noexcept {
        return BitBlt(target,0,0,width,height,source,0,0,SRCCOPY)!=FALSE;
    }
private:
    void append_fps_command(HMENU menu,unsigned base,unsigned language) const {
        AppendMenu(menu,MF_STRING|(show_fps_?MF_CHECKED:MF_UNCHECKED),base+3,
            csp::translate(language,L"Show FPS"));
    }
    HWND output() const noexcept {return fullscreen_?fullscreen_:window_;}
    void target_changed(){
        fps_.reset();
        d2d_.reset();clear_bitmap();frame_dirty_=true;
        if(window_ && !destroying_)InvalidateRect(output(),nullptr,FALSE);
    }
    void enter_fullscreen(){
        if(fullscreen_ || !window_ || destroying_ || settings_open_ || (editing && editing()))return;
        MONITORINFOEX info{};info.cbSize=sizeof(info);
        if(!GetMonitorInfo(MonitorFromWindow(window_,MONITOR_DEFAULTTONEAREST),&info))return;
        if(!fullscreen_registration.ensure(fullscreen_class,fullscreen_proc))return;
        const RECT r=info.rcMonitor;
        wcscpy_s(fullscreen_device_,info.szDevice);
        HWND popup=CreateWindowEx(WS_EX_TOOLWINDOW,fullscreen_class,L"Channel Spectrum Panel",WS_POPUP,
            r.left,r.top,r.right-r.left,r.bottom-r.top,GetAncestor(window_,GA_ROOT),nullptr,core_api::get_my_instance(),this);
        if(!popup){fullscreen_registration.release_if_unused();return;}
        fullscreen_=popup;
        const auto& panel_rate=monitor_events_->current();
        fullscreen_rate_=!_wcsicmp(panel_rate.device,info.szDevice)?panel_rate:csp::MonitorRefresh{};
        reset_display_retries();
        update_fullscreen_rate(popup,false);
        target_changed();
        ShowWindow(popup,SW_SHOW);
        if(fullscreen_==popup && GetForegroundWindow()==popup)SetFocus(popup);
        start_timer();
    }
    void leave_fullscreen(bool user){
        HWND popup=fullscreen_;
        if(!popup)return;
        HWND owner=GetAncestor(window_,GA_ROOT);
        if(user && GetForegroundWindow()==popup && owner && IsWindowEnabled(owner)){
            SetForegroundWindow(owner);
            if(window_ && GetForegroundWindow()==owner)SetFocus(window_);
        }
        if(fullscreen_==popup)DestroyWindow(popup);
        fullscreen_registration.release_if_unused();
    }
    struct DisplaySearch {const wchar_t* device;RECT bounds{};bool found=false,incomplete=false;};
    static BOOL CALLBACK find_display(HMONITOR monitor,HDC,LPRECT,LPARAM param){
        auto& search=*reinterpret_cast<DisplaySearch*>(param);
        MONITORINFOEX info{};info.cbSize=sizeof(info);
        if(!GetMonitorInfo(monitor,&info)){search.incomplete=true;return TRUE;}
        if(!_wcsicmp(info.szDevice,search.device)){
            search.bounds=info.rcMonitor;search.found=true;return FALSE;
        }
        return TRUE;
    }
    void set_display_retry(HWND popup,bool needed){
        if(needed && !display_retry_)display_retry_=SetTimer(popup,display_retry_id,250,nullptr)!=0;
        if(!needed && display_retry_){KillTimer(popup,display_retry_id);display_retry_=false;}
    }
    void reset_display_retries() noexcept {
        topology_retries_=0;
        rate_retries_=0;
        integer_candidate_={};
    }
    void update_fullscreen_rate(HWND popup,bool retry,bool positioned=true){
        const auto rate=positioned?query_rate_(popup):csp::MonitorRefresh{};
        if(fullscreen_!=popup)return;
        if(retry)++rate_retries_;
        const bool attempts_left=rate_retries_<max_display_retries;
        // Only the target display can supply a rate or settle a previous candidate.
        // A failed identity lookup also returns zero Hz, without a device name.
        if(!positioned || !rate.device[0] || _wcsicmp(rate.device,fullscreen_device_)!=0){
            set_display_retry(popup,attempts_left);
            return;
        }
        const bool precise=rate.hz>0 && rate.precise;
        const bool retry_again=!precise && attempts_left;
        if(precise){
            fullscreen_rate_=rate;
        }else{
            if(rate.hz>0)integer_candidate_=rate;
            // The last query can fail even after a usable integer was read.
            if(integer_candidate_.hz>0 && (!fullscreen_rate_.precise || !retry_again))
                fullscreen_rate_=integer_candidate_;
        }
        set_display_retry(popup,retry_again);
    }
    static void end_owned_menu(HWND window) noexcept {
        GUITHREADINFO gui{sizeof(gui)};
        if(GetGUIThreadInfo(GetCurrentThreadId(),&gui) && gui.hwndMenuOwner==window)EndMenu();
    }
    void display_changed(bool retry){
        const HWND popup=fullscreen_;
        if(!retry)reset_display_retries();
        DisplaySearch display{fullscreen_device_};
        const BOOL complete=enumerate_displays_(nullptr,nullptr,find_display,reinterpret_cast<LPARAM>(&display));
        if(fullscreen_!=popup)return;
        if(!display.found && (!complete || display.incomplete)){
            if(retry)++topology_retries_;
            set_display_retry(popup,topology_retries_<max_display_retries);
            return;
        }
        if(!display.found){leave_fullscreen(false);return;}
        const RECT r=display.bounds;
        const BOOL positioned=position_popup_(popup,nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE);
        if(fullscreen_==popup){update_fullscreen_rate(popup,retry,positioned!=FALSE);start_timer();}
    }
    static LRESULT CALLBACK fullscreen_proc(HWND w,UINT msg,WPARAM wp,LPARAM lp) noexcept {
        auto* self=reinterpret_cast<SpectrumWindow*>(GetWindowLongPtr(w,GWLP_USERDATA));
        if(msg==WM_NCCREATE){
            self=static_cast<SpectrumWindow*>(reinterpret_cast<CREATESTRUCT*>(lp)->lpCreateParams);
            SetWindowLongPtr(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
        }
        if(!self)return DefWindowProc(w,msg,wp,lp);
        service_ptr_t<service_base> hold;
        try{
            switch(msg){
            case WM_PAINT:
                if(self->fullscreen_==w)self->paint();else ValidateRect(w,nullptr);
                return 0;
            case WM_ERASEBKGND:return 1;
            case WM_SIZE:self->frame_dirty_=true;InvalidateRect(w,nullptr,FALSE);return 0;
            case WM_TIMER:
                if(wp!=display_retry_id || !self->display_retry_)break;
                [[fallthrough]];
            case WM_DEVICECHANGE:case WM_DISPLAYCHANGE:
                hold=self->retain?self->retain():service_ptr_t<service_base>();
                if(self->fullscreen_==w)self->display_changed(msg==WM_TIMER);
                return 0;
            case WM_KEYDOWN:if(wp!=VK_ESCAPE)break;[[fallthrough]];
            case WM_CLOSE:case WM_LBUTTONDBLCLK:
                hold=self->retain?self->retain():service_ptr_t<service_base>();
                self->leave_fullscreen(true);return 0;
            case WM_CONTEXTMENU:{
                hold=self->retain?self->retain():service_ptr_t<service_base>();
                OwnedMenu menu{CreatePopupMenu()};if(!menu)return 0;
                const auto language=unsigned(interface_language.get_value());
                AppendMenu(menu.get(),MF_STRING,1,csp::translate(language,L"Exit fullscreen"));
                self->append_fps_command(menu.get(),0,language);
                POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
                if(lp==-1)GetCursorPos(&point);
                const auto cmd=TrackPopupMenu(menu.get(),TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,w,nullptr);
                menu.reset();
                if(self->fullscreen_==w){
                    if(cmd==1)self->leave_fullscreen(true);
                    else if(cmd==3)self->menu_command(cmd);
                }
                return 0;
            }
            case WM_DESTROY:end_owned_menu(w);break;
            case WM_NCDESTROY:
                SetWindowLongPtr(w,GWLP_USERDATA,0);
                if(self->fullscreen_==w){
                    KillTimer(w,display_retry_id);self->display_retry_=false;
                    self->fullscreen_=nullptr;self->target_changed();
                    if(self->window_ && !self->destroying_)self->start_timer();
                }
                break;
            }
        }catch(...){}
        return DefWindowProc(w,msg,wp,lp);
    }
    decltype(&EnumDisplayMonitors) enumerate_displays_;
    static constexpr UINT_PTR display_retry_id=4;
    csp::MonitorEvents::Query query_rate_;
    decltype(&SetWindowPos) position_popup_;
    static constexpr unsigned max_display_retries=3;
    unsigned topology_retries_=0,rate_retries_=0;
    csp::MonitorRefresh integer_candidate_;
    bool display_retry_=false;
    HWND fullscreen_=nullptr;
    wchar_t fullscreen_device_[CCHDEVICENAME]{};
    csp::MonitorRefresh fullscreen_rate_;
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
        if(window_)InvalidateRect(output(),nullptr,FALSE);
    }
    void on_playback_starting(play_control::t_track_command,bool) noexcept override {invalidate_snapshot();queue_playback_refresh();}
    void on_playback_new_track(metadb_handle_ptr) noexcept override {invalidate_snapshot();queue_playback_refresh();}
    void on_playback_stop(play_control::t_stop_reason) noexcept override {stop_refresh();animation_clock_valid_=false;invalidate_snapshot();}
    void on_playback_seek(double) noexcept override {invalidate_snapshot();}
    static constexpr UINT playback_refresh_message=WM_APP+103;
    void queue_playback_refresh() noexcept {if(window_)PostMessage(window_,playback_refresh_message,0,0);}
    void stop_refresh() noexcept {
        if(refresh_running_ && show_fps_){
            fps_.reset();
            if(window_ && !destroying_)InvalidateRect(output(),nullptr,FALSE);
        }
        refresh_running_=false;if(window_)KillTimer(window_,1);sync_.pause();frame_timer_.stop();}
    void on_playback_pause(bool paused) noexcept override {
        if(paused){stop_refresh();animation_clock_valid_=false;}
        queue_playback_refresh();
    }
    void start_timer(){
        if(!window_ || destroying_)return;
        if(fullscreen_ && !IsWindowVisible(window_)){leave_fullscreen(false);return;}
        KillTimer(window_,3);
        const bool listening=monitor_events_->bind(window_);
        monitor_events_->refresh(false);
        const bool tracked=csp::start_monitor_tracking(listening,
            [&]{return SetTimer(window_,3,5000,nullptr)!=0;},
            [&]{console::print("Channel Spectrum Panel: display monitoring unavailable; using manual FPS.");});
        monitor_events_->set_polling_active(!listening && tracked);
        const double next_fps=csp::effective_refresh(settings_.follow_monitor && tracked,settings_.target_fps(),(fullscreen_?fullscreen_rate_:monitor_events_->current()));
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
        fps_overlay_.reset();
        dc_=nullptr;bitmap_=nullptr;old_bitmap_=nullptr;pixels_=nullptr;bitmap_w_=bitmap_h_=0;
    }
    void paint() {

        csp::PaintSession paintSession(output());const HDC target=paintSession.dc;
        if(snapshot_.valid && frame_dirty_)update_cached_frame(false);
        RECT rc{};
        if(!target || !GetClientRect(output(),&rc))return;
        const int w=rc.right,h=rc.bottom;
        if(w>0 && h>0) {
            const wchar_t* label=fps_label();
            bool copied=false;
            const bool direct=d2d_.draw_or_fallback(output(),w,h,engine_.bars(),engine_.ticks(),settings_,[&]{
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
                    RECT location;GetWindowRect(output(),&location);
                    if(!settings_.fast_transparent || background_cache_.size()!=size_t(w)*h || !EqualRect(&location,&background_location_)){
                        std::fill_n(pixels_,size_t(w)*h,settings_.background);
                        if(!fullscreen_)DrawThemeParentBackground(window_,dc_,nullptr);
                        GdiFlush();
                        background_cache_.assign(pixels_,pixels_+size_t(w)*h);background_location_=location;
                    }
                    csp::rotate_pixels(background_cache_.data(),w,h,drawing,(4-settings_.rotation)%4);
                }
                csp::render(drawing,lw,lh,engine_.bars(),settings_,settings_.transparent,engine_.ticks(),&render_cache_);
                if(settings_.rotation)csp::rotate_pixels(drawing,lw,lh,pixels_,settings_.rotation);
                if(label)fps_overlay_.draw(dc_,w,h,label);
                copied=present_gdi(target,dc_,w,h);
            } else FillRect(target,&rc,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            },label);
            if(show_fps_ && refresh_running_ && (direct || copied))fps_.presented(GetTickCount64());
        }
    }
    void tick() {

        if(!host_visible_ || !IsWindowVisible(window_)) {stop_refresh();animation_clock_valid_=false;stream_.release();last_time_=-1;return;}
        auto playback=playback_control::get();
        const bool playing=playback->is_playing();
        const bool clearStopped=stop_repaint_.update(playing);
        if(!playing) {stop_refresh();animation_clock_valid_=false;
            if(clearStopped){engine_.reset();snapshot_.invalidate();frame_dirty_=false;last_time_=-1;InvalidateRect(output(),nullptr,FALSE);}
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
            engine_.reset();snapshot_.invalidate();frame_dirty_=false;last_time_=-1;InvalidateRect(output(),nullptr,FALSE);return;
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
        RECT rc;GetClientRect(output(),&rc);
        auto displaySettings=settings_;displaySettings.new_fft=snapshot_.mode_for_redraw(settings_.new_fft);
        engine_.update(samples_,unsigned(bins),channels,left,right,(settings_.rotation&1)?rc.bottom:rc.right,(settings_.rotation&1)?rc.right:rc.bottom,frame_time_,displaySettings,frame_rate_,mask,animate);
        frame_dirty_=false;
        InvalidateRect(output(),nullptr,FALSE);
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
            case WM_PAINT:
                if(self->fullscreen_)ValidateRect(w,nullptr);else self->paint();
                return 0;
            case WM_LBUTTONDBLCLK:
                if(self->editing && self->editing())break;
                keepAlive=self->retain?self->retain():service_ptr_t<service_base>();
                self->enter_fullscreen();return 0;
            case csp::MonitorEvents::changed_message:
                if(self->settings_.follow_monitor && IsWindowVisible(w)){
                    const double next=csp::effective_refresh(self->monitor_events_->tracking_available(),self->settings_.target_fps(),(self->fullscreen_?self->fullscreen_rate_:self->monitor_events_->current()));
                    if(csp::refresh_changed(self->active_fps_,next))self->start_timer();
                }
                return 0;
            case WM_WINDOWPOSCHANGED:self->monitor_events_->location_changed();break;
            case WM_TIMER:

                if(self->frame_timer_.consume(wp))self->tick();
                else if(self->settings_.scanline_sync && self->sync_.ready(wp,1000.0/self->active_fps_,self->output()))self->tick();
                else if(wp==1)self->tick();
                else if(wp==3){
                    self->monitor_events_->refresh(true);
                    auto playback=playback_control::get();
                    if(!self->refresh_running_ && csp::refresh_active(self->host_visible_ && IsWindowVisible(w)!=FALSE,playback->is_playing(),playback->is_paused()))self->start_timer();
                }
                return 0;
            case WM_SHOWWINDOW:
                if(!wp) {self->leave_fullscreen(false);self->stop_refresh();self->animation_clock_valid_=false;KillTimer(w,1);KillTimer(w,3);self->stream_.release();self->last_time_=-1;}
                else {self->frame_dirty_=true;self->start_timer();}
                break;
            case WM_CONTEXTMENU: {
                keepAlive=self->retain?self->retain():service_ptr_t<service_base>();
                if(self->editing && self->editing())break;
                OwnedMenu menu{CreatePopupMenu()};if(!menu)return 0;
                self->build_menu(menu.get(),0);
                POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
                if(p.x==-1 && p.y==-1){p={12,12};ClientToScreen(w,&p);}

                const auto command=TrackPopupMenu(menu.get(),TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,w,nullptr);

                menu.reset();
                if(IsWindow(w) && self->window_==w)self->menu_command(command);
                return 0;
            }
            case WM_DESTROY:
                self->destroying_=true;end_owned_menu(w);self->leave_fullscreen(false);
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
    std::shared_ptr<csp::MonitorEvents> monitor_events_;
    bool refresh_running_=false;
    double active_fps_=71;
    uint32_t animation_wall_=0;
    bool animation_clock_valid_=false;
    csp::Settings settings_;
    csp::Engine engine_;
    csp::RenderCache render_cache_;
    csp::D2DRenderer d2d_;
    csp::RenderedFps fps_;
    bool show_fps_=false;
    csp::StopRepaint stop_repaint_;
    csp::GdiFpsOverlay fps_overlay_;
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
