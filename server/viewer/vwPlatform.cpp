// Palanteer viewer
// Copyright (C) 2021, Damien Feneyrou <dfeneyrou@gmail.com>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as
// published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

// This file implements the base application glue directly on top of the OS, a bit like a "main".
// Drawing is subcontracted to the graphic backend

// System
#include <cstdio>
#include <cstdlib>
#include <cstring>

// External
#include "imgui.h"

// Internal
#define PL_IMPLEMENTATION 1
#define PL_IMPL_COLLECTION_BUFFER_BYTE_QTY 20000000 // Children iterators are verbose
#ifdef _WIN32
#define PL_IMPL_MANAGE_WINDOWS_SOCKET 0
#define PL_IMPL_CONSOLE_COLOR 0
void crashErrorLogger(const char* msg);
#define PL_IMPL_PRINT_STDERR(msg, isCrash, isLastFromCrash) crashErrorLogger(msg)
#endif
#ifndef PL_GROUP_PL_VERBOSE
#define PL_GROUP_PL_VERBOSE 0
#endif

#include "bsOs.h"
#include "bsTime.h"
#include "cmCompress.h"
#include "vwPlatform.h"
#include "vwFontData.h"
#include "vwMain.h"
#include "vwConfig.h"
#include "vwGfxBackend.h"


// Parameters
const int RENDER_FRAME_US      = 33000;
const int BOUNCE_RENDER_GAP_US = 500000; // 0.5 second bounce

#ifdef _WIN32
void
crashErrorLogger(const char* msg)
{
    bsString path   = osGetProgramDataPath()+"\\error_palanteer.log";
    FILE* errorFile = fopen(path.toChar(), "a");
    if(!errorFile) return;
    fprintf(errorFile, "%s", msg);
    fclose(errorFile);
}
#endif


// Clipboard wrappers for ImGui
static
const char*
vwGetClipboardText(void* user_data)
{
    (void)user_data;
    static bsString lastString;
    lastString = osReqFromClipboard(ClipboardType::UTF8).toUtf8();
    return lastString.toChar();
}


static
void
vwSetClipboardText(void* user_data, const char* text)
{
    (void)user_data;
    osPushToClipboard(ClipboardType::UTF8, bsString(text).toUtf16());
}




// ==============================================================================================
// Entry point
// ==============================================================================================

