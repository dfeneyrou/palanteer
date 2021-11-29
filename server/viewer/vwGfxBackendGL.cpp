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

// This file implements the Open GL backend

#if defined(__linux__) || defined(_WIN32)

// External
#include "imgui.h"

// Internal
#include "bs.h"
#include "bsGl.h"
#include "vwGfxBackend.h"


// Shaders
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


// Rendering context
static struct {
    int frameBufferWidth  = 0;
    int frameBufferHeight = 0;
    bsGlProgramVAO guiGlProgram;
    GLuint         fontTextureId             = 0;
    int            unifAttribLocationTex     = 0;
    int            unifAttribLocationProjMtx = 0;
    int attribLocationPosition = 0;
    int attribLocationUV       = 0;
    int attribLocationColor    = 0;
} vwGlCtx;


void
vwBackendInit(void)
{

    // Allocate the font texture (fully initialized later)
    glGenTextures  (1, &vwGlCtx.fontTextureId);
    glBindTexture  (GL_TEXTURE_2D, vwGlCtx.fontTextureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei  (GL_UNPACK_ROW_LENGTH, 0);

    // Build and configure the OpenGL Vertex Array Object for GUI
    vwGlCtx.guiGlProgram.install(guiVertexShaderSrc, guiFragmentShaderSrc);
    vwGlCtx.unifAttribLocationTex      = glGetUniformLocation(vwGlCtx.guiGlProgram.getId(), "Texture");
    vwGlCtx.unifAttribLocationProjMtx  = glGetUniformLocation(vwGlCtx.guiGlProgram.getId(), "ProjMtx");
    vwGlCtx.attribLocationPosition     = glGetAttribLocation(vwGlCtx.guiGlProgram.getId(),  "Position");
    vwGlCtx.attribLocationUV           = glGetAttribLocation(vwGlCtx.guiGlProgram.getId(),  "UV");
    vwGlCtx.attribLocationColor        = glGetAttribLocation(vwGlCtx.guiGlProgram.getId(),  "Color");
    glEnableVertexAttribArray(vwGlCtx.attribLocationPosition);
    glEnableVertexAttribArray(vwGlCtx.attribLocationUV);
    glEnableVertexAttribArray(vwGlCtx.attribLocationColor);
    glVertexAttribPointer(vwGlCtx.attribLocationPosition, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), (GLvoid*)IM_OFFSETOF(ImDrawVert, pos));
    glVertexAttribPointer(vwGlCtx.attribLocationUV, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), (GLvoid*)IM_OFFSETOF(ImDrawVert, uv));
    glVertexAttribPointer(vwGlCtx.attribLocationColor, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(ImDrawVert), (GLvoid*)IM_OFFSETOF(ImDrawVert, col));
    GL_CHECK();

    // Base GL setup
    glClearColor(0.3f, 0.3f, 0.3f, 1.0f);
}


bool
vwBackendDraw(void)
{
    ImDrawData* drawData = ImGui::GetDrawData();
    plAssert(drawData);
    vwGlCtx.frameBufferWidth  = (int)(drawData->DisplaySize.x * drawData->FramebufferScale.x);
    vwGlCtx.frameBufferHeight = (int)(drawData->DisplaySize.y * drawData->FramebufferScale.y);
    if(vwGlCtx.frameBufferWidth==0 || vwGlCtx.frameBufferHeight==0) return false;
    plScope("OpenGL engine");
    plVar(vwGlCtx.frameBufferWidth, vwGlCtx.frameBufferHeight);

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
    glViewport(0, 0, (GLsizei)vwGlCtx.frameBufferWidth, (GLsizei)vwGlCtx.frameBufferHeight);
    ImGuiIO& io = ImGui::GetIO();
    const float ortho_projection[4][4] = {
        { 2.0f/io.DisplaySize.x, 0.0f,                   0.0f, 0.0f },
        { 0.0f,                  2.0f/-io.DisplaySize.y, 0.0f, 0.0f },
        { 0.0f,                  0.0f,                  -1.0f, 0.0f },
        {-1.0f,                  1.0f,                   0.0f, 1.0f },
    };
    glUseProgram(vwGlCtx.guiGlProgram.getId());
    glUniform1i(vwGlCtx.unifAttribLocationTex, 0);
    glUniformMatrix4fv(vwGlCtx.unifAttribLocationProjMtx, 1, GL_FALSE, &ortho_projection[0][0]);
    glBindVertexArray(vwGlCtx.guiGlProgram.getVaoId());
    glBindSampler(0, 0); // Rely on combined texture/sampler state.
    GL_CHECK();

    // Will project scissor/clipping rectangles into framebuffer space
    ImVec2 clipOff   = drawData->DisplayPos;       // (0,0) unless using multi-viewports
    ImVec2 clipScale = drawData->FramebufferScale; // (1,1) unless using retina display which are often (2,2)

    glBindBuffer(GL_ARRAY_BUFFER, vwGlCtx.guiGlProgram.getVboId());
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vwGlCtx.guiGlProgram.getIboId());

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
                if(clipRect.x<vwGlCtx.frameBufferWidth && clipRect.y<vwGlCtx.frameBufferHeight && clipRect.z>=0.0f && clipRect.w>=0.0f) {
                    // Apply scissor/clipping rectangle
                    glScissor((int)cmd->ClipRect.x, (int)(vwGlCtx.frameBufferHeight-cmd->ClipRect.w),
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

    return true; // We drew something
}


bool
vwCaptureScreen(int* width, int* height, u8** buffer)
{
    if(vwGlCtx.frameBufferWidth==0 || vwGlCtx.frameBufferHeight==0) return false;
    plAssert(width && height && buffer);
    *width  = vwGlCtx.frameBufferWidth;
    *height = vwGlCtx.frameBufferHeight;
    *buffer = new u8[3*(*width)*(*height)];  // RGB = 3 components
    glReadPixels(0, 0, *width, *height, GL_RGB, GL_UNSIGNED_BYTE, *buffer);
    return true;
}


void
vwBackendInstallFont(const void* fontData, int fontDataSize, int fontSize)
{
    // Some config
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    io.Fonts->AddFontFromMemoryCompressedTTF(fontData, fontDataSize, (float)fontSize);

    // Build texture atlas
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsAlpha8(&pixels, &width, &height);

    // Upload texture to graphics system
    glBindTexture(GL_TEXTURE_2D, vwGlCtx.fontTextureId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, pixels);
    io.Fonts->TexID = (void *)(intptr_t)vwGlCtx.fontTextureId; // Store our identifier
    GL_CHECK();
}


void
vwBackendUninit(void)
{
    vwGlCtx.guiGlProgram.deinstall();
    if(vwGlCtx.fontTextureId) {
        glDeleteTextures(1, &vwGlCtx.fontTextureId);
        ImGui::GetIO().Fonts->TexID = 0;
        vwGlCtx.fontTextureId = 0;
    }
}


#endif // if defined(__linux__) || defined(_WIN32)
