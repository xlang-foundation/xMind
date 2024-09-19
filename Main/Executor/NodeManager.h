#pragma once
#include <vector>
#include <unordered_map>
#include <string>
#include "singleton.h"
#include "Callable.h"

namespace xMind
{
	class NodeManager : public Singleton<NodeManager>
	{
	public:
		NodeManager() = default;
		~NodeManager() = default;

		// Add a Callable to the list and map
		bool addCallable(const std::string& name, Callable* callable)
		{
			//check same name callable need to release previous one
			auto it = callablesMap.find(name);
			if (it != callablesMap.end())
			{
				return false;
			}
			callablesList.push_back(callable);
			callablesMap[name] = callable;
			return true;
		}

		// Get a Callable by name
		Callable* getCallable(const std::string& name) const
		{
			auto it = callablesMap.find(name);
			if (it != callablesMap.end())
			{
				return it->second;
			}
			return nullptr;
		}

		// Get all Callables
		const std::vector<Callable*>& getAllCallables() const
		{
			return callablesList;
		}

	private:
		std::vector<Callable*> callablesList;
		std::unordered_map<std::string, Callable*> callablesMap;
	};
}