int
bsBootstrap(int argc, char* argv[])
{
    // Parse arguments
    int  rxPort = 59059;
    int  debugPort = -1;
    bool doLoadLastFile = true;
    bool doDisplayHelp = false;
    plMode palanteerMode = PL_MODE_INACTIVE; (void)palanteerMode;
    bsString overrideStoragePath;
    int i = 1;
    while(i<argc) {
        // Port
        if((!strcmp(argv[i], "-port") || !strcmp(argv[i], "--port") || !strcmp(argv[i], "/port")) && i<argc-1) {
            rxPort = strtol(argv[i+1], 0, 0);
            if(rxPort<=0 || rxPort>=65536) {
                printf("ERROR: Port shall be in the range [1;65536[\n");
                return 1;
            }
            printf("Listening port is %d\n", rxPort);
            ++i;
        }
        else if(!strcmp(argv[i], "-f") || !strcmp(argv[i], "/f")) {
#if USE_PL==0
            printf("ERROR: Palanteer is not present in this build, so cannot record on file.\n");
            return 1;
#endif
            palanteerMode = PL_MODE_STORE_IN_FILE;
            plSetFilename("viewer.pltraw");
        }
        else if((!strcmp(argv[i], "-c") || !strcmp(argv[i], "/c")) && i<argc-1) {
#if USE_PL==0
            printf("ERROR: Palanteer is not present in this build, so cannot record in connected mode.\n");
            return 1;
#endif
            debugPort = strtol(argv[i+1], 0, 0);
            if(debugPort<=0 || debugPort>=65536) {
                printf("Port shall be in the range [1;65536[\n");
                return 1;
            }
            palanteerMode = PL_MODE_CONNECTED;
            plSetServer("127.0.0.1", debugPort);
        }
        else if((!strcmp(argv[i], "-tmpdb") || !strcmp(argv[i], "--tmpdb") || !strcmp(argv[i], "/tmpdb")) && i<argc-1) {
            overrideStoragePath = argv[i+1];
            printf("Overriden record database root path: %s\n", overrideStoragePath.toChar());
            ++i;
        }
        else if(!strcmp(argv[i], "-nl") || !strcmp(argv[i], "--nl") || !strcmp(argv[i], "/nl")) {
            doLoadLastFile = false;
        }
        else if(!strcmp(argv[i], "-help") || !strcmp(argv[i], "--help") || !strcmp(argv[i], "-h") ||
                !strcmp(argv[i], "/help")  || !strcmp(argv[i], "/?")) {
            doDisplayHelp = true;
        }
        else if(!strcmp(argv[i], "-version") || !strcmp(argv[i], "--version") || !strcmp(argv[i], "/version")) {
            printf("Palanteer v%s\n", PALANTEER_VERSION);
            return 0;
        }
        else {
            printf("ERROR: Unknown parameter '%s'\n", argv[i]);
            doDisplayHelp = true;
        }
        // Next argument
        ++i;
    }

    if(debugPort==rxPort) {
        printf("ERROR: It is forbidden to have identical listening and debug socket port (Palanteer viewer in connected mode).\n");
        printf("       Indeed, this would create some Larsen effect affecting the debug information.\n");
        printf("       If you want to debug the viewer, launch another viewer or use scripting, on a different port\n");
        doDisplayHelp = true;
    }

    if(doDisplayHelp) {
        printf("Palanteer: a tool to profile and view internals of your application\n");
        printf(" Syntax :  palanteer [options]\n");
        printf(" Options:\n");
        printf("  -port <port>      listen to programs on this port (default: %d)\n", rxPort);
        printf("  -nl               do not load the last record, after launch\n");
        printf("  -f                saves the viewer's instrumentation data in a file\n");
        printf("  -c <debug port>   send  the viewer's instrumentation data remotely.\n");
        printf("                    <debug port> shall be different from the listening <port> to avoid Larsen effect.\n");
        printf("  -tmpdb <path>     non persistent root path for the record database. Typically used for testing\n");
        printf("  --version         dumps the version\n");
        printf("  -h or --help      dumps this help\n");
        return 1;
    }

    // Init
    plInitAndStart("Palanteer viewer", palanteerMode);
    plDeclareThread("Main");
    {
        plScope("Initialize OS layer");
        osCreateWindow("Palanteer", "palanteer", 0.03f, 0.03f, 0.95f, 0.95f);
    }
    vwPlatform* platform = 0;
    {
        plScope("Create platform");
        platform = new vwPlatform(rxPort, doLoadLastFile, overrideStoragePath);
    }

    // Run application
    platform->run();

    // Clean
    {
        plScope("Destroy Platform");
        delete platform;
    }
    {
        plScope("Destroy OS layer");
        osDestroyWindow();
    }
    plStopAndUninit();
    return 0;
}


// ==============================================================================================
// Wrapper on the OS and ImGUI
// ==============================================================================================

