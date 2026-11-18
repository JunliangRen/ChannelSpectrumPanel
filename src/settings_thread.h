#pragma once
#include "settings_ui.h"
#include "settings_focus.h"
#include <process.h>
#include <exception>
#include <stdexcept>

namespace {
// Only this message-only bridge touches the panel, host configuration or language
// preference. The dialog thread owns its controls, font, monitor watcher and saves.
class SettingsThread {
    using GetDpiContext=HANDLE(WINAPI*)();
    using SetDpiContext=HANDLE(WINAPI*)(HANDLE);
    static HANDLE dpi_context() noexcept {
        auto get=reinterpret_cast<GetDpiContext>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetThreadDpiAwarenessContext"));
        return get?get():nullptr;
    }
    static constexpr UINT invoke_message=WM_APP+220;
    struct Call {
        HANDLE done=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        Call()=default;Call(const Call&)=delete;Call& operator=(const Call&)=delete;
        ~Call(){if(done)CloseHandle(done);}
        HWND closing_dialog=nullptr;
        const csp::Settings* settings=nullptr;
        unsigned language=0;
        bool completed=false;
        std::exception_ptr error;
    };
    struct Job {
        HINSTANCE instance; HWND bridge; HWND panel; RECT position; HANDLE dpi;
        csp::Settings settings; unsigned language;
        bool changed=false;
        std::exception_ptr error;
        std::function<void(HWND)> initialized;
    };
    struct BridgeContext {DialogState& state;csp::SettingsModalOwner& owner;};
    static LRESULT CALLBACK bridge_proc(HWND w,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR data) noexcept {
        if(msg==WM_NCDESTROY)RemoveWindowSubclass(w,bridge_proc,id);
        if(msg==invoke_message){
            auto& context=*reinterpret_cast<BridgeContext*>(data);
            auto& state=context.state;
            auto& call=*reinterpret_cast<Call*>(lp);
            try {if(call.closing_dialog)context.owner.closing(call.closing_dialog);else if(call.settings)state.apply(*call.settings);else if(state.save_language)state.save_language(call.language);}
            catch(...){call.error=std::current_exception();}
            call.completed=true;SetEvent(call.done);return 1;
        }
        return DefSubclassProc(w,msg,wp,lp);
    }
    static void invoke(HWND bridge,Call& call){
        if(!call.done)throw std::runtime_error("Could not allocate settings completion event.");
        // Posted requests run in the host message pump, not reentrantly inside a
        // graphics/Win32 call that happens to dispatch synchronous sent messages.
        if(!PostMessage(bridge,invoke_message,0,reinterpret_cast<LPARAM>(&call)))throw std::runtime_error("Could not queue settings update.");
        while(WaitForSingleObject(call.done,0)!=WAIT_OBJECT_0){
            MsgWaitForMultipleObjectsEx(1,&call.done,INFINITE,QS_SENDMESSAGE,MWMO_INPUTAVAILABLE);
            MSG message{};PeekMessage(&message,nullptr,0,0,PM_NOREMOVE|PM_QS_SENDMESSAGE);
        }
        if(call.error)std::rethrow_exception(call.error);
        if(!call.completed)throw std::runtime_error("Settings dispatcher is unavailable.");
    }
    static unsigned __stdcall entry(void* context) noexcept {
        auto& job=*static_cast<Job*>(context);
        HWND owner=nullptr;
        const HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
        try {
            if(job.dpi){
                auto set=reinterpret_cast<SetDpiContext>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"SetThreadDpiAwarenessContext"));
                if(set)set(job.dpi);
            }
            if(FAILED(com))throw std::runtime_error("Could not initialize settings COM apartment.");
            INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_TAB_CLASSES|ICC_LISTVIEW_CLASSES|ICC_BAR_CLASSES|ICC_UPDOWN_CLASS};
            if(!InitCommonControlsEx(&controls))throw std::runtime_error("Could not initialize settings controls.");
            // A same-thread hidden owner keeps the dialog off the taskbar without
            // attaching its input queue to foobar2000's main UI thread.
            owner=CreateWindowEx(WS_EX_TOOLWINDOW,L"STATIC",L"",WS_POPUP,
                job.position.left,job.position.top,job.position.right-job.position.left,job.position.bottom-job.position.top,
                nullptr,nullptr,job.instance,nullptr);
            if(!owner)throw std::runtime_error("Could not create settings owner.");
            {
                DialogState dialog{job.settings,[&](const csp::Settings& next){Call call;call.settings=&next;invoke(job.bridge,call);}};
                dialog.closing=[&](HWND w){Call call;call.closing_dialog=w;invoke(job.bridge,call);};
                dialog.language=job.language;
                dialog.save_language=[&](unsigned language){Call call;call.language=language;invoke(job.bridge,call);};
                dialog.monitor_events=std::make_shared<csp::MonitorEvents>(job.bridge);
                dialog.monitor_fallback=!dialog.monitor_events->bind(job.panel);
                dialog.initialized=[&](HWND w){
                    RECT rect{};GetWindowRect(w,&rect);
                    int width=rect.right-rect.left,height=rect.bottom-rect.top;
                    int x=job.position.left+(job.position.right-job.position.left-width)/2;
                    int y=job.position.top+(job.position.bottom-job.position.top-height)/2;
                    if(HMONITOR monitor=MonitorFromRect(&job.position,MONITOR_DEFAULTTONULL)){
                        MONITORINFO info{sizeof(info)};
                        if(GetMonitorInfo(monitor,&info)){
                            x=std::max(info.rcWork.left,std::min(LONG(x),info.rcWork.right-width));
                            y=std::max(info.rcWork.top,std::min(LONG(y),info.rcWork.bottom-height));
                        }
                    }
                    SetWindowPos(w,nullptr,x,y,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
                    if(job.initialized)job.initialized(w);
                };
                const auto result=DialogBoxParam(job.instance,MAKEINTRESOURCE(IDD_SETTINGS),owner,settings_proc,reinterpret_cast<LPARAM>(&dialog));
                if(result==-1)throw std::runtime_error("Could not open settings dialog.");
                job.changed=dialog.changed;
            } // All UI-thread-owned resources are released before thread completion.
        }catch(...){job.error=std::current_exception();}
        if(owner)DestroyWindow(owner);
        if(SUCCEEDED(com))CoUninitialize();
        return 0; // _beginthreadex retains the DLL through CRT thread cleanup.
    }
