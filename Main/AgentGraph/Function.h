#pragma once

#include "Callable.h"

namespace xMind
{
	class Function :public Callable
	{
		BEGIN_PACKAGE(Function)
			ADD_BASE(Callable);
		END_PACKAGE
		virtual bool ReceiveData(int sessionId, int inputIndex, X::Value& data) override
		{
			//when receive data, call the real function
			//and push the result to the next node
			//only support one input data
			if (m_implObject.IsObject())
			{
				X::ARGS params(1);
				params.push_back(data);
				X::KWARGS kwParams;
				auto* pXPack = Function::APISET().GetProxy(this);
				X::Value varOwner = X::Value(pXPack);
				kwParams.Add("owner", varOwner);
				X::Value retData = m_implObject.ObjCall(params, kwParams);
				PushToOutput(sessionId,0, retData);
			}
			else
			{
				PushEvent(inputIndex,data);
			}
			return true;
		}
		inline bool Create()
		{
			return true;
		}
		inline virtual bool Run() override
		{
			return true;
		}
		inline virtual void Stop() override
		{
		}
		virtual X::Value Clone() override
		{
			Function* pFuncObj = new Function();
			pFuncObj->Copy(this);
			auto* pXPack = Function::APISET().GetProxy(pFuncObj);
			X::Value retValue = X::Value(pXPack);
			return retValue;
		}
		public:
		Function()
		{
			m_inputs.push_back(Pin{ "input" });
			m_outputs.push_back(Pin{ "output" });
		}

	};
}