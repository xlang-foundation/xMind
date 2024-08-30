#pragma once

#include "Callable.h"

namespace xMind
{
	class Function :public Callable
	{
		BEGIN_PACKAGE(Function)
			ADD_BASE(Callable);
		END_PACKAGE
	};
}