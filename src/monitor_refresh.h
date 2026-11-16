#pragma once
#include <windows.h>
#include <vector>
#include <cmath>
#include <cwchar>
namespace csp {
struct MonitorRefresh {
    HMONITOR monitor=nullptr;
    wchar_t device[CCHDEVICENAME]{};
    double hz=0;
    bool precise=false;
};
inline double refresh_rational(unsigned numerator,unsigned denominator) noexcept {
    const double rate=denominator?double(numerator)/denominator:0;
    return std::isfinite(rate) && rate>=1 && rate<=1000?rate:0;
}
inline MonitorRefresh query_monitor_refresh(HWND panel) noexcept {
    MonitorRefresh result;
    result.monitor=MonitorFromWindow(panel,MONITOR_DEFAULTTONEAREST);
    MONITORINFOEX info{};info.cbSize=sizeof(info);
    if(!GetMonitorInfo(result.monitor,&info))return result;
    wcscpy_s(result.device,info.szDevice);
    // Retry a bounded number of times if display topology changes between queries.
    try { for(int attempt=0;attempt<3;++attempt){
        UINT32 pathCount=0,modeCount=0;
        if(GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,&pathCount,&modeCount)!=ERROR_SUCCESS)break;
        if(!pathCount || pathCount>1024 || modeCount>4096)break;
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
        std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
        const LONG status=QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,&pathCount,paths.data(),&modeCount,modes.data(),nullptr);
        if(status==ERROR_INSUFFICIENT_BUFFER)continue;
        if(status!=ERROR_SUCCESS)break;
        for(UINT32 i=0;i<pathCount;++i){
            const auto& path=paths[i];
            DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
            source.header={DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME,sizeof(source),path.sourceInfo.adapterId,path.sourceInfo.id};
            if(DisplayConfigGetDeviceInfo(&source.header)!=ERROR_SUCCESS || _wcsicmp(source.viewGdiDeviceName,info.szDevice)!=0)continue;
            const double rate=refresh_rational(path.targetInfo.refreshRate.Numerator,path.targetInfo.refreshRate.Denominator);
            if(rate){result.hz=rate;result.precise=true;return result;}
        }
        break;
    }
    } catch(...) { /* Allocation failure uses the allocation-free mode query below. */ }
    // Some remote/legacy drivers expose only an integer mode frequency.
    DEVMODE mode{};mode.dmSize=sizeof(mode);
    if(EnumDisplaySettings(info.szDevice,ENUM_CURRENT_SETTINGS,&mode) && mode.dmDisplayFrequency>1)
        result.hz=refresh_rational(mode.dmDisplayFrequency,1);
    return result;
}
inline double effective_refresh(bool follow,unsigned manual,const MonitorRefresh& monitor) noexcept {
    return follow && monitor.hz>0?monitor.hz:double(manual);
}
inline bool refresh_changed(double previous,double next) noexcept {return std::abs(previous-next)>0.000001;}
}
