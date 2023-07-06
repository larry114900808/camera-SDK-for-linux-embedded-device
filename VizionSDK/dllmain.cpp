// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"
#include "VzLog.h"

extern std::unique_ptr<VzLog> vzlog;

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        vzlog = std::make_unique<VzLog>("vzlog.txt");
        break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

