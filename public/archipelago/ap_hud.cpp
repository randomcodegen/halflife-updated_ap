#include "ap_integration.h"
#include "ap_hud.h"

#include "Exports.h"
#include "cl_util.h"
#include "parsemsg.h"
#include "triangleapi.h"
#include "com_model.h"
// [ap] Other dependencies
#include "pmtrace.h"
#include "pm_defs.h"
#include "filesystem_utils.h"
#include <windows.h>
#include <algorithm>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_opengl2.h>

extern IFileSystem* g_pFileSystem;

static std::vector<std::string> mapList;

void LoadMaps()
{
	static bool initialized = false;
	
	static float nextChangeTime = 0.0f;
	static int currentIndex = 0;
	static bool mapMatched = false;
	static std::string lastMapName;

	float curtime = gEngfuncs.GetClientTime();

	// Load map list once
	if (!initialized)
	{
		FileFindHandle_t handle = FILESYSTEM_INVALID_FIND_HANDLE;
		const char* fileName = g_pFileSystem->FindFirst("maps/*.bsp", &handle);
		if (fileName != nullptr)
		{
			do
			{
				std::string mapName = fileName;

				// Remove .bsp suffix
				if (mapName.size() >= 4 && mapName.compare(mapName.size() - 4, 4, ".bsp") == 0)
					mapName.resize(mapName.size() - 4);

				// Avoid duplicates
				auto it = std::find_if(mapList.begin(), mapList.end(),
					[&](const std::string& candidate)
					{ return _stricmp(candidate.c_str(), mapName.c_str()) == 0; });

				if (it == mapList.end())
					mapList.push_back(std::move(mapName));

			} while ((fileName = g_pFileSystem->FindNext(handle)) != nullptr);

			g_pFileSystem->FindClose(handle);
		}

		initialized = true;
	}

	if (AP_DUMP_EDICT) {
		// Get and normalize current map name
		std::string currentMap = gEngfuncs.pfnGetLevelName();

		const char* prefix = "maps/";
		if (_strnicmp(currentMap.c_str(), prefix, strlen(prefix)) == 0)
			currentMap = currentMap.substr(strlen(prefix));

		if (currentMap.size() >= 4 && _stricmp(currentMap.c_str() + currentMap.size() - 4, ".bsp") == 0)
			currentMap.resize(currentMap.size() - 4);

		// Detect map change
		if (_stricmp(currentMap.c_str(), lastMapName.c_str()) != 0)
		{
			mapMatched = false;		  // new map loaded
			lastMapName = currentMap; // update last seen map
		}

		// Run matching logic once per map
		if (!mapMatched)
		{
			auto it = std::find_if(mapList.begin(), mapList.end(),
				[&](const std::string& m)
				{ return _stricmp(m.c_str(), currentMap.c_str()) == 0; });

			if (it != mapList.end())
				currentIndex = static_cast<int>(it - mapList.begin()) + 1; // next map
			else
				currentIndex = 0; // fallback

			nextChangeTime = curtime + 3.0f; // set delay
			mapMatched = true;
		}

		// Execute the changelevel command after delay
		if (mapMatched && curtime >= nextChangeTime)
		{
			if (currentIndex < static_cast<int>(mapList.size()))
			{
				char cmd[256];
				snprintf(cmd, sizeof(cmd), "changelevel %s", mapList[currentIndex].c_str());
				gEngfuncs.pfnClientCmd(cmd);
			}
		}
	}
}


/*

	AP Console Overwrite for Multiline support

*/

bool CHudMultiNotify::Init()
{
	gHUD.AddHudElem(this);
	m_iFlags |= HUD_ACTIVE;
	return true;
}

bool CHudMultiNotify::VidInit()
{
	return true;
}

void CHudMultiNotify::Reset()
{

}

void CHudMultiNotify::AddMessage(const char* pszMessage)
{
	if (m_Lines.size() >= MAX_NOTIFY_LINES)
		m_Lines.erase(m_Lines.begin()); // Remove oldest line

	NotifyLine line;
	line.text = pszMessage;
	line.timeAdded = gHUD.m_flTime;

	m_Lines.push_back(line);
}

float m_flLastDrawTime;

