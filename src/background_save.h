#pragma once
#include <windows.h>
#include <atomic>
#include <memory>
#include <functional>
#include <cstring>
#include <vector>
#include <set>
#include "preset_save.h"
namespace csp {
enum class SaveStatus {running,succeeded,failed};
struct SaveResult {
    std::atomic<SaveStatus> status{SaveStatus::running};
    char error[256]{}; // Read only after an acquire load observes failed.
};
namespace save_detail {
// Lexical normalization avoids doing network/filesystem I/O on the dialog thread.
inline std::filesystem::path save_path(std::filesystem::path path){
    return std::filesystem::absolute(path).lexically_normal().make_preferred();
}
struct PathLess {
    bool operator()(const std::wstring& a,const std::wstring& b) const noexcept {
        return CompareStringOrdinal(a.c_str(),int(a.size()),b.c_str(),int(b.size()),TRUE)==CSTR_LESS_THAN;
    }
};
struct Registry {SRWLOCK lock=SRWLOCK_INIT;std::set<std::wstring,PathLess> paths;};
inline Registry& registry(){static Registry value;return value;}
struct RegistryLock {
    SRWLOCK& lock;
    explicit RegistryLock(SRWLOCK& value):lock(value){AcquireSRWLockExclusive(&lock);}
    ~RegistryLock(){ReleaseSRWLockExclusive(&lock);}
};
class PathLease {
public:
    explicit PathLease(const std::filesystem::path& path):key_(path.native()){
        auto& r=registry();RegistryLock lock(r.lock);
        if(!r.paths.insert(key_).second)throw std::runtime_error("A save to this preset path is still in progress. Try again after it finishes.");
    }
    ~PathLease(){auto& r=registry();RegistryLock lock(r.lock);r.paths.erase(key_);}
    PathLease(const PathLease&)=delete;PathLease& operator=(const PathLease&)=delete;
private:std::wstring key_;
};
struct Work {std::shared_ptr<SaveResult> result;std::function<void()> operation;std::unique_ptr<PathLease> lease;HMODULE module=nullptr;};
inline void CALLBACK run(PTP_CALLBACK_INSTANCE instance,void* context) noexcept {
    std::unique_ptr<Work> work(static_cast<Work*>(context));
    // The thread pool releases the module only AFTER local destructors and this callback return.
    FreeLibraryWhenCallbackReturns(instance,work->module);
    CallbackMayRunLong(instance);
    SaveStatus status=SaveStatus::succeeded;
    try {work->operation();}
    catch(const std::exception& e){strncpy_s(work->result->error,e.what(),_TRUNCATE);status=SaveStatus::failed;}
    catch(...){strcpy_s(work->result->error,"Could not save preset.");status=SaveStatus::failed;}
    work->lease.reset(); // Completion permits an immediate retry, even if the result stays alive.
    work->result->status.store(status,std::memory_order_release);
}
}
// Work owns all inputs. It never receives a dialog, panel pointer, HWND, or UI callback.
using SaveSubmit=BOOL(WINAPI*)(PTP_SIMPLE_CALLBACK,PVOID,PTP_CALLBACK_ENVIRON);
inline std::shared_ptr<SaveResult> start_background_work(std::function<void()> operation,SaveSubmit submit=TrySubmitThreadpoolCallback,std::unique_ptr<save_detail::PathLease> lease={}){
    auto result=std::make_shared<SaveResult>();
    auto work=std::make_unique<save_detail::Work>();work->result=result;work->operation=std::move(operation);work->lease=std::move(lease);
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
        reinterpret_cast<LPCWSTR>(&save_detail::run),&work->module))throw std::runtime_error("Could not retain component for background save.");
    // Publish ownership BEFORE queuing: the callback may finish before submission returns.
    auto* raw=work.release();
    if(!submit(save_detail::run,raw,nullptr)){
        const auto module=raw->module;delete raw;FreeLibrary(module);
        throw std::runtime_error("Could not start background save.");
    }
    return result;
}
// Reservation spans queued and executing work, independent of any dialog lifetime.
inline std::shared_ptr<SaveResult> start_path_save(std::filesystem::path path,
    std::function<void(const std::filesystem::path&)> operation,SaveSubmit submit=TrySubmitThreadpoolCallback){
    path=save_detail::save_path(std::move(path));
    auto lease=std::make_unique<save_detail::PathLease>(path);
    return start_background_work([path=std::move(path),operation=std::move(operation)]{operation(path);},submit,std::move(lease));
}
inline std::shared_ptr<SaveResult> start_preset_save(std::filesystem::path path,std::vector<unsigned char> bytes){
    return start_path_save(std::move(path),[bytes=std::move(bytes)](const std::filesystem::path& target){save_preset_atomic(target,bytes);});
}
}
