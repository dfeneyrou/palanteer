// The MIT License (MIT)
//
// Copyright(c) 2021, Damien Feneyrou <dfeneyrou@gmail.com>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.


#ifdef _WIN32

#ifndef BS_NO_GRAPHIC
#include <SDKDDKVer.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#endif
#include <shlobj_core.h>

// System
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// OS common
#include "bs.h"
#include "bsOs.h"

#ifndef BS_NO_GRAPHIC
#define GL_WINDOWS_IMPLEMENTATION
#include "bsOsGlWin.h" // Before any other include, to ensure that we get the implementation
#include "bsKeycode.h"
#include <dwmapi.h>
#pragma comment(lib,"opengl32.lib")
#pragma comment(lib,"gdi32.lib")
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"dwmapi.lib")
#pragma comment(lib,"Ole32.lib")
#endif // ifndef BS_NO_GRAPHIC

#pragma comment(lib,"shell32.lib")

#include "bsString.h"
#include "bsTime.h"


#ifndef BS_NO_GRAPHIC

// Global graphical context for Windows
// ====================================

// Local context
static struct {
    int windowWidth     = -1;
    int windowHeight    = -1;
    int dpiWidth        = 96;
    int dpiHeight       = 96;
    HINSTANCE hInstance = 0;
    int       nCmdShow  = -1;
    LPTSTR    windowClass;      // Window Class
    HGLRC     renderingContext; // Rendering Context
    HDC       deviceContext;    // Device Context
    HWND      windowHandle;     // Window
    HCURSOR   defaultCursor;    // Cursor
    HCURSOR   currentCursor;
    bsOsHandler* osHandler = 0;
    bsString  userDataPath;
} gGlob;



// Window creation recipe (collection of pieces from internet)
// ===========================================================

