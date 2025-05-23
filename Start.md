## Introduction

This document provides instructions on how to set up and run the **xMind** project. After building the project, you'll find the following key folders and files:

- **Config**: Contains configuration files, including `config.yml` and `config.xml`.
- **Examples**: Includes example agents and reference files.
- **Service**: The webserver directory containing the service xLang code (`main.x`) and other web pages.
- **xMind**: The executable to start the xMind application.

## Prerequisites

Ensure that all necessary dependencies are installed and that you have successfully built the project. The key executable to start the application is `xMind`.

## Configuration

### config.yml

The `config.yml` file contains critical configurations for the xMind application. Below is a summary of the key sections:

- **WebServer**:
  - `Port`: The port on which the web server will run (default: `9901`).
  - `ServiceEntry`: Path to the main xLang code for the web server (`../Service/main.x`).

- **RootAgents**:
  - List of agents to be registered and displayed in the chat page's Agent/Model choose list.
  - Example:
    ```yaml
    RootAgents:
       - name: simple
         file: ../Examples/Simple/simple.yml
       - name: QAJudge
         file: ../Examples/Simple/two_simples.yml
    ```

## Running xMind

1. **Start xMind**:
   Execute the `xMind` binary from the `out/bin` directory:
   ```bash
   ./xMind
   ```

2. **Update OpenAI Key**:
   In a separate terminal window, run the following command to update your OpenAI API key:
   ```bash
   ./xmcli --updatekey openai_key your_key
   ```
   After running this command, your `config.yml` will be updated with the provided key.

3. **Restart xMind**:
   Stop the running `xMind` process (if necessary) and restart it:
   ```bash
   ./xMind
   ```

4. **Access the Web Interface**:
   - Open a web browser and navigate to [http://localhost:9901](http://localhost:9901) to access the chat interface.
   - If accessing from another machine, replace `localhost` with the IP address of the machine running xMind.

5. **Using RootAgents**:
   - The `RootAgents` defined in `config.yml` will appear in the chat page's Agent/Model choose list, allowing you to interact with the registered agents.

## Folder Structure

- **Config**: Contains configuration files such as `config.xml` and `config.yml`.
- **Examples**: Holds example agents that serve as references for creating and configuring your agents.
- **Service**: Contains the webserver code and pages, including `main.x`, which serves as the service entry point.

## Logs

Log files are stored in the `Logs` directory for troubleshooting and monitoring.

