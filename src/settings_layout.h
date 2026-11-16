#pragma once
#include <commctrl.h>
#include <uxtheme.h>
#include "resource.h"
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")
namespace csp {
// Owned by DialogState, which outlives the dialog and all of its child controls.
class SettingsHeadingFont {
public:
    SettingsHeadingFont()=default;
    SettingsHeadingFont(const SettingsHeadingFont&)=delete;
    SettingsHeadingFont& operator=(const SettingsHeadingFont&)=delete;
    ~SettingsHeadingFont(){if(font_)DeleteObject(font_);}
    void initialize(HWND dialog){
        if(font_)return;
        LOGFONT font{};auto base=reinterpret_cast<HFONT>(SendMessage(dialog,WM_GETFONT,0,0));
        if(!GetObject(base,sizeof(font),&font))return;
        font.lfWeight=FW_SEMIBOLD;font_=CreateFontIndirect(&font);if(!font_)return;
        for(int id:{3000,3001,3100,3200,3201,3400,3401})SendDlgItemMessage(dialog,id,WM_SETFONT,reinterpret_cast<WPARAM>(font_),FALSE);
    }
private:HFONT font_=nullptr;
};
inline int settings_page(int id) noexcept {
    if(id>=IDC_CHANNEL_FIRST && id<=IDC_CHANNEL_LAST)return 3;
    switch(id){
    case 3222:case 3223:return 2;
    case 3000:case 2100:case IDC_STYLE:case 2102:case IDC_WIDTH:case 2103:case IDC_SPACING:case 2104:case IDC_BLUR_SLIDER:case IDC_BLUR:case IDC_SEGMENTED:case 2105:case IDC_EDGE:case 3001:case IDC_PEAKS:case 2001:case IDC_FALL_SLIDER:case IDC_FALL:case 2004:case IDC_HOLD:case 1700:case 1701:case 1706:case 1707:return 0;
    case 3100:case 2106:case IDC_FG:case 1300:case 2107:case IDC_FG2:case 1301:case 2108:case IDC_BG:case 1302:case 2109:case IDC_PEAKCOLOR:case 1303:case 2110:case IDC_TICKCOLOR:case 1304:case IDC_FGPALETTE:case IDC_FG_ADVANCED:case 3101:case 1305:case 1306:case 1307:case 1308:case 1309:case 1310:case 1311:case 1312:case IDC_BGPALETTE:case IDC_BG_ADVANCED:case 1313:case 1314:case 1315:case 1316:case 1317:case 1318:case 1319:case 1320:case 3102:return 1;
    case 3200:case 2007:case IDC_FFT:case IDC_NEWFFT:case IDC_LINEAR:case IDC_LOGAMP:case 2005:case IDC_RANGE:case 2006:case 3201:case IDC_FTICKS:case IDC_ATICKS:case 2008:case IDC_FSHORT:case 2009:case IDC_FLONG:case 2010:case IDC_ASHORT:case 2011:case IDC_ALONG:case 1705:return 2;
    case 2201:case IDC_CHANNELMODE:case 2202:case IDC_SELECTED:case IDC_ALLCHANNELS:case 2200:case IDC_DIRECTION:case 2101:case IDC_ROTATION:case IDC_REVFREQ:case IDC_REVCHAN:case 2203:case IDC_PADLEFT:case 2204:case IDC_PADRIGHT:case 2205:case IDC_GAP:case 2206:case IDC_CHANNEL_ALL:case IDC_CHANNEL_NONE:case IDC_ORDER:case IDC_ORDER_UP:case IDC_ORDER_DOWN:case IDC_ORDER_RESET:case 1702:case 1703:case 1704:return 3;
    case 3400:case IDC_FOLLOW_MONITOR:case 2000:case IDC_INTERVAL:case IDC_MONITOR_RATE:case 3401:case IDC_DIRECT2D:case 3402:case IDC_TRANSPARENT:case IDC_FASTTRANS:case IDC_SCANLINE:return 4;
    default:return -1;
    }
}
inline void show_settings_page(HWND dialog,int page) noexcept {
    HWND focus=GetFocus();bool moveFocus=false;
    for(HWND child=GetWindow(dialog,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)){
        const int owner=settings_page(GetDlgCtrlID(child));
        if(owner>=0){
            if(owner!=page && (focus==child || IsChild(child,focus)))moveFocus=true;
            ShowWindow(child,owner==page?SW_SHOW:SW_HIDE);
        }
    }
    if(moveFocus)SetFocus(GetDlgItem(dialog,IDC_SETTINGS_TAB));
}
inline void initialize_settings_tabs(HWND dialog) noexcept {
    // Use the native tab texture for the dialog and its static/checkbox controls.
    // DefDlgProc supplies the matching themed brush, including after theme changes.
    EnableThemeDialogTexture(dialog,ETDT_ENABLETAB);
    HWND tab=GetDlgItem(dialog,IDC_SETTINGS_TAB);
    const wchar_t* labels[]={L"Appearance",L"Colors",L"Spectrum",L"Channels",L"Performance"};
    for(int i=0;i<5;++i){TCITEM item{};item.mask=TCIF_TEXT;item.pszText=const_cast<wchar_t*>(labels[i]);TabCtrl_InsertItem(tab,i,&item);}
    show_settings_page(dialog,0);
}
}
