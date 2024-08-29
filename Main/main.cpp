#include "xMindAPI.h"
#include "log.h"
#include "port.h"
#include "utility.h"

struct ParamConfig
{
    std::string appPath;
    std::string appName;
};

bool ParseCommandLine(std::vector<std::string>& params, ParamConfig& paramCfg)
{
    char buffer[MAX_PATH];
    DWORD length = GetModuleFileName(nullptr, buffer, MAX_PATH);
    std::string  strPath = buffer;
    auto pos = strPath.rfind(Path_Sep);
    if (pos > 0)
    {
        paramCfg.appPath = strPath.substr(0, pos);
        paramCfg.appName = strPath.substr(pos + 1);
    }
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
    auto* pCfg = LoadXLangEngine(paramConfig, xlangSearchPath, false);

    X::RegisterPackage<xMind::MindAPISet>(paramConfig.appName.c_str(), "xMind", &xMind::MindAPISet::I());

    int lrpc_port = 99023;
    LOG << "Local RPC(shared memory) Listen on virtual port " << lrpc_port << LINE_END;
    X::ARGS p_dummy(0);
    X::KWARGS kp_dummy;
    xMind::MindAPISet::I().Fire(0, p_dummy, kp_dummy);
    X::g_pXHost->Lrpc_Listen(lrpc_port, false);
    LOG << "xMind Started" << LINE_END;

    g_xLoad.EventLoop();

    xMind::MindAPISet::I().Fire(1, p_dummy, kp_dummy);
    UnloadXLangEngine();

    delete pCfg;
    LOG << "xMind Shutdown" << LINE_END;
    return 0;
}
