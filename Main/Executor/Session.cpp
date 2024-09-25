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

#include "Session.h"
#include "start.h"
#include "xMindAPI.h"
#include "xlang.h"

namespace xMind
{
    int SessionManager::createSession(const std::string& sessionId) {
        std::lock_guard<std::mutex> lock(m_session_mtx);
        int64_t createdTime = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        int sid = nextSid++;
        m_sessions[sid] = { sid, createdTime, sessionId };
        m_sessionIdToSidMap[sessionId] = sid;
        return sid;
    }
    X::Value SessionManager::HandleChatRequest(X::Value& reqData)
    {
		ChatRequest request = parseChatRequest(reqData);
		int sid = getSessionId(request.sessionId);
		if (sid == NO_SESSION_ID)
		{
            X::Package utils(xMind::MindAPISet::I().RT(), "utils", "xlang_os");
            X::Value uuid = utils["generate_uid"]();
            std::string strSessionId = uuid.ToString();
			sid = createSession(strSessionId);
            request.sessionId = strSessionId;
		}
		X::Value graph = Starter::I().GetOrCreateRunningGraph(request.model);
        X::XPackageValue<AgentGraph> packGraph(graph);
		AgentGraph* pGraph = packGraph.GetRealObj();
		if (pGraph == nullptr)
		{
			return X::Value();
		}
		X::Value retMsg = pGraph->RunInputs(sid, request.messageList);
        ChatCompletionResponse response;
		response.id = request.sessionId;
		response.object = "text";
		response.created = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
		response.model = request.model;
		response.choices.push_back({ 0, { "assistant", retMsg.ToString(), "" }, "", "complete" });

        X::Value retData = createChatResponse(response);
        return retData;
    }
    X::Value SessionManager::HandleCompletionRequest(X::Value& reqData)
    {
		CompletionRequest request = parseCompletionRequest(reqData);
		X::Value graph = Starter::I().GetOrCreateRunningGraph(request.model);
		X::XPackageValue<AgentGraph> packGraph(graph);
		AgentGraph* pGraph = packGraph.GetRealObj();
		if (pGraph == nullptr)
		{
            return X::Value();
		}
		X::Value valInput = request.prompt;
		pGraph->RunInputs(NO_SESSION_ID, valInput);
        return X::Value();
    }
    X::Value SessionManager::createChatResponse(const ChatCompletionResponse& response) {
        // Create the main dictionary
        X::Dict mainDict;
        mainDict->Set("id", response.id);
        mainDict->Set("object", response.object);
        mainDict->Set("created", response.created);
        mainDict->Set("model", response.model);

        // Create the choices list
        X::List choicesList;

        // Fill the choices list
        for (const auto& choice : response.choices) {
            X::Dict choiceDict;
            choiceDict->Set("index",choice.index);

            // Create and add the message dictionary
            X::Dict messageDict;
            messageDict->Set("role", choice.message.role);
            messageDict->Set("content", choice.message.content);
            messageDict->Set("refusal", choice.message.refusal);

            choiceDict->Set("message", messageDict);
            choiceDict->Set("logprobs", choice.logprobs);
            choiceDict->Set("finish_reason", choice.finish_reason);

            choicesList->append(choiceDict);
        }

        // Add the choices list to the main dictionary
        mainDict->Set("choices", choicesList);

        // Create the usage dictionary
        X::Dict usageDict;
        usageDict->Set("prompt_tokens", response.usage.prompt_tokens);
        usageDict->Set("completion_tokens", response.usage.completion_tokens);
        usageDict->Set("total_tokens", response.usage.total_tokens);

        // Add the usage dictionary to the main dictionary
        mainDict->Set("usage", usageDict);

        // Return the main dictionary as X::Value
        return X::Value(mainDict);
    }
    ChatRequest SessionManager::parseChatRequest(X::Value& reqData) {
        ChatRequest request;

		X::Dict reqDict = reqData;
        // Extract "model"
        request.model = reqDict["model"].ToString();

        // Extract "messages"
        X::List messageList = reqDict["messages"];
		request.messageList = messageList;//still keep for agent use
        for (const auto& item : *messageList) {
            X::Dict messageDict = item;
            Message msg;
            msg.role = messageDict["role"].ToString();
            msg.content = messageDict["content"].ToString();
            request.messages.push_back(msg);
        }

        // Extract "temperature"
		X::Value varTemperature = reqDict["temperature"];
        if (varTemperature.IsValid())
        {
            request.temperature = varTemperature.ToDouble();
        }

		X::Value varSessionId = reqData["sessionId"];
        if (varSessionId.IsValid()) {
            request.sessionId = varSessionId.ToString();
        }

        return request;
    }
    CompletionRequest SessionManager::parseCompletionRequest(X::Value& reqData) {
        CompletionRequest request;

        // Extract "model"
        request.model = reqData["model"].ToString();

        // Extract "prompt"
        request.prompt = reqData["prompt"].ToString();
        // Extract "temperature"
        X::Value varTemperature = reqData["temperature"];
        if (varTemperature.IsValid())
        {
            request.temperature = varTemperature.ToDouble();
        }

        // Extract "max_tokens"
        request.max_tokens = reqData["max_tokens"].ToInt();

        // Optional: Extract "stream" if present
        if (reqData.contains("stream")) {
            request.stream = reqData["stream"].ToBool();
        }
        else {
            request.stream = false;  // Default value
        }

        return request;
    }
    X::Value SessionManager::createCompletionResponse(const CompletionResponse& response) {
        // Create the main dictionary
        X::Dict mainDict;
        mainDict->Set("id", response.id);
        mainDict->Set("object", response.object);
        mainDict->Set("created", response.created);
        mainDict->Set("model", response.model);

        // Create the choices list
        X::List choicesList;

        // Fill the choices list
        for (const auto& choice : response.choices) {
            X::Dict choiceDict;
            choiceDict->Set("text", choice.text);
            choiceDict->Set("index", choice.index);
            choiceDict->Set("logprobs", choice.logprobs);
            choiceDict->Set("finish_reason", choice.finish_reason);

            choicesList->append(choiceDict);
        }

        // Add the choices list to the main dictionary
        mainDict->Set("choices", choicesList);

        // Create the usage dictionary
        X::Dict usageDict;
        usageDict->Set("prompt_tokens", response.usage.prompt_tokens);
        usageDict->Set("completion_tokens", response.usage.completion_tokens);
        usageDict->Set("total_tokens", response.usage.total_tokens);

        // Add the usage dictionary to the main dictionary
        mainDict->Set("usage", usageDict);

        // Return the main dictionary as X::Value
        return X::Value(mainDict);
    }

} // namespace xMind