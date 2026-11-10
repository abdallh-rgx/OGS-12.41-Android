// ============================================================================
// GuiRenderer.cpp — in-game GUI rendering for the Android port.
//
// Hooks eglSwapBuffers (libEGL.so, global exported symbol) and renders the
// ImGui menu over the game frame with OpenGL ES 2 (the same technique the
// Erbium/Erbium-Android project uses on-device). Touch events for the menu
// come from the AInputQueue hook (GuiTouchInput.cpp) and are injected into
// ImGui's IO; when the menu is closed all input flows to the game untouched.
// ============================================================================
#include "framework.h"
#include "Hooks.h"
#include "GuiMenu.h"
#include "GuiTouchInput.h"

#include "imgui.h"

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <dlfcn.h>
#include <unordered_map>

namespace Gui
{

// forward declarations within Gui
bool RenderFrame(void* Display, void* Surface);

namespace
{

// ─── EGL hook ───────────────────────────────────────────────────────────────
EGLBoolean (*eglSwapBuffersOG)(EGLDisplay, EGLSurface);

EGLBoolean eglSwapBuffersHK(EGLDisplay Display, EGLSurface Surface)
{
	if (Gui::bMenuOpen)
	{
		RenderFrame(Display, Surface);
	}
	return eglSwapBuffersOG(Display, Surface);
}

// ─── GLES2 shader pipeline (textured, vertex-color) ─────────────────────────
GLuint g_Program = 0, g_VertexShader = 0, g_FragmentShader = 0;
GLuint g_FontTexture = 0;
GLint g_AttribLocationTex = 0, g_UniformLocationProjMtx = 0;
GLint g_AttribLocationPosition = 0, g_AttribLocationUV = 0, g_AttribLocationColor = 0;
GLuint g_VboHandle = 0, g_ElementsHandle = 0;
bool g_Initialized = false;

const char* kVertexShader =
	"precision mediump float;\n"
	"uniform mat4 ProjMtx;\n"
	"attribute vec2 Position;\n"
	"attribute vec2 UV;\n"
	"attribute vec4 Color;\n"
	"varying vec2 Frag_UV;\n"
	"varying vec4 Frag_Color;\n"
	"void main()\n"
	"{\n"
	"    Frag_UV = UV;\n"
	"    Frag_Color = Color;\n"
	"    gl_Position = ProjMtx * vec4(Position.xy, 0.0, 1.0);\n"
	"}\n";

const char* kFragmentShader =
	"precision mediump float;\n"
	"uniform sampler2D Texture;\n"
	"varying vec2 Frag_UV;\n"
	"varying vec4 Frag_Color;\n"
	"void main()\n"
	"{\n"
	"    gl_FragColor = Frag_Color * texture2D(Texture, Frag_UV.st);\n"
	"}\n";

void CreateDeviceObjects()
{
	g_VertexShader = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(g_VertexShader, 1, &kVertexShader, nullptr);
	glCompileShader(g_VertexShader);

	g_FragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(g_FragmentShader, 1, &kFragmentShader, nullptr);
	glCompileShader(g_FragmentShader);

	g_Program = glCreateProgram();
	glAttachShader(g_Program, g_VertexShader);
	glAttachShader(g_Program, g_FragmentShader);
	glLinkProgram(g_Program);

	g_AttribLocationTex = glGetUniformLocation(g_Program, "Texture");
	g_UniformLocationProjMtx = glGetUniformLocation(g_Program, "ProjMtx");
	g_AttribLocationPosition = glGetAttribLocation(g_Program, "Position");
	g_AttribLocationUV = glGetAttribLocation(g_Program, "UV");
	g_AttribLocationColor = glGetAttribLocation(g_Program, "Color");

	glGenBuffers(1, &g_VboHandle);
	glGenBuffers(1, &g_ElementsHandle);

	// font texture from ImGui's built-in atlas (includes Latin + basic glyphs)
	ImGuiIO& Io = ImGui::GetIO();
	unsigned char* Pixels = nullptr;
	int Width = 0, Height = 0;
	Io.Fonts->GetTexDataAsRGBA32(&Pixels, &Width, &Height);

	glGenTextures(1, &g_FontTexture);
	glBindTexture(GL_TEXTURE_2D, g_FontTexture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, Width, Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, Pixels);

	Io.Fonts->SetTexID((ImTextureID)(intptr_t)g_FontTexture);
}

void RenderDrawData(ImDrawData* DrawData)
{
	glEnable(GL_BLEND);
	glBlendEquation(GL_FUNC_ADD);
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_SCISSOR_TEST);
	glEnable(GL_SCISSOR_TEST);

	glViewport(0, 0, (GLsizei)DrawData->DisplaySize.x, (GLsizei)DrawData->DisplaySize.y);

	const float L = DrawData->DisplayPos.x;
	const float R = DrawData->DisplayPos.x + DrawData->DisplaySize.x;
	const float T = DrawData->DisplayPos.y;
	const float B = DrawData->DisplayPos.y + DrawData->DisplaySize.y;
	const float ProjMtx[4][4] =
	{
		{ 2.0f / (R - L),   0.0f,           0.0f,   0.0f },
		{ 0.0f,             2.0f / (T - B), 0.0f,   0.0f },
		{ 0.0f,             0.0f,          -1.0f,   0.0f },
		{ (R + L) / (L - R),(T + B) / (B - T), 0.0f, 1.0f },
	};

