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
namespace {
struct DialogState { csp::Settings value; std::function<void(const csp::Settings&)> apply; bool changed=false; std::shared_ptr<csp::SaveResult> save; };
void combo(HWND w,int id,std::initializer_list<const wchar_t*> labels,unsigned selected){
    SendDlgItemMessage(w,id,CB_RESETCONTENT,0,0);
    for(auto label:labels)SendDlgItemMessage(w,id,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
    SendDlgItemMessage(w,id,CB_SETCURSEL,selected,0);
}
void populate(HWND w,const csp::Settings& s){
    unsigned index=0;for(unsigned n=512;n<s.fft;n*=2)++index;
    combo(w,IDC_FFT,{L"512",L"1024",L"2048",L"4096",L"8192"},index);
    combo(w,IDC_STYLE,{L"Gradient bars (v0.1)",L"Simple-line + peaks",L"Separate thin lines",L"Simple",L"Horizontal",L"Vertical",L"Simple-antialiasing",L"Horizontal-antialiasing"},unsigned(s.style));
    combo(w,IDC_DIRECTION,{L"Mirror",L"Right",L"Left"},s.direction);
    combo(w,IDC_ROTATION,{L"0",L"90",L"180",L"270"},s.rotation);
    combo(w,IDC_CHANNELMODE,{L"Separate",L"Merge all (maximum)",L"Specified input"},s.channel_mode);
    combo(w,IDC_EDGE,{L"None",L"Sunken",L"Static"},s.edge);
    SetDlgItemInt(w,IDC_INTERVAL,s.interval,FALSE);
    SetDlgItemInt(w,IDC_FALL,s.fall,FALSE);
    SetDlgItemInt(w,IDC_HOLD,s.hold,FALSE);
    SetDlgItemInt(w,IDC_RANGE,s.range,FALSE);
    SetDlgItemInt(w,IDC_WIDTH,s.width,FALSE);
    SetDlgItemInt(w,IDC_SPACING,s.spacing,FALSE);
    SetDlgItemInt(w,IDC_PADLEFT,s.padding_left,FALSE);
    SetDlgItemInt(w,IDC_PADRIGHT,s.padding_right,FALSE);
    SetDlgItemInt(w,IDC_GAP,s.channel_gap,FALSE);
    SetDlgItemInt(w,IDC_BLUR,s.blur,FALSE);
    SetDlgItemInt(w,IDC_SELECTED,s.selected+1,FALSE);
    SetDlgItemInt(w,IDC_FSHORT,s.short_frequency,FALSE);
    SetDlgItemInt(w,IDC_FLONG,s.long_frequency,FALSE);
    SetDlgItemInt(w,IDC_ASHORT,s.short_amplitude,FALSE);
    SetDlgItemInt(w,IDC_ALONG,s.long_amplitude,FALSE);
    CheckDlgButton(w,IDC_NEWFFT,s.new_fft?BST_CHECKED:BST_UNCHECKED);
    CheckDlgButton(w,IDC_LOGAMP,s.log_amplitude?BST_CHECKED:BST_UNCHECKED);
    CheckDlgButton(w,IDC_LINEAR,s.linear_frequency?BST_CHECKED:BST_UNCHECKED);
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
    {wchar_t mask[16];swprintf_s(mask,L"%05X",s.active_mask);SetDlgItemText(w,IDC_ACTIVEMASK,mask);}
    std::wstring order;for(unsigned n:s.order){if(!order.empty())order+=L",";order+=std::to_wstring(n+1);}SetDlgItemText(w,IDC_ORDER,order.c_str());
}
bool read_settings(HWND w,csp::Settings& s){
    struct Field {int id;unsigned* value;unsigned low,high;};
    Field fields[]={
        {IDC_INTERVAL,&s.interval,5,100},
        {IDC_FALL,&s.fall,1,99},
        {IDC_HOLD,&s.hold,0,10000},
        {IDC_RANGE,&s.range,1,1000},
        {IDC_WIDTH,&s.width,1,8},
        {IDC_SPACING,&s.spacing,0,8},
        {IDC_PADLEFT,&s.padding_left,0,4096},
        {IDC_PADRIGHT,&s.padding_right,0,4096},
        {IDC_GAP,&s.channel_gap,0,4096},
        {IDC_BLUR,&s.blur,0,100},
        {IDC_SELECTED,&s.selected,1,18},
        {IDC_FSHORT,&s.short_frequency,0,100},
        {IDC_FLONG,&s.long_frequency,0,100},
        {IDC_ASHORT,&s.short_amplitude,0,100},
        {IDC_ALONG,&s.long_amplitude,0,100},
    };
    for(auto f:fields){BOOL ok;unsigned value=GetDlgItemInt(w,f.id,&ok,FALSE);
        if(!ok || value<f.low || value>f.high){SetFocus(GetDlgItem(w,f.id));return false;}*f.value=value;}
    --s.selected;
    s.new_fft=IsDlgButtonChecked(w,IDC_NEWFFT)==BST_CHECKED;
    s.log_amplitude=IsDlgButtonChecked(w,IDC_LOGAMP)==BST_CHECKED;
    s.linear_frequency=IsDlgButtonChecked(w,IDC_LINEAR)==BST_CHECKED;
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
      auto value=wcstoul(text,&end,16);if(end==text || *end || value>0xffffff)return false;s.foreground=value;}
    {wchar_t text[64];GetDlgItemText(w,IDC_FG2,text,64);wchar_t* end=nullptr;
      auto value=wcstoul(text,&end,16);if(end==text || *end || value>0xffffff)return false;s.foreground2=value;}
    {wchar_t text[64];GetDlgItemText(w,IDC_BG,text,64);wchar_t* end=nullptr;
      auto value=wcstoul(text,&end,16);if(end==text || *end || value>0xffffff)return false;s.background=value;}
    {wchar_t text[64];GetDlgItemText(w,IDC_PEAKCOLOR,text,64);wchar_t* end=nullptr;
      auto value=wcstoul(text,&end,16);if(end==text || *end || value>0xffffff)return false;s.peak_color=value;}
    {wchar_t text[64];GetDlgItemText(w,IDC_TICKCOLOR,text,64);wchar_t* end=nullptr;
      auto value=wcstoul(text,&end,16);if(end==text || *end || value>0xffffff)return false;s.tick_color=value;}
    wchar_t order[256];GetDlgItemText(w,IDC_ORDER,order,256);std::wstring text=order;
    std::replace(text.begin(),text.end(),L',',L' ');std::wistringstream input(text);std::array<bool,18> seen{};
    for(auto& n:s.order){unsigned value;if(!(input>>value) || value<1 || value>18 || seen[value-1])return false;n=value-1;seen[n]=true;}
    input>>std::ws;if(!input.eof())return false;
    s.transparent=IsDlgButtonChecked(w,IDC_TRANSPARENT)==BST_CHECKED;
    s.fast_transparent=IsDlgButtonChecked(w,IDC_FASTTRANS)==BST_CHECKED;
    s.scanline_sync=IsDlgButtonChecked(w,IDC_SCANLINE)==BST_CHECKED;
    s.foreground_palette=IsDlgButtonChecked(w,IDC_FGPALETTE)==BST_CHECKED;
    s.background_palette=IsDlgButtonChecked(w,IDC_BGPALETTE)==BST_CHECKED;
    {wchar_t buffer[256];GetDlgItemText(w,IDC_FGCOLORS,buffer,256);std::wstring paletteText=buffer;std::replace(paletteText.begin(),paletteText.end(),L',',L' ');std::wistringstream stream(paletteText);stream>>std::hex;
        for(auto& color:s.foreground_colors){if(!(stream>>color) || color>0xffffff)return false;}stream>>std::ws;if(!stream.eof())return false;}
    {wchar_t buffer[256];GetDlgItemText(w,IDC_BGCOLORS,buffer,256);std::wstring paletteText=buffer;std::replace(paletteText.begin(),paletteText.end(),L',',L' ');std::wistringstream stream(paletteText);stream>>std::hex;
        for(auto& color:s.background_colors){if(!(stream>>color) || color>0xffffff)return false;}stream>>std::ws;if(!stream.eof())return false;}
    {wchar_t mask[32];GetDlgItemText(w,IDC_ACTIVEMASK,mask,32);wchar_t* end=nullptr;auto value=wcstoul(mask,&end,16);if(end==mask || *end || value>0x3ffff)return false;s.active_mask=value;}
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
        if(original && !csp::encode_original_csp(s,words))throw std::runtime_error("This added style has no original equivalent. Export .csm instead.");
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
        if(msg==WM_INITDIALOG){state=reinterpret_cast<DialogState*>(lp);SetWindowLongPtr(w,DWLP_USER,lp);populate(w,state->value);return TRUE;}
        if(msg==WM_DESTROY){
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
            if(id==IDC_RESET){populate(w,csp::Settings{});return TRUE;}
            if(id==IDCANCEL){EndDialog(w,IDCANCEL);return TRUE;}
            if(id==IDC_IMPORT){auto next=state->value;if(preset_file(w,next,false))populate(w,next);return TRUE;}
            if(id==IDOK || id==IDC_APPLY || id==IDC_EXPORT){auto next=state->value;
                if(!read_settings(w,next)){MessageBox(w,L"Check numeric ranges, RGB hex colors, and channel order (each number 1-18 exactly once).",L"Invalid settings",MB_OK|MB_ICONWARNING);return TRUE;}
                if(id==IDC_EXPORT){if(!state->save)preset_file(w,next,true,state);return TRUE;}
                state->apply(next);state->value=next;state->changed=true;if(id==IDOK)EndDialog(w,IDOK);return TRUE;
            }
        }
    }catch(const std::exception& e){MessageBoxA(w,e.what(),"Channel Spectrum Mirror",MB_OK|MB_ICONERROR);return TRUE;}
    return FALSE;
}
}
