#pragma once
#include "../src/background_save.h"
inline void save_conflict_tests(){
    auto waitResult=[](const std::shared_ptr<csp::SaveResult>& result){
        auto until=GetTickCount64()+5000;
        while(result->status.load(std::memory_order_acquire)==csp::SaveStatus::running && GetTickCount64()<until)Sleep(1);
        const auto status=result->status.load(std::memory_order_acquire);
        if(status==csp::SaveStatus::running)std::cerr<<"Save diagnostic: completion timed out after 5000 ms\n";
        if(status==csp::SaveStatus::failed && std::string(result->error)!="injected write failure")std::cerr<<"Save diagnostic: "<<result->error<<"\n";
        return status;
    };
    struct Gate {
        HANDLE entered=CreateEventW(nullptr,TRUE,FALSE,nullptr),release=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        ~Gate(){CloseHandle(entered);CloseHandle(release);}
    };
    auto gate=std::make_shared<Gate>();
    check(gate->entered && gate->release,"create path-conflict test events");
    auto dir=std::filesystem::path("build")/("save-conflict-"+std::to_string(GetCurrentProcessId()));
    check(CreateDirectoryW(dir.c_str(),nullptr)!=FALSE,"create isolated path-conflict directory");
    auto a=dir/L"preset.csm",b=dir/L"other.csm";
    const std::vector<unsigned char> oldBytes{1,2,3,4},newBytes{5,6,7,8};
    auto pending=csp::start_path_save(a,[gate,oldBytes](const std::filesystem::path& target){
        SetEvent(gate->entered);
        if(WaitForSingleObject(gate->release,5000)!=WAIT_OBJECT_0)throw std::runtime_error("test gate timeout");
        csp::save_preset_atomic(target,oldBytes);
    });
    check(WaitForSingleObject(gate->entered,5000)==WAIT_OBJECT_0,"first path save is held in background");
    auto conflict=[&](const std::filesystem::path& target){
        try{csp::start_preset_save(target,newBytes);return false;}catch(const std::runtime_error&){return true;}
    };
    check(conflict(a),"same target rejects overlapping save");
    check(conflict(std::filesystem::absolute(a)),"relative and absolute target share reservation");
    check(conflict(dir/L"."/L"unused"/L".."/L"preset.csm"),"dot and dot-dot aliases share reservation");
    check(conflict(dir/L"PRESET.CSM"),"case aliases share reservation");
    std::weak_ptr<csp::SaveResult> closed=pending;
    pending.reset(); // Closing the first dialog does not release the path lease.
    check(conflict(a),"reopened dialog cannot overwrite an unfinished save");
    auto independent=csp::start_preset_save(b,newBytes);
    check(waitResult(independent)==csp::SaveStatus::succeeded,"different target completes while first target remains blocked");
    check(!closed.expired(),"blocked job remains alive without dialog ownership");
    SetEvent(gate->release);
    auto until=GetTickCount64()+5000;
    while(!closed.expired() && GetTickCount64()<until)Sleep(1);
    check(closed.expired(),"finished orphaned save releases task ownership");
    auto read=[](const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);return std::vector<unsigned char>(std::istreambuf_iterator<char>(in),{});};
    check(read(a)==oldBytes && read(b)==newBytes,"rejected overlapping save never commits its bytes");
    auto retry=csp::start_preset_save(a,newBytes);
    check(waitResult(retry)==csp::SaveStatus::succeeded && read(a)==newBytes,"same target can save after orphaned job finishes");
    // Keep the completed result alive: its lifetime must not hold the reservation.
    auto again=csp::start_preset_save(a,oldBytes);
    check(waitResult(again)==csp::SaveStatus::succeeded,"completed UI result does not retain target reservation");
    auto failure=csp::start_path_save(a,[](const std::filesystem::path&){throw std::runtime_error("injected write failure");});
    check(waitResult(failure)==csp::SaveStatus::failed,"failed operation reports failure");
    auto afterFailure=csp::start_preset_save(a,newBytes);
    check(waitResult(afterFailure)==csp::SaveStatus::succeeded,"failed operation releases path before publishing completion");
    bool rejected=false;
    try{csp::start_path_save(a,[](const std::filesystem::path&){},reject_save_work);}catch(const std::runtime_error&){rejected=true;}
    check(rejected,"submission failure is surfaced for reserved path");
    auto afterReject=csp::start_preset_save(a,oldBytes);
    check(waitResult(afterReject)==csp::SaveStatus::succeeded,"submission failure releases path reservation");
    check(DeleteFileW(a.c_str()) && DeleteFileW(b.c_str()) && RemoveDirectoryW(dir.c_str()),"path-conflict tests clean files");
}
