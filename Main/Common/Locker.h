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

#ifndef Locker_H
#define Locker_H

#ifdef WIN32
#include <Windows.h>
#else
#include <mutex> 
#endif
class Locker
{
public:
	Locker();
	~Locker();

	inline void Lock()
	{
#ifndef WIN32
		m_cs.lock();
#else
		::EnterCriticalSection(&m_cs);
#endif
	}

	inline void Unlock()
	{
#ifndef WIN32
		m_cs.unlock();
#else
		::LeaveCriticalSection(&m_cs);
#endif
	}

private:
#ifndef WIN32
	std::recursive_mutex m_cs;
#else
	CRITICAL_SECTION m_cs;
#endif
};

class AutoLock
{
public:
	AutoLock() :
		m_lock(NULL)
	{
	}

	AutoLock(Locker& lk)
	{
		m_lock = &lk;
		lk.Lock();
	}

	~AutoLock()
	{
		if (m_lock)
		{
			m_lock->Unlock();
		}
	}

private:
	Locker* m_lock;
};

#endif //Locker_H