bool CHudMultiNotify::Draw(float flTime)
{
	int y = 20;
	const int lineHeight = 12;

	// Detect level load by checking for time "jumping backward"
	if (flTime < m_flLastDrawTime - 1.0f) // Allow a second of wiggle room
	{
		float offset = m_flLastDrawTime - flTime;
		for (auto& line : m_Lines)
		{
			line.timeAdded -= offset;
		}
	}
	m_flLastDrawTime = flTime; // Save for next frame

	for (auto it = m_Lines.begin(); it != m_Lines.end();)
	{
		float timeAlive = flTime - it->timeAdded;

		if (timeAlive >= NOTIFY_LINE_LIFETIME)
		{
			// Remove expired line
			it = m_Lines.erase(it);
			continue;
		}

		// Calculate fade factor (1 = full bright, 0 = invisible)
		float fadeFactor = 1.0f - (timeAlive / NOTIFY_LINE_LIFETIME);

		// Clamp fadeFactor to [0, 1]
		if (fadeFactor < 0.0f)
			fadeFactor = 0.0f;
		if (fadeFactor > 1.0f)
			fadeFactor = 1.0f;

		// Calculate RGB values by scaling base white color by fadeFactor
		int r = static_cast<int>(255 * fadeFactor);
		int g = static_cast<int>(255 * fadeFactor);
		int b = static_cast<int>(255 * fadeFactor);

		gHUD.DrawHudString(10, y, ScreenWidth - 20, it->text.c_str(), r, g, b);

		y += lineHeight;
		++it;
	}

	return true;
}

/*

	AP Text HUD Functions

*/
bool CHudAPText::Init(void)
{
	gHUD.AddHudElem(this);
	m_iFlags |= HUD_ACTIVE;
	return true;
}

ImGuiContext* imgui_ctx;

