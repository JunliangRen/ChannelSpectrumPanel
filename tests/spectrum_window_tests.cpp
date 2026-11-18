#include "../src/panel_window.h"
#include "../src/ui_adapters.h"
#include <foobar2000/SDK/component.h>
#include <iostream>
#include <set>
#include <map>

extern "C" foobar2000_client* __cdecl foobar2000_get_interface(foobar2000_api*,HINSTANCE);
namespace {
int checks=0;
void check(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
class Playback : public playback_control {
public:
    bool playing=true,paused=false;
    bool get_now_playing(metadb_handle_ptr&) override{return false;}
    void start(t_track_command,bool p) override{playing=true;paused=p;}
    void stop() override{playing=false;}
    bool is_playing() override{return playing;}
    bool is_paused() override{return paused;}
    void pause(bool p) override{paused=p;}
    bool get_stop_after_current() override{return false;}
    void set_stop_after_current(bool) override{}
    void set_volume(float) override{}
    float get_volume() override{return 0;}
    void volume_up() override{} void volume_down() override{} void volume_mute_toggle() override{}
    void playback_seek(double) override{} void playback_seek_delta(double) override{}
    bool playback_can_seek() override{return true;}
    double playback_get_position() override{return GetTickCount64()/1000.0;}
    bool playback_format_title(titleformat_hook*,pfc::string_base&,const service_ptr_t<titleformat_object>&,titleformat_text_filter*,t_display_level) override{return false;}
};
class Callbacks : public play_callback_manager {
public:
    std::set<play_callback*> clients;
    void register_callback(play_callback* p,unsigned,bool) override{if(!clients.insert(p).second)throw std::runtime_error("duplicate callback");}
    void unregister_callback(play_callback* p) override{clients.erase(p);}
};
class Stream : public visualisation_stream {
public:
    inline static unsigned reads=0,live=0,created=0,probes=0,primary_valid=0;
    inline static bool waiting=false,throw_probe=false,clock_ready=true;
    double requested_time=0;
    Stream(){++live;++created;}~Stream(){--live;}
    bool get_absolute_time(double& time) override{requested_time=time=GetTickCount64()/1000.0;return clock_ready;}
    bool get_chunk_absolute(audio_chunk& chunk,double,double) override{
        ++probes;if(throw_probe)throw std::runtime_error("probe failure");
        audio_sample data[2]{};chunk.set_data(data,1,2,48000,audio_chunk::channel_config_stereo);return true;
    }
    bool get_spectrum_absolute(audio_chunk& chunk,double offset,unsigned fft) override{
        ++reads;
        if(waiting){
            if(fft==4096 && offset==requested_time)return false;
            ++probes;if(throw_probe)throw std::runtime_error("probe FFT failure");
        }else ++primary_valid;
        std::vector<audio_sample> data(fft,audio_sample(.001));data[2]=audio_sample(.1);data[7]=audio_sample(.2);
        chunk.set_data(data.data(),fft/2,2,48000,audio_chunk::channel_config_stereo);return true;
    }
    void make_fake_chunk_absolute(audio_chunk&,double,double) override{}
    void make_fake_spectrum_absolute(audio_chunk&,double,unsigned) override{}
};
class Visualisation : public visualisation_manager {
public:
    bool fail=false;unsigned last_flags=0;
    void create_stream(visualisation_stream::ptr& out,unsigned flags) override{last_flags=flags;if(fail)throw std::runtime_error("create failure");out=new service_impl_t<Stream>();}
};
// The production language preference uses the core config store.
class ConfigStore : public fb2k::configStore {
    std::map<std::string,int64_t> values;
public:
    fb2k::objRef acquireTransactionScope() override{return nullptr;}
    void commitBlocking() override{}
    bool fail_integer_read=false;
    unsigned failed_reads=0;
    int64_t getConfigInt(const char* key,int64_t fallback) override{
        if(fail_integer_read){++failed_reads;throw std::runtime_error("Injected configuration read failure");}
        auto i=values.find(key);return i==values.end()?fallback:i->second;
    }
    void setConfigInt(const char* key,int64_t value) override{values[key]=value;}
    void deleteConfigInt(const char* key) override{values.erase(key);}
    fb2k::stringRef getConfigString(const char*,fb2k::stringRef fallback) override{return fallback;}
    void setConfigString(const char*,const char*) override{}void deleteConfigString(const char*) override{}
    fb2k::memBlockRef getConfigBlob(const char*,fb2k::memBlockRef fallback) override{return fallback;}
    void setConfigBlob(const char*,const void*,size_t) override{}void setConfigBlob(const char*,fb2k::memBlockRef) override{}void deleteConfigBlob(const char*) override{}
    double getConfigFloat(const char*,double fallback) override{return fallback;}
    void setConfigFloat(const char*,double) override{}void deleteConfigFloat(const char*) override{}
    void addNotify(const char*,fb2k::configStoreNotify*) override{}void removeNotify(const char*,fb2k::configStoreNotify*) override{}
    fb2k::arrayRef listDomainValues(const char*,bool) override{return nullptr;}
    void callNotify(const char*) override{}
};
class HostApi : public foobar2000_api {
public:
    service_ptr_t<Playback> playback=new service_impl_t<Playback>();
    service_ptr_t<Callbacks> callbacks=new service_impl_t<Callbacks>();
    service_ptr_t<Visualisation> visualisation=new service_impl_t<Visualisation>();
    service_ptr_t<ConfigStore> store=new service_impl_t<ConfigStore>();
    struct Entry{GUID guid;service_ptr_t<service_base> service;};
    Entry entries[4]={{playback_control::class_guid,playback},{play_callback_manager::class_guid,callbacks},{visualisation_manager::class_guid,visualisation},{fb2k::configStore::class_guid,store}};
    service_class_ref service_enum_find_class(const GUID& guid) override{for(auto& e:entries)if(e.guid==guid)return &e;return nullptr;}
    bool service_enum_create(service_ptr_t<service_base>& out,service_class_ref ref,t_size index) override{if(!ref || index)return false;out=static_cast<const Entry*>(ref)->service;return true;}
    t_size service_enum_get_count(service_class_ref ref) override{return ref?1:0;}
    HWND get_main_window() override{return nullptr;}
    bool assert_main_thread() override{return true;}bool is_main_thread() override{return true;}
    bool is_shutting_down() override{return false;}bool is_initializing() override{return false;}
    const char* get_profile_path() override{return "file://unused-test-profile";}
    bool is_portable_mode_enabled() override{return true;}bool is_quiet_mode_enabled() override{return true;}
};
class UiCallback : public ui_element_instance_callback_v2 {
public:
    bool visible=true;
    void on_min_max_info_change() override{}void on_alt_pressed(bool) override{}
    bool query_color(const GUID&,t_ui_color&) override{return false;}
    bool request_activation(ui_element_instance_ptr) override{return false;}
    bool is_edit_mode_enabled() override{return false;}
    void request_replace(ui_element_instance_ptr) override{}
    t_ui_font query_font_ex(const GUID&) override{return nullptr;}
    bool is_elem_visible(ui_element_instance_ptr) override{return visible;}
};
class ColumnsHost : public uie::window_host {
public:
    const GUID& get_host_guid() const override{return csp::panel_guid;}
    void on_size_limit_change(HWND,unsigned) override{}
    unsigned is_resize_supported(HWND) const override{return 0;}
    bool request_resize(HWND,unsigned,unsigned,unsigned) override{return false;}
    bool override_status_text_create(service_ptr_t<ui_status_text_override>&) override{return false;}
    bool is_visible(HWND window) const override{return IsWindowVisible(window)!=FALSE;}
    bool is_visibility_modifiable(HWND,bool) const override{return false;}
    bool set_window_visibility(HWND,bool) override{return false;}
    void relinquish_ownership(HWND) override{}
};
static uie::window_factory<csp::ColumnsPanel<SpectrumWindow>> columns_factory;
void pump(unsigned ms){
    const auto until=GetTickCount64()+ms;
    do{MSG m;while(PeekMessage(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessage(&m);}MsgWaitForMultipleObjects(0,nullptr,FALSE,5,QS_ALLINPUT);}while(GetTickCount64()<until);
}
bool wait_reads(unsigned previous){const auto until=GetTickCount64()+2500;while(Stream::reads==previous && GetTickCount64()<until)pump(10);return Stream::reads>previous;}
HWND parent_window(){
    HWND w=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"STATIC",L"CSP isolated native tests",WS_POPUP|WS_VISIBLE,-30000,-30000,320,150,nullptr,nullptr,GetModuleHandle(nullptr),nullptr);
    check(w!=nullptr,"native parent created");return w;
}
unsigned monitor_windows(){unsigned count=0;EnumThreadWindows(GetCurrentThreadId(),[](HWND w,LPARAM p)->BOOL{wchar_t name[128]{};GetClassNameW(w,name,128);if(wcscmp(name,L"ChannelSpectrumMirror.MonitorEvents.1")==0)++*reinterpret_cast<unsigned*>(p);return TRUE;},reinterpret_cast<LPARAM>(&count));return count;}
ui_element_config::ptr config(bool d2d,bool scanline=false,unsigned fps=60){csp::Settings s;s.direct2d=d2d;s.follow_monitor=false;s.scanline_sync=scanline;s.fps_milli=fps*1000;const auto b=csp::encode_panel_config(s);return ui_element_config::g_create(csp::default_ui_guid,b.data(),b.size());}
void lifecycle(HostApi& api,bool d2d,bool scanline=false){
    HWND parent=parent_window();
    service_ptr_t<UiCallback> callback=new service_impl_t<UiCallback>();
    service_ptr_t<csp::DefaultElement<SpectrumWindow>> element=new service_impl_t<csp::DefaultElement<SpectrumWindow>>();
    auto panel=element->instantiate(parent,config(d2d,scanline),callback);HWND window=panel->get_wnd();
    check(api.callbacks->clients.size()==1,"real playback callback registered");
    check(wait_reads(Stream::reads),"real timer reaches synthetic visualization service");
    UpdateWindow(window);check(Stream::live==1,"active real window owns stream");
    for(int i=0;i<4;++i)SendMessage(window,csp::MonitorEvents::visibility_message,0,0);
    panel->set_configuration(config(d2d,scanline,75));
    check(wait_reads(Stream::reads),"FPS change resumes rendering with cached scanline device");
    panel->notify(ui_element_notify_visibility_changed,0,nullptr,0);
    check(Stream::live==0,"host hiding releases stream immediately");unsigned before=Stream::reads;pump(100);check(Stream::reads==before,"hidden host does not process queued frames");
    panel->notify(ui_element_notify_visibility_changed,1,nullptr,0);check(wait_reads(before),"host restoring restarts real timer");
    ShowWindow(parent,SW_HIDE);SendMessage(window,csp::MonitorEvents::visibility_message,0,0);check(Stream::live==0,"ancestor visibility event releases stream before next tick");pump(100);check(Stream::live==0,"parent hiding releases stream");before=Stream::reads;pump(80);check(Stream::reads==before,"hidden parent stays inactive");
    ShowWindow(parent,SW_SHOWNOACTIVATE);check(wait_reads(before),"parent restoring resumes processing");
    ShowWindow(parent,SW_HIDE);
    const auto hidden_deadline=GetTickCount64()+2500;
    while(Stream::live && GetTickCount64()<hidden_deadline)pump(10);
    check(Stream::live==0,"native ancestor hide notification releases stream without injected message");
    before=Stream::reads;ShowWindow(parent,SW_SHOWNOACTIVATE);check(wait_reads(before),"native ancestor show resumes real window");
    auto second=element->instantiate(parent,config(!d2d),callback);check(wait_reads(Stream::reads),"second real panel starts");pump(100);
    check(api.callbacks->clients.size()==2 && Stream::live==2,"two real windows independently own callbacks and streams");
    DestroyWindow(window);check(!panel->get_wnd() && Stream::live==1,"removing one HWND releases only its stream");
    check(monitor_windows()==1,"removed panel detaches display observer");panel.release();check(api.callbacks->clients.size()==1,"removed panel unregisters callback");
    before=Stream::reads;check(wait_reads(before),"remaining panel continues rendering");
    HWND other=second->get_wnd();second.release();check(!IsWindow(other),"service-first removal destroys real window");
    check(Stream::live==0 && api.callbacks->clients.empty() && monitor_windows()==0,"all real window resources detached");
    pump(60);DestroyWindow(parent);
}
// Synthetic restricted pump only: this does not model the native close-button
// tracking loop, which can stop all dispatch on its owning thread.
void restricted_pump(unsigned milliseconds){
    const auto until=GetTickCount64()+milliseconds;
    do {
        MSG message;
        while(PeekMessage(&message,nullptr,WM_TIMER,WM_TIMER,PM_REMOVE))DispatchMessage(&message);
        while(PeekMessage(&message,nullptr,WM_PAINT,WM_PAINT,PM_REMOVE))DispatchMessage(&message);
        Sleep(1);
    }while(GetTickCount64()<until);
}
void interaction_refresh(HostApi&,bool scanline){
    HWND parent=parent_window();SpectrumWindow view;csp::Settings settings;
    settings.direct2d=false;settings.follow_monitor=false;settings.scanline_sync=scanline;
    view.apply_settings(settings);HWND window=view.create_window(parent,0,0,320,150);
    ShowWindow(window,SW_SHOWNOACTIVATE);check(wait_reads(Stream::reads),"interaction fixture starts");
    auto before=Stream::reads;restricted_pump(250);
    check(Stream::reads>=before+3,"refresh continues in timer/paint-only interaction loop");
    view.host_visibility(false);before=Stream::reads;restricted_pump(80);
    check(Stream::reads==before,"restricted loop does not restart hidden panel");
    view.host_visibility(true);check(wait_reads(before),"interaction fixture restores");
    settings.scanline_sync=!scanline;view.apply_settings(settings);before=Stream::reads;restricted_pump(250);
    check(Stream::reads>=before+3,"backend switch rejects old tokens and continues refresh");
    view.destroy_window();before=Stream::reads;restricted_pump(80);
    check(Stream::reads==before && Stream::live==0,"queued timer messages cannot revive destroyed panel");
    DestroyWindow(parent);
}
void columns_reopen(HostApi& api){
    HWND parent=parent_window();service_ptr_t<ColumnsHost> host=new service_impl_t<ColumnsHost>();
    service_ptr_t<service_base> instance;columns_factory.instance_create(instance);
    uie::window_ptr panel;check(instance->service_query_t(panel),"real CUI factory interface");instance.release();
    const ui_helpers::window_position_t position(0,0,320,150);
    for(int i=0;i<3;++i){
        HWND window=panel->create_or_transfer_window(parent,host,position);check(window!=nullptr,"real CUI window create/reopen");
        ShowWindow(window,SW_SHOWNOACTIVATE);check(wait_reads(Stream::reads),"reopened CUI window restarts timer and stream");
        panel->destroy_window();check(!panel->get_wnd() && Stream::live==0 && monitor_windows()==0,"CUI close detaches real window resources");
        check(api.callbacks->clients.size()==1,"CUI instance remains valid between window lifetimes");pump(30);
    }
    panel.release();check(api.callbacks->clients.empty(),"CUI final release unregisters callback");DestroyWindow(parent);
}
void early_stream_lifecycle(HostApi& api){
    api.playback->playing=false;
    HWND parent=parent_window();ShowWindow(parent,SW_HIDE);
    {
        SpectrumWindow view;csp::Settings settings;settings.fft=4096;settings.direct2d=false;settings.follow_monitor=false;settings.scanline_sync=false;
        view.apply_settings(settings);HWND window=view.create_window(parent,0,0,320,150);ShowWindow(window,SW_SHOWNOACTIVATE);pump(30);
        check(Stream::live==0,"hidden ancestor prevents early registration");
        const auto reads=Stream::reads,created=Stream::created;
        ShowWindow(parent,SW_SHOWNOACTIVATE);pump(80);
        check(Stream::live==1 && Stream::created==created+1,"visible stopped panel registers before first playback tick");
        check(Stream::reads==reads,"early registration does not request FFT or advance animation");
        for(int i=0;i<3;++i)SendMessage(window,csp::MonitorEvents::visibility_message,0,0);
        check(Stream::created==created+1,"duplicate visibility does not duplicate streams");
        view.host_visibility(false);check(Stream::live==0,"host hide releases prewarmed stream");
        api.playback->paused=true;view.host_visibility(true);pump(30);
        check(Stream::live==1 && Stream::reads==reads,"paused restore prewarms without FFT");
        settings.new_fft=!settings.new_fft;view.apply_settings(settings);
        check(Stream::live==1 && api.visualisation->last_flags==unsigned(settings.new_fft),"prewarmed stream follows FFT mode setting");
        view.host_visibility(false);api.visualisation->fail=true;view.host_visibility(true);pump(20);
        check(Stream::live==0,"early creation failure leaves no partial stream");
        api.visualisation->fail=false;view.host_visibility(true);check(Stream::live==1,"early creation retries on subsequent visibility event");
        Stream::waiting=true;api.playback->playing=true;api.playback->paused=false;
        for(auto* callback:api.callbacks->clients)callback->on_playback_starting(playback_control::track_command_play,false);
        pump(180);
        check(Stream::probes==0,"unavailable primary spectrum does not trigger PCM small FFT or offset probes");
        Stream::waiting=false;const auto valid=Stream::primary_valid;check(wait_reads(Stream::reads),"primary FFT resumes after data availability");
        check(Stream::primary_valid>valid,"configured FFT supplies first rendered frame");
        const auto probes=Stream::probes;pump(120);check(Stream::probes==probes,"probes stop after first primary success");
        api.playback->playing=false;
        for(auto* callback:api.callbacks->clients)callback->on_playback_stop(playback_control::stop_reason_user);
        const auto stopped_reads=Stream::reads;
        SendMessage(window,WM_TIMER,1,0);pump(30);
        check(Stream::live==1 && Stream::reads==stopped_reads,"stopped visible panel retains prewarm and ignores stale timer reads");
        api.playback->playing=true;
        for(auto* callback:api.callbacks->clients)callback->on_playback_starting(playback_control::track_command_play,false);
        Stream::waiting=true;Stream::clock_ready=false;
        for(auto* callback:api.callbacks->clients)callback->on_playback_seek(0);
        const auto no_clock_reads=Stream::reads;pump(120);
        check(Stream::reads==no_clock_reads && Stream::probes==0,"invalid clock does not request FFT or diagnostic data");
        Stream::clock_ready=true;pump(120);
        check(Stream::reads>no_clock_reads && Stream::probes==0,"restored clock resumes only configured FFT");
        view.destroy_window();check(Stream::live==0,"destroy during pending spectrum wait releases stream");
        view.host_visibility(true);pump(30);check(Stream::live==0,"destroyed panel cannot prewarm from later notifications");
        Stream::waiting=false;Stream::clock_ready=true;Stream::throw_probe=false;
    }
    check(api.callbacks->clients.empty(),"prewarm fixture unregisters callbacks");DestroyWindow(parent);
}

// Observe real WM_PAINT completion in the harness, without production counters.
struct PaintObserver {
    HWND window;unsigned long long completed=0,timers=0;
    static LRESULT CALLBACK callback(HWND w,UINT message,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR data){
        auto* self=reinterpret_cast<PaintObserver*>(data);
        if(message==WM_NCDESTROY){RemoveWindowSubclass(w,callback,id);self->window=nullptr;}
        const auto result=DefSubclassProc(w,message,wp,lp);
        if(message==WM_PAINT)++self->completed;
        if(message==WM_TIMER)++self->timers;
        return result;
    }
    explicit PaintObserver(HWND w):window(w){check(SetWindowSubclass(w,callback,reinterpret_cast<UINT_PTR>(this),reinterpret_cast<DWORD_PTR>(this))!=FALSE,"paint observer installed");}
    ~PaintObserver(){if(window)RemoveWindowSubclass(window,callback,reinterpret_cast<UINT_PTR>(this));}
};
struct BusySettingsRun {
    PaintObserver* paint=nullptr;
    std::atomic<unsigned> stage{0};
    std::atomic<DWORD> dialog_thread{0};
    std::atomic<bool> timer_failed{false};
    unsigned init_reads=0,hold_reads=0;
    unsigned long long init_paints=0,hold_paints=0;
    bool saw_init=false,saw_hold=false,init_progress=false;
};
BusySettingsRun* busy_settings=nullptr; // Main-thread-only timer context.
void CALLBACK observe_busy_settings(HWND,UINT,UINT_PTR,DWORD){
    auto& run=*busy_settings;const auto stage=run.stage.load();
    if(stage==1 && !run.saw_init){run.saw_init=true;run.init_reads=Stream::reads;run.init_paints=run.paint->completed;}
    if(stage>=2 && run.saw_init && !run.init_progress)
        run.init_progress=Stream::reads>=run.init_reads+3 && run.paint->completed>=run.init_paints+3;
    if(stage==3 && !run.saw_hold){run.saw_hold=true;run.hold_reads=Stream::reads;run.hold_paints=run.paint->completed;}
}
void CALLBACK block_settings_thread(HWND w,UINT,UINT_PTR id,DWORD){
    KillTimer(w,id);
    auto* run=static_cast<BusySettingsRun*>(GetProp(w,L"CspTestBusy"));
    RemoveProp(w,L"CspTestBusy");
    run->stage=3;
    // Deliberately process NO messages, stronger than a selective modal loop.
    Sleep(400);
    run->stage=4;
    SendMessage(w,WM_COMMAND,IDOK,0);
}
void independent_settings_refresh(HostApi&,bool scanline,bool direct2d){
    HWND parent=parent_window();SpectrumWindow view;csp::Settings settings;
    settings.direct2d=direct2d;settings.follow_monitor=false;settings.scanline_sync=scanline;
    view.apply_settings(settings);HWND window=view.create_window(parent,0,0,320,150);
    ShowWindow(window,SW_SHOWNOACTIVATE);check(wait_reads(Stream::reads),"threaded settings fixture is playing");
    const DWORD main_thread=GetCurrentThreadId();bool applied=false,language_saved=false;
    DialogState state{settings,[&](const csp::Settings& next){
        check(GetCurrentThreadId()==main_thread,"Apply runs on host thread");check(InSendMessageEx(nullptr)==ISMEX_NOSEND,"Apply uses posted dispatch rather than sent-message reentry");view.apply_settings(next);applied=true;
    }};
    state.save_language=[&](unsigned language){check(GetCurrentThreadId()==main_thread && language==1,"language save marshalled to host thread");language_saved=true;};
    PaintObserver paint(window);BusySettingsRun run;run.paint=&paint;busy_settings=&run;
    const auto timer=SetTimer(nullptr,0,10,observe_busy_settings);check(timer!=0,"settings progress observer starts");
    bool changed=false;
    try {
        changed=SettingsThread::run(GetModuleHandle(nullptr),window,state,[&](HWND dialog){
            run.dialog_thread=GetCurrentThreadId();run.stage=1;Sleep(400);
            SetDlgItemInt(dialog,IDC_RANGE,137,FALSE);
            SendDlgItemMessage(dialog,IDC_LANGUAGE,CB_SETCURSEL,1,0);
            SendMessage(dialog,WM_COMMAND,MAKEWPARAM(IDC_LANGUAGE,CBN_SELCHANGE),0);
            run.stage=2;
            if(!SetProp(dialog,L"CspTestBusy",&run) || !SetTimer(dialog,77,80,block_settings_thread)){
                RemoveProp(dialog,L"CspTestBusy");run.timer_failed=true;PostMessage(dialog,WM_COMMAND,IDCANCEL,0);
            }
        });
    }catch(...){KillTimer(nullptr,timer);busy_settings=nullptr;throw;}
    KillTimer(nullptr,timer);busy_settings=nullptr;
    check(run.dialog_thread!=main_thread,"settings window has independent UI thread");
    check(!run.timer_failed,"blocking settings fixture installed");
    check(run.saw_init && run.init_progress,"spectrum sampling AND painting continue through blocked dialog initialization");
    check(run.saw_hold && Stream::reads>=run.hold_reads+3 && run.paint->completed>=run.hold_paints+3,
        "spectrum sampling AND painting continue with settings thread completely blocked");
    check(changed && applied && language_saved && view.settings().range==137,"OK commits custom value through host dispatcher");
    check(IsWindowEnabled(parent)!=FALSE,"original host enabled after settings thread exits");
    view.destroy_window();DestroyWindow(parent);check(Stream::live==0,"threaded settings teardown releases stream");
}

struct DelayedSettingsStart {
    unsigned (__stdcall* entry)(void*);void* context;
    static unsigned __stdcall run(void* value){
        std::unique_ptr<DelayedSettingsStart> start(static_cast<DelayedSettingsStart*>(value));
        Sleep(200);return start->entry(start->context);
    }
};
uintptr_t __cdecl delayed_settings_launch(void* security,unsigned stack,unsigned(__stdcall* entry)(void*),void* context,unsigned flags,unsigned* threadId){
    auto start=std::make_unique<DelayedSettingsStart>(DelayedSettingsStart{entry,context});
    auto result=_beginthreadex(security,stack,DelayedSettingsStart::run,start.get(),flags,threadId);
    if(result)start.release();return result;
}
uintptr_t __cdecl failed_settings_launch(void*,unsigned,unsigned(__stdcall*)(void*),void*,unsigned,unsigned*){return 0;}
void settings_dispatch_shutdown(){
    HWND parent=parent_window();DialogState state{csp::Settings{},[](const csp::Settings&){}};
    const DWORD main_thread=GetCurrentThreadId();
    check(!SettingsThread::run(GetModuleHandle(nullptr),parent,state,[&](HWND){PostThreadMessage(main_thread,WM_QUIT,37,0);}),
        "main-thread quit cancels settings without committing");
    MSG message{};check(PeekMessage(&message,nullptr,WM_QUIT,WM_QUIT,PM_REMOVE) && message.wParam==37,"original quit code restored after joining settings worker");
    check(IsWindowEnabled(parent)!=FALSE,"quit restores disabled host");
    bool failed=false;
    try{SettingsThread::run(GetModuleHandleW(L"ntdll.dll"),parent,state);}catch(const std::exception&){failed=true;}
    check(failed && IsWindowEnabled(parent),"dialog creation failure restores host and propagates after worker cleanup");
    PostQuitMessage(39);
    check(!SettingsThread::run(GetModuleHandle(nullptr),parent,state,{},HWND_MESSAGE,delayed_settings_launch),"quit before settings queue creation eventually cancels dialog");
    check(PeekMessage(&message,nullptr,WM_QUIT,WM_QUIT,PM_REMOVE) && message.wParam==39,"early quit survives delayed worker startup");
    failed=false;
    try{SettingsThread::run(GetModuleHandle(nullptr),parent,state,{},nullptr,failed_settings_launch);}catch(const std::exception&){failed=true;}
    check(failed && IsWindowEnabled(parent) && !GetProp(parent,csp::MonitorEvents::identity_property),"thread launch failure restores host and removes marker");
    DestroyWindow(parent);
}

// Real threaded dialogs and host HWND lifetimes; injectable foreground APIs keep
// these regression tests from stealing focus from applications on the desktop.
struct FakeSettingsFocus : csp::SettingsFocusApi {
    HWND root=nullptr,previous=nullptr,other=nullptr,dialog=nullptr;
    mutable HWND current=nullptr,restored=nullptr;
    DWORD host_thread=GetCurrentThreadId();
    mutable unsigned activation_calls=0,focus_calls=0;
    mutable bool wrong_thread=false,enabled_at_activation=false,dialog_alive_at_activation=false;
    bool accept=true,switch_on_enable=false,switch_after_activation=false;
    void check_thread() const noexcept {if(GetCurrentThreadId()!=host_thread)wrong_thread=true;}
    HWND foreground() const noexcept override {check_thread();return current;}
    HWND keyboard_focus() const noexcept override {check_thread();return previous;}
    bool activate(HWND w) const noexcept override {
        check_thread();++activation_calls;enabled_at_activation=IsWindowEnabled(w)!=FALSE;
        dialog_alive_at_activation=IsWindow(dialog)!=FALSE;
        if(accept)current=switch_after_activation?other:w;return accept;
    }
    void focus(HWND w) const noexcept override {check_thread();++focus_calls;restored=w;}
    static LRESULT CALLBACK owner_proc(HWND w,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR data){
        auto* self=reinterpret_cast<FakeSettingsFocus*>(data);
        if(msg==WM_NCDESTROY)RemoveWindowSubclass(w,owner_proc,id);
        if(msg==WM_ENABLE && wp && self->switch_on_enable)self->current=self->other;
        return DefSubclassProc(w,msg,wp,lp);
    }
};
struct FocusRun {
    FakeSettingsFocus api;std::atomic<HWND> dialog{nullptr};
    unsigned command=IDCANCEL,mode=0,attempts=0;bool visited=false;
};
FocusRun* focus_run=nullptr; // Host thread only.
void CALLBACK close_focus_dialog(HWND,UINT,UINT_PTR timer,DWORD){
    auto& run=*focus_run;HWND dialog=run.dialog.load();
    if(!dialog || !IsWindowVisible(dialog)){
        if(++run.attempts>400){KillTimer(nullptr,timer);PostQuitMessage(1);}return;
    }
    KillTimer(nullptr,timer);run.visited=true;
    run.api.dialog=dialog;run.api.current=run.mode==1?run.api.other:dialog;
    if(run.mode==2)DestroyWindow(run.api.root);
    if(run.mode==3)DestroyWindow(run.api.previous);
    if(run.mode==6)ShowWindow(run.api.root,SW_HIDE);
    if(run.mode==7)SetProp(run.api.root,csp::SettingsModalOwner::owner_property,reinterpret_cast<HANDLE>(99));
    if(run.command==SC_CLOSE)PostMessage(dialog,WM_SYSCOMMAND,SC_CLOSE,0);
    else PostMessage(dialog,WM_COMMAND,run.command,0);
}
void settings_focus_case(unsigned command,unsigned mode){
    FocusRun run;run.command=command;run.mode=mode;auto& api=run.api;
    api.root=parent_window();api.other=parent_window();api.current=api.root;
    api.previous=CreateWindowEx(0,L"EDIT",L"",WS_CHILD|WS_VISIBLE,0,0,50,20,api.root,nullptr,GetModuleHandle(nullptr),nullptr);
    check(api.previous!=nullptr,"focus restoration child created");
    api.accept=mode!=5;api.switch_on_enable=mode==4;api.switch_after_activation=mode==8;
    check(SetWindowSubclass(api.root,FakeSettingsFocus::owner_proc,1,reinterpret_cast<DWORD_PTR>(&api))!=FALSE,"focus race observer installed");
    bool applied=false;DialogState state{csp::Settings{},[&](const csp::Settings& next){
        check(GetCurrentThreadId()==api.host_thread && next.range==137,"focus close preserves main-thread custom setting Apply");applied=true;
    }};
    focus_run=&run;auto timer=SetTimer(nullptr,0,10,close_focus_dialog);check(timer!=0,"focus test close scheduled");
    bool changed=false,notification_failed=false;
    try {
        changed=SettingsThread::run(GetModuleHandle(nullptr),api.root,state,[&](HWND w){
            if(mode==9){
                auto& dialog=*reinterpret_cast<DialogState*>(GetWindowLongPtr(w,DWLP_USER));
                dialog.closing=[&](HWND){notification_failed=true;throw std::runtime_error("Injected focus notification failure");};
            }
            SetDlgItemInt(w,IDC_RANGE,137,FALSE);run.dialog=w;
        },nullptr,&_beginthreadex,&api);
    }catch(...){KillTimer(nullptr,timer);focus_run=nullptr;DestroyWindow(api.root);DestroyWindow(api.other);throw;}
    KillTimer(nullptr,timer);focus_run=nullptr;
    check(notification_failed==(mode==9) && !IsWindow(run.dialog.load()),"settings dialog finished after closing notification");
    check(run.visited && !api.wrong_thread,"close path and focus APIs execute on expected threads");
    check(changed==(command==IDOK) && applied==(command==IDOK),"OK applies, Cancel and X discard drafts");
    const bool restore=mode==0 || mode==3;
    check(api.activation_calls==(restore || mode==5 || mode==8?1u:0u),"foreground restored only when closing dialog still owns it");
    check(api.focus_calls==(restore?1u:0u),"keyboard focus untouched after external switch or activation failure");
    if(restore){
        check(api.enabled_at_activation && api.dialog_alive_at_activation,"host enabled and activated before settings dialog is destroyed");
        check(api.current==api.root && api.restored==(mode==3?api.root:api.previous),"previous valid child focus restored with safe root fallback");
    }
    if(mode==1 || mode==4 || mode==8)check(api.current==api.other,"other application keeps foreground on close");
    if(mode!=2 && mode!=7)check(IsWindowEnabled(api.root)!=FALSE,"close re-enables original host");
    if(mode==7){
        check(!IsWindowEnabled(api.root) && GetProp(api.root,csp::SettingsModalOwner::owner_property)==reinterpret_cast<HANDLE>(99),"replaced host identity is neither activated nor modified by cleanup");
        RemoveProp(api.root,csp::SettingsModalOwner::owner_property);EnableWindow(api.root,TRUE);
    }else check(!GetProp(api.root,csp::SettingsModalOwner::owner_property),"modal owner lifetime marker removed");
    check(!GetProp(api.previous,csp::SettingsModalOwner::focus_property),"previous focus lifetime marker removed");
    DestroyWindow(api.root);DestroyWindow(api.other);
}
void settings_focus_tests(){
    for(unsigned command:{unsigned(IDOK),unsigned(IDCANCEL),unsigned(SC_CLOSE)})
        for(unsigned mode=0;mode<3;++mode)settings_focus_case(command,mode);
    for(unsigned mode=3;mode<=8;++mode)settings_focus_case(IDOK,mode);
    for(unsigned command:{unsigned(IDOK),unsigned(IDCANCEL),unsigned(SC_CLOSE)})settings_focus_case(command,9);
}

// A successful enumeration with zero displays models removal of the selected output.
BOOL WINAPI no_displays(HDC,LPCRECT,MONITORENUMPROC,LPARAM){return TRUE;}
void fullscreen_lifecycle(){
    HWND parent=parent_window();
    SpectrumWindow view(no_displays);
    HWND panel=view.create_window(parent,0,0,320,150);
    ShowWindow(panel,SW_SHOWNOACTIVATE);
    check(wait_reads(Stream::reads),"fullscreen fixture playing");
    HMENU menu=CreatePopupMenu();check(menu!=nullptr,"fullscreen menu fixture created");
    view.build_menu(menu,1000);check(GetMenuState(menu,1002,MF_BYCOMMAND)!=UINT(-1),"fullscreen command uses host menu offset");DestroyMenu(menu);
    view.editing=[]{return true;};
    menu=CreatePopupMenu();view.build_menu(menu,1000);check(GetMenuState(menu,1002,MF_BYCOMMAND)==UINT(-1),"layout edit menu leaves fullscreen out");DestroyMenu(menu);
    SendMessage(panel,WM_LBUTTONDBLCLK,0,0);view.menu_command(2);
    check(!view.fullscreen_window(),"layout editing suppresses fullscreen entry");
    view.editing=[]{return false;};
    const auto streams=Stream::created;
    for(int pass=0;pass<6;++pass){
        if(pass%2)view.menu_command(2);else SendMessage(panel,WM_LBUTTONDBLCLK,0,0);
        HWND screen=view.fullscreen_window();check(screen && view.get_wnd()==panel && GetParent(panel)==parent,"host panel retained while fullscreen");
        RECT rect{};GetWindowRect(screen,&rect);MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfo(MonitorFromWindow(panel,MONITOR_DEFAULTTONEAREST),&monitor);
        check(EqualRect(&rect,&monitor.rcMonitor),"fullscreen covers panel monitor including taskbar");
        check(wait_reads(Stream::reads) && Stream::created==streams,"fullscreen advances existing stream");
        if(pass%3==0)SendMessage(screen,WM_KEYDOWN,VK_ESCAPE,0);
        else if(pass%3==1)SendMessage(screen,WM_LBUTTONDBLCLK,0,0);
        else SendMessage(screen,WM_DISPLAYCHANGE,0,0);
        check(!IsWindow(screen) && wait_reads(Stream::reads) && Stream::created==streams,"exit or disconnected output restores existing stream");
    }
    view.menu_command(2);HWND screen=view.fullscreen_window();view.destroy_window();
    check(screen && !IsWindow(screen) && !view.get_wnd() && Stream::live==0,"panel destruction removes fullscreen and stream");
    DestroyWindow(parent);
}
enum class MonitorResult { enumeration_error, information_error, present, removed };
MonitorResult monitor_result;
unsigned monitor_calls;
BOOL WINAPI changing_displays(HDC dc,LPCRECT clip,MONITORENUMPROC visit,LPARAM data){
    ++monitor_calls;
    switch(monitor_result){
    case MonitorResult::enumeration_error:return FALSE;
    case MonitorResult::information_error:visit(nullptr,nullptr,nullptr,data);return TRUE;
    case MonitorResult::present:return EnumDisplayMonitors(dc,clip,visit,data);
    default:return TRUE;
    }
}
bool registered(const wchar_t* name){
    WNDCLASSEX info{sizeof(info)};
    return GetClassInfoEx(GetModuleHandle(nullptr),name,&info)!=FALSE;
}
csp::MonitorRefresh fixture_rate(HWND window,double hz,bool precise){
    MONITORINFOEX info{};info.cbSize=sizeof(info);
    check(GetMonitorInfo(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&info)!=FALSE,"fixture monitor has a device name");
    csp::MonitorRefresh rate;wcscpy_s(rate.device,info.szDevice);
    rate.hz=hz;rate.precise=precise;return rate;
}
void fullscreen_registration_and_retry(){
    HWND parent=parent_window();
    // Isolate topology retries from an integer-only driver on the test machine.
    const auto precise_rate=[](HWND window){return fixture_rate(window,60,true);};
    SpectrumWindow first(changing_displays,precise_rate),second;
    auto open=[&](SpectrumWindow& view){
        HWND panel=view.create_window(parent,0,0,320,150);
        check(panel!=nullptr,"panel class can register again");
        ShowWindow(panel,SW_SHOWNOACTIVATE);view.menu_command(2);
        HWND popup=view.fullscreen_window();check(popup!=nullptr,"fullscreen opened");return popup;
    };
    HWND a=open(first),b=open(second);
    check(a!=b && registered(window_class) && registered(fullscreen_class),"two instances share registered classes");
    check(first.fullscreen_window()==a && second.fullscreen_window()==b,"each instance retains its own popup under a shared root");
    SendMessage(first.fullscreen_window(),WM_KEYDOWN,VK_ESCAPE,0);
    check(!IsWindow(a) && !first.fullscreen_window() && second.fullscreen_window()==b && IsWindow(b),"closing the first fullscreen window leaves the second open");
    first.menu_command(2);a=first.fullscreen_window();
    check(a && a!=b && second.fullscreen_window()==b,"reopening first instance does not replace the second popup");
    second.destroy_window();
    check(!IsWindow(b) && !second.fullscreen_window() && first.fullscreen_window()==a,"destroying second panel clears only its own popup");
    check(IsWindow(a) && registered(window_class) && registered(fullscreen_class),"destroying one instance preserves both live classes");
    for(auto failure:{MonitorResult::enumeration_error,MonitorResult::information_error}){
        monitor_result=failure;monitor_calls=0;SendMessage(a,WM_DISPLAYCHANGE,0,0);
        check(IsWindow(a) && monitor_calls==1,"failed or incomplete display query preserves fullscreen");
        monitor_result=MonitorResult::present;
        const auto limit=GetTickCount64()+2000;
        while(monitor_calls==1 && GetTickCount64()<limit)pump(10);
        check(IsWindow(a) && monitor_calls>=2,"timer retries and rediscovers original display");
        auto recovered=monitor_calls;pump(320);
        check(monitor_calls==recovered,"successful retry stops polling");
    }
    monitor_result=MonitorResult::enumeration_error;SendMessage(a,WM_DEVICECHANGE,0,0);
    monitor_result=MonitorResult::removed;
    const auto limit=GetTickCount64()+2000;
    while(IsWindow(a) && GetTickCount64()<limit)pump(10);
    check(!IsWindow(a) && !registered(fullscreen_class),"confirmed removal closes and unregisters last popup");
    first.menu_command(2);a=first.fullscreen_window();
    check(a!=nullptr,"fullscreen class registered again");
    monitor_result=MonitorResult::information_error;SendMessage(a,WM_DISPLAYCHANGE,0,0);
    DestroyWindow(parent);first.destroy_window();auto calls=monitor_calls;pump(320);
    check(!IsWindow(a) && monitor_calls==calls && !registered(window_class) && !registered(fullscreen_class),"external parent destruction cancels retry and releases both classes");
}
// Exercise both native handlers before the SDK caches the language setting.
void menu_build_failure(HostApi& api){
    HWND parent=parent_window();SpectrumWindow view;
    HWND panel=view.create_window(parent,0,0,320,150);ShowWindow(panel,SW_SHOWNOACTIVATE);
    api.store->fail_integer_read=true;
    auto check_menu=[&](HWND owner){
        auto open=[&]{SendMessage(owner,WM_CONTEXTMENU,reinterpret_cast<WPARAM>(owner),MAKELPARAM(-1,-1));};
        open(); // Warm up first-use SDK logging before taking the baseline.
        const DWORD handles=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
        const unsigned failures=api.store->failed_reads;
        for(int i=0;i<12;++i)open();
        check(api.store->failed_reads==failures+12,"each menu build threw while reading the language");
        check(handles && GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS)==handles,"failed native menu builds release all USER handles");
    };
    check_menu(panel);
    view.menu_command(2);
    HWND popup=view.fullscreen_window();check(popup!=nullptr,"fullscreen menu failure fixture opened");
    check_menu(popup);
    api.store->fail_integer_read=false;
    view.destroy_window();DestroyWindow(parent);
}
double simulated_hz=0;
bool simulated_precise=true;
unsigned rate_queries=0;
// Declare after the backing arrays so unwinding clears the callback's view first.
template<typename T>
struct ClearTestSequence {
    std::span<const T>& sequence;
    ~ClearTestSequence(){sequence={};}
};
std::span<const double> rate_sequence;
csp::MonitorRefresh simulated_rate(HWND window){
    const auto index=rate_queries++;
    const double hz=rate_sequence.empty()?simulated_hz:rate_sequence[std::min<size_t>(index,rate_sequence.size()-1)];
    return fixture_rate(window,hz,simulated_precise && hz>0);
}
void fullscreen_topology_retry_limit(){
    HWND parent=parent_window();
    SpectrumWindow view(changing_displays,simulated_rate);
    csp::Settings settings;settings.follow_monitor=true;view.apply_settings(settings);
    HWND panel=view.create_window(parent,0,0,320,150);ShowWindow(panel,SW_SHOWNOACTIVATE);
    monitor_result=MonitorResult::present;simulated_hz=144;simulated_precise=true;
    view.menu_command(2);
    HWND popup=view.fullscreen_window();
    check(popup && view.active_fps()==144,"topology retry fixture starts at 144 Hz");
    for(auto failure:{MonitorResult::enumeration_error,MonitorResult::information_error}){
        monitor_result=failure;monitor_calls=0;rate_queries=0;
        const double previous=view.active_fps();
        SendMessage(popup,WM_DISPLAYCHANGE,0,0);
        const auto deadline=GetTickCount64()+2500;
        while(monitor_calls<4 && GetTickCount64()<deadline)pump(10);
        check(monitor_calls==4,"failed topology query gets three timer retries");
        monitor_result=MonitorResult::present;
        simulated_hz=previous==144?165:144;
        pump(350);
        check(monitor_calls==4 && rate_queries==0 && view.fullscreen_window()==popup && view.active_fps()==previous,"exhausted topology retries preserve popup and rate until a display event");
        SendMessage(popup,WM_DEVICECHANGE,0,0);
        check(monitor_calls==5 && rate_queries==1 && view.active_fps()==simulated_hz,"fresh device event detects the recovered monitor");
    }
    view.destroy_window();DestroyWindow(parent);
}
struct TestParentDeleter {
    void operator()(HWND window) const noexcept {DestroyWindow(window);}
};
using TestParent=std::unique_ptr<std::remove_pointer_t<HWND>,TestParentDeleter>;

struct MenuSnapshot {
    struct Items {
        int count=-1;
        wchar_t text[6][100]{};
        void read(HMENU menu){
            count=GetMenuItemCount(menu);
            for(int i=0;i<count && i<6;++i)GetMenuStringW(menu,i,text[i],100,MF_BYPOSITION);
        }
    } top,styles;
    static LRESULT CALLBACK observe(HWND w,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR data){
        if(msg==WM_INITMENUPOPUP){
            auto& snapshot=*reinterpret_cast<MenuSnapshot*>(data);
            auto menu=reinterpret_cast<HMENU>(wp);
            snapshot.top.read(menu);
            if(auto sub=GetSubMenu(menu,3))snapshot.styles.read(sub);
            PostMessage(w,WM_CANCELMODE,0,0);
        }
        if(msg==WM_NCDESTROY)RemoveWindowSubclass(w,observe,id);
        return DefSubclassProc(w,msg,wp,lp);
    }
    static void CALLBACK timeout(HWND,UINT,UINT_PTR,DWORD){EndMenu();}
    void capture(HWND w){
        check(SetWindowSubclass(w,observe,1,reinterpret_cast<DWORD_PTR>(this))!=FALSE,"menu observer installed");
        auto timer=SetTimer(nullptr,0,1500,timeout);
        if(timer)SendMessage(w,WM_CONTEXTMENU,reinterpret_cast<WPARAM>(w),MAKELPARAM(-1,-1));
        if(timer)KillTimer(nullptr,timer);
        RemoveWindowSubclass(w,observe,1);
        check(timer && top.count>=0,"captured native tracking menu");
    }
};
void menu_language_tests(){
    const wchar_t* expected[][11]={
        {L"Configure...",L"Fullscreen",L"Show FPS",L"Color Mode",L"Simple",L"Horizontal",L"Vertical",L"Simple-antialiasing",L"Horizontal-antialiasing",L"Simple-line",L"Exit fullscreen"},
        {L"设置…",L"全屏",L"显示 FPS",L"颜色模式",L"简单",L"横向渐变",L"纵向渐变",L"简单抗锯齿",L"横向渐变抗锯齿",L"简单线条",L"退出全屏"},
        {L"設定…",L"全画面",L"FPS を表示",L"カラーモード",L"シンプル",L"横グラデーション",L"縦グラデーション",L"シンプル（AA）",L"横グラデーション（AA）",L"折れ線",L"全画面を終了"}
    };
    struct RestoreLanguage {
        unsigned original=unsigned(interface_language.get_value());
        ~RestoreLanguage(){interface_language=original;}
    } restore;
    TestParent parent{parent_window()};SpectrumWindow view;
    HWND panel=view.create_window(parent.get(),0,0,150,100);ShowWindow(panel,SW_SHOWNOACTIVATE);
    for(unsigned language:{1u,2u,0u}){
        interface_language=language;
        MenuSnapshot normal;normal.capture(panel);
        check(normal.top.count==4 && normal.styles.count==6,"complete normal and style menu sizes");
        for(int i=0;i<4;++i)check(wcscmp(normal.top.text[i],expected[language][i])==0,"native panel caption in chosen language");
        for(int i=0;i<6;++i)check(wcscmp(normal.styles.text[i],expected[language][4+i])==0,"native style caption in chosen language");
        view.menu_command(2);HWND popup=view.fullscreen_window();check(popup!=nullptr,"fullscreen language fixture opens");
        MenuSnapshot fullscreen;fullscreen.capture(popup);
        check(fullscreen.top.count==2 && wcscmp(fullscreen.top.text[0],expected[language][10])==0 &&
            wcscmp(fullscreen.top.text[1],expected[language][2])==0,"complete localized fullscreen menu");
        SendMessage(popup,WM_KEYDOWN,VK_ESCAPE,0);
    }
}

void verify_fps_pixels(const csp::GdiSurface& surface,bool visible){
    GdiFlush();
    unsigned backing=0,glyph=0;
    for(int y=0;y<surface.height;++y)for(int x=0;x<surface.width;++x){
        auto rgb=surface.pixels[y*surface.width+x]&0xffffff;
        if(!rgb)continue;
        check(visible && x>=surface.width-csp::fps_box_width && x<surface.width-2 &&
            y>=2 && y<2+csp::fps_box_height,"unexpected pixels outside FPS overlay");
        if(rgb==0x202020)++backing;
        if((rgb&255)>160 && ((rgb>>8)&255)>160 && (rgb>>16)>160)++glyph;
    }
    if(visible)check(backing>50 && glyph>10,"FPS backing and bright text glyphs are both visible");
}
// Copy the offscreen panel's actual GDI source only after its normal presentation succeeds.
class CapturePanel final : public SpectrumWindow {
public:
    using SpectrumWindow::SpectrumWindow;
    csp::GdiSurface gdi_pixels;
    unsigned gdi_captures=0;
private:
    bool present_gdi(HDC target,HDC source,int width,int height) noexcept override {
        const bool copied=SpectrumWindow::present_gdi(target,source,width,height);
        if(copied && width==gdi_pixels.width && height==gdi_pixels.height &&
            BitBlt(gdi_pixels.dc,0,0,width,height,source,0,0,SRCCOPY)){
            GdiFlush();
            ++gdi_captures;
        }
        return copied;
    }
};
void compare_frame(const csp::GdiSurface& actual,const csp::GdiSurface& expected,const char* message){
    check(actual.width==expected.width && actual.height==expected.height,"matching capture dimensions");
    check(std::equal(actual.pixels,actual.pixels+actual.width*actual.height,expected.pixels,
        [](uint32_t a,uint32_t b){return ((a^b)&0xffffff)==0;}),message);
}
void fps_overlay_tests(HostApi& api){
    csp::GdiSurface pixels;
    csp::RenderedFps meter;
    for(unsigned ms=0;ms<=500;ms+=10)meter.presented(ms);
    check(wcscmp(meter.label(),L"100.0 FPS")==0,"measured FPS uses successful frame intervals");
    meter.reset();check(!meter.frames() && wcscmp(meter.label(),L"\u2014 FPS")==0,"reset clears measured FPS");

    struct RenderOps final:csp::D2DTargetOperations {
        mutable unsigned creates=0,ends=0,formats=0;
        HRESULT format_error=E_OUTOFMEMORY;
        bool fail_end=false;
        ULONGLONG now_ms=10000;
        csp::GdiSurface* capture=nullptr;
        mutable bool captured=false;
        mutable csp::ComPtr<ID2D1HwndRenderTarget> probe;
        ULONGLONG overlay_time_ms() const noexcept override {return now_ms;}
        HRESULT create_fps_format(IDWriteTextFormat** out) const noexcept override {
            ++formats;
            return FAILED(format_error)?format_error:csp::D2DTargetOperations::create_fps_format(out);
        }
        HRESULT create(ID2D1Factory* f,HWND w,int width,int height,ID2D1HwndRenderTarget** out) const noexcept override {
            ++creates;
            auto hr=f->CreateHwndRenderTarget(D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE),96,96,D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE),
                D2D1::HwndRenderTargetProperties(w,D2D1::SizeU(width,height)),out);
            if(SUCCEEDED(hr))probe=*out;
            return hr;
        }
        HRESULT end_draw(ID2D1HwndRenderTarget* t) const noexcept override {
            if(capture){
                captured=false;
                csp::ComPtr<ID2D1GdiInteropRenderTarget> interop;
                HDC dc=nullptr;
                if(SUCCEEDED(t->QueryInterface(IID_PPV_ARGS(interop.GetAddressOf()))) &&
                    SUCCEEDED(interop->GetDC(D2D1_DC_INITIALIZE_MODE_COPY,&dc))){
                    captured=BitBlt(capture->dc,0,0,capture->width,capture->height,dc,0,0,SRCCOPY)!=FALSE;
                    GdiFlush();
                    RECT unchanged{};
                    if(FAILED(interop->ReleaseDC(&unchanged)))captured=false;
                }
            }
            ++ends;auto hr=t->EndDraw();return fail_end?D2DERR_RECREATE_TARGET:hr;
        }
    } ops;
    TestParent parent{parent_window()};
    CapturePanel first(&EnumDisplayMonitors,csp::query_monitor_refresh,&SetWindowPos,csp::query_monitor_refresh,ops);
    SpectrumWindow second;
    csp::Settings s;s.direct2d=true;s.scanline_sync=false;s.style=csp::Style::Simple;
    // Stream's front-left bin 1 is 0.1, all remaining front-left bins are 0.001.
    // With a linear axis and the legacy gain this produces one four-pixel bar at x=0.
    s.selected=0;s.channel_mode=2;s.direction=1;s.linear_frequency=true;s.show_peaks=false;
    s.foreground=0x32ac56;
    first.apply_settings(s);s.direct2d=false;second.apply_settings(s);
    HWND a=first.create_window(parent.get(),0,0,150,100),b=second.create_window(parent.get(),150,0,150,100);
    ShowWindow(a,SW_SHOWNOACTIVATE);ShowWindow(b,SW_SHOWNOACTIVATE);pump(100);
    auto checked=[](SpectrumWindow& view){
        OwnedMenu menu{CreatePopupMenu()};view.build_menu(menu.get(),1000);
        auto state=GetMenuState(menu.get(),1003,MF_BYCOMMAND);
        check(state!=UINT(-1),"FPS command is present");return (state&MF_CHECKED)!=0;
    };
    auto paint=[](HWND w){InvalidateRect(w,nullptr,FALSE);UpdateWindow(w);};
    check(!checked(first) && !checked(second),"FPS starts disabled per panel");
    first.menu_command(3);
    SendMessage(a,WM_TIMER,1,0);
    auto before=ops.ends;
    const auto creates=ops.creates;
    paint(a);paint(a);
    check(ops.ends==before+2 && ops.formats==1 && first.rendered_frames()==2,
        "text initialization failure leaves spectrum on D2D without allocating on every frame");
    first.menu_command(3);paint(a);
    check(ops.ends==before+3 && ops.creates==creates,"disabling failed overlay immediately reuses healthy D2D target");
    first.menu_command(3);paint(a);
    check(ops.formats==2,"re-enabling the overlay immediately retries optional text");
    ops.format_error=S_OK;
    const auto ends_at_failure=ops.ends;
    RECT client{};GetClientRect(a,&client);
    pixels.resize(client.right,client.bottom);
    csp::GdiSurface reference;reference.resize(client.right,client.bottom);
    RenderOps healthy;healthy.format_error=S_OK;healthy.capture=&reference;
    csp::D2DRenderer reference_renderer(healthy);
    auto reference_settings=s;reference_settings.direct2d=true;
    auto reference_frame=[&](const wchar_t* label){
        healthy.captured=false;
        check(reference_renderer.draw(b,client.right,client.bottom,{},{},reference_settings,label) &&
            healthy.captured,"healthy reference target captured");
        verify_fps_pixels(reference,label!=nullptr);
        for(int y=client.bottom-4;y<client.bottom;++y)reference.pixels[y*client.right]=s.foreground;
    };
    auto check_bar=[&]{
        for(int y=client.bottom-5;y<client.bottom;++y){
            check((pixels.pixels[y*client.right]&0xffffff)==(y==client.bottom-5?0:s.foreground) &&
                (pixels.pixels[y*client.right+1]&0xffffff)==0,"exact four-pixel spectrum bar at x=0");
        }
    };
    ops.capture=&pixels;
    ops.now_ms+=999;paint(a);
    check(ops.captured,"captured 999 ms frame");check_bar();reference_frame(nullptr);
    compare_frame(pixels,reference,"999 ms shows the reference spectrum without text");ops.captured=false;
    check(ops.formats==2 && ops.ends==ends_at_failure+1 && first.rendered_frames()==2,"999 ms: D2D presents the spectrum without retrying text");
    const std::wstring retry_label=first.fps_label();
    ++ops.now_ms;paint(a);
    check(ops.captured,"captured first 1000 ms frame");check_bar();reference_frame(retry_label.c_str());
    compare_frame(pixels,reference,"first 1000 ms draw matches every healthy label and spectrum pixel");
    ops.capture=nullptr;
    check(ops.formats==3 && ops.ends==ends_at_failure+2 && first.rendered_frames()==3 && ops.creates==creates && checked(first),
        "1000 ms: first frame retries text and presents on the original D2D target");
    first.menu_command(3);first.menu_command(3);second.menu_command(3);
    check(checked(first) && checked(second),"FPS menu shows enabled state");
    before=ops.ends;
    paint(a);check(ops.formats==3 && ops.ends==before+1 && first.rendered_frames()==1,"D2D overlay counts one completed draw");
    ops.fail_end=true;paint(a);
    check(ops.ends==before+2 && first.rendered_frames()==2,"failed EndDraw followed by GDI presentation counts once");
    check(second.rendered_frames()==0,"other panel does not count first panel presentations");
    paint(b);check(second.rendered_frames()==1 && first.rendered_frames()==2,"GDI panel has independent counter");
    ops.fail_end=false;paint(a);
    check(ops.ends==before+2 && first.rendered_frames()==3,"real device loss keeps subsequent frames on the GDI fallback");
    {
        RenderOps removed;removed.format_error=D2DERR_RECREATE_TARGET;
        SpectrumWindow loss(&EnumDisplayMonitors,csp::query_monitor_refresh,&SetWindowPos,csp::query_monitor_refresh,removed);
        s.direct2d=true;loss.apply_settings(s);
        HWND window=loss.create_window(parent.get(),0,0,150,100);ShowWindow(window,SW_SHOWNOACTIVATE);
        SendMessage(window,csp::MonitorEvents::visibility_message,0,0);
        paint(window);
        check(removed.ends>0 && removed.probe.Get(),"loss fixture starts on a working D2D target");
        const auto previous=removed.ends;
        loss.menu_command(3);paint(window);
        check(removed.formats==1,"text preparation receives the injected device loss");
        check(removed.ends==previous,"lost target is discarded before BeginDraw");
        check(loss.rendered_frames()==1,
            "device loss preparing text uses successful GDI presentation on the same frame exactly once");
        check(removed.probe.Detach()->Release()==0,"text preparation device loss releases every renderer reference to the target");
        paint(window);
        check(removed.creates==1 && removed.ends==previous && removed.formats==1 && loss.rendered_frames()==2,
            "following frame stays on GDI without reusing or recreating the lost target");
    }
    {
        RenderOps sizing;sizing.format_error=S_OK;sizing.capture=&pixels;
        csp::D2DRenderer renderer(sizing);
        csp::GdiFpsOverlay gdi;
        s.direct2d=true;
        for(SIZE dim:{SIZE{95,100},SIZE{150,26},SIZE{96,27}}){
            const bool visible=dim.cx==96;
            pixels.resize(dim.cx,dim.cy);
            std::fill_n(pixels.pixels,dim.cx*dim.cy,0);
            gdi.draw(pixels.dc,dim.cx,dim.cy,L"200.0 FPS");verify_fps_pixels(pixels,visible);
            sizing.captured=false;
            check(renderer.draw(a,dim.cx,dim.cy,{}, {},s,L"200.0 FPS") && sizing.captured,"captured D2D size boundary");
            verify_fps_pixels(pixels,visible);
            check(sizing.formats==unsigned(visible),"hidden overlay skips text setup");
        }
    }
    auto pending=[&]{return first.fps_label() && wcscmp(first.fps_label(),L"\u2014 FPS")==0;};
    auto wait_for_sample=[&]{
        const auto deadline=GetTickCount64()+4000;
        while(pending() && GetTickCount64()<deadline)pump(10);
        check(first.fps_label() && wcstod(first.fps_label(),nullptr)>0,"enabled overlay obtains a positive measured rate");
    };
    wait_for_sample();
    PaintObserver paused_paint(a);
    api.playback->paused=true;for(auto* callback:api.callbacks->clients)callback->on_playback_pause(true);
    pump(150);
    check(checked(first) && pending() && !first.rendered_frames() && paused_paint.completed>0,
        "pause repaints the dash label without toggling FPS");
    const auto paints=paused_paint.completed,timers=paused_paint.timers;
    const auto reads=Stream::reads;
    pump(650);
    check(paused_paint.completed==paints && paused_paint.timers==timers && Stream::reads==reads && !first.rendered_frames(),
        "paused FPS overlay causes no periodic paints, timers or audio reads");
    // Exercise WM_SIZE on the same paused, checked HWND. UpdateWindow services only
    // the invalidation produced by the resize; no synthetic invalidation or timer tick.
    for(bool direct:{true,false}){
        s.direct2d=direct;s.foreground=0;
        first.apply_settings(s);
        PaintObserver observed(a);
        csp::GdiSurface before_shrink;
        for(SIZE size:{SIZE{150,100},SIZE{95,100},SIZE{150,100},SIZE{150,26},SIZE{150,100}}){
            pixels.resize(size.cx,size.cy);
            first.gdi_pixels.resize(size.cx,size.cy);
            ops.capture=direct?&pixels:nullptr;ops.captured=false;
            auto painted=observed.completed;
            auto d2d_count=ops.ends,gdi_count=first.gdi_captures;
            check(SetWindowPos(a,nullptr,0,0,size.cx,size.cy,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE,"resize paused panel");
            RECT dirty{},bounds{};
            GetClientRect(a,&bounds);
            check(GetUpdateRect(a,&dirty,FALSE) && EqualRect(&dirty,&bounds),"resize invalidated whole client");
            UpdateWindow(a);
            check(observed.completed==painted+1 && checked(first) && pending() && !first.rendered_frames(),
                "first resize repaint while FPS remains checked and paused");
            check(direct ? ops.captured && ops.ends==d2d_count+1 && first.gdi_captures==gdi_count :
                ops.ends==d2d_count && first.gdi_captures==gdi_count+1,"first repaint used requested backend");
            const auto& frame=direct?pixels:first.gdi_pixels;
            const bool visible=size.cx==150 && size.cy==100;
            verify_fps_pixels(frame,visible);
            if(visible){
                if(before_shrink.pixels)compare_frame(frame,before_shrink,"restored first frame exactly matches the original paused frame");
                else {
                    before_shrink.resize(size.cx,size.cy);
                    std::copy_n(frame.pixels,size.cx*size.cy,before_shrink.pixels);
                }
            }
        }
        ops.capture=nullptr;
    }
    api.playback->paused=false;for(auto* callback:api.callbacks->clients)callback->on_playback_pause(false);
    check(checked(first) && pending(),"resume starts with an empty sampling interval");
    wait_for_sample();
    first.menu_command(2);HWND popup=first.fullscreen_window();
    check(popup && checked(first) && pending() && first.rendered_frames()<=1,"fullscreen starts a fresh enabled sample");
    wait_for_sample();
    SendMessage(popup,WM_KEYDOWN,VK_ESCAPE,0);
    check(!first.fullscreen_window() && checked(first) && pending() && first.rendered_frames()<=1,
        "returning to the embedded panel starts a fresh enabled sample");
    wait_for_sample();
}
struct FullscreenMessages {
    unsigned retries=0,extra_display=0,extra_device=0;
    UINT scripted_change=0;
    static LRESULT CALLBACK observe(HWND window,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR data){
        auto& messages=*reinterpret_cast<FullscreenMessages*>(data);
        if(msg==WM_TIMER && wp==4)++messages.retries;
        if(msg==WM_DISPLAYCHANGE || msg==WM_DEVICECHANGE){
            if(msg!=messages.scripted_change){
                if(msg==WM_DISPLAYCHANGE)++messages.extra_display;
                else ++messages.extra_device;
                return 0;
            }
            // Consume the one-shot expectation before forwarding the message.
            messages.scripted_change=0;
        }
        if(msg==WM_NCDESTROY)RemoveWindowSubclass(window,observe,id);
        return DefSubclassProc(window,msg,wp,lp);
    }
    void attach(HWND window){
        check(SetWindowSubclass(window,observe,1,reinterpret_cast<DWORD_PTR>(this))!=FALSE,"fullscreen message observer installed");
    }
    void send_change(HWND window,UINT msg){
        scripted_change=msg;
        SendMessage(window,msg,0,0); // Synchronous, on the window's owning thread.
        check(scripted_change==0,"scripted display event reached the observed popup");
    }
};

