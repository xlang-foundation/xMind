#include "agentflow/windows_tray.hpp"
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <objbase.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <future>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace agentflow {
namespace {
constexpr UINT callback_message=WM_APP+73,icon_id=1;
constexpr UINT open_ui=101,open_folder=103,quit_server=104;
constexpr wchar_t window_class[]=L"xMind.LocalWeb.Tray";
std::wstring wide(const std::string& text){
    if(text.empty())return {};
    const int size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0);
    if(size<=0)throw std::invalid_argument("Tray text must be UTF-8");
    std::wstring result(size,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),result.data(),size);return result;
}
std::wstring local_url(const std::string& url){
    constexpr std::string_view prefix="http://127.0.0.1:",suffix="/ui/";
    if(!url.starts_with(prefix)||!url.ends_with(suffix))throw std::invalid_argument("Tray requires the local web UI address");
    const auto number=std::string_view(url).substr(prefix.size(),url.size()-prefix.size()-suffix.size());unsigned port=0;
    const auto parsed=std::from_chars(number.data(),number.data()+number.size(),port);
    if(parsed.ec!=std::errc{}||parsed.ptr!=number.data()+number.size()||port<1||port>65535)throw std::invalid_argument("Invalid tray web port");
    return wide(url);
}
HICON make_icon(){
    // Small native vector mark, supersampled for a crisp light/dark taskbar.
    constexpr int size=32,samples=4;BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=size;info.bmiHeader.biHeight=-size;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* memory=nullptr;const auto color=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&memory,nullptr,0);if(!color)return nullptr;
    auto* pixels=static_cast<std::uint32_t*>(memory);
    for(int y=0;y<size;++y)for(int x=0;x<size;++x){
        double r=0,g=0,b=0,a=0;
        for(int sy=0;sy<samples;++sy)for(int sx=0;sx<samples;++sx){
            const double px=x+(sx+0.5)/samples,py=y+(sy+0.5)/samples;
            const double dx=std::max(std::abs(px-16)-8.0,0.0),dy=std::max(std::abs(py-16)-8.0,0.0);
            if(dx*dx+dy*dy>36)continue;
            const auto circle=[&](double cx,double cy,double radius){return (px-cx)*(px-cx)+(py-cy)*(py-cy)<=radius*radius;};
            const auto stroke=[&](double ax,double ay,double bx,double by){
                const double vx=bx-ax,vy=by-ay,t=std::clamp(((px-ax)*vx+(py-ay)*vy)/(vx*vx+vy*vy),0.0,1.0);
                return circle(ax+t*vx,ay+t*vy,1.2);
            };
            // A branching thought: three generous nodes and an open stem.
            // The cyan node gives the mark direction without lettering.
            const bool accent=circle(23,10,3.3);
            const bool mark=accent||circle(9,10,3.3)||circle(16,18,3.6)||stroke(9,10,16,18)||stroke(23,10,16,18)||stroke(16,18,16,25);
            const double t=py/32;
            r+=accent?82:mark?245:35+22*t;g+=accent?230:mark?248:47+6*t;b+=accent?207:mark?255:89+33*t;a+=255;
        }
        constexpr double count=samples*samples;pixels[y*size+x]=(std::uint32_t(a/count)<<24)|(std::uint32_t(r/count)<<16)|(std::uint32_t(g/count)<<8)|std::uint32_t(b/count);
    }
    const std::array<unsigned char,size*size/8> mask_bits{};
    const auto mask=CreateBitmap(size,size,1,1,mask_bits.data());ICONINFO icon{TRUE,0,0,mask,color};const auto result=mask?CreateIconIndirect(&icon):nullptr;
    if(mask)DeleteObject(mask);DeleteObject(color);return result;
}
}
struct WindowsTray::Impl {
    std::wstring url,workspace,title;
    std::function<void()> quit;
    std::atomic<HWND> window=nullptr;
    std::atomic<bool> installed=false;
    std::thread thread;
    HICON icon=nullptr;
    UINT taskbar_created=0;
    Impl(std::string address,std::string root,std::function<void()> stop):url(local_url(address)),workspace(wide(root)),quit(std::move(stop)){
        const auto path=std::filesystem::u8path(root);if(!path.is_absolute()||!std::filesystem::is_directory(path))throw std::invalid_argument("Tray workspace is unavailable");
        title=L"xMind - "+path.filename().wstring();if(title.size()>110)title.resize(110);
        std::promise<void> ready;auto future=ready.get_future();
        thread=std::thread([this,ready=std::move(ready)]()mutable{
            struct Apartment{HRESULT status=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED|COINIT_DISABLE_OLE1DDE);~Apartment(){if(SUCCEEDED(status))CoUninitialize();}} apartment;
            WNDCLASSW type{};type.lpfnWndProc=procedure;type.hInstance=GetModuleHandleW(nullptr);type.lpszClassName=window_class;
            if(!RegisterClassW(&type)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS){ready.set_value();return;}
            taskbar_created=RegisterWindowMessageW(L"TaskbarCreated");icon=make_icon();
            const auto handle=CreateWindowExW(WS_EX_TOOLWINDOW,window_class,L"xMind tray",WS_POPUP,0,0,0,0,nullptr,nullptr,type.hInstance,this);
            window.store(handle);if(handle)add_icon();ready.set_value();
            if(handle){MSG message;while(GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}}
            if(icon)DestroyIcon(icon);icon=nullptr;
        });future.get();
    }
    ~Impl(){if(const auto handle=window.load())PostMessageW(handle,WM_CLOSE,0,0);if(thread.joinable())thread.join();}
    NOTIFYICONDATAW notification()const{
        NOTIFYICONDATAW data{};data.cbSize=sizeof(data);data.hWnd=window.load();data.uID=icon_id;
        data.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP|NIF_SHOWTIP;data.uCallbackMessage=callback_message;data.hIcon=icon;
        std::copy(title.begin(),title.end(),data.szTip);return data;
    }
    void add_icon(){
        if(!icon)return;auto data=notification();const bool added=Shell_NotifyIconW(NIM_ADD,&data)||Shell_NotifyIconW(NIM_MODIFY,&data);
        installed.store(added);if(added){data.uVersion=NOTIFYICON_VERSION_4;Shell_NotifyIconW(NIM_SETVERSION,&data);}
    }
    void launch(const std::wstring& target){
        if(reinterpret_cast<INT_PTR>(ShellExecuteW(window.load(),L"open",target.c_str(),nullptr,nullptr,SW_SHOWNORMAL))<=32)
            MessageBoxW(window.load(),L"Windows could not open this location. Check your default browser or file association.",L"xMind",MB_OK|MB_ICONINFORMATION);
    }
    void menu(){
        const auto popup=CreatePopupMenu();if(!popup)return;
        AppendMenuW(popup,MF_STRING|MF_DISABLED,0,title.c_str());AppendMenuW(popup,MF_SEPARATOR,0,nullptr);
        AppendMenuW(popup,MF_STRING,open_ui,L"Open web UI");SetMenuDefaultItem(popup,open_ui,FALSE);
        AppendMenuW(popup,MF_STRING,open_folder,L"Open workspace");
        AppendMenuW(popup,MF_SEPARATOR,0,nullptr);AppendMenuW(popup,MF_STRING,quit_server,L"Quit this xMind server...");
        POINT point{};GetCursorPos(&point);SetForegroundWindow(window.load());
        const auto action=TrackPopupMenuEx(popup,TPM_RETURNCMD|TPM_NONOTIFY|TPM_RIGHTBUTTON,point.x,point.y,window.load(),nullptr);
        DestroyMenu(popup);PostMessageW(window.load(),WM_NULL,0,0);
        if(action==open_ui)launch(url);else if(action==open_folder)launch(workspace);
        else if(action==quit_server&&MessageBoxW(window.load(),L"Quit this xMind server?\n\nActive work on this server will stop. Saved conversations remain on disk.",L"Quit xMind",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)==IDYES&&quit)quit();
    }
    static LRESULT CALLBACK procedure(HWND handle,UINT message,WPARAM wparam,LPARAM lparam){
        auto* self=reinterpret_cast<Impl*>(GetWindowLongPtrW(handle,GWLP_USERDATA));
        if(message==WM_NCCREATE){self=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);SetWindowLongPtrW(handle,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(handle,message,wparam,lparam);
        if(message==self->taskbar_created&&self->taskbar_created){self->add_icon();return 0;}
        if(message==callback_message){try{const auto event=LOWORD(lparam);if(event==WM_CONTEXTMENU||event==NIN_SELECT||event==NIN_KEYSELECT)self->menu();}catch(...){/* No UI failure may stop the backend. */}return 0;}
        if(message==WM_CLOSE){DestroyWindow(handle);return 0;}
        if(message==WM_DESTROY){auto data=self->notification();Shell_NotifyIconW(NIM_DELETE,&data);self->installed.store(false);self->window.store(nullptr);PostQuitMessage(0);return 0;}
        return DefWindowProcW(handle,message,wparam,lparam);
    }
};
WindowsTray::WindowsTray(std::string url,std::string workspace,std::function<void()> quit):impl_(std::make_unique<Impl>(std::move(url),std::move(workspace),std::move(quit))){}
WindowsTray::~WindowsTray()=default;
bool WindowsTray::visible()const noexcept{return impl_->installed.load();}
std::uintptr_t WindowsTray::native_window()const noexcept{return reinterpret_cast<std::uintptr_t>(impl_->window.load());}
}
#endif