vwPlatform::vwPlatform(int rxPort, bool doLoadLastFile, const bsString& overrideStoragePath)
    : _doExit(0), _isVisible(0), _dirtyRedrawCount(VW_REDRAW_PER_NTF)
{
    // Update ImGui
    int dpiWidth, dpiHeight;
    osGetWindowSize(_displayWidth, _displayHeight, dpiWidth, dpiHeight);
    _dpiScale = (float)dpiWidth/96.f; // No support of dynamic DPI change

    // Initialize the compression
    cmInitChunkCompress();

    // Creation of the main application
    _main = new vwMain(this, rxPort, overrideStoragePath);

    // Setup ImGui
    ImGui::CreateContext();
    ImGuiIO& io     = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.DisplaySize  = ImVec2((float)_displayWidth, (float)_displayHeight);
    io.DisplayFramebufferScale  = ImVec2(1.f, 1.f);  // High DPI is handled with increased font size and Imgui spatial constants
    io.IniFilename  = 0; // Disable config file save
    io.MouseDragThreshold = 1.; // 1 pixel threshold to detect that we are dragging
    io.ConfigInputTextCursorBlink = false;
    configureStyle();
    _newFontSizeToInstall = _main->getConfig().getFontSize();
    ImGui::GetStyle().ScaleAllSizes(_dpiScale);

    // @#TODO: set window size & pos from config. And show the window only after that

    // Keyboard mapping. ImGui will use those indices to peek into the io.KeyDown[] array.
    static_assert(KC_KeyCount<512, "Dear ImGui does not expect more than 512 different keys");
    static_assert(sizeof(ImDrawIdx)==4, "ImDrawIdx shall be 32 bits");
    io.KeyMap[ImGuiKey_Tab]        = KC_Tab;
    io.KeyMap[ImGuiKey_LeftArrow]  = KC_Left;
    io.KeyMap[ImGuiKey_RightArrow] = KC_Right;
    io.KeyMap[ImGuiKey_UpArrow]    = KC_Up;
    io.KeyMap[ImGuiKey_DownArrow]  = KC_Down;
    io.KeyMap[ImGuiKey_PageUp]     = KC_PageUp;
    io.KeyMap[ImGuiKey_PageDown]   = KC_PageDown;
    io.KeyMap[ImGuiKey_Home]       = KC_Home;
    io.KeyMap[ImGuiKey_End]        = KC_End;
    io.KeyMap[ImGuiKey_Insert]     = KC_Insert;
    io.KeyMap[ImGuiKey_Delete]     = KC_Delete;
    io.KeyMap[ImGuiKey_Backspace]  = KC_Backspace;
    io.KeyMap[ImGuiKey_Space]      = KC_Space;
    io.KeyMap[ImGuiKey_Enter]      = KC_Enter;
    io.KeyMap[ImGuiKey_Escape]     = KC_Escape;
    io.KeyMap[ImGuiKey_A] = KC_A;
    io.KeyMap[ImGuiKey_C] = KC_C;
    io.KeyMap[ImGuiKey_V] = KC_V;
    io.KeyMap[ImGuiKey_X] = KC_X;
    io.KeyMap[ImGuiKey_Y] = KC_Y;
    io.KeyMap[ImGuiKey_Z] = KC_Z;

    // Install callbacks
    io.SetClipboardTextFn = vwSetClipboardText;
    io.GetClipboardTextFn = vwGetClipboardText;
    io.ClipboardUserData  = 0;

    // Initialize the graphical backend
    vwBackendInit();

    // Notify the start of the main application
    _main->notifyStart(doLoadLastFile);
}


vwPlatform::~vwPlatform(void)
{
    delete _main;
    vwBackendUninit();
    ImGui::DestroyContext();
    cmUninitChunkCompress();
}


void
vwPlatform::run(void)
{
    bool doExit = false;
    while(!doExit) {

        // Inputs
        bsUs_t frameStartUs = bsGetClockUs();
        osProcessInputs(this);
        if(_doExit.load()) doExit = true; // Required to have 1 frame drawn with the exit flag set

        // Render
        if(redraw()) {
            plScope("swapBuffer");
            osSwapBuffer();
        }

        // Power management (frame rate limit)
        _lastRenderingDurationUs = bsGetClockUs()-frameStartUs;
        bsUs_t sleepDurationUs   = RENDER_FRAME_US-_lastRenderingDurationUs;
        if(sleepDurationUs>0) {
            plScope("frame rate limiting");
            bsSleep(sleepDurationUs);
        }
    }
}


