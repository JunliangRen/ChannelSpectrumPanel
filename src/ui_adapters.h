#pragma once
#include <foobar2000/SDK/foobar2000.h>
#include <columns_ui-sdk/ui_extension.h>
#include "panel_config.h"

// The host adapters share one window implementation; View also permits isolated SDK contract tests.
namespace csp {
constexpr GUID panel_guid={0x0c45a893,0x4607,0x4914,{0x8e,0x64,0x21,0x89,0xb3,0xa2,0x79,0x70}};
constexpr GUID default_ui_guid={0xf169149b,0x4249,0x47ee,{0x9f,0x47,0x8b,0x9a,0xab,0x65,0xb8,0x0d}};

template<class View>
class ColumnsPanel : public uie::window {
public:
    ColumnsPanel(){view_.retain=[this]{return service_ptr_t<service_base>(this);};}
    ~ColumnsPanel(){view_.destroy_window();}
    const GUID& get_extension_guid() const override{return panel_guid;}
    void get_name(pfc::string_base& out) const override{out="Channel Spectrum Panel";}
    void get_category(pfc::string_base& out) const override{out="Visualisations";}
    unsigned get_type() const override{return uie::type_panel;}
    bool is_available(const uie::window_host_ptr&) const override{return true;}
    HWND get_wnd() const override{return view_.get_wnd();}
    bool have_config_popup() const override{return true;}
    bool show_config_popup(HWND parent) override{service_ptr_t<uie::window> hold(this);return view_.show_config_popup(parent);}
    void get_config(stream_writer* writer,abort_callback& abort) const override{view_.get_config(writer,abort);}
    void set_config(stream_reader* reader,t_size size,abort_callback& abort) override{view_.set_config(reader,size,abort);}
    HWND create_or_transfer_window(HWND parent,const uie::window_host_ptr& host,const ui_helpers::window_position_t& pos) override{
        service_ptr_t<uie::window> hold(this);
        if(view_.get_wnd()){
            view_.transfer_window(parent);
            if(host_.is_valid())host_->relinquish_ownership(view_.get_wnd());
            host_=host;
            SetWindowPos(view_.get_wnd(),nullptr,pos.x,pos.y,pos.cx,pos.cy,SWP_NOZORDER|SWP_NOACTIVATE);
        }else{
            host_=host;
            if(!view_.create_window(parent,pos.x,pos.y,pos.cx,pos.cy))host_.release();
        }
        return view_.get_wnd();
    }
    void destroy_window() override{view_.destroy_window();host_.release();}
private:
    uie::window_host_ptr host_;
    View view_;
};

template<class View>
class DefaultPanel : public ui_element_instance {
public:
    DefaultPanel(ui_element_config::ptr config,ui_element_instance_callback_ptr callback):callback_(callback){
        view_.retain=[this]{return service_ptr_t<service_base>(this);};
        view_.editing=[this]{return callback_->is_edit_mode_enabled();};
        set_configuration(config);
    }
    ~DefaultPanel(){view_.destroy_window();}
    void initialize(HWND parent){
        RECT rect{};GetClientRect(parent,&rect);
        if(!view_.create_window(parent,0,0,rect.right,rect.bottom))throw std::runtime_error("Could not create spectrum window.");
        view_.host_visibility(callback_->is_elem_visible_(this));
        // Unlike Columns UI, Default UI elements supply their own visible child.
        ShowWindow(view_.get_wnd(),SW_SHOWNOACTIVATE);
    }
    HWND get_wnd() override{return view_.get_wnd();}
    GUID get_guid() override{return default_ui_guid;}
    GUID get_subclass() override{return ui_element_subclass_playback_visualisation;}
    void set_configuration(ui_element_config::ptr config) override{
        if(config.is_empty() || config->get_guid()!=default_ui_guid)throw exception_io_data();
        csp::Settings next;
        if(!csp::decode_panel_config(config->get_data(),config->get_data_size(),next))throw exception_io_data();
        view_.apply_settings(next);
    }
    ui_element_config::ptr get_configuration() override{
        const auto bytes=csp::encode_panel_config(view_.settings());
        return ui_element_config::g_create(default_ui_guid,bytes.data(),bytes.size());
    }
    void notify(const GUID& what,t_size param,const void*,t_size) override{
        if(what==ui_element_notify_visibility_changed)view_.host_visibility(param!=0);
        else if(what==ui_element_notify_colors_changed || what==ui_element_notify_font_changed)view_.theme_changed();
    }
    bool edit_mode_context_menu_test(const POINT&,bool) override{return true;}
    void edit_mode_context_menu_build(const POINT&,bool,HMENU menu,unsigned base) override{view_.build_menu(menu,base);}
    void edit_mode_context_menu_command(const POINT&,bool,unsigned command,unsigned base) override{
        service_ptr_t<ui_element_instance> hold(this);
        if(command>=base)view_.menu_command(command-base);
    }
private:
    ui_element_instance_callback_ptr callback_;
    View view_;
};
template<class View>
class DefaultElement : public ui_element {
public:
    GUID get_guid() override{return default_ui_guid;}
    GUID get_subclass() override{return ui_element_subclass_playback_visualisation;}
    void get_name(pfc::string_base& out) override{out="Channel Spectrum Panel";}
    ui_element_config::ptr get_default_configuration() override{
        const auto bytes=csp::encode_panel_config(csp::Settings{});
        return ui_element_config::g_create(default_ui_guid,bytes.data(),bytes.size());
    }
    ui_element_instance_ptr instantiate(HWND parent,ui_element_config::ptr config,ui_element_instance_callback_ptr callback) override{
        if(callback.is_empty())throw exception_io_data();
        service_ptr_t<DefaultPanel<View>> panel=new service_impl_t<DefaultPanel<View>>(config,callback);
        panel->initialize(parent);return panel;
    }
    ui_element_children_enumerator_ptr enumerate_children(ui_element_config::ptr) override{return nullptr;}
    bool get_description(pfc::string_base& out) override{out="Per-channel spectrum with mirrored layouts and falling peak markers.";return true;}
};
}
