#pragma once
#include <commctrl.h>
#include <cwchar>
#include <cstdint>
#include <algorithm>
#include <stdexcept>
#include <initializer_list>
#include "resource.h"
#include "spectrum.h"
namespace csp {
inline void numeric_choices(HWND dialog,int id,unsigned value,bool hold){
    HWND combo=GetDlgItem(dialog,id);SendMessage(combo,CB_RESETCONTENT,0,0);
    const unsigned lengths[]={0,3,7,15,100},times[]={0,100,250,500};
    const wchar_t* names[]={L"Hidden",L"Short",L"Medium",L"Long",L"Full"};
    bool found=false;int selected=0;
    for(unsigned i=0;i<(hold?4u:5u);++i){
        unsigned number=hold?times[i]:lengths[i];wchar_t label[64]{};
        if(hold)swprintf_s(label,number?L"%u ms":L"No hold (%u ms)",number);
        else if(number==100)swprintf_s(label,L"Full span");
        else if(number==0)swprintf_s(label,L"Hidden");
        else swprintf_s(label,L"%s (%u px)",names[i],number);
        auto index=SendMessage(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
        if(index<0)throw std::runtime_error("Could not populate numeric choices.");
        SendMessage(combo,CB_SETITEMDATA,index,number);if(number==value){selected=int(index);found=true;}
    }
    if(!found){wchar_t label[64]{};swprintf_s(label,hold?L"Custom: %u ms":L"Custom: %u px",value);
        auto index=SendMessage(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));if(index<0)throw std::runtime_error("Could not populate custom value.");
        SendMessage(combo,CB_SETITEMDATA,index,value);selected=int(index);
    }
    SendMessage(combo,CB_SETCURSEL,selected,0);
}
inline bool read_numeric_choice(HWND dialog,int id,unsigned maximum,unsigned& value) noexcept {
    HWND combo=GetDlgItem(dialog,id);wchar_t text[128]{};GetWindowText(combo,text,128);
    const int count=int(SendMessage(combo,CB_GETCOUNT,0,0));
    for(int i=0;i<count;++i){
        if(SendMessage(combo,CB_GETLBTEXTLEN,i,0)>=128)continue;
        wchar_t label[128]{};SendMessage(combo,CB_GETLBTEXT,i,reinterpret_cast<LPARAM>(label));
        if(wcscmp(label,text)==0){auto data=SendMessage(combo,CB_GETITEMDATA,i,0);if(data<0 || static_cast<uint64_t>(data)>maximum)return false;value=unsigned(data);return true;}
    }
    const wchar_t* cursor=text;while(*cursor==L' ')++cursor;if(!*cursor)return false;
    unsigned number=0,digits=0;
    while(*cursor>=L'0' && *cursor<=L'9'){if(++digits>5)return false;number=number*10+unsigned(*cursor++-L'0');}
    while(*cursor==L' ')++cursor;
    if(!digits || *cursor || number>maximum)return false;
    value=number;return true;
}
struct Spinner {int id,buddy,low,high;};
inline constexpr Spinner spinners[]={{1700,IDC_WIDTH,1,8},{1701,IDC_SPACING,0,8},{1702,IDC_PADLEFT,0,4096},{1703,IDC_PADRIGHT,0,4096},{1704,IDC_GAP,0,4096},{1705,IDC_RANGE,1,1000},{1706,IDC_FALL,1,99},{1707,IDC_BLUR,0,100}};
inline void initialize_numeric_controls(HWND dialog){
    for(auto s:spinners){HWND spin=GetDlgItem(dialog,s.id);SendMessage(spin,UDM_SETBUDDY,reinterpret_cast<WPARAM>(GetDlgItem(dialog,s.buddy)),0);SendMessage(spin,UDM_SETRANGE32,s.low,s.high);}
    SendDlgItemMessage(dialog,IDC_FALL_SLIDER,TBM_SETRANGE,TRUE,MAKELPARAM(1,99));
    SendDlgItemMessage(dialog,IDC_BLUR_SLIDER,TBM_SETRANGE,TRUE,MAKELPARAM(0,100));
    HWND tip=CreateWindowEx(WS_EX_TOPMOST,TOOLTIPS_CLASS,nullptr,WS_POPUP|TTS_ALWAYSTIP,0,0,0,0,dialog,nullptr,reinterpret_cast<HINSTANCE>(GetWindowLongPtr(dialog,GWLP_HINSTANCE)),nullptr);
    if(tip){
        SendMessage(tip,TTM_SETMAXTIPWIDTH,0,300);
        for(int id:{IDC_HOLD,IDC_FSHORT,IDC_FLONG,IDC_ASHORT,IDC_ALONG}){
            TOOLINFO tool{sizeof(tool)};tool.uFlags=TTF_IDISHWND|TTF_SUBCLASS;tool.hwnd=dialog;tool.uId=reinterpret_cast<UINT_PTR>(GetDlgItem(dialog,id));
            tool.lpszText=const_cast<wchar_t*>(id==IDC_HOLD?L"Choose a preset or type an exact hold time in milliseconds.":L"Choose a preset or type an exact value: 0 hides ticks, 1-99 is pixels, and 100 spans the graph.");SendMessage(tip,TTM_ADDTOOL,0,reinterpret_cast<LPARAM>(&tool));
        }
    }
}
inline void sync_slider(HWND dialog,int edit,int slider,unsigned maximum) noexcept {
    BOOL valid=FALSE;unsigned value=GetDlgItemInt(dialog,edit,&valid,FALSE);
    if(valid && value<=maximum)SendDlgItemMessage(dialog,slider,TBM_SETPOS,TRUE,value);
}
inline void update_setting_dependencies(HWND dialog) noexcept {
    auto checked=[&](int id){return IsDlgButtonChecked(dialog,id)==BST_CHECKED;};
    auto enable=[&](int id,bool enabled){EnableWindow(GetDlgItem(dialog,id),enabled);};
    const bool peaks=checked(IDC_PEAKS),freq=checked(IDC_FTICKS),amp=checked(IDC_ATICKS);
    for(int id:{IDC_FALL,1706,IDC_FALL_SLIDER,IDC_HOLD,IDC_PEAKCOLOR,1303})enable(id,peaks);
    for(int id:{IDC_FSHORT,IDC_FLONG})enable(id,freq);
    for(int id:{IDC_ASHORT,IDC_ALONG})enable(id,amp);
    enable(IDC_TICKCOLOR,freq||amp);enable(1304,freq||amp);
    const auto mode=SendDlgItemMessage(dialog,IDC_CHANNELMODE,CB_GETCURSEL,0,0);
    const bool ordered=mode==0 && checked(IDC_ALLCHANNELS), listed=ordered || mode==1;
    enable(IDC_SELECTED,mode==2);enable(IDC_ALLCHANNELS,mode==0);
    for(int id:{IDC_ORDER,IDC_CHANNEL_ALL,IDC_CHANNEL_NONE})enable(id,listed);
    for(int id:{IDC_ORDER_UP,IDC_ORDER_DOWN,IDC_ORDER_RESET})enable(id,ordered);
    SetDlgItemText(dialog,2206,ordered?L"Enabled channels / list order":
        mode==1?L"Enabled channels (merged)":
        mode==2?L"List unused: specified input selected":
        L"List unused: enable all inputs first");
    enable(IDC_FASTTRANS,checked(IDC_TRANSPARENT));enable(IDC_INTERVAL,!checked(IDC_FOLLOW_MONITOR));
    const auto style=static_cast<Style>(SendDlgItemMessage(dialog,IDC_STYLE,CB_GETCURSEL,0,0));
    const bool vertical=style==Style::Vertical,blur=style==Style::SimpleAntialias || style==Style::HorizontalAntialias;
    for(int id:{IDC_BLUR,1707,IDC_BLUR_SLIDER,2104})enable(id,blur);
    enable(IDC_SEGMENTED,style!=Style::SimpleLine);
    enable(IDC_FGPALETTE,vertical);
    const bool foregroundPalette=vertical && checked(IDC_FGPALETTE);
    for(int id:{IDC_FG,1300,2106})enable(id,!foregroundPalette);
    const bool twoColors=style==Style::GradientBars || style==Style::Horizontal || style==Style::HorizontalAntialias || (vertical && !foregroundPalette);
    for(int id:{IDC_FG2,1301,2107})enable(id,twoColors);
    SetDlgItemText(dialog,3101,vertical?L"Low to high: click a color to edit it.":L"Not used by this style. Select Vertical to use this palette.");
    enable(IDC_BGPALETTE,!checked(IDC_TRANSPARENT));
    for(int i=1305;i<=1312;++i)enable(i,foregroundPalette);
    for(int i=1313;i<=1320;++i)enable(i,checked(IDC_BGPALETTE) && !checked(IDC_TRANSPARENT));
    enable(IDC_FG_ADVANCED,foregroundPalette);enable(IDC_BG_ADVANCED,checked(IDC_BGPALETTE) && !checked(IDC_TRANSPARENT));
    SetDlgItemText(dialog,3402,!checked(IDC_DIRECT2D)?L"Selected backend: GDI.":checked(IDC_TRANSPARENT)?L"Draft requires GDI: parent background is enabled.":checked(IDC_SEGMENTED)?L"Draft requires GDI: segmented bars are enabled.":L"Direct2D preferred; device failure can still trigger GDI fallback.");
}
}
