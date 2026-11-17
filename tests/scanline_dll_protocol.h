#pragma once
#include <windows.h>
struct Probe {
    HANDLE entered=nullptr,release=nullptr,destroyed=nullptr,initialized=nullptr;
    DWORD ui_thread=0;int mode=0;
    volatile LONG released=0,wrong_thread=0;
};
