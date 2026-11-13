#pragma once

#include <windows.h>

// Keep this virtual-function order and the lack of a virtual destructor:
// these declarations define the binary interface used by BonDriver hosts.
class IBonDriver {
  public:
    virtual const BOOL OpenTuner() = 0;
    virtual void CloseTuner() = 0;
    virtual const BOOL SetChannel(BYTE channel) = 0;
    virtual const float GetSignalLevel() = 0;
    virtual const DWORD WaitTsStream(DWORD timeout = 0) = 0;
    virtual const DWORD GetReadyCount() = 0;
    virtual const BOOL GetTsStream(BYTE *destination, DWORD *size, DWORD *remaining) = 0;
    virtual const BOOL GetTsStream(BYTE **destination, DWORD *size, DWORD *remaining) = 0;
    virtual void PurgeTsStream() = 0;
    virtual void Release() = 0;
};

#ifdef BONDRIVER_SRT_EXPORTS
#define BONDRIVER_API __declspec(dllexport)
#else
#define BONDRIVER_API __declspec(dllimport)
#endif

extern "C" BONDRIVER_API IBonDriver *CreateBonDriver();
