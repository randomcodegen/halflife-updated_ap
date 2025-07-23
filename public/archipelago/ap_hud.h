#pragma once
#include "hud.h"
#include <string>
#include <vector>

#define MAX_NOTIFY_LINES 8
#define NOTIFY_LINE_LIFETIME 5.0f // Seconds on screen

extern cvar_t* ap_hud_speedometer;
extern cvar_t* ap_hud_draw_triggers;
extern cvar_t* ap_hud_crosshair;

extern bool menuOpen;

struct NotifyLine
{
	std::string text;
	float timeAdded;
};

class CHudMultiNotify : public CHudBase
{
public:
	bool Init();
	bool VidInit();
	void Reset();
	bool Draw(float flTime);

	void AddMessage(const char* pszMessage);

private:
	std::vector<NotifyLine> m_Lines;
};

// feature from bunnymodxt
class CHudSpeedometer : public CHudBase
{
public:
	bool Init() override;
	bool VidInit() override;
	void Reset() override {}
	bool Draw(float flTime) override;
};

class CHudAPText : public CHudBase
{
public:
	bool Init();
	bool VidInit();
	bool Draw(float flTime);
	void Reset();

	bool InitImGui();
	void ToggleMenu();
	bool IsMenuOpen() const;
	void DrawCustomCrosshair();
	int HookedSwapBuffers(void* hdc);
	void RenderMenu();

private:
	HLSPRITE m_hSprite;
};
extern CHudAPText g_APHud;

class CHudChangelevelText : public CHudBase
{
public:
	bool Init();
	void SetText(const char* text);
	bool Draw(float time);

private:
	char m_szText[64];
	float m_flDisplayTime;
};