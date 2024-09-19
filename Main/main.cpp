#include "xMindAPI.h"
#include "log.h"
#include "port.h"
#include "utility.h"
#include "DescParser.h"

struct ParamConfig
{
    std::string appPath;
    std::string appName;
};

bool ParseCommandLine(std::vector<std::string>& params, ParamConfig& paramCfg)
{
    std::string progName = params[0];
    std::string strAppPath;
    std::string strAppName;
    auto pos = progName.rfind(Path_Sep);
    if (pos != progName.npos)
    {
        strAppPath = progName.substr(0, pos);
        strAppName = progName.substr(pos + 1);
    }
#if (WIN32)
    else
    {
        char buffer[MAX_PATH];
        DWORD length = GetModuleFileName(nullptr, buffer, MAX_PATH);
        progName = buffer;
        auto pos = progName.rfind(Path_Sep);
        if (pos != progName.npos)
        {
            strAppPath = progName.substr(0, pos);
            strAppName = progName.substr(pos + 1);
        }
        else
        {
            strAppPath = "";
			strAppName = "";
        }
    }
#endif
    paramCfg.appPath = strAppPath;
    paramCfg.appName = strAppName;

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

#include "AgentGraphParser.h"
void Test()
{
    std::string fileName = "D:\\Github\\xMind\\Schema\\compositeAgent.yml";
    //test
    xMind::Parser parser;
    X::Package yaml(xMind::MindAPISet::I().RT(), "yaml", "xlang_yaml");
    X::Value root = yaml["load"](fileName);
	parser.ParseAgentGraphDesc(root);
	std::cout << "Test Done" << std::endl;
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

	Test();

    g_xLoad.EventLoop();

    xMind::MindAPISet::I().Fire(1, p_dummy, kp_dummy);
    xMind::MindAPISet::I().Shutdown();

    UnloadXLangEngine();
    delete pCfg;
    LOG << "xMind Shutdown" << LINE_END;
    return 0;
}
