#include "panel_window.h"
#include "installation_guard.h"
#include "ui_adapters.h"

DECLARE_COMPONENT_VERSION("Channel Spectrum Panel", "1.3.1",
    "Default UI and Columns UI spectrum for x86, x64 and ARM64EC. Independent reconstruction inspired by Channel Spectrum panel 0.17.2.\n"
    "Mirrored channels, original amplitude curves and fixed-point falling peaks. English, Simplified Chinese and Japanese settings.");
class InstallationValidator : public component_installation_validator {
public:
    bool is_installed_correctly() override {
        return test_my_name("foo_uie_channel_spectrum.dll") &&
            csp::legacy_components_absent([](const wchar_t* name){return GetModuleHandleW(name)!=nullptr;});
    }
};
static service_factory_single_t<InstallationValidator> installation_validator;

namespace {
static uie::window_factory<csp::ColumnsPanel<SpectrumWindow>> columns_factory;
static service_factory_single_t<csp::DefaultElement<SpectrumWindow>> default_factory;
}
