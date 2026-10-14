#include "game_data.h"
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <stdexcept>

namespace {
using namespace pcvr_data;
std::unique_ptr<Lock> game_lock;
constexpr int select_id=101, cancel_id=102;
struct Setup {
    fs::path game;
    HWND window=nullptr, status=nullptr, bar=nullptr, select=nullptr, cancel=nullptr;
    std::thread worker;
    std::mutex mutex;
    std::atomic<bool> canceled{false}, working{false}, finished{false};
    std::wstring detail=L"No complete game data was found. Select your original Xbox USA / NTSC-U ISO or XISO. The PC and European versions are not supported.";
    unsigned percent=0;
    bool success=false, close_requested=false;
    void choose() {
        wchar_t filename[32768]={0};
        OPENFILENAMEW dialog={sizeof dialog};
        dialog.hwndOwner=window; dialog.lpstrFile=filename; dialog.nMaxFile=32768;
        dialog.lpstrFilter=L"Xbox disc images (*.iso;*.xiso)\0*.iso;*.xiso\0All files (*.*)\0*.*\0\0";
        dialog.lpstrTitle=L"Select your Need For Speed Underground 2 Xbox USA disc image";
        dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_DONTADDTORECENT;
        if(!GetOpenFileNameW(&dialog)) {
            if(CommDlgExtendedError()) SetWindowTextW(status,L"The file picker could not open. Please try again.");
            return;
        }
        if(worker.joinable()) worker.join();
        canceled=false; finished=false; success=false; working=true; percent=0;
        { std::lock_guard<std::mutex> held(mutex); detail=L"Checking the Xbox disc image..."; }
        EnableWindow(select,FALSE); SetWindowTextW(status,L"Checking the Xbox disc image...");
        try {
            worker=std::thread([this,image=fs::path(filename)] {
                try {
                    import_iso(image,game,retail_manifest(),[this](uint64_t done,uint64_t total,const std::wstring &text) {
                        std::lock_guard<std::mutex> held(mutex);
                        percent=total?unsigned(done*100/total):0; detail=text;
                    },[this] { return canceled.load(); });
                    success=true;
                } catch(const std::exception &error) {
                    std::lock_guard<std::mutex> held(mutex); detail=wide(error.what());
                } catch(...) {
                    std::lock_guard<std::mutex> held(mutex); detail=L"Import failed. Your saved games have been kept. Please retry.";
                }
                finished=true;
            });
        } catch(...) {
            working=false; EnableWindow(select,TRUE);
            SetWindowTextW(status,L"Cannot start the import worker. Close other applications and retry.");
        }
    }
    void request_close() {
        if(working) { close_requested=true; canceled=true; EnableWindow(cancel,FALSE); SetWindowTextW(status,L"Canceling import. Please wait..."); }
        else DestroyWindow(window);
    }
};
LRESULT CALLBACK setup_proc(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
    auto *setup=(Setup *)GetWindowLongPtrW(window,GWLP_USERDATA);
    if(message==WM_NCCREATE) {
        setup=(Setup *)((CREATESTRUCTW *)lparam)->lpCreateParams;
        setup->window=window; SetWindowLongPtrW(window,GWLP_USERDATA,(LONG_PTR)setup);
    }
    if(!setup) return DefWindowProcW(window,message,wparam,lparam);
    switch(message) {
    case WM_COMMAND:
        if(LOWORD(wparam)==select_id && !setup->working) setup->choose();
        else if(LOWORD(wparam)==cancel_id) setup->request_close();
        return 0;
    case WM_CLOSE: setup->request_close(); return 0;
    case WM_TIMER: {
        if(!setup->close_requested) {
            std::lock_guard<std::mutex> held(setup->mutex);
            SetWindowTextW(setup->status,setup->detail.c_str());
            SendMessageW(setup->bar,PBM_SETPOS,setup->percent,0);
        }
        if(setup->working && setup->finished) {
            setup->worker.join(); setup->working=false;
            if(setup->success || setup->close_requested) DestroyWindow(window);
            else EnableWindow(setup->select,TRUE);
        }
        return 0;
    }
    case WM_DESTROY: KillTimer(window,1); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window,message,wparam,lparam);
}
bool show_setup(const fs::path &game) {
    INITCOMMONCONTROLSEX controls={sizeof controls,ICC_PROGRESS_CLASS}; InitCommonControlsEx(&controls);
    HINSTANCE instance=GetModuleHandleW(nullptr);
    WNDCLASSW cls={}; cls.lpfnWndProc=setup_proc; cls.hInstance=instance;
    cls.hCursor=LoadCursorW(nullptr,IDC_ARROW); cls.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);
    cls.lpszClassName=L"NFSU2PCVRGameSetup";
    if(!RegisterClassW(&cls) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) throw std::runtime_error("Cannot create the setup window.");
    Setup setup; setup.game=game;
    RECT rect={0,0,640,344}; AdjustWindowRect(&rect,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,FALSE);
    int width=rect.right-rect.left,height=rect.bottom-rect.top;
    HWND window=CreateWindowExW(WS_EX_APPWINDOW,cls.lpszClassName,L"Need For Speed Underground 2 VR - Game data setup",
        WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,(GetSystemMetrics(SM_CXSCREEN)-width)/2,
        (GetSystemMetrics(SM_CYSCREEN)-height)/2,width,height,nullptr,nullptr,instance,&setup);
    if(!window) throw std::runtime_error("Cannot create the setup window.");
    HFONT font=CreateFontW(-17,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
    auto child=[&](const wchar_t *type,const wchar_t *text,DWORD style,int x,int y,int w,int h,int id=0) {
        HWND control=CreateWindowExW(0,type,text,WS_CHILD|WS_VISIBLE|style,x,y,w,h,window,(HMENU)(INT_PTR)id,instance,nullptr);
        SendMessageW(control,WM_SETFONT,(WPARAM)font,TRUE); return control;
    };
    child(L"STATIC",L"Game data required",0,24,20,590,28);
    setup.status=child(L"STATIC",setup.detail.c_str(),0,24,57,590,72);
    child(L"STATIC",L"The selected image stays on your PC. Allow about 2.5 GB free space.\nYour saved games are preserved. VR starts after verification.",0,24,140,590,48);
    std::wstring destination=L"Install folder: "+game.wstring();
    child(L"STATIC",destination.c_str(),SS_PATHELLIPSIS,24,201,590,24);
    setup.bar=child(PROGRESS_CLASSW,L"",0,24,238,590,20);
    SendMessageW(setup.bar,PBM_SETRANGE32,0,100);
    setup.select=child(L"BUTTON",L"Select ISO...",WS_TABSTOP|BS_DEFPUSHBUTTON,330,281,145,36,select_id);
    setup.cancel=child(L"BUTTON",L"Cancel",WS_TABSTOP,489,281,125,36,cancel_id);
    SetTimer(window,1,100,nullptr);
    // The developer launcher hides the console via STARTUPINFO. Its show state
    // overrides the first ShowWindow call, so explicitly reveal the setup UI.
    ShowWindow(window,SW_SHOWNORMAL); ShowWindow(window,SW_SHOW);
    UpdateWindow(window); SetForegroundWindow(window); SetFocus(setup.select);
    MSG message;
    while(GetMessageW(&message,nullptr,0,0)>0) {
        if(!IsDialogMessageW(window,&message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    setup.canceled=true;
    if(setup.worker.joinable()) setup.worker.join();
    DeleteObject(font);
    return setup.success;
}
fs::path game_folder() {
    const wchar_t *configured=_wgetenv(L"NFSU2_GAME_DIR");
    if(configured && *configured) return fs::absolute(configured).lexically_normal();
    if(installed(fs::current_path()/L"game",retail_manifest())) return fs::current_path()/L"game";
    wchar_t exe[32768]; DWORD length=GetModuleFileNameW(nullptr,exe,32768);
    if(!length || length==32768) throw std::runtime_error("Cannot locate the PCVR application.");
    auto portable=fs::path(exe).parent_path()/L"game";
    if(installed(portable,retail_manifest())) return portable;
    wchar_t local[MAX_PATH];
    if(FAILED(SHGetFolderPathW(nullptr,CSIDL_LOCAL_APPDATA|CSIDL_FLAG_CREATE,nullptr,SHGFP_TYPE_CURRENT,local)))
        throw std::runtime_error("Cannot locate the local game-data folder.");
    return fs::path(local)/L"NFSU2VR"/L"game";
}
}
extern "C" int pcvr_prepare_game_data(void) {
    try {
        auto game=game_folder(); game_lock=std::make_unique<pcvr_data::Lock>(game);
        if(!pcvr_data::installed(game,pcvr_data::retail_manifest()) && !show_setup(game)) { game_lock.reset(); return 0; }
        if(!pcvr_data::installed(game,pcvr_data::retail_manifest())) throw std::runtime_error("Game-data verification failed. Please retry setup.");
        if(_putenv_s("NFSU2_GAME_DIR",pcvr_data::utf8(game.wstring()).c_str())) throw std::runtime_error("Cannot configure the game-data folder.");
        return 1;
    } catch(const std::exception &error) {
        MessageBoxW(nullptr,pcvr_data::wide(error.what()).c_str(),L"Need For Speed Underground 2 VR - Setup error",MB_OK|MB_ICONERROR);
        game_lock.reset(); return -1;
    }
}
extern "C" void pcvr_release_game_data(void) { game_lock.reset(); }
