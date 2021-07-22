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

// This file implements the base application functionalities directly on top of the OS, a bit like a "main".
// It also manages openGL.

// System
#include <stdio.h>
#include <stdlib.h>
#include <cstring>

// External
#include "imgui.h"

// Internal
#define PL_IMPLEMENTATION 1
#define PL_IMPL_COLLECTION_BUFFER_BYTE_QTY 20000000 // Children iterators are verbose
#ifdef _WIN32
#define PL_IMPL_MANAGE_WINDOWS_SOCKET 0
#define PL_IMPL_STACKTRACE_COLOR 0
void crashErrorLogger(const char* msg);
#define PL_IMPL_PRINT_STDERR(msg, isCrash, isLastFromCrash) crashErrorLogger(msg)
#endif
#ifndef PL_GROUP_PL_VERBOSE
#define PL_GROUP_PL_VERBOSE 0
#endif

#include "bsOs.h"
#include "bsGl.h"
#include "bsTime.h"
#include "cmCompress.h"
#include "vwPlatform.h"
#include "vwFontData.h"
#include "vwMain.h"
#include "vwConfig.h"


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


// ==============================================================================================
// OpenGL Shader
// ==============================================================================================

static const GLchar* guiVertexShaderSrc =
    "#version 300 es\n"
    "uniform mat4 ProjMtx;\n"
    "in vec2 Position;\n"
    "in vec2 UV;\n"
    "in vec4 Color;\n"
    "out vec2 Frag_UV;\n"
    "out vec4 Frag_Color;\n"
    "void main()\n"
    "{\n"
    "   Frag_UV = UV;\n"
    "   Frag_Color = Color;\n"
    "   gl_Position = ProjMtx * vec4(Position.xy,0,1);\n"
    "}\n";

static const GLchar* guiFragmentShaderSrc =
    "#version 300 es\n"
    "precision mediump float;\n"
    "uniform sampler2D Texture;\n"
    "in vec2 Frag_UV;\n"
    "in vec4 Frag_Color;\n"
    "out vec4 Out_Color;\n"
    "void main()\n"
    "{\n"
    "   Out_Color = vec4(Frag_Color.xyz, Frag_Color.w*texture(Texture, Frag_UV.st));\n"
    "}\n";



// Clipboard wrappers for ImGui
static
const char*
vwGetClipboardText(void* user_data)
{
    static bsString lastString;
    lastString = osReqFromClipboard(ClipboardType::UTF8).toUtf8();
    return lastString.toChar();
}


static
void
vwSetClipboardText(void* user_data, const char* text)
{
    osPushToClipboard(ClipboardType::UTF8, bsString(text).toUtf16());
}