void test_sequence_unwinding(){
    std::span<const MonitorResult> topology;
    HWND parent=nullptr,panel=nullptr,popup=nullptr;
    bool injecting=false;
    try{
        TestParent owner{parent_window()};parent=owner.get();
        FullscreenMessages messages;
        SpectrumWindow view(changing_displays,simulated_rate);
        panel=view.create_window(parent,0,0,320,150);ShowWindow(panel,SW_SHOWNOACTIVATE);
        monitor_result=MonitorResult::present;simulated_hz=0;simulated_precise=true;
        view.menu_command(2);popup=view.fullscreen_window();
        check(IsWindow(panel) && IsWindow(popup),"exception fixture owns live panel and popup");
        messages.attach(popup);
        const std::array displays{MonitorResult::present};
        const std::array rates{144.0};
        ClearTestSequence clear_topology{topology};
        ClearTestSequence clear_rates{rate_sequence};
        topology=displays;rate_sequence=rates;
        injecting=true;
        check(false,"injected sequence assertion failure");
    }catch(const std::runtime_error&){
        if(!injecting)throw;
        check(!IsWindow(parent) && !IsWindow(panel) && !IsWindow(popup),"assertion unwinding destroys all three native windows");
        check(!registered(window_class) && !registered(fullscreen_class),"assertion unwinding releases both window classes");
        check(topology.empty() && rate_sequence.empty(),"assertion unwinding clears both test sequences");
        return;
    }
    check(false,"injected assertion must throw");
}
void fullscreen_alternating_failures(bool exhaust_topology){
    // Each entry is one enumeration, beginning with the display event.
    static std::span<const MonitorResult> topology;
    const auto enumerate=[](HDC dc,LPCRECT clip,MONITORENUMPROC visit,LPARAM data)->BOOL {
        if(!topology.empty())
            monitor_result=topology[std::min<size_t>(monitor_calls,topology.size()-1)];
        return changing_displays(dc,clip,visit,data);
    };
    TestParent parent{parent_window()};
    FullscreenMessages messages;
    SpectrumWindow view(enumerate,simulated_rate);
    csp::Settings settings;settings.follow_monitor=true;view.apply_settings(settings);
    HWND panel=view.create_window(parent.get(),0,0,320,150);ShowWindow(panel,SW_SHOWNOACTIVATE);
    monitor_result=MonitorResult::present;simulated_hz=165;simulated_precise=true;
    view.menu_command(2);
    HWND popup=view.fullscreen_window();
    check(popup && view.active_fps()==165,"mixed retry fixture starts with precise rate");
    messages.attach(popup);
    const auto ok=MonitorResult::present, fail=MonitorResult::enumeration_error;
    const auto first=exhaust_topology?fail:ok, second=exhaust_topology?ok:fail;
    const std::array mixed{second,first,second,first,second,first};
    // The new event and its first two retries fail on the exhausted path.
    const std::array recovery{first,first,first,ok};
    const std::array<double,4> rates{0,0,0,144};
    ClearTestSequence clear_topology{topology};
    ClearTestSequence clear_rates{rate_sequence};
    topology=mixed;monitor_calls=rate_queries=0;simulated_hz=0;
    messages.send_change(popup,WM_DISPLAYCHANGE);
    const auto deadline=GetTickCount64()+3000;
    while(messages.retries<5 && GetTickCount64()<deadline)pump(10);
    pump(550);
    check(messages.retries==5 && monitor_calls==6 && rate_queries==3 && view.fullscreen_window()==popup && view.active_fps()==165,
        "alternating topology errors and zero Hz stop after exactly five retries");

    topology=recovery;monitor_calls=rate_queries=0;simulated_hz=144;
    if(!exhaust_topology)rate_sequence=rates;
    messages.retries=0;
    messages.send_change(popup,WM_DEVICECHANGE);
    check(messages.retries==0 && monitor_calls==1 && view.active_fps()==165,"fresh event initially fails and keeps the last rate");
    const auto display_before=messages.extra_display,device_before=messages.extra_device;
    SendMessage(popup,WM_DISPLAYCHANGE,0,0);
    SendMessage(popup,WM_DEVICECHANGE,0,0);
    check(messages.extra_display>display_before && messages.extra_device>device_before && messages.retries==0 && monitor_calls==1,
        "unscripted display and device notifications cannot advance the active recovery round");
    const auto recovery_deadline=GetTickCount64()+2500;
    // Dispatch one message at a time so a slow scheduler cannot skip the
    // assertions between two retry messages.
    for(unsigned attempt=1;attempt<=3;++attempt){
        while(messages.retries<attempt && GetTickCount64()<recovery_deadline){
            MSG message{};
            if(PeekMessage(&message,nullptr,0,0,PM_REMOVE)){
                TranslateMessage(&message);DispatchMessage(&message);
            }else MsgWaitForMultipleObjects(0,nullptr,FALSE,10,QS_ALLINPUT);
        }
        check(messages.retries==attempt && monitor_calls==attempt+1 && view.active_fps()==(attempt==3?144:165),
            "only the third retry after a fresh event recovers 144 Hz");
    }
    pump(350);
    check(messages.retries==3 && monitor_calls==4 && rate_queries==(exhaust_topology?1u:4u) && view.fullscreen_window()==popup && view.active_fps()==144,
        "fresh retry budget recovers 144 Hz and stops polling");
}

