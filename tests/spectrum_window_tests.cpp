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
    inline static unsigned reads=0,live=0,created=0;
    Stream(){++live;++created;}~Stream(){--live;}
    bool get_absolute_time(double& time) override{time=GetTickCount64()/1000.0;return true;}
    bool get_chunk_absolute(audio_chunk&,double,double) override{return false;}
    bool get_spectrum_absolute(audio_chunk& chunk,double,unsigned fft) override{
        ++reads;std::vector<audio_sample> data(fft,audio_sample(.001));data[2]=audio_sample(.1);data[7]=audio_sample(.2);
        chunk.set_data(data.data(),fft/2,2,48000,audio_chunk::channel_config_stereo);return true;
    }
    void make_fake_chunk_absolute(audio_chunk&,double,double) override{}
    void make_fake_spectrum_absolute(audio_chunk&,double,unsigned) override{}
};
class Visualisation : public visualisation_manager {
public:void create_stream(visualisation_stream::ptr& out,unsigned) override{out=new service_impl_t<Stream>();}
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
ui_element_config::ptr config(bool d2d){csp::Settings s;s.direct2d=d2d;s.follow_monitor=false;s.scanline_sync=false;s.fps_milli=60000;const auto b=csp::encode_panel_config(s);return ui_element_config::g_create(csp::default_ui_guid,b.data(),b.size());}
void lifecycle(HostApi& api,bool d2d){
    HWND parent=parent_window();
    service_ptr_t<UiCallback> callback=new service_impl_t<UiCallback>();
    service_ptr_t<csp::DefaultElement<SpectrumWindow>> element=new service_impl_t<csp::DefaultElement<SpectrumWindow>>();
    auto panel=element->instantiate(parent,config(d2d),callback);HWND window=panel->get_wnd();
    check(api.callbacks->clients.size()==1,"real playback callback registered");
    check(wait_reads(Stream::reads),"real timer reaches synthetic visualization service");
    UpdateWindow(window);check(Stream::live==1,"active real window owns stream");
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
struct ModalRun {
    ui_element_instance_ptr* panel=nullptr;HostApi* api=nullptr;HWND parent=nullptr;int mode=0;unsigned attempts=0;bool visited=false;std::string error;
};
ModalRun* active=nullptr;
void CALLBACK modal_timer(HWND,UINT,UINT_PTR timer,DWORD){
    auto* run=active;if(!run)return;
    HWND dialog=nullptr;
    EnumThreadWindows(GetCurrentThreadId(),[](HWND w,LPARAM p)->BOOL{if(GetDlgItem(w,IDC_SETTINGS_TAB)){*reinterpret_cast<HWND*>(p)=w;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&dialog));
    if(run->mode==4){
        KillTimer(nullptr,timer);run->visited=true;
        try{DestroyWindow((*run->panel)->get_wnd());run->panel->release();check(run->api->callbacks->clients.size()==1,"real WndProc retains service while context menu tracks");check(Stream::live==0 && monitor_windows()==0,"context menu removal releases window resources");}
        catch(const std::exception& e){run->error=e.what();}
        EndMenu();return;
    }
    if(!dialog){if(++run->attempts>100){run->error="real settings dialog never opened";KillTimer(nullptr,timer);PostQuitMessage(1);}return;}
    KillTimer(nullptr,timer);ShowWindow(dialog,SW_HIDE);run->visited=true;
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
            SetDlgItemInt(dialog,IDC_RANGE,137,FALSE);SendMessage(dialog,WM_COMMAND,IDC_APPLY,0);
            if(run->panel->is_valid()){
                csp::Settings saved;auto cfg=(*run->panel)->get_configuration();check(csp::decode_panel_config(cfg->get_data(),cfg->get_data_size(),saved) && saved.range==137,"Apply after HWND destruction is safe and saves current state");
            }
            if(run->mode==3)SendMessage(dialog,WM_CLOSE,0,0);else SendMessage(dialog,WM_COMMAND,IDCANCEL,0);
        }
    }catch(const std::exception& e){run->error=e.what();if(IsWindow(dialog))EndDialog(dialog,IDCANCEL);}
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
        // Warm up process-wide graphics/dialog caches before checking handle growth.
        lifecycle(api,false);lifecycle(api,true);columns_reopen(api);
        for(int mode=0;mode<5;++mode)modal_lifecycle(api,mode);
        const DWORD gdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS),user=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
        DWORD handles=0;check(GetProcessHandleCount(GetCurrentProcess(),&handles)!=FALSE,"baseline process handle count");
        for(int i=0;i<12;++i)lifecycle(api,(i%2)!=0);
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