	glUseProgram(g_Program);
	glUniformMatrix4fv(g_UniformLocationProjMtx, 1, GL_FALSE, &ProjMtx[0][0]);
	glUniform1i(g_AttribLocationTex, 0);

	glBindBuffer(GL_ARRAY_BUFFER, g_VboHandle);
	glEnableVertexAttribArray((GLuint)g_AttribLocationPosition);
	glEnableVertexAttribArray((GLuint)g_AttribLocationUV);
	glEnableVertexAttribArray((GLuint)g_AttribLocationColor);

#define OFFSETOF(TYPE, ELEMENT) ((size_t)&(((TYPE*)0)->ELEMENT))
	glVertexAttribPointer((GLuint)g_AttribLocationPosition, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert),
		(GLvoid*)OFFSETOF(ImDrawVert, pos));
	glVertexAttribPointer((GLuint)g_AttribLocationUV, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert),
		(GLvoid*)OFFSETOF(ImDrawVert, uv));
	glVertexAttribPointer((GLuint)g_AttribLocationColor, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(ImDrawVert),
		(GLvoid*)OFFSETOF(ImDrawVert, col));
#undef OFFSETOF

	for (int n = 0; n < DrawData->CmdListsCount; n++)
	{
		const ImDrawList* CmdList = DrawData->CmdLists[n];
		const ImDrawIdx* IdxBuffer = nullptr;
		const ImDrawVert* VtxBuffer = nullptr;

		glBindBuffer(GL_ARRAY_BUFFER, g_VboHandle);
		glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)CmdList->VtxBuffer.Size * sizeof(ImDrawVert),
			(const GLvoid*)CmdList->VtxBuffer.Data, GL_STREAM_DRAW);

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_ElementsHandle);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)CmdList->IdxBuffer.Size * sizeof(ImDrawIdx),
			(const GLvoid*)CmdList->IdxBuffer.Data, GL_STREAM_DRAW);

		IdxBuffer = CmdList->IdxBuffer.Data;
		VtxBuffer = CmdList->VtxBuffer.Data;

		for (int CmdIdx = 0; CmdIdx < CmdList->CmdBuffer.Size; CmdIdx++)
		{
			const ImDrawCmd* Pcmd = &CmdList->CmdBuffer[CmdIdx];
			if (Pcmd->UserCallback)
			{
				Pcmd->UserCallback(CmdList, Pcmd);
			}
			else
			{
				glBindTexture(GL_TEXTURE_2D, (GLuint)(intptr_t)Pcmd->GetTexID());
				glScissor((int)Pcmd->ClipRect.x - (int)DrawData->DisplayPos.x,
					(int)(DrawData->DisplaySize.y - Pcmd->ClipRect.w) - (int)DrawData->DisplayPos.y,
					(int)(Pcmd->ClipRect.z - Pcmd->ClipRect.x),
					(int)(Pcmd->ClipRect.w - Pcmd->ClipRect.y));
				glDrawElements(GL_TRIANGLES, (GLsizei)Pcmd->ElemCount,
					sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT, IdxBuffer + Pcmd->IdxOffset);
			}
		}
	}

	glDisableVertexAttribArray((GLuint)g_AttribLocationPosition);
	glDisableVertexAttribArray((GLuint)g_AttribLocationUV);
	glDisableVertexAttribArray((GLuint)g_AttribLocationColor);
	glDisable(GL_BLEND);
	glDisable(GL_SCISSOR_TEST);
}

} // namespace

bool RenderFrame(void* DisplayPtr, void* SurfacePtr)
{
	EGLDisplay Display = (EGLDisplay)DisplayPtr;
	EGLSurface Surface = (EGLSurface)SurfacePtr;
	EGLint Width = 0, Height = 0;
	eglQuerySurface(Display, Surface, EGL_WIDTH, &Width);
	eglQuerySurface(Display, Surface, EGL_HEIGHT, &Height);

	ImGuiIO& Io = ImGui::GetIO();
	Io.DisplaySize = ImVec2((float)Width, (float)Height);

	if (!g_Initialized)
	{
		ImGui::CreateContext();
		ImGui::StyleColorsDark();
		CreateDeviceObjects();
		g_Initialized = true;
		LOGI("Gui: ImGui context ready (%dx%d)", Width, Height);
	}

	// feed the gesture state into ImGui (no per-touch routing needed for the
	// level-triggered menu — the menu buttons consume their own touch events
	// through the input hook below)
	ImGui::NewFrame();

	if (!RenderMenuFrame())
		return false;

	ImGui::Render();
	RenderDrawData(ImGui::GetDrawData());
	return true;
}

void InitRenderer()
{
	void* Sym = dlsym(RTLD_DEFAULT, "eglSwapBuffers");
	if (!Sym)
	{
		LOGW("Gui: eglSwapBuffers not found — GUI rendering disabled");
		return;
	}

	if (!Hooking::InternalHook((uintptr_t)Sym, (void*)eglSwapBuffersHK, (void**)&eglSwapBuffersOG))
	{
		LOGW("Gui: failed to hook eglSwapBuffers — GUI rendering disabled");
		return;
	}

	LOGI("Gui: eglSwapBuffers hooked — in-game GUI rendering armed");
}

} // namespace Gui
