#pragma once
#include "../src/settings_ui.h"
#pragma comment(lib,"comdlg32.lib")
// Drive the real modal palette editor without a human or a player process.
namespace palette_test {
inline HHOOK hook=nullptr;
inline unsigned action=0;
inline bool visited=false;
inline constexpr wchar_t draft[]=L"112233,223344,334455,445566,556677,667788,778899,8899AA";
inline LRESULT CALLBACK activate(int code,WPARAM wp,LPARAM lp){
    if(code==HCBT_ACTIVATE && GetDlgItem(reinterpret_cast<HWND>(wp),IDC_PALETTE_TEXT)){
        HWND dialog=reinterpret_cast<HWND>(wp);visited=true;
        SetDlgItemText(dialog,IDC_PALETTE_TEXT,draft);
        PostMessage(dialog,action==2?WM_CLOSE:WM_COMMAND,action==0?IDOK:IDCANCEL,0);
    }
    return CallNextHookEx(hook,code,wp,lp);
}
inline void run(HWND parent,int field,unsigned command){
    action=command;visited=false;
    hook=SetWindowsHookEx(WH_CBT,activate,nullptr,GetCurrentThreadId());
    check(hook!=nullptr,"palette modal test hook created");
    if(!hook)return;
    struct Cleanup {~Cleanup(){UnhookWindowsHookEx(hook);hook=nullptr;}} cleanup;
    csp::edit_palette(parent,field);
    check(visited,"actual modal palette editor activated and closed");
}
}
inline void settings_options_tests(){
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_TAB_CLASSES|ICC_LISTVIEW_CLASSES|ICC_BAR_CLASSES|ICC_UPDOWN_CLASS};InitCommonControlsEx(&controls);
    csp::Settings source;source.style=csp::Style::Simple;source.fps_milli=60000;source.hold=137;source.fall=27;source.range=413;
    source.width=7;source.spacing=4;source.blur=63;source.padding_left=111;source.padding_right=222;source.channel_gap=17;source.selected=15;
    source.rotation=3;source.direct2d=true;source.show_peaks=false;source.frequency_ticks=false;source.amplitude_ticks=false;
    source.short_frequency=17;source.long_frequency=73;source.short_amplitude=42;source.long_amplitude=99;
    source.transparent=false;source.fast_transparent=true;source.foreground_palette=false;source.background_palette=false;
    source.foreground_colors={0x123456,0x234567,0x345678,0x456789,0x56789a,0x6789ab,0x789abc,0x89abcd};
    source.active_mask=0x2c00;std::reverse(source.order.begin(),source.order.end());
    int applied=0;csp::Settings last;
    DialogState state{source,[&](const csp::Settings& value){++applied;last=value;}};
    HWND dialog=CreateDialogParam(GetModuleHandle(nullptr),MAKEINTRESOURCE(IDD_SETTINGS),nullptr,settings_proc,reinterpret_cast<LPARAM>(&state));
    check(dialog!=nullptr,"complete settings dialog creates without player");
    struct Cleanup{HWND window;~Cleanup(){DestroyWindow(window);}} cleanup{dialog};
    csp::Settings result=source;
    check(read_settings(dialog,result) && result==source,"all custom values survive populated UI including disabled controls");
    check(SendDlgItemMessage(dialog,IDC_SELECTED,CB_GETCOUNT,0,0)==18,"input channel exposes 18 named choices");
    check(!IsWindowEnabled(GetDlgItem(dialog,IDC_SELECTED)),"specified channel disabled in separate mode");
    check(!IsWindowEnabled(GetDlgItem(dialog,IDC_FALL)) && !IsWindowEnabled(GetDlgItem(dialog,IDC_HOLD)),"peak controls disabled together");
    check(!IsWindowEnabled(GetDlgItem(dialog,IDC_FSHORT)) && !IsWindowEnabled(GetDlgItem(dialog,IDC_ALONG)),"tick lengths disabled with ticks");
    check(!IsWindowEnabled(GetDlgItem(dialog,IDC_FASTTRANS)),"background cache disabled with transparency");
    check(!IsWindowEnabled(GetDlgItem(dialog,IDC_FG_ADVANCED)),"palette advanced editing follows palette enabled state");
    for(int page=0;page<5;++page){csp::show_settings_page(dialog,page);
        check(!(GetWindowLongPtr(GetDlgItem(dialog,IDC_FGCOLORS),GWL_STYLE)&WS_VISIBLE) && !(GetWindowLongPtr(GetDlgItem(dialog,IDC_BGCOLORS),GWL_STYLE)&WS_VISIBLE),"palette hex text stays private on all pages");}
    SendMessage(dialog,WM_COMMAND,IDC_APPLY,0);check(applied==1 && last==source,"Apply retains exact custom settings");
    auto click=[&](int id){SendMessage(dialog,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),reinterpret_cast<LPARAM>(GetDlgItem(dialog,id)));};
    // Style applicability must only affect editing, never the stored draft.
    for(unsigned style=0;style<8;++style)for(bool palette:{false,true})for(bool transparent:{false,true}){
        auto expectedSettings=source;expectedSettings.style=static_cast<csp::Style>(style);
        expectedSettings.foreground_palette=palette;expectedSettings.background_palette=true;expectedSettings.transparent=transparent;
        populate(dialog,expectedSettings);
        for(unsigned next:{(style+1)%8,style}){
            SendDlgItemMessage(dialog,IDC_STYLE,CB_SETCURSEL,next,0);
            SendMessage(dialog,WM_COMMAND,MAKEWPARAM(IDC_STYLE,CBN_SELCHANGE),0);
        }
        const bool vertical=expectedSettings.style==csp::Style::Vertical;
        const bool blur=expectedSettings.style==csp::Style::SimpleAntialias || expectedSettings.style==csp::Style::HorizontalAntialias;
        check(bool(IsWindowEnabled(GetDlgItem(dialog,IDC_BLUR)))==blur && bool(IsWindowEnabled(GetDlgItem(dialog,IDC_BLUR_SLIDER)))==blur,"style changes update blur controls together");
        check(bool(IsWindowEnabled(GetDlgItem(dialog,IDC_FGPALETTE)))==vertical,"foreground palette applicability follows Vertical style");
        for(int id:{1305,1312,IDC_FG_ADVANCED})check(bool(IsWindowEnabled(GetDlgItem(dialog,id)))==(vertical && palette),"foreground palette editors follow style and preference");
        check(bool(IsWindowEnabled(GetDlgItem(dialog,IDC_SEGMENTED)))==(expectedSettings.style!=csp::Style::SimpleLine),"line style does not offer segmented bars");
        for(int id:{IDC_BGPALETTE,1313,1320,IDC_BG_ADVANCED})check(bool(IsWindowEnabled(GetDlgItem(dialog,id)))==!transparent,"parent background disables unused background palette editors");
        result=source;check(read_settings(dialog,result) && result==expectedSettings,"style and transparency switches retain every inactive custom value");
    }
    for(bool frequency:{false,true})for(bool amplitude:{false,true}){
        auto expectedSettings=source;expectedSettings.linear_frequency=frequency;expectedSettings.log_amplitude=amplitude;
        populate(dialog,expectedSettings);result=source;
        check(read_settings(dialog,result) && result==expectedSettings,"explicit scale choices preserve legacy boolean meaning");
    }
    populate(dialog,source);SetDlgItemText(dialog,IDC_INTERVAL,L"invalid");
    check(!read_settings(dialog,result) && TabCtrl_GetCurSel(GetDlgItem(dialog,IDC_SETTINGS_TAB))==4,"invalid FPS opens Performance page");
    populate(dialog,source);SetDlgItemText(dialog,IDC_BLUR,L"invalid");
    check(!read_settings(dialog,result) && TabCtrl_GetCurSel(GetDlgItem(dialog,IDC_SETTINGS_TAB))==0 && IsWindowEnabled(GetDlgItem(dialog,IDC_BLUR)),"invalid inactive blur remains correctable on Appearance");
    populate(dialog,source);
    for(int page=0;page<5;++page){
        TabCtrl_SetCurSel(GetDlgItem(dialog,IDC_SETTINGS_TAB),page);csp::show_settings_page(dialog,page);result=source;
        check(read_settings(dialog,result) && result==source,"visiting all five pages never changes the draft");
    }
    DWORD fontsBefore=0;
    for(int cycle=0;cycle<8;++cycle){
        HFONT heading=nullptr;
        {DialogState reopened{source,[](const csp::Settings&){}};
        HWND next=CreateDialogParam(GetModuleHandle(nullptr),MAKEINTRESOURCE(IDD_SETTINGS),nullptr,settings_proc,reinterpret_cast<LPARAM>(&reopened));
        check(next!=nullptr,"five-page dialog reopens");
        heading=reinterpret_cast<HFONT>(SendDlgItemMessage(next,3000,WM_GETFONT,0,0));
        check(heading!=nullptr && heading!=reinterpret_cast<HFONT>(SendMessage(next,WM_GETFONT,0,0)),"headings use an owned emphasis font");
        check(DestroyWindow(next)!=FALSE,"five-page dialog closes before owned heading font is released");}
        GdiFlush(); // GDI may share/cache identical font handles with the still-open parent dialog.
        if(cycle==2)fontsBefore=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS); // Warm native theme/common-control caches.
    }
    check(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<=fontsBefore,"repeated settings dialogs release heading fonts and GDI resources");
    // Every mode keeps the stored list, mask, selection and all-inputs flag intact.
    for(unsigned mode=0;mode<3;++mode)for(bool all:{false,true}){
        populate(dialog,source);
        SendDlgItemMessage(dialog,IDC_CHANNELMODE,CB_SETCURSEL,mode,0);
        CheckDlgButton(dialog,IDC_ALLCHANNELS,all?BST_CHECKED:BST_UNCHECKED);
        SendMessage(dialog,WM_COMMAND,MAKEWPARAM(IDC_CHANNELMODE,CBN_SELCHANGE),0);
        const bool ordered=mode==0 && all,listed=ordered || mode==1;
        for(int id:{IDC_ORDER,IDC_CHANNEL_ALL,IDC_CHANNEL_NONE})check(bool(IsWindowEnabled(GetDlgItem(dialog,id)))==listed,"channel mask controls follow rendering mode");
        for(int id:{IDC_ORDER_UP,IDC_ORDER_DOWN,IDC_ORDER_RESET})check(bool(IsWindowEnabled(GetDlgItem(dialog,id)))==ordered,"ordering controls only enabled when order is used");
        check(bool(IsWindowEnabled(GetDlgItem(dialog,IDC_ALLCHANNELS)))==(mode==0),"all-inputs switch only enabled in separate mode");
        result=source;auto expectedSettings=source;expectedSettings.channel_mode=mode;expectedSettings.all_channels=all;
        check(read_settings(dialog,result) && result==expectedSettings,"mode dependencies preserve inactive configuration values");
    }
    // Invalid drafts survive disabling a feature and become editable on validation.
    struct InvalidCase {int field,toggle;const wchar_t* valid;};
    for(auto test:{InvalidCase{IDC_FALL,IDC_PEAKS,L"27"},InvalidCase{IDC_HOLD,IDC_PEAKS,L"137"},
        InvalidCase{IDC_FSHORT,IDC_FTICKS,L"17"},InvalidCase{IDC_FLONG,IDC_FTICKS,L"73"},
        InvalidCase{IDC_ASHORT,IDC_ATICKS,L"42"},InvalidCase{IDC_ALONG,IDC_ATICKS,L"99"},
        InvalidCase{IDC_PEAKCOLOR,IDC_PEAKS,L"F5F5F5"},InvalidCase{IDC_TICKCOLOR,IDC_FTICKS,L"808080"}}){
        populate(dialog,source);CheckDlgButton(dialog,test.toggle,BST_CHECKED);click(test.toggle);
        SetDlgItemText(dialog,test.field,L"");CheckDlgButton(dialog,test.toggle,BST_UNCHECKED);click(test.toggle);
        check(!IsWindowEnabled(GetDlgItem(dialog,test.field)),"feature disables invalid draft initially");
        result=source;check(!read_settings(dialog,result),"invalid inactive draft is not silently discarded");
        check(IsWindowEnabled(GetDlgItem(dialog,test.field)) && TabCtrl_GetCurSel(GetDlgItem(dialog,IDC_SETTINGS_TAB))==csp::settings_page(test.field),"invalid draft is enabled on the selected correction page");
        check(IsDlgButtonChecked(dialog,test.toggle)==BST_UNCHECKED,"correction does not enable the feature");
        wchar_t text[32]{};GetDlgItemText(dialog,test.field,text,32);check(!*text,"invalid draft retained unchanged");
        SetDlgItemText(dialog,test.field,test.valid);result=source;
        check(read_settings(dialog,result) && result==source,"corrected disabled field retains exact original configuration");
    }
    for(int field:{IDC_FGCOLORS,IDC_BGCOLORS})for(unsigned action:{0u,1u,2u}){
        populate(dialog,source);wchar_t before[256]{};GetDlgItemText(dialog,field,before,256);
        palette_test::run(dialog,field,action);
        wchar_t after[256]{};GetDlgItemText(dialog,field,after,256);
        check(wcscmp(after,action==0?palette_test::draft:before)==0,"palette OK applies draft; Cancel and window Close retain original");
        result=source;check(read_settings(dialog,result),"palette remains valid after modal closure");
        if(action!=0)check(result==source,"cancelled palette preserves complete settings");
        check(IsWindow(dialog) && GetWindowLongPtr(dialog,DWLP_USER)==reinterpret_cast<LONG_PTR>(&state),"parent settings state survives modal closure");
    }
    populate(dialog,source);
    CheckDlgButton(dialog,IDC_ALLCHANNELS,BST_CHECKED);click(IDC_ALLCHANNELS);
    HWND list=GetDlgItem(dialog,IDC_ORDER);ListView_SetItemState(list,4,LVIS_SELECTED,LVIS_SELECTED);
    auto expected=source.order;std::swap(expected[3],expected[4]);click(IDC_ORDER_UP);
    std::array<unsigned,18> order{};
    check(csp::channel_order(dialog,order) && order==expected && csp::enabled_channels(dialog)==source.active_mask,"moving a named channel preserves its enabled bit");
    click(IDC_ORDER_DOWN);check(csp::channel_order(dialog,order) && order==source.order,"moving channel back restores original ordering");
    click(IDC_ORDER_RESET);check(csp::channel_order(dialog,order) && order==csp::default_channel_order() && csp::enabled_channels(dialog)==source.active_mask,"reset order retains enabled channels");
    click(IDC_CHANNEL_ALL);check(csp::enabled_channels(dialog)==0x3ffff,"enable all via dialog");
    click(IDC_CHANNEL_NONE);check(csp::enabled_channels(dialog)==0,"disable all via dialog");
    SendDlgItemMessage(dialog,IDC_CHANNELMODE,CB_SETCURSEL,2,0);SendMessage(dialog,WM_COMMAND,MAKEWPARAM(IDC_CHANNELMODE,CBN_SELCHANGE),0);
    check(IsWindowEnabled(GetDlgItem(dialog,IDC_SELECTED))!=FALSE,"specified mode enables named channel selector");
    SendDlgItemMessage(dialog,IDC_SELECTED,CB_SETCURSEL,8,0);result=source;check(read_settings(dialog,result) && result.selected==8,"named speaker maps to original zero-based position");
    CheckDlgButton(dialog,IDC_PEAKS,BST_CHECKED);click(IDC_PEAKS);
    check(IsWindowEnabled(GetDlgItem(dialog,IDC_FALL)) && IsWindowEnabled(GetDlgItem(dialog,IDC_HOLD)),"enabling peaks restores related controls");
    SendDlgItemMessage(dialog,IDC_FALL_SLIDER,TBM_SETPOS,TRUE,61);SendMessage(dialog,WM_HSCROLL,TB_THUMBPOSITION,reinterpret_cast<LPARAM>(GetDlgItem(dialog,IDC_FALL_SLIDER)));
    check(GetDlgItemInt(dialog,IDC_FALL,nullptr,FALSE)==61,"fall slider updates exact numeric value");
    SetDlgItemInt(dialog,IDC_BLUR,37,FALSE);check(SendDlgItemMessage(dialog,IDC_BLUR_SLIDER,TBM_GETPOS,0,0)==37,"numeric blur updates slider");
    for(unsigned value:{0u,1u,3u,5u,7u,15u,17u,73u,99u,100u}){
        csp::numeric_choices(dialog,IDC_FSHORT,value,false);unsigned read=999;
        check(csp::read_numeric_choice(dialog,IDC_FSHORT,100,read) && read==value,"preset or custom tick value remains exact");
    }
    for(unsigned value:{0u,1u,100u,137u,250u,500u,9999u,10000u}){
        csp::numeric_choices(dialog,IDC_HOLD,value,true);unsigned read=999;
        check(csp::read_numeric_choice(dialog,IDC_HOLD,10000,read) && read==value,"preset or custom hold time remains exact");
    }
    SetDlgItemText(dialog,IDC_HOLD,L"278");unsigned custom=0;
    check(csp::read_numeric_choice(dialog,IDC_HOLD,10000,custom) && custom==278,"editable choice accepts arbitrary exact value");
    for(const auto* invalid:{L"-1",L"101",L"3.5",L"garbage",L""}){SetDlgItemText(dialog,IDC_FSHORT,invalid);check(!csp::read_numeric_choice(dialog,IDC_FSHORT,100,custom),"invalid custom tick lengths rejected");}
    for(bool original:{false,true}){
        std::vector<uint32_t> words;
        if(original)csp::encode_original_csp(source,words);else{words=csp::encode(source);words[0]=3;words.resize(77);}
        csp::Settings imported;
        check(original?csp::decode_original_csp(words,imported):csp::decode(words,imported),"legacy custom preset decodes");
        populate(dialog,imported);auto expectedSettings=imported;expectedSettings.fps_milli=expectedSettings.target_fps()*1000;
        result=imported;check(read_settings(dialog,result) && result==expectedSettings,"legacy custom preset survives new controls without value loss");
    }
}
