#include "ap_integration.h"
#include <fstream>
#include <windows.h>
#include <string>
#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include <xxhash.h>

uint8_t ap_return_to_menu = 0;

std::string ap_message_str;

#ifdef CLIENT_DLL
#define ACTIVE_PLAYER (CBaseEntity*)nullptr
#else
#define ACTIVE_PLAYER UTIL_PlayerByIndex(1)
#endif

std::string generate_hash(float x, float y, float z, const char* classname)
{
	struct
	{
		float x, y, z;
		char name[64];
	} data;

	data.x = x;
	data.y = y;
	data.z = z;
	strncpy(data.name, classname, sizeof(data.name));
	data.name[sizeof(data.name) - 1] = 0;

	uint64_t hash = XXH64(&data, sizeof(data), AP_GOLD_ID_PREFIX);
	return std::to_string(hash);
}

static inline ap_location_t safe_location_id(Json::Value& val)
{
    if (val.isInt())
        return AP_SHORT_LOCATION(val.asInt());
    return -1;
}

std::string current_map = "";

std::string get_dll_directory()
{
	char path[MAX_PATH];
	HMODULE hModule = nullptr;

	if (GetModuleHandleEx(
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
				GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCSTR>(&get_dll_directory), &hModule))
	{
		if (GetModuleFileNameA(hModule, path, MAX_PATH))
		{
			std::string fullPath(path);
			size_t pos = fullPath.find_last_of("\\/");
			if (pos != std::string::npos)
			{
				return fullPath.substr(0, pos); // directory only
			}
		}
	}

	return "";
}


/*
  Load correct game data based on configured AP settings
*/
bool load_settings(const char* filename, ap_connection_settings_t& settings)
{

	std::string basePath = get_dll_directory();
	std::string path = basePath + "\\..\\" + filename;

	std::ifstream file(path.c_str(), std::ifstream::binary);
	if (!file.is_open())
	{
		AP_Errorf("Failed to open %s\n", path.c_str());
		return false;
	}

	Json::Value root;
	Json::CharReaderBuilder builder;
	std::string errs;

	bool parsingSuccessful = Json::parseFromStream(builder, file, &root, &errs);
	if (!parsingSuccessful)
	{
		AP_Errorf("Failed to parse JSON: %s\n", errs.c_str());
		return false;
	}

	if (root.isMember("ip") && root["ip"].isString())
		settings.ip = strdup(root["ip"].asCString());

	if (root.isMember("player") && root["player"].isString())
		settings.player = strdup(root["player"].asCString());

	if (root.isMember("password") && root["password"].isString())
		settings.password = strdup(root["password"].asCString());

    ap_connection_settings.mode = AP_SERVER;

    return true;
}

void ap_startup(void)
{
    // [AP] Get connection settings from user defs
    ap_connection_settings.mode = AP_DISABLED;
	if (load_settings("ap_connect_info.json", ap_connection_settings))
    {
        AP_Printf("Connecting to AP Server " + std::string(ap_connection_settings.ip) + " as " + std::string(ap_connection_settings.player));
    }
    if (ap_connection_settings.mode == AP_DISABLED)
    {
        AP_Printf("No AP connection settings defined; Launching vanilla game.");
        return;
    }
}

Json::Value read_json_from_file(const char* filename)
{
	std::string basePath = get_dll_directory();
	std::string fullPath = basePath + "\\..\\" + filename;

	std::ifstream file(fullPath, std::ifstream::binary);
	if (!file.is_open())
	{
		return Json::Value();
	}

	Json::Value ret;
	Json::CharReaderBuilder builder;
	std::string errs;

	bool parsingSuccessful = Json::parseFromStream(builder, file, &ret, &errs);
	if (!parsingSuccessful)
	{
		// Handle parsing errors
		return Json::Value();
	}

	return ret;
}

std::string ap_episode_names[MAXVOLUMES] = { "" };
std::map<std::string, Json::Value> ap_level_data;

