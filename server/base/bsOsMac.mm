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

#ifdef __APPLE__

#ifndef BS_NO_GRAPHIC
#define GL_SILENCE_DEPRECATION
#import <AppKit/AppKit.h>
#endif

// System
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>

// Internal
#include "bs.h"
#include "bsOs.h"

#ifndef BS_NO_GRAPHIC
#include "bsOsGlMac.h"
#include "bsKeycode.h"
#endif  // ifndef BS_NO_GRAPHIC

#include "bsString.h"
#include "bsTime.h"


#ifndef BS_NO_GRAPHIC

// Originally from Carbon's Event.h. Copied from https://github.com/itfrombit/osx_handmade/blob/85e9828512d0e87dfe762494e30e4e800a930d47/code/osx_handmade_events.h

/*
 *  Summary:
 *    Virtual keycodes
 *
 *  Discussion:
 *    These constants are the virtual keycodes defined originally in
 *    Inside Mac Volume V, pg. V-191. They identify physical keys on a
 *    keyboard. Those constants with "ANSI" in the name are labeled
 *    according to the key position on an ANSI-standard US keyboard.
 *    For example, kVK_ANSI_A indicates the virtual keycode for the key
 *    with the letter 'A' in the US keyboard layout. Other keyboard
 *    layouts may have the 'A' key label on a different physical key;
 *    in this case, pressing 'A' will generate a different virtual
 *    keycode.
 */
enum {
  kVK_ANSI_A                    = 0x00,
  kVK_ANSI_S                    = 0x01,
  kVK_ANSI_D                    = 0x02,
  kVK_ANSI_F                    = 0x03,
  kVK_ANSI_H                    = 0x04,
  kVK_ANSI_G                    = 0x05,
  kVK_ANSI_Z                    = 0x06,
  kVK_ANSI_X                    = 0x07,
  kVK_ANSI_C                    = 0x08,
  kVK_ANSI_V                    = 0x09,
  kVK_ANSI_B                    = 0x0B,
  kVK_ANSI_Q                    = 0x0C,
  kVK_ANSI_W                    = 0x0D,
  kVK_ANSI_E                    = 0x0E,
  kVK_ANSI_R                    = 0x0F,
  kVK_ANSI_Y                    = 0x10,
  kVK_ANSI_T                    = 0x11,
  kVK_ANSI_1                    = 0x12,
  kVK_ANSI_2                    = 0x13,
  kVK_ANSI_3                    = 0x14,
  kVK_ANSI_4                    = 0x15,
  kVK_ANSI_6                    = 0x16,
  kVK_ANSI_5                    = 0x17,
  kVK_ANSI_Equal                = 0x18,
  kVK_ANSI_9                    = 0x19,
  kVK_ANSI_7                    = 0x1A,
  kVK_ANSI_Minus                = 0x1B,
  kVK_ANSI_8                    = 0x1C,
  kVK_ANSI_0                    = 0x1D,
  kVK_ANSI_RightBracket         = 0x1E,
  kVK_ANSI_O                    = 0x1F,
  kVK_ANSI_U                    = 0x20,
  kVK_ANSI_LeftBracket          = 0x21,
  kVK_ANSI_I                    = 0x22,
  kVK_ANSI_P                    = 0x23,
  kVK_ANSI_L                    = 0x25,
  kVK_ANSI_J                    = 0x26,
  kVK_ANSI_Quote                = 0x27,
  kVK_ANSI_K                    = 0x28,
  kVK_ANSI_Semicolon            = 0x29,
  kVK_ANSI_Backslash            = 0x2A,
  kVK_ANSI_Comma                = 0x2B,
  kVK_ANSI_Slash                = 0x2C,
  kVK_ANSI_N                    = 0x2D,
  kVK_ANSI_M                    = 0x2E,
  kVK_ANSI_Period               = 0x2F,
  kVK_ANSI_Grave                = 0x32,
  kVK_ANSI_KeypadDecimal        = 0x41,
  kVK_ANSI_KeypadMultiply       = 0x43,
  kVK_ANSI_KeypadPlus           = 0x45,
  kVK_ANSI_KeypadClear          = 0x47,
  kVK_ANSI_KeypadDivide         = 0x4B,
  kVK_ANSI_KeypadEnter          = 0x4C,
  kVK_ANSI_KeypadMinus          = 0x4E,
  kVK_ANSI_KeypadEquals         = 0x51,
  kVK_ANSI_Keypad0              = 0x52,
  kVK_ANSI_Keypad1              = 0x53,
  kVK_ANSI_Keypad2              = 0x54,
  kVK_ANSI_Keypad3              = 0x55,
  kVK_ANSI_Keypad4              = 0x56,
  kVK_ANSI_Keypad5              = 0x57,
  kVK_ANSI_Keypad6              = 0x58,
  kVK_ANSI_Keypad7              = 0x59,
  kVK_ANSI_Keypad8              = 0x5B,
  kVK_ANSI_Keypad9              = 0x5C
};

