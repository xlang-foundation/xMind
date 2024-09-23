#include "AgentGraphParser.h"
#include "xMindAPI.h"
#include <filesystem>

namespace xMind
{
    bool Parser::ParseRootAgent(const std::string& fileName, X::Value& graph)
    {
        X::Package yaml(xMind::MindAPISet::I().RT(), "yaml", "xlang_yaml");
        X::Value root = yaml["load"](fileName);
        if (root.IsObject() && root.GetObj()->GetType() == X::ObjType::Error)
        {
            return false;

        }
        std::filesystem::path fsPath(fileName);
		std::string moduleName = fsPath.stem().string();
        return ParseAgentGraphDescFromRoot(true, graph, root, moduleName, fileName);
    }
    bool Parser::ParseAgentGraphDesc(const std::string& moduleName, const std::string& fileName)
    {
        X::Package yaml(xMind::MindAPISet::I().RT(), "yaml", "xlang_yaml");
        X::Value root = yaml["load"](fileName);
        if (root.IsObject() && root.GetObj()->GetType() == X::ObjType::Error)
        {
            return false;

        }
        X::Value agentGraph;
        return ParseAgentGraphDescFromRoot(false, agentGraph,root, moduleName, fileName);
    }
    bool Parser::ParseAgentGraphDescFromString(const std::string& desc)
    {
        X::Package yaml(xMind::MindAPISet::I().RT(), "yaml", "xlang_yaml");
        X::Value root = yaml["loads"](desc);
        if (root.IsObject() && root.GetObj()->GetType() == X::ObjType::Error)
        {
            return false;

        }
        X::Value agentGraph;
        return ParseAgentGraphDescFromRoot(false, agentGraph,root);
    }
    
    bool Parser::ParseNodes(X::Value& firstAgent,const X::Value& nodesValue,
        const std::string& moduleName, const std::string& fileName)
    {
        if (!nodesValue.IsList())
        {
            return false;
        }
        std::vector<std::pair<std::string, std::string>> instancePairs;
        X::List list(nodesValue);
        for (const auto item : *list)
        {
            X::Dict nodeItem(item);
            std::string name = nodeItem["name"].ToString();
            std::string instanceName = nodeItem["instanceName"].ToString();
            std::string typeStr = nodeItem["type"].ToString();
            CallableType callableType = CallableType::callable;
            Callable* callable = nullptr;
            X::Value varCallable;
            if (typeStr == "agent")
            {
                callableType = CallableType::agent;
                X::XPackageValue<BaseAgent> valNode;
                callable = valNode.GetRealObj();
                varCallable = valNode;
                if (firstAgent.IsInvalid())
                {
					firstAgent = varCallable;
                }
            }
            else if (typeStr == "action")
            {
                callableType = CallableType::action;
                X::XPackageValue<BaseAction> valNode;
                callable = valNode.GetRealObj();
                varCallable = valNode;
            }
            else if (typeStr == "function")
            {
                callableType = CallableType::function;
                X::XPackageValue<Function> valNode;
                callable = valNode.GetRealObj();
                varCallable = valNode;
            }
            else if (!name.empty())
            {
                //push back when all nodes added,and check back
                instancePairs.push_back(std::make_pair(name, instanceName));

            }
            if (callable == nullptr)
            {
                std::cerr << "Error: Unknown node type: " << typeStr << std::endl;
                continue;
            }
            callable->SetNodeYamlDesc((X::Value&)item);
            callable->SetName(name);
            callable->SetDescription(nodeItem["description"].ToString());

            if (instanceName.empty())
            {
                instanceName = name;
            }
            callable->SetInstanceName(instanceName);
			//Check if has prompts if this is an agent
			if (callableType == CallableType::agent)
			{
                BaseAgent* pAgent = dynamic_cast<BaseAgent*>(callable);
                X::Value promptsValue = nodeItem["prompts"];
                if (promptsValue.IsValid())
                {
                    ParsePrompts(pAgent,moduleName, promptsValue);
                }
				//also for model,selections,temperature
				X::Value modelValue = nodeItem["model"];
				if (modelValue.IsValid())
				{
					pAgent->SetModel(modelValue.ToString());
				}
				X::Value selectionsValue = nodeItem["selections"];
				if (selectionsValue.IsValid())
				{
					pAgent->SetSelections(selectionsValue);
				}
				X::Value temperatureValue = nodeItem["temperature"];
				if (temperatureValue.IsValid())
				{
					pAgent->SetTemperature((double)temperatureValue);
				}
			}
            // Parse inputs
            callable->SetInputs(nodeItem["inputs"]);

            // Parse outputs
            callable->SetOutputs(nodeItem["outputs"]);

            // Parse source
            X::Value sourceValue = nodeItem["source"];
            if (sourceValue.IsDict())
            {
				Source source;
				X::Dict sourceDict(sourceValue);
                //we support python,xlang and shared_lib
				X::Value pythonValue = sourceDict["python"];
                if (pythonValue.IsValid())
                {
					source.python = pythonValue.ToString();
                }
				X::Value xlangValue = sourceDict["xlang"];
                if (xlangValue.IsValid())
                {
                    source.xlang = xlangValue.ToString();
                }
				X::Value sharedLibValue = sourceDict["shared_lib"];
				if (sharedLibValue.IsValid())
				{
					source.shared_lib = sharedLibValue.ToString();
				}
				callable->SetSource(source);
            }

            // Parse parameters
            X::Value parametersValue = nodeItem["parameters"];
            if (parametersValue.IsDict())
            {
                X::Dict parameters(parametersValue);
                X::KWARGS params;
                parameters->Enum([&](X::Value& key, X::Value& value) {
                    params.Add(key.ToString().c_str(), value);
                    });
                callable->SetParams(params);
            }

            // Parse group
            //TODO:
            // callable->SetGroup(nodeItem["group"].ToString());

            NodeManager::I().addCallable(moduleName, fileName, name, varCallable);
            //we add twice, one with instance name, one with name if not identical
            //so for reference we can use either name or instance name
            if (instanceName != name)
            {
                NodeManager::I().addCallable(moduleName, fileName, instanceName, varCallable);
            }
        }
        for (auto& pair : instancePairs)
        {
            X::Value valNodeRefer = QueyNode(moduleName, pair.first);
            //check from NodeManager to find the callable with the name
            X::XPackageValue<Callable> packNodeRefer(valNodeRefer);
            Callable* callableRefer = packNodeRefer.GetRealObj();
            if (callableRefer != nullptr)
            {
                X::Value cloneCallable = callableRefer->Clone();
                X::XPackageValue<Callable> valNode(cloneCallable);
                Callable* pCloneCallable = valNode.GetRealObj();
                pCloneCallable->SetInstanceName(pair.second);
                NodeManager::I().addCallable(moduleName, fileName, pair.second, cloneCallable);
            }
        }

        return true;
    }

}