static
void
vwStyle(void)
{
    // Dark side of the style, as a base
    ImGui::StyleColorsDark();
    // Customization
    ImVec4* colors = ImGui::GetStyle().Colors;
    colors[ImGuiCol_Text]                   = ImVec4(1.00, 1.00, 1.00, 1.00);
    colors[ImGuiCol_TextDisabled]           = ImVec4(0.50, 0.50, 0.50, 1.00);
    colors[ImGuiCol_WindowBg]               = ImVec4(0.113, 0.117, 0.10, 1.00); // Less blue = "warmer" dark
    colors[ImGuiCol_ChildBg]                = ImVec4(1.00, 1.00, 1.00, 0.00);
    colors[ImGuiCol_PopupBg]                = ImVec4(0.15, 0.15, 0.15, 0.90);
    colors[ImGuiCol_Border]                 = ImVec4(0.43, 0.43, 0.50, 0.50);
    colors[ImGuiCol_BorderShadow]           = ImVec4(0.00, 0.00, 0.00, 0.00);
    colors[ImGuiCol_FrameBg]                = ImVec4(0.30, 0.31, 0.32, 1.00);
    colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.20, 0.40, 0.40, 1.00);
    colors[ImGuiCol_FrameBgActive]          = ImVec4(0.25, 0.25, 0.25, 1.00);
    colors[ImGuiCol_TitleBg]                = ImVec4(0.30, 0.30, 0.30, 1.00);
    colors[ImGuiCol_TitleBgActive]          = ImVec4(0.40, 0.40, 0.40, 1.00);
    colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.00, 0.00, 0.00, 0.51);
    colors[ImGuiCol_MenuBarBg]              = ImVec4(0.14, 0.14, 0.14, 1.00);
    colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.02, 0.02, 0.02, 0.53);
    colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.31, 0.31, 0.31, 1.00);
    colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.41, 0.41, 0.41, 1.00);
    colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.51, 0.51, 0.51, 1.00);
    colors[ImGuiCol_CheckMark]              = ImVec4(0.94, 0.94, 0.94, 1.00);
    colors[ImGuiCol_SliderGrab]             = ImVec4(0.51, 0.51, 0.51, 1.00);
    colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.86, 0.86, 0.86, 1.00);
    colors[ImGuiCol_Button]                 = ImVec4(0.30, 0.30, 0.30, 1.00);
    colors[ImGuiCol_ButtonHovered]          = ImVec4(0.50, 0.50, 0.50, 1.00);
    colors[ImGuiCol_ButtonActive]           = ImVec4(0.25, 0.25, 0.25, 1.00);
    colors[ImGuiCol_Header]                 = ImVec4(1.00, 0.70, 0.70, 0.31);
    colors[ImGuiCol_HeaderHovered]          = ImVec4(0.75, 0.70, 0.70, 0.80);
    colors[ImGuiCol_HeaderActive]           = ImVec4(0.58, 0.50, 0.52, 1.00);

    colors[ImGuiCol_Tab]                    = ImVec4(0.13, 0.24, 0.41, 1.);
    colors[ImGuiCol_TabHovered]             = ImVec4(0.26, 0.59, 0.98, 1.);
    colors[ImGuiCol_TabActive]              = ImVec4(0.20, 0.41, 0.68, 1.);
    colors[ImGuiCol_TabUnfocused]           = ImVec4(0.07, 0.10, 0.15, 1.);
    colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.14, 0.26, 0.42, 1.);

    colors[ImGuiCol_Separator]              = ImVec4(0.43, 0.43, 0.50, 0.50);
    colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.72, 0.72, 0.72, 0.78);
    colors[ImGuiCol_SeparatorActive]        = ImVec4(0.51, 0.51, 0.51, 1.00);
    colors[ImGuiCol_ResizeGrip]             = ImVec4(0.91, 0.91, 0.91, 0.25);
    colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.81, 0.81, 0.81, 0.67);
    colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.46, 0.46, 0.46, 0.95);
    colors[ImGuiCol_PlotLines]              = ImVec4(0.61, 0.61, 0.61, 1.00);
    colors[ImGuiCol_PlotLinesHovered]       = ImVec4(1.00, 0.43, 0.35, 1.00);
    colors[ImGuiCol_PlotHistogram]          = ImVec4(0.73, 0.60, 0.15, 1.00);
    colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(1.00, 0.60, 0.00, 1.00);
    colors[ImGuiCol_TableHeaderBg]          = ImVec4(1.00, 0.70, 0.70, 0.31);
    colors[ImGuiCol_TableBorderStrong]      = ImVec4(0.41f, 0.41f, 0.45f, 1.00f);   // Prefer using Alpha=1.0 here
    colors[ImGuiCol_TableBorderLight]       = ImVec4(0.33f, 0.33f, 0.35f, 1.00f);   // Prefer using Alpha=1.0 here
    colors[ImGuiCol_TableRowBg]             = ImVec4(0.00, 0.00, 0.00, 0.00);
    colors[ImGuiCol_TableRowBgAlt]          = ImVec4(0.30, 0.30, 0.30, 0.30);
    colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.87, 0.87, 0.87, 0.35);
    colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.80, 0.80, 0.80, 0.35);
    colors[ImGuiCol_DragDropTarget]         = ImVec4(1.00, 1.00, 0.00, 0.90);
    colors[ImGuiCol_NavHighlight]           = ImVec4(0.60, 0.60, 0.60, 1.00);
    colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(1.00, 1.00, 1.00, 0.70);

    ImGui::GetStyle().WindowRounding    = 2.0;
    ImGui::GetStyle().TabRounding       = 2.0;
    ImGui::GetStyle().ScrollbarRounding = 2.0;
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
                exit(1);
            }
            printf("Listening port is %d\n", rxPort);
            ++i;
        }
        else if(!strcmp(argv[i], "-f") || !strcmp(argv[i], "/f")) {
#if USE_PL==0
            printf("ERROR: Palanteer is not present in this build, so cannot record on file.\n");
            exit(1);
#endif
            palanteerMode = PL_MODE_STORE_IN_FILE;
            plSetFilename("viewer.pltraw");
        }
        else if((!strcmp(argv[i], "-c") || !strcmp(argv[i], "/c")) && i<argc-1) {
#if USE_PL==0
            printf("ERROR: Palanteer is not present in this build, so cannot record in connected mode.\n");
            exit(1);
#endif
            debugPort = strtol(argv[i+1], 0, 0);
            if(debugPort<=0 || debugPort>=65536) {
                printf("Port shall be in the range [1;65536[\n");
                exit(1);
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
        printf("  -h or --help      dumps this help\n");
        exit(1);
    }

    // Init
    plInitAndStart("Palanteer viewer", palanteerMode);
    plDeclareThread("Main");
    {
        plScope("Initialize OS layer");
        osCreateWindow("Palanteer", "palanteer", 0.03, 0.03, 0.95, 0.95);
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
    osGetWindowSize(_displayWidth, _displayHeight);

    // Initialize the compression
    cmInitChunkCompress();

    // Creation of the main application
    _main = new vwMain(this, rxPort, overrideStoragePath);

    // Setup ImGui
    ImGui::CreateContext();
    ImGuiIO& io     = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.DisplaySize  = ImVec2(_displayWidth, _displayHeight);
    io.IniFilename  = 0; // Disable config file save
    io.MouseDragThreshold = 1.; // 1 pixel threshold to detect that we are dragging
    io.ConfigInputTextCursorBlink = false;
    vwStyle();
    _newFontSizeToInstall = _main->getConfig().getFontSize();

    // @#TODO: set window size & pos from config. And show the window only after that

    // Keyboard mapping. ImGui will use those indices to peek into the io.KeyDown[] array.
    static_assert(KC_KeyCount<512, "Dear ImGui does not expect more than 512 different keys");
    static_assert(sizeof(ImDrawIdx)==4, "ImDrawIdx shall be 32 bit");
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

    // Allocate the font (fully initialized later)
    glGenTextures(1, &_fontTextureId);
    glBindTexture(GL_TEXTURE_2D, _fontTextureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

    // Build and configure the OpenGL Vertex Array Object for GUI
    _guiGlProgram.install(guiVertexShaderSrc, guiFragmentShaderSrc);
    _unifAttribLocationTex      = glGetUniformLocation(_guiGlProgram.getId(), "Texture");
    _unifAttribLocationProjMtx  = glGetUniformLocation(_guiGlProgram.getId(), "ProjMtx");
    _attribLocationPosition     = glGetAttribLocation(_guiGlProgram.getId(),  "Position");
    _attribLocationUV           = glGetAttribLocation(_guiGlProgram.getId(),  "UV");
    _attribLocationColor        = glGetAttribLocation(_guiGlProgram.getId(),  "Color");
    glEnableVertexAttribArray(_attribLocationPosition);
    glEnableVertexAttribArray(_attribLocationUV);
    glEnableVertexAttribArray(_attribLocationColor);
    glVertexAttribPointer(_attribLocationPosition, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), (GLvoid*)IM_OFFSETOF(ImDrawVert, pos));
    glVertexAttribPointer(_attribLocationUV, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), (GLvoid*)IM_OFFSETOF(ImDrawVert, uv));
    glVertexAttribPointer(_attribLocationColor, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(ImDrawVert), (GLvoid*)IM_OFFSETOF(ImDrawVert, col));
    GL_CHECK();

    // Base GL setup
    glClearColor(0.3, 0.3, 0.3, 1.0);

    // Notify the start of the main application
    _main->notifyStart(doLoadLastFile);
}


