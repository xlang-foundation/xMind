# xMind -- AgentFlow Framework

**xMind** is a modular framework built with XLang, designed to implement Large Language Model (LLM) Memory, Planning, and Agent-flow capabilities. This project allows developers to seamlessly integrate advanced AI features like context retention, decision-making, and dynamic dataflows into their applications.

## Features

- **LLM Memory**: Retain and utilize context across sessions to enhance interaction and decision-making.
- **Planning**: Implement sophisticated planning mechanisms that allow LLMs to make informed decisions based on historical data and projected outcomes.
- **Agent-flow Management**: Orchestrate complex Agent-flows to streamline processing and enhance the performance of AI-driven applications.
- **Modular Design**: Easily extend and customize the framework to fit your specific needs.

## Getting Started

### Prerequisites

- [XLang](https://github.com/xlang-foundation/xlang) Please clone the Jit branch of XLang into the xMind/ThirdParty folder and ensure the folder is named xlang.

### Build the Framework

Clone the repository:

```bash
git clone https://github.com/xlang-foundation/xMind.git
cd xMind
mkdir build
cd build
cmake ..
make

```
# Term and Concept
1. Blueprint: use YAML to define like variables,prompts,actions,etc.
2. Variable: it is global variable,but in same scope file, no prefix needed. but for other file, need to use prefix. for example: `file1.var1`

### Running the Framework
  [Start Guide](./Start.md)
