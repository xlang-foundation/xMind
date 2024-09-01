#pragma once
#include "xlang.h"
#include "xhost.h"

namespace xMind
{
	class PropScope :
		public X::XCustomScope
	{
		X::KWARGS* kwParams;
		std::vector<std::string> Keys;
	public:
		PropScope(X::KWARGS* kw):kwParams(kw)
		{
		}
		inline virtual int AddOrGet(const char* name, bool bGetOnly) override
		{
			std::string strName(name);
			Keys.push_back(strName);
			return  (int)Keys.size()-1;
		}
		inline virtual bool Get(int idx, X::Value& v, void* lValue = nullptr) override
		{
			return false;
		}
		inline virtual bool Set(int idx, X::Value& v) override
		{
			kwParams->Add(Keys[idx].c_str(), v,true);
			return true;
		}
	};
}