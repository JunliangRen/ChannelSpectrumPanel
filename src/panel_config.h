#pragma once
#include "config.h"
namespace csp {
inline std::vector<uint8_t> encode_panel_config(const Settings& settings){
    const auto words=encode(settings);std::vector<uint8_t> bytes(words.size()*4);
    for(size_t i=0;i<words.size();++i)for(unsigned n=0;n<4;++n)bytes[i*4+n]=uint8_t(words[i]>>(n*8));
    return bytes;
}
inline bool decode_panel_config(const void* data,size_t size,Settings& result){
    if(size>4096 || size%4 || (size && !data))return false;
    const auto* bytes=static_cast<const uint8_t*>(data);std::vector<uint32_t> words(size/4);
    for(size_t i=0;i<words.size();++i)for(unsigned n=0;n<4;++n)words[i]|=uint32_t(bytes[i*4+n])<<(n*8);
    return decode(words,result);
}
}
