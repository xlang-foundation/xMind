#pragma once
#include "singleton.h"
#include "port.h"
#include "xlang.h"
#include <unordered_map>
#include <string>
#include <mutex>

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
	public:
		Starter() = default;
		~Starter() = default;

		void Start(std::string& appPath);
		void Stop();
		std::vector<std::string> GetRootAgents();
		X::Value GetOrCreateRunningGraph(const std::string& rootAgent);
	private:
		std::mutex m_rootAgents_mtx;
		std::unordered_map<std::string, RootAgentDetail> m_rootAgents;
	};
}