#include "agentflow/windows_tray.hpp"
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>

void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){
    try{
        const auto raw=std::filesystem::current_path().u8string();const std::string workspace(reinterpret_cast<const char*>(raw.data()),raw.size());
        for(const auto* bad:{"https://example.com/ui/","http://127.0.0.1:0/ui/","http://127.0.0.1:65536/ui/","http://127.0.0.1:80/ui/?token=private","http://127.0.0.1:80/extra/ui/"}){
            bool rejected=false;try{agentflow::WindowsTray tray(bad,workspace,[]{});}catch(const std::invalid_argument&){rejected=true;}require(rejected,"Tray must accept only its canonical local UI URL");
        }
        HWND window=nullptr;NOTIFYICONIDENTIFIER identity{};identity.cbSize=sizeof(identity);identity.uID=1;
        {
            agentflow::WindowsTray tray("http://127.0.0.1:55653/ui/",workspace,[]{});
            window=reinterpret_cast<HWND>(tray.native_window());
            if(!tray.visible()){std::cout<<"Desktop notification area unavailable; tray registration not qualified\n";return 77;}
            require(IsWindow(window),"Tray needs its own live message window");require(!IsWindowVisible(window),"Tray must not create a visible application window");
            identity.hWnd=window;RECT bounds{};require(SUCCEEDED(Shell_NotifyIconGetRect(&identity,&bounds)),"Windows must recognize the actual registered tray icon");
            SendMessageW(window,RegisterWindowMessageW(L"TaskbarCreated"),0,0);
            require(tray.visible()&&SUCCEEDED(Shell_NotifyIconGetRect(&identity,&bounds)),"Explorer recreation must preserve notification registration");
        }
        require(!IsWindow(window),"Tray shutdown must destroy its owned message window");
        RECT bounds{};require(FAILED(Shell_NotifyIconGetRect(&identity,&bounds)),"Tray shutdown must remove its notification icon");
        std::cout<<"Actual Windows tray registration, Explorer recovery and cleanup passed; no browser or backend was started\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
