// GuiMenu.cpp — the in-game English GUI (see GuiMenu.h).
#include "framework.h"
#include "GuiMenu.h"
#include "GuiTouchInput.h"
#include "ListenServer.h"

#include "imgui.h"
#include "main_GIs.h"

#include <cstring>

namespace Gui
{

namespace
{
template <typename T> static inline T Min(T A, T B) { return A < B ? A : B; }
}


char JoinIp[64] = "";
bool bJoinMode = false;

namespace
{

// ─── mode selection state (backed by the OGS PC Globals) ───────────────────
struct FModeOption
{
	const char* Label;
	bool* Flag;
	const char* Hint;
};

bool bSoloDefaults = true; // default: standard BR

void ApplyDefaults()
{
	Globals::bCreativeEnabled = false;
	Globals::bSTWEnabled = false;
	Globals::bEventEnabled = false;
	Globals::bBossesEnabled = true;
	Globals::bBotsEnabled = true;
	Globals::LateGame = false;
	Globals::BattleLab = false;
	Globals::Blitz = false;
	Globals::StormKing = false;
	Globals::Arsenal = false;
	Globals::TeamRumble = false;
	Globals::SolidGold = false;
	Globals::UnVaulted = false;
	Globals::Siphon = false;
	Globals::Arena = false;
}

void SelectStandard()
{
	ApplyDefaults();
	bSoloDefaults = true;
}

void Select(const char* Label)
{
	ApplyDefaults();
	bSoloDefaults = false;

	if (!strcmp(Label, "Creative / Playground"))
		Globals::bCreativeEnabled = true;
	else if (!strcmp(Label, "Save The World"))
		Globals::bSTWEnabled = true;
	else if (!strcmp(Label, "Late Game"))
		Globals::LateGame = true;
	else if (!strcmp(Label, "Battle Lab"))
		Globals::BattleLab = true;
	else if (!strcmp(Label, "Blitz"))
		Globals::Blitz = true;
	else if (!strcmp(Label, "Storm King"))
	{ Globals::StormKing = true; Globals::bBotsEnabled = false; }
	else if (!strcmp(Label, "Arsenal"))
		Globals::Arsenal = true;
	else if (!strcmp(Label, "Team Rumble"))
		Globals::TeamRumble = true;
	else if (!strcmp(Label, "Solid Gold"))
		Globals::SolidGold = true;
	else if (!strcmp(Label, "Unvaulted"))
		Globals::UnVaulted = true;
	else if (!strcmp(Label, "Siphon"))
		Globals::Siphon = true;
	else if (!strcmp(Label, "Arena"))
		Globals::Arena = true;
	else if (!strcmp(Label, "Event (Music)"))
		Globals::bEventEnabled = true;
}

const char* const kModes[] = {
	"Standard Battle Royale",
	"Creative / Playground",
	"Save The World",
	"Late Game",
	"Battle Lab",
	"Blitz",
	"Storm King",
	"Arsenal",
	"Team Rumble",
	"Solid Gold",
	"Unvaulted",
	"Siphon",
	"Arena",
	"Event (Music)",
};

int SelectedMode = 0;

void JoinByIp(const char* Ip)
{
	if (!Ip || !*Ip)
		return;

	Log(std::string("Gui: joining player at IP ") + Ip);

	// UE client travel to the host's listen server (open <ip>:7777)
	char Cmd[96];
	snprintf(Cmd, sizeof(Cmd), "open %s:%u", Ip, (unsigned)ListenServer::Port);

	std::u16string Wide(Cmd, Cmd + strlen(Cmd));
	UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), Wide.c_str(), nullptr);

	Log(std::string("Gui: travel command issued — ") + Cmd);
}

} // namespace

