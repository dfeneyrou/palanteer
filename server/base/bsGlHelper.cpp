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

#include "bsGl.h"

// This file implements some OpenGL helpers


bsGlProgramVAO::bsGlProgramVAO(void) :
    _programId(0), _vertShaderId(0), _fragShaderId(0),
    _vboId(0), _vaoId(0), _iboId(0)
{
}


bsGlProgramVAO::~bsGlProgramVAO(void)
{
    deinstall();
}


void
bsGlProgramVAO::install(const GLchar *vertexShaderSrc, const GLchar* fragmentShaderSrc)
{
    plAssert(_programId==0);
    // Create the place holders
    _programId    = glCreateProgram();
    _vertShaderId = glCreateShader(GL_VERTEX_SHADER);
    _fragShaderId = glCreateShader(GL_FRAGMENT_SHADER);

    // Compile shaders
    glShaderSource(_vertShaderId, 1, &vertexShaderSrc, 0);
    glShaderSource(_fragShaderId, 1, &fragmentShaderSrc, 0);
    glCompileShader(_vertShaderId);
    GL_CHECK_COMPILATION(_vertShaderId);
    glCompileShader(_fragShaderId);
    GL_CHECK_COMPILATION(_fragShaderId);

    // Link shaders
    glAttachShader(_programId, _vertShaderId);
    glAttachShader(_programId, _fragShaderId);
    glLinkProgram(_programId);
    GL_CHECK();

    glGenBuffers(1, &_vboId);
    glGenBuffers(1, &_iboId);

    glGenVertexArrays(1, &_vaoId);
    glBindVertexArray(_vaoId);
    glBindBuffer(GL_ARRAY_BUFFER, _vboId);
    GL_CHECK();
}


void
bsGlProgramVAO::deinstall(void)
{
    if(_vaoId) { glDeleteVertexArrays(1, &_vaoId); GL_CHECK(); }
    if(_vboId) { glDeleteBuffers(1, &_vboId); GL_CHECK(); }
    if(_iboId) { glDeleteBuffers(1, &_iboId); GL_CHECK(); }
    _vaoId = _vboId = _iboId = 0;

    if(_programId && _vertShaderId) { glDetachShader(_programId, _vertShaderId); GL_CHECK(); }
    if(_vertShaderId) { glDeleteShader(_vertShaderId); GL_CHECK(); }
    _vertShaderId = 0;

    if(_programId && _fragShaderId) { glDetachShader(_programId, _fragShaderId); GL_CHECK(); }
    if(_fragShaderId) { glDeleteShader(_fragShaderId); GL_CHECK(); }
    _fragShaderId = 0;

    if(_programId) { glDeleteProgram(_programId); GL_CHECK(); }
    _programId = 0;
}
