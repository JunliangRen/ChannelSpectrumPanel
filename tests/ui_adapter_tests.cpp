#include "../src/ui_adapters.h"
#include <functional>
#include <iostream>
#include <stdexcept>

namespace {
int checks=0;
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
// Test double for the audio/rendering window only. The adapters, SDK service
// reference counting, configuration objects and native HWND ownership are real.
class TestView {
public:
    inline static int live=0;
    inline static bool fail_create=false;
    inline static TestView* last=nullptr;
    inline static std::function<void()> modal;
    std::function<bool()> editing;
    std::function<service_ptr_t<service_base>()> retain;
    TestView(){++live;last=this;}
    ~TestView(){destroy_window();--live;}
    HWND get_wnd() const{return window_;}
    const csp::Settings& settings() const{return settings_;}
    void apply_settings(const csp::Settings& next){settings_=next;}
    void host_visibility(bool visible){visible_=visible;}
    void theme_changed(){++themes;}
    bool show_config_popup(HWND){if(modal)modal();return true;}
    void get_config(stream_writer* writer,abort_callback& abort) const{
        auto bytes=csp::encode_panel_config(settings_);writer->write(bytes.data(),bytes.size(),abort);
    }
    void set_config(stream_reader* reader,t_size size,abort_callback& abort){
        if(size>4096 || size%4)throw exception_io_data();
        std::vector<uint8_t> bytes(size);if(size)reader->read_object(bytes.data(),size,abort);
        csp::Settings next;if(!csp::decode_panel_config(bytes.data(),size,next))throw exception_io_data();apply_settings(next);
    }
    HWND create_window(HWND parent,int x,int y,int width,int height){
        if(fail_create)return nullptr;
        WNDCLASSW wc{};wc.hInstance=GetModuleHandle(nullptr);wc.lpfnWndProc=proc;wc.lpszClassName=L"CspAdapterTestWindow";
        RegisterClassW(&wc);
        return CreateWindowExW(0,wc.lpszClassName,L"",WS_CHILD,x,y,width,height,parent,nullptr,wc.hInstance,this);
    }
    void transfer_window(HWND parent){SetParent(window_,parent);}
    void destroy_window(){if(window_)DestroyWindow(window_);}
    void build_menu(HMENU menu,unsigned base){AppendMenuW(menu,MF_STRING,base+1,L"Configure");}
    void menu_command(unsigned command){if(command==1)show_config_popup(window_);else if(command==15)settings_.style=csp::Style::SimpleLine;}
    bool visible_=true;
    int themes=0;
private:
    static LRESULT CALLBACK proc(HWND window,UINT message,WPARAM wp,LPARAM lp){
        auto* self=reinterpret_cast<TestView*>(GetWindowLongPtr(window,GWLP_USERDATA));
        if(message==WM_NCCREATE){self=static_cast<TestView*>(reinterpret_cast<CREATESTRUCT*>(lp)->lpCreateParams);self->window_=window;SetWindowLongPtr(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(message==WM_NCDESTROY && self){self->window_=nullptr;SetWindowLongPtr(window,GWLP_USERDATA,0);}
        return DefWindowProc(window,message,wp,lp);
    }
    HWND window_=nullptr;
    csp::Settings settings_;
};
class Callback : public ui_element_instance_callback_v2 {
public:
    inline static int live=0;
    Callback(){++live;}~Callback(){--live;}
    bool visible=true,editing=false;
    void on_min_max_info_change() override{}
    void on_alt_pressed(bool) override{}
    bool query_color(const GUID&,t_ui_color&) override{return false;}
    bool request_activation(ui_element_instance_ptr) override{return false;}
    bool is_edit_mode_enabled() override{return editing;}
    void request_replace(ui_element_instance_ptr) override{}
    t_ui_font query_font_ex(const GUID&) override{return nullptr;}
    bool is_elem_visible(ui_element_instance_ptr) override{return visible;}
};
class ColumnsHost : public uie::window_host {
public:
    std::function<void(HWND)> relinquish;
    const GUID& get_host_guid() const override{return csp::panel_guid;}
    void on_size_limit_change(HWND,unsigned) override{}
    unsigned is_resize_supported(HWND) const override{return 0;}
    bool request_resize(HWND,unsigned,unsigned,unsigned) override{return false;}
    bool override_status_text_create(service_ptr_t<ui_status_text_override>&) override{return false;}
    bool is_visible(HWND) const override{return true;}
    bool is_visibility_modifiable(HWND,bool) const override{return false;}
    bool set_window_visibility(HWND,bool) override{return false;}
    void relinquish_ownership(HWND window) override{if(relinquish)relinquish(window);}
};
std::vector<uint8_t> bytes(ui_element_config::ptr config){
    const auto* data=static_cast<const uint8_t*>(config->get_data());return {data,data+config->get_data_size()};
}
void config_tests(){
    csp::Settings custom;custom.fft=4096;custom.interval=17;custom.fps_milli=59000;custom.hold=357;custom.range=133;
    custom.padding_left=7;custom.channel_gap=13;custom.follow_monitor=true;custom.direct2d=true;
    custom.foreground_palette=true;custom.foreground_colors[3]=0x123456;custom.active_mask=5;
    std::swap(custom.order[0],custom.order[2]);
    const auto words=csp::encode(custom);
    auto raw=csp::encode_panel_config(custom);
    check(raw.size()==words.size()*4,"payload length unchanged");
    for(size_t i=0;i<raw.size();++i)check(raw[i]==uint8_t(words[i/4]>>((i%4)*8)),"payload byte identical to legacy CUI little endian encoding");
    for(unsigned version=1;version<=6;++version){
        auto legacy=words;legacy[0]=version;legacy.resize(version==1?11:version==2?12:version==3?77:version==4?78:version==5?79:80);
        std::vector<uint8_t> unaligned(legacy.size()*4+1);
        for(size_t i=0;i<legacy.size();++i)for(unsigned n=0;n<4;++n)unaligned[1+i*4+n]=uint8_t(legacy[i]>>(n*8));
        csp::Settings expected,actual;check(csp::decode(legacy,expected),"legacy reference decode");
        check(csp::decode_panel_config(unaligned.data()+1,unaligned.size()-1,actual),"unaligned legacy UI payload accepted");
        check(csp::encode(actual)==csp::encode(expected),"legacy defaults and custom values preserved across adapters");
    }
    for(size_t size=1;size<raw.size();++size){csp::Settings result=custom;check(!csp::decode_panel_config(raw.data(),size,result),"truncated config rejected");check(csp::encode(result)==words,"invalid config leaves existing values intact");}
    for(auto invalid: {0u,7u,0xffffffffu}){auto data=raw;for(unsigned n=0;n<4;++n)data[n]=uint8_t(invalid>>(n*8));csp::Settings result=custom;check(!csp::decode_panel_config(data.data(),data.size(),result),"unknown config version rejected");check(csp::encode(result)==words,"unknown version transactional");}
    csp::Settings result=custom;
    check(!csp::decode_panel_config(nullptr,4,result),"null nonempty rejected");
    check(!csp::decode_panel_config(raw.data(),4100,result),"oversized payload rejected before read");
    check(csp::decode_panel_config(nullptr,0,result) && csp::encode(result)==csp::encode(csp::Settings{}),"empty config retains default behavior");
}
void adapter_tests(HWND parent){
    service_ptr_t<Callback> callback=new service_impl_t<Callback>();
    service_ptr_t<csp::DefaultElement<TestView>> element=new service_impl_t<csp::DefaultElement<TestView>>();
    auto defaults=element->get_default_configuration();
    check(defaults->get_guid()==csp::default_ui_guid && csp::default_ui_guid!=csp::panel_guid,"new DUI identity distinct from preserved CUI identity");
    check(bytes(defaults)==csp::encode_panel_config(csp::Settings{}),"default payload unchanged");
    callback->visible=false;
    auto first=element->instantiate(parent,defaults,callback);auto* view=TestView::last;
    check(!view->visible_,"initial host visibility consulted");
    check((GetWindowLongPtr(first->get_wnd(),GWL_STYLE)&WS_VISIBLE)!=0,"Default UI child shown without relying on Columns UI host");
    check(!view->editing(),"normal context menu enabled");callback->editing=true;check(view->editing(),"edit mode read dynamically");
    first->notify(ui_element_notify_visibility_changed,1,nullptr,0);check(view->visible_,"host show forwarded");
    first->notify(ui_element_notify_visibility_changed,0,nullptr,0);check(!view->visible_,"host hide forwarded");
    first->notify(ui_element_notify_colors_changed,0,nullptr,0);first->notify(ui_element_notify_font_changed,0,nullptr,0);check(view->themes==2,"host appearance notifications forwarded");
    auto second=element->instantiate(parent,defaults,callback);
    csp::Settings custom;custom.hold=357;custom.range=133;custom.foreground=0x123456;custom.fps_milli=59000;
    view->apply_settings(custom);
    auto saved=first->get_configuration();check(bytes(saved)==csp::encode_panel_config(custom),"saving captures current applied settings");
    check(bytes(second->get_configuration())==bytes(defaults),"multiple instances isolated");
    first->edit_mode_context_menu_command({},false,1015,1000);
    custom.style=csp::Style::SimpleLine;check(bytes(first->get_configuration())==csp::encode_panel_config(custom),"right-click mutation saved with menu offset");
    check(bytes(saved)!=bytes(first->get_configuration()),"returned configuration is immutable snapshot");
    second->set_configuration(first->get_configuration());check(bytes(second->get_configuration())==bytes(first->get_configuration()),"layout copy reproduces configuration");
    for(auto invalid:{ui_element_config::ptr(),ui_element_config::g_create_empty(csp::panel_guid),ui_element_config::g_create(csp::default_ui_guid,"x",1)}){
        bool rejected=false;try{first->set_configuration(invalid);}catch(const exception_io_data&){rejected=true;}
        check(rejected && bytes(first->get_configuration())==csp::encode_panel_config(custom),"invalid replacement leaves live configuration unchanged");
    }
    HMENU menu=CreatePopupMenu();first->edit_mode_context_menu_build({},false,menu,1000);check(GetMenuItemID(menu,0)==1001,"edit menu IDs rebased");DestroyMenu(menu);
    const auto hwnd=first->get_wnd();DestroyWindow(hwnd);check(!first->get_wnd(),"host-first HWND destruction clears window");
    check(bytes(first->get_configuration())==csp::encode_panel_config(custom),"configuration survives host-first window destruction");first.release();
    const auto other=second->get_wnd();second.release();check(!IsWindow(other),"service-first destruction destroys HWND");
    bool null_rejected=false;try{element->instantiate(parent,defaults,nullptr);}catch(const exception_io_data&){null_rejected=true;}
    check(null_rejected && TestView::live==0,"missing host callback rejected without allocating an instance");
    bool config_rejected=false;try{element->instantiate(parent,ui_element_config::g_create_empty(csp::panel_guid),callback);}catch(const exception_io_data&){config_rejected=true;}
    check(config_rejected && TestView::live==0,"constructor failure releases view and callback references");
    TestView::fail_create=true;bool failed=false;try{element->instantiate(parent,defaults,callback);}catch(const std::runtime_error&){failed=true;}TestView::fail_create=false;
    check(failed && TestView::live==0,"failed instantiate releases instance");
    auto modal=element->instantiate(parent,defaults,callback);HWND modal_window=modal->get_wnd();
    TestView::modal=[&]{modal.release();check(TestView::live==1 && IsWindow(modal_window),"edit menu retains service during modal configuration");};
    modal->edit_mode_context_menu_command({},false,1001,1000);TestView::modal={};
    check(TestView::live==0 && !IsWindow(modal_window),"modal reference released after command");
    auto throwing=element->instantiate(parent,defaults,callback);
    TestView::modal=[&]{throwing.release();check(TestView::live==1,"service retained during throwing modal command");throw std::runtime_error("injected modal failure");};
    bool threw=false;try{throwing->edit_mode_context_menu_command({},false,1001,1000);}catch(const std::runtime_error&){threw=true;}TestView::modal={};
    check(threw && TestView::live==0,"throwing modal command releases retained service");
    HWND nested_parent=CreateWindowExW(0,L"STATIC",L"",WS_POPUP,0,0,200,100,nullptr,nullptr,GetModuleHandle(nullptr),nullptr);
    auto nested=element->instantiate(nested_parent,defaults,callback);DestroyWindow(nested_parent);
    check(!nested->get_wnd(),"parent destruction clears child HWND before service release");nested.release();
    service_ptr_t<ColumnsHost> host=new service_impl_t<ColumnsHost>(),new_host=new service_impl_t<ColumnsHost>();
    static uie::window_factory<csp::ColumnsPanel<TestView>> factory;
    service_ptr_t<service_base> instance;factory.instance_create(instance);
    uie::window_ptr columns;check(instance->service_query_t(columns),"CUI factory exposes window interface");instance.release();
    constexpr GUID original_guid={0x0c45a893,0x4607,0x4914,{0x8e,0x64,0x21,0x89,0xb3,0xa2,0x79,0x70}};
    check(columns->get_extension_guid()==original_guid,"CUI GUID unchanged");
    ui_helpers::window_position_t pos(0,0,100,50);
    HWND cw=columns->create_or_transfer_window(parent,host,pos);check(IsWindow(cw),"CUI window creation");
    stream_writer_memblock output;columns->get_config(&output,fb2k::noAbort);
    check(output.m_data.get_size()==bytes(defaults).size() && !memcmp(output.m_data.get_ptr(),defaults->get_data(),output.m_data.get_size()),"CUI and DUI default payloads identical");
    HWND parent2=CreateWindowExW(0,L"STATIC",L"",WS_POPUP,0,0,200,100,nullptr,nullptr,GetModuleHandle(nullptr),nullptr);
    host->relinquish=[&](HWND w){check(GetParent(w)==parent2,"CUI reparents before relinquishing original host");};
    check(columns->create_or_transfer_window(parent2,new_host,pos)==cw,"CUI transfer preserves HWND");
    columns->destroy_window();check(!IsWindow(cw),"CUI explicit destruction");
    check(IsWindow(columns->create_or_transfer_window(parent,host,pos)),"CUI instance supports reopen");
    TestView::modal=[&]{columns.release();check(TestView::live==1,"CUI configuration retains service");};
    columns->show_config_popup(parent);TestView::modal={};check(TestView::live==0,"CUI modal destruction completes");
    DestroyWindow(parent2);callback.release();check(Callback::live==0,"host callback released with last instance");
}
}
int main(){
    try{
        config_tests();
        for(int n=0;n<25;++n){
            HWND parent=CreateWindowExW(0,L"STATIC",L"",WS_POPUP,0,0,200,100,nullptr,nullptr,GetModuleHandle(nullptr),nullptr);
            check(parent!=nullptr,"hidden native test parent created");adapter_tests(parent);DestroyWindow(parent);
            check(TestView::live==0 && Callback::live==0,"repeated lifecycle releases all adapters and callbacks");
        }
        std::cout<<"PASS: "<<checks<<" SDK adapter checks; simulated rendering window and host callbacks, not foobar2000 integration.\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
