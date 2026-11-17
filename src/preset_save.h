#pragma once
#include <windows.h>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#ifdef CSP_SAVE_TEST_DIAGNOSTICS
#include <atomic>
#endif
namespace csp {
[[noreturn]] inline void preset_io_error(const char* stage,DWORD code=GetLastError()){
    throw std::runtime_error(std::string(stage)+" (Win32 error "+std::to_string(code)+")");
}

#ifdef CSP_SAVE_TEST_DIAGNOSTICS
inline std::atomic<unsigned> preset_retry_access{0},preset_retry_sharing{0},preset_retry_lock{0};
#endif
// Retry only replacement failures that can be caused by transient filesystem
// sharing/deletion activity. Never delete the destination to force a commit.
// Persistent permissions/locks still fail after at most 15 x 20 ms delays.
template<class Attempt,class Delay> bool retry_preset_replace(Attempt&& attempt,Delay&& delay){
    for(unsigned i=0;;++i){
        if(attempt())return true;
        const DWORD error=GetLastError();
        if(i==15 || (error!=ERROR_ACCESS_DENIED && error!=ERROR_SHARING_VIOLATION && error!=ERROR_LOCK_VIOLATION)){
            SetLastError(error);return false;
        }
#ifdef CSP_SAVE_TEST_DIAGNOSTICS
        if(error==ERROR_ACCESS_DENIED)++preset_retry_access;
        if(error==ERROR_SHARING_VIOLATION)++preset_retry_sharing;
        if(error==ERROR_LOCK_VIOLATION)++preset_retry_lock;
#endif
        delay(20);
    }
}
struct PresetFileOps {
    bool write(HANDLE file,std::span<const unsigned char> bytes){DWORD written=0;if(!WriteFile(file,bytes.data(),DWORD(bytes.size()),&written,nullptr))return false;if(written!=bytes.size()){SetLastError(ERROR_WRITE_FAULT);return false;}return true;}
    bool flush(HANDLE file){return FlushFileBuffers(file)!=FALSE;}
    bool close(HANDLE file){return CloseHandle(file)!=FALSE;}
    bool replace(const wchar_t* temp,const wchar_t* target){return retry_preset_replace([&]{return MoveFileExW(temp,target,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;},[](DWORD ms){Sleep(ms);});}
};
// The destination remains untouched until writing, flushing and closing succeed.
template<class Ops> void finish_preset_save(HANDLE& file,const wchar_t* temp,const wchar_t* target,std::span<const unsigned char> bytes,Ops& ops){
    if(!ops.write(file,bytes))preset_io_error("Could not write preset temporary file.");
    if(!ops.flush(file))preset_io_error("Could not flush preset temporary file.");
    HANDLE closing=file;file=INVALID_HANDLE_VALUE;
    if(!ops.close(closing))preset_io_error("Could not close preset temporary file.");
    if(!ops.replace(temp,target))preset_io_error("Could not replace preset; original file is unchanged.");
}
inline void save_preset_atomic(const std::filesystem::path& path,std::span<const unsigned char> bytes){
    const auto target=std::filesystem::absolute(path);
    wchar_t temp[MAX_PATH]{};
    if(!GetTempFileNameW(target.parent_path().c_str(),L"csp",0,temp))preset_io_error("Could not create preset temporary file.");
    HANDLE file=CreateFileW(temp,GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    try {
        if(file==INVALID_HANDLE_VALUE)preset_io_error("Could not open preset temporary file.");
        PresetFileOps ops;finish_preset_save(file,temp,target.c_str(),bytes,ops);
    }catch(...){if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);DeleteFileW(temp);throw;}
}
}
