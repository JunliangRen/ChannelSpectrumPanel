#pragma once
#include <commdlg.h>
#include <cwchar>
#include "resource.h"
#include "localization.h"
namespace csp {
inline int color_field(int button) noexcept {
    constexpr int fields[]={IDC_FG,IDC_FG2,IDC_BG,IDC_PEAKCOLOR,IDC_TICKCOLOR};
    const int index=button-IDC_COLOR_FIRST;
    return index<5?fields[index]:index<13?IDC_FGCOLORS:IDC_BGCOLORS;
}
inline COLORREF rgb_to_colorref(unsigned rgb) noexcept {return RGB((rgb>>16)&255,(rgb>>8)&255,rgb&255);}
inline unsigned colorref_to_rgb(COLORREF color) noexcept {return (GetRValue(color)<<16)|(GetGValue(color)<<8)|GetBValue(color);}
inline bool read_color_values(HWND dialog,int field,unsigned (&values)[8],int count) noexcept {
    wchar_t text[256]{};GetDlgItemText(dialog,field,text,256);
    const wchar_t* cursor=text;
    for(int i=0;i<count;++i){
        while(*cursor==L' ')++cursor;
        unsigned value=0;int digits=0;
        while(true){
            wchar_t c=*cursor;unsigned digit;
            if(c>=L'0' && c<=L'9')digit=c-L'0';
            else if(c>=L'a' && c<=L'f')digit=c-L'a'+10;
            else if(c>=L'A' && c<=L'F')digit=c-L'A'+10;
            else break;
            if(++digits>6)return false;
            value=(value<<4)|digit;++cursor;
        }
        if(!digits)return false;
        while(*cursor==L' ')++cursor;
        values[i]=value;
        if(i+1<count){if(*cursor!=L',')return false;++cursor;}
    }
    return *cursor==0;
}
inline void refresh_color_buttons(HWND dialog) noexcept {
    for(int id=IDC_COLOR_FIRST;id<=IDC_COLOR_LAST;++id)InvalidateRect(GetDlgItem(dialog,id),nullptr,TRUE);
}
inline void choose_setting_color(HWND dialog,int button,COLORREF* custom){
    const int index=button-IDC_COLOR_FIRST,field=color_field(button);
    const int count=index<5?1:8,slot=index<5?0:index<13?index-5:index-13;
    unsigned values[8]{};
    if(!read_color_values(dialog,field,values,count)){
        MessageBox(dialog,csp::tr(dialog,L"Enter valid RGB hex colors before opening the picker (eight comma-separated colors for a palette)."),csp::tr(dialog,L"Invalid color"),MB_OK|MB_ICONWARNING);
        SetFocus(GetDlgItem(dialog,field));return;
    }
    CHOOSECOLOR picker{sizeof(picker)};picker.hwndOwner=dialog;picker.rgbResult=rgb_to_colorref(values[slot]);picker.lpCustColors=custom;picker.Flags=CC_FULLOPEN|CC_RGBINIT;
    if(!ChooseColor(&picker))return;
    values[slot]=colorref_to_rgb(picker.rgbResult);
    wchar_t text[64]{};size_t offset=0;
    for(int i=0;i<count;++i)offset+=swprintf_s(text+offset,64-offset,i?L",%06X":L"%06X",values[i]);
    SetDlgItemText(dialog,field,text);
    refresh_color_buttons(dialog);
}
inline bool draw_color_button(HWND dialog,const DRAWITEMSTRUCT& item) noexcept {
    if(item.CtlID<IDC_COLOR_FIRST || item.CtlID>IDC_COLOR_LAST)return false;
    const int index=item.CtlID-IDC_COLOR_FIRST;
    unsigned values[8]{};const bool valid=read_color_values(dialog,color_field(item.CtlID),values,index<5?1:8);
    const unsigned color=values[index<5?0:index<13?index-5:index-13];
    const int saved=SaveDC(item.hDC);if(!saved)return true;
    RECT rect=item.rcItem;
    FillRect(item.hDC,&rect,GetSysColorBrush(COLOR_BTNFACE));
    DrawEdge(item.hDC,&rect,(item.itemState&ODS_SELECTED)?EDGE_SUNKEN:EDGE_RAISED,BF_RECT);
    InflateRect(&rect,-3,-3);
    SetDCBrushColor(item.hDC,valid?rgb_to_colorref(color):GetSysColor(COLOR_BTNFACE));
    FillRect(item.hDC,&rect,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    if(item.itemState&ODS_FOCUS)DrawFocusRect(item.hDC,&rect);
    RestoreDC(item.hDC,saved);return true;
}
}

namespace csp {
struct PaletteEditor {wchar_t text[256]{};const wchar_t* title;bool accepted=false;};
inline INT_PTR CALLBACK palette_editor_proc(HWND dialog,UINT message,WPARAM wp,LPARAM lp){
    auto* editor=reinterpret_cast<PaletteEditor*>(GetWindowLongPtr(dialog,DWLP_USER));
    if(message==WM_INITDIALOG){editor=reinterpret_cast<PaletteEditor*>(lp);SetWindowLongPtr(dialog,DWLP_USER,lp);SetWindowText(dialog,editor->title);SetDlgItemText(dialog,IDC_PALETTE_HELP,tr(dialog,L"Eight RGB hex colors, separated by commas. Copy or paste the entire palette."));SetDlgItemText(dialog,IDOK,tr(dialog,L"OK"));SetDlgItemText(dialog,IDCANCEL,tr(dialog,L"Cancel"));SendDlgItemMessage(dialog,IDC_PALETTE_TEXT,EM_SETLIMITTEXT,255,0);SetDlgItemText(dialog,IDC_PALETTE_TEXT,editor->text);return TRUE;}
    if(message==WM_COMMAND && editor){
        if(LOWORD(wp)==IDCANCEL){EndDialog(dialog,IDCANCEL);return TRUE;}
        if(LOWORD(wp)==IDOK){unsigned values[8]{};
            if(!read_color_values(dialog,IDC_PALETTE_TEXT,values,8)){MessageBox(dialog,csp::tr(dialog,L"Enter exactly eight RGB hex colors separated by commas."),csp::tr(dialog,L"Invalid palette"),MB_OK|MB_ICONWARNING);return TRUE;}
            GetDlgItemText(dialog,IDC_PALETTE_TEXT,editor->text,256);editor->accepted=true;EndDialog(dialog,IDOK);return TRUE;
        }
    }
    if(message==WM_DESTROY)SetWindowLongPtr(dialog,DWLP_USER,0);
    return FALSE;
}
inline void edit_palette(HWND parent,int field){
    PaletteEditor editor{};editor.title=field==IDC_FGCOLORS?csp::tr(parent,L"Foreground palette - advanced"):csp::tr(parent,L"Background palette - advanced");
    GetDlgItemText(parent,field,editor.text,256);
    auto module=reinterpret_cast<HINSTANCE>(GetWindowLongPtr(parent,GWLP_HINSTANCE));
    if(DialogBoxParam(module,MAKEINTRESOURCE(IDD_PALETTE),parent,palette_editor_proc,reinterpret_cast<LPARAM>(&editor))==IDOK && editor.accepted){SetDlgItemText(parent,field,editor.text);refresh_color_buttons(parent);}
}
}
