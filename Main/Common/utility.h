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

#include <string>
#include <vector>

void _mkdir(const char* dir);
void ReplaceAll(std::string& data, std::string toSearch, std::string replaceStr);
std::wstring s2ws(const std::string& str);
std::string ws2s(const std::wstring& wstr);
bool exists(const std::string& name);
bool isDir(const std::string& name);
bool dir(std::string search_pat,
    std::vector<std::string>& subfolders,
    std::vector<std::string>& files);
bool IsAbsPath(std::string& path);
bool SplitPath(std::string& path, std::string& leftPath, std::string& rightPath);
void MakeOSPath(std::string& path);
std::vector<std::string> split(const std::string& str, char delim);
void merge(std::vector<std::string>& aryStr,std::string& outStr, char delim);
std::vector<std::string> split(const std::string& str, const char* delim);
bool ParsePythonFunctionCode(std::string& wholecode,std::string& funcBody);
std::string& rtrim(std::string& s);
std::string& ltrim(std::string& s);
std::string& trim(std::string& s);
unsigned long long byteStringToNumber(const char* strBytes, int size);
bool RunProcess(std::string cmd,
	std::string initPath,
	unsigned long& processId,bool WaitFor = false);

std::string GetAppName();
std::string getCurrentTimeString();
long long getCurMilliTimeStamp();
long long getCurTimeStamp();
unsigned long GetPID();
unsigned long GetThreadID();

bool LoadStringFromFile(std::string& fileName, std::string& content);
char* NewFromString(std::string& inStr);
std::string GetComputerName();
bool CheckIfNumber(const std::string& str, long long& value);
bool CheckIfDouble(const std::string& str, double& value);
bool CheckIfNumber(const std::string& str, int& value);
char* NewFromString(std::string& inStr);