void ap_parse_levels()
{
    ap_level_data.clear();

    // Iterate over all episodes defined in the game config
    // JSON iteration can be out of order, use flags to keep things sequential
    Json::Value tmp_levels[MAXVOLUMES][MAXLEVELS] = { 0 };

    for (std::string ep_id : ap_game_config["episodes"].getMemberNames())
    {
        uint8_t volume = ap_game_config["episodes"][ep_id]["volumenum"].asInt();
        ap_episode_names[volume] = ap_game_config["episodes"][ep_id]["name"].asString();
        for (std::string lev_id : ap_game_config["episodes"][ep_id]["levels"].getMemberNames())
        {
            uint8_t levelnum = ap_game_config["episodes"][ep_id]["levels"][lev_id]["levelnum"].asInt();
            ap_net_id_t unlock = ap_game_config["episodes"][ep_id]["levels"][lev_id]["unlock"].asInt64();
			std::string mapfile = ap_game_config["episodes"][ep_id]["levels"][lev_id]["mapfile"].asString();

            if (AP_IsLevelUsed(unlock))
            {
                ap_level_data[mapfile] = ap_game_config["episodes"][ep_id]["levels"][lev_id];
            }
        }
    }
}

ap_connection_settings_t ap_connection_settings = {AP_DISABLED, "", "", "", ""};

bool ap_initialize(void)
{
    if (ap_connection_settings.mode == AP_DISABLED) return false;
    Json::Value game_ap_config = read_json_from_file("ap_config.json");
    if (game_ap_config.isNull()) return false;

    ap_connection_settings.game = game_ap_config["game"].asCString();
	if (!AP_Initialize(game_ap_config, ap_connection_settings))
		return false;

    if (AP)
    {
        // Additional initializations after the archipelago setup is done
        ap_parse_levels();
    }
	return true;
}

// Safe check if an item is scoped to a level
static inline bool item_for_level(Json::Value& info, uint8_t level, uint8_t volume)
{
    return (info["levelnum"].isInt() && (info["levelnum"].asInt() == level)) && (info["volumenum"].isInt() && (info["volumenum"].asInt() == volume));
}

static inline bool item_for_current_level(Json::Value& info)
{
    // TODO:
    //return item_for_level(info, ud.level_number, ud.volume_number);
	return false;
}

static inline int64_t json_get_int(Json::Value& val, int64_t def)
{
    return val.isInt() ? val.asInt() : def;
}

// Track inventory unlock state. There might be a better solution to this?
static uint8_t inv_available[INV_MAX];
static uint16_t inv_capacity[INV_MAX];
static uint16_t inv_max_capacity[INV_MAX];
static std::map<std::string, uint8_t> ability_unlocks;



/* 
  Apply whatever item we just got to our current game state 

  Upgrade only provides just the unlock, but no ammo/capacity. This is used
  when loading savegames.
*/

