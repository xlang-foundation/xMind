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