/**/
void SetupMenuStyle()
{
	ImGuiStyle& style = ImGui::GetStyle();
	ImVec4* colors = style.Colors;

	// Base colors
	colors[ImGuiCol_Text] = ImVec4(0.95f, 0.85f, 0.55f, 1.00f); // warm yellow text
	colors[ImGuiCol_TextDisabled] = ImVec4(0.60f, 0.55f, 0.35f, 1.00f);

	// Transparent dark background with a hint of brown/gray
	colors[ImGuiCol_WindowBg] = ImVec4(0.05f, 0.05f, 0.03f, 0.85f);
	colors[ImGuiCol_ChildBg] = ImVec4(0.07f, 0.07f, 0.04f, 0.85f);
	colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.05f, 0.85f);

	// Borders & separators — dark orange/brown
	colors[ImGuiCol_Border] = ImVec4(0.50f, 0.40f, 0.15f, 0.75f);
	colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	colors[ImGuiCol_Separator] = colors[ImGuiCol_Border];
	colors[ImGuiCol_SeparatorHovered] = ImVec4(0.75f, 0.65f, 0.25f, 1.00f);
	colors[ImGuiCol_SeparatorActive] = ImVec4(0.85f, 0.75f, 0.30f, 1.00f);

	// Headers (collapsing headers, etc) — warm orange
	colors[ImGuiCol_Header] = ImVec4(0.45f, 0.35f, 0.10f, 1.00f);
	colors[ImGuiCol_HeaderHovered] = ImVec4(0.70f, 0.60f, 0.20f, 1.00f);
	colors[ImGuiCol_HeaderActive] = ImVec4(0.60f, 0.50f, 0.15f, 1.00f);

	// Buttons — medium brown/orange with hover glow
	colors[ImGuiCol_Button] = ImVec4(0.35f, 0.25f, 0.08f, 1.00f);
	colors[ImGuiCol_ButtonHovered] = ImVec4(0.75f, 0.60f, 0.25f, 1.00f);
	colors[ImGuiCol_ButtonActive] = ImVec4(0.60f, 0.50f, 0.15f, 1.00f);

	// Frames (input boxes, sliders, etc) — dark, transparent brown with highlight
	colors[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.12f, 0.06f, 0.55f);
	colors[ImGuiCol_FrameBgHovered] = ImVec4(0.35f, 0.30f, 0.10f, 0.80f);
	colors[ImGuiCol_FrameBgActive] = ImVec4(0.50f, 0.45f, 0.15f, 1.00f);

	// Tabs
	colors[ImGuiCol_Tab] = ImVec4(0.15f, 0.12f, 0.06f, 0.80f);
	colors[ImGuiCol_TabHovered] = ImVec4(0.60f, 0.50f, 0.15f, 1.00f);
	colors[ImGuiCol_TabActive] = ImVec4(0.75f, 0.65f, 0.25f, 1.00f);
	colors[ImGuiCol_TabUnfocused] = ImVec4(0.10f, 0.08f, 0.04f, 0.50f);
	colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.30f, 0.25f, 0.10f, 0.90f);

	// Titles
	colors[ImGuiCol_TitleBg] = ImVec4(0.45f, 0.35f, 0.10f, 1.00f);
	colors[ImGuiCol_TitleBgActive] = ImVec4(0.65f, 0.55f, 0.20f, 1.00f);
	colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.15f, 0.12f, 0.06f, 0.60f);

	// Scrollbar
	colors[ImGuiCol_ScrollbarBg] = ImVec4(0.10f, 0.08f, 0.04f, 0.60f);
	colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.45f, 0.35f, 0.10f, 0.90f);
	colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.75f, 0.65f, 0.25f, 1.00f);
	colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.85f, 0.75f, 0.30f, 1.00f);

	// Checkmarks, sliders, etc
	colors[ImGuiCol_CheckMark] = ImVec4(0.75f, 0.65f, 0.25f, 1.00f);
	colors[ImGuiCol_SliderGrab] = ImVec4(0.60f, 0.50f, 0.15f, 1.00f);
	colors[ImGuiCol_SliderGrabActive] = ImVec4(0.75f, 0.65f, 0.25f, 1.00f);

	// Modal windows
	colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.65f);

	// Resize grips
	colors[ImGuiCol_ResizeGrip] = ImVec4(0.45f, 0.35f, 0.10f, 0.85f);
	colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.75f, 0.65f, 0.25f, 1.00f);
	colors[ImGuiCol_ResizeGripActive] = ImVec4(0.85f, 0.75f, 0.30f, 1.00f);

	// Rounding & padding for that smooth UI feel
	style.WindowRounding = 5.0f;
	style.FrameRounding = 4.0f;
	style.GrabRounding = 4.0f;
	style.ScrollbarRounding = 3.0f;
	style.ChildRounding = 4.0f;

	style.WindowBorderSize = 1.0f;
	style.FrameBorderSize = 1.0f;

	style.WindowPadding = ImVec2(10, 10);
	style.FramePadding = ImVec2(6, 4);
	style.ItemSpacing = ImVec2(8, 6);
	style.ItemInnerSpacing = ImVec2(6, 4);
	style.TouchExtraPadding = ImVec2(0, 0);

	style.GrabMinSize = 10.0f;
	style.ScrollbarSize = 14.0f;

	ImGuiIO& io = ImGui::GetIO();
	io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 16.0f);
}



bool CHudAPText::VidInit(void)
{
	g_TriggerZones.clear();
	menuOpen = false;
	imgui_ctx = ImGui::CreateContext();
	ImGui::SetCurrentContext(imgui_ctx);
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	(void)io;

	// Setup ImGui style
	ImGui::StyleColorsDark();

	// Initialize platform and renderer bindings
	HWND hwnd = GetActiveWindow();
	ImGui_ImplWin32_Init(hwnd);
	ImGui_ImplOpenGL2_Init();

	SetupMenuStyle();
	
	return true;
}

void CHudAPText::Reset()
{
}

void CHudAPText::DrawCustomCrosshair()
{
	int screenWidth = ScreenWidth;
	int screenHeight = ScreenHeight;
	int x = screenWidth / 2;
	int y = screenHeight / 2;

	int r = 255, g = 0, b = 0; // Red color
	int crosshairSize = 4;

	// Simple cross: vertical and horizontal lines
	FillRGBA(x - crosshairSize, y, crosshairSize * 2 + 1, 1, r, g, b, 255); // horizontal
	FillRGBA(x, y - crosshairSize, 1, crosshairSize * 2 + 1, r, g, b, 255); // vertical
}

