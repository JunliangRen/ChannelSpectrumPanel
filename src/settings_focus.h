#pragma once
#include <windows.h>
#include <stdexcept>
namespace csp {
// All methods are called on the host thread. Injectable only so foreground
// policy can be tested without moving focus away from the user's applications.
struct SettingsFocusApi {
    virtual ~SettingsFocusApi()=default;
    virtual HWND foreground() const noexcept {return GetForegroundWindow();}
    virtual HWND keyboard_focus() const noexcept {return GetFocus();}
    virtual bool activate(HWND w) const noexcept {return SetForegroundWindow(w)!=FALSE;}
    virtual void focus(HWND w) const noexcept {SetFocus(w);}
};
class SettingsModalOwner {
public:
    static constexpr wchar_t owner_property[]=L"CspSettingsModalOwner";
    static constexpr wchar_t focus_property[]=L"CspSettingsPreviousFocus";
    explicit SettingsModalOwner(HWND window,const SettingsFocusApi& api):window_(window),api_(api){
        if(window && IsWindowEnabled(window)){
            if(!SetProp(window,owner_property,this))throw std::runtime_error("Could not disable settings owner safely.");
            restore_=true;
            const HWND focused=api_.keyboard_focus();
            if(focused && (focused==window || IsChild(window,focused)) && SetProp(focused,focus_property,this))focus_=focused;
            EnableWindow(window,FALSE);
        }
    }
    SettingsModalOwner(const SettingsModalOwner&)=delete;
    SettingsModalOwner& operator=(const SettingsModalOwner&)=delete;
    ~SettingsModalOwner(){
        enable(); // Failure/quit cleanup must never activate an application.
        if(valid_owner())RemoveProp(window_,owner_property);
        if(focus_ && GetProp(focus_,focus_property)==this)RemoveProp(focus_,focus_property);
    }
    // Execute BEFORE EndDialog, while foreground ownership is still meaningful.
    // Do not infer user intent from the unrelated window Windows selects after
    // the dialog or its hidden owner has already been destroyed.
    void closing(HWND dialog) noexcept {
        const bool foreground=dialog && api_.foreground()==dialog;
        enable();
        if(!foreground || !valid_owner() || !IsWindowEnabled(window_) || !IsWindowVisible(window_))return;
        // EnableWindow can dispatch host callbacks; honor a foreground switch
        // that happened during those callbacks as well as one before closing.
        if(api_.foreground()!=dialog)return;
        if(!api_.activate(window_) || !valid_owner() || api_.foreground()!=window_)return;
        const bool validFocus=focus_ && GetProp(focus_,focus_property)==this &&
            (focus_==window_ || IsChild(window_,focus_)) && IsWindowVisible(focus_) && IsWindowEnabled(focus_);
        api_.focus(validFocus?focus_:window_);
    }
private:
    bool valid_owner() const noexcept {return restore_ && GetProp(window_,owner_property)==this;}
    void enable() noexcept {
        if(!enabled_ && valid_owner()){enabled_=true;EnableWindow(window_,TRUE);}
    }
    HWND window_=nullptr,focus_=nullptr;
    const SettingsFocusApi& api_;
    bool restore_=false,enabled_=false;
};
}
