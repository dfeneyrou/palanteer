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

#ifdef __linux__

#ifndef BS_NO_GRAPHIC
#include <X11/Xatom.h>
#include <X11/extensions/Xrender.h>
#include <X11/Xutil.h>
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
#include "bsOsGlLnx.h"
#include "bsKeycode.h"
#endif  // ifndef BS_NO_GRAPHIC

#include "bsString.h"
#include "bsTime.h"


#ifndef BS_NO_GRAPHIC


// Global graphical context for X11
// ================================

static struct {
    // Render context
    Colormap    xCmap;
    Display*    xDisplay = 0;
    GLXContext  xRenderContext = 0;
    Window      windowHandle;
    GLXWindow   glXWindowHandle;
    Atom        deleteMessage;
    Cursor      noCursor      = None;
    Cursor      defaultCursor = None;
    Cursor      currentCursor = None;
    int         wWidth    = -1;
    int         wHeight   = -1;
    int         dpiWidth  = -1;
    int         dpiHeight = -1;
    // Character inputs
    XIM xInputMethod  = 0;  // Input method linked to the X display
    XIC xInputContext = 0;  // Input context used to gete unicode input in our window
    // Others
    bsString appPath;
    bool     isDirectOverride = false;
    // Application
    bsOsHandler* osHandler = 0;
} gGlob;

static struct {
    Atom aKind;
    Atom aUtf8;
    Atom aMyApp;
    Atom aMyProperty;
    Atom aTargets;
    ClipboardType ownedType = ClipboardType::NONE;
    bsString      ownedData;
    bool          isReqReceived = false;
    bsStringUtf16 reqData;
} gClip;



// Window creation recipe (collection of pieces from internet)
// ===========================================================

static int VisData[] = {
    GLX_RENDER_TYPE, GLX_RGBA_BIT,
    GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
    GLX_DOUBLEBUFFER, True,
    GLX_RED_SIZE, 8,
    GLX_GREEN_SIZE, 8,
    GLX_BLUE_SIZE, 8,
    GLX_ALPHA_SIZE, 8,
    GLX_DEPTH_SIZE, 16,
    None
};

static int
ctxErrorHandler(Display *dpy, XErrorEvent *ev)
{
    fprintf(stderr, "Error at context creation");
    return 0;
}

static Bool
WaitForMapNotify(Display *d, XEvent *e, char *arg)
{
    return d && e && arg && (e->type==MapNotify) && (e->xmap.window==*(Window*)arg);
}