// GL loading helper
#define LOAD_GLEX(glDecl, glFuncName)                                   \
    glFuncName = reinterpret_cast<glDecl>(wglGetProcAddress(#glFuncName)); \
    if(!glFuncName) { MessageBox(0, L"" #glFuncName "() failed.", L"Window::create", MB_ICONERROR); return; }


// Message box display helper
static void
showMessage(LPCWSTR message)
{
    MessageBox(0, message, L"Window::create", MB_ICONERROR);
}


// Forward declaration
static LRESULT CALLBACK WindowProcedure(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

// DPI awareness and getters from Dear Imgui, file backends/imgui_impl_win32.cpp
#ifndef DPI_ENUMS_DECLARED
typedef enum { MDT_EFFECTIVE_DPI = 0, MDT_ANGULAR_DPI = 1, MDT_RAW_DPI = 2, MDT_DEFAULT = MDT_EFFECTIVE_DPI } MONITOR_DPI_TYPE;
#endif
#ifndef _DPI_AWARENESS_CONTEXTS_
DECLARE_HANDLE(DPI_AWARENESS_CONTEXT);
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE (DPI_AWARENESS_CONTEXT)-3
#endif
typedef HRESULT(WINAPI* PFN_GetDpiForMonitor)(HMONITOR, MONITOR_DPI_TYPE, UINT*, UINT*); // Shcore.lib + dll, Windows 8.1+


void
osCreateWindow(const char* windowTitle, const char* configName, float ratioLeft, float ratioTop, float ratioRight, float ratioBottom, bool overrideWindowManager)
{
    plAssert(ratioLeft>=0.   && ratioLeft<=1.);
    plAssert(ratioTop>=0.    && ratioTop<=1.);
    plAssert(ratioRight>=0.  && ratioRight<=1.);
    plAssert(ratioBottom>=0. && ratioBottom<=1.);
    plAssert(ratioLeft<ratioRight);
    plAssert(ratioTop<ratioBottom);

    // Enable DPI awareness (per application, else there are some display issue when resizing...)
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);

    // Register the application class
    WNDCLASSEX wcex;
    ZeroMemory(&wcex, sizeof(wcex));
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC/* | CS_DBLCLKS*/;
    wcex.lpfnWndProc = WindowProcedure;
    wcex.hInstance   = gGlob.hInstance;
    wcex.hIcon       = LoadIcon(nullptr, IDI_WINLOGO);
    wcex.hCursor     = LoadCursor(NULL, IDC_ARROW);
    wcex.lpszClassName = L"OpenGL class";
    gGlob.windowClass = MAKEINTATOM(RegisterClassEx(&wcex));
    if(gGlob.windowClass==0) {
        showMessage(L"registerClass() failed.");
        return;
    }

    // create temporary window
    HWND fakeWND = CreateWindow(gGlob.windowClass, L"OpenGL class",
                                WS_CAPTION | WS_SYSMENU | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,  0, 0,  1, 1, // position x, y  width, height
                                NULL, NULL, gGlob.hInstance, NULL); // parent window, menu, instance, param
    HDC fakeDC = GetDC(fakeWND);    // Device Context

    PIXELFORMATDESCRIPTOR fakePFD;
    ZeroMemory(&fakePFD, sizeof(fakePFD));
    fakePFD.nSize      = sizeof(fakePFD);
    fakePFD.nVersion   = 1;
    fakePFD.dwFlags    = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_SUPPORT_COMPOSITION | PFD_DOUBLEBUFFER;
    fakePFD.iPixelType = PFD_TYPE_RGBA;
    fakePFD.cColorBits = 32;
    fakePFD.cAlphaBits = 8;
    fakePFD.cDepthBits = 24;

    const int fakePFDID = ChoosePixelFormat(fakeDC, &fakePFD);
    if(fakePFDID==0) {
        showMessage(L"ChoosePixelFormat() failed.");
        return;
    }

    if(SetPixelFormat(fakeDC, fakePFDID, &fakePFD)==false) {
        showMessage(L"SetPixelFormat() failed.");
        return;
    }

    HGLRC fakeRC = wglCreateContext(fakeDC);    // Rendering Context

    if(fakeRC==0) {
        showMessage(L"wglCreateContext() failed.");
        return;
    }

    if(wglMakeCurrent(fakeDC, fakeRC)==false) {
        showMessage(L"wglMakeCurrent() failed.");
        return;
    }

    // Get pointers to GL functions
    PFNWGLCHOOSEPIXELFORMATARBPROC wglChoosePixelFormatARB = nullptr;
    LOAD_GLEX(PFNWGLCHOOSEPIXELFORMATARBPROC, wglChoosePixelFormatARB);
    PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB = nullptr;
    LOAD_GLEX(PFNWGLCREATECONTEXTATTRIBSARBPROC, wglCreateContextAttribsARB);


    // Compute the window location & size
    RECT primaryDisplaySize;
    SystemParametersInfo(SPI_GETWORKAREA, 0, &primaryDisplaySize, 0);  // System taskbar and application desktop toolbars are excluded
    int dWidth  = primaryDisplaySize.right  - primaryDisplaySize.left;
    int dHeight = primaryDisplaySize.bottom - primaryDisplaySize.top;
    int x       = (int)(ratioLeft *(float)dWidth);
    int y       = (int)(ratioTop  *(float)dHeight);
    gGlob.windowWidth  = (int)((ratioRight-ratioLeft)*(float)dWidth);
    gGlob.windowHeight = (int)((ratioBottom-ratioTop)*(float)dHeight);

    // Compute the DPI
    HINSTANCE shcore_dll = ::LoadLibraryA("shcore.dll"); // Reference counted per-process
    PFN_GetDpiForMonitor GetDpiForMonitorFn = (PFN_GetDpiForMonitor)::GetProcAddress(shcore_dll, "GetDpiForMonitor");
    if(GetDpiForMonitorFn) {
        HMONITOR monitor = ::MonitorFromWindow(fakeWND, MONITOR_DEFAULTTONEAREST);
        UINT xdpi = 96, ydpi = 96;
        GetDpiForMonitorFn(monitor, MDT_EFFECTIVE_DPI, &xdpi, &ydpi);
        gGlob.dpiWidth  = xdpi;
        gGlob.dpiHeight = ydpi;
    }

    DWORD dwExStyle = WS_EX_APPWINDOW | WS_EX_WINDOWEDGE;  // Window Extended Style
    DWORD dwStyle   = WS_OVERLAPPEDWINDOW;                 // Windows Style
    RECT WindowRect; // Set the desired window position and size
    WindowRect.left   = (long)x;
    WindowRect.right  = (long)(x+gGlob.windowWidth);
    WindowRect.top    = (long)y;
    WindowRect.bottom = (long)(y+gGlob.windowHeight);
    AdjustWindowRectEx(&WindowRect, dwStyle, FALSE, dwExStyle); // Adjust window boundaries to the true requested size, including decorations

    // Create a new window and context
    gGlob.windowHandle = CreateWindowEx(dwExStyle,
                                        L"OpenGL class", L"Palanteer - viewer", // class name, window name
                                        dwStyle | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
                                        x, y,                                    // posx, posy
                                        WindowRect.right-WindowRect.left, WindowRect.bottom-WindowRect.top,  // width, height
                                        NULL, NULL,                             // parent window, menu
                                        gGlob.hInstance, NULL);                   // instance, param
    if(!gGlob.windowHandle) {
        showMessage(L"CreateWindowEx - failed");
        return;
    }

    gGlob.deviceContext = GetDC(gGlob.windowHandle);

    const int pixelAttribs[] = {
        WGL_DRAW_TO_WINDOW_ARB, GL_TRUE,
        WGL_SUPPORT_OPENGL_ARB, GL_TRUE,
        WGL_DOUBLE_BUFFER_ARB, GL_TRUE,
        WGL_PIXEL_TYPE_ARB, WGL_TYPE_RGBA_ARB,
        WGL_ACCELERATION_ARB, WGL_FULL_ACCELERATION_ARB,
        WGL_COLOR_BITS_ARB, 32,
        WGL_ALPHA_BITS_ARB, 8,
        WGL_DEPTH_BITS_ARB, 16,
        0
    };

    int pixelFormatID; UINT numFormats;
    bool status = wglChoosePixelFormatARB(gGlob.deviceContext, pixelAttribs, NULL, 1, &pixelFormatID, &numFormats);

    if(!status || numFormats==0) {
        showMessage(L"wglChoosePixelFormatARB() failed.");
        return;
    }

    PIXELFORMATDESCRIPTOR PFD;
    DescribePixelFormat(gGlob.deviceContext, pixelFormatID, sizeof(PFD), &PFD);
    SetPixelFormat(gGlob.deviceContext, pixelFormatID, &PFD);

    const int major_min = 3, minor_min = 3;
    const int contextAttribs[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, major_min,
        WGL_CONTEXT_MINOR_VERSION_ARB, minor_min,
        WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
        0
    };
    gGlob.renderingContext = wglCreateContextAttribsARB(gGlob.deviceContext, 0, contextAttribs);
    if(!gGlob.renderingContext) {
        showMessage(L"wglCreateContextAttribsARB() failed.");
        return;
    }

    // delete temporary context and window
    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(fakeRC);
    ReleaseDC(fakeWND, fakeDC);
    DestroyWindow(fakeWND);
    if(!wglMakeCurrent(gGlob.deviceContext, gGlob.renderingContext)) {
        showMessage(L"wglMakeCurrent() failed.");
        return;
    }

    // Load extensions
    LOAD_GLEX(PFNGLBINDBUFFERPROC, glBindBuffer);
    LOAD_GLEX(PFNGLDELETEBUFFERSPROC, glDeleteBuffers);
    LOAD_GLEX(PFNGLGENBUFFERSPROC, glGenBuffers);
    LOAD_GLEX(PFNGLBUFFERDATAPROC, glBufferData);
    LOAD_GLEX(PFNGLATTACHSHADERPROC, glAttachShader);
    LOAD_GLEX(PFNGLCOMPILESHADERPROC, glCompileShader);
    LOAD_GLEX(PFNGLCREATEPROGRAMPROC, glCreateProgram);
    LOAD_GLEX(PFNGLCREATESHADERPROC, glCreateShader);
    LOAD_GLEX(PFNGLDELETEPROGRAMPROC, glDeleteProgram);
    LOAD_GLEX(PFNGLDELETESHADERPROC, glDeleteShader);
    LOAD_GLEX(PFNGLDETACHSHADERPROC, glDetachShader);
    LOAD_GLEX(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray);
    LOAD_GLEX(PFNGLGETATTRIBLOCATIONPROC, glGetAttribLocation);
    LOAD_GLEX(PFNGLGETSHADERIVPROC, glGetShaderiv);
    LOAD_GLEX(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog);
    LOAD_GLEX(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation);
    LOAD_GLEX(PFNGLLINKPROGRAMPROC, glLinkProgram);
    LOAD_GLEX(PFNGLSHADERSOURCEPROC, glShaderSource);
    LOAD_GLEX(PFNGLUSEPROGRAMPROC, glUseProgram);
    LOAD_GLEX(PFNGLUNIFORMMATRIX4FVPROC, glUniformMatrix4fv);
    LOAD_GLEX(PFNGLUNIFORM1FPROC, glUniform1f);
    LOAD_GLEX(PFNGLUNIFORM2FPROC, glUniform2f);
    LOAD_GLEX(PFNGLUNIFORM3FPROC, glUniform3f);
    LOAD_GLEX(PFNGLUNIFORM4FPROC, glUniform4f);
    LOAD_GLEX(PFNGLUNIFORM1IPROC, glUniform1i);
    LOAD_GLEX(PFNGLUNIFORM2IPROC, glUniform2i);
    LOAD_GLEX(PFNGLUNIFORM3IPROC, glUniform3i);
    LOAD_GLEX(PFNGLUNIFORM4IPROC, glUniform4i);
    LOAD_GLEX(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer);
    LOAD_GLEX(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray);
    LOAD_GLEX(PFNGLDELETEVERTEXARRAYSPROC, glDeleteVertexArrays);
    LOAD_GLEX(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays);
    LOAD_GLEX(PFNGLBINDSAMPLERPROC, glBindSampler);
    LOAD_GLEX(PFNGLBLENDEQUATIONPROC, glBlendEquation);
    LOAD_GLEX(PFNGLACTIVETEXTUREPROC, glActiveTexture);
    LOAD_GLEX(PFNGLGENERATEMIPMAPPROC, glGenerateMipmap);

    LOAD_GLEX(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers);
    LOAD_GLEX(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer);
    LOAD_GLEX(PFNGLDELETEFRAMEBUFFERSPROC, glDeleteFramebuffers);
    LOAD_GLEX(PFNGLBINDRENDERBUFFERPROC, glBindRenderbuffer);
    LOAD_GLEX(PFNGLGENRENDERBUFFERSPROC, glGenRenderbuffers);
    LOAD_GLEX(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus);
    LOAD_GLEX(PFNGLRENDERBUFFERSTORAGEPROC, glRenderbufferStorage);
    LOAD_GLEX(PFNGLFRAMEBUFFERRENDERBUFFERPROC, glFramebufferRenderbuffer);
    LOAD_GLEX(PFNGLFRAMEBUFFERTEXTUREPROC, glFramebufferTexture);
    LOAD_GLEX(PFNGLDRAWBUFFERSPROC, glDrawBuffers);

    ShowWindow(gGlob.windowHandle, gGlob.nCmdShow);

    gGlob.defaultCursor = LoadCursor(NULL, IDC_ARROW);
    gGlob.currentCursor = gGlob.defaultCursor;

    // Compute the location of the application context, in the user data
    LPWSTR pProgramDataPath;
    if(FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, NULL, &pProgramDataPath))) {
        gGlob.userDataPath.clear();
        showMessage(L"Unable to get the user app data folder");
    } else {
        bsStringUtf16 path((char16_t*)pProgramDataPath);
        gGlob.userDataPath = path.toUtf8() + bsString("\\") + bsString(configName).capitalize();
        CoTaskMemFree(pProgramDataPath);
        // Create the directory if it does not yet exists
        if(!osDirectoryExists(gGlob.userDataPath)) {
            if(!CreateDirectory((wchar_t*)gGlob.userDataPath.toUtf16().toChar16(), NULL)) {
                showMessage(L"Error: Unable to create the folder");
            }
        }
    }
}


// OS interactions
// ===============

void
osGetWindowSize(int& width, int& height, int& dpiWidth, int& dpiHeight)
{
    width     = gGlob.windowWidth;
    height    = gGlob.windowHeight;
    dpiWidth  = gGlob.dpiWidth;
    dpiHeight = gGlob.dpiHeight;
}


void
osDestroyWindow(void)
{
    // The OS handler shall not be called anymore (maybe already destroyed)
    gGlob.osHandler = 0;

    wglMakeCurrent(NULL, NULL);
    if(gGlob.renderingContext) {
        wglDeleteContext(gGlob.renderingContext);
    }
    if(gGlob.deviceContext) {
        ReleaseDC(gGlob.windowHandle, gGlob.deviceContext);
    }
    if(gGlob.windowHandle) {
        DestroyWindow(gGlob.windowHandle);
    }
}


void
osSetIcon(int width, int height, const u8* pixels)
{
    static HICON currentIcon = 0;
    if(currentIcon) {
        DestroyIcon(currentIcon);
        currentIcon = 0;
    }

    bsVec<u8> iconPixels(width*height*4);
    memcpy(&iconPixels[0], pixels, iconPixels.size()*sizeof(u8));
    for(int i=0; i<iconPixels.size(); i+=4) bsSwap(iconPixels[i], iconPixels[i+2]); // Swap red and blue (Windows requirement...)
    currentIcon = CreateIcon(GetModuleHandleW(NULL), width, height, 1, 32, NULL, &iconPixels[0]);

    if(currentIcon) {
        SendMessageW(gGlob.windowHandle, WM_SETICON, ICON_BIG,   (LPARAM)currentIcon);
        SendMessageW(gGlob.windowHandle, WM_SETICON, ICON_SMALL, (LPARAM)currentIcon);
    }
}


void
osSetWindowTitle(const bsString& title)
{
    SetWindowText(gGlob.windowHandle, (wchar_t*)title.toUtf16().toChar16());
}


bool
osIsMouseVisible(void)
{
    return (gGlob.currentCursor==gGlob.defaultCursor);
}


void
osSetMouseVisible(bool state)
{
    if(state==osIsMouseVisible()) return;
    gGlob.currentCursor = state? gGlob.defaultCursor : NULL;
    SetCursor(gGlob.currentCursor);
}


bsString
osGetProgramDataPath(void)
{
    return gGlob.userDataPath;
}


void
osPushToClipboard(ClipboardType pushType, const bsStringUtf16& data)
{
    if(data.empty() || !OpenClipboard(NULL) || !EmptyClipboard()) return;

    HANDLE clipHandle = GlobalAlloc(GMEM_MOVEABLE, (data.size()+1)*sizeof(WCHAR));
    if(clipHandle) {
        wchar_t* ptr = (wchar_t*)GlobalLock(clipHandle);
        plAssert(ptr);
        memcpy(ptr, &data[0], data.size()*sizeof(WCHAR));
        ptr[data.size()] = 0;
        GlobalUnlock(clipHandle);
        SetClipboardData(CF_UNICODETEXT, clipHandle);
    }
    CloseClipboard();
}


bsStringUtf16
osReqFromClipboard(ClipboardType reqType)
{
    bsStringUtf16 result;

    // Open the clipboard
    if(!IsClipboardFormatAvailable(CF_UNICODETEXT)) return result;
    if(!OpenClipboard(NULL)) return result;

    // Get the data as Wchar (UTF-16 on windows)
    HANDLE clipHandle = GetClipboardData(CF_UNICODETEXT);
    if(!clipHandle) { CloseClipboard(); return result; }
    wchar_t* ptr = (wchar_t*)GlobalLock(clipHandle);
    plAssert(ptr);

    // We consider (erroneously but 'ok enough') wchar as 16-bit truncated unicode
    while(*ptr) result.push_back((char16_t)(*ptr++));

    // Release the clipboard and return
    GlobalUnlock(clipHandle);
    CloseClipboard();
    return result;
}


void
osSwapBuffer(void)
{
    SwapBuffers(gGlob.deviceContext);
}


void
osHideWindow(void)
{
    ShowWindow(gGlob.windowHandle, SW_MINIMIZE);
}


void
osShowWindow(void)
{
    ShowWindow(gGlob.windowHandle, SW_RESTORE);
}



bsKeycode
convertKeyCode(WPARAM key, LPARAM flags)
{
    switch (key) {

    case VK_SHIFT: { // Check the scancode to distinguish between left and right shift
        static UINT lShift = MapVirtualKeyW(VK_LSHIFT, MAPVK_VK_TO_VSC);
        UINT scancode      = static_cast<UINT>((flags & (0xFF << 16)) >> 16);
        return (scancode==lShift) ? KC_LShift : KC_RShift;
    }

    case VK_MENU : // Check the "extended" flag to distinguish between left and right alt
        return (HIWORD(flags) & KF_EXTENDED) ? KC_RAlt : KC_LAlt;

    case VK_CONTROL: // Check the "extended" flag to distinguish between left and right control
        return (HIWORD(flags) & KF_EXTENDED) ? KC_RControl : KC_LControl;

        // Other keys are reported properly
    case VK_LWIN:       return KC_LSystem;
    case VK_RWIN:       return KC_RSystem;
    case VK_APPS:       return KC_Menu;
    case VK_OEM_1:      return KC_Semicolon;
    case VK_OEM_2:      return KC_Slash;
    case VK_OEM_PLUS:   return KC_Equal;
    case VK_OEM_MINUS:  return KC_Hyphen;
    case VK_OEM_4:      return KC_LBracket;
    case VK_OEM_6:      return KC_RBracket;
    case VK_OEM_COMMA:  return KC_Comma;
    case VK_OEM_PERIOD: return KC_Period;
    case VK_OEM_7:      return KC_Quote;
    case VK_OEM_5:      return KC_Backslash;
    case VK_OEM_3:      return KC_Tilde;
    case VK_ESCAPE:     return KC_Escape;
    case VK_SPACE:      return KC_Space;
    case VK_RETURN:     return KC_Enter;
    case VK_BACK:       return KC_Backspace;
    case VK_TAB:        return KC_Tab;
    case VK_PRIOR:      return KC_PageUp;
    case VK_NEXT:       return KC_PageDown;
    case VK_END:        return KC_End;
    case VK_HOME:       return KC_Home;
    case VK_INSERT:     return KC_Insert;
    case VK_DELETE:     return KC_Delete;
    case VK_ADD:        return KC_Add;
    case VK_SUBTRACT:   return KC_Subtract;
    case VK_MULTIPLY:   return KC_Multiply;
    case VK_DIVIDE:     return KC_Divide;
    case VK_PAUSE:      return KC_Pause;
    case VK_F1:         return KC_F1;
    case VK_F2:         return KC_F2;
    case VK_F3:         return KC_F3;
    case VK_F4:         return KC_F4;
    case VK_F5:         return KC_F5;
    case VK_F6:         return KC_F6;
    case VK_F7:         return KC_F7;
    case VK_F8:         return KC_F8;
    case VK_F9:         return KC_F9;
    case VK_F10:        return KC_F10;
    case VK_F11:        return KC_F11;
    case VK_F12:        return KC_F12;
    case VK_F13:        return KC_F13;
    case VK_F14:        return KC_F14;
    case VK_F15:        return KC_F15;
    case VK_LEFT:       return KC_Left;
    case VK_RIGHT:      return KC_Right;
    case VK_UP:         return KC_Up;
    case VK_DOWN:       return KC_Down;
    case VK_NUMPAD0:    return KC_Numpad0;
    case VK_NUMPAD1:    return KC_Numpad1;
    case VK_NUMPAD2:    return KC_Numpad2;
    case VK_NUMPAD3:    return KC_Numpad3;
    case VK_NUMPAD4:    return KC_Numpad4;
    case VK_NUMPAD5:    return KC_Numpad5;
    case VK_NUMPAD6:    return KC_Numpad6;
    case VK_NUMPAD7:    return KC_Numpad7;
    case VK_NUMPAD8:    return KC_Numpad8;
    case VK_NUMPAD9:    return KC_Numpad9;
    case 'A':           return KC_A;
    case 'Z':           return KC_Z;
    case 'E':           return KC_E;
    case 'R':           return KC_R;
    case 'T':           return KC_T;
    case 'Y':           return KC_Y;
    case 'U':           return KC_U;
    case 'I':           return KC_I;
    case 'O':           return KC_O;
    case 'P':           return KC_P;
    case 'Q':           return KC_Q;
    case 'S':           return KC_S;
    case 'D':           return KC_D;
    case 'F':           return KC_F;
    case 'G':           return KC_G;
    case 'H':           return KC_H;
    case 'J':           return KC_J;
    case 'K':           return KC_K;
    case 'L':           return KC_L;
    case 'M':           return KC_M;
    case 'W':           return KC_W;
    case 'X':           return KC_X;
    case 'C':           return KC_C;
    case 'V':           return KC_V;
    case 'B':           return KC_B;
    case 'N':           return KC_N;
    case '0':           return KC_Num0;
    case '1':           return KC_Num1;
    case '2':           return KC_Num2;
    case '3':           return KC_Num3;
    case '4':           return KC_Num4;
    case '5':           return KC_Num5;
    case '6':           return KC_Num6;
    case '7':           return KC_Num7;
    case '8':           return KC_Num8;
    case '9':           return KC_Num9;
    }

    return KC_Unknown;
}


static
inline
bool isDisplayableKc(bsKeycode kc)
{
    return ( (kc>=KC_A && kc<=KC_Num9) ||
             (kc>=KC_LBracket && kc<=KC_Space) ||
             (kc>=KC_Add && kc<=KC_Divide) ||
             (kc>=KC_Numpad0 && kc<=KC_Numpad9) );
}



LRESULT CALLBACK WindowProcedure(HWND hWnd, UINT messageType, WPARAM wParam, LPARAM lParam)
{
    bsKeycode kc;
    bsKeyModState kms;
#define GET_KMS() { HIWORD(GetAsyncKeyState(VK_SHIFT))!=0, HIWORD(GetAsyncKeyState(VK_CONTROL))!=0, HIWORD(GetAsyncKeyState(VK_MENU))!=0, HIWORD(GetAsyncKeyState(VK_LWIN)) || HIWORD(GetAsyncKeyState(VK_RWIN)) }
    // Sanity
    if(!gGlob.osHandler) return DefWindowProc(hWnd, messageType, wParam, lParam);

    switch (messageType) {

    case WM_CHAR:
        if(bsIsUnicodeDisplayable((u32)wParam)) {
            gGlob.osHandler->eventChar((char16_t)wParam);
        }
        break;

    case WM_KEYDOWN:
        kms = GET_KMS();
        kc = convertKeyCode(wParam, lParam);
        gGlob.osHandler->eventKeyPressed(kc, kms);

        if(isDisplayableKc(kc)) {
            return DefWindowProc(hWnd, messageType, wParam, lParam); // So that we have WM_CHAR events generated
        }

        break;

    case WM_KEYUP:
        kms = GET_KMS();
        kc = convertKeyCode(wParam, lParam);
        gGlob.osHandler->eventKeyReleased(kc, kms);
        break;

        /*
          case WM_HOTKEY:
          gGlob.osHandler->eventHotKeyPressed();
          break;
        */

    case WM_KILLFOCUS:
        if(gGlob.osHandler->isVisible()) {
            osHideWindow();
            gGlob.osHandler->notifyUnmapped();
        }
        kms = GET_KMS();
        gGlob.osHandler->notifyLeave(kms);
        break;

    case WM_SETFOCUS:
        if(!gGlob.osHandler->isVisible()) {
            gGlob.osHandler->notifyMapped();
        }
        kms = GET_KMS();
        gGlob.osHandler->notifyEnter(kms);  // KMS resynchronization is important!
        break;

    case WM_LBUTTONDOWN:
        kms = GET_KMS();
        gGlob.osHandler->eventButtonPressed(1, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), kms);
        break;
    case WM_LBUTTONUP:
        kms = GET_KMS();
        gGlob.osHandler->eventButtonReleased(1, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), kms);
        break;

    case WM_MBUTTONDOWN:
        kms = GET_KMS();
        gGlob.osHandler->eventButtonPressed(2, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), kms);
        break;
    case WM_MBUTTONUP:
        kms = GET_KMS();
        gGlob.osHandler->eventButtonReleased(2, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), kms);
        break;

    case WM_RBUTTONDOWN:
        kms = GET_KMS();
        gGlob.osHandler->eventButtonPressed(3, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), kms);
        break;
    case WM_RBUTTONUP:
        kms = GET_KMS();
        gGlob.osHandler->eventButtonReleased(3, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), kms);
        break;
    case WM_MOUSEWHEEL:
        {
            POINT pt; pt.x = GET_X_LPARAM(lParam); pt.y = GET_Y_LPARAM(lParam); // Coordinates for the wheel are screen ones...
            ScreenToClient(hWnd, &pt);
            kms = GET_KMS();
            gGlob.osHandler->eventWheelScrolled(pt.x, pt.y, (GET_WHEEL_DELTA_WPARAM(wParam)<0)? +1:-1, kms);
        }
        break;
    case WM_MOUSEMOVE:
        gGlob.osHandler->eventMouseMotion(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        break;

    case WM_SIZE:
        gGlob.windowWidth  = LOWORD(lParam);
        gGlob.windowHeight = HIWORD(lParam);
        gGlob.osHandler->notifyWindowSize(gGlob.windowWidth, gGlob.windowHeight);
        break;

    case WM_CLOSE:
        gGlob.osHandler->eventKeyPressed(KC_Escape, {false,false,false,false});
        PostQuitMessage(0);
        break;

    default:
        /*printf("IGNORED MT: 0x%x\n", messageType);*/
        return DefWindowProc(hWnd, messageType, wParam, lParam);
    }
    return 0;       // message handled
}


