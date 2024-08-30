#pragma once
#include "singleton.h"
#include "xpackage.h"
#include "log.h"
#include "Locker.h"

namespace xMind
{
    class MindAPISet :
        public Singleton<MindAPISet>
    {
        std::string m_RootPath;
        X::Runtime* m_defaultRuntime = nullptr;

        BEGIN_PACKAGE(MindAPISet)
            APISET().AddEvent("OnReady");
            APISET().AddEvent("OnShutdown");
            APISET().AddVarFunc("log", &MindAPISet::Log);
            APISET().AddVarFunc("logV", &MindAPISet::LogV);
            APISET().AddVarFunc("log_nolineend", &MindAPISet::Log_nolineend);
            APISET().AddFunc<0>("GetRootPath", &MindAPISet::GetRootPath);
        END_PACKAGE
    public:
        void SetRootInfo(std::string path)
        {
            m_RootPath = path;
        }
        std::string GetRootPath()
        {
            return m_RootPath;
        }
        X::Runtime& RT() { return *m_defaultRuntime; }
        bool Start();
        void Shutdown();
        inline bool Log(X::XRuntime* rt, X::XObj* pContext,
            X::ARGS& params, X::KWARGS& kwParams, X::Value& retValue)
        {
            int level = 0;
            auto it = kwParams.find("level");
            if (it)
            {
                level = it->val;
            }
            Cantor::log.LineBegin();
            Cantor::log.SetLevel(level);
            for (auto& p : params)
            {
                Cantor::log << p.ToString();
            }
            Cantor::log << LINE_END;
            return true;
        }
        inline bool LogV(X::XRuntime* rt, X::XObj* pContext,
            X::ARGS& params, X::KWARGS& kwParams, X::Value& retValue)
        {
            std::string moduleFileName = rt->GetXModuleFileName();
            int line = rt->GetTopStackCurrentLine();

            int level = 0;
            auto it = kwParams.find("level");
            if (it)
            {
                level = it->val;
            }
            Cantor::log.SetCurInfo(moduleFileName.c_str(), (const int)line, level);
            Cantor::log.SetLevel(level);
            for (auto& p : params)
            {
                Cantor::log << p.ToString();
            }
            Cantor::log << LINE_END;
            return true;
        }
        inline bool Log_nolineend(X::XRuntime* rt, X::XObj* pContext,
            X::ARGS& params, X::KWARGS& kwParams, X::Value& retValue)
        {
            int level = 0;
            auto it = kwParams.find("level");
            if (it)
            {
                level = it->val;
            }
            Cantor::log.LineBegin();
            Cantor::log.SetLevel(level);
            for (auto& p : params)
            {
                Cantor::log << p.ToString();
            }
            Cantor::log.LineEndUnlock();
            return true;
        }
    };
}