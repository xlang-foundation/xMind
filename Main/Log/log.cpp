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

#include "log.h"
#include "port.h"
#include "utility.h"

Cantor::Log Cantor::log;

Cantor::Log::Log()
{
}

Cantor::Log::~Log()
{
	m_limitedSizeLogger.close();
}

bool Cantor::Log::Init()
{
	m_limitedSizeLogger.Start(m_logFileName); 
	return true;
}

Cantor::Log& Cantor::Log::SetCurInfo(const char* fileName,
	const int line, const int level)
{
	m_lock.Lock();
	m_level = level;
	if (m_level <= m_dumpLevel)
	{
		std::string strFileName(fileName);
		auto pos = strFileName.rfind(Path_Sep_S);
		if (pos != std::string::npos)
		{
			strFileName = strFileName.substr(pos + 1);
		}
		else
		{
			//just in case some file path use '/' as separator
			pos = strFileName.rfind('/');
			if (pos != std::string::npos)
			{
				strFileName = strFileName.substr(pos + 1);
			}
		}
		unsigned long pid = GetPID();
		unsigned long tid = GetThreadID();
		//int64_t ts = getCurTimeStamp();
		auto curTime = getCurrentTimeString();
		const int buf_Len = 1000;
		char szFilter[buf_Len];
		SPRINTF(szFilter, buf_Len,"[%d-%d-%s,%s:%d] ", pid, tid,curTime.c_str(), strFileName.c_str(), line);
		LOG_OUT(szFilter);
	}
	return *this;
}
