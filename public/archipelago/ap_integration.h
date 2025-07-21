#pragma once

#include "ap_lib.h"

#define AP_DEBUG 0
// Enable to spawn in without server connection to grab level data
#define AP_DEBUG_SPAWN 0
// Dump entity data to textfile on level load
#define AP_DUMP_EDICT 0

#define AP_MESSAGE_QUOTE 5120
#define MAXVOLUMES 18
#define MAXLEVELS 32
#define INV_MAX 9
#define AMMO_MAX 9

extern void ap_on_map_load(void);
extern void ap_on_save_load(void);
extern void ap_sync_inventory();
extern void ap_store_dynamic_player_data(void);
extern void ap_startup(void);
extern void ap_shutdown(void);
extern bool ap_initialize(void);
extern bool ap_process_periodic(void);
extern void ap_process_game_tic(void);
extern void ap_check_secret(int16_t sectornum);
extern void ap_level_end(void);
extern void ap_check_exit(int16_t exitnum);
extern void ap_select_level(uint8_t i);
extern std::string ap_format_map_id(uint8_t level_number, uint8_t volume_number);
extern void ap_remaining_items(uint16_t* collected, uint16_t* total);
extern uint16_t ap_steroids_duration(void);

// Conditional abilities
extern bool ap_can_dive();
extern bool ap_can_jump();
extern bool ap_can_run();
extern bool ap_can_door();
extern bool ap_can_use();
extern bool ap_can_crouch();
extern bool ap_can_grab();
extern bool ap_can_break();

extern std::string ap_episode_names[MAXVOLUMES];
extern std::map<std::string, Json::Value> ap_level_data;

extern uint8_t ap_return_to_menu;
extern ap_connection_settings_t ap_connection_settings;
extern std::string ap_message_str;

extern int ap_velocity_modifier;

extern std::string generate_hash(float x, float y, float z, const char* classname);