static int selectedIndex = -1;

bool menuOpen = false;

void UpdateMouseState()
{
	ImGuiIO& io = ImGui::GetIO();
	if (menuOpen)
	{
		io.MouseDrawCursor = true;
		ClipCursor(nullptr);
		POINT mouse_pos;
		GetCursorPos(&mouse_pos);
		io.MousePos = ImVec2((float)mouse_pos.x, (float)mouse_pos.y);

		// Update mouse button states (using WinAPI)
		io.MouseDown[0] = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0; // Left click
		io.MouseDown[1] = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0; // Right click
	}
	else
	{
		io.MouseDrawCursor = false;
		// Lock mouse back to game window
		HWND hwnd = GetActiveWindow();
		RECT rect;
		GetClientRect(hwnd, &rect);
		MapWindowPoints(hwnd, nullptr, (POINT*)&rect, 2);
		ClipCursor(&rect);
	}
}

void ShowMapMenu()
{
	menuOpen = true;
	LoadMaps();
	IN_DeactivateMouse();
	
	ImGuiStyle& style = ImGui::GetStyle();


	ImGui::Begin("Map Menu", nullptr,
		ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_AlwaysAutoResize);

	ImGui::Text("Select a map to load:");

	// Begin child region to get scrollbar if many items
	ImGui::BeginChild("MapListChild", ImVec2(300, 200), 1);

	for (int i = 0; i < (int)mapList.size(); i++)
	{
		if (ImGui::Selectable(mapList[i].c_str(), selectedIndex == i))
		{
			selectedIndex = i;

			// Immediately load the selected map
			char cmd[256];
			snprintf(cmd, sizeof(cmd), "map %s", mapList[i].c_str());
			gEngfuncs.pfnClientCmd(cmd);
		}
	}

	ImGui::EndChild();

	if (ImGui::Button("Cancel", ImVec2(-1, 0)))
	{
		// TODO: Hide menu or whatever
		selectedIndex = -1;
	}

	ImGui::End();
}

bool CHudAPText::Draw(float flTime)
{
	if (menuOpen)
		gEngfuncs.pfnSetMouseEnable(0);
	const char* mapName = gEngfuncs.pfnGetLevelName();
	UpdateMouseState();
	if (AP_DUMP_EDICT)
		LoadMaps();
	else if (mapName && strcmp(mapName, "maps/menu.bsp") == 0) {
		ImGui_ImplOpenGL2_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		ShowMapMenu();

		// Rendering
		ImGui::Render();
		ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
	}
	
	char szText[128];
	snprintf(szText, sizeof(szText),
		"JUMP:%c  CROU:%c  RUN:%c  DIVE:%c\n"
		"DOOR:%c  USE:%c  GRAB:%c  BRK:%c",
		ap_can_jump() ? '1' : '0',
		ap_can_crouch() ? '1' : '0',
		ap_can_run() ? '1' : '0',
		ap_can_dive() ? '1' : '0',		
		ap_can_door() ? '1' : '0',
		ap_can_use() ? '1' : '0',		
		ap_can_grab() ? '1' : '0',
		ap_can_break() ? '1' : '0');

	gEngfuncs.pfnGetScreenInfo(&gHUD.m_scrinfo);
	int screenWidth = gHUD.m_scrinfo.iWidth;

	// Split the text by lines
	char szTextCopy[256];
	strncpy(szTextCopy, szText, sizeof(szTextCopy) - 1);
	szTextCopy[sizeof(szTextCopy) - 1] = '\0';

	int maxLineWidth = 0;
	int totalLines = 0;
	char* line = strtok(szTextCopy, "\n");

	while (line)
	{
		int lineWidth = ConsoleStringLen(line);
		if (lineWidth > maxLineWidth)
			maxLineWidth = lineWidth;

		totalLines++;
		line = strtok(nullptr, "\n");
	}

	int lineHeight = 13; // Approx. font height, TODO: maybe use 'h' from above
	int totalHeight = lineHeight * totalLines;

	int x = screenWidth - maxLineWidth - 10;
	int y = 10;

	// Draw lines again (we need to re-split original string)
	line = strtok(szText, "\n");
	while (line)
	{
		// Set text color to white
		gEngfuncs.pfnDrawSetTextColor(1.0f, 1.0f, 1.0f);
		gEngfuncs.pfnDrawConsoleString(x, y, line);
		y += lineHeight;
		line = strtok(nullptr, "\n");
	}

	if (ap_hud_crosshair && ap_hud_crosshair->value == 1)
		DrawCustomCrosshair();

	return true;
}

