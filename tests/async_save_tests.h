#pragma once
#include "../src/background_save.h"
inline BOOL WINAPI reject_save_work(PTP_SIMPLE_CALLBACK,PVOID,PTP_CALLBACK_ENVIRON){return FALSE;}
inline void async_save_tests(){
    csp::SnapshotState snapshot;snapshot.capture(false);
    check(snapshot.valid && !snapshot.mode_for_redraw(true),"paused old FFT snapshot retains old normalization after settings change");
    snapshot.capture(true);
    check(snapshot.mode_for_redraw(false),"paused new FFT snapshot retains new normalization");
    for(int event=0;event<4;++event){snapshot.capture(true);snapshot.invalidate();check(!snapshot.valid,"start/new-track/stop/seek invalidates retained snapshot even while hidden");}
    snapshot.capture(false);check(snapshot.valid && !snapshot.mode_for_redraw(true),"new successful frame replaces snapshot mode");
    auto waitResult=[](const std::shared_ptr<csp::SaveResult>& result){
        const auto until=GetTickCount64()+5000;
        while(result->status.load(std::memory_order_acquire)==csp::SaveStatus::running && GetTickCount64()<until)Sleep(1);
        return result->status.load(std::memory_order_acquire);
    };
    bool rejected=false;
    try{csp::start_background_work([]{},reject_save_work);}catch(const std::runtime_error&){rejected=true;}
    check(rejected,"threadpool submission failure is reported without leaking job ownership");
    auto fail=csp::start_background_work([]{throw std::runtime_error("injected storage failure");});
    check(waitResult(fail)==csp::SaveStatus::failed && std::string(fail->error)=="injected storage failure","worker exceptions become owned completion results");
    auto unknown=csp::start_background_work([]{throw 7;});
    check(waitResult(unknown)==csp::SaveStatus::failed,"nonstandard worker exceptions are contained");
    struct Gate {
        HANDLE entered=CreateEventW(nullptr,TRUE,FALSE,nullptr),release=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        ~Gate(){CloseHandle(entered);CloseHandle(release);}
    };
    auto gate=std::make_shared<Gate>();
    check(gate->entered && gate->release,"create background lifetime test events");
    const DWORD caller=GetCurrentThreadId();auto worker=std::make_shared<std::atomic<DWORD>>(0);
    auto pending=csp::start_background_work([gate,worker]{*worker=GetCurrentThreadId();SetEvent(gate->entered);WaitForSingleObject(gate->release,5000);});
    check(WaitForSingleObject(gate->entered,5000)==WAIT_OBJECT_0,"blocked storage runs on worker");
    check(worker->load()!=caller && pending->status.load()==csp::SaveStatus::running,"blocked save does not block caller or falsely complete");
    std::weak_ptr<csp::SaveResult> abandoned=pending;
    pending.reset(); // Equivalent to destroying a dialog while its save is blocked.
    check(!abandoned.expired(),"worker owns result after dialog releases it");
    SetEvent(gate->release);
    const auto until=GetTickCount64()+5000;
    while(!abandoned.expired() && GetTickCount64()<until)Sleep(1);
    check(abandoned.expired(),"closed dialog save releases state after completion without UI access");
    auto fresh=csp::start_background_work([]{});
    check(waitResult(fresh)==csp::SaveStatus::succeeded,"new dialog job has independent completion state");

    auto dir=std::filesystem::path("build")/("async-save-"+std::to_string(GetCurrentProcessId()));
    check(CreateDirectoryW(dir.c_str(),nullptr)!=FALSE,"create async IO test directory");
    auto path=dir/L"preset.csm";
    std::vector<unsigned char> bytes{1,2,3,4};
    auto actual=csp::start_preset_save(path,bytes);bytes.assign(4,99);
    check(waitResult(actual)==csp::SaveStatus::succeeded,"real background preset save completes");
    {std::ifstream file(path,std::ios::binary);std::vector<unsigned char> got(std::istreambuf_iterator<char>(file),{});
        check(got==std::vector<unsigned char>({1,2,3,4}),"background save owns immutable input bytes");}
    check(DeleteFileW(path.c_str()) && RemoveDirectoryW(dir.c_str()),"async IO test cleans files");
}