bool
vwPlatform::redraw(void)
{
    // Filter out some redraw based on the dirtiness of the display state.
    // Dear Imgui requires several frames to handle user events properly, so we display per batch.
    // Also a "bounce" is required after a delay for some tooltip to appear, even if no user event occurs.
    bsUs_t currentTimeUs = bsGetClockUs();
    u64 tmp              = _dirtyRedrawCount.load();
    int dirtyRedrawCount = (int)(tmp&0xFFFFFFFF);
    int bounceCount      = (int)((tmp>>32)&0xFFFFFFFF);
    if(dirtyRedrawCount<=0 && !(bounceCount==1 && currentTimeUs-_lastRenderingTimeUs>=BOUNCE_RENDER_GAP_US)) {
        return false; // Display is not dirty and it is not a bounce time: nothing to display
    }
#define WRITE_DIRTY_COUNT(drc,bc) _dirtyRedrawCount.store((((u64)(bc))<<32) | ((u32)((drc)&0xFFFFFFFF)))
    if(dirtyRedrawCount>=0) {
        if(dirtyRedrawCount>0) --dirtyRedrawCount;
        if(dirtyRedrawCount==0) {
            bounceCount++;
            if(bounceCount==2) {
                WRITE_DIRTY_COUNT(VW_REDRAW_PER_BOUNCE, bounceCount);
            } else {
                WRITE_DIRTY_COUNT(dirtyRedrawCount, bounceCount);
                return false;
            }
        } else WRITE_DIRTY_COUNT(dirtyRedrawCount, bounceCount);
    }

    // Change font, if needed
    if(_newFontSizeToInstall>0) {
        vwBackendInstallFont(vwGetFontDataRobotoMedium(), vwGetFontDataSizeRobotoMedium(),
                             (int)bsRound((double)(_dpiScale*_newFontSizeToInstall)));
        _newFontSizeToInstall = -1;
    }

    // Update inputs for ImGui
    plScope("vwPlatform::redraw");
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)_displayWidth, (float)_displayHeight);
    io.DeltaTime = (_lastRenderingTimeUs==0)? 0.001f : 0.000001f*(float)(currentTimeUs-_lastRenderingTimeUs);
    _lastRenderingTimeUs = currentTimeUs;

    // Compute the vertices
    _main->beforeDraw(_doExit.load());
    ImGui::NewFrame();
    _main->draw();
    ImGui::Render();
    _lastUpdateDurationUs = bsGetClockUs()-currentTimeUs;

    // Draw
    return vwBackendDraw();
}


bool
vwPlatform::captureScreen(int* width, int* height, u8** buffer)
{
    return vwCaptureScreen(width, height, buffer);
}