/* keycodes for keys that are independent of keyboard layout*/
enum {
  kVK_Return                    = 0x24,
  kVK_Tab                       = 0x30,
  kVK_Space                     = 0x31,
  kVK_Delete                    = 0x33,
  kVK_Escape                    = 0x35,
  kVK_Command                   = 0x37,
  kVK_Shift                     = 0x38,
  kVK_CapsLock                  = 0x39,
  kVK_Option                    = 0x3A,
  kVK_Alternate                 = 0x3A,
  kVK_Control                   = 0x3B,
  kVK_RightCommand              = 0x36,
  kVK_RightShift                = 0x3C,
  kVK_RightOption               = 0x3D,
  kVK_RightAlternate            = 0x3D,
  kVK_RightControl              = 0x3E,
  kVK_Function                  = 0x3F,
  kVK_F17                       = 0x40,
  kVK_VolumeUp                  = 0x48,
  kVK_VolumeDown                = 0x49,
  kVK_Mute                      = 0x4A,
  kVK_F18                       = 0x4F,
  kVK_F19                       = 0x50,
  kVK_F20                       = 0x5A,
  kVK_F5                        = 0x60,
  kVK_F6                        = 0x61,
  kVK_F7                        = 0x62,
  kVK_F3                        = 0x63,
  kVK_F8                        = 0x64,
  kVK_F9                        = 0x65,
  kVK_F11                       = 0x67,
  kVK_F13                       = 0x69,
  kVK_F16                       = 0x6A,
  kVK_F14                       = 0x6B,
  kVK_F10                       = 0x6D,
  kVK_F12                       = 0x6F,
  kVK_F15                       = 0x71,
  kVK_Help                      = 0x72,
  kVK_Home                      = 0x73,
  kVK_PageUp                    = 0x74,
  kVK_ForwardDelete             = 0x75,
  kVK_F4                        = 0x76,
  kVK_End                       = 0x77,
  kVK_F2                        = 0x78,
  kVK_PageDown                  = 0x79,
  kVK_F1                        = 0x7A,
  kVK_LeftArrow                 = 0x7B,
  kVK_RightArrow                = 0x7C,
  kVK_DownArrow                 = 0x7D,
  kVK_UpArrow                   = 0x7E
};

// Global graphical context for Cocoa
// ==================================

static struct {
    // Render context
    NSOpenGLContext* openGLContext = 0;
    NSWindow* windowHandle;
    
    int         windowWidth  = -1;
    int         windowHeight = -1;
    
    bsKeycode kc;
    bsKeyModState kms;

    NSMutableAttributedString* markedText;

    // Others
    bsString appPath;
    // Application
    bsOsHandler* osHandler = 0;
} gGlob;

static struct {
    ClipboardType ownedType = ClipboardType::NONE;
    bsString      ownedData;
    bool          isReqReceived = false;
    bsStringUtf16 reqData;
} gClip;


// Constant for empty ranges in NSTextInputClient
static const NSRange kEmptyRange = { NSNotFound, 0 };

@interface OpenGLInputView : NSOpenGLView<NSTextInputClient>
@end

@implementation OpenGLInputView

-(BOOL)acceptsFirstResponder{
    return TRUE;
}

- (BOOL)canBecomeKeyView
{
    return YES;
}

- (void)keyDown:(NSEvent*)event {
    // We need to do this or we don't get insertText
    [self interpretKeyEvents:@[event]];
}

- (nullable NSAttributedString *)attributedSubstringForProposedRange:(NSRange)range actualRange:(nullable NSRangePointer)actualRange {
    return nil;
}

- (NSUInteger)characterIndexForPoint:(NSPoint)point {
    return 0;
}

- (void)doCommandBySelector:(nonnull SEL)selector {
}

- (NSRect)firstRectForCharacterRange:(NSRange)range actualRange:(nullable NSRangePointer)actualRange {
    return NSMakeRect(0.0, 0.0, 0.0, 0.0);
}

- (BOOL)hasMarkedText {
    return [gGlob.markedText length] > 0;
}