void fullscreen_reopen_discards_candidate(){
    // The embedded panel seeds a fresh popup. Fix its rate so even a 75 Hz
    // test monitor cannot mask a candidate carried across window lifetimes.
    const auto panel_rate=[](HWND window){
        auto rate=fixture_rate(window,144,true);
        rate.monitor=MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST);
        return rate;
    };
    TestParent parent{parent_window()};
    FullscreenMessages messages;
    SpectrumWindow view(changing_displays,simulated_rate,&SetWindowPos,panel_rate);
    csp::Settings settings;settings.follow_monitor=true;view.apply_settings(settings);
    HWND panel=view.create_window(parent.get(),0,0,320,150);ShowWindow(panel,SW_SHOWNOACTIVATE);

    monitor_result=MonitorResult::present;simulated_hz=144;simulated_precise=true;
    view.menu_command(2);
    HWND old_popup=view.fullscreen_window();
    check(old_popup && view.active_fps()==144,"old popup starts with a precise 144 Hz rate");
    simulated_hz=75;simulated_precise=false;rate_queries=0;
    SendMessage(old_popup,WM_DISPLAYCHANGE,0,0);
    check(rate_queries==1 && view.active_fps()==144,"old popup defers its 75 Hz integer candidate");
    SendMessage(old_popup,WM_KEYDOWN,VK_ESCAPE,0);
    check(!IsWindow(old_popup) && !view.fullscreen_window() && view.get_wnd()==panel && rate_queries==1,"Esc closes old popup before any retry and preserves panel");

    check(!registered(fullscreen_class) && registered(window_class),"old popup class released while panel remains registered");
    simulated_hz=0;rate_queries=0;monitor_calls=0;
    view.menu_command(2);
    HWND reopened=view.fullscreen_window();
    check(reopened && view.get_wnd()==panel && rate_queries==1 && view.active_fps()==144,"same panel reopens with identified zero Hz and a 144 Hz baseline");
    messages.attach(reopened);
    // Only the popup's own timer can advance this round.
    const auto deadline=GetTickCount64()+2500;
    while(rate_queries<4 && GetTickCount64()<deadline)pump(10);
    check(messages.retries==3,"observed all three retry messages on reopened popup");
    pump(600);
    check(IsWindow(reopened) && messages.retries==3,"live popup receives no more retry messages after exhaustion");
    check(view.active_fps()==144,"reopened popup never settles the old 75 Hz candidate");
    check(view.fullscreen_window()==reopened && monitor_calls==3 && rate_queries==4,"reopened popup exhausts exactly three timer retries and stops");
    simulated_precise=true;
    view.destroy_window();
    check(!IsWindow(reopened) && !IsWindow(panel) && !view.fullscreen_window() && !view.get_wnd(),"reopened popup and panel destroyed");
    check(!registered(fullscreen_class) && !registered(window_class),"final teardown releases both window classes");
}
void fullscreen_rate_retry(){
    HWND parent=parent_window();SpectrumWindow view(changing_displays,simulated_rate);
    csp::Settings settings;settings.follow_monitor=true;view.apply_settings(settings);
    HWND panel=view.create_window(parent,0,0,320,150);ShowWindow(panel,SW_SHOWNOACTIVATE);
    monitor_result=MonitorResult::present;simulated_hz=144;view.menu_command(2);
    HWND popup=view.fullscreen_window();
    check(popup && view.active_fps()==144,"fullscreen follows initial display rate");
    simulated_hz=0;rate_queries=0;SendMessage(popup,WM_DISPLAYCHANGE,0,0);
    check(rate_queries==1 && view.active_fps()==144,"missing rate retains previous effective FPS");
    simulated_hz=165;
    auto deadline=GetTickCount64()+2000;
    while(rate_queries==1 && GetTickCount64()<deadline)pump(10);
    check(view.active_fps()==165 && rate_queries==2,"retry alone restores current fullscreen rate");
    pump(330);check(rate_queries==2,"successful rate recovery stops retry timer");

    simulated_hz=0;rate_queries=0;SendMessage(popup,WM_DISPLAYCHANGE,0,0);
    deadline=GetTickCount64()+2500;
    while(rate_queries<4 && GetTickCount64()<deadline)pump(10);
    pump(350);
    check(IsWindow(popup) && rate_queries==4 && view.active_fps()==165,"persistent failure stops after three retries and keeps last rate");
    SendMessage(popup,WM_DEVICECHANGE,0,0);simulated_hz=120;
    deadline=GetTickCount64()+2000;
    while(rate_queries==5 && GetTickCount64()<deadline)pump(10);
    check(rate_queries==6 && view.active_fps()==120,"new display event starts a new retry cycle");

    const double fractional=60000.0/1001;
    simulated_hz=fractional;SendMessage(popup,WM_DISPLAYCHANGE,0,0);
    simulated_precise=false;simulated_hz=60;rate_queries=0;
    SendMessage(popup,WM_DISPLAYCHANGE,0,0);
    check(rate_queries==1 && view.active_fps()==fractional,"integer fallback preserves precise 59.94 Hz");
    deadline=GetTickCount64()+2000;
    while(rate_queries<2 && GetTickCount64()<deadline)pump(10);
    check(rate_queries==2 && view.active_fps()==fractional,"integer retry still preserves fractional FPS");
    simulated_precise=true;simulated_hz=fractional;
    deadline=GetTickCount64()+2000;
    while(rate_queries<3 && GetTickCount64()<deadline)pump(10);
    pump(350);
    check(rate_queries==3 && view.active_fps()==fractional,"precise recovery stops retries without a new display event");

    simulated_precise=false;simulated_hz=75;rate_queries=0;
    SendMessage(popup,WM_DISPLAYCHANGE,0,0);
    deadline=GetTickCount64()+2500;
    while(rate_queries<4 && GetTickCount64()<deadline)pump(10);
    pump(350);
    check(rate_queries==4 && view.active_fps()==75,"integer-only driver settles at current mode after three retries");
    auto exhaust=[&](const std::array<double,4>& readings,double original,double expected){
        ClearTestSequence clear_rates{rate_sequence};
        simulated_precise=false;rate_queries=0;rate_sequence=readings;
        SendMessage(popup,WM_DISPLAYCHANGE,0,0);
        check(view.active_fps()==original,"retry window preserves previous precise rate");
        const auto limit=GetTickCount64()+2500;
        while(rate_queries<4 && GetTickCount64()<limit)pump(10);
        pump(350);
        check(rate_queries==4 && view.active_fps()==expected,"exhaustion uses last valid candidate from this cycle and stops");
    };
    simulated_precise=true;simulated_hz=fractional;SendMessage(popup,WM_DISPLAYCHANGE,0,0);
    exhaust({60,0,75,0},fractional,75);
    simulated_precise=true;simulated_hz=144;SendMessage(popup,WM_DISPLAYCHANGE,0,0);
    simulated_precise=false;simulated_hz=85;SendMessage(popup,WM_DEVICECHANGE,0,0);
    // A fresh event supersedes the pending round that saw 85 Hz.
    exhaust({0,0,0,0},144,144);
    simulated_precise=true;
    view.destroy_window();DestroyWindow(parent);
}
bool fail_position=false;
const wchar_t* fixture_device_override=nullptr;
unsigned position_calls=0;
BOOL WINAPI fixture_position(HWND popup,HWND after,int x,int y,int w,int h,UINT flags){
    ++position_calls;
    return !fail_position && SetWindowPos(popup,after,x,y,w,h,flags);
}
csp::MonitorRefresh multi_display_rate(HWND popup){
    auto rate=simulated_rate(popup);
    if(fixture_device_override)wcscpy_s(rate.device,fixture_device_override);
    return rate;
}
void fullscreen_display_identity(){
    HWND parent=parent_window();SpectrumWindow view(changing_displays,multi_display_rate,fixture_position);
    csp::Settings settings;settings.follow_monitor=true;view.apply_settings(settings);
    HWND panel=view.create_window(parent,0,0,320,150);ShowWindow(panel,SW_SHOWNOACTIVATE);
    monitor_result=MonitorResult::present;simulated_precise=true;simulated_hz=120;
    view.menu_command(2);HWND popup=view.fullscreen_window();
    check(popup && view.active_fps()==120,"target display A starts at 120 Hz");

    auto wait_positions=[&](unsigned count){
        const auto deadline=GetTickCount64()+2000;
        while(position_calls<count && GetTickCount64()<deadline)pump(10);
        check(position_calls==count,"expected placement attempt ran");
    };
    position_calls=rate_queries=0;fail_position=true;simulated_hz=165;
    SendMessage(popup,WM_DISPLAYCHANGE,0,0);
    check(position_calls==1 && rate_queries==0 && view.active_fps()==120,"failed placement skips refresh lookup and keeps active FPS");
    fail_position=false;wait_positions(2);
    check(rate_queries==1 && view.active_fps()==165,"timer retries placement and accepts A after success");

    position_calls=rate_queries=0;fixture_device_override=L"Fixture display B";simulated_hz=60;
    SendMessage(popup,WM_DISPLAYCHANGE,0,0);
    check(rate_queries==1 && view.active_fps()==165,"precise 60 Hz from B cannot replace A");
    fixture_device_override=nullptr;simulated_hz=144;wait_positions(2);pump(350);
    check(position_calls==2 && rate_queries==2 && view.active_fps()==144,"A at 144 Hz recovers and stops retries");

    position_calls=rate_queries=0;simulated_precise=false;simulated_hz=75;
    SendMessage(popup,WM_DISPLAYCHANGE,0,0); // A supplies a usable integer candidate.
    fixture_device_override=L"Fixture display B";simulated_hz=60;wait_positions(2);
    fail_position=true;wait_positions(3);
    check(view.active_fps()==144 && rate_queries==2,"foreign integer and failed placement preserve active rate");
    fail_position=false;fixture_device_override=nullptr;simulated_hz=0;wait_positions(4);pump(350);
    check(position_calls==4 && rate_queries==3 && view.active_fps()==75,"only A's candidate survives foreign sample and failed placement");

    position_calls=rate_queries=0;simulated_hz=60;fixture_device_override=L"Fixture display B";
    SendMessage(popup,WM_DEVICECHANGE,0,0);wait_positions(4);pump(350);
    check(position_calls==4 && rate_queries==4 && view.active_fps()==75,"persistent wrong display stops after three retries without adopting B");
    for(bool final_identity_known:{true,false}){
        fixture_device_override=nullptr;simulated_precise=true;simulated_hz=144;
        SendMessage(popup,WM_DISPLAYCHANGE,0,0);
        position_calls=rate_queries=0;simulated_precise=false;simulated_hz=75;
        SendMessage(popup,WM_DISPLAYCHANGE,0,0); // A's integer candidate.
        fixture_device_override=L"";simulated_hz=90;
        wait_positions(2); // Unidentified integer cannot replace A's candidate.
        simulated_hz=0;wait_positions(3);
        check(rate_queries==3 && view.active_fps()==144,"unknown samples preserve current FPS during retries");
        fixture_device_override=final_identity_known?nullptr:L"";
        wait_positions(4);pump(350);
        check(position_calls==4 && rate_queries==4,"final zero-Hz result ends this retry cycle");
        if(final_identity_known)check(view.active_fps()==75,"A at zero Hz settles A's last integer candidate");
        else check(view.active_fps()==144,"unknown device at zero Hz keeps current FPS on final retry");
    }
    fixture_device_override=nullptr;simulated_precise=true;
    view.destroy_window();DestroyWindow(parent);
}
struct ModalRun {
    ui_element_instance_ptr* panel=nullptr;HostApi* api=nullptr;HWND parent=nullptr;int mode=0;unsigned attempts=0;bool visited=false;std::string error;HWND menu_owner=nullptr;
};
ModalRun* active=nullptr;
void CALLBACK modal_timer(HWND,UINT,UINT_PTR timer,DWORD){
    auto* run=active;if(!run)return;
    if(run->mode>=4){
        if(run->visited){
            if(++run->attempts>60){
                run->error="panel destruction did not dismiss tracking menu";KillTimer(nullptr,timer);EndMenu();
            }
            return;
        }
        run->visited=true;
        try{
            GUITHREADINFO gui{sizeof(gui)};
            check(GetGUIThreadInfo(GetCurrentThreadId(),&gui) && (gui.flags&GUI_INMENUMODE) && gui.hwndMenuOwner==run->menu_owner,"panel or fullscreen window owns active tracking menu");
            DestroyWindow((*run->panel)->get_wnd());run->panel->release();check(run->api->callbacks->clients.size()==1,"real WndProc retains service while context menu tracks");check(Stream::live==0 && monitor_windows()==0,"context menu removal releases window resources");}
        catch(const std::exception& e){run->error=e.what();}
        if(!run->error.empty()){KillTimer(nullptr,timer);EndMenu();}
        return;
    }
    HWND dialog=nullptr;
    EnumWindows([](HWND w,LPARAM p)->BOOL{DWORD process=0;GetWindowThreadProcessId(w,&process);if(process==GetCurrentProcessId() && IsWindowVisible(w) && GetDlgItem(w,IDC_SETTINGS_TAB)){*reinterpret_cast<HWND*>(p)=w;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&dialog));
    if(!dialog){if(++run->attempts>100){run->error="real settings dialog never opened";KillTimer(nullptr,timer);PostQuitMessage(1);}return;}
    KillTimer(nullptr,timer);ShowWindowAsync(dialog,SW_HIDE);run->visited=true;
    try{
        check(GetDlgItem(dialog,IDC_RANGE)!=nullptr,"real dialog resource and controls loaded");
        HWND panel_window=(*run->panel)->get_wnd();
        if(run->mode==2)DestroyWindow(run->parent);else DestroyWindow(panel_window);
        check(!(*run->panel)->get_wnd(),"panel HWND cleared while settings modal loop active");
        check(Stream::live==0 && monitor_windows()==0,"modal panel removal stops stream and observer");
        for(auto* callback:run->api->callbacks->clients){callback->on_playback_pause(true);callback->on_playback_seek(10);callback->on_playback_pause(false);}
        check(Stream::live==0 && monitor_windows()==0,"playback callbacks after HWND destruction cannot restart resources");
        if(run->mode==1){run->panel->release();check(run->api->callbacks->clients.size()==1,"modal command retains real instance after host release");}
        if(IsWindow(dialog)){
            SetDlgItemInt(dialog,IDC_RANGE,137,FALSE);PostMessage(dialog,WM_COMMAND,IDC_APPLY,0);
            if(run->panel->is_valid()){
                csp::Settings saved;bool updated=false;const auto deadline=GetTickCount64()+3000;
                do{auto cfg=(*run->panel)->get_configuration();updated=csp::decode_panel_config(cfg->get_data(),cfg->get_data_size(),saved) && saved.range==137;if(!updated)pump(10);}while(!updated && GetTickCount64()<deadline);
                check(updated,"Apply after HWND destruction is safe and saves current state");
            }
            if(run->mode==3)PostMessage(dialog,WM_CLOSE,0,0);else PostMessage(dialog,WM_COMMAND,IDCANCEL,0);
        }
    }catch(const std::exception& e){run->error=e.what();if(IsWindow(dialog))PostMessage(dialog,WM_COMMAND,IDCANCEL,0);}
}
LRESULT CALLBACK dialog_hook(int code,WPARAM wp,LPARAM lp){
    if(code==HCBT_ACTIVATE && GetDlgItem(reinterpret_cast<HWND>(wp),IDC_SETTINGS_TAB))SetWindowPos(reinterpret_cast<HWND>(wp),nullptr,-30000,-30000,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
    return CallNextHookEx(nullptr,code,wp,lp);
}
void modal_lifecycle(HostApi& api,int mode){
    HWND parent=parent_window();service_ptr_t<UiCallback> callback=new service_impl_t<UiCallback>();
    service_ptr_t<csp::DefaultElement<SpectrumWindow>> element=new service_impl_t<csp::DefaultElement<SpectrumWindow>>();
    auto panel=element->instantiate(parent,config(false),callback);check(wait_reads(Stream::reads),"modal fixture timer active");
    ModalRun run{&panel,&api,parent,mode};active=&run;
    HHOOK hook=SetWindowsHookExW(WH_CBT,dialog_hook,nullptr,GetCurrentThreadId());check(hook!=nullptr,"dialog placement hook installed");
    HWND popup=nullptr;
    if(mode==5){
        panel->edit_mode_context_menu_command({},false,1002,1000);
        // The adapter's embedded HWND stores its own SpectrumWindow in GWLP_USERDATA.
        auto* view=reinterpret_cast<SpectrumWindow*>(GetWindowLongPtr(panel->get_wnd(),GWLP_USERDATA));
        check(view && view->get_wnd()==panel->get_wnd(),"fixture resolved its own adapter view");
        popup=view->fullscreen_window();check(popup!=nullptr,"fullscreen menu fixture opened");
    }
    run.menu_owner=popup?popup:panel->get_wnd();
    UINT_PTR timer=SetTimer(nullptr,0,30,modal_timer);check(timer!=0,"modal test action scheduled");
    if(mode==4)SendMessage(panel->get_wnd(),WM_CONTEXTMENU,reinterpret_cast<WPARAM>(panel->get_wnd()),MAKELPARAM(-1,-1));
    else if(mode==5){
        SendMessage(popup,WM_CONTEXTMENU,reinterpret_cast<WPARAM>(popup),MAKELPARAM(-1,-1));
        check(!IsWindow(popup),"removal while tracking menu destroyed popup");
    }else panel->edit_mode_context_menu_command({},false,1001,1000);
    KillTimer(nullptr,timer);UnhookWindowsHookEx(hook);active=nullptr;
    check(run.visited,"real modal message loop executed");check(run.error.empty(),run.error.c_str());
    panel.release();pump(40);check(api.callbacks->clients.empty() && Stream::live==0 && monitor_windows()==0,"modal teardown releases final real instance resources");
    if(IsWindow(parent))DestroyWindow(parent);
}
}
int main(){
    try{
        HostApi api;auto* client=foobar2000_get_interface(&api,GetModuleHandle(nullptr));client->services_init(true);
        menu_build_failure(api);
        early_stream_lifecycle(api);
        fullscreen_lifecycle();
        fullscreen_registration_and_retry();
        fullscreen_topology_retry_limit();
        menu_language_tests();
        fps_overlay_tests(api);
        test_sequence_unwinding();
        for(bool exhaust_topology:{true,false})fullscreen_alternating_failures(exhaust_topology);
        fullscreen_rate_retry();
        fullscreen_display_identity();
        for(bool scanline:{false,true})for(bool direct2d:{false,true})independent_settings_refresh(api,scanline,direct2d);
        settings_dispatch_shutdown();
        settings_focus_tests();
        interaction_refresh(api,false);interaction_refresh(api,true);
        // Compare equivalent fullscreen batches with all windows and menus closed.
        auto fullscreenBatch=[&]{
            fullscreen_lifecycle();fullscreen_registration_and_retry();fullscreen_reopen_discards_candidate();
            modal_lifecycle(api,5);pump(100);
        };
        fullscreenBatch();
        const auto process=GetCurrentProcess();
        const DWORD fullscreenGdi=GetGuiResources(process,GR_GDIOBJECTS),fullscreenUser=GetGuiResources(process,GR_USEROBJECTS);
        DWORD fullscreenHandles=0,afterFullscreen=0;
        check(GetProcessHandleCount(process,&fullscreenHandles)!=FALSE,"fullscreen handle baseline");
        fullscreenBatch();
        check(GetGuiResources(process,GR_GDIOBJECTS)==fullscreenGdi && GetGuiResources(process,GR_USEROBJECTS)==fullscreenUser,"fullscreen switches, monitor retries and menu teardown release GDI and USER handles");
        check(GetProcessHandleCount(process,&afterFullscreen)!=FALSE,"fullscreen final handle count");
        const auto fullscreenCleanupDeadline=GetTickCount64()+2000;
        while(afterFullscreen>fullscreenHandles && GetTickCount64()<fullscreenCleanupDeadline){
            pump(25);check(GetProcessHandleCount(process,&afterFullscreen)!=FALSE,"pending fullscreen handle cleanup");
        }
        if(afterFullscreen>fullscreenHandles)std::cerr<<"fullscreen process handles before="<<fullscreenHandles<<" after="<<afterFullscreen<<'\n';
        check(afterFullscreen<=fullscreenHandles,"no process handle growth through fullscreen lifetimes");
        // Exercise the same set of graphics, dialog and thread-pool paths on
        // both sides of the process-wide handle comparison.
        auto windowBatch=[&]{
            for(int i=0;i<12;++i)lifecycle(api,(i%2)!=0,i%3==0);
            columns_reopen(api);
            for(int mode=0;mode<6;++mode)modal_lifecycle(api,mode);
            settings_focus_tests();
            pump(100);
        };
        windowBatch();
        const DWORD gdi=GetGuiResources(process,GR_GDIOBJECTS),user=GetGuiResources(process,GR_USEROBJECTS);
        DWORD handles=0;check(GetProcessHandleCount(process,&handles)!=FALSE,"baseline process handle count");
        windowBatch();
        check(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==gdi,"no GDI handle growth across real window/dialog lifetimes");
        check(GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS)==user,"no USER handle growth across real window/dialog lifetimes");
        DWORD final_handles=0;check(GetProcessHandleCount(GetCurrentProcess(),&final_handles)!=FALSE,"final process handle count");
        const auto cleanupDeadline=GetTickCount64()+2000;
        while(final_handles>handles && GetTickCount64()<cleanupDeadline){
            pump(25);check(GetProcessHandleCount(GetCurrentProcess(),&final_handles)!=FALSE,"pending handle cleanup");
        }
        if(final_handles>handles)std::cerr<<"process handles before="<<handles<<" final="<<final_handles<<'\n';
        check(final_handles<=handles,"no process handle growth after worker and hook cleanup");
        client->services_init(false);
        std::cout<<"PASS: "<<checks<<" real SpectrumWindow lifecycle checks (synthetic host/audio services; no foobar2000 process).\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