// feature from bunnymodxt
bool CHudSpeedometer::Init()
{
	gHUD.AddHudElem(this);
	m_iFlags |= HUD_ACTIVE;

	return true;
}

// Assuming these constants and arrays exist like in your original code:
int NumberWidth;
int NumberHeight;

constexpr double FADE_DURATION_JUMPSPEED = 1.0;
int hudColor[3] = {255, 255, 255};

HLSPRITE hudNumberSprite;
HLSPRITE NumberSprites[10] = {0}; // array of digit sprites 0-9
const Rect* NumberSpriteRects[10] = {nullptr};
client_sprite_t* NumberSpritePointers[10] = {nullptr};

bool CHudSpeedometer::VidInit()
{
	// Use "number_0" style format: resolution starts with 1280 (your txt)
	int spriteRes = ScreenWidth; // use current screen width

	int count = 0;
	client_sprite_t* SpriteList = gEngfuncs.pfnSPR_GetList("sprites/hud.txt", &count);
	if (!SpriteList)
		return false;

	for (int i = 0; i < count; i++)
	{
		client_sprite_t* spr = &SpriteList[i];

		if (strncmp(spr->szName, "number_", 7) != 0 || spr->szName[8] != '\0')
			continue;

		char c = spr->szName[7];
		if (c < '0' || c > '9')
			continue;
		int d = c - '0';

		NumberSpritePointers[d] = spr;
		NumberSpriteRects[d] = &spr->rc; // pointer, no copy

		// Load the sprite by name (sprites/hud_number_d.spr)
		std::string path = "sprites/";
		path += spr->szSprite;
		path += ".spr";

		NumberSprites[d] = gEngfuncs.pfnSPR_Load(path.c_str());
		if (d == 0)
		{
			NumberWidth = spr->rc.right - spr->rc.left;
			NumberHeight = spr->rc.bottom - spr->rc.top;
		}
	}
	return true;
}

static void DrawDigit(int digit, int x, int y, int r, int g, int b)
{
	assert(digit >= 0 && digit <= 9);

	// Set sprite color
	SPR_Set(NumberSprites[digit], r, g, b);

	// Draw the digit sprite at (x, y)
	SPR_DrawAdditive(0, x, y, NumberSpriteRects[digit]);
}

int DrawNumber(int number, int x, int y, int r, int g, int b, int fieldMinWidth = 1)
{
	if (number < 0)
	{
		if (number == (std::numeric_limits<int>::min)())
		{
			number = 0;
		}
		else
		{
			number = abs(number);
		}
	}

	int digits[10] = {0};
	int digitCount = 0;

	if (number == 0)
	{
		digits[0] = 0;
		digitCount = 1;
	}
	else
	{
		// Extract digits from least significant to most
		int temp = number;
		while (temp > 0 && digitCount < 10)
		{
			digits[digitCount++] = temp % 10;
			temp /= 10;
		}
	}

	// Ensure minimum field width by padding with zeros
	while (digitCount < fieldMinWidth)
	{
		digits[digitCount++] = 0;
	}

	// Draw digits from most significant to least (reverse order)
	for (int i = digitCount - 1; i >= 0; --i)
	{
		DrawDigit(digits[i], x, y, r, g, b);
		x += NumberWidth; // move right for next digit
	}

	return x; // return new x position after drawing number
}



