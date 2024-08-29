#pragma once

#include "port.h"
#include <vector>

class CantorWait
{
public:
    CantorWait(bool autoReset = true);
    ~CantorWait();
    bool Wait(int timeoutMS);
    void Release();

    static int WaitOne(std::vector<CantorWait*>& waits, int timeoutMS);

private:
    bool m_autoReset = true;
    PasWaitHandle m_waitHandle = nullptr;
};
