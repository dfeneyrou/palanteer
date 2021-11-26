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

// System
#include <cstdio>
#include <cstdlib>

// Internal
#include "bs.h"
#ifdef __linux__
#include "bsOsGlLnx.h"
#endif
#ifdef _WIN32
#include "bsOsGlWin.h"
#endif


// Helper OpenGL debugging function. Just call it after a bunch of GL commands in order to check for issues
inline void bsGlCheckError(const char* filename, int lineNbr) {
    GLenum err;
    while((err=glGetError())!=GL_NO_ERROR) {
        const char* errorKind = 0;
        switch(err) {
        case GL_INVALID_OPERATION: errorKind = "INVALID_OPERATION"; break;
        case GL_INVALID_ENUM:      errorKind = "INVALID_ENUM";      break;
        case GL_INVALID_VALUE:     errorKind = "INVALID_VALUE";     break;
        case GL_OUT_OF_MEMORY:     errorKind = "OUT_OF_MEMORY";     break;
        case GL_INVALID_FRAMEBUFFER_OPERATION: errorKind = "INVALID_FRAMEBUFFER_OPERATION";  break;
        default: errorKind = "(UNKNOWN ERROR TYPE)";
        }
        printf("GL_%s: at line %d of file %s\n", errorKind, lineNbr, filename);
    }
}

// Helper OpenGL debugging function. Just call it after compilation of the shaders. For linking, call bsGlCheckError instead
inline void bsGlCheckShaderCompilation(const char* filename, int lineNbr, int shaderId) {
    // Get the compilation status
    GLint isCompiled = 0;
    glGetShaderiv(shaderId, GL_COMPILE_STATUS, &isCompiled);
    // Error detected?
    if(isCompiled==GL_FALSE) {
        // Get the length of the error message (which includes the ending NULL character)
        GLint maxLength = 0;
        glGetShaderiv(shaderId, GL_INFO_LOG_LENGTH, &maxLength);
        // Get the error message
        GLchar* errorLog = (GLchar*)malloc(maxLength);
        plAssert(errorLog);
        glGetShaderInfoLog(shaderId, maxLength, &maxLength, errorLog);
        // Display and quit (no shader = fatal error)
        printf("GL shader compilation error (file %s line %d) : %s", filename, lineNbr, errorLog);
        free(errorLog);
        exit(1);
    }
}

#ifdef WITH_GL_CHECK
#define GL_CHECK() bsGlCheckError(__FILE__, __LINE__)
#define GL_CHECK_COMPILATION(shaderId) bsGlCheckShaderCompilation(__FILE__, __LINE__, shaderId)
#else
#define GL_CHECK()
#define GL_CHECK_COMPILATION(shaderId)
#endif // WITH_GL_CHECK


class bsGlProgramVAO {
public:
    bsGlProgramVAO(void);
    ~bsGlProgramVAO(void);

    void install(const GLchar *vertexShaderSrc, const GLchar* fragmentShaderSrc);
    void deinstall(void);
    int getId(void) const { return _programId; }
    int getIboId(void) const { return _iboId; }
    int getVboId(void) const { return _vboId; }
    int getVaoId(void) const { return _vaoId; }

private:
    int  _programId, _vertShaderId, _fragShaderId;
    u32 _vboId, _vaoId, _iboId;
};