vwPlatform::~vwPlatform(void)
{
    delete _main;
    _guiGlProgram.deinstall();
    if(_fontTextureId) {
        glDeleteTextures(1, &_fontTextureId);
        ImGui::GetIO().Fonts->TexID = 0;
        _fontTextureId = 0;
    }
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
        if((s64)sleepDurationUs>0) {
            plScope("frame rate limiting");
            bsSleep(sleepDurationUs);
        }
    }
}


void
vwPlatform::installFont(const void* fontData, int fontDataSize, int fontSize)
{
    // Some config
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    io.Fonts->AddFontFromMemoryCompressedTTF(fontData, fontDataSize, fontSize);

    // Build texture atlas
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsAlpha8(&pixels, &width, &height);

    // Upload texture to graphics system
    glBindTexture(GL_TEXTURE_2D, _fontTextureId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, pixels);
    io.Fonts->TexID = (void *)(intptr_t)_fontTextureId; // Store our identifier
    GL_CHECK();
}


bool
vwPlatform::redraw(void)
{
    // Filter out some redraw based on the dirtyness of the state.
    // Due to imgui which requires several frames to handle user events properly, we batch the display.
    // Also we do a "bounce" after a delay, which is needed for some tooltip to appear, even if no user event occurs.
    bsUs_t currentTimeUs = bsGetClockUs();
    u64 tmp = _dirtyRedrawCount.load();
    int dirtyRedrawCount = (int)(tmp&0xFFFFFFFF);
    int bounceCount      = (int)((tmp>>32)&0xFFFFFFFF);
    if(dirtyRedrawCount<=0 &&  // Not a dirty display   or...
       !(bounceCount==1 && currentTimeUs-_lastRenderingTimeUs>=BOUNCE_RENDER_GAP_US)) { // not (we need a bounce and waited enough)
        return false; // Nothing to display
    }
#define WRITE_DIRTY_COUNT(drc,bc) _dirtyRedrawCount.store((((u64)bc)<<32) | ((u32)(drc&0xFFFFFFFF)))
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
        installFont(vwGetFontDataRobotoMedium(), vwGetFontDataSizeRobotoMedium(), _newFontSizeToInstall);
        _newFontSizeToInstall = -1;
    }

    // Update inputs for ImGui
    plScope("vwPlatform::redraw");
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(_displayWidth, _displayHeight);
    io.DeltaTime = (_lastRenderingTimeUs==0)? 0.001 : 0.000001*(double)(currentTimeUs-_lastRenderingTimeUs);
    _lastRenderingTimeUs = currentTimeUs;

    // Compute the vertices
    _main->beforeDraw(_doExit.load());
    ImGui::NewFrame();
    _main->draw();
    ImGui::Render();
    _lastUpdateDurationUs = bsGetClockUs()-currentTimeUs;

    // Open GL calls to draw GUI
    // =========================

    ImDrawData* drawData = ImGui::GetDrawData();
    plAssert(drawData);
    int frameBufferWidth  = (int)(drawData->DisplaySize.x * drawData->FramebufferScale.x);
    int frameBufferHeight = (int)(drawData->DisplaySize.y * drawData->FramebufferScale.y);
    if(frameBufferWidth==0 || frameBufferHeight==0) return false;
    plScope("OpenGL engine");
    plVar(frameBufferWidth, frameBufferHeight);

    // Backup GL state
    //vwGlBackupState backup;
    // Setup render state: alpha-blending enabled, no face culling, no depth testing, scissor enabled, polygon fill
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    GL_CHECK();
    // Clear screen
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
    GL_CHECK();

    // Setup viewport, orthographic projection matrix
    glViewport(0, 0, (GLsizei)frameBufferWidth, (GLsizei)frameBufferHeight);
    const float ortho_projection[4][4] = {
        { 2.0f/io.DisplaySize.x, 0.0f,                   0.0f, 0.0f },
        { 0.0f,                  2.0f/-io.DisplaySize.y, 0.0f, 0.0f },
        { 0.0f,                  0.0f,                  -1.0f, 0.0f },
        {-1.0f,                  1.0f,                   0.0f, 1.0f },
    };
    glUseProgram(_guiGlProgram.getId());
    glUniform1i(_unifAttribLocationTex, 0);
    glUniformMatrix4fv(_unifAttribLocationProjMtx, 1, GL_FALSE, &ortho_projection[0][0]);
    glBindVertexArray(_guiGlProgram.getVaoId());
    glBindSampler(0, 0); // Rely on combined texture/sampler state.
    GL_CHECK();

    // Will project scissor/clipping rectangles into framebuffer space
    ImVec2 clipOff   = drawData->DisplayPos;       // (0,0) unless using multi-viewports
    ImVec2 clipScale = drawData->FramebufferScale; // (1,1) unless using retina display which are often (2,2)

    glBindBuffer(GL_ARRAY_BUFFER, _guiGlProgram.getVboId());
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _guiGlProgram.getIboId());

    for(int n=0; n<drawData->CmdListsCount; ++n) {
        plScope("ImGui list");
        const ImDrawList* cmdList = drawData->CmdLists[n];
        const ImDrawIdx* indexBufferOffset = 0;

        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)cmdList->VtxBuffer.Size * sizeof(ImDrawVert),
                     (const GLvoid*)cmdList->VtxBuffer.Data, GL_STREAM_DRAW);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)cmdList->IdxBuffer.Size * sizeof(ImDrawIdx),
                     (const GLvoid*)cmdList->IdxBuffer.Data, GL_STREAM_DRAW);

        for(int cmdIdx = 0; cmdIdx<cmdList->CmdBuffer.Size; ++cmdIdx) {
            const ImDrawCmd* cmd = &cmdList->CmdBuffer[cmdIdx];
            if(cmd->UserCallback) {
                plScope("GL user callback");
                cmd->UserCallback(cmdList, cmd);
            }
            else {
                plScope("GL draw command");
                plData("elements", cmd->ElemCount);
                // Project scissor/clipping rectangles into framebuffer space
                ImVec4 clipRect;
                clipRect.x = (cmd->ClipRect.x-clipOff.x)*clipScale.x;
                clipRect.y = (cmd->ClipRect.y-clipOff.y)*clipScale.y;
                clipRect.z = (cmd->ClipRect.z-clipOff.x)*clipScale.x;
                clipRect.w = (cmd->ClipRect.w-clipOff.y)*clipScale.y;
                if(clipRect.x<frameBufferWidth && clipRect.y<frameBufferHeight && clipRect.z>=0.0f && clipRect.w>=0.0f) {
                    // Apply scissor/clipping rectangle
                    glScissor((int)cmd->ClipRect.x, (int)(frameBufferHeight-cmd->ClipRect.w),
                              (int)(cmd->ClipRect.z-cmd->ClipRect.x), (int)(cmd->ClipRect.w-cmd->ClipRect.y));
                    // Bind texture, Draw
                    glBindTexture(GL_TEXTURE_2D, (GLuint)(intptr_t)cmd->TextureId);
                    glDrawElements(GL_TRIANGLES, (GLsizei)cmd->ElemCount, GL_UNSIGNED_INT, indexBufferOffset);
                }
            }
            indexBufferOffset += cmd->ElemCount;
        }
    }

    // Restore
    glDisable(GL_SCISSOR_TEST);
    GL_CHECK();
    //backup.restore();

    // We drew something
    return true;
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
}


void
vwPlatform::eventKeyPressed(bsKeycode keycode, bsKeyModState kms)
{
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
    ImGuiIO& io = ImGui::GetIO();
    io.MouseDown[buttonId-1] = true;
    io.MousePos = ImVec2((float)x, (float)y);
    _lastMouseMoveTimeUs = bsGetClockUs();
    notifyDrawDirty();
}


void
vwPlatform::eventButtonReleased(int buttonId, int x, int y, bsKeyModState kms)
{
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
