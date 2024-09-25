
**AgentFlow** is a versatile and dynamic data graph framework designed to efficiently manage multiple chat sessions within a single instance of the graph. It consists of interconnected nodes and edges, where each node can represent a function, action, or agent. These nodes are linked via input and output interfaces known as pins.

### Key Features:

- **Data Structure**: The data flowing through each pin is structured into four key components: session ID, status, output index, and the actual data encapsulated as `X::Value`. This ensures consistent and organized data management across the graph.

- **Node Operations**: Nodes in AgentFlow can run in their own threads or processes, providing flexibility in execution. Function nodes, however, are executed inline within the context of their associated action or agent node, allowing seamless integration within the designated workflow.

- **Agent Integration**: Agents within the graph are directly tied to LLM (Language Model) requests, effectively serving as the brain of the AgentFlow. They drive the logic, decision-making, and context management across the entire graph.

- **Flexible Pin Connections**: Each input pin can accept multiple connections from output pins, facilitating complex data routing and interaction between nodes.

- **Multi-Session Support**: AgentFlow is specifically designed to support multiple chat sessions simultaneously within a single graph instance, optimizing resource usage and scalability.

This structure allows AgentFlow to manage and process data efficiently, making it a powerful tool for handling complex workflows in a multi-session environment.