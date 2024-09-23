#include "LlmPool.h"
#include "xMindAPI.h"

namespace xMind
{
	X::Value LlmRequest::MakeRequest(const std::string& model, const X::Value& prompts, double temperature)
	{
		if (m_request.IsInvalid())
		{
			X::Package http(xMind::MindAPISet::I().RT(), "http", "xlang_http");
			m_request = http["Client"](m_url);
			m_request["setHeaders"](m_headers);
		}
		X::Dict dictData;
		X::Value varModel((std::string&)model);
		dictData->Set("model", varModel);
		if (prompts.IsList())
		{
			dictData->Set("messages", prompts);
		}
		else//treat as string
		{
			std::string strPrompt = ((X::Value&)prompts).ToString();
			X::List listMsg;
			X::Dict prompt;
			prompt->Set("role", "user");
			prompt->Set("content", strPrompt);
			listMsg += prompt;
			dictData->Set("messages", listMsg);
		}
		dictData->Set("temperature", temperature);
		std::string data = dictData.ToString(true);
		m_request["post"]("", m_content_type, data);
		int status = (int)m_request["status"]();
		X::Value body;
		if (status == 200)
		{
			body = m_request["body"]();
			X::Package json(xMind::MindAPISet::I().RT(), "json");
			X::Value jsonBody =json["loads"](body);
			X::Dict dictBody(jsonBody);
			X::Value choices = dictBody["choices"];
			if (choices.IsList())
			{
				X::Value first = choices[(long long)0];
				if (first.IsValid())
				{
					X::Dict dictFirst(first);
					X::Value msg = dictFirst["message"];
					if (msg.IsValid())
					{
						X::Dict dictMsg(msg);
						body = dictMsg["content"];
					}
				}
			}
		}
		else
		{
			X::Error error(status, "");
			LOG << "Request '" << m_url << "' failed with status code : " << status << LINE_END;
			body = error;
		}
		return body;
	}
	void LlmPool::Test()
	{
		X::Dict headers;
		headers->Set("Authorization",
			"Bearer sk-MLQjkKBgq23taG8Ze4N7rPtAZ20tDPKfduzNLN3jOwT3BlbkFJwKoGCDADhKcm6XpumMS-jMd6E2vDw0zgzu0q5vZn4A");
		Add("openai-001",
			"https://api.openai.com/v1/chat/completions", 
			"openai", "application/json", headers
			);
		std::string model = "gpt-4";
		X::List listPrompts;
		X::Dict prompt;
		prompt->Set("role", "system");
		prompt->Set("content", "You are a helpful assistant for xlang coding, xlang has same syntax like python.");
		listPrompts += prompt;
		prompt->Set("role", "user");
		prompt->Set("content", "give me an example code for list and dict");
		listPrompts += prompt;
		std::vector<std::string> allSelectons;
		X::Value ret = RunTask(model, listPrompts, 0.7, allSelectons);
		LOG << ret.ToString() << LINE_END;
	}
}