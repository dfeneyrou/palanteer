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

#pragma once

#include <cstdio>

#include "bs.h"
#include "bsString.h"
#include "bsKeycode.h"

#ifndef BS_NO_GRAPHIC

// To be implemented in the main app
// =================================

// Main function
int bsBootstrap(int argc, char* argv[]);

// Event handler interface
class bsOsHandler {
public:
    bsOsHandler(void) = default;
    virtual ~bsOsHandler(void) { }
    // Events
    virtual void notifyWindowSize(int windowWidth, int windowHeight) = 0;
    virtual void notifyMapped(void) = 0;
    virtual void notifyUnmapped(void) = 0;
    virtual void notifyExposed(void) = 0;
    virtual void notifyFocusOut(void) = 0;
    virtual void notifyEnter(bsKeyModState kms) = 0;
    virtual void notifyLeave(bsKeyModState kms) = 0;
    virtual void eventChar(char16_t codepoint) = 0;
    virtual void eventKeyPressed (bsKeycode keycode, bsKeyModState kms) = 0;
    virtual void eventKeyReleased(bsKeycode keycode, bsKeyModState kms) = 0;
    virtual void eventButtonPressed (int buttonId, int x, int y, bsKeyModState kms) = 0; // 0=left, 1=middle, 2=right
    virtual void eventButtonReleased(int buttonId, int x, int y, bsKeyModState kms) = 0;
    virtual void eventMouseMotion   (int x, int y) = 0;
    virtual void eventWheelScrolled (int x, int y, int steps, bsKeyModState kms) = 0;
    virtual void eventModifiersChanged (bsKeyModState kms) = 0;
    // Others
    virtual bool isVisible(void) const = 0;
    virtual void quit(void) = 0;

};


// OS abstraction layer, to be used by applications
// ================================================

void osCreateWindow(const char* windowTitle, const char* configName,
                      float ratioLeft, float ratioTop, float ratioRight, float ratioBottom,
                      bool overrideWindowManager=false);
void osDestroyWindow(void);
void osSetWindowTitle(const bsString& title);
void osGetWindowSize(int& width, int& height);
void osProcessInputs(bsOsHandler* osHandler);
void osHideWindow(void);
void osShowWindow(void);
void osSwapBuffer(void);
void osSetMouseVisible(bool state);
bool osIsMouseVisible(void);

// Clipboard
enum ClipboardType { NONE, UTF8, APP_INTERNAL };
void          osPushToClipboard (ClipboardType pushType, const bsStringUtf16& data);
bsStringUtf16 osReqFromClipboard(ClipboardType reqType);

#endif // ifndef BS_NO_GRAPHIC

// File system
FILE*    osFileOpen(const bsString& path, const char* mode);
bsString osGetProgramDataPath(void); // UTF-8
bsString osGetCurrentPath(void);
bsString osGetBasename(const bsString& path);
bsString osGetDirname (const bsString& path);
enum bsDirStatusCode { OK, FAILURE, DOES_NOT_EXIST, NOT_A_DIRECTORY, PERMISSION_DENIED, ALREADY_EXISTS };
struct bsDirEntry {
    bsString name;
    bool isDir;
};

bsDate          osGetDate(void);
bsDirStatusCode osGetDirContent(const bsString& path, bsVec<bsDirEntry>& entries);
bsDirStatusCode osMakeDir(const bsString& path);
bool            osFileExists(const bsString& path);
bool            osDirectoryExists(const bsString& path);
size_t          osGetSize(const bsString& path);
bsDate          osGetCreationDate(const bsString& path);
bool            osLoadFileContent(const bsString& path, bsVec<u8>& buffer, int maxSize=-1);
bsDirStatusCode osRemoveFile(const bsString& path);
bsDirStatusCode osRemoveDir(const bsString& path, bool onlyIfEmpty=true);
u32             osGetDriveBitmap(void); // One bit per drive (bit 0 is A:, etc...). No bit set means no such concept
void            osSetIcon(int width, int height, const u8* pixels); // Size of pixels array is 4*width*height (RGBA)


// Some portability fixes
// ======================
#ifdef _WIN32               // Windows case

#define PL_DIR_SEP "\\"
#define PL_DIR_SEP_CHAR '\\'

#if !defined(strcasecmp)
#define strcasecmp _stricmp
#endif

// strcasestr does not exists on windows
const char* strcasestr(const char* s, const char* sToFind);

// ftell and fseek are limited to 32 bit offset, so the 64 bit version is used here
#define bsOsFseek _fseeki64
#define bsOsFtell _ftelli64

#else                       // Linux/Mac case

#define PL_DIR_SEP "/"
#define PL_DIR_SEP_CHAR '/'

// ftell and fseek support big files directly
#define bsOsFseek fseek
#define bsOsFtell ftell

#endif
