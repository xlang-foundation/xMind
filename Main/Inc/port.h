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

#pragma once

typedef void* PasWaitHandle;

#if (WIN32)
#define Path_Sep_S "\\"
#define Path_Sep '\\'
#define SCANF sscanf_s
#define SPRINTF sprintf_s
#define STRCMPNOCASE _strnicmp
#define MS_SLEEP(t) Sleep(t)
#define US_SLEEP(t) Sleep(t/1000)
#else
#define Path_Sep_S "/"
#define Path_Sep '/'
#include <strings.h>
#include <memory.h>
#include <netinet/in.h>
#define SCANF sscanf
#define SPRINTF snprintf

#define STRCMPNOCASE strncasecmp
#define MS_SLEEP(t)  usleep((t)*1000)
#define US_SLEEP(t) usleep(t)
#endif


#if (WIN32)
#include <windows.h>
#define SEMAPHORE_HANDLE HANDLE
#define CREATE_SEMAPHORE(sa,name) CreateEvent(&sa, FALSE,FALSE, name)
#define OPEN_SEMAPHORE(name) OpenEvent(EVENT_ALL_ACCESS, FALSE, name)
#define WAIT_FOR_SEMAPHORE(handle, timeout) WaitForSingleObject(handle, timeout)
#define CLOSE_SEMAPHORE(handle) CloseHandle(handle)
#else
#include <fcntl.h>
#include <semaphore.h>
#include <sys/stat.h>
#define SEMAPHORE_HANDLE sem_t*
#define CREATE_SEMAPHORE(sa,name) sem_open(name, O_CREAT | O_EXCL, 0644, 1)
#define OPEN_SEMAPHORE(name) sem_open(name, 0)
#define WAIT_FOR_SEMAPHORE(handle, timeout) sem_wait(handle) // Implement timeout if needed
#define CLOSE_SEMAPHORE(handle) sem_close(handle)
#endif