static void ap_get_item(ap_net_id_t item_id, bool silent, bool is_new)
{
    Json::Value item_info = ap_item_info[item_id];
    // Check if we have a dynamic override for the item in our seed slot data
    if (!ap_game_settings["dynamic"][std::to_string(item_id)].isNull())
        item_info = ap_game_settings["dynamic"][std::to_string(item_id)];
    bool notify = !(silent || item_info["silent"].asBool());

    // Store counts for stateful items
    if (is_new && item_info["persistent"].asBool())
    {
        uint16_t count = 0;
        if (AP_HasItem(item_id))
        {
            count = ap_game_state.persistent[item_id];
        }
        // Increment
        count++;
        // Set to 1 if it's a unique item
        if (item_info["unique"].asBool())
            count = 1;
        ap_game_state.persistent[item_id] = count;
    }

    std::string item_type = item_info["type"].asString();
    // Poor man's switch
    if (item_type == "progressive")
    {
        // Add to our progressive counter and check how many we have now
        uint16_t prog_count = AP_ProgressiveItem(item_id);
        // And apply whatever item we have next in the queue
        if (item_info["items"].isArray() && item_info["items"].size() > 0)
        {
            // Repeat the last entry if we have more copies
            uint16_t idx = (item_info["items"].size() < prog_count ? item_info["items"].size() : prog_count) - 1;
            ap_net_id_t next_item = AP_NET_ID(json_get_int(item_info["items"][idx], 0));
            ap_get_item(next_item, silent, false);
        }
    }
    else if (item_type == "automap" && item_for_current_level(item_info))
    {
        // TODO: Automap handler
    }
    else if (item_type == "weapon")
    {
        int64_t weaponnum = json_get_int(item_info["weaponnum"], 0);
        if (weaponnum >= MAX_WEAPONS) return;  // Limit to valid weapons
        // TODO: Weapon get handler
		/*
        bool had_weapon = ACTIVE_PLAYER->gotweapon & (1 << weaponnum);
        ACTIVE_PLAYER->gotweapon |= (1 << weaponnum);
        // If it's a new unlock, switch to it
        if (notify && !had_weapon)
        {
            force_set_player_weapon(weaponnum);
        }
        P_AddAmmo(ACTIVE_PLAYER, json_get_int(item_info["weaponnum"], 0), json_get_int(item_info["ammo"], 0));*/
    }
    else if (item_type == "ammo" || item_type == "maxammo")
    {
        int64_t weaponnum = json_get_int(item_info["weaponnum"], 0);
        if (weaponnum >= MAX_WEAPONS) return;  // Limit to valid weapons
        //TODO: Ammo get handler
		/*
        ACTIVE_PLAYER->max_ammo_amount[weaponnum] += json_get_int(item_info["capacity"], 0);
        P_AddAmmo(ACTIVE_PLAYER, weaponnum, json_get_int(item_info["ammo"], 0));*/
    }
    else if (item_type == "inventory")
    {
        int64_t invnum = json_get_int(item_info["invnum"], -1);
        if (invnum < 0 || invnum >= INV_MAX) return;  // Limit to valid slots
		// TODO: Inventory get handler
		/*
        // Add capacity
        ACTIVE_PLAYER->inv_amount[invnum] += json_get_int(item_info["capacity"], 0);
        inv_max_capacity[invnum] += json_get_int(item_info["capacity"], 0);
        if (inv_available[invnum] == 0)
        {
            // Also use stored min capacity
            ACTIVE_PLAYER->inv_amount[invnum] += inv_capacity[invnum];
            inv_max_capacity[invnum] += inv_capacity[invnum];
            inv_capacity[invnum] = 0;
        }
        // Mark as unlocked
        inv_available[invnum] = 1;

        // Saturate
        int64_t max_capacity = json_get_int(item_info["max_capacity"], -1);
        // Special case, armor has a dynamic max value tracked already
        // Armor should usually be provided with an "armor" type item instead, but no
        // Reason not to support this
        if (invnum == GET_SHIELD)
            max_capacity = ACTIVE_PLAYER->max_shield_amount;
        if (max_capacity >= 0 && ACTIVE_PLAYER->inv_amount[invnum] > max_capacity)
            ACTIVE_PLAYER->inv_amount[invnum] = max_capacity;

        // And display item
        if (notify)
            force_set_inventory_item(invnum);*/
    }
    else if (item_type == "invcapacity")
    {
        int64_t invnum = json_get_int(item_info["invnum"], -1);
        if (invnum < 0 || invnum >= INV_MAX) return;  // Limit to valid slots
		
        // If the item is not unlocked yet, just add it to the min capacity
        if (!inv_available[invnum])
            inv_capacity[invnum] += json_get_int(item_info["capacity"], 0);
        else
        {
			/*TODO:: Invcapacity handler
            // Inventory item unlocked, just increase capacity
            ACTIVE_PLAYER->inv_amount[invnum] += json_get_int(item_info["capacity"], 0);
            inv_max_capacity[invnum] += json_get_int(item_info["capacity"], 0);
            // Saturate
            int64_t max_capacity = json_get_int(item_info["max_capacity"], -1);
            if (max_capacity >= 0 && ACTIVE_PLAYER->inv_amount[invnum] > max_capacity)
                ACTIVE_PLAYER->inv_amount[invnum] = max_capacity;

            // And display item
            if (notify)
                ACTIVE_PLAYER->inven_icon = inv_to_icon[invnum];
            */
        }
    }
    else if (item_type == "ability")
    {
        if (item_info["enables"].isString())
            ability_unlocks[item_info["enables"].asString()] = 1;
    }
    else if (item_type == "health")
    {
        uint16_t capacity = json_get_int(item_info["capacity"], 0);
		/* // TODO: Health Handler
        ACTIVE_PLAYER->max_player_health += capacity;
        uint16_t healing = json_get_int(item_info["heal"], 0);
        // Non standard max health, like for atomic health
        bool overheal = item_info["overheal"].asBool();
        uint16_t max_health = overheal ? (2 * ACTIVE_PLAYER->max_player_health) : ACTIVE_PLAYER->max_player_health;
        if (sprite[ACTIVE_PLAYER->i].extra < max_health)
        {
            sprite[ACTIVE_PLAYER->i].extra += healing;
            if (sprite[ACTIVE_PLAYER->i].extra > max_health)
                sprite[ACTIVE_PLAYER->i].extra = max_health;
        }*/
    }
    else if (item_type == "armor")
    {
        uint16_t capacity = json_get_int(item_info["maxcapacity"], 0);
		/* // TODO: Armor Handler
        ACTIVE_PLAYER->max_shield_amount += capacity;
        uint16_t new_armor = json_get_int(item_info["capacity"], 0);
        if (ACTIVE_PLAYER->inv_amount[GET_SHIELD] < ACTIVE_PLAYER->max_shield_amount)
        {
            ACTIVE_PLAYER->inv_amount[GET_SHIELD] += new_armor;
            if (ACTIVE_PLAYER->inv_amount[GET_SHIELD] > ACTIVE_PLAYER->max_shield_amount)
                ACTIVE_PLAYER->inv_amount[GET_SHIELD] = ACTIVE_PLAYER->max_shield_amount;
        }*/
    }
    // Do not retrigger traps that the server has sent us before
    else if (item_type == "trap" && !silent)
    {
        Json::Value& trap_state = ap_game_state.dynamic_player["traps"][std::to_string(item_id)];
        if (!trap_state.isObject())
        {
            // New trap type, initialize
            trap_state["id"] = Json::UInt64(item_id);
            trap_state["count"] = Json::UInt64(1);
            trap_state["remaining"] = Json::UInt64(0);
            trap_state["grace"] = Json::UInt64(0);
        }
        else
        {
            // Just increment the queue trap count
            trap_state["count"] = trap_state["count"].asInt() + 1;
        }
    }
}