void
vwPlatform::configureStyle(void)
{
    // Dark side of the style, as a base
    ImGui::StyleColorsDark();
    // Customization
    ImVec4* colors = ImGui::GetStyle().Colors;
    colors[ImGuiCol_Text]                   = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_TextDisabled]           = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_WindowBg]               = ImVec4(0.113f, 0.117f, 0.10f, 1.00f); // Less blue = "warmer" dark
    colors[ImGuiCol_ChildBg]                = ImVec4(1.00f, 1.00f, 1.00f, 0.00f);
    colors[ImGuiCol_PopupBg]                = ImVec4(0.15f, 0.15f, 0.15f, 0.90f);
    colors[ImGuiCol_Border]                 = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
    colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg]                = ImVec4(0.30f, 0.31f, 0.32f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.20f, 0.40f, 0.40f, 1.00f);
    colors[ImGuiCol_FrameBgActive]          = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_TitleBg]                = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
    colors[ImGuiCol_TitleBgActive]          = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.00f, 0.00f, 0.00f, 0.51f);
    colors[ImGuiCol_MenuBarBg]              = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.02f, 0.02f, 0.02f, 0.53f);
    colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.41f, 0.41f, 0.41f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
    colors[ImGuiCol_CheckMark]              = ImVec4(0.94f, 0.94f, 0.94f, 1.00f);
    colors[ImGuiCol_SliderGrab]             = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.86f, 0.86f, 0.86f, 1.00f);
    colors[ImGuiCol_Button]                 = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
    colors[ImGuiCol_ButtonHovered]          = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_ButtonActive]           = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_Header]                 = ImVec4(1.00f, 0.70f, 0.70f, 0.31f);
    colors[ImGuiCol_HeaderHovered]          = ImVec4(0.75f, 0.70f, 0.70f, 0.80f);
    colors[ImGuiCol_HeaderActive]           = ImVec4(0.58f, 0.50f, 0.52f, 1.00f);

    colors[ImGuiCol_Tab]                    = ImVec4(0.13f, 0.24f, 0.41f, 1.f);
    colors[ImGuiCol_TabHovered]             = ImVec4(0.26f, 0.59f, 0.98f, 1.f);
    colors[ImGuiCol_TabActive]              = ImVec4(0.20f, 0.41f, 0.68f, 1.f);
    colors[ImGuiCol_TabUnfocused]           = ImVec4(0.07f, 0.10f, 0.15f, 1.f);
    colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.14f, 0.26f, 0.42f, 1.f);

    colors[ImGuiCol_Separator]              = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
    colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.72f, 0.72f, 0.72f, 0.78f);
    colors[ImGuiCol_SeparatorActive]        = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
    colors[ImGuiCol_ResizeGrip]             = ImVec4(0.91f, 0.91f, 0.91f, 0.25f);
    colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.81f, 0.81f, 0.81f, 0.67f);
    colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.46f, 0.46f, 0.46f, 0.95f);
    colors[ImGuiCol_PlotLines]              = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered]       = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
    colors[ImGuiCol_PlotHistogram]          = ImVec4(0.73f, 0.60f, 0.15f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
    colors[ImGuiCol_TableHeaderBg]          = ImVec4(1.00f, 0.70f, 0.70f, 0.31f);
    colors[ImGuiCol_TableBorderStrong]      = ImVec4(0.41f, 0.41f, 0.45f, 1.00f);   // Prefer using Alpha=1.0 here
    colors[ImGuiCol_TableBorderLight]       = ImVec4(0.33f, 0.33f, 0.35f, 1.00f);   // Prefer using Alpha=1.0 here
    colors[ImGuiCol_TableRowBg]             = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt]          = ImVec4(0.30f, 0.30f, 0.30f, 0.30f);
    colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.87f, 0.87f, 0.87f, 0.35f);
    colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
    colors[ImGuiCol_DragDropTarget]         = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
    colors[ImGuiCol_NavHighlight]           = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
    colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);

    ImGui::GetStyle().WindowRounding    = 2.0f;
    ImGui::GetStyle().TabRounding       = 2.0f;
    ImGui::GetStyle().ScrollbarRounding = 2.0f;
}


// ==============================================================================================
// OS event handlers
// ==============================================================================================

void
vwPlatform::notifyEnter(bsKeyModState kms)
{
    // Reset the modifier key state
    // Note: we have to do so because we do not know when active if it is the left or the right one...
    ImGuiIO& io = ImGui::GetIO();
    io.KeyCtrl  = io.KeysDown[KC_LControl] = io.KeysDown[KC_RControl] = false;
    io.KeyShift = io.KeysDown[KC_LShift]   = io.KeysDown[KC_RShift]   = false;
    io.KeyAlt   = io.KeysDown[KC_LAlt]     = io.KeysDown[KC_RAlt]     = false;
    io.KeySuper = io.KeysDown[KC_LSystem]  = io.KeysDown[KC_RSystem]  = false;;

    // Set the active modifiers (left)
    if(kms.ctrl)  io.KeyCtrl  = io.KeysDown[KC_LControl] = true;
    if(kms.shift) io.KeyShift = io.KeysDown[KC_LShift]   = true;
    if(kms.alt)   io.KeyAlt   = io.KeysDown[KC_LAlt]     = true;
    if(kms.sys)   io.KeySuper = io.KeysDown[KC_LSystem]  = true;

    // The modifier keys are now up to date, whatver happens outside the window
    notifyDrawDirty();
}