bool CHudSpeedometer::Draw(float flTime)
{
	if (ap_hud_speedometer && ap_hud_speedometer->value == 0.0f)
		return true;


	// Constants
	const int HL_YELLOW[3] = {255, 255, 0};			// Half-Life HUD yellow
	constexpr float FADE_DURATION_JUMPSPEED = 1.0f; // fade duration seconds

	cl_entity_t* local = gEngfuncs.GetLocalPlayer();
	if (!local)
		return true;

	Vector currentPos = local->origin;

	// Static vars to persist state
	static Vector lastPos = {0, 0, 0};
	static float lastTime = flTime;
	static double passedTime = FADE_DURATION_JUMPSPEED;
	static int fadingFrom[3] = {HL_YELLOW[0], HL_YELLOW[1], HL_YELLOW[2]};
	static double jumpSpeed = 0.0;
	static float lastVelZ = 0.0f;
	static bool firstFrame = true;

	if (firstFrame || flTime <= lastTime)
	{
		lastPos = currentPos;
		lastTime = flTime;
		firstFrame = false;
		return true;
	}

	float deltaTime = flTime - lastTime;
	if (deltaTime <= 0.0f)
		deltaTime = 0.01f;

	Vector velocity = (currentPos - lastPos) / deltaTime;
	lastPos = currentPos;

	float horizontalSpeed = sqrt(velocity.x * velocity.x + velocity.y * velocity.y);
	float currentVelZ = velocity.z;

	// Jump speed detection + color flash logic
	if ((currentVelZ != 0.0f && lastVelZ == 0.0f) || (currentVelZ > 0.0f && lastVelZ < 0.0f))
	{
		double speed = horizontalSpeed;
		double diff = speed - jumpSpeed;

		if (diff != 0.0)
		{
			if (diff > 0.0)
			{
				// Green flash (speed increased on jump)
				fadingFrom[0] = 0;
				fadingFrom[1] = 255;
				fadingFrom[2] = 0;
			}
			else
			{
				// Red flash (speed decreased on jump)
				fadingFrom[0] = 255;
				fadingFrom[1] = 0;
				fadingFrom[2] = 0;
			}

			passedTime = 0.0;
			jumpSpeed = speed;
		}
	}

	// Update fade timer
	float timeDelta = (std::max)(flTime - lastTime, 0.0f);
	passedTime += timeDelta;
	if (passedTime > FADE_DURATION_JUMPSPEED || !std::isnormal(passedTime))
		passedTime = FADE_DURATION_JUMPSPEED;

	// Compute color for jump speed number (fades from green/red to yellow)
	int rJump, gJump, bJump;
	if (passedTime < FADE_DURATION_JUMPSPEED)
	{
		float fadeFactor = static_cast<float>(passedTime / FADE_DURATION_JUMPSPEED);
		rJump = static_cast<int>(fadingFrom[0] + (HL_YELLOW[0] - fadingFrom[0]) * fadeFactor);
		gJump = static_cast<int>(fadingFrom[1] + (HL_YELLOW[1] - fadingFrom[1]) * fadeFactor);
		bJump = static_cast<int>(fadingFrom[2] + (HL_YELLOW[2] - fadingFrom[2]) * fadeFactor);
	}
	else
	{
		rJump = HL_YELLOW[0];
		gJump = HL_YELLOW[1];
		bJump = HL_YELLOW[2];
	}

	// Calculate width and positions
	auto calcWidth = [](int speedVal)
	{
		int digits = (speedVal < 1) ? 1 : static_cast<int>(log10f(speedVal)) + 1;
		return digits * NumberWidth;
	};

	int jumpSpeedInt = static_cast<int>(trunc(jumpSpeed));
	int normalSpeedInt = static_cast<int>(trunc(horizontalSpeed));

	int jumpWidth = calcWidth(jumpSpeedInt);
	int normalWidth = calcWidth(normalSpeedInt);

	// Draw jump speed on top (centered)
	int xJump = (ScreenWidth / 2) - (jumpWidth / 2);
	int yJump = ScreenHeight - NumberHeight * 2 - 14; // 14 px padding between numbers

	DrawNumber(jumpSpeedInt, xJump, yJump, rJump, gJump, bJump);

	// Draw normal speed below jump speed, always yellow
	int xNormal = (ScreenWidth / 2) - (normalWidth / 2);
	int yNormal = ScreenHeight - NumberHeight - 10;

	DrawNumber(normalSpeedInt, xNormal, yNormal, HL_YELLOW[0], HL_YELLOW[1], HL_YELLOW[2]);

	lastTime = flTime;
	lastVelZ = currentVelZ;

	return true;
}