void process_message_queue()
{
    // Handle messages from the AP Server
    AP_ProcessMessages();

    CBaseEntity* pPlayer = ACTIVE_PLAYER;
	static_assert(std::is_pointer_v<decltype(ACTIVE_PLAYER)>, "ACTIVE_PLAYER must be a pointer type");
	if (pPlayer && !FNullEnt(pPlayer->edict()) && ap_message_queue.size() > 0)
    {
        ap_message_str = ap_message_queue[0];
        ap_message_queue.erase(ap_message_queue.begin());
		ALERT(at_notice, ap_message_str.c_str());
    }
}



int ap_velocity_modifier = 1;

void ap_handle_trap(Json::Value& trap_info, bool triggered)
{
    if (triggered)
        AP_QueueMessage("^10" + trap_info["name"].asString() + "^12 triggered!");

    // Process supported trap types
    std::string trap_type = trap_info["trap"].asString();
    if (trap_type == "celebrate")
    {
		/*
        // 1 in 25 chance to start a new fist pump each frame
        if (ACTIVE_PLAYER->fist_incs == 0 && (krand2() % 25 == 0))
        {
            ACTIVE_PLAYER->fist_incs = 1;
            switch (krand2()%6)
            {
            case 0:
                A_PlaySound(BONUS_SPEECH1, ACTIVE_PLAYER->i);
                break;
            case 1:
                A_PlaySound(BONUS_SPEECH4, ACTIVE_PLAYER->i);
                break;
            case 2:
                A_PlaySound(JIBBED_ACTOR6, ACTIVE_PLAYER->i);
                break;
            case 3:
                A_PlaySound(JIBBED_ACTOR5, ACTIVE_PLAYER->i);
                break;
            case 4:
                A_PlaySound(JIBBED_ACTOR2, ACTIVE_PLAYER->i);
                break;
            case 5:
                A_PlaySound(YIPPEE1, ACTIVE_PLAYER->i);
                break;
            }
        }*/
    }
    else if (trap_type == "hyperspeed")
    {
        // Good Luck
        ap_velocity_modifier = 6;
    }
    else if (trap_type == "death" && triggered)
    {
        // Implement
    }
}