public:
    using Launch=decltype(&_beginthreadex);
    static bool run(HINSTANCE instance,HWND panel,DialogState& state,std::function<void(HWND)> initialized={},HWND monitorTarget=nullptr,Launch launch=&_beginthreadex,const csp::SettingsFocusApi* focusApi=nullptr){
        if(!monitorTarget)monitorTarget=panel;
        HWND root=GetAncestor(panel,GA_ROOT);
        RECT position{};if(!GetWindowRect(root?root:panel,&position))SystemParametersInfo(SPI_GETWORKAREA,0,&position,0);
        static const csp::SettingsFocusApi nativeFocus;
        csp::SettingsModalOwner modalOwner(root,focusApi?*focusApi:nativeFocus);
        BridgeContext context{state,modalOwner};
        HWND bridge=CreateWindowEx(0,L"STATIC",L"",0,0,0,0,0,HWND_MESSAGE,nullptr,instance,nullptr);
        if(!bridge)throw std::runtime_error("Could not create settings dispatcher.");
        struct BridgeCleanup{HWND window;~BridgeCleanup(){DestroyWindow(window);}} bridgeCleanup{bridge};
        if(!SetWindowSubclass(bridge,bridge_proc,1,reinterpret_cast<DWORD_PTR>(&context)))throw std::runtime_error("Could not initialize settings dispatcher.");
        struct PanelMarker {
            HWND panel;HANDLE token;
            PanelMarker(HWND w,HANDLE value):panel(w),token(value){
                if(IsWindow(w) && !SetProp(w,csp::MonitorEvents::identity_property,value))throw std::runtime_error("Could not track settings monitor target.");
            }
            ~PanelMarker(){if(GetProp(panel,csp::MonitorEvents::identity_property)==token)RemoveProp(panel,csp::MonitorEvents::identity_property);}
        } panelMarker(monitorTarget,bridge);
        Job job{instance,bridge,monitorTarget,position,dpi_context(),state.value,state.language,false,{},std::move(initialized)};
        unsigned threadId=0;
        HANDLE thread=reinterpret_cast<HANDLE>(launch(nullptr,0,entry,&job,0,&threadId));
        if(!thread)throw std::runtime_error("Could not start settings thread.");
        bool quit=false;WPARAM quitCode=0;
        // Never join a UI worker without pumping: Apply can be synchronously
        // waiting for this bridge. Bound each batch so posted traffic cannot
        // prevent observing worker completion.
        while(WaitForSingleObject(thread,0)!=WAIT_OBJECT_0){
            MsgWaitForMultipleObjectsEx(1,&thread,quit?50:INFINITE,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
            MSG message{};
            for(unsigned n=0;n<64 && PeekMessage(&message,nullptr,0,0,PM_REMOVE);++n){
                if(message.message==WM_QUIT){quit=true;quitCode=message.wParam;}
                else {TranslateMessage(&message);DispatchMessage(&message);}
            }
            // Retrying also covers quit received before the worker's queue exists.
            if(quit)PostThreadMessage(threadId,WM_QUIT,quitCode,0);
        }
        CloseHandle(thread);
        if(quit)PostQuitMessage(int(quitCode));
        if(job.error)std::rethrow_exception(job.error);
        state.changed=job.changed;return job.changed;
    }
};
}
