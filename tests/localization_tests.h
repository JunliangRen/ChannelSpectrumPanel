#pragma once
inline std::wstring control_text(HWND dialog,int id){
    HWND control=GetDlgItem(dialog,id);const int n=GetWindowTextLength(control);std::wstring text(size_t(n)+1,L'\0');GetWindowText(control,text.data(),n+1);text.resize(n);return text;
}
inline void localization_tests(){
    for(const auto& caption:csp::settings_captions)for(unsigned language:{1u,2u}){
        bool found=false;for(const auto& row:csp::translations)if(wcscmp(row.en,caption.text)==0)found=true;
        check(found,"every resource caption has Chinese and Japanese translations");
    }
    csp::Settings source;source.fps_milli=60000;source.hold=137;source.short_frequency=17;source.long_amplitude=73;source.blur=63;source.fall=27;source.range=413;
    source.style=csp::Style::Vertical;source.foreground_palette=true;source.background_palette=true;source.active_mask=0x2c00;
    std::reverse(source.order.begin(),source.order.end());
    unsigned saved=0;int writes=0,applied=0;
    DialogState state{source,[&](const csp::Settings&){++applied;}};state.save_language=[&](unsigned language){saved=language;++writes;};
    HWND dialog=CreateDialogParam(GetModuleHandle(nullptr),MAKEINTRESOURCE(IDD_SETTINGS),nullptr,settings_proc,reinterpret_cast<LPARAM>(&state));
    check(dialog!=nullptr,"multilingual settings dialog creates");
    struct Cleanup {HWND w;~Cleanup(){DestroyWindow(w);}} cleanup{dialog};
    auto change=[&](unsigned language){SendDlgItemMessage(dialog,IDC_LANGUAGE,CB_SETCURSEL,language,0);SendMessage(dialog,WM_COMMAND,MAKEWPARAM(IDC_LANGUAGE,CBN_SELCHANGE),0);};
    auto validateText=[&]{
        for(HWND child=GetWindow(dialog,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)){
            wchar_t type[32]{},text[512]{};GetClassName(child,type,32);GetWindowText(child,text,512);
            if(wcscmp(type,L"Static") && wcscmp(type,L"Button"))continue;
            if(wcscmp(type,L"Button")==0 && (GetWindowLongPtr(child,GWL_STYLE)&BS_TYPEMASK)==BS_OWNERDRAW)continue;
            RECT r{};GetClientRect(child,&r);HDC dc=GetDC(child);auto old=SelectObject(dc,reinterpret_cast<HFONT>(SendMessage(child,WM_GETFONT,0,0)));SIZE extent{};
            GetTextExtentPoint32(dc,text,int(wcslen(text)),&extent);SelectObject(dc,old);ReleaseDC(child,dc);
            RECT padding{0,0,12,0};MapDialogRect(dialog,&padding);int extra=wcscmp(type,L"Button")==0?padding.right:0;
            if(extent.cx+extra>r.right)std::wcerr<<L"Translation clipped: "<<text<<L"\n";
            check(extent.cx+extra<=r.right,"translated labels fit native controls");
        }
    };
    for(unsigned language:{1u,2u,0u,2u,1u,0u}){
        TabCtrl_SetCurSel(GetDlgItem(dialog,IDC_SETTINGS_TAB),3);csp::show_settings_page(dialog,3);
        HWND list=GetDlgItem(dialog,IDC_ORDER);ListView_SetItemState(list,5,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
        change(language);
        check(csp::ui_language(dialog)==language && saved==language,"language change is immediate and saved independently");
        check(TabCtrl_GetCurSel(GetDlgItem(dialog,IDC_SETTINGS_TAB))==3,"language change preserves the selected page");
        check(ListView_GetNextItem(list,-1,LVNI_SELECTED)==5,"language change preserves selected speaker row");
        csp::Settings result=source;check(read_settings(dialog,result) && result==source,"all custom values and palette data survive language switching");
        check(control_text(dialog,IDC_APPLY)==csp::translate(language,L"Apply"),"footer buttons change language");
        for(int id:{3000,3001,3100,3110,3111,3200,3201,3300,3301,3400,3401}){
            const wchar_t* key=nullptr;for(const auto& caption:csp::settings_captions)if(caption.id==id)key=caption.text;
            check(key && control_text(dialog,id)==csp::translate(language,key),"all eleven group titles switch language immediately");
        }
        validateText();
        palette_test::run(dialog,IDC_FGCOLORS,1);
        check(palette_test::seen_language==language && palette_test::seen_title==csp::translate(language,L"Foreground palette - advanced"),"modal palette editor follows chosen language");
    }
    check(writes==6,"language persistence callback called once per selection");
    for(const wchar_t* draft:{L"",L"invalid",L"000137",L" 17 "}){
        SetDlgItemText(dialog,IDC_HOLD,draft);SetDlgItemText(dialog,IDC_FSHORT,draft);SetDlgItemText(dialog,IDC_BLUR,draft);SetDlgItemText(dialog,IDC_FG,draft);
        for(unsigned language:{1u,2u,0u}){change(language);for(int id:{IDC_HOLD,IDC_FSHORT,IDC_BLUR,IDC_FG})check(control_text(dialog,id)==draft,"language switching preserves raw and invalid drafts exactly");}
    }
    check(applied==0 && state.value==source,"language changes neither apply nor overwrite spectrum settings");
    change(1);
    for(unsigned language:{0u,1u,2u}){
        DialogState reopened{source,[](const csp::Settings&){}};reopened.language=language;
        HWND next=CreateDialogParam(GetModuleHandle(nullptr),MAKEINTRESOURCE(IDD_SETTINGS),nullptr,settings_proc,reinterpret_cast<LPARAM>(&reopened));
        check(next!=nullptr && csp::ui_language(next)==language,"reopened dialog restores language preference");
        csp::Settings result=source;check(read_settings(next,result) && result==source,"localized initial controls retain exact settings");
        DestroyWindow(next);
        check(csp::ui_language(dialog)==1,"different dialog languages remain isolated");
    }
}
