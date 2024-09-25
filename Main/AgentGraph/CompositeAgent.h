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

#include "BaseAgent.h"

namespace xMind
{
	/**
	 * @class CompositeAgent
	 * @brief Represents a composite agent in the xMind framework.
	 *
	 * The CompositeAgent class extends the BaseAgent class to provide functionality for
	 managing a group of agents as a single entity. It includes methods for adding and
	 removing agents from the group, connecting/disconnecting agents, and enqueuing/dequeuing
	 input data.
	 */
	class CompositeAgent : public BaseAgent
	{
		std::vector<X::Value> m_agents;
		public:
			CompositeAgent()
			{
			}
			~CompositeAgent()
			{
			}
	};
}