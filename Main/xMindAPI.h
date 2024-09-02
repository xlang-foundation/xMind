#pragma once
#include "singleton.h"
#include "xpackage.h"
#include "log.h"
#include "Locker.h"
#include "AgentGraph.h"
#include "BaseAgent.h"
#include "Function.h"
#include "BaseAction.h"
#include "type_def.h"

namespace xMind
{
    class MindAPISet :
        public Singleton<MindAPISet>
    {
        std::string m_RootPath;
        X::Runtime* m_defaultRuntime = nullptr;

        BEGIN_PACKAGE(MindAPISet)
            APISET().AddConst("OK", (int)Status::Ok);
            APISET().AddConst("Fail", (int)Status::Fail);
            APISET().AddConst("Timeout", (int)Status::Timeout);
            APISET().AddConst("Running", (int)Status::Running);
            APISET().AddConst("Stopped", (int)Status::Stopped);
            APISET().AddEvent("OnReady");
            APISET().AddEvent("OnShutdown");
            APISET().AddVarFunc("log", &MindAPISet::Log);
            APISET().AddVarFunc("logV", &MindAPISet::LogV);
            APISET().AddVarFunc("log_nolineend", &MindAPISet::Log_nolineend);
            APISET().AddFunc<0>("GetRootPath", &MindAPISet::GetRootPath);
            APISET().AddVarFuncEx("Function", &MindAPISet::Create<Function>);
            APISET().AddVarFuncEx("Action", &MindAPISet::Create<BaseAction>);
            APISET().AddVarFuncEx("Agent", &MindAPISet::Create<BaseAgent>);
            APISET().AddClass<0, AgentGraph>("Graph");
            APISET().AddClass<0, Function>("MindFunction");
            APISET().AddClass<0, BaseAction>("MindAction");
            APISET().AddClass<0, BaseAgent>("MindAgent");

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
        void CalcDecoratorParameter(X::ARGS& expres, X::KWARGS* pKwParams);

        template<class T>   
        inline void Create(X::XRuntime* rt, X::XObj* pThis, X::XObj* pContext,
            X::ARGS& params, X::KWARGS& kwParams, X::Value& trailer, X::Value& retValue)
        {   
            X::Value realObj;
            std::string name;
            //if trailer is valid, it means it is a decorator
            //and params is the expression objects of the decorator
            if (trailer.IsValid())
            {
                CalcDecoratorParameter(params, &kwParams);
                auto it = kwParams.find("name");
                if (it)
                {
                    name = it->val.ToString(); 

                }
                else
                {
                    it = kwParams.find("Name");
                    if(it)
					{
						name = it->val.ToString();
					}
                    else
                    {
                        if (trailer.IsObject())
                        {
                            X::Func func(trailer);
                            name = func->GetName().ToString();
                        }
                    }
                }
                realObj = trailer;
            }
            else
            {
                if (params.size() >= 2)
                {
                    realObj = params[0];
                    name = params[1].ToString();
                }
                else if(params.size() == 1)
				{
					realObj = params[0];
                    if(realObj.IsObject())
					{
                        X::Func func(realObj);
						name = func->GetName().ToString();
					}
				}
            }
            T* pObj = new T();
            pObj->SetName(name);
            pObj->SetParams(kwParams);
            pObj->SetImplObject(realObj);
            pObj->Create();
            auto* pXPack = T::APISET().GetProxy(pObj);
            retValue = X::Value(pXPack);
        }
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