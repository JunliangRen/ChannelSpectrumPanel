#pragma once
#include <commdlg.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <functional>
#include "config.h"
#include "preset.h"
#include "background_save.h"
#include "resource.h"
#include "settings_layout.h"
#include "color_picker.h"
#include "channel_selection.h"
#include "settings_controls.h"
#include "monitor_events.h"
namespace {
struct DialogState { csp::Settings value; std::function<void(const csp::Settings&)> apply; bool changed=false; std::shared_ptr<csp::MonitorEvents> monitor_events; bool monitor_fallback=false; COLORREF custom_colors[16]{}; std::shared_ptr<csp::SaveResult> save; csp::SettingsHeadingFont heading_font; };
void combo(HWND w,int id,std::initializer_list<const wchar_t*> labels,unsigned selected){
    SendDlgItemMessage(w,id,CB_RESETCONTENT,0,0);
    for(auto label:labels)SendDlgItemMessage(w,id,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
    SendDlgItemMessage(w,id,CB_SETCURSEL,selected,0);
}
void update_monitor_label(HWND w,const DialogState& state){
    const auto monitor=state.monitor_events?state.monitor_events->current():csp::MonitorRefresh{};
    wchar_t text[192]{};
    const wchar_t* device=monitor.device;
    if(wcsncmp(device,L"\\\\.\\",4)==0)device+=4;
    if(state.monitor_events && !state.monitor_events->tracking_available())swprintf_s(text,L"Display monitoring unavailable; using manual FPS.");
    else if(monitor.hz>0)swprintf_s(text,L"Panel monitor: %s - %.3f Hz%s",device,monitor.hz,monitor.precise?L"":L" (integer report)");
    else {
        wchar_t manualText[64]{};GetDlgItemText(w,IDC_INTERVAL,manualText,64);
        const unsigned manual=csp::parse_target_fps(manualText);
        swprintf_s(text,L"Monitor rate unavailable; fallback: %u FPS",manual?manual:state.value.target_fps());
    }
    wchar_t previous[192]{};GetDlgItemText(w,IDC_MONITOR_RATE,previous,192);
    if(wcscmp(previous,text)!=0)SetDlgItemText(w,IDC_MONITOR_RATE,text);
    EnableWindow(GetDlgItem(w,IDC_INTERVAL),IsDlgButtonChecked(w,IDC_FOLLOW_MONITOR)!=BST_CHECKED);
}
void populate(HWND w,const csp::Settings& s){
    unsigned index=0;for(unsigned n=512;n<s.fft;n*=2)++index;
    combo(w,IDC_FFT,{L"512",L"1024",L"2048",L"4096",L"8192"},index);
    combo(w,IDC_STYLE,{L"Gradient bars (v0.1)",L"Simple-line + peaks",L"Separate thin lines",L"Simple",L"Horizontal",L"Vertical",L"Simple-antialiasing",L"Horizontal-antialiasing"},unsigned(s.style));
    combo(w,IDC_DIRECTION,{L"Mirror",L"Right",L"Left"},s.direction);
    combo(w,IDC_ROTATION,{L"No rotation (0)",L"Clockwise 90 degrees",L"Upside down (180)",L"Counterclockwise 90"},s.rotation);
    combo(w,IDC_CHANNELMODE,{L"Separate",L"Merge all (maximum)",L"Specified input"},s.channel_mode);
    combo(w,IDC_EDGE,{L"None",L"Sunken",L"Static"},s.edge);
    combo(w,IDC_INTERVAL,{L"30",L"60",L"90",L"120",L"144",L"200"},0);
    {wchar_t fps[32];swprintf_s(fps,L"%u",s.target_fps());SetDlgItemText(w,IDC_INTERVAL,fps);}
    CheckDlgButton(w,IDC_DIRECT2D,s.direct2d?BST_CHECKED:BST_UNCHECKED);
    CheckDlgButton(w,IDC_FOLLOW_MONITOR,s.follow_monitor?BST_CHECKED:BST_UNCHECKED);
    EnableWindow(GetDlgItem(w,IDC_INTERVAL),!s.follow_monitor);
    SetDlgItemInt(w,IDC_FALL,s.fall,FALSE);
    csp::numeric_choices(w,IDC_HOLD,s.hold,true);
    SetDlgItemInt(w,IDC_RANGE,s.range,FALSE);
    SetDlgItemInt(w,IDC_WIDTH,s.width,FALSE);
    SetDlgItemInt(w,IDC_SPACING,s.spacing,FALSE);
    SetDlgItemInt(w,IDC_PADLEFT,s.padding_left,FALSE);
    SetDlgItemInt(w,IDC_PADRIGHT,s.padding_right,FALSE);
    SetDlgItemInt(w,IDC_GAP,s.channel_gap,FALSE);
    SetDlgItemInt(w,IDC_BLUR,s.blur,FALSE);
    SendDlgItemMessage(w,IDC_SELECTED,CB_RESETCONTENT,0,0);
    for(const auto* name:csp::speaker_names)if(SendDlgItemMessage(w,IDC_SELECTED,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name))<0)throw std::runtime_error("Could not populate speaker names.");
    SendDlgItemMessage(w,IDC_SELECTED,CB_SETCURSEL,s.selected,0);
    csp::numeric_choices(w,IDC_FSHORT,s.short_frequency,false);
    csp::numeric_choices(w,IDC_FLONG,s.long_frequency,false);
    csp::numeric_choices(w,IDC_ASHORT,s.short_amplitude,false);
    csp::numeric_choices(w,IDC_ALONG,s.long_amplitude,false);
    CheckDlgButton(w,IDC_NEWFFT,s.new_fft?BST_CHECKED:BST_UNCHECKED);
    combo(w,IDC_LOGAMP,{L"Linear",L"Logarithmic"},s.log_amplitude?1:0);
    combo(w,IDC_LINEAR,{L"Logarithmic",L"Linear"},s.linear_frequency?1:0);
    CheckDlgButton(w,IDC_REVFREQ,s.reverse_frequency?BST_CHECKED:BST_UNCHECKED);
    CheckDlgButton(w,IDC_REVCHAN,s.reverse_channels?BST_CHECKED:BST_UNCHECKED);
    CheckDlgButton(w,IDC_PEAKS,s.show_peaks?BST_CHECKED:BST_UNCHECKED);
    CheckDlgButton(w,IDC_SEGMENTED,s.segmented?BST_CHECKED:BST_UNCHECKED);
    CheckDlgButton(w,IDC_FTICKS,s.frequency_ticks?BST_CHECKED:BST_UNCHECKED);
    CheckDlgButton(w,IDC_ATICKS,s.amplitude_ticks?BST_CHECKED:BST_UNCHECKED);
    CheckDlgButton(w,IDC_ALLCHANNELS,s.all_channels?BST_CHECKED:BST_UNCHECKED);
    {wchar_t text[16];swprintf_s(text,L"%06X",s.foreground);SetDlgItemText(w,IDC_FG,text);}
    {wchar_t text[16];swprintf_s(text,L"%06X",s.foreground2);SetDlgItemText(w,IDC_FG2,text);}
    {wchar_t text[16];swprintf_s(text,L"%06X",s.background);SetDlgItemText(w,IDC_BG,text);}
    {wchar_t text[16];swprintf_s(text,L"%06X",s.peak_color);SetDlgItemText(w,IDC_PEAKCOLOR,text);}
    {wchar_t text[16];swprintf_s(text,L"%06X",s.tick_color);SetDlgItemText(w,IDC_TICKCOLOR,text);}
    CheckDlgButton(w,IDC_TRANSPARENT,s.transparent?BST_CHECKED:BST_UNCHECKED);
    CheckDlgButton(w,IDC_FASTTRANS,s.fast_transparent?BST_CHECKED:BST_UNCHECKED);
    CheckDlgButton(w,IDC_SCANLINE,s.scanline_sync?BST_CHECKED:BST_UNCHECKED);
    CheckDlgButton(w,IDC_FGPALETTE,s.foreground_palette?BST_CHECKED:BST_UNCHECKED);
    CheckDlgButton(w,IDC_BGPALETTE,s.background_palette?BST_CHECKED:BST_UNCHECKED);
    {std::wstring text;for(auto color:s.foreground_colors){wchar_t item[16];swprintf_s(item,L"%06X",color);if(!text.empty())text+=L",";text+=item;}SetDlgItemText(w,IDC_FGCOLORS,text.c_str());}
    {std::wstring text;for(auto color:s.background_colors){wchar_t item[16];swprintf_s(item,L"%06X",color);if(!text.empty())text+=L",";text+=item;}SetDlgItemText(w,IDC_BGCOLORS,text.c_str());}
    csp::populate_channel_list(w,s.order,s.active_mask);
    csp::sync_slider(w,IDC_FALL,IDC_FALL_SLIDER,99);csp::sync_slider(w,IDC_BLUR,IDC_BLUR_SLIDER,100);
    csp::update_setting_dependencies(w);
}
// An invalid draft must remain editable even if its feature was just disabled.
// Keep the feature checkbox and all saved values unchanged.
void focus_invalid_setting(HWND w,int id){
    const int page=csp::settings_page(id);
    TabCtrl_SetCurSel(GetDlgItem(w,IDC_SETTINGS_TAB),page);csp::show_settings_page(w,page);
    EnableWindow(GetDlgItem(w,id),TRUE);SetFocus(GetDlgItem(w,id));
}
bool read_settings(HWND w,csp::Settings& s){
    wchar_t fpsText[64]{};GetDlgItemText(w,IDC_INTERVAL,fpsText,64);
    const unsigned fps=csp::parse_target_fps(fpsText);
    const bool follow=IsDlgButtonChecked(w,IDC_FOLLOW_MONITOR)==BST_CHECKED;
    if(!fps && !follow){
        focus_invalid_setting(w,IDC_INTERVAL);return false;
    }
    // Retain the independent legacy interval used by peak animation.
    if(fps)s.fps_milli=fps*1000;
    s.follow_monitor=follow;
    s.direct2d=IsDlgButtonChecked(w,IDC_DIRECT2D)==BST_CHECKED;
    struct Field {int id;unsigned* value;unsigned low,high;};
    Field fields[]={
        {IDC_FALL,&s.fall,1,99},
        {IDC_RANGE,&s.range,1,1000},
        {IDC_WIDTH,&s.width,1,8},
        {IDC_SPACING,&s.spacing,0,8},
        {IDC_PADLEFT,&s.padding_left,0,4096},
        {IDC_PADRIGHT,&s.padding_right,0,4096},
        {IDC_GAP,&s.channel_gap,0,4096},
        {IDC_BLUR,&s.blur,0,100},
    };
    for(auto f:fields){BOOL ok;unsigned value=GetDlgItemInt(w,f.id,&ok,FALSE);
        if(!ok || value<f.low || value>f.high){focus_invalid_setting(w,f.id);return false;}*f.value=value;}
    const int selected=int(SendDlgItemMessage(w,IDC_SELECTED,CB_GETCURSEL,0,0));
    if(selected<0 || selected>=18)return false;s.selected=unsigned(selected);
    struct Choice {int id;unsigned* value;unsigned maximum;};
    for(auto choice:{Choice{IDC_HOLD,&s.hold,10000},Choice{IDC_FSHORT,&s.short_frequency,100},Choice{IDC_FLONG,&s.long_frequency,100},Choice{IDC_ASHORT,&s.short_amplitude,100},Choice{IDC_ALONG,&s.long_amplitude,100}}){
        if(!csp::read_numeric_choice(w,choice.id,choice.maximum,*choice.value)){
            focus_invalid_setting(w,choice.id);return false;
        }
    }
    s.new_fft=IsDlgButtonChecked(w,IDC_NEWFFT)==BST_CHECKED;
    s.log_amplitude=SendDlgItemMessage(w,IDC_LOGAMP,CB_GETCURSEL,0,0)==1;
    s.linear_frequency=SendDlgItemMessage(w,IDC_LINEAR,CB_GETCURSEL,0,0)==1;
    s.reverse_frequency=IsDlgButtonChecked(w,IDC_REVFREQ)==BST_CHECKED;
    s.reverse_channels=IsDlgButtonChecked(w,IDC_REVCHAN)==BST_CHECKED;
    s.show_peaks=IsDlgButtonChecked(w,IDC_PEAKS)==BST_CHECKED;
    s.segmented=IsDlgButtonChecked(w,IDC_SEGMENTED)==BST_CHECKED;
    s.frequency_ticks=IsDlgButtonChecked(w,IDC_FTICKS)==BST_CHECKED;
    s.amplitude_ticks=IsDlgButtonChecked(w,IDC_ATICKS)==BST_CHECKED;
    s.all_channels=IsDlgButtonChecked(w,IDC_ALLCHANNELS)==BST_CHECKED;
    s.fft=512u<<std::clamp(int(SendDlgItemMessage(w,IDC_FFT,CB_GETCURSEL,0,0)),0,4);
    s.style=static_cast<csp::Style>(SendDlgItemMessage(w,IDC_STYLE,CB_GETCURSEL,0,0));
    s.direction=unsigned(SendDlgItemMessage(w,IDC_DIRECTION,CB_GETCURSEL,0,0));
    s.rotation=unsigned(SendDlgItemMessage(w,IDC_ROTATION,CB_GETCURSEL,0,0));
    s.channel_mode=unsigned(SendDlgItemMessage(w,IDC_CHANNELMODE,CB_GETCURSEL,0,0));
    s.edge=unsigned(SendDlgItemMessage(w,IDC_EDGE,CB_GETCURSEL,0,0));
    {wchar_t text[64];GetDlgItemText(w,IDC_FG,text,64);wchar_t* end=nullptr;
      auto value=wcstoul(text,&end,16);if(end==text || *end || value>0xffffff){focus_invalid_setting(w,IDC_FG);return false;}s.foreground=value;}
    {wchar_t text[64];GetDlgItemText(w,IDC_FG2,text,64);wchar_t* end=nullptr;
      auto value=wcstoul(text,&end,16);if(end==text || *end || value>0xffffff){focus_invalid_setting(w,IDC_FG2);return false;}s.foreground2=value;}
    {wchar_t text[64];GetDlgItemText(w,IDC_BG,text,64);wchar_t* end=nullptr;
      auto value=wcstoul(text,&end,16);if(end==text || *end || value>0xffffff){focus_invalid_setting(w,IDC_BG);return false;}s.background=value;}
    {wchar_t text[64];GetDlgItemText(w,IDC_PEAKCOLOR,text,64);wchar_t* end=nullptr;
      auto value=wcstoul(text,&end,16);if(end==text || *end || value>0xffffff){focus_invalid_setting(w,IDC_PEAKCOLOR);return false;}s.peak_color=value;}
    {wchar_t text[64];GetDlgItemText(w,IDC_TICKCOLOR,text,64);wchar_t* end=nullptr;
      auto value=wcstoul(text,&end,16);if(end==text || *end || value>0xffffff){focus_invalid_setting(w,IDC_TICKCOLOR);return false;}s.tick_color=value;}
    if(!csp::channel_order(w,s.order))return false;
    s.transparent=IsDlgButtonChecked(w,IDC_TRANSPARENT)==BST_CHECKED;
    s.fast_transparent=IsDlgButtonChecked(w,IDC_FASTTRANS)==BST_CHECKED;
    s.scanline_sync=IsDlgButtonChecked(w,IDC_SCANLINE)==BST_CHECKED;
    s.foreground_palette=IsDlgButtonChecked(w,IDC_FGPALETTE)==BST_CHECKED;
    s.background_palette=IsDlgButtonChecked(w,IDC_BGPALETTE)==BST_CHECKED;
    {wchar_t buffer[256];GetDlgItemText(w,IDC_FGCOLORS,buffer,256);std::wstring paletteText=buffer;std::replace(paletteText.begin(),paletteText.end(),L',',L' ');std::wistringstream stream(paletteText);stream>>std::hex;
        for(auto& color:s.foreground_colors){if(!(stream>>color) || color>0xffffff)return false;}stream>>std::ws;if(!stream.eof())return false;}
    {wchar_t buffer[256];GetDlgItemText(w,IDC_BGCOLORS,buffer,256);std::wstring paletteText=buffer;std::replace(paletteText.begin(),paletteText.end(),L',',L' ');std::wistringstream stream(paletteText);stream>>std::hex;
        for(auto& color:s.background_colors){if(!(stream>>color) || color>0xffffff)return false;}stream>>std::ws;if(!stream.eof())return false;}
    s.active_mask=csp::enabled_channels(w);
    s.sanitize();return true;
}
bool preset_file(HWND w,csp::Settings& s,bool writing,DialogState* dialog=nullptr){
    wchar_t path[MAX_PATH]=L"";OPENFILENAME ofn{sizeof(ofn)};ofn.hwndOwner=w;
    ofn.lpstrFilter=writing?L"Mirror preset (*.csm)\0*.csm\0Original 0.17.2 preset (*.csp)\0*.csp\0\0":L"Spectrum presets (*.csm;*.csp)\0*.csm;*.csp\0\0";ofn.lpstrFile=path;ofn.nMaxFile=MAX_PATH;
    ofn.lpstrDefExt=L"csm";ofn.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|(writing?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
    if(!(writing?GetSaveFileName(&ofn):GetOpenFileName(&ofn)))return false;
    if(writing){
        bool original=ofn.nFilterIndex==2 || _wcsicmp(std::filesystem::path(path).extension().c_str(),L".csp")==0;
        auto words=csp::encode(s);
        auto exportSettings=s;
        if(original && s.follow_monitor){const auto monitor=(dialog && dialog->monitor_events?dialog->monitor_events->current():csp::MonitorRefresh{});exportSettings.fps_milli=unsigned(std::lround(csp::effective_refresh(dialog && dialog->monitor_events && dialog->monitor_events->tracking_available(),s.target_fps(),monitor)))*1000;}
        if(original && !csp::encode_original_csp(exportSettings,words))throw std::runtime_error("This added style has no original equivalent. Export .csm instead.");
        std::vector<unsigned char> bytes;
        if(!original)bytes={'C','S','M','X'};
        for(uint32_t word:words)for(int i=0;i<4;++i)bytes.push_back(static_cast<unsigned char>(word>>(i*8)));
        if(!dialog)throw std::runtime_error("Missing save dialog state.");
        if(!SetTimer(w,2,100,nullptr))throw std::runtime_error("Could not monitor background save.");
        try {dialog->save=csp::start_preset_save(std::filesystem::path(path),std::move(bytes));}
        catch(...){KillTimer(w,2);throw;}
        EnableWindow(GetDlgItem(w,IDC_EXPORT),FALSE);
        SetDlgItemText(w,IDC_EXPORT,L"Saving...");
    }else{std::ifstream file(std::filesystem::path(path),std::ios::binary|std::ios::ate);auto length=file.tellg();
        if(length<4 || length>4096 || int(length)%4)throw std::runtime_error("Invalid preset size.");
        file.seekg(0);std::vector<unsigned char> bytes(static_cast<size_t>(length));file.read(reinterpret_cast<char*>(bytes.data()),bytes.size());
        if(!file)throw std::runtime_error("Could not read preset.");
        if(!csp::decode_preset(bytes,s))throw std::runtime_error("Unsupported or truncated preset; the current settings are unchanged.");
    }return true;
}
INT_PTR CALLBACK settings_proc(HWND w,UINT msg,WPARAM wp,LPARAM lp){
    auto* state=reinterpret_cast<DialogState*>(GetWindowLongPtr(w,DWLP_USER));
    try{
        if(msg==WM_INITDIALOG){state=reinterpret_cast<DialogState*>(lp);SetWindowLongPtr(w,DWLP_USER,lp);csp::initialize_numeric_controls(w);populate(w,state->value);csp::initialize_settings_tabs(w);state->heading_font.initialize(w);if(state->monitor_events)state->monitor_events->attach_dialog(w);if(state->monitor_fallback && state->monitor_events)state->monitor_events->set_polling_active(SetTimer(w,3,5000,nullptr)!=0);update_monitor_label(w,*state);return TRUE;}
        if(msg==WM_HSCROLL){const int id=GetDlgCtrlID(reinterpret_cast<HWND>(lp));
            if(id==IDC_FALL_SLIDER || id==IDC_BLUR_SLIDER){SetDlgItemInt(w,id==IDC_FALL_SLIDER?IDC_FALL:IDC_BLUR,unsigned(SendMessage(reinterpret_cast<HWND>(lp),TBM_GETPOS,0,0)),FALSE);return TRUE;}
        }
        if(msg==WM_DRAWITEM && csp::draw_color_button(w,*reinterpret_cast<DRAWITEMSTRUCT*>(lp)))return TRUE;
        if(msg==WM_NOTIFY && reinterpret_cast<NMHDR*>(lp)->idFrom==IDC_SETTINGS_TAB && reinterpret_cast<NMHDR*>(lp)->code==TCN_SELCHANGE){
            csp::show_settings_page(w,TabCtrl_GetCurSel(GetDlgItem(w,IDC_SETTINGS_TAB)));return TRUE;
        }
        if(msg==csp::MonitorEvents::changed_message && state){update_monitor_label(w,*state);return TRUE;}
        if(msg==WM_TIMER && wp==3 && state && state->monitor_fallback){if(state->monitor_events)state->monitor_events->refresh(true);return TRUE;}
        if(msg==WM_DESTROY){
            KillTimer(w,3);
            if(state && state->monitor_events)state->monitor_events->detach_dialog(w);
            KillTimer(w,2);if(state)state->save.reset();SetWindowLongPtr(w,DWLP_USER,0);return FALSE;
        }
        if(msg==WM_TIMER && wp==2 && state && state->save){
            const auto status=state->save->status.load(std::memory_order_acquire);
            if(status!=csp::SaveStatus::running){
                auto result=std::move(state->save);KillTimer(w,2);
                EnableWindow(GetDlgItem(w,IDC_EXPORT),TRUE);SetDlgItemText(w,IDC_EXPORT,L"Export...");
                if(status==csp::SaveStatus::failed)MessageBoxA(w,result->error,"Preset save failed",MB_OK|MB_ICONERROR);
            }
            return TRUE;
        }
        if(msg==WM_COMMAND && state){auto id=LOWORD(wp);
            if(id==IDC_INTERVAL && (HIWORD(wp)==CBN_EDITCHANGE || HIWORD(wp)==CBN_SELCHANGE)){PostMessage(w,csp::MonitorEvents::changed_message,0,0);return TRUE;}
            if(id==IDC_FOLLOW_MONITOR){update_monitor_label(w,*state);csp::update_setting_dependencies(w);return TRUE;}
            if(id>=IDC_COLOR_FIRST && id<=IDC_COLOR_LAST && HIWORD(wp)==BN_CLICKED){csp::choose_setting_color(w,id,state->custom_colors);return TRUE;}
            if(HIWORD(wp)==EN_CHANGE){
                if(id==IDC_FALL)csp::sync_slider(w,IDC_FALL,IDC_FALL_SLIDER,99);
                if(id==IDC_BLUR)csp::sync_slider(w,IDC_BLUR,IDC_BLUR_SLIDER,100);
                csp::refresh_color_buttons(w);return TRUE;
            }
            if(id==IDC_ORDER_UP || id==IDC_ORDER_DOWN){csp::move_channel(w,id==IDC_ORDER_UP?-1:1);return TRUE;}
            if(id==IDC_ORDER_RESET){const auto mask=csp::enabled_channels(w);csp::populate_channel_list(w,csp::default_channel_order(),mask);return TRUE;}
            if(id==IDC_CHANNEL_ALL || id==IDC_CHANNEL_NONE){csp::set_enabled_channels(w,id==IDC_CHANNEL_ALL?0x3ffff:0);return TRUE;}
            if(id==IDC_FG_ADVANCED || id==IDC_BG_ADVANCED){csp::edit_palette(w,id==IDC_FG_ADVANCED?IDC_FGCOLORS:IDC_BGCOLORS);return TRUE;}
            if(HIWORD(wp)==BN_CLICKED || ((id==IDC_CHANNELMODE || id==IDC_STYLE) && HIWORD(wp)==CBN_SELCHANGE))csp::update_setting_dependencies(w);
            if(id==IDC_RESET){auto next=csp::Settings{};state->value.interval=next.interval;state->value.fps_milli=next.fps_milli;populate(w,next);return TRUE;}
            if(id==IDCANCEL){EndDialog(w,IDCANCEL);return TRUE;}
            if(id==IDC_IMPORT){auto next=state->value;if(preset_file(w,next,false)){state->value.interval=next.interval;state->value.fps_milli=next.fps_milli;populate(w,next);}return TRUE;}
            if(id==IDOK || id==IDC_APPLY || id==IDC_EXPORT){auto next=state->value;
                if(!read_settings(w,next)){MessageBox(w,L"Target FPS must be a whole number from 10 to 200. Choose a listed value or type a number for custom hold time / tick lengths. The invalid field is selected and enabled for correction. Check numeric ranges and RGB colors.",L"Invalid settings",MB_OK|MB_ICONWARNING);return TRUE;}
                if(id==IDC_EXPORT){if(!state->save)preset_file(w,next,true,state);return TRUE;}
                state->apply(next);state->value=next;state->changed=true;if(id==IDOK)EndDialog(w,IDOK);return TRUE;
            }
        }
    }catch(const std::exception& e){MessageBoxA(w,e.what(),"Channel Spectrum Mirror",MB_OK|MB_ICONERROR);return TRUE;}
    return FALSE;
}
}
