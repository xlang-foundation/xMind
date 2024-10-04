/*
Copyright (C) 2024 The XLang Foundation

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#pragma once


#include <iostream>
#include <string>
#include <map>
#include <set>
#include <mutex>
#include <thread>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#endif

class PythonWorkerManager {
public:
    PythonWorkerManager() : monitoringThreadRunning(false) {}

    ~PythonWorkerManager() {
        Shutdown();
    }

    // Adds a task and starts a Python worker with the given moduleId and pyfilePath
    void AddTask(int moduleId, const std::string& pyfilePath) {
        std::lock_guard<std::mutex> lock(processMutex_);

        ProcessKey key = std::make_pair(moduleId, pyfilePath);

        // Add task to the tasks set
        tasks_.insert(key);

        // If process is already running, do nothing
        if (processes_.count(key)) {
            return;
        }

        ProcessHandle process = StartProcess(moduleId, pyfilePath);
        if (!IsValidProcessHandle(process)) {
            std::cerr << "Failed to start process for moduleId: " << moduleId << std::endl;
            return;
        }

        processes_[key] = process;

        // Start the monitoring thread if not already running
        if (!monitoringThreadRunning) {
            monitoringThreadRunning = true;
            monitorThread_ = std::thread(&PythonWorkerManager::MonitorProcesses, this);
        }
    }

    // Removes a task and kills the corresponding process
    void RemoveTask(int moduleId, const std::string& pyfilePath) {
        std::lock_guard<std::mutex> lock(processMutex_);

        ProcessKey key = std::make_pair(moduleId, pyfilePath);

        // Remove task from the tasks set
        tasks_.erase(key);

        // If process is running, kill it
        auto it = processes_.find(key);
        if (it != processes_.end()) {
            ProcessHandle process = it->second;
            KillProcess(process);
            CloseProcessHandle(process);
            processes_.erase(it);
        }
    }

    // Shuts down all running Python worker instances
    void Shutdown() {
        {
            std::lock_guard<std::mutex> lock(processMutex_);
            monitoringThreadRunning = false;
        }

        // Wait for the monitoring thread to finish
        if (monitorThread_.joinable()) {
            monitorThread_.join();
        }

        // Terminate all running processes
        std::lock_guard<std::mutex> lock(processMutex_);
        for (auto& entry : processes_) {
            ProcessHandle process = entry.second;
            KillProcess(process);
            CloseProcessHandle(process);
        }
        processes_.clear();
        tasks_.clear();
    }

private:
    using ProcessKey = std::pair<int, std::string>;

#ifdef _WIN32
    using ProcessHandle = HANDLE;
    static constexpr ProcessHandle INVALID_PROCESS_HANDLE = NULL;
#else
    using ProcessHandle = pid_t;
    static constexpr ProcessHandle INVALID_PROCESS_HANDLE = -1;
#endif

    // Monitors all running processes and restarts them if they crash
    void MonitorProcesses() {
        while (true) {
            {
                std::lock_guard<std::mutex> lock(processMutex_);
                if (!monitoringThreadRunning) {
                    break;
                }

                for (auto it = processes_.begin(); it != processes_.end();) {
                    ProcessHandle process = it->second;
                    ProcessKey key = it->first;

                    if (IsProcessRunning(process)) {
                        // Process is still running
                        ++it;
                    }
                    else {
                        // Process has terminated
                        CloseProcessHandle(process);
                        it = processes_.erase(it); // Remove from processes_

                        // Check if the task is still supposed to be running
                        if (tasks_.count(key)) {
                            // Restart the process
                            int moduleId = key.first;
                            std::string pyfilePath = key.second;
                            ProcessHandle newProcess = StartProcess(moduleId, pyfilePath);
                            if (IsValidProcessHandle(newProcess)) {
                                processes_[key] = newProcess;
                            }
                            else {
                                std::cerr << "Failed to restart process for moduleId: " << moduleId << std::endl;
                            }
                        }
                        // Else, task has been removed; do not restart
                    }
                }
            }
            // Sleep before next check
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    // Helper function to start a process
    ProcessHandle StartProcess(int moduleId, const std::string& pyfilePath) {
#ifdef _WIN32
        STARTUPINFOA si;
        PROCESS_INFORMATION pi;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        ZeroMemory(&pi, sizeof(pi));

        // Build the command line: python pyfilePath moduleId=moduleId
        std::string cmd = "python \"" + pyfilePath + "\" moduleId=" + std::to_string(moduleId);

        // Create the child process.
        if (!CreateProcessA(NULL,   // No module name (use command line)
            cmd.data(),             // Command line
            NULL,                   // Process handle not inheritable
            NULL,                   // Thread handle not inheritable
            FALSE,                  // Set handle inheritance to FALSE
            0,                      // No creation flags
            NULL,                   // Use parent's environment block
            NULL,                   // Use parent's starting directory 
            &si,                    // Pointer to STARTUPINFO structure
            &pi)                    // Pointer to PROCESS_INFORMATION structure
            )
        {
            // Failed to create process
            return INVALID_PROCESS_HANDLE;
        }

        // Close thread handle; we don't need it
        CloseHandle(pi.hThread);

        return pi.hProcess;
#else
        pid_t pid = fork();
        if (pid == -1) {
            // Fork failed
            return INVALID_PROCESS_HANDLE;
        }
        else if (pid == 0) {
            // Child process: execute the Python script
            execlp("python", "python", pyfilePath.c_str(), ("moduleId=" + std::to_string(moduleId)).c_str(), (char*)NULL);
            // If execlp returns, execution failed
            std::cerr << "Failed to execute Python script: " << pyfilePath << std::endl;
            exit(EXIT_FAILURE);
        }
        else {
            // Parent process: return the child PID
            return pid;
        }
#endif
    }

    // Helper function to check if a process is running
    bool IsProcessRunning(ProcessHandle process) {
#ifdef _WIN32
        DWORD exitCode;
        if (GetExitCodeProcess(process, &exitCode)) {
            return (exitCode == STILL_ACTIVE);
        }
        return false;
#else
        int status;
        pid_t result = waitpid(process, &status, WNOHANG);
        if (result == 0) {
            // Process is still running
            return true;
        }
        return false;
#endif
    }

    // Helper function to kill a process
    void KillProcess(ProcessHandle process) {
#ifdef _WIN32
        TerminateProcess(process, 0);
        WaitForSingleObject(process, INFINITE);
#else
        kill(process, SIGTERM); // Send termination signal
        waitpid(process, nullptr, 0); // Wait for process to terminate
#endif
    }

    // Helper function to close process handle
    void CloseProcessHandle(ProcessHandle process) {
#ifdef _WIN32
        CloseHandle(process);
#endif
    }

    // Helper function to check if process handle is valid
    bool IsValidProcessHandle(ProcessHandle process) {
#ifdef _WIN32
        return process != INVALID_HANDLE_VALUE && process != NULL;
#else
        return process != -1;
#endif
    }

    std::set<ProcessKey> tasks_;
    std::map<ProcessKey, ProcessHandle> processes_;
    std::mutex processMutex_;
    std::thread monitorThread_;
    bool monitoringThreadRunning;
};