void
vwPlatform::notifyLeave(bsKeyModState kms)
{
    // Nothing special to do
    (void)kms;
}


void
vwPlatform::eventKeyPressed(bsKeycode keycode, bsKeyModState kms)
{
    (void)kms;
    plAssert(keycode>=KC_A && keycode<KC_KeyCount, (int)keycode);
    ImGuiIO& io = ImGui::GetIO();
    io.KeysDown[keycode] = true;
    io.KeyCtrl  = io.KeysDown[KC_LControl] || io.KeysDown[KC_RControl];
    io.KeyShift = io.KeysDown[KC_LShift]   || io.KeysDown[KC_RShift];
    io.KeyAlt   = io.KeysDown[KC_LAlt]     || io.KeysDown[KC_RAlt];
    io.KeySuper = io.KeysDown[KC_LSystem]  || io.KeysDown[KC_RSystem];

#if 0
    // Escape: quit application. Enabled only for easier iterations when developing
    if(keycode==KC_Escape) {
        quit(); // Position the flag to exit the application
    }
#endif

    notifyDrawDirty();
}


void
vwPlatform::eventKeyReleased(bsKeycode keycode, bsKeyModState kms)
{
    (void)kms;
    plAssert(keycode>=KC_A && keycode<KC_KeyCount, (int)keycode);
    ImGuiIO& io = ImGui::GetIO();
    io.KeysDown[keycode] = false;
    io.KeyCtrl  = io.KeysDown[KC_LControl] || io.KeysDown[KC_RControl];
    io.KeyShift = io.KeysDown[KC_LShift]   || io.KeysDown[KC_RShift];
    io.KeyAlt   = io.KeysDown[KC_LAlt]     || io.KeysDown[KC_RAlt];
    io.KeySuper = io.KeysDown[KC_LSystem]  || io.KeysDown[KC_RSystem];
    notifyDrawDirty();
}


void
vwPlatform::eventWheelScrolled (int x, int y, int steps, bsKeyModState kms)
{
    (void)x; (void)y; (void)kms;
    ImGuiIO& io     = ImGui::GetIO();
    io.MouseWheel  -= (float)steps;
    _lastMouseMoveTimeUs = bsGetClockUs();
    notifyDrawDirty();
}


void
vwPlatform::eventChar(char16_t codepoint)
{
    ImGuiIO& io = ImGui::GetIO();
    if(codepoint!=0) {
        io.AddInputCharacter((u16)codepoint);
    }
    notifyDrawDirty();
}


void
vwPlatform::eventButtonPressed (int buttonId, int x, int y, bsKeyModState kms)
{
    (void)kms;
    ImGuiIO& io = ImGui::GetIO();
    io.MouseDown[buttonId-1] = true;
    io.MousePos = ImVec2((float)x, (float)y);
    _lastMouseMoveTimeUs = bsGetClockUs();
    notifyDrawDirty();
}


void
vwPlatform::eventButtonReleased(int buttonId, int x, int y, bsKeyModState kms)
{
    (void)kms;
    ImGuiIO& io = ImGui::GetIO();
    io.MouseDown[buttonId-1] = false;
    io.MousePos = ImVec2((float)x, (float)y);
    _lastMouseMoveTimeUs = bsGetClockUs();
    notifyDrawDirty();
}


void
vwPlatform::eventMouseMotion(int x, int y)
{
    ImGuiIO& io = ImGui::GetIO();
    io.MousePos = ImVec2((float)x, (float)y);
    _lastMouseMoveTimeUs = bsGetClockUs();
    notifyDrawDirty();
}
