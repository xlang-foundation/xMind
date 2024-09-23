#pragma once
#include "singleton.h"
#include <string>
#include <vector>
#include <unordered_map>
#include "xlang.h"
#include <mutex>
#include <chrono>

namespace xMind
{
    #define NO_SESSION_ID 0
    struct Message {
        std::string role;
        std::string content;
        std::string refusal;// Optional, used in the response context
    };

    struct Choice {
        int index;
        Message message;
        std::string logprobs;
        std::string finish_reason;
    };

    struct Usage {
        int prompt_tokens;
        int completion_tokens;
        int total_tokens;
    };

    struct ChatCompletionResponse {
        std::string id;
        std::string object;
        int64_t created;
        std::string model;
        std::vector<Choice> choices;
        Usage usage;
    };
    struct ChatRequest {
        std::string model;
        std::vector<Message> messages;
        X::List messageList;
        double temperature;
        std::string sessionId;  // Optional sessionId
    };
	//for completion, no session
    struct CompletionRequest {
        std::string model;
        std::string prompt;
        double temperature;
        int max_tokens;
        bool stream;  // Optional, default is false
    };
    struct CompletionChoice {
        std::string text;
        int index;
        std::string logprobs;
        std::string finish_reason;
    };

    struct CompletionUsage {
        int prompt_tokens;
        int completion_tokens;
        int total_tokens;
    };

    struct CompletionResponse {
        std::string id;
        std::string object;
        int64_t created;
        std::string model;
        std::vector<CompletionChoice> choices;
        CompletionUsage usage;
    };
    struct SessionInfo {
        int id;
        int64_t created;
        std::string sessionId;
    };
    class SessionManager : public Singleton<SessionManager>
    {
		X::Value createChatResponse(const ChatCompletionResponse& response);
        ChatRequest parseChatRequest(X::Value& reqData);
        CompletionRequest parseCompletionRequest(X::Value& reqData);
        X::Value createCompletionResponse(const CompletionResponse& response);
        int createSession(const std::string& sessionId);
        int getSessionId(const std::string& sessionId)
        {
            std::lock_guard<std::mutex> lock(m_session_mtx);
            if (m_sessionIdToSidMap.find(sessionId) != m_sessionIdToSidMap.end())
            {
                return m_sessionIdToSidMap[sessionId];
            }
            return NO_SESSION_ID;
        }
    public:
		X::Value HandleChatRequest(X::Value& reqData);
		X::Value HandleCompletionRequest(X::Value& reqData);
    private:
        std::unordered_map<std::string, int> m_sessionIdToSidMap;
        std::unordered_map<int, SessionInfo> m_sessions;
        int nextSid = 1;
        std::mutex m_session_mtx;
    };
}