- (void)insertText:(nonnull id)string replacementRange:(NSRange)replacementRange {
    NSString* characters;

    if ([string isKindOfClass:[NSAttributedString class]]) {
        characters = [string string];
    } else {
        characters = (NSString*)string;
    }

    NSRange range = NSMakeRange(0, [characters length]);
    while (range.length) {
        uint16_t codepoint = 0;

        if ([characters getBytes:&codepoint
                       maxLength:sizeof(codepoint)
                      usedLength:NULL
                        encoding:NSUTF16StringEncoding
                         options:0
                           range:range
                  remainingRange:&range]) {
            if (codepoint!=0 && bsIsUnicodeDisplayable(codepoint)) { gGlob.osHandler->eventChar(codepoint); }
        }
    }
}

- (NSRange)markedRange {
    if ([gGlob.markedText length] > 0) {
        return NSMakeRange(0, [gGlob.markedText length] - 1);
    } else {
        return kEmptyRange;
    }
}

- (NSRange)selectedRange {
    return kEmptyRange;
}

- (void)setMarkedText:(nonnull id)string selectedRange:(NSRange)selectedRange replacementRange:(NSRange)replacementRange {
    [gGlob.markedText release];
    if ([string isKindOfClass:[NSAttributedString class]]) {
        gGlob.markedText = [[NSMutableAttributedString alloc] initWithAttributedString:string];
    } else {
        gGlob.markedText = [[NSMutableAttributedString alloc] initWithString:string];
    }
}

- (void)unmarkText {
    [[gGlob.markedText mutableString] setString:@""];
}

- (nonnull NSArray<NSAttributedStringKey> *)validAttributesForMarkedText {
    // No attributes
    return [NSArray array];
}
@end


@interface WindowDelegate : NSObject<NSApplicationDelegate, NSWindowDelegate>
@end

