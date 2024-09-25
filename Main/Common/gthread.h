/*
Copyright (C) 2024 The XLang Foundation

This file is part of the xMind Project.

xMind is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

xMind is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with xMind. If not, see <https://www.gnu.org/licenses/>.
*/

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