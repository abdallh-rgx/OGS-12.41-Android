// ============================================================================
// GuiMenu.h — the in-game English GUI (ImGui over GLES2).
//
// Opens with the 4-finger / 2-second gesture. Implements the PC server's
// commands at match-entry, as required:
//   * pick any game mode (the OGS Globals flags: Creative, STW, Event,
//     Arena, Team Rumble, Solid Gold, Unvaulted, Siphon, Blitz, Late Game,
//     Storm King, Arsenal, Battle Lab, bots, bosses, playlist overrides)
//   * OR join another player by entering their device IP
//   * Start — applies the selection and enters the match
// ============================================================================
#pragma once

namespace Gui
{
	// Render one frame of the menu (called from the eglSwapBuffers hook when
	// the menu is open). Returns false if the renderer isn't ready yet.
	bool RenderMenuFrame();

	// Render a full ImGui frame over the EGL surface (called from the
	// eglSwapBuffers hook).
	bool RenderFrame(void* Display, void* Surface);

	// Hook eglSwapBuffers (idempotent).
	void InitRenderer();

	// The join-by-IP target the user typed (empty = host mode).
	extern char JoinIp[64];
	extern bool bJoinMode;
}