// Called from an actor in game, ensures we are in a valid game state and an in game tic has expired since last call
void ap_process_game_tic(void)
{
    // Ensure we don't have a velocity modifier unless something actively changes it for the current tic
    ap_velocity_modifier = 1;

    // Check for items in our queue to process
    while (!ap_game_state.ap_item_queue.empty())
    {
        auto queue_item = ap_game_state.ap_item_queue.front();
        ap_game_state.ap_item_queue.erase(ap_game_state.ap_item_queue.begin());
        ap_get_item(queue_item.first, !queue_item.second, true);
    }

    // Check for outstanding or active traps
    for (std::string trap_str : ap_game_state.dynamic_player["traps"].getMemberNames())
    {
        // Fetch relevant trap information for this trap type
        Json::Value& trap_state = ap_game_state.dynamic_player["traps"][trap_str];
        ap_net_id_t trap_id = trap_state["id"].asUInt64();
        Json::Value& trap_info = ap_item_info[trap_id];
        if (!ap_game_settings["dynamic"][trap_str].isNull())
            trap_info = ap_game_settings["dynamic"][trap_str];

        bool triggered = false;
        bool active = false;
        uint32_t remaining = trap_state["remaining"].asUInt64();
        if (remaining > 1)
        {
            remaining--;
            trap_state["remaining"] = remaining;
            active = true;
        }
        else if (remaining == 1)
        {
            // Disable trap and set a grace period
            trap_state["remaining"] = 0;
            trap_state["grace"] = trap_info["grace"];
            active = false;
        }
        
        if (!active)
        {
            uint32_t grace = trap_state["grace"].asUInt64();
            uint32_t count = trap_state["count"].asUInt64();
            if (grace > 0)
            {
                grace--;
                trap_state["grace"] = grace;
            }
            else if (count > 0)
            {
                // Trigger new trap instance
                trap_state["remaining"] = trap_info["duration"];
                count--;
                trap_state["count"] = count;
                triggered = true;
                active = true;
            }
        }

        if (active)
        {
            ap_handle_trap(trap_info, triggered);
        }
    }

    // Process outstanding messages to quote at the player
    process_message_queue();
}

// This is called from the main game loop to ensure these checks happen globally
/*
bool ap_process_periodic(void)
{
    bool reset_game = false;

    // Force HUD settings. Not nice, clean up in the future when we can support custom hud on all settings
    ud.screen_size = 4;
    ud.althud = 1;

    // Peek item queue and process stuff we already can while not in game
    if (!(ACTIVE_PLAYER->gm & MODE_GAME))
    {
        for (auto iter = ap_game_state.ap_item_queue.begin(); iter != ap_game_state.ap_item_queue.end();)
        {
            Json::Value item_info = ap_item_info[iter->first];

            // If it's a silent item or a map or key unlock or trap, process them outside the game
            // already. Handling traps here ensures we don't lose them during sessions
            std::string item_type = item_info["type"].asString();
            if (!iter->second || item_type == "map" || item_type == "trap" || item_type == "key")
            {
                ap_get_item(iter->first, !iter->second, true);
                // And then remove the entry from the queue
                iter = ap_game_state.ap_item_queue.erase(iter);
            }
            else iter++;
        }
    }

    // Handle credits trap, as we can't count on in-game tics in the menu
    if (credits_trap_end > timerGetFractionalTicks())
    {
        switch(m_currentMenu->menuID)
        {
        case MENU_CREDITS:
        case MENU_CREDITS2:
        case MENU_CREDITS3:
        case MENU_CREDITS4:
        case MENU_CREDITS5:
        case MENU_CREDITS6:
        case MENU_CREDITS7:
        case MENU_CREDITS8:
        case MENU_CREDITS9:
        case MENU_CREDITS10:
        case MENU_CREDITS11:
        case MENU_CREDITS12:
        case MENU_CREDITS13:
        case MENU_CREDITS14:
        case MENU_CREDITS15:
        case MENU_CREDITS16:
        case MENU_CREDITS17:
        case MENU_CREDITS18:
        case MENU_CREDITS19:
        case MENU_CREDITS20:
        case MENU_CREDITS21:
        case MENU_CREDITS22:
        case MENU_CREDITS23:
        case MENU_CREDITS24:
        case MENU_CREDITS25:
        case MENU_CREDITS26:
        case MENU_CREDITS27:
        case MENU_CREDITS28:
        case MENU_CREDITS29:
        case MENU_CREDITS30:
        case MENU_CREDITS31:
        case MENU_CREDITS32:
        case MENU_CREDITS33:
            // good boy
            break;
        default:
            Menu_Change(MENU_CREDITS);
            break;
        }
        if(!(ACTIVE_PLAYER->gm & MODE_MENU))
            Menu_Open(myconnectindex);
    }

    // Sync our save status, this only does something if there are changes
    // So it's save to call it every game tic
    AP_SyncProgress();

    // Check if we have reached all goals
    if (AP_CheckVictory())
    {
        uint8_t old_volumenum = ud.volume_number;
        ud.volume_number = 2;
        ud.eog = 1;
        G_BonusScreen(0);
        ap_return_to_menu = 0;
        G_BackToMenu();
        reset_game = true;
        ud.volume_number = old_volumenum;
    }

    return reset_game;
}

/* Configures the default inventory state */