bool RenderMenuFrame()
{
	if (ConsumeToggleRequest())
	{
		// the gesture toggles the menu; log for on-device debugging
		Log(bMenuOpen ? "Gui: menu OPEN" : "Gui: menu CLOSED");
	}

	if (!bMenuOpen)
		return true;

	ImGuiIO& Io = ImGui::GetIO();

	ImGui::SetNextWindowPos(ImVec2(Io.DisplaySize.x * 0.5f, Io.DisplaySize.y * 0.5f),
		ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(Min(Io.DisplaySize.x * 0.92f, 720.0f),
		Min(Io.DisplaySize.y * 0.85f, 900.0f)),
		ImGuiCond_FirstUseEver);

	if (!ImGui::Begin("OGS 12.41 — Android Game Server", nullptr,
		ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::End();
		return true;
	}

	ImGui::TextUnformatted("Fortnite 12.41 (UE 4.25) — Listen Server (host & play)");
	ImGui::Separator();

	if (ImGui::BeginTabBar("##tabs"))
	{
		// ─── Tab: choose a mode ───────────────────────────────────────────
		if (ImGui::BeginTabItem("Choose a Mode"))
		{
			ImGui::TextUnformatted("Pick any game mode for this match:");
			ImGui::Spacing();

			for (int i = 0; i < (int)(sizeof(kModes) / sizeof(kModes[0])); ++i)
			{
				if (ImGui::Selectable(kModes[i], SelectedMode == i))
					SelectedMode = i;
			}

			ImGui::Spacing();
			ImGui::Separator();

			ImGui::TextUnformatted("Options:");
			ImGui::Checkbox("Enable bots", &Globals::bBotsEnabled);
			ImGui::SameLine();
			ImGui::Checkbox("Enable bosses", &Globals::bBossesEnabled);
			ImGui::SliderInt("Bots to spawn", &Globals::MaxBotsToSpawn, 0, 200);
			ImGui::SliderInt("Players for early start", &Globals::MinPlayersForEarlyStart, 1, 200);
			ImGui::Checkbox("Automatics", &Globals::Automatics);

			ImGui::Spacing();
			if (ImGui::Button("Start Match", ImVec2(200, 60)))
			{
				Select(kModes[SelectedMode]);
				bJoinMode = false;
				ListenServer::Setup(); // host mode — listen for joiners
				bMenuOpen = false;
				Log(std::string("Gui: match started — mode: ") + kModes[SelectedMode]);
			}

			ImGui::EndTabItem();
		}

		// ─── Tab: join another player by IP ──────────────────────────────
		if (ImGui::BeginTabItem("Join a Player"))
		{
			ImGui::TextUnformatted("Enter the other player's device IP to join their match:");
			ImGui::Spacing();

			static char IpBuf[64] = "";
			ImGui::SetNextItemWidth(-1.0f);
			ImGui::InputTextWithHint("##ip", "e.g. 192.168.1.42", IpBuf, sizeof(IpBuf));
			ImGui::Spacing();

			ImGui::TextUnformatted("The host must have OGS running (port 7777).");
			ImGui::Spacing();

			if (ImGui::Button("Join", ImVec2(200, 60)))
			{
				snprintf(JoinIp, sizeof(JoinIp), "%s", IpBuf);
				if (*JoinIp)
				{
					bJoinMode = true;
					JoinByIp(JoinIp);
					bMenuOpen = false;
				}
			}

			ImGui::EndTabItem();
		}

		// ─── Tab: server status ──────────────────────────────────────────
		if (ImGui::BeginTabItem("Status"))
		{
			ImGui::Text("Listen server: %s", ListenServer::bActive ? "ACTIVE (0.0.0.0:7777)" : "inactive");
			ImGui::Text("GIsClient: %s   GIsServer: %s",
				GIs::Client ? (*GIs::Client ? "true" : "false") : "?",
				GIs::Server ? (*GIs::Server ? "true" : "false") : "?");
			ImGui::Text("Mode: %s", kModes[SelectedMode]);
			ImGui::TextUnformatted("Open/close this menu: hold 4 fingers on the screen for 2 seconds.");
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}

	ImGui::End();
	return true;
}

} // namespace Gui
