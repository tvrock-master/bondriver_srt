#pragma once

#include "IBonDriver.h"

class IBonDriver2 : public IBonDriver {
  public:
    virtual LPCTSTR GetTunerName() = 0;
    virtual const BOOL IsTunerOpening() = 0;
    virtual LPCTSTR EnumTuningSpace(DWORD space) = 0;
    virtual LPCTSTR EnumChannelName(DWORD space, DWORD channel) = 0;
    virtual const BOOL SetChannel(DWORD space, DWORD channel) = 0;
    virtual const DWORD GetCurSpace() = 0;
    virtual const DWORD GetCurChannel() = 0;
    virtual void Release() = 0;
};
