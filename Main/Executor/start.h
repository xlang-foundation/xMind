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
#include "singleton.h"
#include "port.h"
#include "xlang.h"
#include <unordered_map>
#include <string>
#include <mutex>
#include <vector>

namespace xMind
{
	struct RootAgentDetail
	{
		std::string filename;
		X::Value varRunningGraph;//for this root agent
	};

	class Starter : public Singleton<Starter>
	{
		bool ParseConfig(X::Value& root);
		void RunService(const std::string& serviceEntry,int port =9901);
	public:
		Starter() = default;
		~Starter() = default;

		void Start(std::string& appPath);
		void Stop();
		std::vector<std::string> GetRootAgents();
		X::Value GetOrCreateRunningGraph(const std::string& rootAgent);
	private:
		std::string m_appPath;
		std::string m_configPath;
		std::mutex m_rootAgents_mtx;
		std::unordered_map<std::string, RootAgentDetail> m_rootAgents;
	};
}