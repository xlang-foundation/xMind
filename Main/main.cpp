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

#include "xMindAPI.h"
#include "log.h"
#include "port.h"
#include "utility.h"
#include "start.h"
#ifdef __linux__
#include <unistd.h>
#elif __APPLE__
#include <mach-o/dyld.h>
#elif _WIN32
#include <windows.h>
#endif


struct ParamConfig
{
    std::string appPath;
    std::string appName;
};

std::string getExecutableFilePath() 
{
    std::string exe_path;
#ifdef __linux__
    std::filesystem::path fs_exe_path = std::filesystem::canonical("/proc/self/exe");
	exe_path = fs_exe_path.string();
#elif __APPLE__
    char path[1024];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) == 0) {
        std::filesystem::path fs_exe_path = std::filesystem::canonical(path);
		exe_path = fs_exe_path.string();
    }
#elif _WIN32
    char buffer[MAX_PATH];
    DWORD length = GetModuleFileName(nullptr, buffer, MAX_PATH);
    if (length > 0) {
        exe_path = buffer;
    }    
#endif
    return exe_path;
}

bool ParseCommandLine(std::vector<std::string>& params, ParamConfig& paramCfg)
{
    std::string progName = params[0];
    std::filesystem::path fsPath(progName);
    // Check if the path is absolute, if not, combine it with RootPath
    if (!fsPath.is_absolute())
    {
		progName = getExecutableFilePath();
    }
    auto pos = progName.rfind(Path_Sep);
    if (pos != progName.npos)
    {
        paramCfg.appName = progName.substr(pos + 1);
		paramCfg.appPath = progName.substr(0, pos);
    }
    else
    {
        paramCfg.appName = progName;
    }
	std::cout << "AppPath: " << paramCfg.appPath <<",AppName:"<< paramCfg.appName << std::endl;
    return true;
}

static X::XLoad g_xLoad;
X::Config* LoadXLangEngine(ParamConfig& paramConfig, std::string searchPath,
    bool dbg = false, bool python_dbg = false)
{
    X::Config* pCfg = new X::Config();
    X::Config& cfg = *pCfg;
    cfg.appPath = NewFromString(paramConfig.appPath);
    cfg.dbg = dbg;
    cfg.enablePythonDebug = python_dbg;
    cfg.enablePython = true;
    cfg.enterEventLoop = false;//will call g_xLoad.EventLoop() later
    cfg.runEventLoopInThread = false;
    cfg.dllSearchPath = NewFromString(searchPath);
    int retCode = g_xLoad.Load(pCfg);
    if (retCode == 0)
    {
        g_xLoad.Run();
        LOG << "XLang Engine Loaded" << LINE_END;
    }
    return pCfg;
}
void UnloadXLangEngine()
{
    g_xLoad.Unload();
}

int WorkerMain(ParamConfig& paramConfig)
{
    X::Config* pCfg = nullptr;
    std::string xlangSearchPath = paramConfig.appPath;
    pCfg = LoadXLangEngine(paramConfig, xlangSearchPath, false, false);

    int lrpc_port = 1023;
    //TODO: call worker logic here

    UnloadXLangEngine();
    if (pCfg)
    {
        delete pCfg;
    }
    return 0;
}
int main(int argc, char* argv[]) 
{
    ParamConfig paramConfig;
    std::vector<std::string> params(argv, argv + argc);
    ParseCommandLine(params, paramConfig);

    xMind::MindAPISet::I().SetRootInfo(paramConfig.appPath);

    std::string logFolder = paramConfig.appPath + Path_Sep_S + "Logs";
    if (isDir(logFolder.c_str()) == false)
    {
        _mkdir(logFolder.c_str());
    }
    std::string logFilePath = logFolder + Path_Sep + "log.txt";
    Cantor::log.SetLogFileName(logFilePath);
    Cantor::log.Init();

    std::string xlangSearchPath = paramConfig.appPath;
    auto* pCfg = LoadXLangEngine(paramConfig, xlangSearchPath, true);
    xMind::MindAPISet::I().Start();

    X::RegisterPackage<xMind::MindAPISet>(paramConfig.appName.c_str(), "xMind", &xMind::MindAPISet::I());

    int lrpc_port = 99023;
    LOG << "Local RPC(shared memory) Listen on virtual port " << lrpc_port << LINE_END;
    X::ARGS p_dummy(0);
    X::KWARGS kp_dummy;
    xMind::MindAPISet::I().Fire(0, p_dummy, kp_dummy);
    X::g_pXHost->Lrpc_Listen(lrpc_port, false);
    LOG << "xMind Started" << LINE_END;

	xMind::Starter::I().Start(paramConfig.appPath);
    g_xLoad.EventLoop();
	xMind::Starter::I().Stop();

    xMind::MindAPISet::I().Fire(1, p_dummy, kp_dummy);
    xMind::MindAPISet::I().Shutdown();

    UnloadXLangEngine();
    delete pCfg;
    LOG << "xMind Shutdown" << LINE_END;
    return 0;
}
