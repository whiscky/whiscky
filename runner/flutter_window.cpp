#pragma comment(lib, "winhttp.lib")
#include "flutter_window.h"
#include <dwmapi.h>
#include <flutter/method_channel.h>
#include <flutter/event_channel.h>
#include <flutter/event_sink.h>
#include <flutter/event_stream_handler_functions.h>
#include <optional>
#include "flutter/generated_plugin_registrant.h"
#include <flutter/standard_method_codec.h>
#include <winhttp.h>
#include <Windows.h>
#include <winbase.h>
#define _CRT_SECURE_NO_WARNINGS

std::unique_ptr<flutter::EventSink<flutter::EncodableValue>>&& mouseEvents = nullptr;

char* wideCharToMultiByte(wchar_t* pWCStrKey)
{
    size_t pSize = WideCharToMultiByte(CP_OEMCP, 0, pWCStrKey, wcslen(pWCStrKey), NULL, 0, NULL, NULL);
    char* pCStrKey = new char[pSize + 1];
    WideCharToMultiByte(CP_OEMCP, 0, pWCStrKey, wcslen(pWCStrKey), pCStrKey, pSize, NULL, NULL);
    pCStrKey[pSize] = '\0';
    GlobalFree(pWCStrKey);
    return pCStrKey;
}

char* getProxy() {
    _WINHTTP_CURRENT_USER_IE_PROXY_CONFIG net;
    WinHttpGetIEProxyConfigForCurrentUser(&net);
    if (net.lpszProxy == nullptr) {
        GlobalFree(net.lpszAutoConfigUrl);
        GlobalFree(net.lpszProxyBypass);
        return nullptr;
    }
    else {
        GlobalFree(net.lpszAutoConfigUrl);
        GlobalFree(net.lpszProxyBypass);
        return wideCharToMultiByte(net.lpszProxy);
    }
}

FlutterWindow::FlutterWindow(const flutter::DartProject& project)
    : project_(project) {}

FlutterWindow::~FlutterWindow() {}

bool FlutterWindow::OnCreate() {
    if (!Win32Window::OnCreate())
    {
        return false;
    }

    const RECT frame = GetClientArea();

    // The size here must match the window dimensions to avoid unnecessary surface
    // creation / destruction in the startup path.
    flutter_controller_ = std::make_unique<flutter::FlutterViewController>(
        frame.right - frame.left, frame.bottom - frame.top, project_);
    // Ensure that basic setup of the controller was successful.
    if (!flutter_controller_->engine() || !flutter_controller_->view()) {
        return false;
    }
    RegisterPlugins(flutter_controller_->engine());

    //检查系统代理的MethodChannel
    const flutter::MethodChannel<> channel(
        flutter_controller_->engine()->messenger(), "kokoiro.xyz.pica_comic/proxy", 
        &flutter::StandardMethodCodec::GetInstance()
    );
    channel.SetMethodCallHandler(
      [](const flutter::MethodCall<>& call,const std::unique_ptr<flutter::MethodResult<>>& result) {
          const auto res = getProxy();
          if (res != nullptr){
              std::string s = res;
              result->Success(s);
          }
          else
              result->Success(flutter::EncodableValue("No Proxy"));
          delete(res);
    });

    //监听鼠标侧键的EventChannel
    const auto channelName = "kokoiro.xyz.pica_comic/mouse";
    flutter::EventChannel<> channel2(
        flutter_controller_->engine()->messenger(), channelName, 
        &flutter::StandardMethodCodec::GetInstance()
    );

    auto eventHandler = std::make_unique<
        flutter::StreamHandlerFunctions<flutter::EncodableValue>>(
    [](
        const flutter::EncodableValue* arguments,
        std::unique_ptr<flutter::EventSink<flutter::EncodableValue>>&& events){
            mouseEvents = std::move(events);
            return nullptr;
    },
    [](const flutter::EncodableValue* arguments)
        -> std::unique_ptr<flutter::StreamHandlerError<flutter::EncodableValue>> {
            mouseEvents = nullptr;
            return nullptr;
    });
    
    channel2.SetStreamHandler(std::move(eventHandler));

    const flutter::MethodChannel<> channel3(
        flutter_controller_->engine()->messenger(), "pica_comic/title_bar",
        &flutter::StandardMethodCodec::GetInstance()
    );
    channel3.SetMethodCallHandler(
        [this](const flutter::MethodCall<>& call, const std::unique_ptr<flutter::MethodResult<>>& result) {
            auto value = static_cast<COLORREF>(std::get<int64_t>(*call.arguments()));
            COLORREF color = RGB(GetRValue(value), GetGValue(value), GetBValue(value));
            DwmSetWindowAttribute(GetHandle(), DWMWA_CAPTION_COLOR,
            &color, sizeof(color));
            RedrawWindow(GetHandle(), NULL, 0, RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
            result->Success();
        });

    const flutter::MethodChannel<> channel4(
        flutter_controller_->engine()->messenger(), "pica_comic/full_screen",
        &flutter::StandardMethodCodec::GetInstance()
    );
    channel4.SetMethodCallHandler(
        [this](const flutter::MethodCall<>& call, const std::unique_ptr<flutter::MethodResult<>>& result) {
            if (std::get<bool>(*call.arguments())) {
                GetWindowRect(GetHandle(), &windowRect);
                int screenWidth = GetSystemMetrics(SM_CXSCREEN);
                int screenHeight = GetSystemMetrics(SM_CYSCREEN);
                SetWindowLong(GetHandle(), GWL_STYLE, WS_POPUP);
                SetWindowPos(GetHandle(), HWND_TOP, 0, 0, screenWidth, screenHeight, SWP_SHOWWINDOW);
            }
            else {
                SetWindowLong(GetHandle(), GWL_STYLE, WS_OVERLAPPEDWINDOW);
                SetWindowPos(GetHandle(), HWND_TOP, windowRect.left, windowRect.top, windowRect.right - windowRect.left, windowRect.bottom - windowRect.top, SWP_SHOWWINDOW);
            }
            result->Success();
        });
    
    SetChildContent(flutter_controller_->view()->GetNativeWindow());

    flutter_controller_->engine()->SetNextFrameCallback([&]() {
        //this->Show();
    });

    return true;
}

void FlutterWindow::OnDestroy() {
  if (flutter_controller_) {
    flutter_controller_ = nullptr;
  }

  Win32Window::OnDestroy();
}

void mouse_side_button_listener(unsigned int input)
{
    if(mouseEvents != nullptr)
    {
        mouseEvents->Success(static_cast<int>(input));
    }
}

LRESULT
FlutterWindow::MessageHandler(HWND hwnd, UINT const message,
                              WPARAM const wparam,
                              LPARAM const lparam) noexcept {
  // Give Flutter, including plugins, an opportunity to handle window messages.
    UINT button = GET_XBUTTON_WPARAM(wparam);  
    if (button == XBUTTON1 && message == 528)
    {
        mouse_side_button_listener(0);
    }
    else if (button == XBUTTON2 && message == 528)
    {
        mouse_side_button_listener(1);
    }
    if (flutter_controller_) {
        std::optional<LRESULT> result =
            flutter_controller_->HandleTopLevelWindowProc(hwnd, message, wparam,
                                                      lparam);
        if (result) {
          return *result;
        }
    }

    switch (message) {
      case WM_FONTCHANGE:
          flutter_controller_->engine()->ReloadSystemFonts();
          break;
    }

  return Win32Window::MessageHandler(hwnd, message, wparam, lparam);
}