/*
static void ap_set_default_inv(void)
{
    // Clear inventory info
    for (uint8_t i = 0; i < INV_MAX; i++)
    {
        inv_capacity[i] = 0;
        inv_max_capacity[i] = 0;
        inv_available[i] = 0;
        ACTIVE_PLAYER->inv_amount[i] = 0;
    }
    // Clear ammo info
    for (uint8_t i = 0; i < MAX_WEAPONS; i++)
    {
        ACTIVE_PLAYER->ammo_amount[i] = 0;
    }

    // Set default max ammo amounts
    ACTIVE_PLAYER->max_ammo_amount[PISTOL_WEAPON__STATIC] = json_get_int(ap_game_settings["maximum"]["pistol"], 120);
    ACTIVE_PLAYER->max_ammo_amount[SHOTGUN_WEAPON__STATIC] = json_get_int(ap_game_settings["maximum"]["shotgun"], 20);
    ACTIVE_PLAYER->max_ammo_amount[CHAINGUN_WEAPON__STATIC] = json_get_int(ap_game_settings["maximum"]["chaingun"], 100);
    ACTIVE_PLAYER->max_ammo_amount[RPG_WEAPON__STATIC] = json_get_int(ap_game_settings["maximum"]["rpg"], 5);
    ACTIVE_PLAYER->max_ammo_amount[HANDBOMB_WEAPON__STATIC] = json_get_int(ap_game_settings["maximum"]["pipebomb"], 5);
    ACTIVE_PLAYER->max_ammo_amount[SHRINKER_WEAPON__STATIC] = json_get_int(ap_game_settings["maximum"]["shrinker"], 5);
    ACTIVE_PLAYER->max_ammo_amount[DEVISTATOR_WEAPON__STATIC] = json_get_int(ap_game_settings["maximum"]["devastator"], 25);
    ACTIVE_PLAYER->max_ammo_amount[TRIPBOMB_WEAPON__STATIC] = json_get_int(ap_game_settings["maximum"]["tripmine"], 3);
    ACTIVE_PLAYER->max_ammo_amount[FREEZE_WEAPON__STATIC] = json_get_int(ap_game_settings["maximum"]["freezethrower"], 50);
    ACTIVE_PLAYER->max_ammo_amount[GROW_WEAPON__STATIC] = json_get_int(ap_game_settings["maximum"]["expander"], 15);
    ACTIVE_PLAYER->max_player_health = json_get_int(ap_game_settings["maximum"]["health"], 100);
    ACTIVE_PLAYER->max_shield_amount = json_get_int(ap_game_settings["maximum"]["armor"], 100);
    // Always have mighty foot and pistol
    ACTIVE_PLAYER->gotweapon = 3;
    // Always have pistol ammo, and a bit more since we might have no other weapons
    ACTIVE_PLAYER->ammo_amount[PISTOL_WEAPON__STATIC] = 96;
    if (ACTIVE_PLAYER->ammo_amount[PISTOL_WEAPON__STATIC] > ACTIVE_PLAYER->max_ammo_amount[PISTOL_WEAPON__STATIC])
        ACTIVE_PLAYER->ammo_amount[PISTOL_WEAPON__STATIC] = ACTIVE_PLAYER->max_ammo_amount[PISTOL_WEAPON__STATIC];

    ability_unlocks.clear();
    if (!ap_game_settings["lock"]["crouch"].asBool())
        ability_unlocks["crouch"] = true;
    if (!ap_game_settings["lock"]["dive"].asBool())
        ability_unlocks["dive"] = true;
    if (!ap_game_settings["lock"]["jump"].asBool())
        ability_unlocks["jump"] = true;
    if (!ap_game_settings["lock"]["run"].asBool())
        ability_unlocks["run"] = true;
    if (!ap_game_settings["lock"]["open"].asBool())
        ability_unlocks["open"] = true;
    if (!ap_game_settings["lock"]["use"].asBool())
        ability_unlocks["use"] = true;
}

void ap_load_dynamic_player_data()
{
    Json::Value player_data = ap_game_state.dynamic_player["player"];

    int health = json_get_int(player_data["health"], -1);
    // Player died, reset to max HP
    if(health <= 0 || health > ACTIVE_PLAYER->max_player_health) health = ACTIVE_PLAYER->max_player_health;
    // Give at least some minimum halth to the player on entering a level
    if (health < 30) health = 30;

    sprite[ACTIVE_PLAYER->i].extra = health;
    ACTIVE_PLAYER->last_extra = health;

    // Upgrade inventory capacity if we had more left over
    for (uint8_t i = 0; i < INV_MAX; i++)
    {
        int inv_capacity = json_get_int(player_data["inv"][i], -1);
        if (inv_capacity > ACTIVE_PLAYER->inv_amount[i])
            ACTIVE_PLAYER->inv_amount[i] = inv_capacity;
    }

    // Upgrade ammo count if we had more left over
    for (uint8_t i = 0; i < MAX_WEAPONS; i++)
    {
        int ammo_capacity = json_get_int(player_data["ammo"][i], -1);
        if (ammo_capacity > ACTIVE_PLAYER->ammo_amount[i])
            ACTIVE_PLAYER->ammo_amount[i] = ammo_capacity;
    }

    int curr_weapon = json_get_int(player_data["curr_weapon"], -1);
    if (curr_weapon >= 0)
        force_set_player_weapon(curr_weapon);

    int selected_inv = json_get_int(player_data["selected_inv"], -1);
    if (selected_inv >= 0)
        force_set_inventory_item(selected_inv);

    ACTIVE_PLAYER->player_par = json_get_int(player_data["time"], 0);
}

void ap_store_dynamic_player_data(void)
{
    Json::Value new_player_data = Json::Value();

    // Only store data if we are actually in game mode
    if (!(ACTIVE_PLAYER->gm & (MODE_EOL | MODE_GAME))) return;

    new_player_data["health"] = Json::Int(sprite[ACTIVE_PLAYER->i].extra);
    bool player_died = sprite[ACTIVE_PLAYER->i].extra <= 0 ? true : false;
    for (uint8_t i = 0; i < INV_MAX; i++)
    {
        // Only store non progressive inventory values
        switch(i)
        {
        case GET_SHIELD:
        case GET_HOLODUKE:
        case GET_HEATS:
        case GET_FIRSTAID:
        case GET_BOOTS:
            new_player_data["inv"][i] = Json::Int(player_died ? 0 : ACTIVE_PLAYER->inv_amount[i]);
            break;
        }
    }

    // Upgrade ammo count if we had more left over
    for (uint8_t i = 0; i < MAX_WEAPONS; i++)
    {
        new_player_data["ammo"][i] = Json::Int(player_died ? 0 : ACTIVE_PLAYER->ammo_amount[i]);
    }

    new_player_data["curr_weapon"] = Json::Int(ACTIVE_PLAYER->curr_weapon);
    new_player_data["selected_inv"] = Json::Int(icon_to_inv[ACTIVE_PLAYER->inven_icon]);

    new_player_data["time"] = Json::Int(ACTIVE_PLAYER->player_par);

    ap_game_state.dynamic_player["player"] = new_player_data;
    // Mark our save state as to be synced
    ap_game_state.need_sync = true;
}

void ap_sync_inventory()
{
    // Reset the count of each progressive item we've processed
    ap_game_state.progressive.clear();

    ap_set_default_inv();

    // Apply state for all persistent items we have unlocked
    for (auto item_pair : ap_game_state.persistent)
    {
        for(unsigned int i = 0; i < item_pair.second; i++)
            ap_get_item(item_pair.first, true, false);
    }

    // Restore dynamic data like HP and ammo from last map
    ap_load_dynamic_player_data();
}

void ap_on_map_load(void)
{
    current_map = ap_format_map_id(ud.level_number, ud.volume_number);

#ifdef AP_DEBUG_ON
    print_level_template();
#endif

    ap_map_patch_sprites();

#ifdef AP_DEBUG_ON
    print_debug_level_info();
#endif

    ap_mark_known_secret_sectors(false);
}

void ap_on_save_load(void)
{
    current_map = ap_format_map_id(ud.level_number, ud.volume_number);

    ap_mark_known_secret_sectors(true);
    ap_sync_inventory();
    ap_delete_collected_sprites();
}

void ap_check_secret(int16_t sectornum)
{
    AP_CheckLocation(safe_location_id(ap_game_config["locations"][current_map]["sectors"][std::to_string(sectornum)]["id"]));
}

void ap_level_end(void)
{
    ap_store_dynamic_player_data();
    // Return to menu after beating a level
    ACTIVE_PLAYER->gm = 0;
    Menu_Open(myconnectindex);
    Menu_Change(MENU_AP_LEVEL);
    ap_return_to_menu = 1;
}

void ap_check_exit(int16_t exitnum)
{
    ap_location_t exit_location = safe_location_id(ap_game_config["locations"][current_map]["exits"][std::to_string(exitnum)]["id"]);
    // Might not have a secret exit defined, so in this case treat as regular exit
    if (exit_location < 0)
        exit_location = safe_location_id(ap_game_config["locations"][current_map]["exits"][std::to_string(0)]["id"]);
    AP_CheckLocation(exit_location);
}

void ap_select_level(uint8_t i)
{
    ud.m_level_number = ap_active_levels[ud.m_volume_number][i];
}

void ap_shutdown(void)
{
    AP_LibShutdown();
}

void ap_remaining_items(uint16_t* collected, uint16_t* total)
{
    for (std::string sprite_str : ap_game_config["locations"][current_map]["sprites"].getMemberNames())
    {
        ap_location_t pickup_loc = safe_location_id(ap_game_config["locations"][current_map]["sprites"][sprite_str]["id"]);
        if (pickup_loc > 0 && AP_LOCATION_CHECK_MASK(pickup_loc, (AP_LOC_PICKUP | AP_LOC_USED)))
        {
            (*total)++;
            if (AP_LOCATION_CHECKED(pickup_loc)) (*collected)++;
        }
    }
}

void ap_con_hook(void)
{
    const char* shrink_buf = "PSHRINKING";
    int32_t i = hash_find(&h_labels, shrink_buf);
    if (i >= 0)
        pshrinking_label_code = labelcode[i];
}

uint16_t ap_steroids_duration(void)
{
    uint16_t duration = ap_game_settings["steroids_duration"].asUInt();
    if (duration == 0)
        // default value
        duration = 40;
    return duration;
}
*/

bool ap_can_dive()
{
	return ability_unlocks.count("dive") || CVAR_GET_FLOAT("ap_dive");
}

bool ap_can_jump()
{
	return ability_unlocks.count("jump") || CVAR_GET_FLOAT("ap_jump");
}

bool ap_can_crouch()
{
	return ability_unlocks.count("crouch") || CVAR_GET_FLOAT("ap_crouch");
}

bool ap_can_run()
{
	return ability_unlocks.count("run") || CVAR_GET_FLOAT("ap_run");
}

bool ap_can_door()
{
	return ability_unlocks.count("door") || CVAR_GET_FLOAT("ap_door");
}

bool ap_can_use()
{
	return ability_unlocks.count("use") || CVAR_GET_FLOAT("ap_use");
}

bool ap_can_grab()
{
	return ability_unlocks.count("grab") || CVAR_GET_FLOAT("ap_grab");
}

bool ap_can_break()
{
	return ability_unlocks.count("break") || CVAR_GET_FLOAT("ap_break");
}