void
osCreateWindow(const char* windowTitle, const char* configName, float ratioLeft, float ratioTop, float ratioRight, float ratioBottom, bool overrideWindowManager)
{
    plAssert(ratioLeft>=0.   && ratioLeft<=1.);
    plAssert(ratioTop>=0.    && ratioTop<=1.);
    plAssert(ratioRight>=0.  && ratioRight<=1.);
    plAssert(ratioBottom>=0. && ratioBottom<=1.);
    plAssert(ratioLeft<ratioRight);
    plAssert(ratioTop<ratioBottom);

    gGlob.isDirectOverride = overrideWindowManager;
    gGlob.xDisplay         = XOpenDisplay(NULL);
    plAssert(gGlob.xDisplay, "Unable to connect to X server");

    int    screen = DefaultScreen(gGlob.xDisplay);
    Window Xroot  = RootWindow(gGlob.xDisplay, screen);
    int         fbconfigQty;
    GLXFBConfig* fbconfigs = glXChooseFBConfig(gGlob.xDisplay, screen, VisData, &fbconfigQty);
    GLXFBConfig  fbconfig  = 0;
    XVisualInfo* visual    = 0;
    for(int i=0; i<fbconfigQty; ++i) {
        visual = (XVisualInfo*) glXGetVisualFromFBConfig(gGlob.xDisplay, fbconfigs[i]);
        if(!visual) continue;

        XRenderPictFormat* pict_format = XRenderFindVisualFormat(gGlob.xDisplay, visual->visual);
        if(pict_format && pict_format->direct.alphaMask==0) {  // Note: choose pict_format->direct.alphaMask>=0 for transparent background...
            fbconfig = fbconfigs[i];
            break;
        }
        XFree(visual);
    }
    XFree(fbconfigs);
    plAssert(fbconfig, "No matching GLX frame buffer config found");

    // Create a colormap
    gGlob.xCmap = XCreateColormap(gGlob.xDisplay, Xroot, visual->visual, AllocNone);

    XSetWindowAttributes attr = {0,};
    attr.colormap          = gGlob.xCmap;
    attr.background_pixmap = None;
    attr.border_pixmap     = None;
    attr.border_pixel      = 0;
    attr.override_redirect = gGlob.isDirectOverride;
    attr.event_mask = StructureNotifyMask | EnterWindowMask | LeaveWindowMask | ExposureMask  | ButtonPressMask | PointerMotionMask |
        ButtonReleaseMask | OwnerGrabButtonMask | FocusChangeMask | KeyPressMask | KeyReleaseMask;
    int attr_mask = CWBackPixmap | CWColormap | CWBorderPixel| CWEventMask | CWOverrideRedirect;
    int defaultScreenId = DefaultScreen(gGlob.xDisplay);
    int dWidth    = DisplayWidth   (gGlob.xDisplay, defaultScreenId); // In pixel
    int dHeight   = DisplayHeight  (gGlob.xDisplay, defaultScreenId);

    // Compute DPI
#define COMPUTE_DPI(pix, mm) (int)(9.6*((int)(((double)pix)/((double)mm)*(254./96.)+0.5)))  /* Quantize the DPI each 5%, to clean approximated values */
    int dWidthMm    = DisplayWidthMM (gGlob.xDisplay, defaultScreenId); // In millimeter
    int dHeightMm   = DisplayHeightMM(gGlob.xDisplay, defaultScreenId);
    gGlob.dpiWidth  = COMPUTE_DPI(dWidth, dWidthMm);
    gGlob.dpiHeight = COMPUTE_DPI(dHeight, dHeightMm);

    int x = (int)(ratioLeft *(float)dWidth);
    int y = (int)(ratioTop  *(float)dHeight);
    gGlob.wWidth    = (int)((ratioRight-ratioLeft)*(float)dWidth);
    gGlob.wHeight   = (int)((ratioBottom-ratioTop)*(float)dHeight);
    gGlob.windowHandle = XCreateWindow(gGlob.xDisplay, Xroot, x, y, gGlob.wWidth, gGlob.wHeight,
                                       0, visual->depth, InputOutput, visual->visual, attr_mask, &attr);
    gGlob.glXWindowHandle = gGlob.windowHandle;
    plAssert(gGlob.windowHandle, "Couldn't create the window\n");

    XTextProperty textprop;
    textprop.value    = (unsigned char*)windowTitle;
    textprop.encoding = XA_STRING;
    textprop.format   = 8;
    textprop.nitems   = strlen(windowTitle);

    XSizeHints hints;
    hints.x      = x;
    hints.y      = y;
    hints.width  = gGlob.wWidth;
    hints.height = gGlob.wHeight;
    hints.flags  = USPosition|USSize;

    XWMHints* startup_state = XAllocWMHints();
    startup_state->initial_state = NormalState;
    startup_state->flags = StateHint;

    XSetWMProperties(gGlob.xDisplay, gGlob.windowHandle,&textprop, &textprop,
                     NULL, 0, &hints, startup_state, NULL);
    XFree(startup_state);
    XFree(visual);

    XMapWindow(gGlob.xDisplay, gGlob.windowHandle);
    XEvent event;
    XIfEvent(gGlob.xDisplay, &event, WaitForMapNotify, (char*)&gGlob.windowHandle);

    // Create the input context, in order to handle unicode
    gGlob.xInputMethod = XOpenIM(gGlob.xDisplay, NULL, NULL, NULL);
    if(gGlob.xInputMethod) {
        gGlob.xInputContext = XCreateIC(gGlob.xInputMethod, XNClientWindow, gGlob.glXWindowHandle, XNFocusWindow, gGlob.glXWindowHandle,
                                        XNInputStyle, XIMPreeditNothing | XIMStatusNothing, NULL);
    }

    // Create the OpenGL 3.0 context
    int  dummy;
    bool status = glXQueryExtension(gGlob.xDisplay, &dummy, &dummy);
    plAssert(status, "OpenGL not supported by X server\n");

    typedef GLXContext (*glXCreateContextAttribsARBProc)(Display*, GLXFBConfig, GLXContext, Bool, const int*);
    glXCreateContextAttribsARBProc glXCreateContextAttribsARB = (glXCreateContextAttribsARBProc)glXGetProcAddressARB((const GLubyte *) "glXCreateContextAttribsARB");
    plAssert(glXCreateContextAttribsARB, "No support for OpenGL3 or GLX1.4\n");

#define GLX_CONTEXT_MAJOR_VERSION_ARB       0x2091
#define GLX_CONTEXT_MINOR_VERSION_ARB       0x2092
    int context_attribs[] = { GLX_CONTEXT_MAJOR_VERSION_ARB, 3,   GLX_CONTEXT_MINOR_VERSION_ARB, 3,   None };
    int (*oldHandler)(Display*, XErrorEvent*) = XSetErrorHandler(&ctxErrorHandler); // Temporarily installed to get a meaningful message in case of error
    gGlob.xRenderContext = glXCreateContextAttribsARB(gGlob.xDisplay, fbconfig, 0, True, context_attribs);
    XSync(gGlob.xDisplay, False);
    XSetErrorHandler(oldHandler);
    plAssert(gGlob.xRenderContext, "Failed to create a GL context\n");
    status = glXMakeContextCurrent(gGlob.xDisplay, gGlob.glXWindowHandle, gGlob.glXWindowHandle, gGlob.xRenderContext);
    plAssert(status, "glXMakeContextCurrent failed for window\n");

    // Force focus
    XSetInputFocus(gGlob.xDisplay, gGlob.windowHandle, RevertToParent, CurrentTime);

    // Enable the "close window" message
    gGlob.deleteMessage = XInternAtom(gGlob.xDisplay, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(gGlob.xDisplay, gGlob.windowHandle, &gGlob.deleteMessage, 1);

    // Clipboard init
    gClip.aKind       = XInternAtom(gGlob.xDisplay, "CLIPBOARD", False);
    gClip.aUtf8       = XInternAtom(gGlob.xDisplay, "UTF8_STRING", False);
    gClip.aMyApp      = XInternAtom(gGlob.xDisplay, "PALANTEER", False);
    gClip.aMyProperty = XInternAtom(gGlob.xDisplay, "ARBITRARY_PROPERTY", False);
    gClip.aTargets    = XInternAtom(gGlob.xDisplay, "TARGETS", False);

    // Create the hidden mouse cursor to simulate "no cursor"
    Pixmap cursorPixmap = XCreatePixmap(gGlob.xDisplay, gGlob.windowHandle, 1, 1, 1);
    GC graphicsContext  = XCreateGC(gGlob.xDisplay, cursorPixmap, 0, NULL);
    XDrawPoint(gGlob.xDisplay, cursorPixmap, graphicsContext, 0, 0);
    XFreeGC(gGlob.xDisplay, graphicsContext);
    XColor color;
    color.flags    = DoRed | DoGreen | DoBlue;
    color.red      = color.blue = color.green = 0;
    gGlob.noCursor = XCreatePixmapCursor(gGlob.xDisplay, cursorPixmap, cursorPixmap, &color, &color, 0, 0);
    XFreePixmap(gGlob.xDisplay, cursorPixmap);

    // Get the application path. On Linux, it is "~/.palanteer"
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
osGetWindowSize(int& width, int& height, int& dpiWidth, int& dpiHeight)
{
    width     = gGlob.wWidth;
    height    = gGlob.wHeight;
    dpiWidth  = gGlob.dpiWidth;
    dpiHeight = gGlob.dpiHeight;
}


void
osDestroyWindow(void)
{
    glXMakeCurrent(gGlob.xDisplay, 0, 0);
    glXDestroyContext(gGlob.xDisplay, gGlob.xRenderContext);

    XFreeCursor(gGlob.xDisplay, gGlob.noCursor);
    if(gGlob.xInputContext) XDestroyIC(gGlob.xInputContext);
    XDestroyWindow(gGlob.xDisplay, gGlob.windowHandle);
    XFreeColormap(gGlob.xDisplay, gGlob.xCmap);
    if(gGlob.xInputMethod) XCloseIM(gGlob.xInputMethod);
    XCloseDisplay(gGlob.xDisplay);
}


void
osSwapBuffer(void)
{
    glXSwapBuffers(gGlob.xDisplay, gGlob.glXWindowHandle);
}


void
osHideWindow(void)
{
    XUnmapWindow(gGlob.xDisplay, gGlob.windowHandle);
    XFlush(gGlob.xDisplay);
}

void
osShowWindow(void)
{
    XMapRaised(gGlob.xDisplay, gGlob.windowHandle);
    XSync(gGlob.xDisplay, False);
    if(gGlob.isDirectOverride) {
        XSetInputFocus(gGlob.xDisplay, gGlob.windowHandle, RevertToParent, CurrentTime);
    }
}


void
osSetIcon(int width, int height, const u8* pixels)
{
    static Pixmap iconPixmap     = 0;
    static Pixmap iconMaskPixmap = 0;

    // Malloc because this memory will be freed by XDestroyImage
    int iconSize   = width*height*4;
    u8* iconPixels = (u8*)malloc(iconSize);
    memcpy(iconPixels, pixels, iconSize*sizeof(u8));
    for(int i=0; i<iconSize; i+=4) bsSwap(iconPixels[i], iconPixels[i+2]); // Swap red and blue

    // Create the icon pixmap
    int     screen    = DefaultScreen(gGlob.xDisplay);
    Visual* defVisual = DefaultVisual(gGlob.xDisplay, screen);
    u32     defDepth  = DefaultDepth(gGlob.xDisplay, screen);
    XImage* iconImage = XCreateImage(gGlob.xDisplay, defVisual, defDepth, ZPixmap, 0, (char*)&iconPixels[0], width, height, 32, 0);
    if(!iconImage) { printf("Unable to create the icon\n"); return; }
    if(iconPixmap) XFreePixmap(gGlob.xDisplay, iconPixmap);

    if(iconMaskPixmap) XFreePixmap(gGlob.xDisplay, iconMaskPixmap);
    iconPixmap = XCreatePixmap(gGlob.xDisplay, RootWindow(gGlob.xDisplay, screen), width, height, defDepth);
    XGCValues values;
    GC iconGC = XCreateGC(gGlob.xDisplay, iconPixmap, 0, &values);
    XPutImage(gGlob.xDisplay, iconPixmap, iconGC, iconImage, 0, 0, 0, 0, width, height);
    XFreeGC(gGlob.xDisplay, iconGC);
    XDestroyImage(iconImage);  // This frees the iconPixels array

    // Mask pixmap (with 1 bit depth)
    int pitch = (width + 7) / 8;
    bsVec<u8> maskPixels(pitch*height);
    memset(&maskPixels[0], 0, maskPixels.size()*sizeof(u8));
    for(int j=0; j<height; ++j) {
        for(int i=0; i<pitch; ++i) {
            for(int k = 0; k < 8; ++k) {
                if(i*8+k>=width) continue;
                u8 alpha = (pixels[(i*8+k+j*width)*4+3]>0)? 1 : 0;
                maskPixels[i+j*pitch] |= (alpha << k);
            }
        }
    }
    iconMaskPixmap = XCreatePixmapFromBitmapData(gGlob.xDisplay, gGlob.windowHandle, (char*)&maskPixels[0], width, height, 1, 0, 1);

    // Send our new icon to the window through the WMHints
    XWMHints* hints = XAllocWMHints();
    hints->flags       = IconPixmapHint | IconMaskHint;
    hints->icon_pixmap = iconPixmap;
    hints->icon_mask   = iconMaskPixmap;
    XSetWMHints(gGlob.xDisplay, gGlob.windowHandle, hints);
    XFree(hints);

    // First 2 u64 are the width and height of the icon. Image data is with swapped red and blue.
    bsVec<u64> icccmIconPixels(2+width*height);
    u64* ptr = &icccmIconPixels[0];
    *ptr++ = width; *ptr++ = height;
    for(int i=0; i<width*height; ++i) {
        *(ptr++) = pixels[i*4+2] | (pixels[i*4+1]<<8) | (pixels[i*4+0]<<16) | (pixels[i*4+3]<<24);
    }

    Atom netWmIcon = XInternAtom(gGlob.xDisplay, "_NET_WM_ICON", False);
    XChangeProperty(gGlob.xDisplay, gGlob.windowHandle, netWmIcon, XA_CARDINAL, 32, PropModeReplace, reinterpret_cast<const u8*>(&icccmIconPixels[0]), 2+width*height);
    XFlush(gGlob.xDisplay);
}


void
osSetWindowTitle(const bsString& title)
{
    XStoreName(gGlob.xDisplay, gGlob.windowHandle, title.toChar());
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
    gGlob.currentCursor = state? gGlob.defaultCursor : gGlob.noCursor;
    XDefineCursor(gGlob.xDisplay, gGlob.windowHandle, gGlob.currentCursor);
    XFlush(gGlob.xDisplay);
}


bsString
osGetProgramDataPath(void)
{
    return gGlob.appPath;
}


void
osPushToClipboard(ClipboardType pushType, const bsStringUtf16& data)
{
    // Create the UTF8 buffer (linux way)
    gClip.ownedType = pushType; // Provision: can be used later for app specific type
    gClip.ownedData = data.toUtf8();
    // We own the clipboard
    XSetSelectionOwner(gGlob.xDisplay, gClip.aKind, gGlob.windowHandle, CurrentTime);
    if(XGetSelectionOwner(gGlob.xDisplay, gClip.aKind)!=gGlob.windowHandle) {
        gClip.ownedType = ClipboardType::NONE;
        gClip.ownedData.clear();
    }
}


bsStringUtf16
osReqFromClipboard(ClipboardType reqType)
{
    const bsUs_t CLIPBOARD_REQ_TIMEOUT_US = 1000000;
    gClip.reqData.clear();
    // Check if there is something to retrieve
    if(XGetSelectionOwner(gGlob.xDisplay, gClip.aKind)==None) return gClip.reqData;
    // Request the text
    bsUs_t startUs = bsGetClockUs();
    gClip.isReqReceived = false;
    gClip.reqData.clear();
    XConvertSelection(gGlob.xDisplay, gClip.aKind, gClip.aUtf8, gClip.aMyProperty, gGlob.windowHandle, CurrentTime);
    while(!gClip.isReqReceived && bsGetClockUs()<startUs+CLIPBOARD_REQ_TIMEOUT_US) {
        bsSleep(1000);
        osProcessInputs(gGlob.osHandler);
    }
    // Return the data (if any...)
    return gClip.reqData;
}


static
void
answerReqClipboard(void)
{
    Atom da, incr, type;
    int di;
    unsigned long size, dul;
    unsigned char* prop_ret = NULL;
    // Dummy call to get type and size
    XGetWindowProperty(gGlob.xDisplay, gGlob.windowHandle, gClip.aMyProperty, 0, 0, False, AnyPropertyType, &type, &di, &dul, &size, &prop_ret);
    XFree(prop_ret);
    // Check that data is not too large (which implies the INCR mechanism)
    incr = XInternAtom(gGlob.xDisplay, "INCR", False);
    if(type==incr) return;
    // Get the property content
    XGetWindowProperty(gGlob.xDisplay, gGlob.windowHandle, gClip.aMyProperty, 0, size, False, AnyPropertyType, &da, &di, &dul, &dul, &prop_ret);
    if(!prop_ret) return;
    u8* begin = prop_ret;
    u8* end   = prop_ret+size;
    char16_t codepoint;
    while(begin && *begin && begin!=end) {
        begin = bsCharUtf8ToUnicode(begin, end, codepoint);
        if(begin && codepoint) gClip.reqData.push_back(codepoint);
    }
    XFree(prop_ret);
    // Signal the selection owner that we have successfully read the data.
    XDeleteProperty(gGlob.xDisplay, gGlob.windowHandle, gClip.aMyProperty);
}


static
void
targetsPushClipboard(XSelectionRequestEvent* sev)
{
    Atom targets[] = { gClip.aTargets, gClip.aUtf8 };
    XChangeProperty(gGlob.xDisplay, sev->requestor, sev->property, XA_ATOM, 32, PropModeReplace, (u8*)&targets[0], sizeof(targets)/sizeof(targets[0]));
    XSelectionEvent ssev;
    ssev.type      = SelectionNotify;
    ssev.requestor = sev->requestor;
    ssev.selection = sev->selection;
    ssev.target    = gClip.aTargets;
    ssev.property  = sev->property;
    ssev.time      = sev->time;
    XSendEvent(gGlob.xDisplay, sev->requestor, True, NoEventMask, (XEvent*)&ssev);
}

static
void
denyPushClipboard(XSelectionRequestEvent* sev)
{
    XSelectionEvent ssev;
    ssev.type      = SelectionNotify;
    ssev.requestor = sev->requestor;
    ssev.selection = sev->selection;
    ssev.target    = sev->target;
    ssev.property  = None; // No property :-(
    ssev.time      = sev->time;
    XSendEvent(gGlob.xDisplay, sev->requestor, True, NoEventMask, (XEvent*)&ssev);
}

static
void
answerPushClipboard(XSelectionRequestEvent* sev)
{
    XChangeProperty(gGlob.xDisplay, sev->requestor, sev->property, gClip.aUtf8, 8, PropModeReplace, &gClip.ownedData[0], gClip.ownedData.size());
    XSelectionEvent ssev;
    ssev.type      = SelectionNotify;
    ssev.requestor = sev->requestor;
    ssev.selection = sev->selection;
    ssev.target    = sev->target;
    ssev.property  = sev->property;
    ssev.time      = sev->time;
    XSendEvent(gGlob.xDisplay, sev->requestor, True, NoEventMask, (XEvent *)&ssev);
}


static bsKeycode
keysymToKeycode(KeySym symbol)
{
    switch (symbol) {
    case XK_Shift_L:      return KC_LShift;
    case XK_Shift_R:      return KC_RShift;
    case XK_Control_L:    return KC_LControl;
    case XK_Control_R:    return KC_RControl;
    case XK_Alt_L:        return KC_LAlt;
    case XK_Alt_R:        return KC_RAlt;
    case XK_Super_L:      return KC_LSystem;
    case XK_Super_R:      return KC_RSystem;
    case XK_Menu:         return KC_Menu;
    case XK_Escape:       return KC_Escape;
    case XK_semicolon:    return KC_Semicolon;
    case XK_slash:        return KC_Slash;
    case XK_equal:        return KC_Equal;
    case XK_minus:        return KC_Hyphen;
    case XK_bracketleft:  return KC_LBracket;
    case XK_bracketright: return KC_RBracket;
    case XK_comma:        return KC_Comma;
    case XK_period:       return KC_Period;
    case XK_apostrophe:   return KC_Quote;
    case XK_backslash:    return KC_Backslash;
    case XK_grave:        return KC_Tilde;
    case XK_space:        return KC_Space;
    case XK_Return:       return KC_Enter;
    case XK_KP_Enter:     return KC_Enter;
    case XK_BackSpace:    return KC_Backspace;
    case XK_Tab:          return KC_Tab;
    case XK_Prior:        return KC_PageUp;
    case XK_Next:         return KC_PageDown;
    case XK_End:          return KC_End;
    case XK_Home:         return KC_Home;
    case XK_Insert:       return KC_Insert;
    case XK_Delete:       return KC_Delete;
    case XK_KP_Add:       return KC_Add;
    case XK_KP_Subtract:  return KC_Subtract;
    case XK_KP_Multiply:  return KC_Multiply;
    case XK_KP_Divide:    return KC_Divide;
    case XK_Pause:        return KC_Pause;
    case XK_F1:           return KC_F1;
    case XK_F2:           return KC_F2;
    case XK_F3:           return KC_F3;
    case XK_F4:           return KC_F4;
    case XK_F5:           return KC_F5;
    case XK_F6:           return KC_F6;
    case XK_F7:           return KC_F7;
    case XK_F8:           return KC_F8;
    case XK_F9:           return KC_F9;
    case XK_F10:          return KC_F10;
    case XK_F11:          return KC_F11;
    case XK_F12:          return KC_F12;
    case XK_F13:          return KC_F13;
    case XK_F14:          return KC_F14;
    case XK_F15:          return KC_F15;
    case XK_Left:         return KC_Left;
    case XK_Right:        return KC_Right;
    case XK_Up:           return KC_Up;
    case XK_Down:         return KC_Down;
    case XK_KP_Insert:    return KC_Numpad0;
    case XK_KP_End:       return KC_Numpad1;
    case XK_KP_Down:      return KC_Numpad2;
    case XK_KP_Page_Down: return KC_Numpad3;
    case XK_KP_Left:      return KC_Numpad4;
    case XK_KP_Begin:     return KC_Numpad5;
    case XK_KP_Right:     return KC_Numpad6;
    case XK_KP_Home:      return KC_Numpad7;
    case XK_KP_Up:        return KC_Numpad8;
    case XK_KP_Page_Up:   return KC_Numpad9;
    case XK_a:            return KC_A;
    case XK_b:            return KC_B;
    case XK_c:            return KC_C;
    case XK_d:            return KC_D;
    case XK_e:            return KC_E;
    case XK_f:            return KC_F;
    case XK_g:            return KC_G;
    case XK_h:            return KC_H;
    case XK_i:            return KC_I;
    case XK_j:            return KC_J;
    case XK_k:            return KC_K;
    case XK_l:            return KC_L;
    case XK_m:            return KC_M;
    case XK_n:            return KC_N;
    case XK_o:            return KC_O;
    case XK_p:            return KC_P;
    case XK_q:            return KC_Q;
    case XK_r:            return KC_R;
    case XK_s:            return KC_S;
    case XK_t:            return KC_T;
    case XK_u:            return KC_U;
    case XK_v:            return KC_V;
    case XK_w:            return KC_W;
    case XK_x:            return KC_X;
    case XK_y:            return KC_Y;
    case XK_z:            return KC_Z;
    case XK_0:            return KC_Num0;
    case XK_1:            return KC_Num1;
    case XK_2:            return KC_Num2;
    case XK_3:            return KC_Num3;
    case XK_4:            return KC_Num4;
    case XK_5:            return KC_Num5;
    case XK_6:            return KC_Num6;
    case XK_7:            return KC_Num7;
    case XK_8:            return KC_Num8;
    case XK_9:            return KC_Num9;
    }

    return KC_Unknown;
}


void
osProcessInputs(bsOsHandler* handler)
{
    gGlob.osHandler = handler;
    XEvent event;
    /* // Debug code
    static const char* eventNames[LASTEvent] = { "None", "None", "KeyPress", "KeyRelease", "ButtonPress", "ButtonRelease",
                                                 "MotionNotify", "EnterNotify", "LeaveNotify", "FocusIn",
                                                 "FocusOut", "KeymapNotify", "Expose", "GraphicsExpose", "NoExpose",
                                                 "VisibilityNotify", "CreateNotify", "DestroyNotify", "UnmapNotify",
                                                 "MapNotify", "MapRequest", "ReparentNotify", "ConfigureNotify",
                                                 "ConfigureRequest", "GravityNotify", "ResizeRequest", "CirculateNotify",
                                                 "CirculateRequest", "PropertyNotify", "SelectionClear", "SelectionRequest",
                                                 "SelectionNotify", "ColormapNotify", "ClientMessage",
                                                 "MappingNotify", "GenericEvent" };
    */

    bsKeycode kc;
    bsKeyModState kms;
    while(XPending(gGlob.xDisplay)) {
        // Get next event
        XNextEvent(gGlob.xDisplay, &event);
        /* // Debug code
          if(event.type<LASTEvent) printf("Event %-14s-%2d:", eventNames[event.type], event.type);
          else printf("Unknown event %2d", event.type);
        */

        switch(event.type) {

        case ClientMessage:
            if((Atom)event.xclient.data.l[0]==gGlob.deleteMessage) gGlob.osHandler->quit();
            break;

        case SelectionClear:
            gClip.ownedType = ClipboardType::NONE;
            gClip.ownedData.clear();
            break;

        case SelectionRequest:
            {
                // Sending our content to external
                XSelectionRequestEvent* sev = (XSelectionRequestEvent*)&event.xselection;
                if     (sev->target==gClip.aTargets) targetsPushClipboard(sev);
                else if(sev->target!=gClip.aUtf8 || sev->property==None || gClip.ownedData.empty()) denyPushClipboard(sev);
                else answerPushClipboard(sev);
            }
            break;

        case SelectionNotify:
            {
                // Reception of external content
                XSelectionEvent* sev = (XSelectionEvent*)&event.xselection;
                gClip.isReqReceived = true;
                if(sev->property!=None) answerReqClipboard();
            }
            break;

        case KeyPress:
            for(int i=0; i<4; ++i) { // Try each KeySym index (modifier group) until we get a match
                if((kc=keysymToKeycode(XLookupKeysym(&event.xkey, i)))!=KC_Unknown) break;
            }
            if(kc!=KC_Unknown) {
                kms = { (bool)(event.xkey.state&ShiftMask), (bool)(event.xkey.state&ControlMask), (bool)(event.xkey.state&Mod1Mask), (bool)(event.xkey.state&Mod4Mask) };
                gGlob.osHandler->eventKeyPressed(kc, kms);
            }

            // Generate a character event
            if(XFilterEvent(&event, None)) break; // Filtered by the system
#ifdef X_HAVE_UTF8_STRING
            if(gGlob.xInputContext) {
                Status status;
                u8  keyBuffer[16];
                int length = Xutf8LookupString(gGlob.xInputContext, &event.xkey, (char*)keyBuffer, 16, 0, &status);
                if(length>0) {
                    char16_t codepoint;
                    (void)bsCharUtf8ToUnicode(keyBuffer, keyBuffer+length, codepoint);
                    if(codepoint!=0 && bsIsUnicodeDisplayable(codepoint)) gGlob.osHandler->eventChar(codepoint);
                }
            }
            else
#endif
                { // Fallback if no UTF-8 support: handles only Latin-1
                    static XComposeStatus status;
                    char keyBuffer[16];
                    if(XLookupString(&event.xkey, keyBuffer, sizeof(keyBuffer), NULL, &status)) {
                        gGlob.osHandler->eventChar(static_cast<char16_t>(keyBuffer[0]));
                    }
                }

            break;

        case KeyRelease:
            for(int i=0; i<4; ++i) { // Try each KeySym index (modifier group) until we get a match
                if((kc=keysymToKeycode(XLookupKeysym(&event.xkey, i)))!=KC_Unknown) break;
            }
            if(kc!=KC_Unknown) {
                kms = { (bool)(event.xkey.state&ShiftMask), (bool)(event.xkey.state&ControlMask), (bool)(event.xkey.state&Mod1Mask), (bool)(event.xkey.state&Mod4Mask) };
                gGlob.osHandler->eventKeyReleased(kc, kms);
            }
            break;
        case FocusIn:
            if(gGlob.xInputContext) {
                XSetICFocus(gGlob.xInputContext);
            }
            break;
        case FocusOut:
            if(gGlob.xInputContext) {
                XUnsetICFocus(gGlob.xInputContext);
            }
            if(gGlob.osHandler->isVisible() && event.xfocus.mode==NotifyNormal) {
                gGlob.osHandler->notifyFocusOut();
            }
            break;
        case EnterNotify:
            kms = { (bool)(event.xcrossing.state&ShiftMask), (bool)(event.xcrossing.state&ControlMask), (bool)(event.xcrossing.state&Mod1Mask), (bool)(event.xcrossing.state&Mod4Mask) };
            gGlob.osHandler->notifyEnter(kms);  // KMS resynchronization is important!
            break;
        case LeaveNotify:
            kms = { (bool)(event.xcrossing.state&ShiftMask), (bool)(event.xcrossing.state&ControlMask), (bool)(event.xcrossing.state&Mod1Mask), (bool)(event.xcrossing.state&Mod4Mask) };
            gGlob.osHandler->notifyLeave(kms);
            break;
        case MapNotify:
            if(!gGlob.osHandler->isVisible()) {
                gGlob.osHandler->notifyMapped();
            }
            break;
        case UnmapNotify:
            gGlob.osHandler->notifyUnmapped();
            if(gGlob.xInputContext) {
                XUnsetICFocus(gGlob.xInputContext);
            }
            break;
        case ButtonPress:
            kms = { (bool)(event.xbutton.state&ShiftMask), (bool)(event.xbutton.state&ControlMask), (bool)(event.xbutton.state&Mod1Mask), (bool)(event.xbutton.state&Mod4Mask) };
            if     (event.xbutton.button==4) gGlob.osHandler->eventWheelScrolled(event.xbutton.x, event.xbutton.y, -1, kms);
            else if(event.xbutton.button==5) gGlob.osHandler->eventWheelScrolled(event.xbutton.x, event.xbutton.y, +1, kms);
            else gGlob.osHandler->eventButtonPressed(event.xbutton.button, event.xbutton.x, event.xbutton.y, kms);
            break;
        case ButtonRelease:
            kms = { (bool)(event.xbutton.state&ShiftMask), (bool)(event.xbutton.state&ControlMask), (bool)(event.xbutton.state&Mod1Mask), (bool)(event.xbutton.state&Mod4Mask) };
            if(event.xbutton.button<4) gGlob.osHandler->eventButtonReleased(event.xbutton.button, event.xbutton.x, event.xbutton.y, kms);
            break;
        case MotionNotify :
            gGlob.osHandler->eventMouseMotion(event.xmotion.x, event.xmotion.y);
            break;
        case ConfigureNotify:
            {
                XConfigureEvent* e = &event.xconfigure;
                gGlob.wWidth  = e->width;
                gGlob.wHeight = e->height;
                gGlob.osHandler->notifyWindowSize(gGlob.wWidth, gGlob.wHeight);
            }
            break;
        case Expose:
            {
                XExposeEvent* e = &event.xexpose;
                if(e->count==0) { // We are interested in last expose event only
                    gGlob.osHandler->notifyExposed();
                }
            }
            break;
        }

    }
}


// Linux entry point
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
    // No concept of "drive" in linux
    return 0;
}


FILE*
osFileOpen(const bsString& path, const char* mode)
{
    return fopen(path.toChar(), mode); // Utf-8 does not change the fopen prototype on Linux!
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


#endif // __linux__