void
osProcessInputs(bsOsHandler* handler)
{
    MSG msg;
    gGlob.osHandler = handler;
    while(PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        if(msg.message==WM_QUIT) {
            gGlob.osHandler->quit();
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}


// Windows entry point
int
APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
    // Update global scope
    gGlob.hInstance = hInstance;
    gGlob.nCmdShow  = nCmdShow;

    // Convert the command line from tchar to standard structure (in utf8)
    bsVec<bsString> argStrings;
    bsVec<char*> args;
    for(int i=0; i<__argc; ++i) {
        argStrings.push_back(bsStringUtf16((char16_t*)__wargv[i]).toUtf8());
        args.push_back((char*)argStrings.back().toChar());
    }

    return bsBootstrap(args.size(), &args[0]);
}

#endif  // ifndef BS_NO_GRAPHIC



// Misc OS
// =======

// strcasestr does not exists on windows
const char*
strcasestr(const char* s1, const char* s2)
{
    if(!s1 || !s2) return 0;  // Sanity

    while(*s1) {
        const char *ss1=s1, *ss2=s2;
        while(*ss1 && *ss2 && tolower(*ss1)==tolower(*ss2)) { ++ss1; ++ss2; }
        if(!*ss2) return s1; // Found!
        ++s1; // Try next character of s1 as a start
    }
    return 0; // s2 not found
}


bsDate
osGetDate(void)
{
    time_t currentTime;
    time(&currentTime);
    struct tm* t = localtime(&currentTime);
    plAssert(t);
    return bsDate{1900+t->tm_year, 1+t->tm_mon, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec};
}



// File system
// ===========

bsString
osGetCurrentPath(void)
{
    char16_t result[MAX_PATH];
    GetCurrentDirectory(MAX_PATH, (wchar_t*)result);
    return bsStringUtf16(result).toUtf8();
}


bool
osFileExists(const bsString& path)
{
    DWORD attribs = GetFileAttributesW((wchar_t*)path.toUtf16().toChar16());
    return (attribs!=INVALID_FILE_ATTRIBUTES && !(attribs&FILE_ATTRIBUTE_DIRECTORY));
}


bool
osDirectoryExists(const bsString& path)
{
    DWORD attribs = GetFileAttributes((wchar_t*)path.toUtf16().toChar16());
    return (attribs!=INVALID_FILE_ATTRIBUTES && (attribs&FILE_ATTRIBUTE_DIRECTORY));
}


bsString
osGetDirname(const bsString& path)
{
    int idx = path.rfindChar('\\');
    return (idx<0)? path : path.subString(0, idx);
}


bsString
osGetBasename(const bsString& path)
{
    int idx = path.rfindChar('\\');
    return (idx<0)? path : path.subString(idx+1, path.size());
}


u32
osGetDriveBitmap(void)
{
    return GetLogicalDrives();
}


FILE*
osFileOpen(const bsString& path, const char* mode)
{
    return _wfopen((wchar_t*)path.toUtf16().toChar16(), (wchar_t*)bsString(mode).toUtf16().toChar16());
}


bool
osLoadFileContent(const bsString& path, bsVec<u8>& buffer, int maxSize)
{
    // Get file size
    buffer.clear();
    size_t fileSize = osGetSize(path);
    if(fileSize==0) return false;
    if(maxSize>0 && fileSize>(size_t)maxSize) fileSize = maxSize;
    // Open it
    FILE* fh = osFileOpen(path, "rb");
    if(!fh) return false;
    // Read it in buffer
    buffer.resize((int)fileSize);
    if(fread(&buffer[0], 1, fileSize, fh)!=fileSize) {
        buffer.clear();
        fclose(fh);
        return false;
    }
    // Close and return
    fclose(fh);
    return true;
}


bool
osCopyFile(const bsString& srcPath, const bsString& dstPath)
{
    constexpr int chunkSize = 256*1024;
    bool isSuccess = false;
    u8* chunk = 0;
    FILE *fSrc = 0, *fDst = 0;

    // Open the 2 files
    fSrc = osFileOpen(srcPath, "rb");
    if(!(fSrc=osFileOpen(srcPath, "rb"))) goto end;
    if(!(fDst=osFileOpen(dstPath, "wb"))) goto end;

    // Read per chunk
    chunk = new u8[chunkSize];
    int readBytes;
    while((readBytes=(int)fread(chunk, 1, chunkSize, fSrc))>0) {
        fwrite(chunk, 1, readBytes, fDst);
    }
    isSuccess = true;

 end:
    if(fSrc) fclose(fSrc);
    if(fDst) fclose(fDst);
    delete[] chunk;
    return isSuccess;
}


bsDirStatusCode
osMakeDir(const bsString& path)
{
    int status = SHCreateDirectoryEx(0, (wchar_t*)path.toUtf16().toChar16(), 0);
    if     (status==ERROR_SUCCESS) return bsDirStatusCode::OK;
    else if(status==ERROR_FILE_EXISTS || status==ERROR_ALREADY_EXISTS) return bsDirStatusCode::ALREADY_EXISTS;
    return bsDirStatusCode::FAILURE;
}


bsDirStatusCode
osGetDirContent(const bsString& path, bsVec<bsDirEntry>& entries)
{
    entries.clear();
    DWORD attribs = GetFileAttributes((wchar_t*)path.toUtf16().toChar16());
    if(attribs==INVALID_FILE_ATTRIBUTES)    return bsDirStatusCode::DOES_NOT_EXIST;
    if(!(attribs&FILE_ATTRIBUTE_DIRECTORY)) return bsDirStatusCode::NOT_A_DIRECTORY;

    // Open directory
    WIN32_FIND_DATAW data;
    HANDLE sh = FindFirstFile((wchar_t*)(path+"\\*").toUtf16().toChar16(), &data);
    if(sh==INVALID_HANDLE_VALUE) {
        DWORD errorCode = GetLastError();
        if(errorCode==ERROR_FILE_NOT_FOUND) return bsDirStatusCode::DOES_NOT_EXIST;
        return bsDirStatusCode::FAILURE;
    }

    // List the content
    do {
        if(wcscmp(data.cFileName, L".")!=0 && wcscmp(data.cFileName, L"..")!=0) {
            bool isDir = ((data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)==FILE_ATTRIBUTE_DIRECTORY);
            entries.push_back(bsDirEntry{bsStringUtf16((char16_t*)data.cFileName).toUtf8(), isDir});
        }
    } while(FindNextFile(sh, &data));

    FindClose(sh);
    return bsDirStatusCode::OK;
}


size_t
osGetSize(const bsString& path)
{
    // Get the first file
    WIN32_FIND_DATAW data;
    HANDLE sh = FindFirstFile((wchar_t*)(path+(osDirectoryExists(path)? "\\*":"")).toUtf16().toChar16(), &data);
    if(sh==INVALID_HANDLE_VALUE) return 0;

    // Loop until the last one
    size_t dirSize = 0;
    do {
        if(wcscmp(data.cFileName, L".")!=0 && wcscmp(data.cFileName, L"..")!=0) {
            if((data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)==FILE_ATTRIBUTE_DIRECTORY) {
                dirSize += osGetSize(path+"\\"+ bsStringUtf16((char16_t*)data.cFileName).toUtf8());
            } else
                dirSize += (size_t)(data.nFileSizeHigh*(MAXDWORD)+data.nFileSizeLow);
        }
    } while(FindNextFile(sh, &data));
    FindClose(sh);
    return dirSize;
}


bsDate
osGetCreationDate(const bsString& path)
{
    WIN32_FILE_ATTRIBUTE_DATA fileInfo;
    FILETIME                  localFileTime;
    SYSTEMTIME                sysTime;
    if(!GetFileAttributesEx((wchar_t*)path.toUtf16().toChar16(), GetFileExInfoStandard, &fileInfo)) return bsDate(); // If error, all fields are zero
    FileTimeToLocalFileTime(&fileInfo.ftCreationTime, &localFileTime);
    FileTimeToSystemTime(&localFileTime, &sysTime);
    return bsDate{sysTime.wYear, sysTime.wMonth, sysTime.wDay, sysTime.wHour, sysTime.wMinute, sysTime.wSecond};
}


bsDirStatusCode
osRemoveFile(const bsString& path)
{
    if(DeleteFile((wchar_t*)path.toUtf16().toChar16())!=0) return bsDirStatusCode::OK;
    return bsDirStatusCode::FAILURE;
}


bsDirStatusCode
osRemoveDir(const bsString& path, bool onlyIfEmpty)
{
    plAssert(0, "Not implemented for Windows");
    return bsDirStatusCode::FAILURE;
}

#endif // _WIN32
