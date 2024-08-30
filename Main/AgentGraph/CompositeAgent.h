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