@implementation WindowDelegate
- (void)applicationDidFinishLaunching:(NSNotification *)notification {
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender{
    return YES;
}

- (void)applicationWillTerminate:(NSNotification *)notification {
}


-(void)windowDidResize:(NSNotification *)notification {
    // Unlike willResize or willZoom, this gets called after any resize for any reason.
    NSRect windowRect = [gGlob.windowHandle frame];
    NSRect contentRect = [gGlob.windowHandle contentRectForFrameRect:windowRect];

    gGlob.windowWidth  = (int)contentRect.size.width;
    gGlob.windowHeight = (int)contentRect.size.height;

    if (gGlob.osHandler) {
        gGlob.osHandler->notifyWindowSize(gGlob.windowWidth * 2, gGlob.windowHeight * 2);
    }
}

-(void)windowDidBecomeKey:(NSNotification *)notification {
    if (!gGlob.osHandler) {
        return;
    }
    if(!gGlob.osHandler->isVisible()) {
        gGlob.osHandler->notifyMapped();
    }
    gGlob.osHandler->notifyEnter(gGlob.kms);
}

-(void)windowDidResignKey:(NSNotification *)notification {
    if (!gGlob.osHandler) {
        return;
    }
    if(gGlob.osHandler->isVisible()) {
        osHideWindow();
        gGlob.osHandler->notifyUnmapped();
    }
    gGlob.osHandler->notifyLeave(gGlob.kms);
}

- (void)windowWillClose:(NSNotification *)notification {
    if (!gGlob.osHandler) {
        return;
    }

    gGlob.osHandler->quit();
}
@end


// Window creation recipe (collection of pieces from internet)
// ===========================================================

void
osCreateWindow(const char* windowTitle, const char* configName, float ratioLeft, float ratioTop, float ratioRight, float ratioBottom, bool overrideWindowManager)
{
    plAssert(ratioLeft>=0.   && ratioLeft<=1.);
    plAssert(ratioTop>=0.    && ratioTop<=1.);
    plAssert(ratioRight>=0.  && ratioRight<=1.);
    plAssert(ratioBottom>=0. && ratioBottom<=1.);
    plAssert(ratioLeft<ratioRight);
    plAssert(ratioTop<ratioBottom);

    NSApplication *app = [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    
    // @#TODO Find out why this menu isn't interactive while the window has focus
    { // Menu
        NSMenu* mainMenu = [NSMenu new];

        NSMenuItem* appMenuItem = [NSMenuItem new];
        [mainMenu addItem:appMenuItem];

        NSMenu* appMenu = [NSMenu new];

        [appMenu addItem:[[NSMenuItem alloc] initWithTitle:@"Enter Full Screen"
                                                     action:@selector(toggleFullScreen:)
                                              keyEquivalent:@"f"]];

        [appMenu addItem:[[NSMenuItem alloc] initWithTitle:@"Quit Palanteer"
                                                    action:@selector(terminate:)
                                             keyEquivalent:@"q"]];
        [appMenuItem setSubmenu:appMenu];

        [app setMainMenu:mainMenu];
    }
    
    WindowDelegate *windowDelegate = [[WindowDelegate alloc] init];
    [app setDelegate:windowDelegate];
    [NSApp finishLaunching];
    
    NSRect screenRect = [[NSScreen mainScreen] frame];
    float dWidth  = screenRect.size.width;
    float dHeight = screenRect.size.height;
    int x = (int)(ratioLeft *(float)dWidth);
    int y = (int)(ratioTop  *(float)dHeight);
    int width  = (int)((ratioRight-ratioLeft) * dWidth);
    int height = (int)((ratioBottom-ratioTop) * dHeight);
    NSRect initialWindowRect = NSMakeRect(x, y, width, height);
    gGlob.windowWidth  = width;
    gGlob.windowHeight = height;

    NSOpenGLPixelFormatAttribute pixelFormatAttributes[] ={
        NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion3_2Core,
        NSOpenGLPFAColorSize    , 32                           ,
        NSOpenGLPFAAlphaSize    , 8                            ,
        NSOpenGLPFADepthSize    , 24                           ,
        NSOpenGLPFADoubleBuffer ,
        NSOpenGLPFAAccelerated  ,
        NSOpenGLPFANoRecovery   ,
        0
    };
    NSOpenGLPixelFormat* format = [[NSOpenGLPixelFormat alloc]initWithAttributes:pixelFormatAttributes];

    NSWindow* window = [[NSWindow alloc]
                        initWithContentRect:initialWindowRect
                        styleMask: NSWindowStyleMaskTitled
                        | NSWindowStyleMaskClosable
                        | NSWindowStyleMaskMiniaturizable
                        | NSWindowStyleMaskResizable
                        backing:NSBackingStoreBuffered
                        defer:NO];
    
    NSRect glRect = window.contentView.bounds;
    OpenGLInputView *glView = [[OpenGLInputView alloc]initWithFrame:glRect pixelFormat:format];

    gGlob.windowHandle = window;
    gGlob.openGLContext = [glView openGLContext];
    [gGlob.openGLContext makeCurrentContext];

    [window setContentView:glView];
    [glView prepareOpenGL];

    [window setDelegate:windowDelegate];
    [window setTitle:@(windowTitle)];
    [window makeFirstResponder:glView];
        
    [NSApp activateIgnoringOtherApps:YES];
    [window makeKeyAndOrderFront:nil];
    
    // Get the application path. We use "~/.palanteer" for now
    // @#TODO "technically" we should probably use ~/Library/Application Support
    //        and/or ~/Library/Preferences depending on the data.
    gGlob.appPath = getenv("HOME")? getenv("HOME") : ".";
    gGlob.appPath += bsString("/.")+configName;
    if(!osDirectoryExists(gGlob.appPath.toChar())) {
        if(mkdir(gGlob.appPath.toChar(), S_IRWXU)<0) {
            printf("Error: Unable to create the folder %s\n", gGlob.appPath.toChar());
        }
    }
}


// OS interactions
// ===============

void
osGetWindowSize(int& width, int& height)
{
    float scaleFactor = [gGlob.windowHandle backingScaleFactor];
    width  = gGlob.windowWidth * scaleFactor;
    height = gGlob.windowHeight * scaleFactor;
}


void
osDestroyWindow(void)
{
    // @#TODO Implement this when needed
}


void
osSwapBuffer(void)
{
    [gGlob.openGLContext flushBuffer];
}


void
osHideWindow(void)
{
    [gGlob.windowHandle miniaturize:nil];
}

void
osShowWindow(void)
{
    [gGlob.windowHandle deminiaturize:nil];
}


void
osSetIcon(int width, int height, const u8* pixels)
{
   // Windows don't really have icons on MacOS. Do nothing.
}


void
osSetWindowTitle(const bsString& title)
{
    NSString *windowTitle = @(title.toChar());
    [gGlob.windowHandle setTitle:windowTitle];
}


bool
osIsMouseVisible(void)
{
    // @#TODO Implement this when needed
    return true;
}


void
osSetMouseVisible(bool state)
{
    // @#TODO Implement this when needed
    if(state==osIsMouseVisible()) return;
}


bsString
osGetProgramDataPath(void)
{
    return gGlob.appPath;
}


void
osPushToClipboard(ClipboardType pushType, const bsStringUtf16& data)
{
    [[NSPasteboard generalPasteboard] clearContents];
    [[NSPasteboard generalPasteboard] setString:@(data.toUtf8().toChar()) forType:NSPasteboardTypeString];
}


bsStringUtf16
osReqFromClipboard(ClipboardType reqType)
{
    gClip.reqData.clear();
    NSString* clipboardData = [[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString];
    if (clipboardData != nil) {
        NSUInteger byteCount = [clipboardData lengthOfBytesUsingEncoding:NSUTF16StringEncoding];
        uint16_t* chars = (uint16_t*)malloc(byteCount);
        if (chars) {
            [clipboardData
             getBytes:chars
             maxLength:byteCount
             usedLength:NULL
             encoding:NSUTF16StringEncoding
             options:0
             range:NSMakeRange(0, [clipboardData length])
             remainingRange:NULL];
            uint16_t* ptr = chars;
            while (byteCount) {
                gClip.reqData.push_back(*ptr++);
                byteCount -= 2; // Since we're going 2 bytes at a time
            }
            free(chars);
        }
    }
    
    return gClip.reqData;
}

static bsKeycode
keysymToKeycode(unsigned short symbol)
{
    // @#TODO Fix the mappings with ???
    switch (symbol) {
    case kVK_Shift:               return KC_LShift;
    case kVK_RightShift:          return KC_RShift;
    case kVK_Control:             return KC_LControl;
    case kVK_RightControl:        return KC_RControl;
    case kVK_Alternate:           return KC_LAlt;
    case kVK_RightAlternate:      return KC_RAlt;
    case kVK_Command:             return KC_LSystem;
    case kVK_RightCommand:        return KC_RSystem;
    // case kVK_Option:              return KC_Menu; //???
    case kVK_Escape:              return KC_Escape;
    case kVK_ANSI_Semicolon:      return KC_Semicolon;
    case kVK_ANSI_Slash:          return KC_Slash;
    case kVK_ANSI_Equal:          return KC_Equal;
    case kVK_ANSI_Minus:          return KC_Hyphen;
    case kVK_ANSI_LeftBracket:    return KC_LBracket;
    case kVK_ANSI_RightBracket:   return KC_RBracket;
    case kVK_ANSI_Comma:          return KC_Comma;
    case kVK_ANSI_Period:         return KC_Period;
    case kVK_ANSI_Quote:          return KC_Quote;
    case kVK_ANSI_Backslash:      return KC_Backslash;
    case kVK_ANSI_Grave:          return KC_Tilde;
    case kVK_Space:               return KC_Space;
    case kVK_Return:              return KC_Enter;
    case kVK_ANSI_KeypadEnter:    return KC_Enter;
    case kVK_Delete:              return KC_Backspace;
    case kVK_Tab:                 return KC_Tab;
    case kVK_PageUp:              return KC_PageUp;
    case kVK_PageDown:            return KC_PageDown;
    case kVK_End:                 return KC_End;
    case kVK_Home:                return KC_Home;
    case kVK_Help:                return KC_Insert; // ???
    case kVK_ForwardDelete:       return KC_Delete;
    case kVK_ANSI_KeypadPlus:     return KC_Add;
    case kVK_ANSI_KeypadMinus:    return KC_Subtract;
    case kVK_ANSI_KeypadMultiply: return KC_Multiply;
    case kVK_ANSI_KeypadDivide:   return KC_Divide;
    case kVK_Mute:                return KC_Pause; // ???
    case kVK_F1:                  return KC_F1;
    case kVK_F2:                  return KC_F2;
    case kVK_F3:                  return KC_F3;
    case kVK_F4:                  return KC_F4;
    case kVK_F5:                  return KC_F5;
    case kVK_F6:                  return KC_F6;
    case kVK_F7:                  return KC_F7;
    case kVK_F8:                  return KC_F8;
    case kVK_F9:                  return KC_F9;
    case kVK_F10:                 return KC_F10;
    case kVK_F11:                 return KC_F11;
    case kVK_F12:                 return KC_F12;
    case kVK_F13:                 return KC_F13;
    case kVK_F14:                 return KC_F14;
    case kVK_F15:                 return KC_F15;
    case kVK_LeftArrow:           return KC_Left;
    case kVK_RightArrow:          return KC_Right;
    case kVK_UpArrow:             return KC_Up;
    case kVK_DownArrow:           return KC_Down;
    case kVK_ANSI_Keypad0:        return KC_Numpad0;
    case kVK_ANSI_Keypad1:        return KC_Numpad1;
    case kVK_ANSI_Keypad2:        return KC_Numpad2;
    case kVK_ANSI_Keypad3:        return KC_Numpad3;
    case kVK_ANSI_Keypad4:        return KC_Numpad4;
    case kVK_ANSI_Keypad5:        return KC_Numpad5;
    case kVK_ANSI_Keypad6:        return KC_Numpad6;
    case kVK_ANSI_Keypad7:        return KC_Numpad7;
    case kVK_ANSI_Keypad8:        return KC_Numpad8;
    case kVK_ANSI_Keypad9:        return KC_Numpad9;
    case kVK_ANSI_A:              return KC_A;
    case kVK_ANSI_B:              return KC_B;
    case kVK_ANSI_C:              return KC_C;
    case kVK_ANSI_D:              return KC_D;
    case kVK_ANSI_E:              return KC_E;
    case kVK_ANSI_F:              return KC_F;
    case kVK_ANSI_G:              return KC_G;
    case kVK_ANSI_H:              return KC_H;
    case kVK_ANSI_I:              return KC_I;
    case kVK_ANSI_J:              return KC_J;
    case kVK_ANSI_K:              return KC_K;
    case kVK_ANSI_L:              return KC_L;
    case kVK_ANSI_M:              return KC_M;
    case kVK_ANSI_N:              return KC_N;
    case kVK_ANSI_O:              return KC_O;
    case kVK_ANSI_P:              return KC_P;
    case kVK_ANSI_Q:              return KC_Q;
    case kVK_ANSI_R:              return KC_R;
    case kVK_ANSI_S:              return KC_S;
    case kVK_ANSI_T:              return KC_T;
    case kVK_ANSI_U:              return KC_U;
    case kVK_ANSI_V:              return KC_V;
    case kVK_ANSI_W:              return KC_W;
    case kVK_ANSI_X:              return KC_X;
    case kVK_ANSI_Y:              return KC_Y;
    case kVK_ANSI_Z:              return KC_Z;
    case kVK_ANSI_0:              return KC_Num0;
    case kVK_ANSI_1:              return KC_Num1;
    case kVK_ANSI_2:              return KC_Num2;
    case kVK_ANSI_3:              return KC_Num3;
    case kVK_ANSI_4:              return KC_Num4;
    case kVK_ANSI_5:              return KC_Num5;
    case kVK_ANSI_6:              return KC_Num6;
    case kVK_ANSI_7:              return KC_Num7;
    case kVK_ANSI_8:              return KC_Num8;
    case kVK_ANSI_9:              return KC_Num9;
    }

    return KC_Unknown;
}

static bsKeyModState
modifierFlagsToModState(NSEventModifierFlags flags) {
    return { (bool)(flags&NSEventModifierFlagShift), (bool)(flags&NSEventModifierFlagControl), (bool)(flags&NSEventModifierFlagOption), (bool)(flags&NSEventModifierFlagCommand) };
}

void
osProcessInputs(bsOsHandler* handler)
{
    bsKeycode kc;
    bsKeyModState kms;
    NSEvent* event;
    NSPoint loc;
    
    gGlob.osHandler = handler;
    do {
        event = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:nil inMode:NSDefaultRunLoopMode dequeue:YES];
        
        switch ([event type]) {
            case NSEventTypeKeyDown:
                kms = modifierFlagsToModState([event modifierFlags]);
                kc = keysymToKeycode([event keyCode]);
                gGlob.osHandler->eventKeyPressed(kc, kms);
                gGlob.kms = kms;
                break;
            case NSEventTypeKeyUp:
                kms = modifierFlagsToModState([event modifierFlags]);
                kc = keysymToKeycode([event keyCode]);
                gGlob.osHandler->eventKeyReleased(kc, kms);
                gGlob.kms = kms;
                break;
            case NSEventTypeMouseMoved:
            case NSEventTypeLeftMouseDragged:
            case NSEventTypeRightMouseDragged:
            case NSEventTypeOtherMouseDragged:
                loc = [event locationInWindow];
                gGlob.osHandler->eventMouseMotion((int)loc.x*2, (gGlob.windowHeight - (int)loc.y)*2);
                break;
            case NSEventTypeLeftMouseDown:
                kms = modifierFlagsToModState([event modifierFlags]);
                loc = [event locationInWindow];
                gGlob.osHandler->eventButtonPressed(1, (int)loc.x*2, (gGlob.windowHeight - (int)loc.y)*2, kms);
                gGlob.kms = kms;
                break;
            case NSEventTypeRightMouseDown:
                kms = modifierFlagsToModState([event modifierFlags]);
                loc = [event locationInWindow];
                gGlob.osHandler->eventButtonPressed(3, (int)loc.x*2, (gGlob.windowHeight - (int)loc.y)*2, kms);
                gGlob.kms = kms;
                break;
            case NSEventTypeOtherMouseDown: {
                if ([event buttonNumber] == 2) {
                    kms = modifierFlagsToModState([event modifierFlags]);
                    loc = [event locationInWindow];
                    gGlob.osHandler->eventButtonPressed(2, (int)loc.x*2, (gGlob.windowHeight - (int)loc.y)*2, kms);
                    gGlob.kms = kms;
                }
                break;
            }
            case NSEventTypeLeftMouseUp:
                kms = modifierFlagsToModState([event modifierFlags]);
                loc = [event locationInWindow];
                gGlob.osHandler->eventButtonReleased(1, (int)loc.x*2, (gGlob.windowHeight - (int)loc.y)*2, kms);
                gGlob.kms = kms;
                break;
            case NSEventTypeRightMouseUp:
                kms = modifierFlagsToModState([event modifierFlags]);
                loc = [event locationInWindow];
                gGlob.osHandler->eventButtonReleased(3, (int)loc.x*2, (gGlob.windowHeight - (int)loc.y)*2, kms);
                gGlob.kms = kms;
                break;
            case NSEventTypeOtherMouseUp: {
                if ([event buttonNumber] == 2) {
                    kms = modifierFlagsToModState([event modifierFlags]);
                    loc = [event locationInWindow];
                    gGlob.osHandler->eventButtonReleased(2, (int)loc.x*2, (gGlob.windowHeight - (int)loc.y)*2, kms);
                    gGlob.kms = kms;
                }
                break;
            }
            case NSEventTypeScrollWheel:
                kms = modifierFlagsToModState([event modifierFlags]);
                loc = [event locationInWindow];
                gGlob.osHandler->eventWheelScrolled((int)loc.x, (int)loc.y, ([event scrollingDeltaY]<0)? +1:-1, kms);
                gGlob.kms = kms;
                break;
            case NSEventTypeFlagsChanged:
                kms = modifierFlagsToModState([event modifierFlags]);
                gGlob.osHandler->eventModifiersChanged(kms);
            default:
                break;
        }
        [NSApp sendEvent:event];
    } while (event != nil);
}


// Mac entry point
int
main(int argc, char* argv[])
{
    return bsBootstrap(argc, argv);
}

#endif // ifndef BS_NO_GRAPHIC



// Misc OS
// =======

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
    char cwd[PATH_MAX];
    if(getcwd(cwd, sizeof(cwd)) != NULL) return bsString(&cwd[0]);
    return "";
}


bool
osFileExists(const bsString& path)
{
    struct stat st;
    return stat(path.toChar(),&st)==0 && S_ISREG(st.st_mode);
}


bool
osDirectoryExists(const bsString& path)
{
    struct stat st;
    return stat(path.toChar(),&st)==0 && S_ISDIR(st.st_mode);
}


bsString
osGetDirname(const bsString& path)
{
    int idx = path.rfindChar('/');
    return (idx<0)? path : path.subString(0, idx);
}


bsString
osGetBasename(const bsString& path)
{
    int idx = path.rfindChar('/');
    return (idx<0)? path : path.subString(idx+1, path.size());
}


u32
osGetDriveBitmap(void)
{
    // No concept of "drive" in MacOS
    return 0;
}


FILE*
osFileOpen(const bsString& path, const char* mode)
{
    return fopen(path.toChar(), mode); // Utf-8 does not change the fopen prototype on Mac!
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
    buffer.resize(fileSize);
    if(fread(&buffer[0], 1, fileSize, fh)!=fileSize) {
        buffer.clear();
        fclose(fh);
        return false;
    }
    // Close and return
    fclose(fh);
    return true;
}

bsDirStatusCode
osMakeDir(const bsString& path)
{
    bsString tmp(path);
    if(!tmp.empty() && tmp.back()=='/') tmp.pop_back();
    if(tmp.empty()) return bsDirStatusCode::FAILURE;
    for(u8* p=&tmp[1]; *p; ++p) {
        if(*p=='/') {
            *p = 0;
            int status = mkdir(tmp.toChar(), S_IRWXU);
            if(status!=0 && errno!=EEXIST) {
                if(errno==EACCES) return bsDirStatusCode::PERMISSION_DENIED;
                return bsDirStatusCode::FAILURE;
            }
            *p = '/';
        }
    }
    int status = mkdir(tmp.toChar(), S_IRWXU);
    if(status==0)          return bsDirStatusCode::OK;
    else if(errno==EACCES) return bsDirStatusCode::PERMISSION_DENIED;
    else if(errno==EEXIST) return bsDirStatusCode::ALREADY_EXISTS;
    return bsDirStatusCode::FAILURE;
}


bsDirStatusCode
osGetDirContent(const bsString& path, bsVec<bsDirEntry>& entries)
{
    // Open directory
    entries.clear();
    DIR* dir = opendir(path.toChar());
    if(!dir) {
        if     (errno==EACCES)  return bsDirStatusCode::PERMISSION_DENIED;
        else if(errno==ENOTDIR) return bsDirStatusCode::NOT_A_DIRECTORY;
        else if(errno==ENOENT)  return bsDirStatusCode::DOES_NOT_EXIST;
        return bsDirStatusCode::FAILURE;
    }

    struct dirent* fe;
    while((fe=readdir(dir))) {
        if(fe->d_type==DT_REG || (fe->d_type==DT_DIR && strcmp(fe->d_name, ".")!=0 && strcmp(fe->d_name, "..")!=0)) {
            entries.push_back(bsDirEntry{fe->d_name, fe->d_type==DT_DIR});
        }
    }

    closedir(dir);
    return bsDirStatusCode::OK;
}


size_t
osGetSize(const bsString& path)
{
    // Get infos on this path
    struct stat statbuf;
    if(stat(path.toChar(), &statbuf)==-1) {
        return 0; // Return 0 in case of problem
    }

    // File case
    if(statbuf.st_mode&S_IFREG) {
        return statbuf.st_size;   // Return the size of the file
    }
    if(!(statbuf.st_mode&S_IFDIR)) {
        return 0; // Not a file nor a directory
    }

    // Directory case (recursive)
    size_t dirSize = 0;
    DIR* dir = opendir(path.toChar());
    if(!dir) return 0;
    struct dirent* fe;
    while((fe=readdir(dir))) {
        if(fe->d_type==DT_REG || (fe->d_type==DT_DIR && strcmp(fe->d_name, ".")!=0 && strcmp(fe->d_name, "..")!=0)) {
            dirSize += osGetSize((path+"/")+bsString(fe->d_name));
        }
    }
    closedir(dir);
    return dirSize;
}


bsDate
osGetCreationDate(const bsString& path)
{
    struct stat s;
    if(stat(path.toChar(), &s)==-1) return bsDate(); // If error, all fields are zero
    struct tm* t = localtime(&s.st_mtime);
    plAssert(t);
    return bsDate{1900+t->tm_year, 1+t->tm_mon, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec};
}


bsDirStatusCode
osRemoveFile(const bsString& path)
{
    if(unlink(path.toChar())==0) return bsDirStatusCode::OK;
    else if (errno==EACCES)      return bsDirStatusCode::PERMISSION_DENIED;
    else if(errno==ENOENT)       return bsDirStatusCode::DOES_NOT_EXIST;
    return bsDirStatusCode::FAILURE;
}


bsDirStatusCode
osRemoveDir(const bsString& path, bool onlyIfEmpty)
{
    // Open directory
    DIR* dir = opendir(path.toChar());
    if(!dir) {
        if     (errno==EACCES)  return bsDirStatusCode::PERMISSION_DENIED;
        else if(errno==ENOTDIR) return bsDirStatusCode::NOT_A_DIRECTORY;
        else if(errno==ENOENT)  return bsDirStatusCode::DOES_NOT_EXIST;
        return bsDirStatusCode::FAILURE;
    }
    // Empty it (non recursive)
    if(!onlyIfEmpty) {
        struct dirent* fe;
        while((fe=readdir(dir))) {
            if(fe->d_type==DT_REG || (fe->d_type==DT_DIR && strcmp(fe->d_name, ".")!=0 && strcmp(fe->d_name, "..")!=0)) {
                unlink(((path+"/")+fe->d_name).toChar());
            }
        }
    }
    closedir(dir);

    // Remove it
    if(rmdir(path.toChar())==0) return bsDirStatusCode::OK;
    else if(errno==EACCES)      return bsDirStatusCode::PERMISSION_DENIED;
    else if(errno==ENOTDIR)     return bsDirStatusCode::NOT_A_DIRECTORY;
    else if(errno==ENOENT)      return bsDirStatusCode::DOES_NOT_EXIST;
    return bsDirStatusCode::FAILURE;
}


#endif // __APPLE__
