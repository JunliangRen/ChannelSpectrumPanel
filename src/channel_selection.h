#pragma once
#include <windows.h>
#include <commctrl.h>
#include <cstdint>
#include <array>
#include <stdexcept>
#include "resource.h"
namespace csp {
inline constexpr const wchar_t* speaker_names[]={L"Front left",L"Front right",L"Front center",L"LFE",L"Back left",L"Back right",L"Front left of center",L"Front right of center",L"Back center",L"Side left",L"Side right",L"Top center",L"Top front left",L"Top front center",L"Top front right",L"Top back left",L"Top back center",L"Top back right"};
inline std::array<unsigned,18> default_channel_order(){std::array<unsigned,18> order{};for(unsigned i=0;i<18;++i)order[i]=i;return order;}
inline void populate_channel_list(HWND dialog,const std::array<unsigned,18>& order,uint32_t mask){
    HWND list=GetDlgItem(dialog,IDC_ORDER);
    ListView_SetExtendedListViewStyle(list,LVS_EX_CHECKBOXES|LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);
    if(Header_GetItemCount(ListView_GetHeader(list))==0){
        RECT r{};GetClientRect(list,&r);LVCOLUMN column{};column.mask=LVCF_WIDTH;column.cx=r.right-GetSystemMetrics(SM_CXVSCROLL)-4;
        if(ListView_InsertColumn(list,0,&column)<0)throw std::runtime_error("Could not initialize the channel list.");
    }
    ListView_DeleteAllItems(list);
    for(unsigned i=0;i<18;++i){
        if(order[i]>=18)throw std::runtime_error("Invalid speaker position.");
        LVITEM item{};item.mask=LVIF_TEXT|LVIF_PARAM;item.iItem=i;item.pszText=const_cast<wchar_t*>(speaker_names[order[i]]);item.lParam=order[i];
        if(ListView_InsertItem(list,&item)<0)throw std::runtime_error("Could not populate the channel list.");
        ListView_SetCheckState(list,i,(mask&(1u<<order[i]))!=0);
    }
}
inline bool channel_order(HWND dialog,std::array<unsigned,18>& order) noexcept {
    HWND list=GetDlgItem(dialog,IDC_ORDER);if(ListView_GetItemCount(list)!=18)return false;
    uint32_t seen=0;
    for(unsigned i=0;i<18;++i){LVITEM item{};item.mask=LVIF_PARAM;item.iItem=i;
        if(!ListView_GetItem(list,&item) || item.lParam<0 || item.lParam>=18 || (seen&(1u<<item.lParam)))return false;
        order[i]=unsigned(item.lParam);seen|=1u<<item.lParam;
    }return true;
}
inline uint32_t enabled_channels(HWND dialog) noexcept {
    std::array<unsigned,18> order{};if(!channel_order(dialog,order))return 0;
    uint32_t mask=0;HWND list=GetDlgItem(dialog,IDC_ORDER);
    for(unsigned i=0;i<18;++i)if(ListView_GetCheckState(list,i))mask|=1u<<order[i];
    return mask;
}
inline void set_enabled_channels(HWND dialog,uint32_t mask){
    HWND list=GetDlgItem(dialog,IDC_ORDER);
    if(ListView_GetItemCount(list)==0)populate_channel_list(dialog,default_channel_order(),mask);
    std::array<unsigned,18> order{};if(!channel_order(dialog,order))throw std::runtime_error("Invalid channel list.");
    for(unsigned i=0;i<18;++i)ListView_SetCheckState(list,i,(mask&(1u<<order[i]))!=0);
}
inline void move_channel(HWND dialog,int delta){
    HWND list=GetDlgItem(dialog,IDC_ORDER);const int from=ListView_GetNextItem(list,-1,LVNI_SELECTED),to=from+delta;
    if(from<0 || to<0 || to>=18)return;
    std::array<unsigned,18> order{};if(!channel_order(dialog,order))throw std::runtime_error("Invalid channel list.");
    const auto mask=enabled_channels(dialog);std::swap(order[from],order[to]);populate_channel_list(dialog,order,mask);
    ListView_SetItemState(list,to,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);ListView_EnsureVisible(list,to,FALSE);SetFocus(list);
}
}
