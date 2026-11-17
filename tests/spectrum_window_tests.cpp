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
    int64_t getConfigInt(const char* key,int64_t fallback) override{auto i=values.find(key);return i==values.end()?fallback:i->second;}
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
    HWND window;unsigned long long completed=0;
    static LRESULT CALLBACK callback(HWND w,UINT message,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR data){
        auto* self=reinterpret_cast<PaintObserver*>(data);
        if(message==WM_NCDESTROY){RemoveWindowSubclass(w,callback,id);self->window=nullptr;}
        const auto result=DefSubclassProc(w,message,wp,lp);
        if(message==WM_PAINT)++self->completed;
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
struct ModalRun {
    ui_element_instance_ptr* panel=nullptr;HostApi* api=nullptr;HWND parent=nullptr;int mode=0;unsigned attempts=0;bool visited=false;std::string error;
};
ModalRun* active=nullptr;
void CALLBACK modal_timer(HWND,UINT,UINT_PTR timer,DWORD){
    auto* run=active;if(!run)return;
    HWND dialog=nullptr;
    EnumWindows([](HWND w,LPARAM p)->BOOL{DWORD process=0;GetWindowThreadProcessId(w,&process);if(process==GetCurrentProcessId() && IsWindowVisible(w) && GetDlgItem(w,IDC_SETTINGS_TAB)){*reinterpret_cast<HWND*>(p)=w;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&dialog));
    if(run->mode==4){
        KillTimer(nullptr,timer);run->visited=true;
        try{DestroyWindow((*run->panel)->get_wnd());run->panel->release();check(run->api->callbacks->clients.size()==1,"real WndProc retains service while context menu tracks");check(Stream::live==0 && monitor_windows()==0,"context menu removal releases window resources");}
        catch(const std::exception& e){run->error=e.what();}
        EndMenu();return;
    }
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
    UINT_PTR timer=SetTimer(nullptr,0,30,modal_timer);check(timer!=0,"modal test action scheduled");
    if(mode==4)SendMessage(panel->get_wnd(),WM_CONTEXTMENU,reinterpret_cast<WPARAM>(panel->get_wnd()),MAKELPARAM(-1,-1));
    else panel->edit_mode_context_menu_command({},false,1001,1000);
    KillTimer(nullptr,timer);UnhookWindowsHookEx(hook);active=nullptr;
    check(run.visited,"real modal message loop executed");check(run.error.empty(),run.error.c_str());
    panel.release();pump(40);check(api.callbacks->clients.empty() && Stream::live==0 && monitor_windows()==0,"modal teardown releases final real instance resources");
    if(IsWindow(parent))DestroyWindow(parent);
}
}
int main(){
    try{
        HostApi api;auto* client=foobar2000_get_interface(&api,GetModuleHandle(nullptr));client->services_init(true);
        early_stream_lifecycle(api);
        for(bool scanline:{false,true})for(bool direct2d:{false,true})independent_settings_refresh(api,scanline,direct2d);
        settings_dispatch_shutdown();
        interaction_refresh(api,false);interaction_refresh(api,true);
        // Warm up process-wide graphics/dialog caches before checking handle growth.
        lifecycle(api,false,true);lifecycle(api,true);columns_reopen(api);
        for(int mode=0;mode<5;++mode)modal_lifecycle(api,mode);
        const DWORD gdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS),user=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
        DWORD handles=0;check(GetProcessHandleCount(GetCurrentProcess(),&handles)!=FALSE,"baseline process handle count");
        for(int i=0;i<12;++i)lifecycle(api,(i%2)!=0,i%3==0);
        columns_reopen(api);
        for(int mode=0;mode<5;++mode)modal_lifecycle(api,mode);
        pump(100);
        check(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==gdi,"no GDI handle growth across real window/dialog lifetimes");
        check(GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS)==user,"no USER handle growth across real window/dialog lifetimes");
        DWORD final_handles=0;check(GetProcessHandleCount(GetCurrentProcess(),&final_handles)!=FALSE,"final process handle count");
        check(final_handles<=handles,"no process handle growth after worker and hook cleanup");
        client->services_init(false);
        std::cout<<"PASS: "<<checks<<" real SpectrumWindow lifecycle checks (synthetic host/audio services; no foobar2000 process).\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
