#pragma once
#include "monitor_refresh.h"
#include <memory>
namespace csp {
// UI-thread owned. Hooks never dereference Panel or dialog objects; observers receive messages.
class MonitorEvents {
public:
    static constexpr UINT changed_message=WM_APP+110;
    static constexpr UINT visibility_message=WM_APP+111;
    using Query=MonitorRefresh(*)(HWND);
    using Locate=HMONITOR(*)(HWND);
    explicit MonitorEvents(Query query=query_monitor_refresh,Locate locate=locate_monitor):query_(query),locate_(locate){}
    static constexpr wchar_t identity_property[]=L"CspSettingsMonitorTarget";
    explicit MonitorEvents(HANDLE identity,Query query=query_monitor_refresh,Locate locate=locate_monitor):MonitorEvents(query,locate){identity_=identity;}
    MonitorEvents(const MonitorEvents&)=delete;
    MonitorEvents& operator=(const MonitorEvents&)=delete;
    ~MonitorEvents(){stop();}
    const MonitorRefresh& current() const noexcept {return current_;}
    bool bind(HWND panel,bool simulate_hook_failure=false){
        if(panel_==panel && receiver_)return valid_panel() && hook_!=nullptr;
        HWND observer=panel_==panel?dialog_:nullptr;
        stop();panel_=panel;dialog_=observer;
        if(!valid_panel())return false;
        refresh(true);
        HMODULE module=nullptr;
        GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&window_proc),&module);
        module_=module;
        WNDCLASS wc{};wc.hInstance=module_;wc.lpfnWndProc=window_proc;wc.lpszClassName=class_name;
        if(!RegisterClass(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;
        // A hidden top-level window receives broadcasts; HWND_MESSAGE would not.
        receiver_=CreateWindowEx(WS_EX_TOOLWINDOW,class_name,L"",WS_POPUP,0,0,0,0,nullptr,nullptr,module_,this);
        if(!receiver_)return false;
        next_=head_;head_=this;linked_=true;
        if(!simulate_hook_failure)hook_=SetWinEventHook(EVENT_OBJECT_SHOW,EVENT_OBJECT_LOCATIONCHANGE,nullptr,event_proc,
            GetCurrentProcessId(),0,WINEVENT_OUTOFCONTEXT);

        schedule_retry();
        return hook_!=nullptr;
    }
    void stop() noexcept {
        if(hook_){UnhookWinEvent(hook_);hook_=nullptr;}
        if(linked_){auto** link=&head_;while(*link && *link!=this)link=&(*link)->next_;if(*link)*link=next_;linked_=false;next_=nullptr;}
        if(receiver_){DestroyWindow(receiver_);receiver_=nullptr;}
        if(module_)UnregisterClass(class_name,module_);
        module_=nullptr;panel_=nullptr;dialog_=nullptr;pending_=false;force_=false;retry_pending_=false;retry_attempt_=0;polling_active_=false;
    }
    bool tracking_available() const noexcept {return hook_ || polling_active_;}
    void set_polling_active(bool active) noexcept {polling_active_=active;}
    bool has_receiver() const noexcept {return receiver_!=nullptr;}
    void attach_dialog(HWND dialog){dialog_=dialog;reset_retry();refresh(true);}
    void detach_dialog(HWND dialog) noexcept {if(dialog_==dialog)dialog_=nullptr;}
    void location_changed(){request(false);}
    void display_changed(){request(true);}
    void refresh(bool force){
        if(!valid_panel()){current_={};return;}
        if(!force && locate_(panel_)==current_.monitor)return;
        current_=query_(panel_);
        if(current_.precise)reset_retry();else schedule_retry();
        // Identity-guarded observers belong to the settings thread: never post
        // private messages to a host HWND that can be destroyed concurrently.
        if(!identity_)PostMessage(panel_,changed_message,0,0);
        if(dialog_ && IsWindow(dialog_))PostMessage(dialog_,changed_message,0,0);
    }
private:
    bool valid_panel() const noexcept {return panel_ && IsWindow(panel_) && (!identity_ || GetProp(panel_,identity_property)==identity_);}
    void reset_retry() noexcept {
        if(receiver_)KillTimer(receiver_,2);
        retry_pending_=false;retry_attempt_=0;
    }
    void schedule_retry() noexcept {
        constexpr UINT delays[]={100,250,500};
        if(current_.precise || retry_pending_ || retry_attempt_>=3 || !receiver_)return;
        if(SetTimer(receiver_,2,delays[retry_attempt_],nullptr)){retry_pending_=true;++retry_attempt_;}
    }
    static HMONITOR locate_monitor(HWND window){return MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST);}
    void request(bool force){
        if(!valid_panel()){refresh(false);return;}
        if(!force && locate_(panel_)==current_.monitor)return;
        reset_retry();
        force_|=force;
        if(pending_)return;
        if(receiver_ && SetTimer(receiver_,1,50,nullptr)){pending_=true;return;}
        const bool forced=force_;force_=false;refresh(forced);
    }
    static void CALLBACK event_proc(HWINEVENTHOOK hook,DWORD event,HWND window,LONG object,LONG child,DWORD,DWORD) noexcept {
        if(event!=EVENT_OBJECT_LOCATIONCHANGE && event!=EVENT_OBJECT_SHOW && event!=EVENT_OBJECT_HIDE)return;
        if(object!=OBJID_WINDOW || child!=CHILDID_SELF || !window)return;
        for(auto* watcher=head_;watcher;watcher=watcher->next_){
            if(watcher->hook_==hook && watcher->valid_panel() && (window==watcher->panel_ || IsChild(window,watcher->panel_))){
                if(event==EVENT_OBJECT_LOCATIONCHANGE){try{watcher->location_changed();}catch(...){}}
                else if(!watcher->identity_)PostMessage(watcher->panel_,visibility_message,0,0);
                return;
            }
        }
    }
    static LRESULT CALLBACK window_proc(HWND w,UINT msg,WPARAM wp,LPARAM lp) noexcept {
        auto* self=reinterpret_cast<MonitorEvents*>(GetWindowLongPtr(w,GWLP_USERDATA));
        if(msg==WM_NCCREATE){self=static_cast<MonitorEvents*>(reinterpret_cast<CREATESTRUCT*>(lp)->lpCreateParams);SetWindowLongPtr(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(self)try{
            if(msg==WM_DISPLAYCHANGE || msg==WM_DEVICECHANGE || msg==WM_SETTINGCHANGE){self->display_changed();return 0;}
            if(msg==WM_TIMER && wp==1 && self->pending_){KillTimer(w,1);self->pending_=false;const bool force=self->force_;self->force_=false;self->refresh(force);return 0;}
            if(msg==WM_TIMER && wp==2 && self->retry_pending_){KillTimer(w,2);self->retry_pending_=false;self->refresh(true);return 0;}
            if(msg==WM_NCDESTROY)SetWindowLongPtr(w,GWLP_USERDATA,0);
        }catch(...){}
        return DefWindowProc(w,msg,wp,lp);
    }
    inline static thread_local MonitorEvents* head_=nullptr;
    static constexpr wchar_t class_name[]=L"ChannelSpectrumMirror.MonitorEvents.1";
    MonitorEvents* next_=nullptr;
    HWND panel_=nullptr,dialog_=nullptr,receiver_=nullptr;
    HMODULE module_=nullptr;
    HWINEVENTHOOK hook_=nullptr;
    bool linked_=false,pending_=false,force_=false;
    unsigned retry_attempt_=0;
    bool retry_pending_=false,polling_active_=false;
    MonitorRefresh current_;
    HANDLE identity_=nullptr;
    Query query_;
    Locate locate_;
};
}
