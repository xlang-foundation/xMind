#include "start.h"
#include <filesystem>
#include <iostream>
#include "xlang.h"
#include "xMindAPI.h"
#include "AgentGraphParser.h"

namespace xMind
{
	std::vector<std::string> Starter::GetRootAgents()
	{
		std::vector<std::string> ret;
		for (auto& item : m_rootAgents)
		{
			ret.push_back(item.first);
		}
		return ret;
	}
	X::Value Starter::GetOrCreateRunningGraph(const std::string& rootAgent)
	{
		std::lock_guard<std::mutex> lock(m_rootAgents_mtx);
		auto it = m_rootAgents.find(rootAgent);
		if (it != m_rootAgents.end())
		{
			if (it->second.varRunningGraph.IsValid())
			{
				return it->second.varRunningGraph;
			}
		}
		Parser parser;
		X::Value graph;
		if (parser.ParseRootAgent(it->second.filename, graph))
		{
			it->second.varRunningGraph = graph;
			return graph;
		}
		return X::Value();
	}
	bool Starter::ParseConfig(X::Value& root)
	{
		X::Dict dict(root);
		X::Value RootAgents = dict["RootAgents"];
		if (RootAgents.IsList())
		{
			X::List list(RootAgents);
			for (const auto item : *list)
			{
				X::Dict nodeItem(item);
				std::string moduleName = nodeItem["name"].ToString();
				std::string fileName = nodeItem["file"].ToString();
				m_rootAgents.emplace(std::make_pair(moduleName, RootAgentDetail{ fileName }));
			}
		}
		return true;
	}
	void Starter::Start(std::string& appPath)
	{
		std::filesystem::path configPath = std::filesystem::path(appPath) / "Config" / "config.yml";
		std::cout << "configPath: " << configPath << std::endl;
		X::Package yaml(xMind::MindAPISet::I().RT(), "yaml", "xlang_yaml");
		X::Value root = yaml["load"](configPath.string());
		if (root.IsObject() && root.GetObj()->GetType() == X::ObjType::Dict)
		{
			ParseConfig(root);
		}
		else
		{
			std::cout << "Failed to load config file: " << configPath << std::endl;
		}

	}

	void Starter::Stop()
	{
	}
}

