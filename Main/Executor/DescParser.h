#pragma once
#include "BaseAgent.h"
#include "BaseAction.h"
#include "AgentGraph.h"
#include "Function.h"
#include <string>
#include <fstream>
#include <iostream>

namespace xMind
{
	class DescParser
	{
	public:
		bool LoadFromFile(const std::string& filePath, AgentGraph& graph)
		{
#if 0
			{
				//X::Package yaml(MindAPISet::I().RT(), "yaml");
				//X::Value configRoot = yaml["load"](filePath);

			}
			std::ifstream file(filePath);
			if (!file.is_open())
			{
				return false;
			}
			std::string yamlContent((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
			return Parse(yamlContent, graph);
#else
			return false;
#endif
		}
		bool Parse(const std::string& yamlContent, AgentGraph& graph)
		{
			try
			{
				// Load the YAML content
				X::Package yaml(MindAPISet::I().RT(), "yaml");
				X::Value configRoot = yaml["loads"](yamlContent);

				// Parse general properties
				std::string name = configRoot.Query("name").ToString();
				std::string type = configRoot.Query("type").ToString();
				std::string version = configRoot.Query("version").ToString();
				std::string description = configRoot.Query("description").ToString();

				// Parse inputs
				X::Value inputs = configRoot.Query("inputs");
				if (inputs.IsValid())
				{
					auto inputChildren = inputs["children"]();
					int inputCount = (int)inputChildren.Size();
					for (int i = 0; i < inputCount; i++)
					{
						auto input = inputChildren[i];
						std::string inputName = input.Query("name").ToString();
						// Process input formats if needed
					}
				}

				// Parse outputs
				X::Value outputs = configRoot.Query("outputs");
				if (outputs.IsValid())
				{
					auto outputChildren = outputs["children"]();
					int outputCount = (int)outputChildren.Size();
					for (int i = 0; i < outputCount; i++)
					{
						auto output = outputChildren[i];
						std::string outputName = output.Query("name").ToString();
						// Process output formats if needed
					}
				}

				// Parse source
				X::Value source = configRoot.Query("source");
				if (source.IsValid())
				{
					// Process source properties if needed
				}

				// Parse parameters
				X::Value parameters = configRoot.Query("parameters");
				if (parameters.IsValid())
				{
					auto parameterChildren = parameters["children"]();
					int parameterCount = (int)parameterChildren.Size();
					for (int i = 0; i < parameterCount; i++)
					{
						auto param = parameterChildren[i];
						std::string paramName = param.Query("key").ToString();
						std::string paramValue = param.Query("value").ToString();
						// Process parameters if needed
					}
				}

				// Parse imports
				X::Value imports = configRoot.Query("imports");
				if (imports.IsValid())
				{
					auto importChildren = imports["children"]();
					int importCount = (int)importChildren.Size();
					for (int i = 0; i < importCount; i++)
					{
						auto import = importChildren[i];
						std::string file = import.Query("file").ToString();
						std::string alias = import.Query("alias").ToString();
						// Process imports if needed
					}
				}

				// Parse nodes
				X::Value nodes = configRoot.Query("nodes");
				if (nodes.IsValid())
				{
					auto nodeChildren = nodes["children"]();
					int nodeCount = (int)nodeChildren.Size();
					for (int i = 0; i < nodeCount; i++)
					{
						auto node = nodeChildren[i];
						std::string nodeType = node.Query("type").ToString();
						std::string nodeName = node.Query("name").ToString();

						Callable* callable = nullptr;
						if (nodeType == "function")
						{
							callable = new Function();
						}
						else if (nodeType == "action")
						{
							callable = new BaseAction();
						}
						else if (nodeType == "agent")
						{
							callable = new BaseAgent();
						}

						if (callable)
						{
							callable->SetName(nodeName);

							X::Value instanceName = node.Query("instanceName");
							if (instanceName.IsValid())
							{
								callable->SetInstanceName(instanceName.ToString());
							}
							else
							{
								callable->SetInstanceName(nodeName);
							}

							X::Value inputs = node.Query("inputs");
							if (inputs.IsValid())
							{
								callable->SetInputs(inputs);
							}

							X::Value outputs = node.Query("outputs");
							if (outputs.IsValid())
							{
								callable->SetOutputs(outputs);
							}

							X::Value nodeParameters = node.Query("parameters");
							if (nodeParameters.IsValid())
							{
								auto nodeParameterChildren = nodeParameters["children"]();
								int nodeParameterCount = (int)nodeParameterChildren.Size();
								for (int j = 0; j < nodeParameterCount; j++)
								{
									auto param = nodeParameterChildren[j];
									//callable->AddParameter(param.Query("key").ToString(), param.Query("value").ToString());
								}
							}

							X::Value group = node.Query("group");
							if (group.IsValid())
							{
								//callable->SetGroup(group.ToString());
							}
							X::Value varCallable;
							graph.AddCallable(callable, varCallable);
						}
					}
				}

				// Parse connections
				X::Value connections = configRoot.Query("connections");
				if (connections.IsValid())
				{
					auto connectionChildren = connections["children"]();
					int connectionCount = (int)connectionChildren.Size();
					for (int i = 0; i < connectionCount; i++)
					{
						auto connection = connectionChildren[i];
						std::string fromInstanceName = connection.Query("fromNodeName").ToString();
						std::string toInstanceName = connection.Query("toNodeName").ToString();
						std::string fromPinName = connection.Query("fromPinName").IsValid() ? connection.Query("fromPinName").ToString() : "";
						std::string toPinName = connection.Query("toPinName").IsValid() ? connection.Query("toPinName").ToString() : "";

						X::ARGS params(4);
						params.push_back(fromInstanceName);
						params.push_back(fromPinName);
						params.push_back(toInstanceName);
						params.push_back(toPinName);
						X::KWARGS kwParams;
						X::Value retValue;
						graph.AddConnection(nullptr, nullptr, params, kwParams, retValue);
					}
				}

				// Parse groups
				X::Value groups = configRoot.Query("groups");
				if (groups.IsValid())
				{
					auto groupChildren = groups["children"]();
					int groupCount = (int)groupChildren.Size();
					for (int i = 0; i < groupCount; i++)
					{
						auto group = groupChildren[i];
						std::string groupName = group.Query("name").ToString();
						auto groupNodes = group.Query("nodes")["children"]();
						int groupNodeCount = (int)groupNodes.Size();
						for (int j = 0; j < groupNodeCount; j++)
						{
							auto groupNode = groupNodes[j];
							std::string groupNodeName = groupNode.ToString();
							// Process group nodes if needed
						}
					}
				}
				return true;
			}
			catch (const std::exception& e)
			{
				std::cerr << "Error parsing YAML: " << e.what() << std::endl;
				return false;
			}
		}

	};
}
