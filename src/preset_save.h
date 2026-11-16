#pragma once
#include <windows.h>
#include <filesystem>
#include <span>
#include <stdexcept>
namespace csp {
struct PresetFileOps {
    bool write(HANDLE file,std::span<const unsigned char> bytes){DWORD written=0;return WriteFile(file,bytes.data(),DWORD(bytes.size()),&written,nullptr) && written==bytes.size();}
    bool flush(HANDLE file){return FlushFileBuffers(file)!=FALSE;}
    bool close(HANDLE file){return CloseHandle(file)!=FALSE;}
    bool replace(const wchar_t* temp,const wchar_t* target){return MoveFileExW(temp,target,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;}
};
// The destination remains untouched until writing, flushing and closing succeed.
template<class Ops> void finish_preset_save(HANDLE& file,const wchar_t* temp,const wchar_t* target,std::span<const unsigned char> bytes,Ops& ops){
    if(!ops.write(file,bytes))throw std::runtime_error("Could not write preset temporary file.");
    if(!ops.flush(file))throw std::runtime_error("Could not flush preset temporary file.");
    HANDLE closing=file;file=INVALID_HANDLE_VALUE;
    if(!ops.close(closing))throw std::runtime_error("Could not close preset temporary file.");
    if(!ops.replace(temp,target))throw std::runtime_error("Could not replace preset; original file is unchanged.");
}
inline void save_preset_atomic(const std::filesystem::path& path,std::span<const unsigned char> bytes){
    const auto target=std::filesystem::absolute(path);
    wchar_t temp[MAX_PATH]{};
    if(!GetTempFileNameW(target.parent_path().c_str(),L"csp",0,temp))throw std::runtime_error("Could not create preset temporary file.");
    HANDLE file=CreateFileW(temp,GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    try {
        if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("Could not open preset temporary file.");
        PresetFileOps ops;finish_preset_save(file,temp,target.c_str(),bytes,ops);
    }catch(...){if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);DeleteFileW(temp);throw;}
}
}
