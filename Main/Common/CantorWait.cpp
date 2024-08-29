#include "CantorWait.h"

#if (!WIN32)
#include <time.h>
#include <semaphore.h>
#endif

#if defined(__APPLE__)
#include <pthread.h>
#include <semaphore.h>
#include <sys/time.h>
#include <errno.h>

static int sem_timedwait(sem_t* sem, const struct timespec* abs_timeout) {
    pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
    int result = 0;

    pthread_mutex_lock(&mtx);
    while (sem_trywait(sem) != 0) {
        result = pthread_cond_timedwait(&cond, &mtx, abs_timeout);
        if (result == ETIMEDOUT) break;
    }
    pthread_mutex_unlock(&mtx);

    pthread_cond_destroy(&cond);
    pthread_mutex_destroy(&mtx);

    return result;
}

#endif

CantorWait::CantorWait(bool autoReset)
{
    m_autoReset = autoReset;
    PasWaitHandle handle = NULL;
#if (WIN32)
    HANDLE hEvt = CreateEvent(NULL, autoReset ? FALSE : TRUE, FALSE, NULL);
    handle = hEvt;
#else
    sem_t* pWait = new sem_t;
    if (sem_init(pWait, 0, 0) != -1)
    {
        handle = pWait;
    }
    else
    {
        delete pWait;
    }
#endif
    m_waitHandle = handle;
}

CantorWait::~CantorWait()
{
    if (m_waitHandle)
    {
#if (WIN32)
        HANDLE hEvt = (HANDLE)m_waitHandle;
        CloseHandle(hEvt);
#else
        sem_t* pWait = (sem_t*)m_waitHandle;
        delete pWait;
#endif
    }
}

bool CantorWait::Wait(int timeoutMS)
{
    if (m_waitHandle == nullptr)
    {
        return false;
    }
#if (WIN32)
    return (WAIT_OBJECT_0 == ::WaitForSingleObject(
        (HANDLE)m_waitHandle, timeoutMS));
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) == -1)
    {
        return false;
    }
    if (timeoutMS == -1)
    {
        ts.tv_sec += 3600 * 24;
    }
    else
    {
        ts.tv_nsec += timeoutMS * 1000000;
        if (ts.tv_nsec >= 1000000000)
        {
            ts.tv_sec += ts.tv_nsec / 1000000000;
            ts.tv_nsec %= 1000000000;
        }
    }
    int ret = sem_timedwait((sem_t*)m_waitHandle, &ts);
    if (ret == -1)
    {
        return false;
    }
    else
    {
        if (m_autoReset)
        {
            //sem_post((sem_t*)m_waitHandle);
        }
        return true;
    }
#endif
}

void CantorWait::Release()
{
    if (m_waitHandle)
    {
#if (WIN32)
        SetEvent((HANDLE)m_waitHandle);
#else
        sem_post((sem_t*)m_waitHandle);
#endif
    }
}

int CantorWait::WaitOne(std::vector<CantorWait*>& waits, int timeoutMS)
{
#if (WIN32)
    std::vector<HANDLE> handles;
    for (CantorWait* wait : waits)
    {
        handles.push_back((HANDLE)wait->m_waitHandle);
    }
    DWORD result = WaitForMultipleObjects((DWORD)handles.size(), handles.data(), FALSE, timeoutMS);
    if (result >= WAIT_OBJECT_0 && result < WAIT_OBJECT_0 + handles.size())
    {
        return result - WAIT_OBJECT_0;
    }
    else
    {
        return -1; // Timeout or error
    }
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) == -1)
    {
        return -1;
    }
    if (timeoutMS != -1)
    {
        ts.tv_nsec += timeoutMS * 1000000;
        if (ts.tv_nsec >= 1000000000)
        {
            ts.tv_sec += ts.tv_nsec / 1000000000;
            ts.tv_nsec %= 1000000000;
        }
    }

    for (int i = 0; i < waits.size(); ++i)
    {
        CantorWait* wait = waits[i];
        int ret = sem_timedwait((sem_t*)wait->m_waitHandle, &ts);
        if (ret == 0) // Signaled
        {
            if (wait->m_autoReset)
            {
                // Optionally re-lock the semaphore if auto-reset is enabled
            }
            return i;
        }
    }
    return -1; // Timeout
#endif
}
