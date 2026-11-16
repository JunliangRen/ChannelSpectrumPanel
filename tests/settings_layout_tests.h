#pragma once
#include "../src/settings_layout.h"
#include "../src/color_picker.h"
#include "../src/channel_selection.h"
#include "../src/settings_controls.h"
#include "../src/original_config.h"
inline INT_PTR CALLBACK layout_test_proc(HWND,UINT,WPARAM,LPARAM){return FALSE;}
inline void settings_layout_tests(){
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_TAB_CLASSES|ICC_LISTVIEW_CLASSES|ICC_BAR_CLASSES|ICC_UPDOWN_CLASS};
    check(InitCommonControlsEx(&controls)!=FALSE,"tab common controls available");
    HWND dialog=CreateDialogParam(GetModuleHandle(nullptr),MAKEINTRESOURCE(IDD_SETTINGS),nullptr,layout_test_proc,0);
    check(dialog!=nullptr,"settings resource creates a hidden native dialog");
    csp::SettingsHeadingFont heading;
    struct Cleanup {HWND window;~Cleanup(){DestroyWindow(window);}} cleanup{dialog};
    csp::initialize_numeric_controls(dialog);
    csp::initialize_settings_tabs(dialog);heading.initialize(dialog);
    check(TabCtrl_GetItemCount(GetDlgItem(dialog,IDC_SETTINGS_TAB))==5,"five settings tabs");
    check(csp::rgb_to_colorref(0xff0000)==RGB(255,0,0),"picker red does not become blue");
    check(csp::colorref_to_rgb(RGB(0,0,255))==0x0000ff,"picker blue converts to RGB hex");
    for(unsigned color:{0u,0xffffffu,0x123456u,0xe4e4e4u})check(csp::colorref_to_rgb(csp::rgb_to_colorref(color))==color,"picker color conversion round trip");
    unsigned colors[8]{};
    SetDlgItemText(dialog,IDC_FGCOLORS,L"FF0000,00FF00,0000FF,123456,abcdef,000000,FFFFFF,808080");
    check(csp::read_color_values(dialog,IDC_FGCOLORS,colors,8) && colors[0]==0xff0000 && colors[2]==255 && colors[4]==0xabcdef && colors[7]==0x808080,"picker preserves palette order and hex values");
    for(const wchar_t* invalid:{L"",L"GG0000",L"1000000",L"123456,",L"-1"}){
        SetDlgItemText(dialog,IDC_FG,invalid);check(!csp::read_color_values(dialog,IDC_FG,colors,1),"picker rejects invalid color input");
    }
    SetDlgItemText(dialog,IDC_FGCOLORS,L"FF0000,00FF00");
    check(!csp::read_color_values(dialog,IDC_FGCOLORS,colors,8),"picker rejects incomplete palettes");
    for(int id=IDC_COLOR_FIRST;id<=IDC_COLOR_LAST;++id)check(GetDlgItem(dialog,csp::color_field(id))!=nullptr,"every swatch maps to an existing color field");
    for(unsigned bit=0;bit<18;++bit){
        csp::set_enabled_channels(dialog,1u<<bit);
        check(csp::enabled_channels(dialog)==(1u<<bit),"each speaker checkbox maps to its original mask bit");
    }
    for(unsigned mask:{0u,3u,0x3fu,0x60fu,0x15555u,0x3ffffu}){
        csp::Settings source;source.style=csp::Style::Simple;source.active_mask=mask;
        std::vector<uint32_t> original;csp::encode_original_csp(source,original);
        csp::Settings imported;check(csp::decode_original_csp(original,imported),"original channel preset loads");
        csp::set_enabled_channels(dialog,imported.active_mask);
        check(csp::enabled_channels(dialog)==mask,"original preset populates correct speaker checkboxes");
        auto old=csp::encode(source);old[0]=3;old.resize(77);
        check(csp::decode(old,imported),"old Mirror channel preset loads");
        csp::set_enabled_channels(dialog,imported.active_mask);
        source.active_mask=csp::enabled_channels(dialog);
        check(source.active_mask==mask && csp::decode(csp::encode(source),imported) && imported.active_mask==mask,"checkbox selection preserves old and new preset masks");
    }
    csp::set_enabled_channels(dialog,csp::Settings{}.active_mask);
    check(csp::enabled_channels(dialog)==0x3ffff,"defaults enable all 18 speakers");
    std::vector<int> ids;
    for(HWND child=GetWindow(dialog,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)){
        const int id=GetDlgCtrlID(child);
        check(std::find(ids.begin(),ids.end(),id)==ids.end(),"settings resource has unique child IDs");ids.push_back(id);
        const bool global=id==IDC_LANGUAGE || id==IDC_LANGUAGE_LABEL || id==IDC_LANGUAGE_NOTE || id==IDC_SETTINGS_TAB || id==IDC_RESET || id==IDC_IMPORT || id==IDC_EXPORT || id==IDC_APPLY || id==IDOK || id==IDCANCEL || id==IDC_FGCOLORS || id==IDC_BGCOLORS;
        check(global || csp::settings_page(id)>=0,"every label and control belongs to a page or the common footer");
    }
    auto isGroup=[](HWND child){wchar_t type[32]{};GetClassName(child,type,32);return wcscmp(type,L"Button")==0 && (GetWindowLongPtr(child,GWL_STYLE)&BS_TYPEMASK)==BS_GROUPBOX;};
    int groupCount=0;
    for(HWND child=GetWindow(dialog,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))if(isGroup(child)){
        ++groupCount;check(!(GetWindowLongPtr(child,GWL_STYLE)&WS_TABSTOP),"group boxes do not add keyboard tab stops");
    }
    check(groupCount==11,"exactly eleven native group boxes");
    RECT content{12,26,414,248};MapDialogRect(dialog,&content);
    for(int page=0;page<5;++page){
        csp::show_settings_page(dialog,page);
        std::vector<std::pair<int,RECT>> occupied;
        for(HWND child=GetWindow(dialog,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)){
            int owner=csp::settings_page(GetDlgCtrlID(child));if(owner<0)continue;
            check(bool(GetWindowLongPtr(child,GWL_STYLE)&WS_VISIBLE)==(owner==page),"only active page controls are visible");
            if(owner!=page)continue;
            RECT rect{};GetWindowRect(child,&rect);MapWindowPoints(nullptr,dialog,reinterpret_cast<POINT*>(&rect),2);
            if(!(rect.left>=content.left && rect.top>=content.top && rect.right<=content.right && rect.bottom<=content.bottom))std::cerr<<"Outside settings content: "<<GetDlgCtrlID(child)<<" rect "<<rect.left<<","<<rect.top<<","<<rect.right<<","<<rect.bottom<<"\n";
            check(rect.left>=content.left && rect.top>=content.top && rect.right<=content.right && rect.bottom<=content.bottom,"settings control fits inside tab content");
            for(const auto& previous:occupied){RECT overlap{};
                const bool intersects=IntersectRect(&overlap,&rect,&previous.second)!=FALSE;
                const bool currentGroup=isGroup(child),previousGroup=isGroup(GetDlgItem(dialog,previous.first));
                const RECT& outer=currentGroup?rect:previous.second;const RECT& inner=currentGroup?previous.second:rect;
                const bool containment=currentGroup!=previousGroup && inner.left>=outer.left && inner.top>=outer.top && inner.right<=outer.right && inner.bottom<=outer.bottom;
                const bool collision=intersects && overlap.right-overlap.left>2 && overlap.bottom-overlap.top>2 && !containment;
                if(collision)std::cerr<<"Settings overlap: "<<previous.first<<" and "<<GetDlgCtrlID(child)<<"\n";
                check(!collision,"same-page settings controls do not overlap");
            }
            if(!isGroup(child)){
                int containers=0;
                for(HWND group=GetWindow(dialog,GW_CHILD);group;group=GetWindow(group,GW_HWNDNEXT))if(isGroup(group) && csp::settings_page(GetDlgCtrlID(group))==page){
                    RECT bounds{};GetWindowRect(group,&bounds);MapWindowPoints(nullptr,dialog,reinterpret_cast<POINT*>(&bounds),2);
                    RECT inset{4,14,4,4};MapDialogRect(dialog,&inset);
                    if(rect.left>=bounds.left+inset.left && rect.top>=bounds.top+inset.top && rect.right<=bounds.right-inset.right && rect.bottom<=bounds.bottom-inset.bottom)++containers;
                }
                if(containers!=1)std::cerr<<"Invalid group membership: "<<GetDlgCtrlID(child)<<"\n";
                check(containers==1,"each page control fits one group with title and border clearance");
            }
            occupied.push_back({GetDlgCtrlID(child),rect});
            wchar_t type[32]{},text[256]{};GetClassName(child,type,32);GetWindowText(child,text,256);
            if(wcscmp(type,L"Static")!=0 && wcscmp(type,L"Button")!=0)continue;
            if((GetWindowLongPtr(child,GWL_STYLE)&BS_TYPEMASK)==BS_OWNERDRAW && wcscmp(type,L"Button")==0)continue;
            HDC dc=GetDC(child);auto previous=SelectObject(dc,reinterpret_cast<HFONT>(SendMessage(child,WM_GETFONT,0,0)));
            SIZE size{};GetTextExtentPoint32(dc,text,int(wcslen(text)),&size);SelectObject(dc,previous);ReleaseDC(child,dc);
            RECT checkPadding{0,0,12,0};MapDialogRect(dialog,&checkPadding);
            const int padding=wcscmp(type,L"Button")==0?checkPadding.right:0;
            if(size.cx+padding>rect.right-rect.left)std::wcerr<<L"Clipped settings label: "<<text<<L"\n";
            check(size.cx+padding<=rect.right-rect.left,"settings label fits its control");
        }
    }
}
