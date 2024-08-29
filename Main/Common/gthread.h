#ifndef __gthread_h__
#define __gthread_h__


class GThread
{
public:
    GThread()
    {

    }
    virtual ~GThread()
    {

    }
    virtual bool Start();
    virtual void Stop();
    virtual void WaitToEnd();
public:
    virtual void run() =0;
protected:
    void* mThreadHandle =nullptr;
};

class GThread2
    :public virtual GThread
{
public:
    virtual bool Start();
    virtual void Stop();
    virtual void WaitToEnd();
    virtual void run2() = 0;
protected:
    void* mThreadHandle2 = nullptr;
};

class GThread3
    :public virtual GThread2
{
public:
    virtual bool Start();
    virtual void Stop();
    virtual void WaitToEnd();
    virtual void run3() = 0;
protected:
    void* mThreadHandle3 = nullptr;
};

#endif //!__gthread_h__