#pragma once
#include "config.h"
#include "original_config.h"
namespace csp {
// File decoding differs from empty host configuration: a header-only CSMX is corrupt.
inline bool decode_preset(std::span<const unsigned char> bytes,Settings& settings){
    if(bytes.size()<4 || bytes.size()>4096 || bytes.size()%4)return false;
    const bool mirror=bytes[0]=='C' && bytes[1]=='S' && bytes[2]=='M' && bytes[3]=='X';
    if(mirror && bytes.size()==4)return false;
    std::vector<uint32_t> words;
    for(size_t i=mirror?4:0;i<bytes.size();i+=4)
        words.push_back(uint32_t(bytes[i])|uint32_t(bytes[i+1])<<8|uint32_t(bytes[i+2])<<16|uint32_t(bytes[i+3])<<24);
    Settings next=settings;
    if(!(mirror?decode(words,next):decode_original_csp(words,next)))return false;
    settings=next;return true;
}
}
