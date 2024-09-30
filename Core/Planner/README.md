
---

# Planner Module

## Overview

The **Planner** module is designed to manage task workflows by generating a graph of **Functions**, **Actions**, and **Agents**. It takes task descriptions as input and, with the help of a Large Language Model (LLM), creates a structured plan to execute tasks, breaking them into components and representing them as a graph. The Planner automatically verifies the graph and reruns the validation process if any issues are detected, ensuring a valid graph before execution.

## Key Features

- **Graph-Based Planning**: Breaks down task descriptions into graph nodes, where each **Function**, **Action**, and **Agent** represents a node with specified input/output pins.
- **LLM-Driven Graph Creation**: Uses an LLM to output the graph structure in YAML format, describing task nodes and their connections.
- **Graph Validation**: Automatically verifies that the generated graph is valid. If invalid, it requests the LLM to correct the structure and repeats the verification process.
- **Task Execution**: Once the graph is validated, the Planner executes the tasks by traversing the graph, collecting results from each node, and feeding those results through its outputs.
- **Modular Design**: Supports system-defined, user-supplied, and third-party **Functions**, **Actions**, and **Agents**, which can all participate as graph nodes.
- **Feedback and Iteration**: Supports feedback loops to improve task execution over iterations.

## Flow Description

### 1. Task Input

The Planner starts with a task description provided by the user. This description is fed into the system and prompts the Planner to ask the LLM for assistance in generating a detailed graph-based plan.

### 2. LLM Output (Graph Creation)

Using the task description, the Planner requests the LLM to create a task plan in YAML format. This output includes a series of **Functions**, **Actions**, and **Agents**, described as nodes in a graph. The YAML specifies the nodes, their inputs and outputs, and the connections between them.

### 3. Graph Validation

Once the Planner receives the graph from the LLM, it performs a validation step. The validation process ensures the following:

- All necessary nodes are correctly defined.
- Inputs and outputs of the nodes are correctly connected.
- There are no broken links, invalid nodes, or logical inconsistencies in the graph.

If the graph is **invalid**, the Planner will automatically request the LLM to correct the structure, providing necessary details about the errors detected. This process repeats until the graph is valid.

### 4. Task Execution

Once the graph is validated, the Planner begins executing the task:

- Each node in the graph, representing a **Function**, **Action**, or **Agent**, is executed in sequence based on the connections defined in the graph.
- Data flows through the graph from one node to the next, following the connections between input and output pins.
- The execution results are collected from each node and passed through to the subsequent nodes, ensuring that the entire task is completed step-by-step.

### 5. Results Collection and Output

As the task progresses, results are fetched from the executed nodes. The Planner collects these results and outputs them through its designated output pins. The final results reflect the successful completion of the task, and any intermediate outputs can also be provided as needed.

### 6. Feedback Loops and Iterations (Optional)

If the task or workflow requires feedback and iteration, the Planner can loop through certain nodes, performing additional cycles of execution based on the graph’s logic. This ensures that tasks are continuously refined until a desired output is achieved.
