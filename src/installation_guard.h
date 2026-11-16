#pragma once
#include <initializer_list>
namespace csp {
// The host calls installation validators after loading component modules and before normal startup.
// Refuse a mixed installation; an init/quit callback would be too late to prevent duplicate services.
template<typename IsLoaded> bool legacy_components_absent(IsLoaded&& isLoaded) {
    for(const wchar_t* name:{L"foo_uie_channel_spectrum_x86.dll",L"foo_uie_channel_spectrum_x64.dll",L"foo_uie_channel_spectrum_arm64ec.dll"})
        if(isLoaded(name))return false;
    return true;
}
}
