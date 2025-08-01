
/*
 * Copyright (C) Volition, Inc. 1999.  All rights reserved.
 *
 * All source code herein is the property of Volition, Inc. You may not sell 
 * or otherwise commercially exploit the source or things you created based on the 
 * source.
 *
*/

#include <csetjmp>
#include <algorithm>

#include "ai/aibig.h"
#include "ai/aigoals.h"
#include "asteroid/asteroid.h"
#include "autopilot/autopilot.h"
#include "cmdline/cmdline.h"
#include "cmeasure/cmeasure.h"
#include "debugconsole/console.h"
#include "fireball/fireballs.h"
#include "freespace.h"
#include "gamesequence/gamesequence.h"
#include "gamesnd/eventmusic.h"
#include "gamesnd/gamesnd.h"
#include "globalincs/alphacolors.h"
#include "graphics/light.h"
#include "graphics/matrix.h"
#include "graphics/shadows.h"
#include "def_files/def_files.h"
#include "globalincs/linklist.h"
#include "hud/hud.h"
#include "hud/hudartillery.h"
#include "hud/hudets.h"
#include "hud/hudmessage.h"
#include "hud/hudshield.h"
#include "hud/hudsquadmsg.h"
#include "hud/hudtargetbox.h"
#include "hud/hudwingmanstatus.h"
#include "iff_defs/iff_defs.h"
#include "io/joy_ff.h"
#include "io/timer.h"
#include "jumpnode/jumpnode.h"
#include "lighting/lighting.h"
#include "localization/localize.h"
#include "math/fvi.h"
#include "math/staticrand.h"
#include "math/vecmat.h"
#include "mission/missioncampaign.h"
#include "mission/missionlog.h"
#include "mission/missionmessage.h"
#include "missionui/missionshipchoice.h"
#include "missionui/redalert.h"
#include "mod_table/mod_table.h"
#include "model/model.h"
#include "model/modelrender.h"
#include "model/modelreplace.h"
#include "model/animation/modelanimation_driver.h"
#include "nebula/neb.h"
#include "network/multimsgs.h"
#include "network/multiutil.h"
#include "network/multi_interpolate.h"
#include "object/deadobjectdock.h"
#include "object/objcollide.h"
#include "object/object.h"
#include "object/objectdock.h"
#include "object/objectshield.h"
#include "object/objectsnd.h"
#include "object/waypoint.h"
#include "parse/parselo.h"
#include "particle/ParticleEffect.h"
#include "particle/volumes/LegacyAACuboidVolume.h"
#include "scripting/hook_api.h"
#include "scripting/global_hooks.h"
#include "particle/particle.h"
#include "playerman/player.h"
#include "radar/radar.h"
#include "radar/radarsetup.h"
#include "render/3d.h"
#include "render/batching.h"
#include "scripting/api/objs/vecmath.h"
#include "ship/afterburner.h"
#include "ship/awacs.h"
#include "ship/ship.h"
#include "ship/shipcontrails.h"
#include "ship/shipfx.h"
#include "ship/shiphit.h"
#include "ship/subsysdamage.h"
#include "species_defs/species_defs.h"
#include "tracing/Monitor.h"
#include "tracing/tracing.h"
#include "utils/Random.h"
#include "utils/string_utils.h"
#include "weapon/beam.h"
#include "weapon/corkscrew.h"
#include "weapon/emp.h"
#include "weapon/flak.h"								//phreak addded 11/05/02 for flak primaries
#include "weapon/shockwave.h"
#include "weapon/swarm.h"
#include "weapon/weapon.h"

using namespace Ship;

#ifdef MessageBox
#undef MessageBox
#endif

#define NUM_SHIP_SUBSYSTEMS_PER_SET		200 	// Reduced from 1000 to 400 by MK on 4/1/98.  DTP; bumped from 700 to 2100
												// Reduced to 200 by taylor on 3/13/07  --  it's managed in dynamically allocated sets now
												//    Highest I saw was 164 in sm2-03a which Sandeep says has a lot of ships.
												//    JAS: sm3-01 needs 460.   You cannot know this number until *all* ships
												//    have warped in.   So I put code in the paging code which knows all ships
												//    that will warp in.

static int Num_ship_subsystems = 0;
static int Num_ship_subsystems_allocated = 0;

static SCP_vector<ship_subsys*> Ship_subsystems;
ship_subsys ship_subsys_free_list;

extern bool splodeing;
extern float splode_level;
extern int splodeingtexture;

// The minimum required fuel to engage afterburners
static const float DEFAULT_MIN_AFTERBURNER_FUEL_TO_ENGAGE = 10.0f;


int	Ai_render_debug_flag=0;
#ifndef NDEBUG
int	Ship_auto_repair = 1;		// flag to indicate auto-repair of subsystem should occur
#endif

int	Num_wings = 0;
int	Num_reinforcements = 0;
ship	Ships[MAX_SHIPS];

ship	*Player_ship;
std::shared_ptr<model_texture_replace> Player_cockpit_textures;
SCP_vector<cockpit_display> Player_displays;
bool Disable_cockpits = false;
bool Disable_cockpit_sway = false;
bool Cockpit_active = false;

wing	Wings[MAX_WINGS];
bool	Ships_inited = false;
bool	Armor_inited = false;

int	Starting_wings[MAX_STARTING_WINGS];  // wings player starts a mission with (-1 = none)

// Goober5000
int Squadron_wings[MAX_SQUADRON_WINGS];
int TVT_wings[MAX_TVT_WINGS];

// Goober5000
char Starting_wing_names[MAX_STARTING_WINGS][NAME_LENGTH];
char Squadron_wing_names[MAX_SQUADRON_WINGS][NAME_LENGTH];
char TVT_wing_names[MAX_TVT_WINGS][NAME_LENGTH];

SCP_vector<engine_wash_info> Engine_wash_info;

static engine_wash_info *get_engine_wash_pointer(char* engine_wash_name);
static int subsys_set(int objnum, int ignore_subsys_info = 0);
static void ship_add_cockpit_display(cockpit_display_info *display, int cockpit_model_num);
static void ship_set_hud_cockpit_targets();
static int thruster_glow_anim_load(generic_anim *ga);
static int ship_get_exp_propagates(ship *sp);
static bool ship_subsys_is_fighterbay(ship_subsys *ss);
static int ship_template_lookup(const char *token);
static void ship_set_eye(object *obj, int eye_index);
static void ship_start_targeting_laser(ship *shipp);
static void ship_add_ship_type_kill_count(int ship_info_index);
static int ship_info_lookup_sub(const char *token);

enum class LegacyShipParticleType : uint8_t {DAMAGE_SPEW, SPLIT_PARTICLES, OTHER};
static particle::ParticleEffectHandle default_ship_particle_effect(LegacyShipParticleType type, int n_high, int n_low, float max_rad, float min_rad, float max_life, float min_life, float max_vel, float min_vel, float variance, float range, int bitmap, float velocityInherit, bool useNormal = false);

void ship_reset_disabled_physics(object *objp, int ship_class);

// forward declaring for parse_ship()
static void parse_ship_values(ship_info* sip, const bool is_template, const bool first_time, const bool replace);

// information for ships which have exited the game
SCP_vector<exited_ship> Ships_exited;

ship_registry_entry::ship_registry_entry(const char* _name)
{
	strcpy_s(name, _name);
}

p_object* ship_registry_entry::p_objp() const
{
	Assertion(has_p_objp(), "ship_registry_entry::p_objp() was called on a ship entry that does not have a parse object");
	return &Parse_objects[pobj_num];
}

object* ship_registry_entry::objp() const
{
	Assertion(has_objp(), "ship_registry_entry::objp() was called on a ship entry that does not have an object");
	return &Objects[objnum];
}

ship* ship_registry_entry::shipp() const
{
	Assertion(has_shipp(), "ship_registry_entry::shipp() was called on a ship entry that does not have a ship");
	return &Ships[shipnum];
}

p_object* ship_registry_entry::p_objp_or_null() const
{
	return (pobj_num < 0) ? nullptr : &Parse_objects[pobj_num];
}

object* ship_registry_entry::objp_or_null() const
{
	return (objnum < 0) ? nullptr : &Objects[objnum];
}

ship* ship_registry_entry::shipp_or_null() const
{
	return (shipnum < 0) ? nullptr : &Ships[shipnum];
}

SCP_vector<ship_registry_entry> Ship_registry;
SCP_unordered_map<SCP_string, int, SCP_string_lcase_hash, SCP_string_lcase_equal_to> Ship_registry_map;

int ship_registry_get_index(const char *name)
{
	auto ship_it = Ship_registry_map.find(name);
	if (ship_it != Ship_registry_map.end())
		return ship_it->second;

	return -1;
}

int ship_registry_get_index(const SCP_string &name)
{
	auto ship_it = Ship_registry_map.find(name);
	if (ship_it != Ship_registry_map.end())
		return ship_it->second;

	return -1;
}

bool ship_registry_exists(const char *name)
{
	return Ship_registry_map.find(name) != Ship_registry_map.end();
}

bool ship_registry_exists(const SCP_string &name)
{
	return Ship_registry_map.find(name) != Ship_registry_map.end();
}

const ship_registry_entry *ship_registry_get(const char *name)
{
	auto ship_it = Ship_registry_map.find(name);
	if (ship_it != Ship_registry_map.end())
		return &Ship_registry[ship_it->second];

	return nullptr;
}

const ship_registry_entry *ship_registry_get(const SCP_string &name)
{
	auto ship_it = Ship_registry_map.find(name);
	if (ship_it != Ship_registry_map.end())
		return &Ship_registry[ship_it->second];

	return nullptr;
}


int	Num_engine_wash_types;
int	Num_ship_subobj_types;
int	Num_ship_subobjects;
int	Player_ship_class;	// needs to be player specific, move to player structure	

bool Sensor_static_forced = false; //forced static effect sexp initialization

#define		SHIP_OBJ_USED	(1<<0)				// flag used in ship_obj struct
#define		MAX_SHIP_OBJS	MAX_SHIPS			// max number of ships tracked in ship list
ship_obj		Ship_objs[MAX_SHIP_OBJS];		// array used to store ship object indexes
ship_obj		Ship_obj_list;							// head of linked list of ship_obj structs, Standalone ship cannot be in this list or it will cause bugs.

SCP_vector<ship_info>	Ship_info;
reinforcements	Reinforcements[MAX_REINFORCEMENTS];
SCP_vector<ship_info>	Ship_templates;

SCP_vector<ship_type_info> Ship_types;
bool Fighter_bomber_valid = false;
const char *Fighter_bomber_type_name = "fighter/bomber";
int Ship_type_fighter = -1, Ship_type_bomber = -1, Ship_type_fighter_bomber = -1;

SCP_vector<ArmorType> Armor_types;
SCP_vector<DamageTypeStruct>	Damage_types;

flag_def_list Armor_flags[] = {
	{ "ignore subsystem armor",		SAF_IGNORE_SS_ARMOR,	0 }
};

const int Num_armor_flags = sizeof(Armor_flags)/sizeof(flag_def_list);


flag_def_list_new<Thruster_Flags> RCS_types[] = {
    { "Bank right", Thruster_Flags::Bank_right, true, false },
    { "Bank left",	Thruster_Flags::Bank_left,	true, false },
    { "Pitch up",	Thruster_Flags::Pitch_up,	true, false },
    { "Pitch down", Thruster_Flags::Pitch_down, true, false },
    { "Yaw right",  Thruster_Flags::Yaw_right,  true, false },
    { "Yaw left",	Thruster_Flags::Yaw_left,	true, false },
    { "Slide right",Thruster_Flags::Slide_right,true, false },
    { "Slide left", Thruster_Flags::Slide_left, true, false },
    { "Slide up",	Thruster_Flags::Slide_up,	true, false },
    { "Slide down", Thruster_Flags::Slide_down, true, false },
    { "Forward",	Thruster_Flags::Forward,	true, false },
    { "Reverse",	Thruster_Flags::Reverse,	true, false }
};

const size_t Num_rcs_types = sizeof(RCS_types) / sizeof(flag_def_list_new<Thruster_Flags>);

// Use the last parameter here to tell the parser whether to stuff the flag into flags or flags2
flag_def_list_new<Model::Subsystem_Flags> Subsystem_flags[] = {
	{ "untargetable",			    Model::Subsystem_Flags::Untargetable,		                true, false },
	{ "carry no damage",		    Model::Subsystem_Flags::Carry_no_damage,	                true, false },
	{ "use multiple guns",		    Model::Subsystem_Flags::Use_multiple_guns,	                true, false },
	{ "fire down normals",		    Model::Subsystem_Flags::Fire_on_normal,	                    true, false },
	{ "check hull",				    Model::Subsystem_Flags::Turret_hull_check,	                true, false },
	{ "fixed firingpoints",		    Model::Subsystem_Flags::Turret_fixed_fp,	                true, false },
	{ "salvo mode",				    Model::Subsystem_Flags::Turret_salvo,		                true, false },
	{ "no subsystem targeting",	    Model::Subsystem_Flags::No_ss_targeting,	                true, false },
	{ "fire on target",			    Model::Subsystem_Flags::Fire_on_target,	                    true, false },
    { "reset when idle",		    Model::Subsystem_Flags::Turret_reset_idle,	                true, false },
	{ "carry shockwave",		    Model::Subsystem_Flags::Carry_shockwave,	                true, false },
	{ "allow landing",			    Model::Subsystem_Flags::Allow_landing,		                true, false },
	{ "target requires fov",	    Model::Subsystem_Flags::Fov_required,		                true, false },
	{ "fov edge checks",		    Model::Subsystem_Flags::Fov_edge_check,	                    true, false },
	{ "no replace",				    Model::Subsystem_Flags::No_replace,		                    true, false },
	{ "no live debris",			    Model::Subsystem_Flags::No_live_debris,	                    true, false },
	{ "ignore if dead",			    Model::Subsystem_Flags::Ignore_if_dead,	                    true, false },
	{ "allow vanishing",		    Model::Subsystem_Flags::Allow_vanishing,	                true, false },
	{ "damage as hull",			    Model::Subsystem_Flags::Damage_as_hull,	                    true, false },
	{ "starts locked",              Model::Subsystem_Flags::Turret_locked,                      true, false },
	{ "no aggregate",			    Model::Subsystem_Flags::No_aggregate,		                true, false },
	{ "wait for animation",         Model::Subsystem_Flags::Turret_anim_wait,                   true, false },
	{ "play fire sound for player", Model::Subsystem_Flags::Player_turret_sound,                true, false },
	{ "only target if can fire",    Model::Subsystem_Flags::Turret_only_target_if_can_fire,     true, false },
	{ "no disappear",			    Model::Subsystem_Flags::No_disappear,                       true, false },
	{ "collide submodel",		    Model::Subsystem_Flags::Collide_submodel,                   true, false },
	{ "allow destroyed rotation",	Model::Subsystem_Flags::Destroyed_rotation,                 true, false },
	{ "turret use ammo",		    Model::Subsystem_Flags::Turret_use_ammo,                    true, false },
	{ "autorepair if disabled",	    Model::Subsystem_Flags::Autorepair_if_disabled,             true, false },
	{ "don't autorepair if disabled", Model::Subsystem_Flags::No_autorepair_if_disabled,        true, false },
	{ "share fire direction",       Model::Subsystem_Flags::Share_fire_direction,               true, false },
	{ "no damage spew",             Model::Subsystem_Flags::No_sparks,                          true, false },
	{ "no impact debris",           Model::Subsystem_Flags::No_impact_debris,                   true, false },
	{ "hide turret from loadout stats", Model::Subsystem_Flags::Hide_turret_from_loadout_stats, true, false },
	{ "turret has distant firepoint", Model::Subsystem_Flags::Turret_distant_firepoint,         true, false },
	{ "override submodel impact",   Model::Subsystem_Flags::Override_submodel_impact,           true, false },
	{ "burst ignores rof mult", 	Model::Subsystem_Flags::Burst_ignores_RoF_Mult,				true, false },
};

const size_t Num_subsystem_flags = sizeof(Subsystem_flags)/sizeof(flag_def_list_new<Model::Subsystem_Flags>);


flag_def_list_new<Info_Flags> Ship_flags[] = {
    { "no_collide",					Info_Flags::No_collide,				true, false },
    { "player_ship",				Info_Flags::Player_ship,			true, false },
    { "default_player_ship",		Info_Flags::Default_player_ship,	true, false },
    { "repair_rearm",				Info_Flags::Support,				true, false },
    { "cargo",						Info_Flags::Cargo,					true, false },
    { "fighter",					Info_Flags::Fighter,				true, false },
    { "bomber",						Info_Flags::Bomber,					true, false },
    { "transport",					Info_Flags::Transport,				true, false },
    { "freighter",					Info_Flags::Freighter,				true, false },
    { "capital",					Info_Flags::Capital,				true, false },
    { "supercap",					Info_Flags::Supercap,				true, false },
    { "drydock",					Info_Flags::Drydock,				true, false },
    { "cruiser",					Info_Flags::Cruiser,				true, false },
    { "navbuoy",					Info_Flags::Navbuoy,				true, false },
    { "sentrygun",					Info_Flags::Sentrygun,				true, false },
    { "escapepod",					Info_Flags::Escapepod,				true, false },
    { "stealth",					Info_Flags::Stealth,				true, false },
    { "no type",					Info_Flags::No_ship_type,			true, false },
    { "ship copy",					Info_Flags::Ship_copy,				true, false },
    { "in tech database",			Info_Flags::In_tech_database,		true, false },
    { "in tech database multi",		Info_Flags::In_tech_database_m,		true, false },
    { "don't collide invisible",	Info_Flags::Ship_class_dont_collide_invis, true, false },
    { "big damage",					Info_Flags::Big_damage,				true, false },
    { "corvette",					Info_Flags::Corvette,				true, false },
    { "gas miner",					Info_Flags::Gas_miner,				true, false },
    { "awacs",						Info_Flags::Awacs,					true, false },
    { "knossos",					Info_Flags::Knossos_device,			true, false },
    { "no_fred",					Info_Flags::No_fred,				true, false },
    { "flash",						Info_Flags::Flash,					true, false },
    { "surface shields",			Info_Flags::Surface_shields,		true, false },
    { "show ship",					Info_Flags::Show_ship_model,		true, false },
    { "generate icon",				Info_Flags::Generate_hud_icon,		true, false },
    { "no weapon damage scaling",	Info_Flags::Disable_weapon_damage_scaling, true, false },
    { "gun convergence",			Info_Flags::Gun_convergence,		true, false },
    { "no thruster geometry noise", Info_Flags::No_thruster_geo_noise,	true, false },
    { "intrinsic no shields",		Info_Flags::Intrinsic_no_shields,	true, false },
    { "dynamic primary linking",	Info_Flags::Dyn_primary_linking,	true, false },
    { "no primary linking",			Info_Flags::No_primary_linking,		true, false },
    { "no pain flash",				Info_Flags::No_pain_flash,			true, false },
    { "no ets",						Info_Flags::No_ets,					true, false },
    { "no lighting",				Info_Flags::No_lighting,			true, false },
    { "auto spread shields",		Info_Flags::Auto_spread_shields,	true, false },
    { "model point shields",		Info_Flags::Model_point_shields,	true, false },
    { "repair disabled subsystems", Info_Flags::Subsys_repair_when_disabled, true, false},
	{ "don't bank when turning",	Info_Flags::Dont_bank_when_turning,		true, false },
	{ "don't clamp max velocity",	Info_Flags::Dont_clamp_max_velocity,	true, false },
	{ "instantaneous acceleration",	Info_Flags::Instantaneous_acceleration,	true, false },
	{ "large ship deathroll",		Info_Flags::Large_ship_deathroll,	true, false },
	{ "no impact debris",			Info_Flags::No_impact_debris,		true, false },
    // to keep things clean, obsolete options go last
    { "ballistic primaries",		Info_Flags::Ballistic_primaries,	false, false }
};

const size_t Num_ship_flags = sizeof(Ship_flags) / sizeof(flag_def_list_new<Info_Flags>);

/*
++Here be dragons.. err.. begins the section for the ai targeting revision
++  First flag_def_list (& its size) for object types (ship/asteroid/weapon)
++  List of reasonable object flags (from object.h)
++  List of potentially useful ship class flags
++  List of potentially useful weapon class flags
*/
flag_def_list ai_tgt_objects[] = {
	{ "ship",		OBJ_SHIP,		0 },
	{ "asteroid",	OBJ_ASTEROID,	0 },
	{ "weapon",		OBJ_WEAPON,		0 },
	{ "debris",		OBJ_DEBRIS,		0 },
};

const int num_ai_tgt_objects = sizeof(ai_tgt_objects) / sizeof(flag_def_list);

flag_def_list_new<Object::Object_Flags> ai_tgt_obj_flags[] = {
	{ "no shields",			Object::Object_Flags::No_shields,			true, false },
	{ "targetable as bomb",	Object::Object_Flags::Targetable_as_bomb,	true, false },
	{ "renders",			Object::Object_Flags::Renders,				true, false },
	{ "collides",			Object::Object_Flags::Collides,				true, false },
	{ "physics",			Object::Object_Flags::Physics,				true, false },
	{ "invulnerable",		Object::Object_Flags::Invulnerable,			true, false },
	{ "player ship",		Object::Object_Flags::Player_ship,			true, false },
	{ "special warpin",		Object::Object_Flags::Special_warpin,		true, false },
	{ "immobile",			Object::Object_Flags::Immobile,				true, false },
	{ "don't-change-position",	Object::Object_Flags::Dont_change_position,	true, false },
	{ "don't-change-orientation",	Object::Object_Flags::Dont_change_orientation,	true, false },
};

const int num_ai_tgt_obj_flags = sizeof(ai_tgt_obj_flags) / sizeof(flag_def_list_new<Object::Object_Flags>);


flag_def_list_new<Ship::Info_Flags> ai_tgt_ship_flags[] = {
	{ "afterburners",					Info_Flags::Afterburner,					true, false },
	{ "big damage",						Info_Flags::Big_damage,						true, false },
	{ "has awacs",						Info_Flags::Has_awacs,						true, false },
	{ "no collide",						Info_Flags::No_collide,						true, false },
	{ "player ship",					Info_Flags::Player_ship,					true, false },
	{ "default player ship", 			Info_Flags::Default_player_ship,			true, false },
	{ "path fixup",						Info_Flags::Path_fixup,						true, false },
	{ "support",						Info_Flags::Support,						true, false },
	{ "cargo",							Info_Flags::Cargo,							true, false },
	{ "fighter",						Info_Flags::Fighter,						true, false },
	{ "bomber",							Info_Flags::Bomber,							true, false },
	{ "cruiser",						Info_Flags::Cruiser,						true, false },
	{ "freighter",						Info_Flags::Freighter,						true, false },
	{ "capital",						Info_Flags::Capital,						true, false },
	{ "transport",						Info_Flags::Transport,						true, false },
	{ "navbuoy",						Info_Flags::Navbuoy,						true, false },
	{ "sentrygun",						Info_Flags::Sentrygun,						true, false },
	{ "escapepod",						Info_Flags::Escapepod,						true, false },
	{ "no type",						Info_Flags::No_ship_type,					true, false },
	{ "ship copy",						Info_Flags::Ship_copy,						true, false },
	{ "in tech database", 				Info_Flags::In_tech_database,				true, false },
	{ "in tech database multi",			Info_Flags::In_tech_database_m,				true, false },
	{ "stealth",						Info_Flags::Stealth,						true, false },
	{ "supercap",						Info_Flags::Supercap,						true, false },
	{ "drydock",						Info_Flags::Drydock,						true, false },
	{ "dont collide invisible",			Info_Flags::Ship_class_dont_collide_invis,	true, false },
	{ "corvette",						Info_Flags::Corvette,						true, false },
	{ "gas miner",						Info_Flags::Gas_miner,						true, false },
	{ "awacs",							Info_Flags::Awacs,							true, false },
	{ "knossos",						Info_Flags::Knossos_device,					true, false },
	{ "no fred",						Info_Flags::No_fred,						true, false },
	{ "default in tech database",		Info_Flags::Default_in_tech_database,		true, false },
	{ "default in tech database multi",	Info_Flags::Default_in_tech_database_m,		true, false },
	{ "flash",							Info_Flags::Flash,							true, false },
	{ "show ship",						Info_Flags::Show_ship_model,				true, false },
	{ "surface shields",				Info_Flags::Surface_shields,				true, false },
	{ "generate icon",					Info_Flags::Generate_hud_icon,				true, false },
	{ "no weapon damage scaling",		Info_Flags::Disable_weapon_damage_scaling,	true, false },
	{ "gun convergence",				Info_Flags::Gun_convergence,				true, false },
	{ "no thruster geometry noise",		Info_Flags::No_thruster_geo_noise,			true, false },
	{ "intrinsic no shields",			Info_Flags::Intrinsic_no_shields,			true, false },
	{ "no primary linking",				Info_Flags::No_primary_linking,				true, false },
	{ "no pain flash",					Info_Flags::No_pain_flash,					true, false },
	{ "allow landings",					Info_Flags::Allow_landings,					true, false },
	{ "no ets",							Info_Flags::No_ets,							true, false },
	{ "no lighting",					Info_Flags::No_lighting,					true, false },
	{ "dyn primary linking",			Info_Flags::Dyn_primary_linking,			true, false },
	{ "auto spread shields",			Info_Flags::Auto_spread_shields,			true, false },
	{ "draw weapon models",				Info_Flags::Draw_weapon_models,				true, false },
	{ "model point shields",			Info_Flags::Model_point_shields,			true, false },
	{ "repair disabled subsystems",		Info_Flags::Subsys_repair_when_disabled,	true, false },
};

const int num_ai_tgt_ship_flags = sizeof(ai_tgt_ship_flags) / sizeof(flag_def_list_new<Ship::Info_Flags>);

flag_def_list_new<Weapon::Info_Flags> ai_tgt_weapon_flags[] = {
    { "bomb",				        Weapon::Info_Flags::Bomb,			                    true, false },
    { "huge damage",		        Weapon::Info_Flags::Huge,			                    true, false },
    { "supercap damage",	        Weapon::Info_Flags::Supercap,		                    true, false },
    { "bomber+",			        Weapon::Info_Flags::Bomber_plus,	                    true, false },
    { "electronics",		        Weapon::Info_Flags::Electronics,	                    true, false },
    { "puncture",			        Weapon::Info_Flags::Puncture,		                    true, false },
    { "emp",				        Weapon::Info_Flags::Emp,			                    true, false },
    { "heat seeking",		        Weapon::Info_Flags::Homing_heat,	                    true, false },
    { "aspect seeking",		        Weapon::Info_Flags::Homing_aspect,	                    true, false },
    { "engine seeking",		        Weapon::Info_Flags::Homing_javelin,                     true, false },
    { "pierce shields",		        Weapon::Info_Flags::Pierce_shields,                     true, false },
    { "local ssm",			        Weapon::Info_Flags::Local_ssm,		                    true, false },
    { "capital+",			        Weapon::Info_Flags::Capital_plus,	                    true, false },
    { "heat-seeking",				Weapon::Info_Flags::Homing_heat,						true, false },
    { "aspect-seeking",				Weapon::Info_Flags::Homing_aspect,						true, false },
    { "javelin",					Weapon::Info_Flags::Homing_javelin,						true, false },
    { "spawn",						Weapon::Info_Flags::Spawn,								true, false },
    { "remote detonate",			Weapon::Info_Flags::Remote,								true, false },
    { "countermeasure",				Weapon::Info_Flags::Cmeasure,							true, false },
    { "turns",						Weapon::Info_Flags::Turns,								true, false },
    { "swarm",						Weapon::Info_Flags::Swarm,								true, false },
    { "trail",						Weapon::Info_Flags::Trail,								true, false },
    { "big ship",					Weapon::Info_Flags::Big_only,							true, false },
    { "child",						Weapon::Info_Flags::Child,								true, false },
    { "no dumbfire",				Weapon::Info_Flags::No_dumbfire,						true, false },
	{ "no doublefire",				Weapon::Info_Flags::No_doublefire,						true, false },
    { "thruster",					Weapon::Info_Flags::Thruster,							true, false },
    { "in tech database",			Weapon::Info_Flags::In_tech_database,					true, false },
    { "player allowed",				Weapon::Info_Flags::Player_allowed,						true, false },
	{ "default player weapon",		Weapon::Info_Flags::Default_player_weapon,				true, false },
    { "corkscrew",					Weapon::Info_Flags::Corkscrew,							true, false },
    { "particle spew",				Weapon::Info_Flags::Particle_spew,						true, false },
    { "esuck",						Weapon::Info_Flags::Energy_suck,						true, false },
    { "flak",						Weapon::Info_Flags::Flak,								true, false },
    //{ "beam",						Weapon::Info_Flags::Beam,								true, false },	// Okay, this one probably doesn't make sense.
    { "tag",						Weapon::Info_Flags::Tag,								true, false },
    { "shudder",					Weapon::Info_Flags::Shudder,							true, false },
    { "lockarm",					Weapon::Info_Flags::Lockarm,							true, false },
    { "ballistic",					Weapon::Info_Flags::Ballistic,							true, false },
    { "default in tech database",	Weapon::Info_Flags::Default_in_tech_database,			true, false },
    { "tagged only",				Weapon::Info_Flags::Tagged_only,						true, false },
    //{ "cycle",					Weapon::Info_Flags::Cycle,								true, false },
    { "small only",					Weapon::Info_Flags::Small_only,					    	true, false },
    { "same turret cooldown",		Weapon::Info_Flags::Same_turret_cooldown,				true, false },
    { "apply no light",				Weapon::Info_Flags::Mr_no_lighting,				    	true, false },
    { "transparent",				Weapon::Info_Flags::Transparent,						true, false },
    { "training",					Weapon::Info_Flags::Training,							true, false },
    { "smart spawn",				Weapon::Info_Flags::Smart_spawn,						true, false },
    { "inherit parent target",		Weapon::Info_Flags::Inherit_parent_target,				true, false },
    { "no emp kill",				Weapon::Info_Flags::No_emp_kill,						true, false },
    { "variable lead homing",		Weapon::Info_Flags::Variable_lead_homing,				true, false },
    { "untargeted heat seeker",		Weapon::Info_Flags::Untargeted_heat_seeker,			    true, false },
    { "no radius doubling",			Weapon::Info_Flags::No_radius_doubling,					true, false },
    { "no subsystem homing",		Weapon::Info_Flags::Non_subsys_homing,					true, false },
    { "no lifeleft penalty",		Weapon::Info_Flags::No_life_lost_if_missed,			    true, false },
    { "custom seeker str",			Weapon::Info_Flags::Custom_seeker_str,					true, false },
    { "can be targeted",			Weapon::Info_Flags::Can_be_targeted,					true, false },
    { "show on radar",				Weapon::Info_Flags::Shown_on_radar,					    true, false },
    { "show friendly on radar",		Weapon::Info_Flags::Show_friendly,						true, false },
    { "chain external model fps",	Weapon::Info_Flags::External_weapon_fp,				    true, false },
    { "external model launcher",	Weapon::Info_Flags::External_weapon_lnch,				true, false },
    { "takes blast damage",			Weapon::Info_Flags::Takes_blast_damage,				    true, false },
    { "takes shockwave damage",		Weapon::Info_Flags::Takes_shockwave_damage,			    true, false },
    { "hide from radar",			Weapon::Info_Flags::Dont_show_on_radar,				    true, false },
    { "render flak",				Weapon::Info_Flags::Render_flak,						true, false },
    { "ciws",						Weapon::Info_Flags::Ciws,								true, false },
    { "anti-subsystem beam",		Weapon::Info_Flags::Antisubsysbeam,					    true, false },
    { "no primary linking",			Weapon::Info_Flags::Nolink,							    true, false },
    { "same emp time for capships",	Weapon::Info_Flags::Use_emp_time_for_capship_turrets,	true, false },
    { "no primary linked penalty",	Weapon::Info_Flags::No_linked_penalty,					true, false },
    { "no homing speed ramp",		Weapon::Info_Flags::No_homing_speed_ramp,				true, false },
    { "pulls aspect seekers",		Weapon::Info_Flags::Cmeasure_aspect_home_on,			true, false },
    { "turret interceptable",		Weapon::Info_Flags::Turret_Interceptable,				true, false },
    { "fighter interceptable",		Weapon::Info_Flags::Fighter_Interceptable,				true, false },
    { "aoe electronics",			Weapon::Info_Flags::Aoe_Electronics,					true, false },
    { "apply recoil",				Weapon::Info_Flags::Apply_Recoil,						true, false },
    { "don't spawn if shot",		Weapon::Info_Flags::Dont_spawn_if_shot,				    true, false },
    { "die on lost lock",			Weapon::Info_Flags::Die_on_lost_lock,					true, false },
	{ "no impact spew",				Weapon::Info_Flags::No_impact_spew,						true, false },
	{ "require exact los",			Weapon::Info_Flags::Require_exact_los,					true, false },
	{ "multilock target dead subsys", Weapon::Info_Flags::Multilock_target_dead_subsys,		true, false },
	{ "ignores countermeasures",	Weapon::Info_Flags::Ignores_countermeasures,			true, false },
	{ "freespace 1 missile behavior",Weapon::Info_Flags::Freespace_1_missile_behavior,		true, false },
};

const int num_ai_tgt_weapon_info_flags = sizeof(ai_tgt_weapon_flags) / sizeof(flag_def_list_new<Weapon::Info_Flags>);

SCP_vector <ai_target_priority> Ai_tp_list;

//	Constant for flag,							Name of flag
ship_flag_name Ship_flag_names[] = {
	{ Ship_Flags::Vaporize,						"vaporize" },
	{ Ship_Flags::Warp_broken,					"break-warp" },
	{ Ship_Flags::Warp_never,					"never-warp" },
	{ Ship_Flags::Scannable,					"scannable" },
	{ Ship_Flags::Cargo_revealed,				"cargo-known" },
	{ Ship_Flags::Hidden_from_sensors,			"hidden-from-sensors" },
	{ Ship_Flags::Stealth,						"stealth" },
	{ Ship_Flags::Friendly_stealth_invis,		"friendly-stealth-invisible" },
	{ Ship_Flags::Hide_ship_name,				"hide-ship-name" },
	{ Ship_Flags::Primitive_sensors,			"primitive-sensors" },
	{ Ship_Flags::Afterburner_locked,			"afterburners-locked" },
	{ Ship_Flags::Primaries_locked,				"primaries-locked" },
	{ Ship_Flags::Secondaries_locked,			"secondaries-locked" },
	{ Ship_Flags::No_subspace_drive,			"no-subspace-drive" },
	{ Ship_Flags::No_departure_warp,			"no-departure-warp"},
	{ Ship_Flags::Dont_collide_invis,			"don't-collide-invisible" },
	{ Ship_Flags::No_ets,						"no-ets" },
	{ Ship_Flags::Cloaked,						"cloaked" },
	{ Ship_Flags::Toggle_subsystem_scanning,	"toggle-subsystem-scanning" },
	{ Ship_Flags::No_secondary_lockon,			"no-secondary-lock-on" },
	{ Ship_Flags::No_disabled_self_destruct,	"no-disabled-self-destruct" },
	{ Ship_Flags::Hide_mission_log,				"hide-in-mission-log" },
	{ Ship_Flags::No_passive_lightning,			"no-ship-passive-lightning" },
	{ Ship_Flags::Glowmaps_disabled,			"glowmaps-disabled" },
	{ Ship_Flags::No_thrusters,					"no-thrusters" },
	{ Ship_Flags::Fail_sound_locked_primary, 	"fail-sound-locked-primary"},
	{ Ship_Flags::Fail_sound_locked_secondary, 	"fail-sound-locked-secondary"},
	{ Ship_Flags::Aspect_immune, 				"aspect-immune"},
	{ Ship_Flags::Cannot_perform_scan,			"cannot-perform-scan"},
	{ Ship_Flags::No_targeting_limits,			"no-targeting-limits"},
	{ Ship_Flags::No_death_scream,				"no-death-scream"},
	{ Ship_Flags::Always_death_scream,			"always-death-scream"},
	{ Ship_Flags::No_builtin_messages,			"no-builtin-messages"},
	{ Ship_Flags::Scramble_messages,			"scramble-messages"},
	{ Ship_Flags::Maneuver_despite_engines,		"maneuver-despite-engines" },
	{ Ship_Flags::No_scanned_cargo,             "no-scanned-cargo"},
	{ Ship_Flags::EMP_doesnt_scramble_messages,	"emp-doesn't-scramble-messages" },
};

ship_flag_description Ship_flag_descriptions[] = {
	{ Ship_Flags::Vaporize,						"Causes a ship to vanish (no deathroll, no debris, no explosion) when destroyed."},
	{ Ship_Flags::Warp_broken,					"Causes a ship's subspace drive to break. Can be repaired by a support ship."},
	{ Ship_Flags::Warp_never,					"Causes a ship's subspace drive to never work. Cannot be repaired by a support ship."},
	{ Ship_Flags::Scannable,					"Whether or not the ship can be scanned."},
	{ Ship_Flags::Cargo_revealed,				"If set, the ship's cargo can be seen without scanning the ship."},
	{ Ship_Flags::Hidden_from_sensors,			"If set, the ship can't be targeted and appears on radar as a blinking dot."},
	{ Ship_Flags::Stealth,						"If set, the ship can't be targeted, is invisible on radar, and is ignored by AI unless firing."},
	{ Ship_Flags::Friendly_stealth_invis,		"If set, the ship can't be targeted even by ships on the same team."},
	{ Ship_Flags::Hide_ship_name,				"If set, the ship name can't be seen when the ship is targeted."},
	{ Ship_Flags::Primitive_sensors,			"Targets will only be a blip on the radar. Other ships cannot be targeted and aspect lock cannot be used."},
	{ Ship_Flags::Afterburner_locked,			"Will stop a ship from firing their afterburner."},
	{ Ship_Flags::Primaries_locked,				"Will stop a ship from firing their primary weapons."},
	{ Ship_Flags::Secondaries_locked,			"Will stop a ship from firing their secondary weapons."},
	{ Ship_Flags::No_subspace_drive,			"Will not allow a ship to jump into subspace."},
	{ Ship_Flags::No_departure_warp,			"The ship will depart without the normal warp-out effect."},
	{ Ship_Flags::Dont_collide_invis,			"Will cause polygons with an invisible texture to stop colliding with objects."},
	{ Ship_Flags::No_ets,						"Will not allow a ship to alter its ETS system."},
	{ Ship_Flags::Cloaked,						"This ship will not be rendered.  (There is no effect on whether the ship can be targeted.)"},
	{ Ship_Flags::Toggle_subsystem_scanning,	"Switches between being able to scan a whole ship or individual subsystems."},
	{ Ship_Flags::No_secondary_lockon,			"Will disable target acquisition for secondaries of all types (does not affect turrets)."},
	{ Ship_Flags::No_disabled_self_destruct,	"Ship will not self-destruct after 90 seconds if engines or weapons destroyed."},
	{ Ship_Flags::Hide_mission_log,				"Mission log events generated for this ship will not be viewable."},
	{ Ship_Flags::No_passive_lightning,			"Disables ship passive lightning."},
	{ Ship_Flags::Glowmaps_disabled,			"Ship is rendered without glowmaps."},
	{ Ship_Flags::No_thrusters,					"Thrusters on this ship are not rendered."},
	{ Ship_Flags::Fail_sound_locked_primary,	"Play the firing fail sound when the weapon is locked."},
	{ Ship_Flags::Fail_sound_locked_secondary,	"Play the firing fail sound when the weapon is locked."},
	{ Ship_Flags::Aspect_immune,				"Ship cannot be targeted by Aspect Seekers."},
	{ Ship_Flags::Cannot_perform_scan,			"Ship cannot scan other ships."},
	{ Ship_Flags::No_targeting_limits,			"Ship is always targetable regardless of AWACS or targeting range limits."},
	{ Ship_Flags::No_death_scream,				"Ship will never send a death message when destroyed."},
	{ Ship_Flags::Always_death_scream,			"Ship will always send a death message when destroyed."},
	{ Ship_Flags::No_builtin_messages,			"Ship will not send any persona messages."},
	{ Ship_Flags::Scramble_messages,			"All messages sent from or received by this ship will appear scrambled, as if the ship had been hit by an EMP." },
	{ Ship_Flags::Maneuver_despite_engines,		"Ship can maneuver even if its engines are disabled or disrupted" },
	{ Ship_Flags::No_scanned_cargo,             "Ship cargo will never be revealed and will instead only show scanned or not scanned. Only available if using New Scanning Behavior in game_settings.tbl."},
	{ Ship_Flags::EMP_doesnt_scramble_messages, "EMP does not affect whether messages appear scrambled when sent from or received by this ship." },
};

extern const size_t Num_ship_flag_names = sizeof(Ship_flag_names) / sizeof(ship_flag_name);

// Ditto for wings
wing_flag_name Wing_flag_names[] = {
	{ Wing_Flags::No_arrival_music,					"no-arrival-music" },
	{ Wing_Flags::No_arrival_message,				"no-arrival-message" },
	{ Wing_Flags::No_first_wave_message,		"no-first-wave-message" },
	{ Wing_Flags::No_arrival_warp,					"no-arrival-warp" },
	{ Wing_Flags::No_departure_warp,				"no-departure-warp" },
	{ Wing_Flags::Same_arrival_warp_when_docked,	"same-arrival-warp-when-docked" },
	{ Wing_Flags::Same_departure_warp_when_docked,	"same-departure-warp-when-docked" },
	{ Wing_Flags::Waypoints_no_formation,	"waypoints-no-formation" }
};

wing_flag_description Wing_flag_descriptions[] = {
	{ Wing_Flags::No_arrival_music,					"Arrival music will not be played when this wing arrives."},
	{ Wing_Flags::No_arrival_message,				"Arrival messages will not be sent when this wing arrives."},
	{ Wing_Flags::No_first_wave_message,		"Arrival messages will not be sent when the first wave of wing arrives."},
	{ Wing_Flags::No_arrival_warp,					"No ship in the wing will use a warp effect upon arrival."},
	{ Wing_Flags::No_departure_warp,				"No ship in the wing will use a warp effect upon departure."},
	{ Wing_Flags::Same_arrival_warp_when_docked,	"All ships in the wing will use the same warp effect size upon arrival as if they were not docked instead of the enlarged aggregate size."},
	{ Wing_Flags::Same_departure_warp_when_docked,	"All ships in the wing will use the same warp effect size upon departure as if they were not docked instead of the enlarged aggregate size."},
	{ Wing_Flags::Waypoints_no_formation,		"Ships in the wing will not try to form up when running a waypoint together." },
};

extern const size_t Num_wing_flag_names = sizeof(Wing_flag_names) / sizeof(wing_flag_name);

static int Laser_energy_out_snd_timer;	// timer so we play out of laser sound effect periodically
static int Missile_out_snd_timer;	// timer so we play out of laser sound effect periodically

SCP_vector<ship_counts>	Ship_type_counts;

SCP_vector<wing_formation> Wing_formations;

const std::shared_ptr<scripting::Hook<scripting::hooks::ShipSourceConditions>> OnCountermeasureFireHook = scripting::Hook<scripting::hooks::ShipSourceConditions>::Factory("On Countermeasure Fire",
	"Called when a ship fires a countermeasure.",
	{
		{"Ship", "ship", "The ship that has fired the countermeasure."},
		{"CountermeasuresLeft", "number", "The number of countermeasures left on the ship after firing the current countermeasure."},
		{"Countermeasure", "weapon", "The countermeasure object that was just fired."},
	});

// I don't want to do an AI cargo check every frame, so I made a global timer to limit check to
// every SHIP_CARGO_CHECK_INTERVAL ms.  Didn't want to make a timer in each ship struct.  Ensure
// inited to 1 at mission start.
static int Ship_cargo_check_timer;

static int Thrust_anim_inited = 0;

SCP_vector<ship_effect> Ship_effects;

int ship_render_mode = MODEL_RENDER_ALL;
/**
 * Set the ship_obj struct fields to default values
 */
static void ship_obj_list_reset_slot(int index)
{
	Ship_objs[index].flags = 0;
	Ship_objs[index].next = NULL;
	Ship_objs[index].prev = (ship_obj*)-1;
}

/**
 * If the given ship is in my squadron wings
 */
static int ship_in_my_squadron(ship *shipp)
{
	int i;

	for (i=0; i<MAX_STARTING_WINGS; i++)
	{
		if (shipp->wingnum == Starting_wings[i])
			return 1;
	}

	for (i=0; i<MAX_TVT_WINGS; i++)
	{
		if (shipp->wingnum == TVT_wings[i])
			return 1;
	}

	// not in
	return 0;
}

/**
 * Initialise Ship_obj_list
 */
static void ship_obj_list_init()
{
	int i;

	list_init(&Ship_obj_list);
	for ( i = 0; i < MAX_SHIP_OBJS; i++ ) {
		ship_obj_list_reset_slot(i);
	}
}

/**
 * Function to add a node to the Ship_obj_list.  Only
 * called from ::ship_create()
 */
static int ship_obj_list_add(int objnum)
{
	int i;

	for ( i = 0; i < MAX_SHIP_OBJS; i++ ) {
		if ( !(Ship_objs[i].flags & SHIP_OBJ_USED) )
			break;
	}
	if ( i == MAX_SHIP_OBJS ) {
		Error(LOCATION, "Fatal Error: Ran out of ship object nodes\n");
		return -1;
	}
	
	Ship_objs[i].flags = 0;
	Ship_objs[i].objnum = objnum;
	list_append(&Ship_obj_list, &Ship_objs[i]);
	Ship_objs[i].flags |= SHIP_OBJ_USED;

	return i;
}

/**
 * Function to remove a node from the Ship_obj_list.  Only
 * called from ::ship_delete()
 */
static void ship_obj_list_remove(int index)
{
	Assert(index >= 0 && index < MAX_SHIP_OBJS);
	list_remove( Ship_obj_list, &Ship_objs[index]);	
	ship_obj_list_reset_slot(index);
}

ship_obj *get_ship_obj_ptr_from_index(int index)
{
	Assert(index >= 0 && index < MAX_SHIP_OBJS);
	return &Ship_objs[index];
}

/**
 * Return number of ships in the game.
 */
int ship_get_num_ships()
{
	int count;
	ship_obj *so;

	count = 0;
	for ( so = GET_FIRST(&Ship_obj_list); so != END_OF_LIST(&Ship_obj_list); so = GET_NEXT(so) )
	{
		if (Objects[so->objnum].flags[Object::Object_Flags::Should_be_dead])
			continue;

		count++;
	}

	return count;
}

static void engine_wash_info_init(engine_wash_info *ewi)
{
	ewi->name[0] = '\0';
	ewi->angle = PI / 10.0f;
	ewi->radius_mult = 1.0f;
	ewi->length = 500.0f;
	ewi->intensity = 1.0f;
}

/**
 * Parse an engine wash info record
 */
static void parse_engine_wash(bool replace)
{
	engine_wash_info ewt;
	engine_wash_info_init(&ewt);

	engine_wash_info *ewp;
	bool create_if_not_found  = true;

	// name of engine wash info
	required_string("$Name:");
	stuff_string(ewt.name, F_NAME, NAME_LENGTH);

	if(optional_string("+nocreate")) {
		if(!replace) {
			Warning(LOCATION, "+nocreate flag used for engine wash in non-modular table");
		}
		create_if_not_found = false;
	}

	//Does this engine wash exist already?
	//If so, load this new info into it
	//Otherwise, increment Num_engine_wash_types
	ewp = get_engine_wash_pointer(ewt.name);
	if(ewp != NULL)
	{
		if(replace)
		{
			nprintf(("Warning", "More than one version of engine wash %s exists; using newer version.", ewt.name));
		}
		else
		{
			error_display(1, "Error:  Engine wash %s already exists.  All engine wash names must be unique.", ewt.name);
		}
	}
	else
	{
		//Don't create engine wash if it has +nocreate and is in a modular table.
		if (!create_if_not_found && replace)
		{
			if (!skip_to_start_of_string_either("$Name:", "#End")) {
				error_display(1, "Missing [#End] or [$Name] after engine wash %s", ewt.name);
			}
			return;
		}
		Engine_wash_info.push_back(ewt);
		ewp = &Engine_wash_info[Num_engine_wash_types++];
	}


	// half angle of cone of wash from thruster
	if(optional_string("$Angle:"))
	{
		stuff_float(&ewp->angle);
		ewp->angle *= (PI / 180.0f);
	}

	// radius multiplier for hemisphere around thruster pt
	if(optional_string("$Radius Mult:")) {
		stuff_float(&ewp->radius_mult);
	}

	// length of cone
	if(optional_string("$Length:")) {
		stuff_float(&ewp->length);
	}

	// intensity inside hemisphere (or at 0 distance from frustated cone)
	if(optional_string("$Intensity:")) {
		stuff_float(&ewp->intensity);
	}
}

static void parse_wing_formation(bool replace)
{
	char name[NAME_LENGTH];
	SCP_vector<vec3d> position_list;

	required_string("$Name:");
	stuff_string(name, F_NAME, NAME_LENGTH);

	stuff_vec3d_list(position_list);
	if (position_list.size() != MAX_SHIPS_PER_WING - 1)
	{
		error_display(0, "Wing formation %s did not have " SIZE_T_ARG " positions.  Ignoring.", name, (size_t)(MAX_SHIPS_PER_WING - 1));
		return;
	}

	if (!stricmp(name, "Retail") || !stricmp(name, "Default"))
	{
		error_display(0, "A wing formation cannot be named \"Retail\" or \"Default\".  Overriding the default formation is not currently supported.");
		return;
	}

	int idx = wing_formation_lookup(name);
	if (idx < 0)
	{
		idx = (int)Wing_formations.size();
		Wing_formations.emplace_back();
	}
	else if (!replace)
		error_display(0, "Formation %s already exists.  All formation names must be unique.", name);

	strcpy_s(Wing_formations[idx].name, name);
	std::copy_n(position_list.begin(), MAX_SHIPS_PER_WING - 1, Wing_formations[idx].positions.begin());
}

static const char *Lightning_types[] = {
	"None",
	"Default",
};

static int Num_lightning_types = sizeof(Lightning_types)/sizeof(char*);

static int lightningtype_match(char *p)
{
	int i;
	for(i = 0; i < Num_lightning_types; i++)
	{
		if(!stricmp(Lightning_types[i], p))
			return i;
	}

	return -1;
}

// Kazan -- Volition had this set to 1500, Set it to 4K for WC Saga
#define SHIP_MULTITEXT_LENGTH 4096
#define DEFAULT_DELTA_BANK_CONST	0.5f

const float DEFAULT_ASK_HELP_SHIELD_PERCENT = 0.1f; // percent shields at which ship will ask for help
const float DEFAULT_ASK_HELP_HULL_PERCENT = 0.3f;   // percent hull at which ship will ask for help
const float AWACS_HELP_HULL_HI = 0.75f;     // percent hull at which ship will ask for help
const float AWACS_HELP_HULL_LOW = 0.25f;    // percent hull at which ship will ask for help

#define CLONE_UNIQUE_PTR(attribute) attribute = util::unique_copy(other.attribute.get(), true)

void ship_info::clone(const ship_info& other)
{
	strcpy_s(name, other.name);
	strcpy_s(display_name, other.display_name);
	strcpy_s(short_name, other.short_name);
	species = other.species;
	class_type = other.class_type;

	CLONE_UNIQUE_PTR(type_str);
	CLONE_UNIQUE_PTR(maneuverability_str);
	CLONE_UNIQUE_PTR(armor_str);
	CLONE_UNIQUE_PTR(manufacturer_str);
	CLONE_UNIQUE_PTR(desc);
	CLONE_UNIQUE_PTR(tech_desc);

	strcpy_s(tech_title, other.tech_title);

	CLONE_UNIQUE_PTR(ship_length);
	CLONE_UNIQUE_PTR(gun_mounts);
	CLONE_UNIQUE_PTR(missile_banks);

	strcpy_s(cockpit_pof_file, other.cockpit_pof_file);
	cockpit_offset = other.cockpit_offset;
	cockpit_sway_val = other.cockpit_sway_val;
	strcpy_s(pof_file, other.pof_file);
	strcpy_s(pof_file_hud, other.pof_file_hud);
	strcpy_s(pof_file_tech, other.pof_file_tech);
	num_detail_levels = other.num_detail_levels;
	memcpy(detail_distance, other.detail_distance, sizeof(int) * MAX_SHIP_DETAIL_LEVELS);
	collision_lod = other.collision_lod;

	// I'm not sure if these three are A) a good idea or B) relevant at all. -MageKing17
	cockpit_model_num = other.cockpit_model_num;
	model_num = other.model_num;
	model_num_hud = other.model_num_hud;

	hud_target_lod = other.hud_target_lod;
	density = other.density;
	damp = other.damp;
	rotdamp = other.rotdamp;
	delta_bank_const = other.delta_bank_const;
	max_vel = other.max_vel;
	min_vel = other.min_vel;
	max_rotvel = other.max_rotvel;
	rotation_time = other.rotation_time;
	srotation_time = other.srotation_time;
	max_rear_vel = other.max_rear_vel;
	forward_accel = other.forward_accel;
	forward_decel = other.forward_decel;
	slide_accel = other.slide_accel;
	slide_decel = other.slide_decel;
	gravity_const = other.gravity_const;
	dying_gravity_const = other.dying_gravity_const;

	warpin_params_index = other.warpin_params_index;
	warpout_params_index = other.warpout_params_index;

	flags = other.flags;
	ai_class = other.ai_class;
	max_speed = other.max_speed;
	min_speed = other.min_speed;
	max_accel = other.max_accel;

	collision_damage_type_idx = other.collision_damage_type_idx;
	collision_physics = other.collision_physics;

	memcpy(&shockwave, &other.shockwave, sizeof(shockwave_create_info));
	explosion_propagates = other.explosion_propagates;
	explosion_splits_ship = other.explosion_splits_ship;
	big_exp_visual_rad = other.big_exp_visual_rad;
	prop_exp_rad_mult = other.prop_exp_rad_mult;
	death_roll_r_mult = other.death_roll_r_mult;
	death_fx_r_mult = other.death_fx_r_mult;
	death_roll_time_mult = other.death_roll_time_mult;
	death_roll_rotation_mult = other.death_roll_rotation_mult;
	death_roll_xrotation_cap = other.death_roll_xrotation_cap;
	death_roll_yrotation_cap = other.death_roll_yrotation_cap;
	death_roll_zrotation_cap = other.death_roll_zrotation_cap;
	death_roll_base_time = other.death_roll_base_time;
	death_fx_count = other.death_fx_count;
	shockwave_count = other.shockwave_count;
	explosion_bitmap_anims = other.explosion_bitmap_anims;
	skip_deathroll_chance = other.skip_deathroll_chance;

	impact_spew = other.impact_spew;
	damage_spew = other.damage_spew;
	split_particles = other.split_particles;
	knossos_end_particles = other.knossos_end_particles;
	regular_end_particles = other.regular_end_particles;
	debris_flame_particles = other.debris_flame_particles;

	debris_min_lifetime = other.debris_min_lifetime;
	debris_max_lifetime = other.debris_max_lifetime;
	debris_min_speed = other.debris_min_speed;
	debris_max_speed = other.debris_max_speed;
	debris_min_rotspeed = other.debris_min_rotspeed;
	debris_max_rotspeed = other.debris_max_rotspeed;
	debris_damage_type_idx = other.debris_damage_type_idx;
	debris_min_hitpoints = other.debris_min_hitpoints;
	debris_max_hitpoints = other.debris_max_hitpoints;
	debris_hitpoints_radius_multi = other.debris_hitpoints_radius_multi;
	debris_damage_mult = other.debris_damage_mult;
	debris_arc_percent = other.debris_arc_percent;
	debris_gravity_const = other.debris_gravity_const;
	debris_density = other.debris_density;
	debris_ambient_sound = other.debris_ambient_sound;
	debris_collision_sound_light = other.debris_collision_sound_light;
	debris_collision_sound_heavy = other.debris_collision_sound_heavy;
	debris_explosion_sound = other.debris_explosion_sound;
	strcpy_s(generic_debris_pof_file, other.generic_debris_pof_file);
	generic_debris_model_num = other.generic_debris_model_num;
	generic_debris_num_submodels = other.generic_debris_num_submodels;
	generic_debris_spew_num = other.generic_debris_spew_num;

	if ( other.n_subsystems > 0 ) {
		if( n_subsystems < 1 ) {
			subsystems = new model_subsystem[other.n_subsystems];
		} else {
			delete[] subsystems;
			subsystems = new model_subsystem[other.n_subsystems];
		}

		Assert(subsystems != nullptr);

		for ( int i = 0; i < other.n_subsystems; i++ ) {
			subsystems[i] = other.subsystems[i];
		}
	}
	n_subsystems = other.n_subsystems;

	animations = other.animations;
	cockpit_animations = other.cockpit_animations;

	power_output = other.power_output;
	max_overclocked_speed = other.max_overclocked_speed;
	max_weapon_reserve = other.max_weapon_reserve;
	max_shield_regen_per_second = other.max_shield_regen_per_second;
	shield_regen_hit_delay = other.shield_regen_hit_delay;
	max_weapon_regen_per_second = other.max_weapon_regen_per_second;

	shield_weap_amount = other.shield_weap_amount;
	shield_weap_efficiency = other.shield_weap_efficiency;
	shield_weap_speed = other.shield_weap_speed;
	weap_shield_amount = other.weap_shield_amount;
	weap_shield_efficiency = other.weap_shield_efficiency;
	weap_shield_speed = other.weap_shield_speed;

	afterburner_max_vel = other.afterburner_max_vel;
	afterburner_forward_accel = other.afterburner_forward_accel;
	afterburner_fuel_capacity = other.afterburner_fuel_capacity;
	afterburner_burn_rate = other.afterburner_burn_rate;
	afterburner_recover_rate = other.afterburner_recover_rate;
	afterburner_max_reverse_vel = other.afterburner_max_reverse_vel;
	afterburner_reverse_accel = other.afterburner_reverse_accel;
	afterburner_min_start_fuel = other.afterburner_min_start_fuel;
	afterburner_min_fuel_to_burn = other.afterburner_min_fuel_to_burn;
	afterburner_cooldown_time = other.afterburner_cooldown_time;

	cmeasure_type = other.cmeasure_type;
	cmeasure_max = other.cmeasure_max;

	num_primary_banks = other.num_primary_banks;
	memcpy(primary_bank_weapons, other.primary_bank_weapons, sizeof(int) * MAX_SHIP_PRIMARY_BANKS);
	memcpy(primary_bank_ammo_capacity, other.primary_bank_ammo_capacity, sizeof(int) * MAX_SHIP_PRIMARY_BANKS);

	num_secondary_banks = other.num_secondary_banks;
	memcpy(secondary_bank_weapons, other.secondary_bank_weapons, sizeof(int) * MAX_SHIP_SECONDARY_BANKS);
	memcpy(secondary_bank_ammo_capacity, other.secondary_bank_ammo_capacity, sizeof(int) * MAX_SHIP_SECONDARY_BANKS);

	memcpy(draw_primary_models, other.draw_primary_models, sizeof(bool) * MAX_SHIP_PRIMARY_BANKS);
	memcpy(draw_secondary_models, other.draw_secondary_models, sizeof(bool) * MAX_SHIP_SECONDARY_BANKS);
	weapon_model_draw_distance = other.weapon_model_draw_distance;

	max_hull_strength = other.max_hull_strength;
	ship_recoil_modifier = other.ship_recoil_modifier;
	ship_shudder_modifier = other.ship_shudder_modifier;

	for (int i = 0; i < MAX_SHIP_PRIMARY_BANKS; i++) {
		dyn_firing_patterns_allowed[i] = other.dyn_firing_patterns_allowed[i];
	}

	max_shield_strength = other.max_shield_strength;
	max_shield_recharge = other.max_shield_recharge;
	auto_shield_spread = other.auto_shield_spread;
	auto_shield_spread_bypass = other.auto_shield_spread_bypass;
	auto_shield_spread_from_lod = other.auto_shield_spread_from_lod;
	auto_shield_spread_min_span = other.auto_shield_spread_min_span;
	max_shield_impact_effect_radius = other.max_shield_impact_effect_radius;

	// ...Hmm. A memcpy() seems slightly overkill here, but I've settled into the pattern of "array gets memcpy'd", so... -MageKing17
	memcpy(shield_point_augment_ctrls, other.shield_point_augment_ctrls, sizeof(int) * 4);

	hull_repair_rate = other.hull_repair_rate;
	subsys_repair_rate = other.subsys_repair_rate;

	hull_repair_max = other.hull_repair_max;
	subsys_repair_max = other.subsys_repair_max;

	sup_hull_repair_rate = other.sup_hull_repair_rate;
	sup_shield_repair_rate = other.sup_shield_repair_rate;
	sup_subsys_repair_rate = other.sup_subsys_repair_rate;

	closeup_pos = other.closeup_pos;
	closeup_zoom = other.closeup_zoom;

	closeup_pos_targetbox = other.closeup_pos_targetbox;
	closeup_zoom_targetbox = other.closeup_zoom_targetbox;

	chase_view_offset = other.chase_view_offset;
	chase_view_rigidity = other.chase_view_rigidity;

	allowed_weapons = other.allowed_weapons;

	restricted_loadout_flag = other.restricted_loadout_flag;
	allowed_bank_restricted_weapons = other.allowed_bank_restricted_weapons;

	shield_icon_index = other.shield_icon_index;
	strcpy_s(icon_filename, other.icon_filename);
	model_icon_angles = other.model_icon_angles;
	strcpy_s(anim_filename, other.anim_filename);
	strcpy_s(overhead_filename, other.overhead_filename);
	selection_effect = other.selection_effect;

	wingmen_status_dot_override = other.wingmen_status_dot_override;

	bii_index_ship = other.bii_index_ship;
	bii_index_ship_with_cargo = other.bii_index_ship_with_cargo;
	bii_index_wing = other.bii_index_wing;
	bii_index_wing_with_cargo = other.bii_index_wing_with_cargo;

	score = other.score;

	scan_time = other.scan_time;
	scan_range_normal = other.scan_range_normal;
	scan_range_capital = other.scan_range_capital;
	scanning_time_multiplier = other.scanning_time_multiplier;
	scanning_range_multiplier = other.scanning_range_multiplier;

	ask_help_shield_percent = other.ask_help_shield_percent;
	ask_help_hull_percent = other.ask_help_hull_percent;

	memcpy(ct_info, other.ct_info, sizeof(trail_info) * MAX_SHIP_CONTRAILS);
	ct_count = other.ct_count;

	memcpy(shield_color, other.shield_color, sizeof(ubyte) * 3);

	uses_team_colors = other.uses_team_colors;
	default_team_name = other.default_team_name;

	afterburner_trail = other.afterburner_trail; 
	afterburner_trail_tex_stretch = other.afterburner_trail_tex_stretch;
	afterburner_trail_width_factor = other.afterburner_trail_width_factor;
	afterburner_trail_alpha_factor = other.afterburner_trail_alpha_factor;
	afterburner_trail_alpha_end_factor = other.afterburner_trail_alpha_end_factor;
	afterburner_trail_alpha_decay_exponent = other.afterburner_trail_alpha_decay_exponent;
	afterburner_trail_life = other.afterburner_trail_life;
	afterburner_trail_faded_out_sections = other.afterburner_trail_faded_out_sections;
	afterburner_trail_spread = other.afterburner_trail_spread;

	normal_thruster_particles = other.normal_thruster_particles;
	afterburner_thruster_particles = other.afterburner_thruster_particles;

	memcpy(&thruster_flame_info, &other.thruster_flame_info, sizeof(thrust_pair));
	memcpy(&thruster_glow_info, &other.thruster_glow_info, sizeof(thrust_pair));
	memcpy(&thruster_secondary_glow_info, &other.thruster_secondary_glow_info, sizeof(thrust_pair_bitmap));
	memcpy(&thruster_tertiary_glow_info, &other.thruster_tertiary_glow_info, sizeof(thrust_pair_bitmap));
	memcpy(&thruster_distortion_info, &other.thruster_distortion_info, sizeof(thrust_pair_bitmap));

	thruster01_glow_rad_factor = other.thruster01_glow_rad_factor;
	thruster02_glow_rad_factor = other.thruster02_glow_rad_factor;
	thruster03_glow_rad_factor = other.thruster03_glow_rad_factor;
	thruster02_glow_len_factor = other.thruster02_glow_len_factor;
	thruster_dist_rad_factor = other.thruster_dist_rad_factor;
	thruster_dist_len_factor = other.thruster_dist_len_factor;
	thruster_glow_noise_mult = other.thruster_glow_noise_mult;

	draw_distortion = other.draw_distortion;

	splodeing_texture = other.splodeing_texture;
	strcpy_s(splodeing_texture_name, other.splodeing_texture_name);

	replacement_textures = other.replacement_textures;

	armor_type_idx = other.armor_type_idx;
	shield_armor_type_idx = other.shield_armor_type_idx;
	
	can_glide = other.can_glide;
	glide_cap = other.glide_cap;
	glide_dynamic_cap = other.glide_dynamic_cap;
	glide_accel_mult = other.glide_accel_mult;
	use_newtonian_damp = other.use_newtonian_damp;
	newtonian_damp_override = other.newtonian_damp_override;

	autoaim_fov = other.autoaim_fov;
	for (int i = 0; i < MAX_SHIP_PRIMARY_BANKS; i++) {
		bank_autoaim_fov[i] = other.bank_autoaim_fov[i];
	}
	autoaim_lock_snd = other.autoaim_lock_snd;
	autoaim_lost_snd = other.autoaim_lost_snd;

	aims_at_flight_cursor = other.aims_at_flight_cursor;
	flight_cursor_aim_extent = other.flight_cursor_aim_extent;

	topdown_offset_def = other.topdown_offset_def;
	topdown_offset = other.topdown_offset;

	engine_snd = other.engine_snd;
	min_engine_vol = other.min_engine_vol;
	glide_start_snd = other.glide_start_snd;
	glide_end_snd = other.glide_end_snd;
	flyby_snd = other.flyby_snd;

	ship_sounds = other.ship_sounds;

	custom_data = other.custom_data;

	custom_strings = other.custom_strings;

	rcs_thrusters = other.rcs_thrusters;

	radar_image_2d_idx = other.radar_image_2d_idx;
	radar_color_image_2d_idx = other.radar_color_image_2d_idx;
	radar_image_size = other.radar_image_size;
	radar_projection_size_mult = other.radar_projection_size_mult;

	ship_iff_info = other.ship_iff_info;

	aiming_flags = other.aiming_flags;
	minimum_convergence_distance = other.minimum_convergence_distance;
	convergence_distance = other.convergence_distance;
	convergence_offset = other.convergence_offset;

	emp_resistance_mod = other.emp_resistance_mod;

	piercing_damage_draw_limit = other.piercing_damage_draw_limit;

	damage_lightning_type = other.damage_lightning_type;

	shield_impact_explosion_anim = other.shield_impact_explosion_anim;

	// We can't copy the HUD gauge vector here since we can't construct a copy of every HUD gauge since the type
	// information is not available anymore when we are at this point. Since this function is only needed before HUD
	// gauge parsing it should be save to assume that the other HUD gauge vector is empty
	Assertion(other.hud_gauges.empty(), "Ship_info cloning is only possible if there are no HUD gauges in the ship class.");
	hud_gauges.clear();

	hud_enabled = other.hud_enabled;
	hud_retail = other.hud_retail;

	displays = other.displays;

	pathMetadata = other.pathMetadata;

	ship_passive_arcs = other.ship_passive_arcs;

	glowpoint_bank_override_map = other.glowpoint_bank_override_map;
}

void ship_info::move(ship_info&& other)
{
	std::swap(name, other.name);
	std::swap(display_name, other.display_name);
	std::swap(short_name, other.short_name);
	species = other.species;
	class_type = other.class_type;

	std::swap(type_str, other.type_str);
	std::swap(maneuverability_str, other.maneuverability_str);
	std::swap(armor_str, other.armor_str);
	std::swap(manufacturer_str, other.manufacturer_str);
	std::swap(desc, other.desc);
	std::swap(tech_desc, other.tech_desc);

	std::swap(tech_title, other.tech_title);

	std::swap(ship_length, other.ship_length);
	std::swap(gun_mounts, other.gun_mounts);
	std::swap(missile_banks, other.missile_banks);

	std::swap(cockpit_pof_file, other.cockpit_pof_file);
	std::swap(cockpit_offset, other.cockpit_offset);
	cockpit_sway_val = other.cockpit_sway_val;
	std::swap(pof_file, other.pof_file);
	std::swap(pof_file_hud, other.pof_file_hud);
	std::swap(pof_file_tech, other.pof_file_tech);
	num_detail_levels = other.num_detail_levels;
	std::swap(detail_distance, other.detail_distance);
	collision_lod = other.collision_lod;

	cockpit_model_num = other.cockpit_model_num;
	model_num = other.model_num;
	model_num_hud = other.model_num_hud;

	hud_target_lod = other.hud_target_lod;
	density = other.density;
	damp = other.damp;
	rotdamp = other.rotdamp;
	delta_bank_const = other.delta_bank_const;
	std::swap(max_vel, other.max_vel);
	std::swap(min_vel, other.min_vel);
	std::swap(max_rotvel, other.max_rotvel);
	std::swap(rotation_time, other.rotation_time);
	srotation_time = other.srotation_time;
	max_rear_vel = other.max_rear_vel;
	forward_accel = other.forward_accel;
	forward_decel = other.forward_decel;
	slide_accel = other.slide_accel;
	slide_decel = other.slide_decel;
	gravity_const = other.gravity_const;
	dying_gravity_const = other.dying_gravity_const;

	warpin_params_index = other.warpin_params_index;
	warpout_params_index = other.warpout_params_index;

	flags = other.flags;
	ai_class = other.ai_class;
	max_speed = other.max_speed;
	min_speed = other.min_speed;
	max_accel = other.max_accel;

	collision_damage_type_idx = other.collision_damage_type_idx;
	std::swap(collision_physics, other.collision_physics);

	std::swap(shockwave, other.shockwave);
	explosion_propagates = other.explosion_propagates;
	explosion_splits_ship = other.explosion_splits_ship;
	big_exp_visual_rad = other.big_exp_visual_rad;
	prop_exp_rad_mult = other.prop_exp_rad_mult;
	death_roll_r_mult = other.death_roll_r_mult;
	death_fx_r_mult = other.death_fx_r_mult;
	death_roll_time_mult = other.death_roll_time_mult;
	death_roll_rotation_mult = other.death_roll_rotation_mult;
	death_roll_xrotation_cap = other.death_roll_xrotation_cap;
	death_roll_yrotation_cap = other.death_roll_yrotation_cap;
	death_roll_zrotation_cap = other.death_roll_zrotation_cap;
	death_roll_base_time = other.death_roll_base_time;
	death_fx_count = other.death_fx_count;
	shockwave_count = other.shockwave_count;
	std::swap(explosion_bitmap_anims, other.explosion_bitmap_anims);
	skip_deathroll_chance = other.skip_deathroll_chance;

	std::swap(impact_spew, other.impact_spew);
	std::swap(damage_spew, other.damage_spew);
	std::swap(split_particles, other.split_particles);
	std::swap(knossos_end_particles, other.knossos_end_particles);
	std::swap(regular_end_particles, other.regular_end_particles);
	std::swap(debris_flame_particles, other.debris_flame_particles);

	debris_min_lifetime = other.debris_min_lifetime;
	debris_max_lifetime = other.debris_max_lifetime;
	debris_min_speed = other.debris_min_speed;
	debris_max_speed = other.debris_max_speed;
	debris_min_rotspeed = other.debris_min_rotspeed;
	debris_max_rotspeed = other.debris_max_rotspeed;
	debris_damage_type_idx = other.debris_damage_type_idx;
	debris_min_hitpoints = other.debris_min_hitpoints;
	debris_max_hitpoints = other.debris_max_hitpoints;
	debris_hitpoints_radius_multi = other.debris_hitpoints_radius_multi;
	debris_damage_mult = other.debris_damage_mult;
	debris_arc_percent = other.debris_arc_percent;
	debris_gravity_const = other.debris_gravity_const;
	debris_density = other.debris_density;
	debris_ambient_sound = other.debris_ambient_sound;
	debris_collision_sound_light = other.debris_collision_sound_light;
	debris_collision_sound_heavy = other.debris_collision_sound_heavy;
	debris_explosion_sound = other.debris_explosion_sound;
	strcpy_s(generic_debris_pof_file, other.generic_debris_pof_file);
	generic_debris_model_num = other.generic_debris_model_num;
	generic_debris_num_submodels = other.generic_debris_num_submodels;
	generic_debris_spew_num = other.generic_debris_spew_num;

	std::swap(subsystems, other.subsystems);
	std::swap(n_subsystems, other.n_subsystems);

	power_output = other.power_output;
	max_overclocked_speed = other.max_overclocked_speed;
	max_weapon_reserve = other.max_weapon_reserve;
	max_shield_regen_per_second = other.max_shield_regen_per_second;
	shield_regen_hit_delay = other.shield_regen_hit_delay;
	max_weapon_regen_per_second = other.max_weapon_regen_per_second;

	shield_weap_amount = other.shield_weap_amount;
	shield_weap_efficiency = other.shield_weap_efficiency;
	shield_weap_speed = other.shield_weap_speed;
	weap_shield_amount = other.weap_shield_amount;
	weap_shield_efficiency = other.weap_shield_efficiency;
	weap_shield_speed = other.weap_shield_speed;

	std::swap(afterburner_max_vel, other.afterburner_max_vel);
	afterburner_forward_accel = other.afterburner_forward_accel;
	afterburner_fuel_capacity = other.afterburner_fuel_capacity;
	afterburner_burn_rate = other.afterburner_burn_rate;
	afterburner_recover_rate = other.afterburner_recover_rate;
	afterburner_max_reverse_vel = other.afterburner_max_reverse_vel;
	afterburner_reverse_accel = other.afterburner_reverse_accel;
	afterburner_min_start_fuel = other.afterburner_min_start_fuel;
	afterburner_min_fuel_to_burn = other.afterburner_min_fuel_to_burn;
	afterburner_cooldown_time = other.afterburner_cooldown_time;

	cmeasure_type = other.cmeasure_type;
	cmeasure_max = other.cmeasure_max;

	num_primary_banks = other.num_primary_banks;
	std::swap(primary_bank_weapons, other.primary_bank_weapons);
	std::swap(primary_bank_ammo_capacity, other.primary_bank_ammo_capacity);

	num_secondary_banks = other.num_secondary_banks;
	std::swap(secondary_bank_weapons, other.secondary_bank_weapons);
	std::swap(secondary_bank_ammo_capacity, other.secondary_bank_ammo_capacity);

	std::swap(draw_primary_models, other.draw_primary_models);
	std::swap(draw_secondary_models, other.draw_secondary_models);
	weapon_model_draw_distance = other.weapon_model_draw_distance;

	max_hull_strength = other.max_hull_strength;
	ship_recoil_modifier = other.ship_recoil_modifier;
	ship_shudder_modifier = other.ship_shudder_modifier;

	for (int i = 0; i < MAX_SHIP_PRIMARY_BANKS; i++) {
		std::swap(dyn_firing_patterns_allowed[i], other.dyn_firing_patterns_allowed[i]);
	}

	max_shield_strength = other.max_shield_strength;
	max_shield_recharge = other.max_shield_recharge;
	auto_shield_spread = other.auto_shield_spread;
	auto_shield_spread_bypass = other.auto_shield_spread_bypass;
	auto_shield_spread_from_lod = other.auto_shield_spread_from_lod;
	auto_shield_spread_min_span = other.auto_shield_spread_min_span;
	max_shield_impact_effect_radius = other.max_shield_impact_effect_radius;

	std::swap(shield_point_augment_ctrls, other.shield_point_augment_ctrls);

	hull_repair_rate = other.hull_repair_rate;
	subsys_repair_rate = other.subsys_repair_rate;

	hull_repair_max = other.hull_repair_max;
	subsys_repair_max = other.subsys_repair_max;

	sup_hull_repair_rate = other.sup_hull_repair_rate;
	sup_shield_repair_rate = other.sup_shield_repair_rate;
	sup_subsys_repair_rate = other.sup_subsys_repair_rate;

	std::swap(closeup_pos, other.closeup_pos);
	closeup_zoom = other.closeup_zoom;

	std::swap(closeup_pos_targetbox, other.closeup_pos_targetbox);
	closeup_zoom_targetbox = other.closeup_zoom_targetbox;

	std::swap(chase_view_offset, other.chase_view_offset);
	chase_view_rigidity = other.chase_view_rigidity;

	std::swap(allowed_weapons, other.allowed_weapons);

	std::swap(restricted_loadout_flag, other.restricted_loadout_flag);
	std::swap(allowed_bank_restricted_weapons, other.allowed_bank_restricted_weapons);

	shield_icon_index = other.shield_icon_index;
	std::swap(icon_filename, other.icon_filename);
	model_icon_angles = other.model_icon_angles;
	std::swap(anim_filename, other.anim_filename);
	std::swap(overhead_filename, other.overhead_filename);
	selection_effect = other.selection_effect;

	wingmen_status_dot_override = other.wingmen_status_dot_override;

	bii_index_ship = other.bii_index_ship;
	bii_index_ship_with_cargo = other.bii_index_ship_with_cargo;
	bii_index_wing = other.bii_index_wing;
	bii_index_wing_with_cargo = other.bii_index_wing_with_cargo;

	score = other.score;

	scan_time = other.scan_time;
	scan_range_normal = other.scan_range_normal;
	scan_range_capital = other.scan_range_capital;
	scanning_time_multiplier = other.scanning_time_multiplier;
	scanning_range_multiplier = other.scanning_range_multiplier;

	ask_help_shield_percent = other.ask_help_shield_percent;
	ask_help_hull_percent = other.ask_help_hull_percent;

	std::swap(ct_info, other.ct_info);
	ct_count = other.ct_count;

	std::swap(shield_color, other.shield_color);

	uses_team_colors = other.uses_team_colors;
	std::swap(default_team_name, other.default_team_name);

	std::swap(afterburner_trail, other.afterburner_trail);
	afterburner_trail_tex_stretch = other.afterburner_trail_tex_stretch;
	afterburner_trail_width_factor = other.afterburner_trail_width_factor;
	afterburner_trail_alpha_factor = other.afterburner_trail_alpha_factor;
	afterburner_trail_alpha_end_factor = other.afterburner_trail_alpha_end_factor;
	afterburner_trail_alpha_decay_exponent = other.afterburner_trail_alpha_decay_exponent;
	afterburner_trail_life = other.afterburner_trail_life;
	afterburner_trail_faded_out_sections = other.afterburner_trail_faded_out_sections;
	afterburner_trail_spread = other.afterburner_trail_spread;

	std::swap(normal_thruster_particles, other.normal_thruster_particles);
	std::swap(afterburner_thruster_particles, other.afterburner_thruster_particles);

	std::swap(thruster_flame_info, other.thruster_flame_info);
	std::swap(thruster_glow_info, other.thruster_glow_info);
	std::swap(thruster_secondary_glow_info, other.thruster_secondary_glow_info);
	std::swap(thruster_tertiary_glow_info, other.thruster_tertiary_glow_info);
	std::swap(thruster_distortion_info, other.thruster_distortion_info);

	thruster01_glow_rad_factor = other.thruster01_glow_rad_factor;
	thruster02_glow_rad_factor = other.thruster02_glow_rad_factor;
	thruster03_glow_rad_factor = other.thruster03_glow_rad_factor;
	thruster02_glow_len_factor = other.thruster02_glow_len_factor;
	thruster_dist_rad_factor = other.thruster_dist_rad_factor;
	thruster_dist_len_factor = other.thruster_dist_len_factor;
	thruster_glow_noise_mult = other.thruster_glow_noise_mult;

	draw_distortion = other.draw_distortion;

	splodeing_texture = other.splodeing_texture;
	std::swap(splodeing_texture_name, other.splodeing_texture_name);

	std::swap(replacement_textures, other.replacement_textures);

	armor_type_idx = other.armor_type_idx;
	shield_armor_type_idx = other.shield_armor_type_idx;
	
	can_glide = other.can_glide;
	glide_cap = other.glide_cap;
	glide_dynamic_cap = other.glide_dynamic_cap;
	glide_accel_mult = other.glide_accel_mult;
	use_newtonian_damp = other.use_newtonian_damp;
	newtonian_damp_override = other.newtonian_damp_override;

	autoaim_fov = other.autoaim_fov;
	for (int i = 0; i < MAX_SHIP_PRIMARY_BANKS; i++) {
		bank_autoaim_fov[i] = other.bank_autoaim_fov[i];
	}
	autoaim_lock_snd = other.autoaim_lock_snd;
	autoaim_lost_snd = other.autoaim_lost_snd;

	aims_at_flight_cursor = other.aims_at_flight_cursor;
	flight_cursor_aim_extent = other.flight_cursor_aim_extent;

	topdown_offset_def = other.topdown_offset_def;
	std::swap(topdown_offset, other.topdown_offset);

	engine_snd = other.engine_snd;
	min_engine_vol = other.min_engine_vol;
	glide_start_snd = other.glide_start_snd;
	glide_end_snd = other.glide_end_snd;
	flyby_snd  = other.flyby_snd;

	std::swap(ship_sounds, other.ship_sounds);

	std::swap(custom_data, other.custom_data);

	std::swap(custom_strings, other.custom_strings);

	std::swap(rcs_thrusters, other.rcs_thrusters);

	radar_image_2d_idx = other.radar_image_2d_idx;
	radar_color_image_2d_idx = other.radar_color_image_2d_idx;
	radar_image_size = other.radar_image_size;
	radar_projection_size_mult = other.radar_projection_size_mult;

	ship_iff_info = other.ship_iff_info;

	aiming_flags = other.aiming_flags;
	minimum_convergence_distance = other.minimum_convergence_distance;
	convergence_distance = other.convergence_distance;
	std::swap(convergence_offset, other.convergence_offset);

	emp_resistance_mod = other.emp_resistance_mod;

	piercing_damage_draw_limit = other.piercing_damage_draw_limit;

	damage_lightning_type = other.damage_lightning_type;

	shield_impact_explosion_anim = other.shield_impact_explosion_anim;
	std::swap(hud_gauges, other.hud_gauges);
	hud_enabled = other.hud_enabled;
	hud_retail = other.hud_retail;

	std::swap(displays, other.displays);

	std::swap(pathMetadata, other.pathMetadata);

	std::swap(ship_passive_arcs, other.ship_passive_arcs);

	std::swap(glowpoint_bank_override_map, other.glowpoint_bank_override_map);

	animations = std::move(other.animations);
	cockpit_animations = std::move(other.cockpit_animations);
}

ship_info &ship_info::operator= (ship_info&& other) noexcept
{
	if (this != &other) {
		move(std::move(other));
	}
	return *this;
}

ship_info::ship_info(ship_info&& other) noexcept
{
	// MageKing17 - Initialize these pointers to NULL because otherwise move() will leave them uninitialized.
	subsystems = NULL;
	n_subsystems = 0;

	// Then we swap everything (well, everything that matters to the deconstructor).
	move(std::move(other));
}

ship_info::ship_info()
{
	name[0] = '\0';
	display_name[0] = '\0';
	sprintf(short_name, "ShipClass%d", ship_info_size());
	species = 0;
	class_type = -1;

	tech_title[0] = 0;

	cockpit_pof_file[0] = '\0';
	vm_vec_zero(&cockpit_offset);
	cockpit_sway_val = 0.0;
	pof_file[0] = '\0';
	pof_file_hud[0] = '\0';
	pof_file_tech[0] = '\0';
	num_detail_levels = 1;
	detail_distance[0] = 0;
	collision_lod = -1;
	cockpit_model_num = -1;
	model_num = -1;
	model_num_hud = -1;
	hud_target_lod = -1;

	density = 1.0f;
	damp = 0.0f;
	rotdamp = 0.0f;
	delta_bank_const = DEFAULT_DELTA_BANK_CONST;
	vm_vec_zero(&max_vel);
	vm_vec_zero(&min_vel);
	vm_vec_zero(&max_rotvel);
	vm_vec_zero(&rotation_time);
	srotation_time = 0.0f;
	max_rear_vel = 0.0f;
	forward_accel = 0.0f;
	forward_decel = 0.0f;
	slide_accel = 0.0f;
	slide_decel = 0.0f;
	gravity_const = 0.0f;
	dying_gravity_const = 0.0f;

	warpin_params_index = -1;
	warpout_params_index = -1;

	flags.reset();
	ai_class = 0;
	max_speed = 0.0f;
	min_speed = 0.0f;
	max_accel = 0.0f;

	collision_damage_type_idx = -1;
	// Retail default collision physics and default landing parameters
	collision_physics = ship_collision_physics();
	collision_physics.both_small_bounce = 5.0;
	collision_physics.bounce = 5.0;
	collision_physics.friction = COLLISION_FRICTION_FACTOR;
	collision_physics.rotation_factor = COLLISION_ROTATION_FACTOR;
	collision_physics.reorient_mult = 1.0f;
	collision_physics.landing_sound_idx = gamesnd_id();
	collision_physics.collision_sound_light_idx = gamesnd_id();
	collision_physics.collision_sound_heavy_idx = gamesnd_id();
	collision_physics.collision_sound_shielded_idx = gamesnd_id();

	shockwave_create_info_init(&shockwave);
	explosion_propagates = 0;
	explosion_splits_ship = false;
	big_exp_visual_rad = -1.0f;
	prop_exp_rad_mult = 1.0f;
	death_roll_r_mult = 1.0f;
	death_fx_r_mult = 1.0f;
	death_roll_time_mult = 1.0f;
	death_roll_rotation_mult = 1.0f;
	death_roll_xrotation_cap = 0.75f*DEATHROLL_ROTVEL_CAP;
	death_roll_yrotation_cap = 0.75f*DEATHROLL_ROTVEL_CAP;
	death_roll_zrotation_cap = 0.75f*DEATHROLL_ROTVEL_CAP;
	death_roll_base_time = 3000;
	death_fx_count = 6;
	shockwave_count = 1;
	explosion_bitmap_anims.clear();
	skip_deathroll_chance = 0.0f;

	// default values from shipfx.cpp
	static auto default_impact_spew = default_ship_particle_effect(LegacyShipParticleType::OTHER, 30, 25, 0.5f, 0.2f, 0.55f, 0.05f, 12.0f, 2.0f, 1.0f, 1.0f, particle::Anim_bitmap_id_fire, 0.7f);
	impact_spew = default_impact_spew;

	// default values from shipfx.cpp
	static auto default_damage_spew = default_ship_particle_effect(LegacyShipParticleType::DAMAGE_SPEW, 1, 0, 1.3f, 0.7f, 1.0f, 1.0f, 12.0f, 3.0f, 0.0f, 1.0f, particle::Anim_bitmap_id_smoke, 0.7f);
	damage_spew = default_damage_spew;

	static auto default_split_particles = default_ship_particle_effect(LegacyShipParticleType::SPLIT_PARTICLES, 80, 40, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 2.0f, 1.0f, particle::Anim_bitmap_id_smoke2, 1.f);
	split_particles = default_split_particles;

	static auto default_knossos_end_particles = default_ship_particle_effect(LegacyShipParticleType::OTHER, 30, 15, 100.0f, 30.0f, 12.0f, 2.0f, 350.0f, 50.0f, 2.0f, 50.0f, particle::Anim_bitmap_id_smoke2, 1.f);
	knossos_end_particles = default_knossos_end_particles;

	static auto default_regular_end_particles = default_ship_particle_effect(LegacyShipParticleType::OTHER, 100, 50, 1.5f, 0.1f, 4.0f, 0.5f, 20.0f, 0.0f, 2.0f, 1.0f, particle::Anim_bitmap_id_smoke2, 1.f, true);
	regular_end_particles = default_regular_end_particles;

	debris_flame_particles = particle::ParticleEffectHandle::invalid();

	debris_min_lifetime = -1.0f;
	debris_max_lifetime = -1.0f;
	debris_min_speed = -1.0f;
	debris_max_speed = -1.0f;
	debris_min_rotspeed = -1.0f;
	debris_max_rotspeed = -1.0f;
	debris_hitpoints_radius_multi = -1.0f;
	debris_damage_type_idx = -1;
	debris_min_hitpoints = -1.0f;
	debris_max_hitpoints = -1.0f;
	debris_damage_mult = 1.0f;
	debris_arc_percent = 0.5f;
	debris_gravity_const = 1.0f;
	debris_density = 1.0f;
	debris_ambient_sound = GameSounds::DEBRIS;
	debris_collision_sound_light = gamesnd_id();
	debris_collision_sound_heavy = gamesnd_id();
	debris_explosion_sound = GameSounds::MISSILE_IMPACT1;
	generic_debris_pof_file[0] = '\0';
	generic_debris_model_num = -1;
	generic_debris_num_submodels = -1;
	generic_debris_spew_num = 20;

	n_subsystems = 0;
	subsystems = NULL;

	power_output = 0.0f;
	max_overclocked_speed = 0.0f;
	max_weapon_reserve = 0.0f;
	max_shield_regen_per_second = 0.0f;
	shield_regen_hit_delay = 0.0f;
	max_weapon_regen_per_second = 0.0f;

	shield_weap_amount = 0.0f;
	shield_weap_efficiency = 0.0f;
	shield_weap_speed = 0.0f;
	weap_shield_amount = 0.0f;
	weap_shield_efficiency = 0.0f;
	weap_shield_speed = 0.0f;

	vm_vec_zero(&afterburner_max_vel);
	afterburner_forward_accel = 0.0f;
	afterburner_fuel_capacity = 0.0f;
	afterburner_burn_rate = 0.0f;
	afterburner_recover_rate = 0.0f;
	afterburner_max_reverse_vel = 0.0f;
	afterburner_reverse_accel = 0.0f;
	afterburner_min_start_fuel = DEFAULT_MIN_AFTERBURNER_FUEL_TO_ENGAGE;
	afterburner_min_fuel_to_burn = 0.0f;
	afterburner_cooldown_time = 0.0f;

	cmeasure_type = Default_cmeasure_index;
	cmeasure_max = 0;

	num_primary_banks = 0;
	for (int i = 0; i < MAX_SHIP_PRIMARY_BANKS; i++ )
	{
		primary_bank_weapons[i] = -1;
		draw_primary_models[i] = false;
		primary_bank_ammo_capacity[i] = 0;
		dyn_firing_patterns_allowed[i].push_back(FiringPattern::CYCLE_FORWARD);
	}
	
	num_secondary_banks = 0;
	for (int i = 0; i < MAX_SHIP_SECONDARY_BANKS; i++ )
	{
		secondary_bank_weapons[i] = -1;
		draw_secondary_models[i] = false;
		secondary_bank_ammo_capacity[i] = 0;
	}

	weapon_model_draw_distance = 200.0f;

	ship_recoil_modifier = 1.0f;
	ship_shudder_modifier = 1.0f;

	max_hull_strength = 100.0f;
	max_shield_strength = 0.0f;

	max_shield_recharge = 1.0f;

	auto_shield_spread = 0.0f;
	auto_shield_spread_bypass = false;
	auto_shield_spread_from_lod = -1;
	auto_shield_spread_min_span = -1.0f;

	max_shield_impact_effect_radius = -1.0f;

	for (int i = 0; i < 4; i++)
	{
		shield_point_augment_ctrls[i] = -1;
	}

	hull_repair_rate = 0.0f;
	// -2 represents not set, in which case the default is used for the ship (if it is small)
	subsys_repair_rate = -2.0f;

	hull_repair_max = 1.0f;
	subsys_repair_max = 1.0f;

	sup_hull_repair_rate = 0.15f;
	sup_shield_repair_rate = 0.20f;
	sup_subsys_repair_rate = 0.15f;		// The retail default is 0.10, but it actually used the hull rate of 0.15

	vm_vec_zero(&closeup_pos);
	closeup_zoom = 0.5f;

	vm_vec_zero(&closeup_pos_targetbox);
	closeup_zoom_targetbox = 0.5f;

	vm_vec_zero(&chase_view_offset);
	chase_view_rigidity = 5.0f;

	allowed_weapons.clear();

	restricted_loadout_flag.clear();
	allowed_bank_restricted_weapons.clear();

	shield_icon_index = 255;		// stored as ubyte
	icon_filename[0] = '\0';
	memset(&model_icon_angles, 0, sizeof(angles));
	anim_filename[0] = '\0';
	overhead_filename[0] = '\0';

	selection_effect = Default_ship_select_effect;

	wingmen_status_dot_override = -1;

	bii_index_ship = -1;
	bii_index_ship_with_cargo = -1;
	bii_index_wing = -1;
	bii_index_wing_with_cargo = -1;

	score = 0;

	scan_time = 2000;
	scan_range_normal = CARGO_REVEAL_MIN_DIST;
	scan_range_capital = CAP_CARGO_REVEAL_MIN_DIST;
	scanning_time_multiplier = 1.0f;
	scanning_range_multiplier = 1.0f;

	ask_help_shield_percent = DEFAULT_ASK_HELP_SHIELD_PERCENT;
	ask_help_hull_percent = DEFAULT_ASK_HELP_HULL_PERCENT;

	memset(&ct_info, 0, sizeof(trail_info) * MAX_SHIP_CONTRAILS);
	ct_count = 0;

	shield_color[0] = 255;
	shield_color[1] = 255;
	shield_color[2] = 255;

	// Team colors
	uses_team_colors = false;
	default_team_name = "";

	generic_bitmap_init(&afterburner_trail, NULL);
	afterburner_trail_tex_stretch = 1.0f;
	afterburner_trail_width_factor = 1.0f;
	afterburner_trail_alpha_factor = 1.0f;
	afterburner_trail_alpha_end_factor = 0.0f;
	afterburner_trail_alpha_decay_exponent = 1.0f;
	afterburner_trail_life = 5.0f;
	afterburner_trail_spread = 0.0f;
	afterburner_trail_faded_out_sections = 0;

	normal_thruster_particles.clear();
	afterburner_thruster_particles.clear();

	// Bobboau's thruster stuff
	generic_anim_init( &thruster_flame_info.normal );
	generic_anim_init( &thruster_flame_info.afterburn );
	generic_anim_init( &thruster_glow_info.normal );
	generic_anim_init( &thruster_glow_info.afterburn );
	generic_bitmap_init( &thruster_secondary_glow_info.normal );
	generic_bitmap_init( &thruster_secondary_glow_info.afterburn );
	generic_bitmap_init( &thruster_tertiary_glow_info.normal );
	generic_bitmap_init( &thruster_tertiary_glow_info.afterburn );
	generic_bitmap_init( &thruster_distortion_info.normal );
	generic_bitmap_init( &thruster_distortion_info.afterburn );

	// Bobboau's thruster stuff
	thruster01_glow_rad_factor = 1.0f;
	thruster02_glow_rad_factor = 1.0f;
	thruster03_glow_rad_factor = 1.0f;
	thruster02_glow_len_factor = 1.0f;
	thruster_dist_rad_factor = 2.0f;
	thruster_dist_len_factor = 2.0f;
	thruster_glow_noise_mult = 1.0f;

	draw_distortion = true;

	splodeing_texture = -1;
	strcpy_s(splodeing_texture_name, "boom");

	replacement_textures.clear();

	armor_type_idx = -1;
	shield_armor_type_idx = -1;

	can_glide = false;
	glide_cap = 0.0f;
	glide_dynamic_cap = false;
	glide_accel_mult = 0.0f;
	use_newtonian_damp = false;
	newtonian_damp_override = false;

	autoaim_fov = 0.0f;
	for (int i = 0; i < MAX_SHIP_PRIMARY_BANKS; i++) {
		bank_autoaim_fov[i] = 0.0f;
	}
	autoaim_lock_snd = gamesnd_id();
	autoaim_lost_snd = gamesnd_id();

	aims_at_flight_cursor = false;
	flight_cursor_aim_extent = -1.0f;

	topdown_offset_def = false;
	vm_vec_zero(&topdown_offset);

	engine_snd = gamesnd_id();
	min_engine_vol = -1.0f;
	glide_start_snd = gamesnd_id();
	glide_end_snd = gamesnd_id();
	flyby_snd = gamesnd_id();

	ship_sounds.clear();

	rcs_thrusters.clear();

	custom_data.clear();

	custom_strings.clear();

	radar_image_2d_idx = -1;
	radar_color_image_2d_idx = -1;
	radar_image_size = -1;
	radar_projection_size_mult = 1.0f;

	ship_iff_info.clear();

	aiming_flags.reset();
	minimum_convergence_distance = 0.0f;
	convergence_distance = 100.0f;
	vm_vec_zero(&convergence_offset);

	emp_resistance_mod = 0.0f;

	piercing_damage_draw_limit = 0.10f;

	damage_lightning_type = SLT_DEFAULT;

	shield_impact_explosion_anim = particle::ParticleEffectHandle::invalid();
	hud_gauges.clear();
	hud_enabled = false;
	hud_retail = false;

	displays.clear();

	pathMetadata.clear();

	glowpoint_bank_override_map.clear();

	ship_passive_arcs.clear();
}

ship_info::~ship_info()
{
	if ( subsystems != NULL ) {
		delete[] subsystems;
		subsystems = nullptr;
	}
}

const char* ship_info::get_display_name() const
{
	if (has_display_name())
		return display_name;
	else
		return name;
}

bool ship_info::has_display_name() const
{
	return flags[Ship::Info_Flags::Has_display_name];
}

ubyte allowed_weapon_bank::find_flags(int weapon_info_index) const
{
	for (auto &wf : weapon_and_flags)
		if (wf.first == weapon_info_index)
			return wf.second;

	// since we can return 0 here, we can't make the return type 'const ubyte &'
	return 0;
}

void allowed_weapon_bank::set_flag(int weapon_info_index, ubyte flag)
{
	for (auto &wf : weapon_and_flags)
	{
		if (wf.first == weapon_info_index)
		{
			wf.second |= flag;
			return;
		}
	}

	// if we couldn't find it, add a new entry with that flag
	weapon_and_flags.emplace_back(weapon_info_index, flag);
}

void allowed_weapon_bank::clear_flag(int weapon_info_index, ubyte flag)
{
	// clear the flag from all matching weapons, and remove any that end up with a flag of 0
	weapon_and_flags.erase(
		std::remove_if(weapon_and_flags.begin(), weapon_and_flags.end(), [weapon_info_index, flag](std::pair<int, ubyte> &wf)
		{
			if (weapon_info_index < 0 || weapon_info_index == wf.first)
			{
				wf.second &= ~flag;
				return (wf.second == 0);
			}
			else
				return false;
		}),
		weapon_and_flags.end()
	);
}

void allowed_weapon_bank::clear_flag(ubyte flag)
{
	clear_flag(-1, flag);
}

void allowed_weapon_bank::clear()
{
	weapon_and_flags.clear();
}

// read-only!
// (because we don't want to fill up our member vector with a bunch of zeros)
ubyte allowed_weapon_bank::operator[](int index) const
{
	return find_flags(index);
}
ubyte allowed_weapon_bank::operator[](size_t index) const
{
	return find_flags(static_cast<int>(index));
}


static SCP_vector<SCP_string> Removed_ships;

/**
 * Parse the information for a specific ship type.
 */
static void parse_ship(const char *filename, bool replace)
{
	char fname[NAME_LENGTH];
	ship_info *sip = nullptr;
	bool first_time = false;
	bool create_if_not_found = true;
	bool remove_ship = false;
	bool new_name = false;

	required_string("$Name:");
	stuff_string(fname, F_NAME, NAME_LENGTH);

	// Remove @ symbol -- these used to denote ships that would only be parsed in demo builds
	if (fname[0] == '@') {
		backspace(fname);
	}

	diag_printf("Ship name -- %s\n", fname);

	if (optional_string("+nocreate")) {
		if (!replace) {
			Warning(LOCATION, "+nocreate flag used for ship in non-modular table");
		}
		create_if_not_found = false;
	}

	// check first if this is on the remove blacklist
	auto it = std::find(Removed_ships.begin(), Removed_ships.end(), fname);
	if (it != Removed_ships.end()) {
		remove_ship = true;
	}

	// we might have a remove tag
	if (optional_string("+remove")) {
		if (!replace) {
			Warning(LOCATION, "+remove flag used for ship in non-modular table");
		}
		if (!remove_ship) {
			Removed_ships.push_back(fname);
			remove_ship = true;
		}
	}

	//Check if ship exists already
	int ship_id = ship_info_lookup_sub(fname);

	// maybe remove it
	if (remove_ship)
	{
		if (ship_id >= 0) {
			mprintf(("Removing previously parsed ship '%s'\n", fname));
			Ship_info.erase(Ship_info.begin() + ship_id);
		}

		if (!skip_to_start_of_string_either("$Name:", "#End")) {
			error_display(1, "Missing [#End] or [$Name] after ship class %s", fname);
		}
		return;
	}
	
	// an entry for this ship exists
	if (ship_id >= 0)
	{
		sip = &Ship_info[ship_id];
		if (!replace)
		{
			Warning(LOCATION, "Error:  Ship name %s already exists in %s.  All ship class names must be unique.", fname, filename);
			if (!skip_to_start_of_string_either("$Name:", "#End")) {
				error_display(1, "Missing [#End] or [$Name] after duplicate ship class %s", fname);
			}
			return;
		}
	}
	// an entry does not exist
	else
	{
		// Don't create ship if it has +nocreate and is in a modular table.
		if (!create_if_not_found && replace)
		{
			if (!skip_to_start_of_string_either("$Name:", "#End")) {
				error_display(1, "Missing [#End] or [$Name] after ship class %s", fname);
			}
			return;
		}

		// Check if there are too many ship classes
		if (Ship_info.size() >= MAX_SHIP_CLASSES) {
			Error(LOCATION, "Too many ship classes before '%s'; maximum is %d.\n", fname, MAX_SHIP_CLASSES);
		}

		Ship_info.push_back(ship_info());
		sip = &Ship_info.back();
		first_time = true;

		strcpy_s(sip->name, fname);
		new_name = true;
	}

	// Use a template for this ship.
	if( optional_string( "+Use Template:" ) ) {
		if ( !create_if_not_found ) {
			Warning(LOCATION, "Both '+nocreate' and '+Use Template:' were specified for ship class '%s', ignoring '+Use Template:'\n", fname);
		}
		else {
			char template_name[SHIP_MULTITEXT_LENGTH];
			stuff_string(template_name, F_NAME, SHIP_MULTITEXT_LENGTH);
			int template_id = ship_template_lookup(template_name);
			if ( template_id != -1 ) {
				first_time = false;
				sip->clone(Ship_templates[template_id]);
				strcpy_s(sip->name, fname);
				new_name = true;
				create_if_not_found = false;
			}
			else {
				Warning(LOCATION, "Unable to find ship template '%s' requested by ship class '%s', ignoring template request...", template_name, fname);
			}
		}
	}
	if( optional_string( "+Use Ship as Template:" ) ) {
		if ( !create_if_not_found ) {
			Warning(LOCATION, "Either '+nocreate' or '+Use Template:' were specified with '+Use Ship as Template:' for ship class '%s', ignoring '+Use Ship as Template:'\n", fname);
		}
		else {
			char template_name[SHIP_MULTITEXT_LENGTH];
			stuff_string(template_name, F_NAME, SHIP_MULTITEXT_LENGTH);
			int template_id = ship_info_lookup_sub(template_name);
			if ( template_id != -1 ) {
				first_time = false;
				sip->clone(Ship_info[template_id]);
				strcpy_s(sip->name, fname);
				new_name = true;
			}
			else {
				Warning(LOCATION, "Unable to find ship class '%s' (name must be exact, check table loading order) requested by ship class '%s', ignoring template request...", template_name, fname);
			}
		}
	}

	if (new_name) {
		if (!sip->flags[Ship::Info_Flags::Has_display_name]) {
			// if this name has a hash, create a default display name
			if (get_pointer_to_first_hash_symbol(sip->name)) {
				strcpy_s(sip->display_name, sip->name);
				end_string_at_first_hash_symbol(sip->display_name);
				sip->flags.set(Ship::Info_Flags::Has_display_name);
			}
		}

		sip->animations.changeShipName(sip->name);
		sip->cockpit_animations.changeShipName(sip->name);
	}

	parse_ship_values(sip, false, first_time, replace);
}

/**
 * Parse the information for a specific ship type template.
 */
static void parse_ship_template()
{
	char buf[SHIP_MULTITEXT_LENGTH];
	ship_info *sip;
	bool first_time = true;

	required_string("$Template:");
	stuff_string(buf, F_NAME, SHIP_MULTITEXT_LENGTH);
	
	if( optional_string("+nocreate") ) {
		Warning(LOCATION, "+nocreate flag used on ship template. Ship templates can not be modified. Ignoring +nocreate.");
	}
	
	diag_printf ("Ship template name -- %s\n", buf);
	//Check if the template exists already
	int template_id;
	template_id = ship_template_lookup( buf );
	
	if( template_id != -1 ) {
		sip = &Ship_templates[template_id];
		Warning(LOCATION, "WARNING: Ship template %s already exists. All ship template names must be unique.", sip->name);
		if (!skip_to_start_of_string_either("$Template:", "#End")) {
			error_display(1, "Missing [#End] or [$Template] after duplicate ship template %s", sip->name);
		}
		return;
	}
	else {
		
		Ship_templates.push_back(ship_info());
		sip = &Ship_templates.back();
		
		strcpy_s(sip->name, buf);
		//Use another template for this template. This allows for template hierarchies. - Turey
		if( optional_string("+Use Template:") ) {
			char template_name[SHIP_MULTITEXT_LENGTH];
			stuff_string(template_name, F_NAME, SHIP_MULTITEXT_LENGTH);
			template_id = ship_template_lookup( template_name);
			
			if ( template_id != -1 ) {
				first_time = false;
				sip->clone(Ship_templates[template_id]);
				strcpy_s(sip->name, buf);
				sip->animations.changeShipName(sip->name);
				sip->cockpit_animations.changeShipName(sip->name);
			}
			else {
				Warning(LOCATION, "Unable to find ship template '%s' requested by ship template '%s', ignoring template request...", template_name, buf);
			}
		}
	}
	
	parse_ship_values( sip, true, first_time, false );
}

static void parse_ship_sound(const char *name, GameSounds id, ship_info *sip)
{
	Assert( name != NULL );

	gamesnd_id temp_index;

	if (parse_game_sound(name, &temp_index))
		sip->ship_sounds.insert(std::make_pair(id, temp_index));
}

static void parse_ship_sounds(ship_info *sip)
{
	parse_ship_sound("$CockpitEngineSnd:",                GameSounds::ENGINE, sip);
	parse_ship_sound("$FullThrottleSnd:",                 GameSounds::FULL_THROTTLE, sip);
	parse_ship_sound("$ZeroThrottleSnd:",                 GameSounds::ZERO_THROTTLE, sip);
	parse_ship_sound("$ThrottleUpSnd:",                   GameSounds::THROTTLE_UP, sip);
	parse_ship_sound("$ThrottleDownSnd:",                 GameSounds::THROTTLE_DOWN, sip);
	parse_ship_sound("$AfterburnerSnd:",                  GameSounds::ABURN_LOOP, sip);
	parse_ship_sound("$AfterburnerEngageSnd:",            GameSounds::ABURN_ENGAGE, sip);
	parse_ship_sound("$AfterburnerFailedSnd:",            GameSounds::ABURN_FAIL, sip);
	parse_ship_sound("$MissileTrackingSnd:",              GameSounds::MISSILE_TRACKING, sip);
	parse_ship_sound("$MissileLockedSnd:",                GameSounds::MISSILE_LOCK, sip);
	parse_ship_sound("$PrimaryCycleSnd:",                 GameSounds::PRIMARY_CYCLE, sip);
	parse_ship_sound("$SecondaryCycleSnd:",               GameSounds::SECONDARY_CYCLE, sip);
	parse_ship_sound("$TargetAcquiredSnd:",               GameSounds::TARGET_ACQUIRE, sip);
	parse_ship_sound("$PrimaryFireFailedSnd:",            GameSounds::OUT_OF_WEAPON_ENERGY, sip);
	parse_ship_sound("$SecondaryFireFailedSnd:",          GameSounds::OUT_OF_MISSLES, sip);
	parse_ship_sound("$HeatSeekerLaunchWarningSnd:",      GameSounds::HEATLOCK_WARN, sip);
	parse_ship_sound("$AspectSeekerLaunchWarningSnd:",    GameSounds::ASPECTLOCK_WARN, sip);
	parse_ship_sound("$MissileLockWarningSnd:",           GameSounds::THREAT_FLASH, sip);
	parse_ship_sound("$HeatSeekerProximityWarningSnd:",   GameSounds::PROXIMITY_WARNING, sip);
	parse_ship_sound("$AspectSeekerProximityWarningSnd:", GameSounds::PROXIMITY_ASPECT_WARNING, sip);
	parse_ship_sound("$MissileEvadedSnd:",                GameSounds::MISSILE_EVADED_POPUP, sip);
	parse_ship_sound("$CargoScanningSnd:",                GameSounds::CARGO_SCAN, sip);

	parse_ship_sound("$DeathRollSnd:",                    GameSounds::DEATH_ROLL, sip);
	parse_ship_sound("$ExplosionSnd:",                    GameSounds::SHIP_EXPLODE_1, sip);
	parse_ship_sound("$SubsysExplosionSnd:",              GameSounds::SUBSYS_EXPLODE, sip);
}

static inline void parse_ship_particle_random_range_parse_helper(float* value) {
	stuff_float(value);
}

static inline void parse_ship_particle_random_range_parse_helper(int* value) {
	stuff_int(value);
}

template<typename T_parse, typename T_range = T_parse>
static ::util::UniformRange<T_range> parse_ship_particle_random_range(const char *min_name, const char *max_name, const char* id_string, const char *error_name, const char* shipname, bool min_can_be_0) {
	constexpr const char* const non_neg_print_str = std::is_integral<T_parse>::value ? "Bad value %i, defined as %s particle %s (%s) in ship '%s'.\nValue should be non-negative.\n" : "Bad value %f, defined as %s particle %s (%s) in ship '%s'.\nValue should be non-negative.\n";
	constexpr const char* const pos_print_str = std::is_integral<T_parse>::value ? "Bad value %i, defined as %s particle %s (%s) in ship '%s'.\nValue should be positive.\n" : "Bad value %f, defined as %s particle %s (%s) in ship '%s'.\nValue should be positive.\n";
	T_parse uninitialized = min_can_be_0 ? static_cast<T_parse>(0) : (static_cast<T_parse>(std::numeric_limits<T_range>::min()) + (std::is_integral<T_parse>::value ? 1 : 0));
	T_parse max_value = uninitialized, min_value = uninitialized;

	if(optional_string(max_name))
	{
		parse_ship_particle_random_range_parse_helper(&max_value);
		if (max_value < 0 || (max_value <= 0.0f && !min_can_be_0)) {
			Warning(LOCATION, min_can_be_0 ? non_neg_print_str : pos_print_str, max_value, id_string, error_name, "min", shipname);
			max_value = uninitialized;
		}
	}

	if(optional_string(min_name))
	{
		parse_ship_particle_random_range_parse_helper(&min_value);
		if (min_value < 0 || (min_value <= 0.0f && !min_can_be_0)) {
			Warning(LOCATION, min_can_be_0 ? non_neg_print_str : pos_print_str, min_value, id_string, error_name, "max", shipname);
			min_value = uninitialized;
		}
	}
	if (min_value >= max_value)
		return ::util::UniformRange<T_range>(static_cast<T_range>(max_value));
	else
		return ::util::UniformRange<T_range>(static_cast<T_range>(min_value), static_cast<T_range>(max_value));
}

particle::ParticleEffectHandle create_ship_legacy_particle_effect(LegacyShipParticleType type, float range, int bitmap, ::util::UniformFloatRange particle_num, ::util::UniformFloatRange radius, ::util::UniformFloatRange lifetime, ::util::UniformFloatRange velocity, float normal_variance, bool useNormal, float velocityInherit)
{
	//Unfortunately legacy ship effects did a lot of ad-hoc computation of effect parameters.
	//To mimic this in the modern system, these ad-hoc parameters are represented as hard-coded modular curves applied to various parts of the effect
	std::optional<modular_curves_entry> part_number_curve, lifetime_curve, radius_curve, velocity_curve;
	std::optional<modular_curves_entry> variance_curve;

	switch (type) {
	case LegacyShipParticleType::DAMAGE_SPEW: {
		static const int damage_spew_curve = []() -> int {
			int curve_id = static_cast<int>(Curves.size());
			auto& curve = Curves.emplace_back(";ShipParticleDamageSpew");
			curve.keyframes.emplace_back(curve_keyframe{vec2d{0.f, 1.f}, CurveInterpFunction::Constant, 0.f, 0.f});
			curve.keyframes.emplace_back(curve_keyframe{vec2d{20.f, 1.f}, CurveInterpFunction::Linear, 0.f, 0.f});
			curve.keyframes.emplace_back(curve_keyframe{vec2d{40.f, 1.2f}, CurveInterpFunction::Constant, 0.f, 0.f});
			return curve_id;
		}();

		if (particle_num.max() <= 1.f) {
			particle_num = ::util::UniformFloatRange(20.f, 50.f);
		}
		part_number_curve.emplace(modular_curves_entry{damage_spew_curve, ::util::UniformFloatRange(1.f), ::util::UniformFloatRange(0.f), false});

		if (normal_variance <= 0.f) {
			normal_variance = 0.2f;
		}
		variance_curve.emplace(modular_curves_entry{damage_spew_curve, ::util::UniformFloatRange(1.f), ::util::UniformFloatRange(0.f), false});

		if (lifetime.max() <= 0.f) {
			lifetime = ::util::UniformFloatRange(0.7f, 1.5f);
		}
		lifetime_curve.emplace(modular_curves_entry{damage_spew_curve, ::util::UniformFloatRange(1.f), ::util::UniformFloatRange(0.f), false});

		break;
	}
	case LegacyShipParticleType::SPLIT_PARTICLES: {
		static const int split_particles_lifetime_curve = []() -> int {
			int curve_id = static_cast<int>(Curves.size());
			auto& curve = Curves.emplace_back(";ShipParticleSplitLifetime");
			curve.keyframes.emplace_back(curve_keyframe{vec2d{0.f, 1.f}, CurveInterpFunction::Linear, 0.f, 0.f});
			curve.keyframes.emplace_back(curve_keyframe{vec2d{100000.f, 1.f + 100000.f * 0.002f}, CurveInterpFunction::Linear, 0.f, 0.f});
			return curve_id;
		}();
		static const int split_particles_radius_curve = []() -> int {
			int curve_id = static_cast<int>(Curves.size());
			auto& curve = Curves.emplace_back(";ShipParticleSplitRadius");
			curve.keyframes.emplace_back(curve_keyframe{vec2d{0.f, 0.f}, CurveInterpFunction::Linear, 0.f, 0.f});
			curve.keyframes.emplace_back(curve_keyframe{vec2d{100000.f, 100000.f * 0.01f}, CurveInterpFunction::Linear, 0.f, 0.f});
			return curve_id;
		}();
		static const int split_particles_velocity_curve = []() -> int {
			int curve_id = static_cast<int>(Curves.size());
			auto& curve = Curves.emplace_back(";ShipParticleSplitVelocity");
			curve.keyframes.emplace_back(curve_keyframe{vec2d{0.f, 0.f}, CurveInterpFunction::Linear, 0.f, 0.f});
			curve.keyframes.emplace_back(curve_keyframe{vec2d{100000.f, 100000.f}, CurveInterpFunction::Linear, 0.f, 0.f});
			return curve_id;
		}();

		if (lifetime.max() <= 0.f) {
			lifetime = ::util::UniformFloatRange(0.5f, 6.f);
		}
		lifetime_curve.emplace(modular_curves_entry{split_particles_lifetime_curve, ::util::UniformFloatRange(1.f), ::util::UniformFloatRange(0.f), false});

		if (radius.max() <= 0.f) {
			radius = ::util::UniformFloatRange(0.5f, 1.5f);
		}
		radius_curve.emplace(modular_curves_entry{split_particles_radius_curve, ::util::UniformFloatRange(1.f), ::util::UniformFloatRange(0.f), false});

		if (velocity.max() <= 0.f) {
			velocity = ::util::UniformFloatRange(0.f, 1.f);
		}
		velocity_curve.emplace(modular_curves_entry{split_particles_velocity_curve, ::util::UniformFloatRange(1.f), ::util::UniformFloatRange(0.f), false});

		break;
	}
	default:
		break;
	}

	auto velocity_volume = make_shared<particle::LegacyAACuboidVolume>(normal_variance, 1.f, true);
	if (variance_curve) {
		velocity_volume->m_modular_curves.add_curve("Host Radius", particle::LegacyAACuboidVolume::VolumeModularCurveOutput::VARIANCE, *variance_curve);
	}

	auto effect = particle::ParticleEffect(
		"", //Name
		particle_num, //Particle num
		particle::ParticleEffect::Duration::ONETIME, //Single Particle Emission
		::util::UniformFloatRange(), //No duration
		::util::UniformFloatRange (-1.f), //Single particle only
		useNormal ? particle::ParticleEffect::ShapeDirection::HIT_NORMAL : particle::ParticleEffect::ShapeDirection::ALIGNED, //Particle direction
		::util::UniformFloatRange(velocityInherit), //Velocity Inherit
		false, //Velocity Inherit absolute?
		std::move(velocity_volume), //Velocity volume
		velocity, //Velocity volume multiplier
		particle::ParticleEffect::VelocityScaling::NONE, //Velocity directional scaling
		std::nullopt, //Orientation-based velocity
		std::nullopt, //Position-based velocity
		nullptr, //Position volume
		particle::ParticleEffectHandle::invalid(), //Trail
		1.f, //Chance
		true, //Affected by detail
		range, //Culling range multiplier
		true, //Disregard Animation Length. Must be true for everything using particle::Anim_bitmap_X
		false, //Don't reverse animation
		false, //parent local
		false, //ignore velocity inherit if parented
		false, //position velocity inherit absolute?
		std::nullopt, //Local velocity offset
		std::nullopt, //Local offset
		lifetime, //Lifetime
		radius, //Radius
		bitmap); //Bitmap

	if (part_number_curve) {
		effect.m_modular_curves.add_curve("Host Radius", particle::ParticleEffect::ParticleCurvesOutput::PARTICLE_NUM_MULT, *part_number_curve);
	}

	if (lifetime_curve) {
		effect.m_modular_curves.add_curve("Host Radius", particle::ParticleEffect::ParticleCurvesOutput::LIFETIME_MULT, *lifetime_curve);
	}

	if (radius_curve) {
		effect.m_modular_curves.add_curve("Host Radius", particle::ParticleEffect::ParticleCurvesOutput::RADIUS_MULT, *radius_curve);
	}

	if (velocity_curve) {
		effect.m_modular_curves.add_curve("Trigger Velocity", particle::ParticleEffect::ParticleCurvesOutput::VOLUME_VELOCITY_MULT, *velocity_curve);
	}

	return particle::ParticleManager::get()->addEffect(std::move(effect));
}

static particle::ParticleEffectHandle parse_ship_legacy_particle_effect(LegacyShipParticleType type, ship_info* sip, const char *id_string, float range, int bitmap, float velocityInherit, bool useNormal = false)
{
	auto particle_num = parse_ship_particle_random_range<int, float>("+Min particles:", "+Max particles:", id_string, "number", sip->name, true);
	auto radius = parse_ship_particle_random_range<float>("+Min Radius:", "+Max Radius:", id_string, "radius", sip->name, true);
	auto lifetime = parse_ship_particle_random_range<float>("+Min Lifetime:", "+Max Lifetime:", id_string, "lifetime", sip->name, true);
	auto velocity = parse_ship_particle_random_range<float>("+Min Velocity:", "+Max Velocity:", id_string, "velocity", sip->name, false);
	float normal_variance = 0.0f;

	if(optional_string("+Normal Variance:"))
	{
		stuff_float(&normal_variance);
		if ((normal_variance < 0.0f) || (normal_variance > 2.0f)) {
			normal_variance = 0.0f;
			Warning(LOCATION,"Bad value %f, defined as %s particle normal variance in ship '%s'.\nValue should be a float from 0.0 to 2.0.\n", normal_variance, id_string, sip->name);
		}
	}

	return create_ship_legacy_particle_effect(type, range, bitmap, particle_num, radius, lifetime, velocity, normal_variance, useNormal, velocityInherit);
}

static particle::ParticleEffectHandle default_ship_particle_effect(LegacyShipParticleType type, int n_high, int n_low, float max_rad, float min_rad, float max_life, float min_life, float max_vel, float min_vel, float variance, float range, int bitmap, float velocityInherit, bool useNormal) {
	return create_ship_legacy_particle_effect(
		type, range, bitmap,
		::util::UniformFloatRange(i2fl(n_low), i2fl(n_high)),
		::util::UniformFloatRange(min_rad, max_rad),
		::util::UniformFloatRange(min_life, max_life),
		::util::UniformFloatRange(min_vel, max_vel),
		variance, useNormal, velocityInherit);
}

static void parse_allowed_weapons(ship_info *sip, const bool is_primary, const bool is_dogfight, const bool first_time)
{
	int i, num_allowed;
	int allowed_weapons[MAX_WEAPON_TYPES];
	const int max_banks = (is_primary ? MAX_SHIP_PRIMARY_BANKS : MAX_SHIP_SECONDARY_BANKS);
	const ubyte weapon_type = (is_dogfight ? DOGFIGHT_WEAPON : REGULAR_WEAPON);
	const int offset = (is_primary ? 0 : MAX_SHIP_PRIMARY_BANKS);
	const char *allowed_banks_str = is_primary ? (is_dogfight ? "$Allowed Dogfight PBanks:" : "$Allowed PBanks:")
		: (is_dogfight ? "$Allowed Dogfight SBanks:" : "$Allowed SBanks:");
	const char *bank_type_str = is_primary ? "primary" : "secondary";

	// Goober5000 - fixed Bobboau's implementation of restricted banks
	int bank;

	// Set the weapons filter used in weapons loadout
	if (optional_string(allowed_banks_str))
	{
		// this ship has loadout stuff, so make room
		if (sip->restricted_loadout_flag.empty())
			sip->restricted_loadout_flag.resize(MAX_SHIP_WEAPONS);
		if (sip->allowed_bank_restricted_weapons.empty())
			sip->allowed_bank_restricted_weapons.resize(MAX_SHIP_WEAPONS);

		// MageKing17 - We need to make modular tables replace bank restrictions by default, instead of adding to them.
		if (!first_time && !(optional_string("+noreplace"))) {	// Only makes sense for modular tables.
			// clear allowed weapons so the modular table can define new ones
			for (bank = 0; bank < max_banks; bank++) {
				sip->allowed_bank_restricted_weapons[offset+bank].clear_flag(weapon_type);
				sip->restricted_loadout_flag[offset+bank] &= ~weapon_type;
			}
		}

		bank = -1;

		while (check_for_string("("))
		{
			bank++;

			// make sure we don't specify more than we have banks for
			if (bank >= max_banks)
			{
				Warning(LOCATION, "%s bank-specific loadout for %s exceeds permissible number of %s banks.  Ignoring the rest...", allowed_banks_str, sip->name, bank_type_str);
				bank--;
				break;
			}

			num_allowed = (int)stuff_int_list(allowed_weapons, MAX_WEAPON_TYPES, WEAPON_LIST_TYPE);

			// actually say which weapons are allowed
			for ( i = 0; i < num_allowed; i++ )
			{
				if ( allowed_weapons[i] >= 0 )		// MK, Bug fix, 9/6/99.  Used to be "allowed_weapons" not "allowed_weapons[i]".
				{
					sip->allowed_bank_restricted_weapons[offset+bank].set_flag(allowed_weapons[i], weapon_type);
				}
			}
		}

		// set flags if need be
		if (bank > 0)	// meaning there was a restricted bank table entry
		{
			for (i=0; i<=bank; i++)
			{
				sip->restricted_loadout_flag[offset+i] |= weapon_type;
			}
		}
	}
}

/**
 * Common method for parsing ship/subsystem primary/secondary weapons so that the parser doesn't flip out in the event of a problem.
 *
 */
static void parse_weapon_bank(ship_info *sip, bool is_primary, int *num_banks, int *bank_default_weapons, int *bank_capacities)
{
	Assert(sip != NULL);
	Assert(bank_default_weapons != NULL);
	Assert(bank_capacities != NULL);
	const int max_banks = is_primary ? MAX_SHIP_PRIMARY_BANKS : MAX_SHIP_SECONDARY_BANKS;
	const char *default_banks_str = is_primary ? "$Default PBanks:" : "$Default SBanks:";
	const char *bank_capacities_str = is_primary ? "$PBank Capacity:" : "$SBank Capacity:";

	if (optional_string(default_banks_str))
	{
		// get weapon list
		if (num_banks != NULL)
			*num_banks = (int)stuff_int_list(bank_default_weapons, max_banks, WEAPON_LIST_TYPE);
		else
			stuff_int_list(bank_default_weapons, max_banks, WEAPON_LIST_TYPE);
	}

	// we initialize to the previous parse, which presumably worked
	int num_bank_capacities = num_banks != NULL ? *num_banks : 0;

	if (optional_string(bank_capacities_str))
	{
		// get capacity list
		num_bank_capacities = (int)stuff_int_list(bank_capacities, max_banks, RAW_INTEGER_TYPE);
	}

	// num_banks can be null if we're parsing weapons for a turret
	if ((num_banks != NULL) && (*num_banks != num_bank_capacities))
	{
		// okay for a ship to have 0 primary capacities, since it won't be ammo-enabled
		if (is_primary && num_bank_capacities != 0)
		{
			Warning(LOCATION, "Ship class '%s' has %d primary banks, but %d primary capacities... fix this!!", sip->name, *num_banks, num_bank_capacities);
		}

		// secondaries have no excuse!
		if (!is_primary)
		{
			Warning(LOCATION, "Ship class '%s' has %d secondary banks, but %d secondary capacities... fix this!!", sip->name, *num_banks, num_bank_capacities);
		}
	}
}

/**
 * Common method for parsing briefing icon info.
 */
static int parse_and_add_briefing_icon_info()
{
	int bii_index = -1;
	size_t icon;
	char regular_temp[MAX_FILENAME_LEN];
	char fade_temp[MAX_FILENAME_LEN];
	char highlight_temp[MAX_FILENAME_LEN];

	required_string("+Regular:");
	stuff_string(regular_temp, F_NAME, MAX_FILENAME_LEN);
	required_string("+Fade:");
	stuff_string(fade_temp, F_NAME, MAX_FILENAME_LEN);
	required_string("+Highlight:");
	stuff_string(highlight_temp, F_NAME, MAX_FILENAME_LEN);

	// search among our existing icons
	for (icon = 0; icon < Briefing_icon_info.size(); icon++)
	{
		if (   !stricmp(regular_temp, Briefing_icon_info[icon].regular.filename)
			&& !stricmp(fade_temp, Briefing_icon_info[icon].fade.filename)
			&& !stricmp(highlight_temp, Briefing_icon_info[icon].highlight.filename) )
		{
			bii_index = (int) icon;
			break;
		}
	}

	// icon not found: create new one
	if (bii_index < 0)
	{
		briefing_icon_info bii;
		generic_anim_init(&bii.regular, regular_temp);
		hud_anim_init(&bii.fade, 0, 0, fade_temp);
		hud_anim_init(&bii.highlight, 0, 0, highlight_temp);

		bii_index = (int) Briefing_icon_info.size();
		Briefing_icon_info.push_back(bii);			
	}

	return bii_index;
}

/**
 * Determines the warp parameters for this ship class (or ship).
 *
 * If we are creating a ship, we want to inherit the parameters of the ship class, then override on a field-by-field basis.
 */
int parse_warp_params(const WarpParams *inherit_from, WarpDirection direction, const char *info_type_name, const char *info_name, bool set_supercap_warp_physics)
{
	Assert(info_type_name != nullptr);
	Assert(info_name != nullptr);

	// for parsing
	const char *prefix = (direction == WarpDirection::WARP_IN) ? "$Warpin" : "$Warpout";
	char str[NAME_LENGTH];

	WarpParams params;
	if (inherit_from != nullptr)
		params = *inherit_from;
	params.direction = direction;

	sprintf(str, "%s type:", prefix);
	if (optional_string(str))
	{
		char buf[NAME_LENGTH];

		stuff_string(buf, F_NAME, NAME_LENGTH);
		int j = warptype_match(buf);
		if (j >= 0) {
			params.warp_type = j;
		}
		else {
			// try to match the warp type with one of our fireballs
			j = fireball_info_lookup(buf);
			if (j >= 0) {
				params.warp_type = j | WT_DEFAULT_WITH_FIREBALL;
			}
			else {
				error_display(0, "Invalid %s '%s' specified for %s '%s'", str, buf, info_type_name, info_name);
				params.warp_type = WT_DEFAULT;
			}
		}
	}

	sprintf(str, "%s Start Sound:", prefix);
	parse_game_sound(str, &params.snd_start);

	sprintf(str, "%s End Sound:", prefix);
	parse_game_sound(str, &params.snd_end);

	if (direction == WarpDirection::WARP_OUT)
	{
		sprintf(str, "%s engage time:", prefix);
		if (optional_string(str))
		{
			float t_time;
			stuff_float(&t_time);
			if (t_time > 0.0f)
				params.warpout_engage_time = fl2i(t_time*1000.0f);
			else
				error_display(0, "%s specified as 0 or less on %s '%s'; value ignored", str, info_type_name, info_name);
		}
	}

	sprintf(str, "%s speed:", prefix);
	if (optional_string(str))
	{
		float speed;
		stuff_float(&speed);
		if (speed > 0.0f)
			params.speed = speed;
		else
			error_display(0, "%s specified as 0 or less on %s '%s'; value ignored", str, info_type_name, info_name);
	}

	sprintf(str, "%s time:", prefix);
	if (optional_string(str))
	{
		float t_time;
		stuff_float(&t_time);
		if (t_time > 0.0f)
			params.time = fl2i(t_time*1000.0f);
		else
			error_display(0, "%s specified as 0 or less on %s '%s'; value ignored", str, info_type_name, info_name);
	}

	sprintf(str, "%s %s exp:", prefix, direction == WarpDirection::WARP_IN ? "decel" : "accel");
	if (optional_string(str))
	{
		float accel_exp;
		stuff_float(&accel_exp);
		if (accel_exp >= 0.0f)
			params.accel_exp = accel_exp;
		else
			error_display(0, "%s specified as less than 0 on %s '%s'; value ignored", str, info_type_name, info_name);
	}

	sprintf(str, "%s radius:", prefix);
	if (optional_string(str))
	{
		float rad;
		stuff_float(&rad);
		if (rad > 0.0f)
			params.radius = rad;
		else
			error_display(0, "%s specified as 0 or less on %s '%s'; value ignored", str, info_type_name, info_name);
	}

	sprintf(str, "%s animation:", prefix);
	if (optional_string(str))
	{
		stuff_string(params.anim, F_NAME, MAX_FILENAME_LEN);
	}

	// we might need to explicitly set this flag; but if so, the modder has the option of unsetting it
	if (set_supercap_warp_physics)
		params.supercap_warp_physics = true;

	sprintf(str, "$Supercap warp%s physics:", (direction == WarpDirection::WARP_IN) ? "in" : "out");
	if (optional_string(str))
	{
		stuff_boolean(&params.supercap_warp_physics);
	}

	if (direction == WarpDirection::WARP_OUT)
	{
		sprintf(str, "$Player warpout speed:");
		if (optional_string(str))
		{
			float speed;
			stuff_float(&speed);
			if (speed > 0.0f)
				params.warpout_player_speed = speed;
			else
				error_display(0, "%s specified as 0 or less on %s '%s'; value ignored", str, info_type_name, info_name);
		}
	}

	// get the index of these warp params, which may be a new index
	return find_or_add_warp_params(params);
}

/**
 * Puts values into a ship_info.
 */
static void parse_ship_values(ship_info* sip, const bool is_template, const bool first_time, const bool replace)
{
	char buf[SHIP_MULTITEXT_LENGTH];
	const char* info_type_name;
	const char* type_name;
	char name_tmp[NAME_LENGTH];

	if ( ! is_template ) {
		info_type_name = "Ship Class";
		type_name = "$Name";
	}
	else {
		info_type_name = "Ship Template";
		type_name = "$Template";
	}
	
	if (optional_string("$Alt name:") || optional_string("$Display Name:"))
	{
		stuff_string(sip->display_name, F_NAME, NAME_LENGTH);
		end_string_at_first_hash_symbol(sip->display_name, true);
		consolidate_double_characters(sip->display_name, '#');
		sip->flags.set(Ship::Info_Flags::Has_display_name);
	}

	if(optional_string("$Short name:"))
		stuff_string(sip->short_name, F_NAME, NAME_LENGTH);
	else if (first_time)
	{
		char *srcpos, *srcend, *destpos;
		srcpos = sip->name;
		destpos = sip->short_name;
		srcend = srcpos + strlen(sip->name);
		while(srcpos <= srcend)
		{
			if(*srcpos != ' ')
				*destpos++ = *srcpos++;
			else
				srcpos++;
		}
	}
	diag_printf ("Ship short name -- %s\n", sip->short_name);

	if (optional_string("$Species:")) {
		char temp[NAME_LENGTH];
		stuff_string(temp, F_NAME, NAME_LENGTH);
		int i_species = 0;
		
		bool found = false;
		for (SCP_vector<species_info>::iterator sii = Species_info.begin(); sii != Species_info.end(); ++sii, ++i_species) {
			if (!stricmp(temp, sii->species_name)) {
				sip->species = i_species;
				found = true;
				break;
			}
		}

		if (!found) {
			Error(LOCATION, "Invalid Species %s defined in table entry for ship %s.\n", temp, sip->name);
		}
	}

	diag_printf ("Ship species -- %s\n", Species_info[sip->species].species_name);

	if (optional_string("+Type:")) {
		stuff_string(sip->type_str, F_TRIMMED, true);
	}

	if (optional_string("+Maneuverability:")) {
		stuff_string(sip->maneuverability_str, F_TRIMMED, true);
	}

	if (optional_string("+Armor:")) {
		stuff_string(sip->armor_str, F_TRIMMED, true);
	}

	if (optional_string("+Manufacturer:")) {
		stuff_string(sip->manufacturer_str, F_TRIMMED, true);
	}

	if (optional_string("+Description:")) {
		stuff_string(sip->desc, F_MULTITEXT, true);
	}
	
	if (optional_string("+Tech Title:")) {
		stuff_string(sip->tech_title, F_NAME, NAME_LENGTH);
	}

	if (optional_string("+Tech Description:")) {
		stuff_string(sip->tech_desc, F_MULTITEXT, true);
	}

	if (optional_string("+Length:")) {
		stuff_string(sip->ship_length, F_TRIMMED, true);
	}
	
	if (optional_string("+Gun Mounts:")) {
		stuff_string(sip->gun_mounts, F_TRIMMED, true);
	}
	
	if (optional_string("+Missile Banks:")) {
		stuff_string(sip->missile_banks, F_TRIMMED, true);
	}

	// Ship fadein effect, used when no ani is specified or ship_select_3d is active
	if(optional_string("$Selection Effect:")) {
		char effect[NAME_LENGTH];
		stuff_string(effect, F_NAME, NAME_LENGTH);
		if (!stricmp(effect, "FS2"))
			sip->selection_effect = 2;
		else if (!stricmp(effect, "FS1"))
			sip->selection_effect = 1;
		else if (!stricmp(effect, "off"))
			sip->selection_effect = 0;
	}

	// This only works if the hud gauge defined uses $name assignment
	if (optional_string("$HUD Gauge Configs:")) {
		SCP_vector<SCP_string> gauge_configs;
		stuff_string_list(gauge_configs);

		// Save the ship name and not the index because we don't know what the final index will be yet
		for (const auto& config : gauge_configs) {
			Hud_parsed_ships.push_back(std::make_pair(config, sip->name));
		}

	}

	if(optional_string( "$Cockpit POF file:" ))
	{
		char temp[MAX_FILENAME_LEN];
		stuff_string(temp, F_NAME, MAX_FILENAME_LEN);

		// assume we're using this file name
		bool valid = true;

		// Goober5000 - if this is a modular table, and we're replacing an existing file name, and the file doesn't exist, don't replace it
		if (replace)
			if (VALID_FNAME(sip->cockpit_pof_file))
				if (!model_exists(temp))
					valid = false;

		if (valid)
			strcpy_s(sip->cockpit_pof_file, temp);
		else
			WarningEx(LOCATION, "Ship %s\nCockpit POF file \"%s\" invalid!", sip->name, temp);

		if (optional_string("$Cockpit Animations:")) {
			animation::ModelAnimationParseHelper::parseAnimsetInfo(sip->cockpit_animations, 'c', sip->name);
		}
		if (optional_string("$Driven Cockpit Animations:")) {
			//Despite not being a ship, cockpits have access to full ship driver sources as they can get data fed from the player ship
			animation::ModelAnimationParseHelper::parseAnimsetInfoDrivers(sip->cockpit_animations, 'c', sip->name, animation::parse_ship_property_driver_source);
		}
		if (optional_string("$Cockpit Moveables:")) {
			animation::ModelAnimationParseHelper::parseMoveablesetInfo(sip->cockpit_animations);
		}
	}
	if(optional_string( "+Cockpit offset:" ))
	{
		stuff_vec3d(&sip->cockpit_offset);
	}

	if (optional_string("+Cockpit Sway Multiplier:"))
	{
		stuff_float(&sip->cockpit_sway_val);
	}

	while(optional_string( "$Cockpit Display:" )) 
	{
		cockpit_display_info display;

		display.bg_filename[0] = 0;
		display.fg_filename[0] = 0;
		display.filename[0] = 0;
		display.name[0] = 0;
		display.offset[0] = 0;
		display.offset[1] = 0;

		required_string("+Texture:");
		stuff_string(display.filename, F_NAME, MAX_FILENAME_LEN);

		if ( optional_string("+Offsets:") ) {
			stuff_int_list(display.offset, 2);
		}
		
		required_string("+Size:");
		stuff_int_list(display.size, 2);
		
		if ( optional_string("+Background:") ) {
			stuff_string(display.bg_filename, F_NAME, MAX_FILENAME_LEN);
		}
		if ( optional_string("+Foreground:") ) {
			stuff_string(display.fg_filename, F_NAME, MAX_FILENAME_LEN);
		}
		
		required_string("+Display Name:");
		stuff_string(display.name, F_NAME, MAX_FILENAME_LEN);

		if ( display.offset[0] < 0 || display.offset[1] < 0 ) {
			Warning(LOCATION, "Negative display offsets given for cockpit display on %s, skipping entry", sip->name);
			continue;
		}

		if( display.size[0] <= 0 || display.size[1] <= 0 ) {
			Warning(LOCATION, "Negative or zero display size given for cockpit display on %s, skipping entry", sip->name);
			continue;
		}

		sip->displays.push_back(display);
	}

	if(optional_string( "$POF file:" ))
	{
		char temp[MAX_FILENAME_LEN];
		stuff_string(temp, F_NAME, MAX_FILENAME_LEN);

		// assume we're using this file name
		bool valid = true;

		// Goober5000 - if this is a modular table, and we're replacing an existing file name, and the file doesn't exist, don't replace it
		if (replace)
			if (VALID_FNAME(sip->pof_file))
				if (!model_exists(temp))
					valid = false;

		if (valid)
			strcpy_s(sip->pof_file, temp);
		else
			WarningEx(LOCATION, "Ship %s\nPOF file \"%s\" invalid!", sip->name, temp);
	}

	if(optional_string( "$POF file Techroom:" ))
	{
		char temp[MAX_FILENAME_LEN];
		stuff_string(temp, F_NAME, MAX_FILENAME_LEN);

		// assume we're using this file name
		bool valid = true;

		// if this is a modular table, and we're replacing an existing file name, and the file doesn't exist, don't replace it
		if (replace)
			if (VALID_FNAME(sip->pof_file_tech))
				if (!cf_exists_full(temp, CF_TYPE_MODELS))
					valid = false;

		if (valid)
			strcpy_s(sip->pof_file_tech, temp);
		else
			WarningEx(LOCATION, "Ship %s\nTechroom POF file \"%s\" invalid!", sip->name, temp);
	}

	// ship class texture replacement - Goober5000
	// don't clear the vector because we could be parsing a TBM
	if (optional_string("$Texture Replace:"))
	{
		texture_replace tr;
		char *p;

		tr.from_table = true;

		while (optional_string("+old:"))
		{
			strcpy_s(tr.ship_name, sip->name);
			tr.new_texture_id = -1;

			stuff_string(tr.old_texture, F_NAME, MAX_FILENAME_LEN);
			required_string("+new:");
			stuff_string(tr.new_texture, F_NAME, MAX_FILENAME_LEN);

			// get rid of extensions
			p = strchr(tr.old_texture, '.');
			if (p)
			{
				mprintf(("Extraneous extension found on replacement texture %s!\n", tr.old_texture));
				*p = 0;
			}
			p = strchr(tr.new_texture, '.');
			if (p)
			{
				mprintf(("Extraneous extension found on replacement texture %s!\n", tr.new_texture));
				*p = 0;
			}

			// add it if we aren't over the limit
			if (sip->replacement_textures.size() < MAX_MODEL_TEXTURES)
				sip->replacement_textures.push_back(tr);
			else
				mprintf(("Too many replacement textures specified for ship '%s'!\n", sip->name));
		}
	}

	// optional hud targeting model
	if(optional_string( "$POF target file:"))
	{
		char temp[MAX_FILENAME_LEN];
		stuff_string(temp, F_NAME, MAX_FILENAME_LEN);

		// assume we're using this file name
		bool valid = true;

		// Goober5000 - if this is a modular table, and we're replacing an existing file name, and the file doesn't exist, don't replace it
		if (replace)
			if (VALID_FNAME(sip->pof_file_hud))
				if (!cf_exists_full(temp, CF_TYPE_MODELS))
					valid = false;

		if (valid)
			strcpy_s(sip->pof_file_hud, temp);
		else
			WarningEx(LOCATION, "Ship \"%s\" POF target file \"%s\" invalid!", sip->name, temp);
	}

	// optional hud target LOD if not using special hud model
	if (optional_string( "$POF target LOD:" )) {
		stuff_int(&sip->hud_target_lod);
	}

	if(optional_string("$Detail distance:")) {
		sip->num_detail_levels = (int)stuff_int_list(sip->detail_distance, MAX_SHIP_DETAIL_LEVELS, RAW_INTEGER_TYPE);
	}

	if(optional_string("$Collision LOD:")) {
		stuff_int(&sip->collision_lod);

		// Cap to sane values, just in case
		sip->collision_lod = MAX(0, MIN(sip->collision_lod, sip->num_detail_levels));
	}

	// check for optional pixel colors
	// This is dummied out; we no longer support palettized rendering modes
	while(optional_string("$ND:")){		
		ubyte nr, ng, nb;
		stuff_ubyte(&nr);
		stuff_ubyte(&ng);
		stuff_ubyte(&nb);

	}

	if (optional_string("$Enable Team Colors:")) {
		stuff_boolean(&sip->uses_team_colors);
		sip->default_team_name = "None";
	}

	if (optional_string("$Default Team:")) {
		char temp[NAME_LENGTH];
		stuff_string(temp, F_NAME, NAME_LENGTH);
		if (!stricmp(temp, "none")) {
			sip->uses_team_colors = true;
		} else {
			SCP_string name = temp;
			if (Team_Colors.find(name) != Team_Colors.end()) {
				sip->default_team_name = std::move(name);
				sip->uses_team_colors = true;
			} else {
				Warning(LOCATION, "Team name %s is invalid. Teams must be defined in colors.tbl.\n", temp);
			}
		}
	}

	if(optional_string("$Show damage:"))
	{
		int bogus_bool;
		stuff_boolean(&bogus_bool);
	}

	if(optional_string("$Damage Lightning Type:"))
	{
		stuff_string(buf, F_NAME, SHIP_MULTITEXT_LENGTH);
		auto j = lightningtype_match(buf);
		if(j >= 0) {
			sip->damage_lightning_type = j;
		} else {
			Warning(LOCATION, "Invalid lightning type '%s' specified for ship '%s'", buf, sip->name);
			sip->damage_lightning_type = SLT_DEFAULT;
		}
	}

	if(optional_string("$Impact:"))
	{
		if(optional_string("+Damage Type:"))
		{
			stuff_string(buf, F_NAME, NAME_LENGTH);
			sip->collision_damage_type_idx = damage_type_add(buf);
		}
	}

	if(optional_string("$Impact Spew Effect:"))
	{
		sip->impact_spew = particle::util::parseEffect(sip->name);
	}
	else if(optional_string("$Impact Spew:"))
	{
		sip->impact_spew = parse_ship_legacy_particle_effect(LegacyShipParticleType::OTHER, sip, "impact spew", 1.f, particle::Anim_bitmap_id_fire, 0.7f);
	}
	if(optional_string("$Damage Spew Effect:"))
	{
		sip->damage_spew = particle::util::parseEffect(sip->name);
	}
	else if(optional_string("$Damage Spew:"))
	{
		sip->damage_spew = parse_ship_legacy_particle_effect(LegacyShipParticleType::DAMAGE_SPEW, sip, "damage spew", 1.f, particle::Anim_bitmap_id_smoke, 0.7f);
	}

	if(optional_string("$Collision Physics:"))
	{
		if(optional_string("+Bounce:"))	{
			stuff_float(&sip->collision_physics.bounce);
		}
		if(optional_string("+Both Small Bounce:")) {
			stuff_float(&sip->collision_physics.both_small_bounce);
		}
		if(optional_string("+Friction:")) {
			stuff_float(&sip->collision_physics.friction);
		}
		if(optional_string("+Rotation Factor:")) {
			stuff_float(&sip->collision_physics.rotation_factor);
		}
		if(optional_string("+Landing Max Forward Vel:")) {
			stuff_float(&sip->collision_physics.landing_max_z);
		}
		if(optional_string("+Landing Min Forward Vel:")) {
			stuff_float(&sip->collision_physics.landing_min_z);
		}
		if(optional_string("+Landing Max Descent Vel:")) {
			stuff_float(&sip->collision_physics.landing_min_y);
			sip->collision_physics.landing_min_y *= -1;
		}
		if(optional_string("+Landing Max Horizontal Vel:")) {
			stuff_float(&sip->collision_physics.landing_max_x);
		}
		if(optional_string("+Landing Max Angle:")) {
			float degrees;
			stuff_float(&degrees);
			sip->collision_physics.landing_max_angle = cosf(fl_radians(90.0f - degrees));
		}
		if(optional_string("+Landing Min Angle:")) {
			float degrees;
			stuff_float(&degrees);
			sip->collision_physics.landing_min_angle = cosf(fl_radians(90.0f - degrees));
		}
		if(optional_string("+Landing Max Rotate Angle:")) {
			float degrees;
			stuff_float(&degrees);
			sip->collision_physics.landing_max_rot_angle = cosf(fl_radians(90.0f - degrees));
		}
		if(optional_string("+Reorient Max Forward Vel:")) {
			stuff_float(&sip->collision_physics.reorient_max_z);
		}
		if(optional_string("+Reorient Min Forward Vel:")) {
			stuff_float(&sip->collision_physics.reorient_min_z);
		}
		if(optional_string("+Reorient Max Descent Vel:")) {
			stuff_float(&sip->collision_physics.reorient_min_y);
			sip->collision_physics.reorient_min_y *= -1;
		}
		if(optional_string("+Reorient Max Horizontal Vel:")) {
			stuff_float(&sip->collision_physics.reorient_max_x);
		}
		if(optional_string("+Reorient Max Angle:")) {
			float degrees;
			stuff_float(&degrees);
			sip->collision_physics.reorient_max_angle = cosf(fl_radians(90.0f - degrees));
		}
		if(optional_string("+Reorient Min Angle:")) {
			float degrees;
			stuff_float(&degrees);
			sip->collision_physics.reorient_min_angle = cosf(fl_radians(90.0f - degrees));
		}
		if(optional_string("+Reorient Max Rotate Angle:")) {
			float degrees;
			stuff_float(&degrees);
			sip->collision_physics.reorient_max_rot_angle = cosf(fl_radians(90.0f - degrees));
		}
		if(optional_string("+Reorient Speed Mult:")) {
			stuff_float(&sip->collision_physics.reorient_mult);
		}
		if(optional_string("+Landing Rest Angle:")) {
			float degrees;
			stuff_float(&degrees);
			sip->collision_physics.landing_rest_angle = cosf(fl_radians(90.0f - degrees));
		}
		parse_game_sound("+Landing Sound:", &sip->collision_physics.landing_sound_idx);

		parse_game_sound("+Collision Sound Light:", &sip->collision_physics.collision_sound_light_idx);
		parse_game_sound("+Collision Sound Heavy:", &sip->collision_physics.collision_sound_heavy_idx);
		parse_game_sound("+Collision Sound Shielded:", &sip->collision_physics.collision_sound_shielded_idx);
	}

	if (optional_string("$Gravity Const:")) {
		stuff_float(&sip->gravity_const);
	}

	if (optional_string("$Dying Gravity Const:")) {
		stuff_float(&sip->dying_gravity_const);
	} else if (first_time) {
		sip->dying_gravity_const = sip->gravity_const;
	}

	if(optional_string("$Debris:"))
	{
		if(optional_string("+Min Lifetime:"))	{
			stuff_float(&sip->debris_min_lifetime);
			if(sip->debris_min_lifetime < 0.0f)
				Warning(LOCATION, "Debris min lifetime on %s '%s' is below 0 and will be ignored", info_type_name, sip->name);
		}
		if(optional_string("+Max Lifetime:"))	{
			stuff_float(&sip->debris_max_lifetime);
			if(sip->debris_max_lifetime < 0.0f)
				Warning(LOCATION, "Debris max lifetime on %s '%s' is below 0 and will be ignored", info_type_name, sip->name);
		}
		if(optional_string("+Min Speed:"))	{
			stuff_float(&sip->debris_min_speed);
			if(sip->debris_min_speed < 0.0f)
				Warning(LOCATION, "Debris min speed on %s '%s' is below 0 and will be ignored", info_type_name, sip->name);
		}
		if(optional_string("+Max Speed:"))	{
			stuff_float(&sip->debris_max_speed);
			if(sip->debris_max_speed < 0.0f)
				Warning(LOCATION, "Debris max speed on %s '%s' is below 0 and will be ignored", info_type_name, sip->name);
		}
		if(optional_string("+Min Rotation speed:"))	{
			stuff_float(&sip->debris_min_rotspeed);
			if(sip->debris_min_rotspeed < 0.0f)
				Warning(LOCATION, "Debris min speed on %s '%s' is below 0 and will be ignored", info_type_name, sip->name);
		}
		if(optional_string("+Max Rotation speed:"))	{
			stuff_float(&sip->debris_max_rotspeed);
			if(sip->debris_max_rotspeed < 0.0f)
				Warning(LOCATION, "Debris max speed on %s '%s' is below 0 and will be ignored", info_type_name, sip->name);
		}
		if(optional_string("+Damage Type:")) {
			stuff_string(buf, F_NAME, NAME_LENGTH);
			sip->debris_damage_type_idx = damage_type_add(buf);
		}
		if(optional_string("+Min Hitpoints:")) {
			stuff_float(&sip->debris_min_hitpoints);
			if(sip->debris_min_hitpoints < 0.0f)
				Warning(LOCATION, "Debris min hitpoints on %s '%s' is below 0 and will be ignored", info_type_name, sip->name);
		}
		if(optional_string("+Max Hitpoints:")) {
			stuff_float(&sip->debris_max_hitpoints);
			if(sip->debris_max_hitpoints < 0.0f)
				Warning(LOCATION, "Debris max hitpoints on %s '%s' is below 0 and will be ignored", info_type_name, sip->name);
		}
		if(optional_string("+Hitpoints Radius Multiplier:")) {
			stuff_float(&sip->debris_hitpoints_radius_multi);
			if (sip->debris_hitpoints_radius_multi < 0.0f)
				Warning(LOCATION, "Hitpoints Radius Multiplier on %s '%s' is below 0 and will be ignored", info_type_name, sip->name);
		}
		if(optional_string("+Damage Multiplier:")) {
			stuff_float(&sip->debris_damage_mult);
			if(sip->debris_damage_mult < 0.0f)
				Warning(LOCATION, "Debris damage multiplier on %s '%s' is below 0 and will be ignored", info_type_name, sip->name);
		}
		if(optional_string("+Lightning Arc Percent:")) {
			stuff_float(&sip->debris_arc_percent);
			if(sip->debris_arc_percent < 0.0f || sip->debris_arc_percent > 100.0f) {
				Warning(LOCATION, "Lightning Arc Percent on %s '%s' should be between 0 and 100.0 (read %f). Entry will be ignored.", info_type_name, sip->name, sip->debris_arc_percent);
				sip->debris_arc_percent = 50.0;
			}
			//Percent is nice for modders, but here in the code we want it between 0 and 1.0
			sip->debris_arc_percent /= 100.0;
		}
		if (optional_string("+Debris Gravity Const:")) {
			stuff_float(&sip->debris_gravity_const);
		} else if (first_time) {
			sip->debris_gravity_const = sip->dying_gravity_const;
		}

		// when Debris Density was added in 23.2 it did not have a colon, so keep that backwards compatibility
		if (optional_string("+Debris Density:") || optional_string("+Debris Density"))
			stuff_float(&sip->debris_density);

		gamesnd_id ambient_snd, collision_snd_light, collision_snd_heavy, explosion_snd;
		if (parse_game_sound("+Ambient Sound:", &ambient_snd))
			sip->debris_ambient_sound = ambient_snd;
		if (parse_game_sound("+Collision Sound Light:", &collision_snd_light))
			sip->debris_collision_sound_light = collision_snd_light;
		if (parse_game_sound("+Collision Sound Heavy:", &collision_snd_heavy))
			sip->debris_collision_sound_heavy = collision_snd_heavy;
		if (parse_game_sound("+Explosion Sound:", &explosion_snd))
			sip->debris_explosion_sound = explosion_snd;

		if (optional_string("+Generic Debris POF file:"))
		{
			char temp[MAX_FILENAME_LEN];
			stuff_string(temp, F_NAME, MAX_FILENAME_LEN);

			bool valid = true;

			if (replace)
				if (VALID_FNAME(sip->generic_debris_pof_file))
					if (!cf_exists_full(temp, CF_TYPE_MODELS))
						valid = false;

			if (valid)
				strcpy_s(sip->generic_debris_pof_file, temp);
			else
				WarningEx(LOCATION, "Ship %s\nGeneric Debris POF file \"%s\" invalid!", sip->name, temp);
		}

		if (optional_string("+Generic Debris Spew Num:")) {
			stuff_int(&(sip->generic_debris_spew_num));
			if (sip->generic_debris_spew_num < 0) {
				Warning(LOCATION, "Generic Debris Spew Num for %s '%s' cannot be below 0.", info_type_name, sip->name);
				sip->generic_debris_spew_num = 0;
			}
		}

	}
	//WMC - sanity checking
	if(sip->debris_min_speed > sip->debris_max_speed && sip->debris_max_speed >= 0.0f) {
		Warning(LOCATION, "Debris min speed (%f) on %s '%s' is greater than debris max speed (%f), and will be set to debris max speed.", sip->debris_min_speed, info_type_name, sip->name, sip->debris_max_speed);
		sip->debris_min_speed = sip->debris_max_speed;
	}
	if(sip->debris_min_rotspeed > sip->debris_max_rotspeed && sip->debris_max_rotspeed >= 0.0f) {
		Warning(LOCATION, "Debris min rotation speed (%f) on %s '%s' is greater than debris max rotation speed (%f), and will be set to debris max rotation speed.", sip->debris_min_rotspeed, info_type_name, sip->name, sip->debris_max_rotspeed);
		sip->debris_min_rotspeed = sip->debris_max_rotspeed;
	}
	if(sip->debris_min_lifetime > sip->debris_max_lifetime && sip->debris_max_lifetime >= 0.0f) {
		Warning(LOCATION, "Debris min lifetime (%f) on %s '%s' is greater than debris max lifetime (%f), and will be set to debris max lifetime.", sip->debris_min_lifetime, info_type_name, sip->name, sip->debris_max_lifetime);
		sip->debris_min_lifetime = sip->debris_max_lifetime;
	}
	if(sip->debris_min_hitpoints > sip->debris_max_hitpoints && sip->debris_max_hitpoints >= 0.0f) {
		Warning(LOCATION, "Debris min hitpoints (%f) on %s '%s' is greater than debris max hitpoints (%f), and will be set to debris max hitpoints.", sip->debris_min_hitpoints, info_type_name, sip->name, sip->debris_max_hitpoints);
		sip->debris_min_hitpoints = sip->debris_max_hitpoints;
	}

	if(optional_string("$Density:"))
		stuff_float( &(sip->density) );
	diag_printf ("Ship density -- %7.3f\n", sip->density);

	if(optional_string("$Damp:"))
		stuff_float( &(sip->damp) );
	diag_printf ("Ship damp -- %7.3f\n", sip->damp);

	if(optional_string("$Rotdamp:"))
		stuff_float( &(sip->rotdamp) );
	diag_printf ("Ship rotdamp -- %7.3f\n", sip->rotdamp);

	if(optional_string("$Banking Constant:"))
		stuff_float( &(sip->delta_bank_const) );
	diag_printf ("%s '%s' delta_bank_const -- %7.3f\n", info_type_name, sip->name, sip->delta_bank_const);

	if(optional_string("$Max Velocity:"))
	{
		stuff_vec3d(&sip->max_vel);
		sip->max_accel = sip->max_vel.xyz.z;
	}

	// calculate the max speed from max_velocity
	sip->max_speed = sip->max_vel.xyz.z;

	if(optional_string("$Player Minimum Velocity:"))
		stuff_vec3d(&sip->min_vel);

	if(optional_string("$Rotation Time:"))
	{
		stuff_vec3d(&sip->rotation_time);

		// div/0 safety check.
		if ((sip->rotation_time.xyz.x == 0) || (sip->rotation_time.xyz.y == 0) || (sip->rotation_time.xyz.z == 0))
			Warning(LOCATION, "Rotation time must have non-zero values in each of the three variables.\nFix this in %s %s\n", info_type_name, sip->name);

		sip->srotation_time = (sip->rotation_time.xyz.x + sip->rotation_time.xyz.y)/2.0f;

		sip->max_rotvel.xyz.x = (2 * PI) / sip->rotation_time.xyz.x;
		sip->max_rotvel.xyz.y = (2 * PI) / sip->rotation_time.xyz.y;
		sip->max_rotvel.xyz.z = (2 * PI) / sip->rotation_time.xyz.z;
	}

	// get the backwards velocity;
	if(optional_string("$Rear Velocity:"))
	{
		stuff_float(&sip->max_rear_vel);
		sip->min_speed = -sip->max_rear_vel;
	}

	// get the accelerations
	if(optional_string("$Forward accel:"))
		stuff_float(&sip->forward_accel );

	if(optional_string("$Forward decel:"))
		stuff_float(&sip->forward_decel );

	if(optional_string("$Slide accel:"))
		stuff_float(&sip->slide_accel );

	if(optional_string("$Slide decel:"))
		stuff_float(&sip->slide_decel );
		
	if(optional_string("$Glide:"))
	{
		stuff_boolean(&sip->can_glide);
	}

	if(sip->can_glide == true)
	{
		if(optional_string("+Dynamic Glide Cap:"))
			stuff_boolean(&sip->glide_dynamic_cap);
		if(optional_string("+Max Glide Speed:"))
			stuff_float(&sip->glide_cap );
		if(optional_string("+Glide Accel Mult:"))
			stuff_float(&sip->glide_accel_mult);
	}

	if(optional_string("$Use Newtonian Dampening:")) {
			sip->newtonian_damp_override = true;
			stuff_boolean(&sip->use_newtonian_damp);
	}

	if(optional_string("$Autoaim FOV:"))
	{
		float fov_temp;
		stuff_float(&fov_temp);

		// Make sure it is a reasonable value
		if (fov_temp < 0.0f)
			fov_temp = 0.0f;

		if (fov_temp > 180.0f)
			fov_temp = 180.0f;

		sip->aiming_flags.set(Object::Aiming_Flags::Autoaim); 
		sip->autoaim_fov = fov_temp * PI / 180.0f;

		if(optional_string("+Converging Autoaim"))
			sip->aiming_flags.set(Object::Aiming_Flags::Autoaim_convergence);

		if(optional_string("+Minimum Distance:"))
			stuff_float(&sip->minimum_convergence_distance);

		parse_game_sound("+Autoaim Lock Snd:", &sip->autoaim_lock_snd);
		parse_game_sound("+Autoaim Lost Snd:", &sip->autoaim_lost_snd);
	}
	int bank_fov_count = 0;
	while (optional_string("+Primary Bank Autoaim FOV:")) {

		if (bank_fov_count < sip->num_primary_banks) {
			stuff_float(&sip->bank_autoaim_fov[bank_fov_count]);
		} else {
			error_display(1, "Too many bank autoaims defined. Ship only has %i banks!", sip->num_primary_banks);
		}
	}

	if (optional_string("$Aims at Flight Cursor:")) {
		stuff_boolean(&sip->aims_at_flight_cursor);

		if (optional_string("+Extent:")) {
			stuff_float(&sip->flight_cursor_aim_extent);
			sip->flight_cursor_aim_extent = fl_radians(sip->flight_cursor_aim_extent);
		} else if (sip->aims_at_flight_cursor && sip->flight_cursor_aim_extent < 0.0f) {
			error_display(0, "Ship %s needs to have an +Extent defined if $Aims at Flight Cursor is true.", sip->name);
			sip->aims_at_flight_cursor = false;
		}
	}

	if(optional_string("$Convergence:"))
	{
		if(optional_string("+Automatic"))
		{
			sip->aiming_flags.set(Object::Aiming_Flags::Auto_convergence);
			if(optional_string("+Minimum Distance:"))
				stuff_float(&sip->minimum_convergence_distance);
		}
		if(optional_string("+Standard"))
		{
			sip->aiming_flags.set(Object::Aiming_Flags::Std_convergence);
			if(required_string("+Distance:"))
				stuff_float(&sip->convergence_distance);
		}
		if(optional_string("+Offset:")) {
			stuff_vec3d(&sip->convergence_offset);

            sip->aiming_flags.set(Object::Aiming_Flags::Convergence_offset, !IS_VEC_NULL(&sip->convergence_offset));				
		}
	}

	bool is_supercap_for_warp_params = false;
	if (first_time) {
		// the first time we get to this point, initialize to the species parameter indexes (which may be -1)
		sip->warpin_params_index = Species_info[sip->species].warpin_params_index;
		sip->warpout_params_index = Species_info[sip->species].warpout_params_index;

		// figure out whether this is a supercap by doing some parse gymnastics
		// (flags are parsed several lines later, so we need to skip ahead, peek at the flags, and jump back)
		pause_parse();
		if (skip_to_string("$Flags:", "$Name:") == 1) {
			// cache the flag definition so we don't have to keep looking it up
			static auto supercap_flag_def = std::find_if(std::begin(Ship_flags), std::end(Ship_flags), [](const flag_def_list_new<Info_Flags> &item) {
				return item.def == Ship::Info_Flags::Supercap;
			});

			// look up the flag in this flag list
			if (supercap_flag_def != std::end(Ship_flags)) {
				SCP_vector<SCP_string> flags;
				stuff_string_list(flags);
				auto supercap_string = std::find_if(flags.begin(), flags.end(), [](const SCP_string& item) {
					return lcase_equal(item, supercap_flag_def->name);
				});
				if (supercap_string != flags.end()) {
					is_supercap_for_warp_params = true;
				}
			}
		}
		unpause_parse();
	}

	// get ship parameters for warpin and warpout
	// Note: if the index is not -1, we must have already assigned warp parameters, probably because we are now
	// parsing a TBM.  In that case, inherit from ourselves.
	// Note2: In retail, supercaps have the PF_SUPERCAP_WARP_IN applied by default (but not PF_SUPERCAP_WARP_OUT).  So,
	// if we are parsing a supercap for the first time, and this is a warpin, set the flag.
	sip->warpin_params_index = parse_warp_params(sip->warpin_params_index >= 0 ? &Warp_params[sip->warpin_params_index] : nullptr, WarpDirection::WARP_IN, info_type_name, sip->name, first_time && is_supercap_for_warp_params);
	sip->warpout_params_index = parse_warp_params(sip->warpout_params_index >= 0 ? &Warp_params[sip->warpout_params_index] : nullptr, WarpDirection::WARP_OUT, info_type_name, sip->name, false);

	// get ship explosion info
	shockwave_create_info *sci = &sip->shockwave;
	if(optional_string("$Expl inner rad:")){
		stuff_float(&sci->inner_rad);
	}

	if(optional_string("$Expl outer rad:")){
		stuff_float(&sci->outer_rad);
	}

	if(optional_string("$Expl damage:")){
		stuff_float(&sci->damage);
	}

	if(optional_string("$Expl blast:")){
		stuff_float(&sci->blast);
	}

	if(optional_string("$Expl Propagates:")){
		stuff_boolean(&sip->explosion_propagates);
	}

	if(optional_string("$Expl Splits Ship:")){
		stuff_boolean(&sip->explosion_splits_ship);
	}
	else if (first_time) {
		sip->explosion_splits_ship = sip->explosion_propagates == 1;
	}

	if(optional_string("$Propagating Expl Radius Multiplier:")){
		stuff_float(&sip->prop_exp_rad_mult);
		if(sip->prop_exp_rad_mult <= 0) {
			// on invalid value return to default setting
			Warning(LOCATION, "Propagating explosion radius multiplier was set to non-positive value.\nDefaulting multiplier to 1.0 on %s '%s'.\n", info_type_name, sip->name);
			sip->prop_exp_rad_mult = 1.0f;
		}
	}

	if(optional_string("$Expl Visual Rad:")){
		stuff_float(&sip->big_exp_visual_rad);
	}

	if(optional_string("$Base Death-Roll Time:")){
		stuff_int(&sip->death_roll_base_time);
		if (sip->death_roll_base_time < 2)
			sip->death_roll_base_time = 2;
	}

	if(optional_string("$Death-Roll Explosion Radius Mult:")){
		stuff_float(&sip->death_roll_r_mult);
		if (sip->death_roll_r_mult < 0)
			sip->death_roll_r_mult = 0;
	}

	if(optional_string("$Death-Roll Explosion Intensity Mult:")){
		stuff_float(&sip->death_roll_time_mult);
		if (sip->death_roll_time_mult <= 0)
			sip->death_roll_time_mult = 1.0f;
	}

	if(optional_string("$Death FX Explosion Radius Mult:")){
		stuff_float(&sip->death_fx_r_mult);
		if (sip->death_fx_r_mult < 0)
			sip->death_fx_r_mult = 0;
	}

	if(optional_string("$Death FX Explosion Count:")){
		stuff_int(&sip->death_fx_count);
		if (sip->death_fx_count < 0)
			sip->death_fx_count = 0;
	}

	if (optional_string("$Death Roll Rotation Multiplier:")) {
		stuff_float(&sip->death_roll_rotation_mult);
	}

	if(optional_string("$Death Roll X rotation Cap:")){
		stuff_float(&sip->death_roll_xrotation_cap);
		if (sip->death_roll_xrotation_cap < 0.0)
			sip->death_roll_xrotation_cap = 0.0;
	}

	if(optional_string("$Death Roll Y rotation Cap:")){
		stuff_float(&sip->death_roll_yrotation_cap);
		if (sip->death_roll_yrotation_cap < 0.0)
			sip->death_roll_yrotation_cap = 0.0;
	}

	if(optional_string("$Death Roll Z rotation Cap:")){
		stuff_float(&sip->death_roll_zrotation_cap);
		if (sip->death_roll_zrotation_cap < 0.0)
			sip->death_roll_zrotation_cap = 0.0;
	}

	if(optional_string("$Ship Splitting Effect:"))
	{
		sip->split_particles = particle::util::parseEffect(sip->name);
	}
	else if(optional_string("$Ship Splitting Particles:"))
	{
		sip->split_particles = parse_ship_legacy_particle_effect(LegacyShipParticleType::SPLIT_PARTICLES, sip, "ship split spew", 1.f, particle::Anim_bitmap_id_smoke2, 1.f);
	}

	if (optional_string("$Ship Death Effect:"))
	{
		sip->regular_end_particles = particle::util::parseEffect(sip->name);
	}
	else if(optional_string("$Ship Death Particles:"))
	{
		sip->regular_end_particles = parse_ship_legacy_particle_effect(LegacyShipParticleType::OTHER, sip, "normal death spew", 1.f, particle::Anim_bitmap_id_smoke2, 1.f);
	}

	if(optional_string("$Alternate Death Effect:"))
	{
		sip->knossos_end_particles = particle::util::parseEffect(sip->name);
	}
	else if(optional_string("$Alternate Death Particles:"))
	{
		sip->knossos_end_particles = parse_ship_legacy_particle_effect(LegacyShipParticleType::OTHER, sip, "knossos death spew", 50.f, particle::Anim_bitmap_id_smoke2, 1.f, true);
	}

	if(optional_string("$Debris Flame Effect:"))
	{
		sip->debris_flame_particles = particle::util::parseEffect(sip->name);
	}

	auto skip_str = "$Skip Death Roll Percent Chance:";
	auto vaporize_str = "$Vaporize Percent Chance:";
	int which;
	if ((which = optional_string_either(skip_str, vaporize_str)) >= 0) {
		if (which == 1) {
			mprintf(("Found %s for ship class '%s'.  This has been deprecated; use %s instead.\n", vaporize_str, sip->name, skip_str));
		}
		stuff_float(&sip->skip_deathroll_chance);
		if (sip->skip_deathroll_chance < 0.0f || sip->skip_deathroll_chance > 100.0f) {
			Warning(LOCATION, "%s should be between 0 and 100.0 (read %f) for ship class '%s'. Setting to 0.", which == 0 ? skip_str : vaporize_str, sip->skip_deathroll_chance, sip->name);
			sip->skip_deathroll_chance = 0.0f;
		}
		//Percent is nice for modders, but here in the code we want it between 0 and 1.0
		sip->skip_deathroll_chance /= 100.0f;
	}

	if(optional_string("$Shockwave Damage Type:")) {
		stuff_string(buf, F_NAME, SHIP_MULTITEXT_LENGTH);
		sci->damage_type_idx_sav = damage_type_add(buf);
		sci->damage_type_idx = sci->damage_type_idx_sav;
	}

	if(optional_string("$Shockwave Speed:")){
		stuff_float( &sci->speed );
	}

	if(optional_string("$Shockwave Count:")){
		stuff_int(&sip->shockwave_count);
	}

	if(optional_string("$Shockwave model:")){
		stuff_string( sci->pof_name, F_NAME, MAX_FILENAME_LEN);
	}
	
	if(optional_string("$Shockwave name:")) {
		stuff_string( sci->name, F_NAME, NAME_LENGTH);
	}

	parse_game_sound("$Shockwave Sound:", &sci->blast_sound_id);

	if(optional_string("$Explosion Animations:")){
		stuff_fireball_index_list(sip->explosion_bitmap_anims, sip->name);
	}

	if (optional_string("$Weapon Model Draw Distance:")) {
		stuff_float( &sip->weapon_model_draw_distance );
	}

	// Set the weapons filter used in weapons loadout (for primary weapons)
	parse_allowed_weapons(sip, true, false, first_time);
	parse_allowed_weapons(sip, true, true, first_time);

	// Get primary bank weapons
	parse_weapon_bank(sip, true, &sip->num_primary_banks, sip->primary_bank_weapons, sip->primary_bank_ammo_capacity);

	if(optional_string("$Show Primary Models:"))
	{
		sip->flags.set(Ship::Info_Flags::Draw_weapon_models);
		stuff_bool_list(sip->draw_primary_models, sip->num_primary_banks);
	}

	// Set the weapons filter used in weapons loadout (for secondary weapons)
	parse_allowed_weapons(sip, false, false, first_time);
	parse_allowed_weapons(sip, false, true, first_time);

	// Get secondary bank weapons
	parse_weapon_bank(sip, false, &sip->num_secondary_banks, sip->secondary_bank_weapons, sip->secondary_bank_ammo_capacity);
	
	if(optional_string("$Show Secondary Models:"))
	{
		sip->flags.set(Ship::Info_Flags::Draw_weapon_models);
		stuff_bool_list(sip->draw_secondary_models, sip->num_secondary_banks);
	}

	if (optional_string("$Ship Recoil Modifier:")){
		stuff_float(&sip->ship_recoil_modifier);
	}

	if (optional_string("$Ship Shudder Modifier:")) {
		stuff_float(&sip->ship_shudder_modifier);
	}

	int pattern_index = 0;
	SCP_vector<SCP_string> temp_string_list;

	while (optional_string("$Allowed Firing Patterns for Dynamic Primary Linking:")) {
		if (pattern_index >= MAX_SHIP_PRIMARY_BANKS) {
			error_display(0, "Firing pattern lists for ship %s defined more than %i times!", sip->name, MAX_SHIP_PRIMARY_BANKS - 1);
			advance_to_eoln(nullptr);
			continue;
		}
		stuff_string_list(temp_string_list);
		sip->dyn_firing_patterns_allowed[pattern_index].clear();
		FiringPattern pattern;
		for (auto &entry : temp_string_list) {
			if (entry == "CYCLE FORWARD") {
				pattern = FiringPattern::CYCLE_FORWARD;
			} else if (entry == "CYCLE REVERSE") {
				pattern = FiringPattern::CYCLE_REVERSE;
			} else if (entry == "RANDOM EXHAUSTIVE") {
				pattern = FiringPattern::RANDOM_EXHAUSTIVE;
			} else if (entry == "RANDOM NONREPEATING") {
				pattern = FiringPattern::RANDOM_NONREPEATING;
			} else if (entry == "RANDOM REPEATING") {
				pattern = FiringPattern::RANDOM_REPEATING;
			} else {
				Warning(LOCATION, "%s is not a valid firing pattern!", entry.c_str());
				continue;
			}
			sip->dyn_firing_patterns_allowed[pattern_index].push_back(pattern);
		}
		if (sip->dyn_firing_patterns_allowed[pattern_index].empty()) {
			sip->dyn_firing_patterns_allowed[pattern_index].push_back(FiringPattern::CYCLE_FORWARD);
		}
		pattern_index++;
	}

	if(optional_string("$Shields:")) {
		stuff_float(&sip->max_shield_strength);

		if(optional_string("+Auto Spread:")) {
			stuff_float(&sip->auto_shield_spread);
		}
		if(optional_string("+Minimum Weapon Span:")) {
			stuff_float(&sip->auto_shield_spread_min_span);
		}
		if(optional_string("+Allow Bypass:")) {
			stuff_boolean(&sip->auto_shield_spread_bypass);
		}
		if(optional_string("+Spread From LOD:")) {
			int temp;
			stuff_int(&temp);

			if (temp > sip->num_detail_levels)
				Warning(LOCATION, "+Spread From LOD for %s was %i whereas ship only has %i detail levels, ignoring...", sip->name, temp, sip->num_detail_levels);
			else
				sip->auto_shield_spread_from_lod = temp;
		}

		if (optional_string("+Max Shield Impact Effect Radius:"))
			stuff_float(&sip->max_shield_impact_effect_radius);
	}

	if(optional_string("$Model Point Shield Controls:")) {
		SCP_vector<SCP_string> ctrl_strings;
		stuff_string_list(ctrl_strings);

		// Init all to -1 in case some aren't supplied...
		sip->shield_point_augment_ctrls[FRONT_QUAD] = -1;
		sip->shield_point_augment_ctrls[REAR_QUAD] = -1;
		sip->shield_point_augment_ctrls[LEFT_QUAD] = -1;
		sip->shield_point_augment_ctrls[RIGHT_QUAD] = -1;

		for (int i = 0; i < (int)ctrl_strings.size(); i++) {
			const char *str = ctrl_strings[i].c_str();

			if (!stricmp(str, "front"))
				sip->shield_point_augment_ctrls[FRONT_QUAD] = i;
			else if (!stricmp(str, "rear"))
				sip->shield_point_augment_ctrls[REAR_QUAD] = i;
			else if (!stricmp(str, "left"))
				sip->shield_point_augment_ctrls[LEFT_QUAD] = i;
			else if (!stricmp(str, "right"))
				sip->shield_point_augment_ctrls[RIGHT_QUAD] = i;
			else if (!stricmp(str, "none"))
				;
			else
				Warning(LOCATION, "Unrecognized value \"%s\" passed to $Model Point Shield Controls, ignoring...", str);
		}
	}

	// optional shield color
	if(optional_string("$Shield Color:")){
		stuff_ubyte(&sip->shield_color[0]);
		stuff_ubyte(&sip->shield_color[1]);
		stuff_ubyte(&sip->shield_color[2]);
	}

	if(optional_string("$Shield Impact Explosion Effect:")) {
		sip->shield_impact_explosion_anim = particle::util::parseEffect(sip->name);
	}
	else if(optional_string("$Shield Impact Explosion:")) {
		char fname[MAX_NAME_LEN];
		stuff_string(fname, F_NAME, NAME_LENGTH);

		if ( VALID_FNAME(fname) ) {
			auto particle = particle::ParticleEffect(
				"", //Name
				::util::UniformFloatRange(1.f), //Particle num
				particle::ParticleEffect::Duration::ONETIME, //Single Particle Emission
				::util::UniformFloatRange(), //No duration
				::util::UniformFloatRange (-1.f), //Single particle only
				particle::ParticleEffect::ShapeDirection::HIT_NORMAL, //Particle direction
				::util::UniformFloatRange(0.f), //Velocity Inherit
				false, //Velocity Inherit absolute?
				nullptr, //Velocity volume
				::util::UniformFloatRange(), //Velocity volume multiplier
				particle::ParticleEffect::VelocityScaling::NONE, //Velocity directional scaling
				std::nullopt, //Orientation-based velocity
				std::nullopt, //Position-based velocity
				nullptr, //Position volume
				particle::ParticleEffectHandle::invalid(), //Trail
				1.f, //Chance
				false, //Affected by detail
				-1.f, //Culling range multiplier
				false, //Disregard Animation Length. Must be true for everything using particle::Anim_bitmap_X
				false, //Don't reverse animation
				true, //parent local
				false, //ignore velocity inherit if parented
				false, //position velocity inherit absolute?
				std::nullopt, //Local velocity offset
				std::nullopt, //Local offset
				::util::UniformFloatRange(0.f), //Lifetime
				::util::UniformFloatRange(1.f), //Radius
				bm_load_animation(fname)); //Bitmap

			static const int thruster_particle_curve = []() -> int {
				int curve_id = static_cast<int>(Curves.size());
				auto& curve = Curves.emplace_back(";ShipShieldParticles");
				curve.keyframes.emplace_back(curve_keyframe{vec2d{0.f, 0.f}, CurveInterpFunction::Linear, 0.f, 0.f});
				curve.keyframes.emplace_back(curve_keyframe{vec2d{100000.f, 100000.f}, CurveInterpFunction::Linear, 0.f, 0.f});
				return curve_id;
			}();

			particle.m_modular_curves.add_curve("Trigger Radius", particle::ParticleEffect::ParticleCurvesOutput::RADIUS_MULT, modular_curves_entry{thruster_particle_curve});

			sip->shield_impact_explosion_anim = particle::ParticleManager::get()->addEffect(std::move(particle));
		}
	}

	if(optional_string("$Max Shield Recharge:")){
		stuff_float(&sip->max_shield_recharge);
		CLAMP(sip->max_shield_recharge, 0.0f, 1.0f);
	}

	// The next five fields are used for the ETS
	if (optional_string("$Power Output:"))
		stuff_float(&sip->power_output);

	// Goober5000
	if (optional_string("$Shield Regeneration Rate:"))
		stuff_float(&sip->max_shield_regen_per_second);
	else if (first_time)
		sip->max_shield_regen_per_second = 0.02f;

	if (optional_string("+Shield Regen Hit Delay:")) {
		stuff_float(&sip->shield_regen_hit_delay);
		if (sip->shield_regen_hit_delay < 0.0f) {
			Warning(LOCATION, "Shield Regen Hit Delay on ship '%s' cannot be less than 0.\n", sip->name);
			sip->shield_regen_hit_delay = 0.0f;
		}
	}

	// Support ship hull shield rate - if allowed
	if(optional_string("$Support Shield Repair Rate:"))
	{
		stuff_float(&sip->sup_shield_repair_rate);
		sip->sup_shield_repair_rate *= 0.01f;
		CLAMP(sip->sup_shield_repair_rate, 0.0f, 1.0f);
	}

	// Goober5000
	if (optional_string("$Weapon Regeneration Rate:"))
		stuff_float(&sip->max_weapon_regen_per_second);
	else if (first_time)
		sip->max_weapon_regen_per_second = 0.04f;

	if (optional_string("$Shield to Weapon Transfer Quantity:"))
		stuff_float(&sip->shield_weap_amount);
	else if (first_time)
		sip->shield_weap_amount = 0.2f;

	if (optional_string("$Shield to Weapon Transfer Efficiency:"))
		stuff_float(&sip->shield_weap_efficiency);
	else if (first_time)
		sip->shield_weap_efficiency = 1.0f;

	if (optional_string("$Shield to Weapon Transfer Speed:"))
		stuff_float(&sip->shield_weap_speed);
	else if (first_time)
		sip->shield_weap_speed = 0.04f;

	if (optional_string("$Weapon to Shield Transfer Quantity:"))
		stuff_float(&sip->weap_shield_amount);
	else if (first_time)
		sip->weap_shield_amount = 0.1f;

	if (optional_string("$Weapon to Shield Transfer Efficiency:"))
		stuff_float(&sip->weap_shield_efficiency);
	else if (first_time)
		sip->weap_shield_efficiency = 1.0f;

	if (optional_string("$Weapon to Shield Transfer Speed:"))
		stuff_float(&sip->weap_shield_speed);
	else if (first_time)
		sip->weap_shield_speed = 0.04f;

	if (optional_string("$Max Oclk Speed:") || optional_string("$Max Overclock Speed:"))
		stuff_float(&sip->max_overclocked_speed);
	else if (first_time)
		sip->max_overclocked_speed = sip->max_vel.xyz.z * 1.5f;

	if (optional_string("$Max Weapon Eng:") || optional_string("$Max Weapon Energy:"))
		stuff_float(&sip->max_weapon_reserve);

	if(optional_string("$Hitpoints:"))
	{
		stuff_float(&sip->max_hull_strength);
		if (sip->max_hull_strength < 0.0f)
		{
			Warning(LOCATION, "Max hull strength on %s '%s' cannot be less than 0.  Defaulting to 100.\n", info_type_name, sip->name);
			sip->max_hull_strength = 100.0f;
		}
	}

	// Hull rep rate
	if (optional_string("$Hull Repair Rate:"))
	{
		float temp;
		stuff_float(&temp);
		temp *= 0.01f;
		
		//Sanity checking
		if (temp > 1.0f) {
			mprintf(("$Hull Repair Rate: value of %f for ship class '%s' is > 1.0, setting to 1.0!\n", temp, sip->name));
			temp = 1.0f;
		} else if (temp < -1.0f) {
			mprintf(("$Hull Repair Rate: value of %f for ship class '%s' is < -1.0, setting to -1.0!\n", temp, sip->name));
			temp = -1.0f;
		}

		sip->hull_repair_rate = temp;
	}

	// Maximum percent that hull can be self repaired
	if (optional_string("$Hull Self Repair Maximum:")) 
	{
		float temp;
		stuff_float(&temp);
		temp *= 0.01f;

		// Sanity checking
		if (temp < 0.0f) {
			mprintf(("$Hull Self Repair Maximum: value of %f for ship class '%s' is < 0.0, setting to 0.0!\n", temp, sip->name));
			temp = 0.0f;
		} else if (temp > 1.0f) {
			mprintf(("$Hull Self Repair Maximum: value of %f for ship class '%s' is > 1.0, setting to 1.0!\n", temp, sip->name));
			temp = 1.0f;
		}

		sip->hull_repair_max = temp;
	}

	// Support ship hull repair rate - if allowed
	if (optional_string("$Support Hull Repair Rate:"))
	{
		stuff_float(&sip->sup_hull_repair_rate);
		sip->sup_hull_repair_rate *= 0.01f;
		CLAMP(sip->sup_hull_repair_rate, 0.0f, 1.0f);
	}

	// Subsys rep rate
	if (optional_string("$Subsystem Repair Rate:"))
	{
		float temp;
		stuff_float(&temp);
		temp *= 0.01f;
		
		//Sanity checking
		if (temp > 1.0f) {
			mprintf(("$Subsystem Repair Rate: value of %f for ship class '%s' is > 1.0, setting to 1.0!\n", temp, sip->name));
			temp = 1.0f;
		} else if (temp < -1.0f) {
			mprintf(("$Subsystem Repair Rate: value of %f for ship class '%s' is < -1.0, setting to -1.0!\n", temp, sip->name));
			temp = -1.0f;
		}

		sip->subsys_repair_rate = temp;
	}

	// Maximum percent that subsystems can be self repaired
	if (optional_string("$Subsystem Self Repair Maximum:")) 
	{
		float temp;
		stuff_float(&temp);
		temp *= 0.01f;

		// Sanity checking
		if (temp < 0.0f) {
			mprintf(("$Subsystem Self Repair Maximum: value of %f for ship class '%s' is < 0.0, setting to 0.0!\n", temp, sip->name));
			temp = 0.0f;
		} else if (temp > 1.0f) {
			mprintf(("$Subsystem Self Repair Maximum: value of %f for ship class '%s' is > 1.0, setting to 1.0!\n", temp, sip->name));
			temp = 1.0f;
		}

		sip->subsys_repair_max = temp;
	}

	// Support ship hull repair rate
	if (optional_string("$Support Subsystem Repair Rate:"))
	{
		stuff_float(&sip->sup_subsys_repair_rate);
		sip->sup_subsys_repair_rate *= 0.01f;
		CLAMP(sip->sup_subsys_repair_rate, 0.0f, 1.0f);
	}

	if(optional_string("$Armor Type:"))
	{
		stuff_string(buf, F_NAME, SHIP_MULTITEXT_LENGTH);
		sip->armor_type_idx = armor_type_get_idx(buf);

		if(sip->armor_type_idx == -1)
			Warning(LOCATION,"Invalid armor name %s specified for hull in %s '%s'", buf, info_type_name, sip->name);
	}

	if(optional_string("$Shield Armor Type:"))
	{
		stuff_string(buf, F_NAME, SHIP_MULTITEXT_LENGTH);
		sip->shield_armor_type_idx = armor_type_get_idx(buf);

		if(sip->shield_armor_type_idx == -1)
			Warning(LOCATION,"Invalid armor name %s specified for shield in %s '%s'", buf, info_type_name, sip->name);
	}

	if (optional_string("$Flags:"))
	{
		SCP_vector<SCP_string> ship_strings;
		stuff_string_list(ship_strings);

		int ship_type_index = -1;

		if (!optional_string("+noreplace")) {
			// clear flags since we might have a modular table
			// clear only those which are actually set in the flags
			bool has_afterburner = sip->flags[Ship::Info_Flags::Afterburner];
			bool draw_weapon_models = sip->flags[Ship::Info_Flags::Draw_weapon_models];
			bool has_display_name = sip->flags[Ship::Info_Flags::Has_display_name];
			sip->flags.reset();
			sip->flags.set(Ship::Info_Flags::Afterburner, has_afterburner);
			sip->flags.set(Ship::Info_Flags::Draw_weapon_models, draw_weapon_models);
			sip->flags.set(Ship::Info_Flags::Has_display_name, has_display_name);
		}

		for (const auto &flag : ship_strings)
		{
			// get ship type from ship flags
			const char *cur_flag = flag.c_str();
			bool flag_found = false;

			// look it up in the object types table
			ship_type_index = ship_type_name_lookup(cur_flag);

			// set ship class type
			if (ship_type_index >= 0)
				sip->class_type = ship_type_index;

			// check various ship flags
			for (size_t idx = 0; idx < Num_ship_flags; idx++) {
				if ( !stricmp(Ship_flags[idx].name, cur_flag) ) {
					flag_found = true;

					if (!Ship_flags[idx].in_use)
						Warning(LOCATION, "Use of '%s' flag for %s '%s' - this flag is no longer needed.", Ship_flags[idx].name, info_type_name, sip->name);
					else 
						sip->flags.set(Ship_flags[idx].def);

					break;
				}
			}

			// catch typos or deprecations
			if (!stricmp(cur_flag, "no-collide") || !stricmp(cur_flag, "no_collide")) {
				flag_found = true;
				sip->flags.set(Ship::Info_Flags::No_collide);
			}
			if (!stricmp(cur_flag, "dont collide invisible")) {
				flag_found = true;
				sip->flags.set(Ship::Info_Flags::Ship_class_dont_collide_invis);
			}
			if (!stricmp(cur_flag, "dont bank when turning")) {
				flag_found = true;
				sip->flags.set(Ship::Info_Flags::Dont_bank_when_turning);
			}
			if (!stricmp(cur_flag, "dont clamp max velocity")) {
				flag_found = true;
				sip->flags.set(Ship::Info_Flags::Dont_clamp_max_velocity);
			}

			if ( !flag_found && (ship_type_index < 0) )
				Warning(LOCATION, "Bogus string in ship flags: %s\n", cur_flag);
		}

		// set original status of tech database flags - Goober5000
		if (sip->flags[Info_Flags::In_tech_database])
			sip->flags.set(Info_Flags::Default_in_tech_database);
		if (sip->flags[Info_Flags::In_tech_database_m])
			sip->flags.set(Info_Flags::Default_in_tech_database_m);
	}

	// Goober5000 - ensure number of banks checks out
	if (sip->num_primary_banks > MAX_SHIP_PRIMARY_BANKS)
	{
		Error(LOCATION, "%s '%s' has too many primary banks (%d).  Maximum for ships is currently %d.\n", info_type_name, sip->name, sip->num_primary_banks, MAX_SHIP_PRIMARY_BANKS);
	}

	sip->allowed_weapons.clear();

	// copy to regular allowed_weapons array
	if (!sip->allowed_bank_restricted_weapons.empty())
	{
		for (auto i = 0; i < MAX_SHIP_WEAPONS; i++)
		{
			for (auto &wf : sip->allowed_bank_restricted_weapons[i].weapon_and_flags)
			{
				if (wf.second & REGULAR_WEAPON)
					sip->allowed_weapons.set_flag(wf.first, REGULAR_WEAPON);

				if (wf.second & DOGFIGHT_WEAPON)
					sip->allowed_weapons.set_flag(wf.first, DOGFIGHT_WEAPON);
			}
		}
	}

	find_and_stuff_optional("$AI Class:", &sip->ai_class, F_NAME, Ai_class_names, Num_ai_classes, "AI class names");

	// Get Afterburner information
	// Be aware that if $Afterburner is not 1, the other Afterburner fields are not read in
	int has_afterburner = 0;

	if(optional_string("$Afterburner:"))
		stuff_boolean(&has_afterburner);

	if ( has_afterburner == 1 )
	{
		sip->flags.set(Ship::Info_Flags::Afterburner);

		if(optional_string("+Aburn Max Vel:")) {
			stuff_vec3d(&sip->afterburner_max_vel);
		}

		if(optional_string("+Aburn For accel:")) {
			stuff_float(&sip->afterburner_forward_accel);
		}

		// SparK: added reverse burner capability
		if(optional_string("+Aburn Max Reverse Vel:")) {
			stuff_float(&sip->afterburner_max_reverse_vel);
		}
		if(optional_string("+Aburn Rev accel:")) {
			stuff_float(&sip->afterburner_reverse_accel);
		}

		if(optional_string("+Aburn Fuel:")) {
			stuff_float(&sip->afterburner_fuel_capacity);
		}

		if(optional_string("+Aburn Burn Rate:")) {
			stuff_float(&sip->afterburner_burn_rate);
		}

		if(optional_string("+Aburn Rec Rate:")) {
			stuff_float(&sip->afterburner_recover_rate);
		}

		if (optional_string("+Aburn Minimum Start Fuel:")) {
			stuff_float(&sip->afterburner_min_start_fuel);
		}

		if (optional_string("+Aburn Minimum Fuel to Burn:")) {
			stuff_float(&sip->afterburner_min_fuel_to_burn);
		}

		if (optional_string("+Aburn Cooldown Time:")) {
			stuff_float(&sip->afterburner_cooldown_time);
		}

		if (!(sip->afterburner_fuel_capacity) ) {
			Warning(LOCATION, "%s '%s' has an afterburner but has no afterburner fuel. Setting fuel to 1", info_type_name, sip->name);
			sip->afterburner_fuel_capacity = 1.0f;
		}
	}
	
	if ( optional_string("$Trails:") ) {
		bool trails_warning = true;

		if (optional_string("+Bitmap:") ) {
			trails_warning = false;
			generic_bitmap_init(&sip->afterburner_trail, NULL);
			stuff_string(sip->afterburner_trail.filename, F_NAME, MAX_FILENAME_LEN);
		}

		if (optional_string("+Bitmap Stretch:")) {
			trails_warning = false;
			stuff_float(&sip->afterburner_trail_tex_stretch);
			if (sip->afterburner_trail_tex_stretch == 0.0f) {
				Warning(LOCATION, "Trail bitmap stretch of ship %s cannot be 0.  Setting to 1.\n", sip->name);
				sip->afterburner_trail_tex_stretch = 1.0f;
			}
		}
		
		if ( optional_string("+Width:") ) {
			trails_warning = false;
			stuff_float(&sip->afterburner_trail_width_factor);
		}
			
		if ( optional_string("+Alpha:") ) {
			trails_warning = false;
			stuff_float(&sip->afterburner_trail_alpha_factor);
		}

		if (optional_string("+Alpha End:")) {
			trails_warning = false;
			stuff_float(&sip->afterburner_trail_alpha_end_factor);
		}

		if (optional_string("+Alpha Decay Exponent:")) {
			trails_warning = false;
			stuff_float(&sip->afterburner_trail_alpha_decay_exponent);
			if (sip->afterburner_trail_alpha_decay_exponent < 0.0f) {
				Warning(LOCATION, "Trail Alpha Decay Exponent of ship %s cannot be negative. Resetting to 1.\n", sip->name);
				sip->afterburner_trail_alpha_decay_exponent = 1.0f;
			}
		}
			
		if ( optional_string("+Life:") ) {
			trails_warning = false;
			stuff_float(&sip->afterburner_trail_life);
		}

		if (optional_string("+Spread:")) {
			trails_warning = false;
			stuff_float(&sip->afterburner_trail_spread);
		}

		if ( optional_string("+Faded Out Sections:") ) {
			trails_warning = false;
			stuff_int(&sip->afterburner_trail_faded_out_sections);
		}

		if (trails_warning)
			Warning(LOCATION, "%s %s entry has $Trails field specified, but no properties given.", info_type_name, sip->name);
	}

	if (optional_string("$Countermeasure type:")) {
		stuff_string(buf, F_NAME, SHIP_MULTITEXT_LENGTH);
		int res = weapon_info_lookup(buf);
		if (res < 0) {
			Warning(LOCATION, "Could not find weapon type '%s' to use as countermeasure on %s '%s'", buf, info_type_name, sip->name);
		} else if (Weapon_info[res].wi_flags[Weapon::Info_Flags::Beam]) {
			Warning(LOCATION, "Attempt made to set a beam weapon as a countermeasure on %s '%s'", info_type_name, sip->name);
		} else {
			sip->cmeasure_type = res;
		}
	} else if (first_time && Species_info[sip->species].cmeasure_index >= 0) {
		sip->cmeasure_type = Species_info[sip->species].cmeasure_index;
	}

	if(optional_string("$Countermeasures:"))
		stuff_int(&sip->cmeasure_max);

	if(optional_string("$Scan time:"))
		stuff_int(&sip->scan_time);

	if(optional_string("$Scan range Normal:"))
		stuff_float(&sip->scan_range_normal);

	if(optional_string("$Scan range Capital:"))
		stuff_float(&sip->scan_range_capital);

	if(optional_string("$Scanning time multiplier:"))
		stuff_float(&sip->scanning_time_multiplier);

	if(optional_string("$Scanning range multiplier:"))
		stuff_float(&sip->scanning_range_multiplier);

	if (optional_string("$Ask Help Shield Percent:")) {
		float help_shield_val;
		stuff_float(&help_shield_val);
		if (help_shield_val > 0.0f && help_shield_val <= 1.0f) {
			sip->ask_help_shield_percent = help_shield_val;
		} else {
			error_display(0,"Ask Help Shield Percent for ship class %s is %f. This value is not within range of 0-1.0."
			              "Assuming default value of %f.", sip->name, help_shield_val, DEFAULT_ASK_HELP_SHIELD_PERCENT);
		}
	}

	if (optional_string("$Ask Help Hull Percent:")) {
		float help_hull_val;
		stuff_float(&help_hull_val);
		if (help_hull_val > 0.0f && help_hull_val <= 1.0f) {
			sip->ask_help_shield_percent = help_hull_val;
		} else {
			error_display(0,"Ask Help Hull Percent for ship class %s is %f. This value is not within range of 0-1.0."
			              "Assuming default value of %f.", sip->name, help_hull_val, DEFAULT_ASK_HELP_HULL_PERCENT);
		}
	}

	//Parse the engine sound
	parse_game_sound("$EngineSnd:", &sip->engine_snd);

	if(optional_string("$Minimum Engine Volume:"))
		stuff_float(&sip->min_engine_vol);

	//Parse optional sound to be used for beginning of a glide
	parse_game_sound("$GlideStartSnd:", &sip->glide_start_snd);

	//Parse optional sound to be used for end of a glide
	parse_game_sound("$GlideEndSnd:", &sip->glide_end_snd);

	// Parse optional sound to be used for flyby sound
	parse_game_sound("$Flyby Sound:", &sip->flyby_snd);

	parse_ship_sounds(sip);
	
	if(optional_string("$Closeup_pos:"))
	{
		stuff_vec3d(&sip->closeup_pos);
	}
	else if (first_time && VALID_FNAME(sip->pof_file))
	{
		//Calculate from the model file. This is inefficient, but whatever
		int model_idx = model_load(sip->pof_file);
		polymodel *pm = model_get(model_idx);

		//Go through, find best
		sip->closeup_pos.xyz.z = fabsf(pm->maxs.xyz.z);

		float temp = fabsf(pm->mins.xyz.z);
		if(temp > sip->closeup_pos.xyz.z)
			sip->closeup_pos.xyz.z = temp;

		//Now multiply by 2
		sip->closeup_pos.xyz.z *= -2.0f;

		//We're done with the model.
		model_unload(model_idx);
	}

	if (optional_string("$Closeup_zoom:")) {
		stuff_float(&sip->closeup_zoom);

		if (sip->closeup_zoom <= 0.0f) {
			mprintf(("Warning!  Ship '%s' has a $Closeup_zoom value that is less than or equal to 0 (%f). Setting to default value.\n", sip->name, sip->closeup_zoom));
			sip->closeup_zoom = 0.5f;
		}
	}

	if(optional_string("$Closeup_pos_targetbox:"))
	{
		stuff_vec3d(&sip->closeup_pos_targetbox);
	} 
	else if (first_time)
	{ 
		sip->closeup_pos_targetbox = sip->closeup_pos;
	}

	if (optional_string("$Closeup_zoom_targetbox:")) {
		stuff_float(&sip->closeup_zoom_targetbox);

		if (sip->closeup_zoom_targetbox <= 0.0f) {
			mprintf(("Warning!  Ship '%s' has a $Closeup_zoom_targetbox value that is less than or equal to 0 (%f). Setting to default value.\n", sip->name, sip->closeup_zoom_targetbox));
			sip->closeup_zoom_targetbox = 0.5f;
		}
	}
	else if (first_time)
	{
		sip->closeup_zoom_targetbox = sip->closeup_zoom;
	}
		
	if(optional_string("$Topdown offset:")) {
		sip->topdown_offset_def = true;
		stuff_vec3d(&sip->topdown_offset);
	}

	if (optional_string("$Chase View Offset:"))	{
		stuff_vec3d(&sip->chase_view_offset);
	}

	if (optional_string("$Chase View Rigidity:")) {
		stuff_float(&sip->chase_view_rigidity);
		if (sip->chase_view_rigidity <= 0) {
			Warning(LOCATION, "Ship \'%s\' must have a Chase View Rigidity greater than 0.", sip->name);
			sip->chase_view_rigidity = 5.0f;
		}
	}

	if (optional_string("$Shield_icon:")) {
		stuff_string(name_tmp, F_NAME, sizeof(name_tmp));
		hud_shield_assign_info(sip, name_tmp);
	}

	// read in filename for icon that is used in ship selection
	if ( optional_string("$Ship_icon:") ) {
		stuff_string(sip->icon_filename, F_NAME, MAX_FILENAME_LEN);
	}

	if ( optional_string("$Model Icon Direction:") ) {
		char str[NAME_LENGTH];
		stuff_string(str, F_NAME, NAME_LENGTH);

		angles model_icon_angles = vmd_zero_angles;

		if (!stricmp(str, "top")) {
			model_icon_angles.p = -PI_2;
		} else if (!stricmp(str, "bottom")) {
			model_icon_angles.p = -PI_2;
			model_icon_angles.b = 2 * PI_2;
		} else if (!stricmp(str, "front")) {
			model_icon_angles.h = 2 * PI_2;
		} else if (!stricmp(str, "back")) {
			model_icon_angles.h = 4 * PI_2;
		} else if (!stricmp(str, "left")) {
			model_icon_angles.h = -PI_2;
		} else if (!stricmp(str, "right")) {
			model_icon_angles.h = PI_2;
		} else {
			Warning(LOCATION, "Unrecognized value \"%s\" passed to $Model Icon Direction, ignoring...", str);
		}

		sip->model_icon_angles = model_icon_angles;
	}

	// read in filename for animation that is used in ship selection
	if ( optional_string("$Ship_anim:") ) {
		stuff_string(sip->anim_filename, F_NAME, MAX_FILENAME_LEN);
	}

	// read in filename for animation that is used in ship selection
	if ( optional_string("$Ship_overhead:") ) {
		stuff_string(sip->overhead_filename, F_NAME, MAX_FILENAME_LEN);
	}

	// read in filename for optional animation to use for wingmen dot status --wookieejedi
	if (optional_string("$Wingmen Gauge Dot Override:")) {
		stuff_string(name_tmp, F_NAME, sizeof(name_tmp));
		int wm_dot_idx = -1;
		int wm_dot_num_frames = 0;
		wm_dot_idx = bm_load_animation(name_tmp, &wm_dot_num_frames);
		if ((wm_dot_idx > -1) && (wm_dot_num_frames == 2)) {
			sip->wingmen_status_dot_override = wm_dot_idx;
		} else {
			// two things could have gone wrong, so tell the modder
			if (wm_dot_idx < 0) {
				Warning(LOCATION, "Error loading '%s' animation for $Wingmen Gauge Dot Override, "
					"ignoring and using default dot animation from HUD table.", name_tmp);
			}
			if (wm_dot_num_frames != 2) {
				Warning(LOCATION, "Error, number of frames in '%s' animation for $Wingmen Gauge Dot Override is %i and not 2, "
					"ignoring and using default dot animation from HUD table.", name_tmp, wm_dot_num_frames);
			}
		}
	}

	// read in briefing stuff
	if ( optional_string("$Briefing icon:") )
		sip->bii_index_ship = parse_and_add_briefing_icon_info();
	if ( optional_string("$Briefing icon with cargo:") )
		sip->bii_index_ship_with_cargo = parse_and_add_briefing_icon_info();
	if ( optional_string("$Briefing wing icon:") )
		sip->bii_index_wing = parse_and_add_briefing_icon_info();
	if ( optional_string("$Briefing wing icon with cargo:") )
		sip->bii_index_wing_with_cargo = parse_and_add_briefing_icon_info();

	// check for inconsistencies
	if ((sip->bii_index_wing_with_cargo >= 0) && (sip->bii_index_wing < 0 || sip->bii_index_ship_with_cargo < 0))
		Warning(LOCATION, "%s '%s' has a wing-with-cargo briefing icon but is missing a wing briefing icon or a ship-with-cargo briefing icon!", info_type_name, sip->name);
	if ((sip->bii_index_wing_with_cargo < 0) && (sip->bii_index_wing >= 0) && (sip->bii_index_ship_with_cargo >= 0))
		Warning(LOCATION, "%s '%s' has both a wing briefing icon and a ship-with-cargo briefing icon but does not have a wing-with-cargo briefing icon!", info_type_name, sip->name);

	if ( optional_string("$Score:") ){
		stuff_int( &sip->score );
	}

	if (first_time)
	{
		species_info *species = &Species_info[sip->species];

		sip->thruster_flame_info = species->thruster_info.flames;
		sip->thruster_glow_info = species->thruster_info.glow;
		sip->thruster_secondary_glow_info = species->thruster_secondary_glow_info;
		sip->thruster_tertiary_glow_info = species->thruster_tertiary_glow_info;
		sip->thruster_distortion_info = species->thruster_distortion_info;
	}

	if ( optional_string("$Thruster Normal Flame:") ) {
		stuff_string( name_tmp, F_NAME, sizeof(name_tmp) );
	
		if ( VALID_FNAME(name_tmp) )
			generic_anim_init( &sip->thruster_flame_info.normal, name_tmp );
	}

	if ( optional_string("$Thruster Afterburner Flame:") ) {
		stuff_string( name_tmp, F_NAME, sizeof(name_tmp) );

		if ( VALID_FNAME(name_tmp) )
			generic_anim_init( &sip->thruster_flame_info.afterburn, name_tmp );
	}

	if ( optional_string("$Thruster Bitmap 1:") ) {
		stuff_string( name_tmp, F_NAME, sizeof(name_tmp) );
	
		if ( VALID_FNAME(name_tmp) ) {
			strcpy_s(sip->thruster_glow_info.normal.filename, name_tmp);
			thruster_glow_anim_load( &sip->thruster_glow_info.normal );
		}
	}

	if ( optional_string("$Thruster Bitmap 1a:") ) {
		stuff_string( name_tmp, F_NAME, sizeof(name_tmp) );

		if ( VALID_FNAME(name_tmp) ) {
			strcpy_s(sip->thruster_glow_info.afterburn.filename, name_tmp);
			thruster_glow_anim_load( &sip->thruster_glow_info.afterburn );
		}
	}

	if ( optional_string("$Thruster01 Radius factor:") ) {
		stuff_float(&sip->thruster01_glow_rad_factor);
	}

	if ( optional_string("$Thruster Bitmap 2:") ) {
		stuff_string( name_tmp, F_NAME, sizeof(name_tmp) );

		if ( VALID_FNAME(name_tmp) )
			generic_bitmap_init( &sip->thruster_secondary_glow_info.normal, name_tmp );
	}

	if ( optional_string("$Thruster Bitmap 2a:") ) {
		stuff_string( name_tmp, F_NAME, sizeof(name_tmp) );

		if ( VALID_FNAME(name_tmp) )
			generic_bitmap_init( &sip->thruster_secondary_glow_info.afterburn, name_tmp );
	}

	if ( optional_string("$Thruster02 Radius factor:") ) {
		stuff_float(&sip->thruster02_glow_rad_factor);
	}

	if ( optional_string("$Thruster01 Length factor:") ) {
		stuff_float(&sip->thruster02_glow_len_factor);
		Warning(LOCATION, "Deprecated spelling: \"$Thruster01 Length factor:\".  Use \"$Thruster02 Length factor:\" instead.");
	}

	if ( optional_string("$Thruster02 Length factor:") ) {
		stuff_float(&sip->thruster02_glow_len_factor);
	}

	if ( optional_string("$Thruster Bitmap 3:") ) {
		stuff_string( name_tmp, F_NAME, sizeof(name_tmp) );

		if ( VALID_FNAME(name_tmp) )
			generic_bitmap_init( &sip->thruster_tertiary_glow_info.normal, name_tmp );
	}

	if ( optional_string("$Thruster Bitmap 3a:") ) {
		stuff_string( name_tmp, F_NAME, sizeof(name_tmp) );

		if ( VALID_FNAME(name_tmp) )
			generic_bitmap_init( &sip->thruster_tertiary_glow_info.afterburn, name_tmp );
	}

	if ( optional_string("$Thruster03 Radius factor:") ) {
		stuff_float(&sip->thruster03_glow_rad_factor);
	}

	// Valathil - Custom Thruster Distortion
	if ( optional_string("$Thruster Bitmap Distortion:") ) {
		stuff_string( name_tmp, F_NAME, sizeof(name_tmp) );

		if ( VALID_FNAME(name_tmp) )
			generic_bitmap_init( &sip->thruster_distortion_info.normal, name_tmp );
	}

	if ( optional_string("$Thruster Bitmap Distortion a:") ) {
		stuff_string( name_tmp, F_NAME, sizeof(name_tmp) );

		if ( VALID_FNAME(name_tmp) )
			generic_bitmap_init( &sip->thruster_distortion_info.afterburn, name_tmp );
	}

	if ( optional_string("$Thruster Distortion Radius factor:") ) {
		stuff_float(&sip->thruster_dist_rad_factor);
	}

	if ( optional_string("$Thruster Distortion Length factor:") ) {
		stuff_float(&sip->thruster_dist_len_factor);
	}

	if ( optional_string("$Thruster Distortion:") ) {
		stuff_boolean(&sip->draw_distortion);
	}

	if ( optional_string("$Thruster Glow Noise Mult:") ) {
		stuff_float(&sip->thruster_glow_noise_mult);
	}

	while ( optional_string("$Thruster Particles:") ) {
		bool afterburner = false;
		bool modern_particle = false;
		thruster_particles tpart;

		if ( optional_string("$Thruster Particle Bitmap:") )
			afterburner = false;
		else if ( optional_string("$Afterburner Particle Bitmap:") )
			afterburner = true;
		else if ( optional_string("$Thruster Effect:") ) {
			afterburner = true;
			modern_particle = true;
		}
		else if ( optional_string("$Afterburner Effect:") ) {
			afterburner = true;
			modern_particle = true;
		}
		else
			Error( LOCATION, "formatting error in the thruster's particle section for %s '%s'\n", info_type_name, sip->name );

		if (modern_particle) {
			tpart.particle_handle = particle::util::parseEffect(sip->name);
		}
		else {
			generic_anim_init(&tpart.thruster_bitmap, nullptr);

			stuff_string(tpart.thruster_bitmap.filename, F_NAME, MAX_FILENAME_LEN);

			float min_rad, max_rad;
			int min_n, max_n;
			float variance;
			required_string("$Min Radius:");
			stuff_float(&min_rad);

			required_string("$Max Radius:");
			stuff_float(&max_rad);

			required_string("$Min created:");
			stuff_int(&min_n);

			required_string("$Max created:");
			stuff_int(&max_n);

			required_string("$Variance:");
			stuff_float(&variance);

			generic_anim_load(&tpart.thruster_bitmap);

			auto particle = particle::ParticleEffect(
				"", //Name
				::util::UniformFloatRange(i2fl(min_n), i2fl(max_n)), //Particle num
				particle::ParticleEffect::Duration::ONETIME, //Single Particle Emission
				::util::UniformFloatRange(), //No duration
				::util::UniformFloatRange (-1.f), //Single particle only
				particle::ParticleEffect::ShapeDirection::ALIGNED, //Particle direction
				::util::UniformFloatRange(1.f), //Velocity Inherit
				true, //Velocity Inherit absolute?
				make_unique<particle::LegacyAACuboidVolume>(variance, 1.f, true), //Velocity volume
				::util::UniformFloatRange(0.75f, 1.25f), //Velocity volume multiplier
				particle::ParticleEffect::VelocityScaling::NONE, //Velocity directional scaling
				std::nullopt, //Orientation-based velocity
				std::nullopt, //Position-based velocity
				nullptr, //Position volume
				particle::ParticleEffectHandle::invalid(), //Trail
				1.f, //Chance
				true, //Affected by detail
				1.0f, //Culling range multiplier
				false, //Disregard Animation Length. Must be true for everything using particle::Anim_bitmap_X
				false, //Don't reverse animation
				false, //parent local
				false, //ignore velocity inherit if parented
				false, //position velocity inherit absolute?
				std::nullopt, //Local velocity offset
				std::nullopt, //Local offset
				::util::UniformFloatRange(0.0f, 1.0f), //Lifetime
				::util::UniformFloatRange(min_rad, max_rad), //Radius
				tpart.thruster_bitmap.first_frame); //Bitmap

			static const int thruster_particle_curve = []() -> int {
				int curve_id = static_cast<int>(Curves.size());
				auto& curve = Curves.emplace_back(";ShipParticleThruster");
				curve.keyframes.emplace_back(curve_keyframe{vec2d{0.f, 0.f}, CurveInterpFunction::Linear, 0.f, 0.f});
				curve.keyframes.emplace_back(curve_keyframe{vec2d{100000.f, 100000.f}, CurveInterpFunction::Linear, 0.f, 0.f});
				return curve_id;
			}();

			particle.m_modular_curves.add_curve("Trigger Velocity", particle::ParticleEffect::ParticleCurvesOutput::VOLUME_VELOCITY_MULT, modular_curves_entry{thruster_particle_curve});
			particle.m_modular_curves.add_curve("Trigger Velocity", particle::ParticleEffect::ParticleCurvesOutput::INHERIT_VELOCITY_MULT, modular_curves_entry{thruster_particle_curve});
			particle.m_modular_curves.add_curve("Trigger Radius", particle::ParticleEffect::ParticleCurvesOutput::RADIUS_MULT, modular_curves_entry{thruster_particle_curve});

			tpart.particle_handle = particle::ParticleManager::get()->addEffect(std::move(particle));
		}

		if (afterburner) {
			sip->afterburner_thruster_particles.push_back( tpart );
		} else {
			sip->normal_thruster_particles.push_back( tpart );
		}
	}

	// if the ship is a stealth ship
	if ( optional_string("$Stealth:") ) {
		sip->flags.set(Ship::Info_Flags::Stealth);
	}	
	else if ( optional_string("$Stealth") ) {
		Warning(LOCATION, "%s '%s' is missing the colon after \"$Stealth\". Note that you may also use the ship flag \"stealth\".", info_type_name, sip->name);
		sip->flags.set(Ship::Info_Flags::Stealth);
	}

	if ( optional_string("$max decals:") ){
		int bogus;
		stuff_int(&bogus);
		WarningEx(LOCATION, "The decal system has been deactivated in FSO builds. Entries will be discarded.\n");
		mprintf(("WARNING: The decal system has been deactivated in FSO builds. Entries will be discarded.\n"));
		//Do nothing, left in for compatibility.
	}

	// parse contrail info
	while ( optional_string("$Trail:") ) {
		// setting '+ClearAll' resets the trails
		if ( optional_string("+ClearAll")) {
			memset(&sip->ct_info, 0, sizeof(trail_info) * MAX_SHIP_CONTRAILS);
			sip->ct_count = 0;
		}

		// this means you've reached the max # of contrails for a ship
		if (sip->ct_count >= MAX_SHIP_CONTRAILS) {
			Warning(LOCATION, "%s '%s' has more contrails than the max of %d", info_type_name, sip->name, MAX_SHIP_CONTRAILS);
			break;
		}

		trail_info *ci = &sip->ct_info[sip->ct_count++];
		trail_info_init(ci);
		
		required_string("+Offset:");
		stuff_vec3d(&ci->pt);
		
		required_string("+Start Width:");
		stuff_float(&ci->w_start);
		
		required_string("+End Width:");
		stuff_float(&ci->w_end);
		
		required_string("+Start Alpha:");
		stuff_float(&ci->a_start);
		
		required_string("+End Alpha:");
		stuff_float(&ci->a_end);

		if (optional_string("+Alpha Decay Exponent:")) {
			stuff_float(&ci->a_decay_exponent);
			if (ci->a_decay_exponent < 0.0f) {
				Warning(LOCATION, "Trail Alpha Decay Exponent of ship %s cannot be negative. Resetting to 1.\n", sip->name);
				ci->a_decay_exponent = 1.0f;
			}
		}

		required_string("+Max Life:");
		stuff_float(&ci->max_life);

		if (optional_string("+Spread:"))
			stuff_float(&ci->spread);
		
		required_string("+Spew Time:");
		stuff_int(&ci->spew_duration);

		required_string("+Bitmap:");
		stuff_string(name_tmp, F_NAME, NAME_LENGTH);
		generic_bitmap_init(&ci->texture, name_tmp);
		generic_bitmap_load(&ci->texture);
		if (ci->texture.bitmap_id == -1) {
			Warning(LOCATION, "Trail bitmap %s could not be loaded. Trail will not be rendered.", name_tmp);
		}

		if (optional_string("+Bitmap Stretch:")) {
			stuff_float(&ci->texture_stretch);
			if (ci->texture_stretch == 0.0f) {
				Warning(LOCATION, "Trail bitmap stretch of ship %s cannot be 0.  Setting to 1.\n", sip->name);
				ci->texture_stretch = 1.0f;
			}
		}

		if (optional_string("+Faded Out Sections:") ) {
			stuff_int(&ci->n_fade_out_sections);
		}
	}

	while(optional_string("$Thruster:"))
	{
		rcs_thruster_info *mtp = nullptr;
		rcs_thruster_info rcs_info_placeholder;	// The variable formerly known as manwich
		int idx = -1;
		if(optional_string("+index:")) {
			stuff_int(&idx);
		}

		if(idx >= 0 && idx < static_cast<int>(sip->rcs_thrusters.size())) {
			mtp = &sip->rcs_thrusters[idx];
		} else if(idx < 0) {
			sip->rcs_thrusters.emplace_back();
			mtp = &sip->rcs_thrusters.back();
		} else {
			mtp = &rcs_info_placeholder;
			Warning(LOCATION, "Invalid index (%d) specified for maneuvering thruster on %s '%s'", idx, info_type_name, sip->name);
		}

		if(optional_string("+Used for:")) {
			SCP_vector<SCP_string> unparsed;
			parse_string_flag_list(mtp->use_flags, RCS_types, Num_rcs_types, &unparsed);

			// backwards compatibility
			for (const auto& str : unparsed) {
				if (!stricmp(str.c_str(), "Roll right"))
					mtp->use_flags.set(Ship::Thruster_Flags::Yaw_right);
				else if (!stricmp(str.c_str(), "Roll left"))
					mtp->use_flags.set(Ship::Thruster_Flags::Yaw_left);
				else
					Warning(LOCATION, "Unrecognized string in %s $Thruster +Used for: %s", sip->name, str.c_str());
			}
		}

		if(optional_string("+Position:")) {
			stuff_float_list(mtp->pos.a1d, 3);
		}

		if(optional_string("+Normal:")) {
			stuff_float_list(mtp->norm.a1d, 3);
		}

		if(optional_string("+Texture:"))
		{
			stuff_string(name_tmp, F_NAME, sizeof(name_tmp));
			int tex_fps=0, tex_nframes=0, tex_id=-1;;
			try {
				tex_id = bm_load_animation(name_tmp, &tex_nframes, &tex_fps, nullptr, nullptr, true, CF_TYPE_ANY, true);
			} catch (const std::exception& e) {
				mprintf(("Could not load thruster texture %s as animation (%s). Attempting to load as static image.\n",
					name_tmp,
					e.what()));
				tex_id = -1;
			}

			if(tex_id < 0)
				tex_id = bm_load(name_tmp);
			if(tex_id >= 0)
			{
				if(mtp->tex_id >= 0) {
					bm_unload(mtp->tex_id);
				}

				mtp->tex_id = tex_id;
				mtp->tex_fps = tex_fps;
				mtp->tex_nframes = tex_nframes;
			} else {
				mprintf(("Failed to load thruster texture %s\n", name_tmp));
			}
		}

		if(optional_string("+Radius:")) {
			stuff_float(&mtp->radius);
		}

		if(optional_string("+Length:")) {
			stuff_float(&mtp->length);
		}

		// make sure norm is normalized, and put any extra in length
		mtp->length *= vm_vec_normalize(&mtp->norm);

		parse_game_sound("+StartSnd:", &mtp->start_snd);
		parse_game_sound("+LoopSnd:", &mtp->loop_snd);
		parse_game_sound("+StopSnd:", &mtp->stop_snd);
	}

	if (optional_string("$Glowpoint overrides:")) {
		SCP_vector<SCP_string> tokens;
		tokens.clear();
		stuff_string_list(tokens);
		for(SCP_vector<SCP_string>::iterator token = tokens.begin(); token != tokens.end(); ++token) {
			SCP_string name, banks;
			size_t seppos;
			seppos = token->find_first_of(':');
			if(seppos == SCP_string::npos) {
				Warning(LOCATION, "Couldn't find ':' separator in Glowpoint override for ship %s ignoring token", sip->name);
				continue;
			}
			name = token->substr(0, seppos);
			banks = token->substr(seppos+1);
			SCP_vector<glow_point_bank_override>::iterator gpo = get_glowpoint_bank_override_by_name(name.data());
			if(gpo == glowpoint_bank_overrides.end()){
				Warning(LOCATION, "Couldn't find preset %s in glowpoints.tbl when parsing ship: %s", name.data(), sip->name);
				continue;
			}
			if(banks == "*") {
				sip->glowpoint_bank_override_map[-1] = (void*)(&(*gpo));
				continue;
			}
			SCP_string banktoken;
			size_t start = 0;
			size_t end;
			do {
				end = banks.find_first_of(',', start);
				banktoken = banks.substr(start, end);
				start = end + 1;
				
				size_t fromtopos;
				fromtopos = banktoken.find_first_of('-');
				if(fromtopos != SCP_string::npos) {
					SCP_string from, to;
					int ifrom, ito;
					from = banktoken.substr(0, fromtopos);
					to = banktoken.substr(fromtopos+1);
					ifrom = atoi(from.data()) - 1;
					ito = atoi(to.data()) - 1;
					for(int bank = ifrom; bank <= ito; ++bank) {
						sip->glowpoint_bank_override_map[bank] = (void*)(&(*gpo));
					}
				} else {
					int bank = atoi(banktoken.data()) - 1;
					sip->glowpoint_bank_override_map[bank] = (void*)(&(*gpo));
				}
			} while(end !=SCP_string::npos);
		}
	}

	if (optional_string("$Radar Image 2D:"))
	{
		stuff_string(name_tmp, F_NAME, NAME_LENGTH);
		sip->radar_image_2d_idx = bm_load(name_tmp);

		if ( optional_string("$Radar Color Image 2D:") ) {
			stuff_string(name_tmp, F_NAME, NAME_LENGTH);
			sip->radar_color_image_2d_idx = bm_load(name_tmp);
		}

		if (optional_string("$Radar Image Size:"))
			stuff_int(&sip->radar_image_size);

		if (optional_string("$3D Radar Blip Size Multiplier:"))
			stuff_float(&sip->radar_projection_size_mult);
	}

	// Alternate - per ship class - IFF colors
	while((optional_string("$Ship IFF Colors:")) || (optional_string("$Ship IFF Colours:")))
	{
		char iff_1[NAME_LENGTH];
		char iff_2[NAME_LENGTH];
		int iff_color_data[3];
		int iff_data[2];
		
		// Get the iff strings and get the iff indexes
		required_string("+Seen By:");
		stuff_string(iff_1, F_NAME, NAME_LENGTH);
		
		required_string("+When IFF Is:");
		stuff_string(iff_2, F_NAME, NAME_LENGTH);
		iff_data[0] = iff_lookup(iff_1);
		iff_data[1] = iff_lookup(iff_2);

		if (iff_data[0] == -1)
			WarningEx(LOCATION, "%s '%s'\nIFF colour seen by \"%s\" invalid!", info_type_name, sip->name, iff_1);

		if (iff_data[1] == -1)
			WarningEx(LOCATION, "%s '%s'\nIFF colour when IFF is \"%s\" invalid!", info_type_name, sip->name, iff_2);

		// Set the color
		required_string("+As Color:");
		stuff_int_list(iff_color_data, 3, RAW_INTEGER_TYPE);
		sip->ship_iff_info[{iff_data[0],iff_data[1]}] = iff_init_color(iff_color_data[0], iff_color_data[1], iff_color_data[2]);
	}

	if (optional_string("$Target Priority Groups:") ) {
		SCP_vector<SCP_string> target_group_strings;
		stuff_string_list(target_group_strings);
		size_t num_groups = Ai_tp_list.size();
		bool override_strings = false;

		if (optional_string("+Override")) {
			override_strings = true;
		}

		for(size_t j = 0; j < target_group_strings.size(); j++) {
			size_t i;
			for(i = 0; i < num_groups; i++) {
				if ( !stricmp(target_group_strings[j].c_str(), Ai_tp_list[i].name) ) {
					//so now the string from the list above as well as the ai priority group name match
					//clear it if override has been set
					if (override_strings) {
						Ai_tp_list[i].ship_class.clear();
						override_strings = false;
					}
					for (auto it = Ship_info.cbegin(); it != Ship_info.cend(); ++it) {
						//find the index number of the current ship info type
						if (&(*it) == sip) {
							Ai_tp_list[i].ship_class.push_back((int)std::distance(Ship_info.cbegin(), it));
							break;
						}
					}
					// found something, try next string
					break;
				}
			}
			if (i == num_groups) {
				Warning(LOCATION,"Unidentified priority group '%s' set for %s '%s'\n", target_group_strings[j].c_str(), info_type_name, sip->name);
			}
		}
	}

	if (optional_string("$EMP Resistance Modifier:")) {
		stuff_float(&sip->emp_resistance_mod);
	}
	
	if (optional_string("$Piercing Damage Draw Limit:")) {
		float tempf;
		stuff_float(&tempf);
		sip->piercing_damage_draw_limit = tempf / 100.0f;
	}

	while(optional_string("$Path Metadata:")) 
	{
		char path_name[64];
		stuff_string(path_name, F_NAME, sizeof(path_name));

		path_metadata metadata;
		init_path_metadata(metadata);

		//Get +departure and arrival rvec and store on the path_metadata object
		if (optional_string("+arrival rvec:"))
		{
			stuff_vec3d(&metadata.arrival_rvec);
		}

		if (optional_string("+departure rvec:"))
		{
			stuff_vec3d(&metadata.departure_rvec);
		}

		if (optional_string("+arrive speed multiplier:"))
		{
			stuff_float(&metadata.arrive_speed_mult);
		}
		if (optional_string("+depart speed multiplier:"))
		{
			stuff_float(&metadata.depart_speed_mult);
		}

		//Add the new path_metadata to sip->pathMetadata keyed by path name
		SCP_string pathName(path_name);
		sip->pathMetadata[pathName] = metadata;
	}

	while (optional_string("$Passive Lightning Arcs:")) {
		ship_passive_arc_info new_info;

		SCP_string temp1, temp2;
		vec3d pos1, pos2;
		required_string("+Submodel 1:");
		stuff_string(temp1, F_NAME);
		required_string("+Position 1:");
		stuff_vec3d(&pos1);
		required_string("+Submodel 2:");
		stuff_string(temp2, F_NAME);
		required_string("+Position 2:");
		stuff_vec3d(&pos2);

		new_info.pos = std::make_pair(pos1, pos2);
		new_info.submodel_strings = std::make_pair(temp1, temp2);
		new_info.submodels = std::make_pair(-1, -1); // this will be filled later once we have the model

		required_string("+Duration:");
		stuff_float(&new_info.duration);
		required_string("+Frequency:");
		stuff_float(&new_info.frequency);

		if (optional_string("+Width:")) {
			stuff_float(&new_info.width);
		} else {
			new_info.width = 0.0f;
		}

		if (optional_string("+Primary color 1:")) {
			int rgb[3];
			stuff_int_list(rgb, 3, RAW_INTEGER_TYPE);
			gr_init_color(&new_info.primary_color_1, rgb[0], rgb[1], rgb[2]);
		} else {
			new_info.primary_color_1 = Arc_color_damage_p1;
		}
		if (optional_string("+Primary color 2:")) {
			int rgb[3];
			stuff_int_list(rgb, 3, RAW_INTEGER_TYPE);
			gr_init_color(&new_info.primary_color_2, rgb[0], rgb[1], rgb[2]);
		} else {
			new_info.primary_color_2 = Arc_color_damage_p2;
		}
		if (optional_string("+Secondary color:")) {
			int rgb[3];
			stuff_int_list(rgb, 3, RAW_INTEGER_TYPE);
			gr_init_color(&new_info.secondary_color, rgb[0], rgb[1], rgb[2]);
		} else {
			new_info.secondary_color = Arc_color_damage_s1;
		}

		sip->ship_passive_arcs.push_back(new_info);
	}

	if (optional_string("$Animations:")) {
		animation::ModelAnimationParseHelper::parseAnimsetInfo(sip->animations, sip);
	}

	if (optional_string("$Driven Animations:")) {
		animation::ModelAnimationParseHelper::parseAnimsetInfoDrivers(sip->animations, sip);
	}

	if (optional_string("$Animation Moveables:")) {
		animation::ModelAnimationParseHelper::parseMoveablesetInfo(sip->animations);
	}


	if (optional_string("$Custom data:")) 
	{
		parse_string_map(sip->custom_data, "$end_custom_data", "+Val:");
	}

	if (optional_string("$Custom Strings")) {
		while (optional_string("$Name:")) {
			custom_string cs;

			// The name of the string
			stuff_string(cs.name, F_NAME);

			// Arbitrary string value used for grouping strings together
			required_string("+Value:");
			stuff_string(cs.value, F_NAME);

			// The string text itself
			required_string("+String:");
			stuff_string(cs.text, F_MULTITEXT);

			sip->custom_strings.push_back(cs);
		}

		required_string("$end_custom_strings");
	}

	int n_subsystems = 0;
	int n_excess_subsystems = 0;
	int cont_flag = 1;
	model_subsystem subsystems[MAX_MODEL_SUBSYSTEMS] = {}; // see model.h for max_model_subsystems
	for (auto i=0; i<MAX_MODEL_SUBSYSTEMS; i++) {
		subsystems[i].reset();
	}
	
	while (cont_flag) {
		int r = required_string_one_of(3, "#End", "$Subsystem:", type_name);
		switch (r) {
		case 0:
			cont_flag = 0;
			break;
		case 1:
		{
			float	turning_rate;
			float	percentage_of_hits;
			bool turret_has_base_fov = false;
			bool turret_has_max_fov = false;
			bool turret_has_barrel_fov = false;
			model_subsystem *sp = NULL;			// to append on the ships list of subsystems
			
			int sfo_return;
			required_string("$Subsystem:");
			stuff_string(name_tmp, F_NAME, sizeof(name_tmp), ",");
			Mp++;
			for(auto i = 0;i < sip->n_subsystems; i++)
			{
				if(!subsystem_stricmp(sip->subsystems[i].subobj_name, name_tmp))
					sp = &sip->subsystems[i];
			}

			if(sp == NULL)
			{
				if( sip->n_subsystems + n_subsystems >= MAX_MODEL_SUBSYSTEMS )
				{
					n_excess_subsystems++;
					skip_to_start_of_string_one_of({"$Subsystem:", type_name, "#End"});
					break;
				}
				sp = &subsystems[n_subsystems++];			// subsystems a local -- when done, we will malloc and copy
				strcpy_s(sp->subobj_name, name_tmp);
				
				//Init blank values
				sp->max_subsys_strength = 0.0f;
				sp->turret_turning_rate = 0.0f;
				sp->weapon_rotation_pbank = -1;

                memset(sp->alt_sub_name, 0, sizeof(sp->alt_sub_name));
                memset(sp->alt_dmg_sub_name, 0, sizeof(sp->alt_dmg_sub_name));

				for (auto i=0; i<MAX_SHIP_PRIMARY_BANKS; i++) {
					sp->primary_banks[i] = -1;
					sp->primary_bank_capacity[i] = 0;
				}

				for (auto i=0; i<MAX_SHIP_SECONDARY_BANKS; i++) {
					sp->secondary_banks[i] = -1;
					sp->secondary_bank_capacity[i] = 0;
				}

				sp->engine_wash_pointer = NULL;
				
				sp->alive_snd = gamesnd_id();
				sp->dead_snd = gamesnd_id();
				sp->rotation_snd = gamesnd_id();
				sp->turret_gun_rotation_snd = gamesnd_id();
				sp->turret_gun_rotation_snd_mult = 1.0f;
				sp->turret_base_rotation_snd = gamesnd_id();
				sp->turret_base_rotation_snd_mult = 1.0f;
				
                sp->flags.reset();
				
				sp->model_num = -1;		// init value for later sanity checking!!
				sp->armor_type_idx = -1;
				sp->path_num = -1;
				sp->turret_max_fov = 1.0f;

				sp->awacs_intensity = 0.0f;
				sp->awacs_radius = 0.0f;
				sp->scan_time = -1;

				sp->turret_reset_delay = 2000;

				sp->num_target_priorities = 0;
				for (auto i = 0; i < 32; i++) {
					sp->target_priority[i] = -1;
				}
				sp->optimum_range = 0.0f;
				sp->favor_current_facing = 0.0f;

				sp->turret_rof_scaler = 1.0f;

				sp->turret_max_bomb_ownage = -1;
				sp->turret_max_target_ownage = -1;
				sp->density = 1.0f;
			}
			sfo_return = stuff_float_optional(&percentage_of_hits);
			if(sfo_return==2)
			{
				if (Calculate_subsystem_hitpoints_after_parsing)
					sp->max_subsys_strength = percentage_of_hits;
				else
					sp->max_subsys_strength = sip->max_hull_strength * (percentage_of_hits / 100.0f);

				sp->type = SUBSYSTEM_UNKNOWN;
			}
			if(sfo_return > 0)
			{
				if(stuff_float_optional(&turning_rate)==2)
				{
					// specified as how long to turn 360 degrees in ships.tbl
					if ( turning_rate > 0.0f ){
						sp->turret_turning_rate = PI2 / turning_rate;		
					} else {
						sp->turret_turning_rate = 0.0f;		
					}
				}
				else
				{
					Error(LOCATION, "Malformed $Subsystem entry '%s' in %s '%s'.\n\n"
						"Specify a turning rate or remove the trailing comma.",
						sp->subobj_name, info_type_name, sip->name);
				}
			}

			if(optional_string("$Alt Subsystem Name:")) {
				stuff_string(buf, F_NAME, SHIP_MULTITEXT_LENGTH);
				strcpy_s(sp->alt_sub_name,  buf);
			}

			if(optional_string("$Alt Damage Popup Subsystem Name:")) {
				stuff_string(buf, F_NAME, SHIP_MULTITEXT_LENGTH);
                strcpy_s(sp->alt_dmg_sub_name, buf);
			}

			if(optional_string("$Armor Type:")) {
				stuff_string(buf, F_NAME, SHIP_MULTITEXT_LENGTH);
				sp->armor_type_idx = armor_type_get_idx(buf);

				if (sp->armor_type_idx == -1)
					WarningEx(LOCATION, "%s '%s', subsystem %s\nInvalid armor type %s!", info_type_name, sip->name, sp->subobj_name, buf);
			}

			//	Get primary bank weapons
			parse_weapon_bank(sip, true, NULL, sp->primary_banks, sp->primary_bank_capacity);

			//	Get secondary bank weapons
			parse_weapon_bank(sip, false, NULL, sp->secondary_banks, sp->secondary_bank_capacity);

			// Get optional engine wake info
			if (optional_string("$Engine Wash:")) {
				stuff_string(name_tmp, F_NAME, sizeof(name_tmp));
				// get and set index
				sp->engine_wash_pointer = get_engine_wash_pointer(name_tmp);

				if(sp->engine_wash_pointer == NULL)
					WarningEx(LOCATION,"Invalid engine wash name %s specified for subsystem %s in %s '%s'", name_tmp, sp->subobj_name, info_type_name, sip->name);
			}

			parse_game_sound("$AliveSnd:", &sp->alive_snd);
			parse_game_sound("$DeadSnd:", &sp->dead_snd);
			parse_game_sound("$RotationSnd:", &sp->rotation_snd);
			parse_game_sound("$Turret Base RotationSnd:", &sp->turret_base_rotation_snd);
			parse_game_sound("$Turret Gun RotationSnd:", &sp->turret_gun_rotation_snd);

			if (optional_string("$Turret BaseSnd Volume:"))
				stuff_float(&sp->turret_base_rotation_snd_mult);

			if (optional_string("$Turret GunSnd Volume:"))
				stuff_float(&sp->turret_gun_rotation_snd_mult);
				
			// Get any AWACS info
			if(optional_string("$AWACS:")){
				sfo_return = stuff_float_optional(&sp->awacs_intensity);
				if(sfo_return > 0)
					stuff_float_optional(&sp->awacs_radius);
				sip->flags.set(Ship::Info_Flags::Has_awacs);
			}

			if(optional_string("$Scan time:")){
				stuff_int(&sp->scan_time);
			}

			if(optional_string("$Maximum Barrel Elevation:")){
				float value;
				stuff_float(&value);
				CAP(value, 0.0f, 90.0f);
				float angle = fl_radians(90.0f - value);
				sp->turret_max_fov = cosf(angle);
				turret_has_max_fov = true;
			}

			if(optional_string("$Turret Base FOV:")) {
				float value;
				stuff_float(&value);
				CAP(value, 0.0f, 360.0f);
				float angle = fl_radians(value)/2.0f;
				sp->turret_base_fov = cosf(angle);
				turret_has_base_fov = true;
			}

			if (optional_string("$Turret Barrel FOV:")) {
				float value;
				stuff_float(&value);
				CAP(value, 0.0f, 360.0f);
				float angle = fl_radians(value) / 2.0f;
				sp->turret_fov = cosf(angle);
				turret_has_barrel_fov = true;
			}

			if (optional_string("$Turret Reset Delay:"))
				stuff_int(&sp->turret_reset_delay);

			if (optional_string("$Turret Optimum Range:"))
				stuff_float(&sp->optimum_range);

			if (optional_string("$Turret Direction Preference:")) {
				int temp;
				stuff_int(&temp);
				if (temp == 0) {
					sp->favor_current_facing = 0.0f;
				} else {
					CAP(temp, 1, 100);
					sp->favor_current_facing = 1.0f + (((float) (100 - temp)) / 10.0f);
				}
			}

			if (optional_string("$Target Priority:")) {
				SCP_vector <SCP_string> tgt_priorities;
				stuff_string_list(tgt_priorities);
				sp->num_target_priorities = 0;

				if (tgt_priorities.size() > 32)
					tgt_priorities.resize(32);

				size_t num_groups = Ai_tp_list.size();

				for (size_t i = 0; i < tgt_priorities.size(); ++i) {
					size_t j;
					for(j = 0; j < num_groups; j++) {
						if ( !stricmp(Ai_tp_list[j].name, tgt_priorities[i].c_str()))  {
							sp->target_priority[i] = (int)j;
							sp->num_target_priorities++;
							break;
						}
					}
					if (j == num_groups) {
						Warning(LOCATION, "Unidentified target priority '%s' set for\nsubsystem '%s' in %s '%s'.", tgt_priorities[i].c_str(), sp->subobj_name, info_type_name, sip->name);
					}
				}
			}

			if (optional_string("$Max Turrets per Bomb:")) {
				stuff_int(&sp->turret_max_bomb_ownage);
			}

			if (optional_string("$Max Turrets per Target:")) {
				stuff_int(&sp->turret_max_target_ownage);
			}

			if (optional_string("$ROF:")) {

				if (optional_string("+Use firingpoints")) {
					sp->turret_rof_scaler = 0;
				} else {
					if (optional_string("+Multiplier:")) {
						float tempf;
						stuff_float(&tempf);

						if (tempf < 0) {
							mprintf(("RoF multiplier clamped to 0 for subsystem '%s' in %s '%s'.\n", sp->subobj_name, info_type_name, sip->name));
							sp->turret_rof_scaler = 0;
						} else {
							sp->turret_rof_scaler = tempf;
						}
					} else {
						Warning(LOCATION, "RoF multiplier not set for subsystem\n'%s' in %s '%s'.", sp->subobj_name, info_type_name, sip->name);
					}
				}
			}

			if (optional_string("$Debris Density:")) {
				stuff_float(&sp->density);
			}

			if (optional_string("$Flags:")) {
                SCP_vector<SCP_string> errors;
                flagset<Model::Subsystem_Flags> tmp_flags;
                parse_string_flag_list(tmp_flags, Subsystem_flags, Num_subsystem_flags, &errors);
                
                if (optional_string("+noreplace")) {
                    sp->flags |= tmp_flags;
                }
                else {
                    sp->flags = tmp_flags;
                }

                if (!errors.empty()) {
                    for (auto const &error : errors) {
                        Warning(LOCATION, "Bogus string in subsystem flags: %s\n", error.c_str());
                    }
                }

				//If we've set any subsystem as landable, set a ship-info flag as a shortcut for later
				if (sp->flags[Model::Subsystem_Flags::Allow_landing])
					sip->flags.set(Ship::Info_Flags::Allow_landings);
			}

			if (turret_has_barrel_fov)
				sp->flags.set(Model::Subsystem_Flags::Turret_barrel_fov_overridden);
			if (turret_has_base_fov)
				sp->flags.set(Model::Subsystem_Flags::Turret_base_fov_overridden);
			if (turret_has_max_fov)
				sp->flags.set(Model::Subsystem_Flags::Turret_max_fov_overridden);

			if (optional_string("+non-targetable")) {
				Warning(LOCATION, "Grammar error in table file.  Please change \"+non-targetable\" to \"+untargetable\".");
				sp->flags.set(Model::Subsystem_Flags::Untargetable);
			}

			bool old_flags = false;
			if (optional_string("+untargetable")) {
				sp->flags.set(Model::Subsystem_Flags::Untargetable);
				old_flags = true;
			}

			if (optional_string("+carry-no-damage")) {
				sp->flags.set(Model::Subsystem_Flags::Carry_no_damage); 
				old_flags = true;
			}

			if (optional_string("+use-multiple-guns")) {
				sp->flags.set(Model::Subsystem_Flags::Use_multiple_guns);
				old_flags = true;
			}

			if (optional_string("+fire-down-normals")) {
				sp->flags.set(Model::Subsystem_Flags::Fire_on_normal);
				old_flags = true;
			}

			if ((sp->flags[Model::Subsystem_Flags::Turret_fixed_fp]) && !(sp->flags[Model::Subsystem_Flags::Use_multiple_guns])) {
				Warning(LOCATION, "\"fixed firingpoints\" flag used without \"use multiple guns\" flag on a subsystem on %s '%s'.\n\"use multiple guns\" flags added by default\n", info_type_name, sip->name);
				sp->flags.set(Model::Subsystem_Flags::Use_multiple_guns);
			}

			if ((sp->flags[Model::Subsystem_Flags::Autorepair_if_disabled]) && (sp->flags[Model::Subsystem_Flags::No_autorepair_if_disabled])) {
				Warning(LOCATION, "\"autorepair if disabled\" flag used with \"don't autorepair if disabled\" flag on a subsystem on %s '%s'.\nWhichever flag would be default behavior anyway for this ship has been removed.\n", info_type_name, sip->name);
				if (sip->flags[Ship::Info_Flags::Subsys_repair_when_disabled]){
                    sp->flags.remove(Model::Subsystem_Flags::Autorepair_if_disabled);
				} else {
                    sp->flags.remove(Model::Subsystem_Flags::No_autorepair_if_disabled);
				}
			}

			if (old_flags) {
				mprintf(("Use of deprecated subsystem syntax.  Please use the $Flags: field for subsystem flags.\n\n" \
				"At least one of the following tags was used on %s '%s', subsystem %s:\n" \
				"\t+untargetable\n" \
				"\t+carry-no-damage\n" \
				"\t+use-multiple-guns\n" \
				"\t+fire-down-normals\n", info_type_name, sip->name, sp->subobj_name));
			}

			while(optional_string("$animation:"))
			{
				stuff_string(name_tmp, F_NAME, sizeof(name_tmp));
				if(!stricmp(name_tmp, "triggered"))
				{
					animation::ModelAnimationParseHelper::parseLegacyAnimationTable(sp, sip);
				}
				else if(!stricmp(name_tmp, "linked"))
				{
					mprintf(("TODO: set up linked animation\n"));
				}
			}

			sp->beam_warmdown_program = actions::ProgramSet::parseProgramSet("$On Beam Warmdown:",
				{actions::ProgramContextFlags::HasObject, actions::ProgramContextFlags::HasSubobject});
		}
		break;
		case 2:
			cont_flag = 0;
			break;
		case -1:	// Possible return value if -noparseerrors is used
			break;
		default:
			UNREACHABLE("This should never happen.\n");	// Impossible return value from required_string_one_of.
		}
	}

	// maybe warn
	if (n_excess_subsystems > 0) {
		Warning(LOCATION, "For %s '%s', number of existing subsystems (%d) plus additional subsystems (%d) exceeds max of %d; only the first %d additional subsystems will be used", info_type_name, sip->name, sip->n_subsystems, n_subsystems + n_excess_subsystems, MAX_MODEL_SUBSYSTEMS, n_subsystems);
	}

	// when done reading subsystems, malloc and copy the subsystem data to the ship info structure
	int orig_n_subsystems = sip->n_subsystems;
	if ( n_subsystems > 0 ) {
		// Let's make sure that n_subsystems is not negative
		Assertion(sip->n_subsystems >= 0, "Invalid n_subsystems detected!");
		auto new_n = sip->n_subsystems + n_subsystems;

		std::unique_ptr<model_subsystem[]> subsys_storage(new model_subsystem[MAX(0,new_n)]);

		if(sip->n_subsystems <= 0) {
			sip->n_subsystems = n_subsystems;
		} else {
			// This used realloc originally so we need to copy the existing subsystems to the new storage
			std::copy(sip->subsystems, sip->subsystems + sip->n_subsystems, subsys_storage.get());

			// done with old storage
			delete[] sip->subsystems;

			sip->n_subsystems += n_subsystems;
		}
		sip->subsystems = subsys_storage.release();

	    Assert(sip->subsystems != NULL);
    }
		
	for ( int i = 0; i < n_subsystems; i++ ){
		sip->subsystems[orig_n_subsystems+i] = subsystems[i];
	}
}

static engine_wash_info *get_engine_wash_pointer(char *engine_wash_name)
{
	for(int i = 0; i < Num_engine_wash_types; i++)
	{
		if(!stricmp(engine_wash_name, Engine_wash_info[i].name))
		{
			return &Engine_wash_info[i];
		}
	}

	//Didn't find anything.
	return NULL;
}

static void parse_ship_type(const char *filename, const bool replace)
{
	char name_buf[NAME_LENGTH];
	bool create_if_not_found = true;
	ship_type_info *stp = nullptr;

	required_string("$Name:");
	stuff_string(name_buf, F_NAME, NAME_LENGTH);

	if(optional_string("+nocreate")) {
		if(!replace) {
			Warning(LOCATION, "+nocreate flag used for ship type '%s' in non-modular table", name_buf);
		}
		create_if_not_found = false;
	}

	bool first_time;
	int idx = ship_type_name_lookup(name_buf);
	if (idx >= 0)
	{
		if (!replace)
		{
			Warning(LOCATION, "ship type '%s' already exists in %s; ship type names must be unique.", name_buf, filename);
			if (!skip_to_start_of_string_either("$Name:", "#End")) {
				error_display(1, "Missing [#End] or [$Name] after duplicate ship type %s", name_buf);
			}
			return;
		}
		stp = &Ship_types[idx];
		first_time = false;
	}
	else
	{
		if (!create_if_not_found && replace)
		{
			if (!skip_to_start_of_string_either("$Name:", "#End")) {
				error_display(1, "Missing [#End] or [$Name] after ship type %s", name_buf);
			}
			return;
		}
		Ship_types.push_back(ship_type_info());
		stp = &Ship_types.back();
		strcpy_s(stp->name, name_buf);
		first_time = true;
	}

	const char *ship_type = NULL;
	if (!stricmp(stp->name, "sentrygun")) {
		ship_type = "sentry gun";
	} else if (!stricmp(stp->name, "escapepod")) {
		ship_type = "escape pod";
	} else if (!stricmp(stp->name, "repair_rearm")) {
		ship_type = "support";
	} else if (!stricmp(stp->name, "supercap")) {
		ship_type = "super cap";
	} else if (!stricmp(stp->name, "knossos")) {
		ship_type = "knossos device";
	}

	if (ship_type != NULL) {
		Warning(LOCATION, "Bad ship type name in %s\n\nUsed ship type is redirected to another ship type.\nReplace \"%s\" with \"%s\"\nin %s to fix this.\n", filename, stp->name, ship_type, filename);
	}

	bool big_ship = false;
	bool huge_ship = false;
	if (stricmp(stp->name, "cruiser") == 0 || stricmp(stp->name, "freighter") == 0 || stricmp(stp->name, "transport") == 0 ||
		stricmp(stp->name, "corvette") == 0 || stricmp(stp->name, "gas miner") == 0 || stricmp(stp->name, "awacs") == 0)
		big_ship = true;

	if (stricmp(stp->name, "capital") == 0 || stricmp(stp->name, "super cap") == 0 || stricmp(stp->name, "knossos device") == 0 || stricmp(stp->name, "drydock") == 0)
		huge_ship = true;

	//Okay, now we should have the values to parse
	//But they aren't here!! :O
	//Now they are!! Whee fogging!!

	//AI turret targeting priority setup
	if (optional_string("$Target Priority Groups:") ) {
		SCP_vector <SCP_string> target_group_strings;
		stuff_string_list(target_group_strings);
		auto num_strings = target_group_strings.size();
		auto num_groups = Ai_tp_list.size();
		bool override_strings = false;

		if (optional_string("+Override")) {
			override_strings = true;
		}

		for(size_t j = 0; j < num_strings; j++) {
			size_t i;
			for(i = 0; i < num_groups; i++) {
				if ( !stricmp(target_group_strings[j].c_str(), Ai_tp_list[i].name) ) {
					//so now the string from the list above as well as the ai priority group name match
					//clear it if override has been set
					if (override_strings) {
						Ai_tp_list[i].ship_type.clear();
						override_strings = false;
					}
					//find the index number of the current ship info type
					Ai_tp_list[i].ship_type.push_back(ship_type_name_lookup(name_buf));
					break;
				}
			}
			if (i == num_groups) {
				Warning(LOCATION,"Unidentified priority group '%s' set for ship type '%s' in %s\n", target_group_strings[j].c_str(), stp->name, filename);
			}
		}
	}

	if(optional_string("$Counts for Alone:")) {
		stuff_boolean_flag(stp->flags, Ship::Type_Info_Flags::Counts_for_alone);
	}

	if(optional_string("$Praise Destruction:")) {
        stuff_boolean_flag(stp->flags, Ship::Type_Info_Flags::Praise_destruction);
	}

	if(optional_string("$On Hotkey list:")) {
		stuff_boolean_flag(stp->flags, Ship::Type_Info_Flags::Hotkey_on_list);
	}

	if(optional_string("$Target as Threat:")) {
		stuff_boolean_flag(stp->flags, Ship::Type_Info_Flags::Target_as_threat);
	}

	if(optional_string("$Show Attack Direction:")) {
		stuff_boolean_flag(stp->flags, Ship::Type_Info_Flags::Show_attack_direction);
	}

	if(optional_string("$Scannable:")) {
		stuff_boolean_flag(stp->flags, Ship::Type_Info_Flags::Scannable);
	}

	if(optional_string("$Warp Pushes:")) {
		stuff_boolean_flag(stp->flags, Ship::Type_Info_Flags::Warp_pushes);
	}

	if(optional_string("$Warp Pushable:")) {
		stuff_boolean_flag(stp->flags, Ship::Type_Info_Flags::Warp_pushable);
	}

	if(optional_string("$Turrets prioritize ship target:")) {
		stuff_boolean_flag(stp->flags, Ship::Type_Info_Flags::Turret_tgt_ship_tgt);
	}

	if(optional_string("$Max Debris Speed:")) {
		stuff_float(&stp->debris_max_speed);
	}

	if(optional_string("$FF Multiplier:")) {
		stuff_float(&stp->ff_multiplier);
	}

	if(optional_string("$EMP Multiplier:")) {
		stuff_float(&stp->emp_multiplier);
	}

	if(optional_string("$Warp Sound Range Multiplier:")) {
		stuff_float(&stp->warp_sound_range_multiplier);
	}

	if(optional_string("$Beams Easily Hit:")) {
		stuff_boolean_flag(stp->flags, Ship::Type_Info_Flags::Beams_easily_hit);
	}

	if(optional_string("$Protected on cripple:")) {
		stuff_boolean_flag(stp->flags, Ship::Type_Info_Flags::AI_protected_on_cripple);
	}

	if(optional_string("$No Huge Beam Impact Effects:")) {
		stuff_boolean_flag(stp->flags, Ship::Type_Info_Flags::No_huge_impact_eff);
	}

	if(optional_string("$Don't display class in briefing:")) {
		stuff_boolean_flag(stp->flags, Ship::Type_Info_Flags::No_class_display);
	}

	if(optional_string("$Fog:"))
	{
		if(optional_string("+Start dist:")) {
			stuff_float(&stp->fog_start_dist);
		}

		if(optional_string("+Compl dist:")) {
			stuff_float(&stp->fog_complete_dist);
		}
	}

	if(optional_string("$AI:"))
	{
		if(optional_string("+Valid goals:")) {
			parse_string_flag_list(stp->ai_valid_goals, Ai_goal_names, Num_ai_goals);
		}

		if(optional_string("+Accept Player Orders:")) {
			stuff_boolean_flag(stp->flags, Ship::Type_Info_Flags::AI_accept_player_orders);
		}

		if(optional_string("+Player Orders:")) {
			SCP_vector<SCP_string> slp;
			stuff_string_list(slp);
			
			for(size_t i = 0; i < Player_orders.size(); i++){
				if(std::find(slp.begin(), slp.end(), Player_orders[i].parse_name) != slp.end()){
					stp->ai_player_orders.insert(i);
				}
			}
		}

		if(optional_string("+Auto attacks:")) {
            stuff_boolean_flag(stp->flags, Ship::Type_Info_Flags::AI_auto_attacks);
		}

		if(optional_string("+Attempt broadside:")) {
            stuff_boolean_flag(stp->flags, Ship::Type_Info_Flags::AI_attempt_broadside);
		}

		if(optional_string("+Actively Pursues:")) {
			stuff_string_list(stp->ai_actively_pursues_temp);
		}

		if(optional_string("+Guards attack this:")) {
            stuff_boolean_flag(stp->flags, Ship::Type_Info_Flags::AI_guards_attack);
		}

		if(optional_string("+Turrets attack this:")) {
            stuff_boolean_flag(stp->flags, Ship::Type_Info_Flags::AI_turrets_attack);
		}

		if(optional_string("+Can form wing:")) {
            stuff_boolean_flag(stp->flags, Ship::Type_Info_Flags::AI_can_form_wing);
		}

		if(optional_string("+Active docks:")) {
			parse_string_flag_list(stp->ai_active_dock, Dock_type_names, Num_dock_type_names);
		}

		if(optional_string("+Passive docks:")) {
			parse_string_flag_list(stp->ai_passive_dock, Dock_type_names, Num_dock_type_names);
		}

		if(optional_string("+Ignored on cripple by:")) {
			stuff_string_list(stp->ai_cripple_ignores_temp); 
		}

		if (optional_string("+Targeted by 'Huge' weapons and Ignored by 'small only' weapons:")) {
			stuff_boolean_flag(stp->flags, Ship::Type_Info_Flags::Targeted_by_huge_Ignored_by_small_only);
		} else if (first_time && (big_ship || huge_ship)) {
			stp->flags.set(Ship::Type_Info_Flags::Targeted_by_huge_Ignored_by_small_only);
		}
	}

	if(optional_string("$Explosion Animations:"))
	{
		stuff_fireball_index_list(stp->explosion_bitmap_anims, stp->name);
	}

	auto skip_str = "$Skip Death Roll Percent Chance:";
	auto vaporize_str = "$Vaporize Percent Chance:";
	int which;
	if ((which = optional_string_either(skip_str, vaporize_str)) >= 0) {
		if (which == 1) {
			mprintf(("Found %s for ship type '%s'.  This has been deprecated; use %s instead.\n", vaporize_str, stp->name, skip_str));
		}
		stuff_float(&stp->skip_deathroll_chance);
		if (stp->skip_deathroll_chance < 0.0f || stp->skip_deathroll_chance > 100.0f) {
			Warning(LOCATION, "%s should be between 0 and 100.0 (read %f) for ship type '%s'. Setting to 0.", which == 0 ? skip_str : vaporize_str, stp->skip_deathroll_chance, stp->name);
			stp->skip_deathroll_chance = 0.0f;
		}
		//Percent is nice for modders, but here in the code we want it between 0 and 1.0
		stp->skip_deathroll_chance /= 100.0f;
	}
}

static void parse_shiptype_tbl(const char *filename)
{
	try
	{
		if (filename != NULL)
			read_file_text(filename, CF_TYPE_TABLES);
		else
			read_file_text_from_default(defaults_get_file("objecttypes.tbl"));

		reset_parse();

		if (optional_string("#Target Priorities"))
		{
			while (required_string_either("#End", "$Name:"))
				parse_ai_target_priorities();

			required_string("#End");
		}

		if (optional_string("#Weapon Targeting Priorities"))
		{
			while (required_string_either("#End", "$Name:"))
				parse_weapon_targeting_priorities();

			required_string("#End");
		}

		if (optional_string("#Ship Types"))
		{
			while (required_string_either("#End", "$Name:"))
				parse_ship_type(filename ? filename : "built-in objecttypes.tbl", Parsing_modular_table);

			required_string("#End");
		}
	}
	catch (const parse::ParseException& e)
	{
		mprintf(("TABLES: Unable to parse '%s'!  Error message = %s.\n", filename, e.what()));
		return;
	}
}

// The E - Simple lookup function for FRED.
int get_default_player_ship_index() 
{
	if (strlen(default_player_ship)) 
	{
		for (auto it = Ship_info.cbegin(); it != Ship_info.cend(); ++it) 
		{
			if (stricmp(default_player_ship, it->name) == 0)
				return (int)std::distance(Ship_info.cbegin(), it);
		}
		return 0;
	} else
		return 0;
}

// Goober5000 - this works better in its own function
static void ship_set_default_player_ship()
{
	// already have one
	if(strlen(default_player_ship))
		return;

	// find the first with the default flag
	for (auto it = Ship_info.cbegin(); it != Ship_info.end(); ++it)
	{
		if(it->flags[Ship::Info_Flags::Default_player_ship])
		{
			strcpy_s(default_player_ship, it->name);
			return;
		}
	}

	// find the first player ship
	for (auto it = Ship_info.cbegin(); it != Ship_info.end(); ++it)
	{
		if(it->flags[Ship::Info_Flags::Player_ship])
		{
			strcpy_s(default_player_ship, it->name);
			return;
		}
	}

	// find the first ship
	if(!Ship_info.empty())
	{
		strcpy_s(default_player_ship, Ship_info[0].name);
	}
}

static void parse_shiptbl(const char *filename)
{
	try
	{
		read_file_text(filename, CF_TYPE_TABLES);
		reset_parse();

		// parse default ship
		//Override default player ship
		if (optional_string("#Default Player Ship"))
		{
			required_string("$Name:");
			stuff_string(default_player_ship, F_NAME, sizeof(default_player_ship));
			required_string("#End");
		}
		//Add engine washes
		//This will override if they already exist
		if (optional_string("#Engine Wash Info"))
		{
			while (required_string_either("#End", "$Name:"))
				parse_engine_wash(Parsing_modular_table);

			required_string("#End");
		}

		if (optional_string("#Ship Templates"))
		{
			while (required_string_either("#End", "$Template:"))
				parse_ship_template();

			required_string("#End");
		}

		//Add ship classes
		if (optional_string("#Ship Classes"))
		{
			while (required_string_either("#End", "$Name:"))
				parse_ship(filename, Parsing_modular_table);

			required_string("#End");
		}

		//Add formations
		if (optional_string("#Wing Formations"))
		{
			while (required_string_either("#End", "$Name:"))
				parse_wing_formation(Parsing_modular_table);

			required_string("#End");
		}

		//Set default player ship
		ship_set_default_player_ship();
	}
	catch (const parse::ParseException& e)
	{
		mprintf(("TABLES: Unable to parse '%s'!  Error message = %s.\n", filename, e.what()));
		return;
	}
}

int ship_show_velocity_dot = 0;


DCF_BOOL( show_velocity_dot, ship_show_velocity_dot )

static bool ballistic_possible_for_this_ship(const ship_info *sip)
{
	// has no weapons!
	if (sip->num_primary_banks < 1)
		return false;

	for (int i = 0; i < MAX_SHIP_PRIMARY_BANKS; i++)
	{
		// check default weapons
		if (sip->primary_bank_weapons[i] >= 0 && Weapon_info[sip->primary_bank_weapons[i]].wi_flags[Weapon::Info_Flags::Ballistic])
			return true;

		// check allowed weapons
		if (!sip->allowed_bank_restricted_weapons.empty()) {
			for (int j = 0; j < weapon_info_size(); ++j)
			{
				if (sip->allowed_bank_restricted_weapons[i][j] && (Weapon_info[j].wi_flags[Weapon::Info_Flags::Ballistic]))
					return true;
			}
		}
	}

	return false;
}

/**
 * Clean up ship entries, making sure various flags and settings are correct
 */
static void ship_parse_post_cleanup()
{
	int j;
	char name_tmp[NAME_LENGTH];

	for (auto sip = Ship_info.begin(); sip != Ship_info.end(); ++sip)
	{
		// ballistic primary fixage...
		{
			bool pbank_capacity_specified = false;

			// determine whether this ship had primary capacities specified for it
			for (j = 0; j < sip->num_primary_banks; j++) {
				if (sip->primary_bank_ammo_capacity[j] > 0) {
					pbank_capacity_specified = true;
					break;
				}
			}

			// be friendly; ensure ballistic flags check out
			if (!pbank_capacity_specified) {
				if ( ballistic_possible_for_this_ship(&(*sip)) ) {
					Warning(LOCATION, "Pbank capacity not specified for ballistic-primary-enabled ship %s.\nDefaulting to capacity of 1 per bank.\n", sip->name);

					for (j = 0; j < MAX_SHIP_PRIMARY_BANKS; j++) {
						sip->primary_bank_ammo_capacity[j] = 1;
					}
				}
			}
		}

		// ultra stupid compatbility handling for the once broken "generate hud" flag.
		// it was previously testing the afterburner flag, so that's what we check for that
		if ( (sip->shield_icon_index == 255) && (sip->flags[Ship::Info_Flags::Afterburner])
				&& !(sip->flags[Ship::Info_Flags::Generate_hud_icon]) && (sip->flags[Ship::Info_Flags::Player_ship]) )
		{
			Warning(LOCATION, "Compatibility warning:\nNo shield icon specified for '%s' but the \"generate icon\" flag is not specified.\nEnabling flag by default.\n", sip->name);
			sip->flags.set(Ship::Info_Flags::Generate_hud_icon);
		}

		// if we have a ship copy, then check to be sure that our base ship exists
		if (sip->flags[Ship::Info_Flags::Ship_copy])
		{
			strcpy_s(name_tmp, sip->name);

			if (end_string_at_first_hash_symbol(name_tmp))
			{
				if (ship_info_lookup(name_tmp) < 0)
				{
					Warning(LOCATION, "Ship %s is a copy, but base ship %s couldn't be found.", sip->name, name_tmp);
					sip->flags.remove(Ship::Info_Flags::Ship_copy);
				}
			}
			else
			{
				Warning(LOCATION, "Ship %s is defined as a copy (ship flag 'ship copy' is set), but is not named like one (no '#').\n", sip->name);
				sip->flags.remove(Ship::Info_Flags::Ship_copy);
			}
		}

		// very low rotational velocity values disable rotational collisions
		// warn early rather than ambush the modder @ runtime (unless the ship is also no-collide!)
		// the 2nd part of this check is duplicated from collideshipship.cpp:ship_ship_check_collision()
		if (!(sip->flags[Ship::Info_Flags::No_collide]) && (vm_vec_mag_squared( &sip->max_rotvel ) * .04) >= (PI*PI/4))
		{
			Warning(LOCATION, "$Rotation time: too low; this will disable rotational collisions. All three variables should be >= 1.39.\nFix this in ship '%s'\n", sip->name);
		}

		// ensure player min velocity makes sense
		if (sip->min_vel.xyz.x > sip->max_vel.xyz.x || sip->min_vel.xyz.x < 0.0f)
		{
			error_display(0, "$Player Minimum Velocity X-value (%f) is negative or greater than max velocity X-value (%f), setting to zero\nFix for ship '%s'\n",
					sip->min_vel.xyz.x, sip->max_vel.xyz.x, sip->name);
			sip->min_vel.xyz.x = 0.0f;
		}
		if (sip->min_vel.xyz.y > sip->max_vel.xyz.y || sip->min_vel.xyz.y < 0.0f)
		{
			error_display(0, "$Player Minimum Velocity Y-value (%f) is negative or greater than max velocity Y-value (%f), setting to zero\nFix for ship '%s'\n",
					sip->min_vel.xyz.y, sip->max_vel.xyz.y, sip->name);
			sip->min_vel.xyz.y = 0.0f;
		}
		if (sip->min_vel.xyz.z > sip->max_vel.xyz.z || sip->min_vel.xyz.z < 0.0f)
		{
			error_display(0, "$Player Minimum Velocity Z-value (%f) is negative or greater than max velocity Z-value (%f), setting to zero\nFix for ship '%s'\n",
					sip->min_vel.xyz.z, sip->max_vel.xyz.z, sip->name);
			sip->min_vel.xyz.z = 0.0f;
		}

		// we might need to calculate subsystem strength
		if (Calculate_subsystem_hitpoints_after_parsing)
		{
			for (int i = 0; i < sip->n_subsystems; ++i)
			{
				auto sp = &sip->subsystems[i];
				sp->max_subsys_strength = sip->max_hull_strength * (sp->max_subsys_strength / 100.0f);
			}
		}
	}

	// check also target groups here
	size_t n_tgt_groups = Ai_tp_list.size();

	if (n_tgt_groups > 0) {
		for(size_t i = 0; i < n_tgt_groups; i++) {
			if (!(Ai_tp_list[i].obj_flags.any_set() || Ai_tp_list[i].sif_flags.any_set() || Ai_tp_list[i].wif_flags.any_set())) {
				//had none of these, check next
				if (Ai_tp_list[i].obj_type == -1) {
					//didn't have this one
					if (!(Ai_tp_list[i].ship_class.size() || Ai_tp_list[i].ship_type.size() || Ai_tp_list[i].weapon_class.size())) {
						// had nothing - time to issue a warning
						Warning(LOCATION, "Target priority group '%s' had no targeting rules issued for it.\n", Ai_tp_list[i].name);
					}
				}
			}
		}
	}

	// Clear out ship templates, since they're no longer needed. -MageKing17
	Ship_templates.clear();
}

/**
 * Called once at the beginning of the game to parse ships.tbl and stuff the ::Ship_info
 * vector
 */
void ship_init()
{
	int idx;

	if ( !Ships_inited )
	{
		//Initialize Ignore_List for targeting
		set_default_ignore_list();

		//Parse main TBL first
		if (cf_exists_full("objecttypes.tbl", CF_TYPE_TABLES))
			parse_shiptype_tbl("objecttypes.tbl");
		else
			parse_shiptype_tbl(NULL);

		//Then other ones
		parse_modular_table(NOX("*-obt.tbm"), parse_shiptype_tbl);

		// Remove the "stealth" ship type if it exists, since it was never supposed to be an actual ship type, and it conflicts with ship class flags (Github #6366)
		idx = ship_type_name_lookup("stealth");
		if (idx >= 0)
		{
			Warning(LOCATION, "A ship type \"stealth\" was found in objecttypes.tbl or *-obt.tbm.  This ship type will be removed since it will conflict with the \"stealth\" ship class flag.  Please update your mod files.");
			Ship_types.erase(Ship_types.begin() + idx);
		}

		// See whether this mod defines an explicit "fighter/bomber" type (which would take priority over the special usage), and if not, whether the component types exist
		if (ship_type_name_lookup(Fighter_bomber_type_name) < 0)
		{
			Ship_type_fighter_bomber = static_cast<int>(Ship_types.size()) + 1;	// This is a convenient way to indicate that we want the fighter/bomber special case; it is not an actual valid Ship_types index
			Ship_type_fighter = ship_type_name_lookup("fighter");
			Ship_type_bomber = ship_type_name_lookup("bomber");
			if (Ship_type_fighter >= 0 && Ship_type_bomber >= 0)
				Fighter_bomber_valid = true;
		}

		// DO ALL THE STUFF WE NEED TO DO AFTER LOADING Ship_types
		ship_type_info *stp;

		uint i,j;
		for(i = 0; i < Ship_types.size(); i++)
		{
			stp = &Ship_types[i];

			//Handle active pursuit
			for(j = 0; j < stp->ai_actively_pursues_temp.size(); j++)
			{
				idx = ship_type_name_lookup((char*)stp->ai_actively_pursues_temp[j].c_str());
				if(idx >= 0) {
					stp->ai_actively_pursues.push_back(idx);
				}
			}
			stp->ai_actively_pursues_temp.clear();

			//Handle disabled/disarmed behaviour
			for(j = 0; j < stp->ai_cripple_ignores_temp.size(); j++) {
				idx = ship_type_name_lookup((char*)stp->ai_cripple_ignores_temp[j].c_str());
				if(idx >= 0) {
					stp->ai_cripple_ignores.push_back(idx);
				}
			}
			stp->ai_cripple_ignores_temp.clear();
		}

		//ships.tbl
		{			
			Num_engine_wash_types = 0;
			strcpy_s(default_player_ship, "");

			//Parse main TBL first
			Removed_ships.clear();
			Ship_info.clear();
			Hud_parsed_ships.clear();
			parse_shiptbl("ships.tbl");

			//Then other ones
			parse_modular_table(NOX("*-shp.tbm"), parse_shiptbl);

			ship_parse_post_cleanup();

			Ships_inited = true;
		}

		for (SCP_vector<species_info>::iterator ii = Species_info.begin(); ii != Species_info.end(); ++ii) {
			if (*ii->support_ship_name) {
				int index = ship_info_lookup(ii->support_ship_name);
				if (index < 0) {
					Warning(LOCATION, "Could not find support ship class '%s' for species species '%s'", ii->support_ship_name, ii->species_name);
				} else {
					ii->support_ship_index = index;
				}
			} else {
				// No support ship explicitly given
				int species_index = (int) std::distance(Species_info.begin(), ii);
				for (auto it = Ship_info.cbegin(); it != Ship_info.cend(); ++it) {
					if ((it->species == species_index) && (it->flags[Ship::Info_Flags::Support])) {
						int ship_index = (int) std::distance(Ship_info.cbegin(), it);
						ii->support_ship_index = ship_index;
						break;
					}
				}
			}
		}

		// We shouldn't already have any subsystem pointers at this point.
		Assertion(Ship_subsystems.empty(), "Some pre-allocated subsystems didn't get cleared out: " SIZE_T_ARG " batches present during ship_init(); get a coder!\n", Ship_subsystems.size());
	}
}

static int Man_thruster_reset_timestamp = 0;

static void ship_clear_subsystems()
{
	for (auto it = Ship_subsystems.begin(); it != Ship_subsystems.end(); ++it) {
		delete[] *it;
	}
	Ship_subsystems.clear();

	Num_ship_subsystems = 0;
	Num_ship_subsystems_allocated = 0;
}

static int ship_allocate_subsystems(int num_so, bool page_in = false)
{
	int i;
	int num_subsystems_save = 0;

	// "0" itself is safe
	if (num_so < 0) {
		Int3();
		return 0;
	}

	// allow a page-in thingy, so that we can grab as much as possible before mission
	// start, but without messing up our count for future things
	if (page_in)
		num_subsystems_save = Num_ship_subsystems;

	Num_ship_subsystems += num_so;

	// bail if we don't actually need any more
	if ( Num_ship_subsystems < Num_ship_subsystems_allocated )
		return 1;

	mprintf(("Allocating space for at least %i new ship subsystems ... ", num_so));

	// we might need more than one set worth of new subsystems, so make as many as required
	do {
		ship_subsys* new_batch = new ship_subsys[NUM_SHIP_SUBSYSTEMS_PER_SET];
		Ship_subsystems.push_back(new_batch);

		// append the new set to our free list
		for (i = 0; i < NUM_SHIP_SUBSYSTEMS_PER_SET; i++)
			list_append( &ship_subsys_free_list, &new_batch[i] );

		Num_ship_subsystems_allocated += NUM_SHIP_SUBSYSTEMS_PER_SET;
	} while ( (Num_ship_subsystems - Num_ship_subsystems_allocated) > 0 );

	if (page_in)
		Num_ship_subsystems = num_subsystems_save;

	// because the Ship_subsystems vector changed, it might have moved in memory, so invalidate all the subsystem caches
	for (auto so : list_range(&Ship_obj_list))
	{
		// don't skip should-be-dead ships for this flag removal
		Ships[Objects[so->objnum].instance].flags.remove(Ship::Ship_Flags::Subsystem_cache_valid);
	}

	mprintf(("a total of %i is now available (%i in-use).\n", Num_ship_subsystems_allocated, Num_ship_subsystems));
	return 1;
}

/**
 * This will get called at the start of each level.
 */
void ship_level_init()
{
	int i;

	// Reset everything between levels
	Ships_exited.clear(); 
	Ships_exited.reserve(100);
	for (i=0; i<MAX_SHIPS; i++ )
	{
		Ships[i].ship_name[0] = '\0';
		Ships[i].objnum = -1;
	}

	Num_wings = 0;
	for (i = 0; i < MAX_WINGS; i++ )
		Wings[i].clear();

	for (i=0; i<MAX_STARTING_WINGS; i++)
		Starting_wings[i] = -1;

	for (i=0; i<MAX_SQUADRON_WINGS; i++)
		Squadron_wings[i] = -1;

	for (i=0; i<MAX_TVT_WINGS; i++)
		TVT_wings[i] = -1;

	// Goober5000

	// set starting wing names to default
	strcpy_s(Starting_wing_names[0], "Alpha");
	strcpy_s(Starting_wing_names[1], "Beta");
	strcpy_s(Starting_wing_names[2], "Gamma");

	// set squadron wing names to default
	strcpy_s(Squadron_wing_names[0], "Alpha");
	strcpy_s(Squadron_wing_names[1], "Beta");
	strcpy_s(Squadron_wing_names[2], "Gamma");
	strcpy_s(Squadron_wing_names[3], "Delta");
	strcpy_s(Squadron_wing_names[4], "Epsilon");

	// set tvt wing names to default
	strcpy_s(TVT_wing_names[0], "Alpha");
	strcpy_s(TVT_wing_names[1], "Zeta");

	// clear out ship registry
	Ship_registry.clear();
	Ship_registry_map.clear();


	// Empty the subsys list
	ship_clear_subsystems();
	list_init( &ship_subsys_free_list );

	Laser_energy_out_snd_timer = 1;
	Missile_out_snd_timer		= 1;

	ship_obj_list_init();

	Ship_cargo_check_timer = 1;

	shipfx_large_blowup_level_init();

	Man_thruster_reset_timestamp = timestamp(0);
}

/**
 * Add a ship onto the exited ships list.
 *
 * The reason parameter tells us why the ship left the mission (i.e. departed or destroyed)
 */
void ship_add_exited_ship( ship *sp, Ship::Exit_Flags reason )
{
	exited_ship entry; 

	strcpy_s(entry.ship_name, sp->ship_name );
	entry.display_name = sp->get_display_name();
	entry.obj_signature = Objects[sp->objnum].signature;
	entry.ship_class = sp->ship_info_index;
	entry.team = sp->team;
	entry.wingnum = sp->wingnum;
	entry.flags += reason;
	// copy some flags
	if (sp->flags[Ship_Flags::Red_alert_store_status]) {
        entry.flags.set(Ship::Exit_Flags::Red_alert_carry);
	}
	if (sp->flags[Ship_Flags::From_player_wing]) {
		entry.flags.set(Ship::Exit_Flags::From_player_wing);
	}
	entry.time = Missiontime;
	entry.hull_strength = int(Objects[sp->objnum].hull_strength);

	entry.cargo1 = sp->cargo1;

	entry.time_cargo_revealed = (fix)0;
	if ( sp->flags[Ship_Flags::Cargo_revealed] )
	{
        entry.flags.set(Ship::Exit_Flags::Cargo_known);
		entry.time_cargo_revealed = sp->time_cargo_revealed;
	}

    if (sp->time_first_tagged > 0) {
		entry.flags.set(Ship::Exit_Flags::Been_tagged);
	}
	
	//copy across the damage_ship arrays
	for (int i = 0; i < MAX_DAMAGE_SLOTS ; i++) {
		entry.damage_ship_id[i] = sp->damage_ship_id[i] ;
		entry.damage_ship[i] = sp->damage_ship[i] ;
	}
	
	// record this in the ship registry
	auto ship_it = Ship_registry_map.find(sp->ship_name);
	if (ship_it != Ship_registry_map.end())
		Ship_registry[ship_it->second].exited_index = static_cast<int>(Ships_exited.size());

	Ships_exited.push_back(entry);
}

/**
 * Attempt to find information about an exited ship based on shipname
 */
int ship_find_exited_ship_by_name( const char *name )
{
	int i;

	for (i = 0; i < (int)Ships_exited.size(); i++) {
		if ( !stricmp(name, Ships_exited[i].ship_name) )
			return i;
	}

	return -1;
}

/**
 * Attempt to find information about an exited ship based on signature
 */
int ship_find_exited_ship_by_signature( int signature )
{
	int i;

	for (i = 0; i < (int)Ships_exited.size(); i++) {
		if ( signature == Ships_exited[i].obj_signature )
			return i;
	}

	return -1;
}


void physics_ship_init(object *objp)
{
	ship_info	*sinfo = &Ship_info[Ships[objp->instance].ship_info_index];
	physics_info	*pi = &objp->phys_info;
	polymodel *pm = model_get(sinfo->model_num);

	// use mass and I_body_inv from POF read into polymodel
	physics_init(pi);

	if (sinfo->density == 0) {
		sinfo->density = 1;
		nprintf(("Physics", "pi->density==0.0f. setting to 1\n"));
		Warning(LOCATION, "%s has 0 density! setting to 1", sinfo->name);
	}

	if (pm->mass==0.0f)
	{
		// make a guess for the ship's mass
		vec3d size;
		vm_vec_sub(&size,&pm->maxs,&pm->mins);
		float vmass=size.xyz.x*size.xyz.y*size.xyz.z;
		float amass=4.65f*(float)pow(vmass,(2.0f/3.0f));

		nprintf(("Physics", "pi->mass==0.0f. setting to %f\n",amass));
		Warning(LOCATION, "%s (%s) has no mass! setting to %f", sinfo->name, sinfo->pof_file, amass);
		pm->mass=amass;
		pi->mass=amass * sinfo->density;
	}
	else 
		pi->mass = pm->mass * sinfo->density;

	// it was print-worthy back in read_and_process_model_file() in modelread.cpp, but now that its being used for an actual ship the user should be warned.
	if (IS_MOI_VEC_NULL(&pm->moment_of_inertia.vec.fvec)
		&& IS_MOI_VEC_NULL(&pm->moment_of_inertia.vec.uvec)
		&& IS_MOI_VEC_NULL(&pm->moment_of_inertia.vec.rvec))
		Warning(LOCATION, "%s (%s) has a null moment of inertia!", sinfo->name, sinfo->pof_file);

	// if invalid they were already warned about this in read_and_process_model_file() in modelread.cpp, so now we just need to try and sweep it under the rug
	if (!is_valid_matrix(&pm->moment_of_inertia))
	{
		// TODO: generate MOI properly
		vm_mat_zero(&pi->I_body_inv);
	}
	// it's valid, so we can use it
	else
		pi->I_body_inv = pm->moment_of_inertia;

	// scale inverse moment of inertia value by inverse density
	vm_vec_scale( &pi->I_body_inv.vec.rvec, 1/sinfo->density );
	vm_vec_scale( &pi->I_body_inv.vec.uvec, 1/sinfo->density );
	vm_vec_scale( &pi->I_body_inv.vec.fvec, 1/sinfo->density );

	pi->center_of_mass = pm->center_of_mass;
	pi->side_slip_time_const = sinfo->damp;
	pi->delta_bank_const = sinfo->delta_bank_const;
	pi->rotdamp = sinfo->rotdamp;
	pi->max_vel = sinfo->max_vel;
	pi->afterburner_max_vel = sinfo->afterburner_max_vel;
	pi->max_rotvel = sinfo->max_rotvel;
	pi->max_rear_vel = sinfo->max_rear_vel;
	pi->flags |= PF_ACCELERATES;	
	pi->flags &= ~PF_GLIDING; //Turn off glide
	pi->flags &= ~PF_FORCE_GLIDE;

	pi->forward_accel_time_const=sinfo->forward_accel;
	pi->afterburner_forward_accel_time_const=sinfo->afterburner_forward_accel;
	pi->forward_decel_time_const=sinfo->forward_decel;
	pi->slide_accel_time_const=sinfo->slide_accel;
	pi->slide_decel_time_const=sinfo->slide_decel;

	if ( (pi->max_vel.xyz.x > 0.000001f) || (pi->max_vel.xyz.y > 0.000001f) )
		pi->flags |= PF_SLIDE_ENABLED;

	pi->cur_glide_cap = pi->max_vel.xyz.z; //Init dynamic glide cap stuff to the max vel.
	if (sinfo->glide_cap > 0.000001f || sinfo->glide_cap < -0.000001f)		//Backslash
		pi->glide_cap = sinfo->glide_cap;
	else
		pi->glide_cap = std::max({ pi->max_vel.xyz.z, sinfo->max_overclocked_speed, pi->afterburner_max_vel.xyz.z });
	// If there's not a value for +Max Glide Speed set in the table, we want this cap to default to the fastest speed the ship can go.
	// However, a negative value means we want no cap, thus allowing nearly infinite maximum gliding speeds.

	//SUSHI: If we are using dynamic glide capping, force the glide cap to 0 (understood by physics.cpp to mean the cap should be dynamic)
	if (sinfo->glide_dynamic_cap)
		pi->glide_cap = 0;

	pi->glide_accel_mult = sinfo->glide_accel_mult;

	//SUSHI: This defaults to the AI_Profile value, and is only optionally overridden
	if (The_mission.ai_profile->flags[AI::Profile_Flags::Use_newtonian_dampening])
		pi->flags |= PF_NEWTONIAN_DAMP;
	if (sinfo->newtonian_damp_override)
	{
		if (sinfo->use_newtonian_damp)
			pi->flags |= PF_NEWTONIAN_DAMP;
		else
			pi->flags &= ~PF_NEWTONIAN_DAMP;
	}

	vm_vec_zero(&pi->vel);
	vm_vec_zero(&pi->rotvel);
	pi->speed = 0.0f;
	pi->heading = 0.0f;
	vm_set_identity(&pi->last_rotmat);

	//SparK: setting the reverse burners
	pi->afterburner_max_reverse_vel = sinfo->afterburner_max_reverse_vel;
	pi->afterburner_reverse_accel = sinfo->afterburner_reverse_accel;

	pi->gravity_const = sinfo->gravity_const;
}

/**
 * Get the type of the given ship as a string
 */
const char *ship_get_type(const ship_info *sip)
{
	if(sip->class_type < 0) {
		return XSTR("Unknown", 497);
	}

	return Ship_types[sip->class_type].name;
}

/**
 * Get the orders allowed for a ship -- based on ship type.
 *
 * This value might get overridden by a value in the mission file.
 */
const SCP_set<size_t>& ship_get_default_orders_accepted( ship_info *sip )
{
	if(sip->class_type >= 0) {
		return Ship_types[sip->class_type].ai_player_orders;
	} else {
		static SCP_set<size_t> inv_class_set;
		return inv_class_set;
	}
}

/**
 * Get the orders allowed against a ship -- all allowed by default
 *
 * This value might get overridden by a value in the mission file.
 */
SCP_set<size_t> ship_get_default_orders_against()
{
	SCP_set<size_t> orders;
	
	for (int i = 0; i <= (int)Player_orders.size(); i++) {
		orders.insert(i);
	}

	return orders;
}

vec3d get_submodel_offset(int model, int submodel){
	polymodel*pm = model_get(model);
	if(pm->submodel[submodel].parent == -1)
		return pm->submodel[submodel].offset;
	vec3d ret = pm->submodel[submodel].offset;
	vec3d v = get_submodel_offset(model,pm->submodel[submodel].parent);
	vm_vec_add2(&ret, &v);
	return ret;

}

// Reset all ship values to empty/unused.
void ship::clear()
{
	objnum = -1;
	ai_index = -1;
	ship_info_index = -1;
	hotkey = -1;
	escort_priority = 0;
	score = 0;
	assist_score_pct = 0.0f;
	respawn_priority = 0;

	pre_death_explosion_happened = 0;
	wash_killed = 0;	// serenity lies
	cargo1 = 0;							// "Nothing"
	cargo_title[0] = '\0';

	wing_status_wing_index = -1;
	wing_status_wing_pos = -1;

	alt_type_index = -1;
	callsign_index = -1;

	targeting_laser_bank = -1;
	targeting_laser_objnum = -1;

	num_corkscrew_to_fire = 0;
	corkscrew_missile_bank = -1;
	next_corkscrew_fire = timestamp(0);

	final_death_time = timestamp(-1);
	death_time = timestamp(-1);
	end_death_time = timestamp(-1);
	really_final_death_time = timestamp(-1);
	deathroll_rotvel = vmd_zero_vector;

	if (warpin_effect != nullptr)
		delete warpin_effect;
	if (warpout_effect != nullptr)
		delete warpout_effect;
	warpin_effect = nullptr;
	warpout_effect = nullptr;

	warpin_params_index = -1;
	warpout_params_index = -1;

	next_fireball = timestamp(-1);
	next_hit_spark = timestamp(-1);
	num_sparks = 0;
	memset(sparks, 0, MAX_SHIP_SPARKS * sizeof(ship_spark));

	use_special_explosion = false;
	special_exp_damage = -1;
	special_exp_blast = -1;
	special_exp_inner = -1;
	special_exp_outer = -1;
	use_shockwave = false;
	special_exp_shockwave_speed = 0;
	special_exp_deathroll_time = 0;

	special_hitpoints = 0;
	special_shield = -1;

	shield_points.clear();

	ship_max_shield_strength = 0.0f;
	ship_max_hull_strength = 0.0f;

	ship_guardian_threshold = 0;

	ship_name[0] = 0;
	display_name.clear();
	team = 0;

	time_cargo_revealed = 0;

	arrival_location = ArrivalLocation::AT_LOCATION;
	arrival_distance = 0;
	arrival_anchor = -1;
	arrival_path_mask = 0;
	arrival_cue = -1;
	arrival_delay = 0;

	departure_location = DepartureLocation::AT_LOCATION;
	departure_anchor = -1;
	departure_path_mask = 0;
	departure_cue = -1;
	departure_delay = 0;

	wingnum = -1;
	orders_accepted.clear();
	orders_allowed_against.clear();

	subsys_list_indexer.reset();
	subsys_list.clear();
	// since these aren't cleared by clear()
	subsys_list.next = NULL;
	subsys_list.prev = NULL;

	memset(&subsys_info, 0, SUBSYSTEM_MAX * sizeof(ship_subsys_info));

	memset(last_targeted_subobject, 0, MAX_PLAYERS * sizeof(ship_subsys *));

	shield_recharge_index = INTIAL_SHIELD_RECHARGE_INDEX;
	weapon_recharge_index = INTIAL_WEAPON_RECHARGE_INDEX;
	engine_recharge_index = INTIAL_ENGINE_RECHARGE_INDEX;
	weapon_energy = 0;
	prev_engine_strength = 1.0f;
	next_manage_ets = timestamp(0);

	flags.reset();
	reinforcement_index = -1;
	
	afterburner_fuel = 0.0f;
	afterburner_last_engage_fuel = 0.0f;
	afterburner_last_end_time = 0;

	cmeasure_count = 0;
	current_cmeasure = -1;

	cmeasure_fire_stamp = timestamp(0);

	target_shields_delta = 0.0f;
	target_weapon_energy_delta = 0.0f;

	weapons.clear();

	// ---------- special weapons init that isn't setting things to 0
	for (int i = 0; i < MAX_SHIP_PRIMARY_BANKS; i++)
	{
		// not part of weapons!
		primary_rotate_rate[i] = 0.0f;
		primary_rotate_ang[i] = 0.0f;
		// for fighter beams
		was_firing_last_frame[i] = 0;
	}

	secondary_point_reload_pct.init(0, 0, 0.0f);
	// ---------- done with weapons init

	shield_hits = 0;

	wash_intensity = 0.0f;
	wash_rot_axis = vmd_zero_vector;
	wash_timestamp = timestamp(0);

	num_swarm_missiles_to_fire = 0;
	next_swarm_fire = timestamp(0);
	num_turret_swarm_info = 0;
	swarm_missile_bank = -1;

	group = -1;
	death_roll_snd  = sound_handle::invalid();
	ship_list_index = -1;

	thruster_bitmap = -1;
	thruster_frame = 0.0f;

	thruster_glow_bitmap = -1;
	thruster_glow_frame = 0.0f;
	thruster_glow_noise = 1.0f;

	thruster_secondary_glow_bitmap = -1;
	thruster_tertiary_glow_bitmap = -1;
	thruster_distortion_bitmap = -1;

	next_engine_stutter = timestamp(0);

	base_texture_anim_timestamp = _timestamp();

	total_damage_received = 0.0f;
	memset(&damage_ship, 0, MAX_DAMAGE_SLOTS * sizeof(float));
	for(int i = 0; i < MAX_DAMAGE_SLOTS; i++)
		damage_ship_id[i] = -1;

	persona_index = -1;

	subsys_disrupted_flags = 0;
	subsys_disrupted_check_timestamp = timestamp(0);

	create_time = 0;

	ts_index = -1;

	large_ship_blowup_index = -1;
	for (int i = 0; i < NUM_SUB_EXPL_HANDLES; i++)
		sub_expl_sound_handle[i] = sound_handle::invalid();

	electrical_arcs.clear();
	arc_next_time = TIMESTAMP::invalid();

	emp_intensity = -1.0f;
	emp_decr = 0.0f;

	memset(trail_ptr, 0, MAX_SHIP_CONTRAILS * sizeof(trail *));

	tag_total = 0.0f;
	tag_left = -1.0f;
	time_first_tagged = 0;
	level2_tag_total = 0.0f;
	level2_tag_left = -1.0f;

	lightning_stamp = timestamp(-1);

	// set awacs warning flags so awacs ship only asks for help once at each level
    awacs_warning_flag.reset();

	special_warpin_objnum = -1;
	special_warpout_objnum = -1;

	fighter_beam_turret_data.clear();
	beam_sys_info.reset();

	primitive_sensor_range = DEFAULT_SHIP_PRIMITIVE_SENSOR_RANGE;

	current_viewpoint = -1;

	for (int i = 0; i < MAX_SHIP_CONTRAILS; i++)
		ABtrail_ptr[i] = NULL;
	memset(&ab_info, 0, MAX_SHIP_CONTRAILS * sizeof(trail_info));
	ab_count = 0;

	glow_point_bank_active.clear();

	shader_effect_num = 0;
	shader_effect_duration = 0;
	shader_effect_timestamp = TIMESTAMP::invalid();

	alpha_mult = 1.0f;

	last_fired_turret = NULL;

	bay_doors_anim_done_time = 0;
	bay_doors_status = MA_POS_NOT_SET;
	bay_doors_wanting_open = 0;
	bay_doors_launched_from = 0;
	bay_doors_need_open = false;
	bay_doors_parent_shipnum = -1;

	rcs_activity.clear();

	s_alt_classes.clear();

	ship_iff_color.clear();

	ammo_low_complaint_count = 0;

	armor_type_idx = -1;
	shield_armor_type_idx = -1;
	collision_damage_type_idx = -1;
	debris_damage_type_idx = -1;
	debris_net_sig = 0;

	model_instance_num = -1;

	time_created = 0;

	radar_visible_since = -1;
	radar_last_contact = -1;

	radar_last_status = VISIBLE;
	radar_current_status = VISIBLE;

	team_name = "";
	secondary_team_name = "";
	team_change_timestamp = timestamp(-1);
	team_change_time = 0;

	autoaim_fov = 0.0f;
	for (int i = 0; i < MAX_SHIP_PRIMARY_BANKS; i++) {
		bank_autoaim_fov[i] = 0.0f;
	}

	cockpit_model_instance = -1;

	multi_client_collision_timestamp = TIMESTAMP::immediate();

	passive_arc_next_times.clear();
}
bool ship::has_display_name() const {
	return flags[Ship::Ship_Flags::Has_display_name];
}
const char* ship::get_display_name() const {
	if (has_display_name()) {
		return display_name.c_str();
	} else {
		return ship_name;
	}
}

void ship::apply_replacement_textures(const SCP_vector<texture_replace> &replacements)
{
	if (replacements.empty())
		return;

	polymodel_instance* pmi = model_get_instance(model_instance_num);

	pmi->texture_replace = make_shared<model_texture_replace>();

	auto pm = model_get(Ship_info[ship_info_index].model_num);

	// now fill them in
	for (const auto& tr : replacements)
	{
		// look for textures
		for (auto j = 0; j < pm->n_textures; j++)
		{
			texture_map *tmap = &pm->maps[j];

			int tnum = tmap->FindTexture(tr.old_texture);
			if (tnum > -1)
				(*pmi->texture_replace)[j * TM_NUM_TYPES + tnum] = tr.new_texture_id;
		}
	}
}

void ship_weapon::clear() 
{
    flags.reset();

    num_primary_banks = 0;
    num_secondary_banks = 0;
    num_tertiary_banks = 0;

    current_primary_bank = 0;
    current_secondary_bank = 0;
    current_tertiary_bank = 0;

    previous_primary_bank = 0;
    previous_secondary_bank = 0;

    next_tertiary_fire_stamp = timestamp(0);

    for (int i = 0; i < MAX_SHIP_PRIMARY_BANKS; i++)
    {
        primary_bank_weapons[i] = -1;

		primary_bank_external_model_instance[i] = -1;
		primary_bank_model_instance_check[i] = false;

        next_primary_fire_stamp[i] = timestamp(0);
        last_primary_fire_stamp[i] = timestamp(-1);
        last_primary_fire_sound_stamp[i] = timestamp(0);

        primary_bank_slot_count[i] = 1;

        primary_bank_rearm_time[i] = timestamp(0);
        primary_animation_done_time[i] = timestamp(0);
        primary_bank_ammo[i] = 0;
        primary_bank_start_ammo[i] = 0;
        primary_bank_capacity[i] = 0;
        primary_next_slot[i] = 0;
        primary_bank_fof_cooldown[i] = 0;

        primary_animation_position[i] = EModelAnimationPosition::MA_POS_NOT_SET;

        primary_bank_substitution_pattern_index[i] = 0;

        burst_counter[i] = 0;
		burst_seed[i] = Random::next();
        external_model_fp_counter[i] = 0;

		primary_firepoint_indices[i].clear();
		primary_firepoint_next_to_fire_index[i] = 0;

		firing_loop_sounds[i] = -1;
    }

    for (int i = 0; i < MAX_SHIP_SECONDARY_BANKS; i++)
    {
        secondary_bank_weapons[i] = -1;

        next_secondary_fire_stamp[i] = timestamp(0);
        last_secondary_fire_stamp[i] = timestamp(-1);

        secondary_bank_rearm_time[i] = timestamp(0);
        secondary_animation_done_time[i] = timestamp(0);

        secondary_bank_ammo[i] = 0;
        secondary_bank_start_ammo[i] = 0;
        secondary_bank_capacity[i] = 0;
        secondary_next_slot[i] = 0;

		secondary_animation_position[i] = EModelAnimationPosition::MA_POS_NOT_SET;

		secondary_bank_substitution_pattern_index[i] = 0;

        burst_counter[i + MAX_SHIP_PRIMARY_BANKS] = 0;
        external_model_fp_counter[i + MAX_SHIP_PRIMARY_BANKS] = 0;
    }

    tertiary_bank_ammo = 0;
    tertiary_bank_start_ammo = 0;
    tertiary_bank_capacity = 0;
    tertiary_bank_rearm_time = timestamp(0);

    detonate_weapon_time = 0;
    ai_class = 0;

	remote_detonaters_active = 0;

	per_burst_rot = 0.f;
}

ship_weapon::ship_weapon() {
	clear();
}

// Reset all wing values to empty/unused.
void wing::clear()
{
	name[0] = '\0';
	wing_squad_filename[0] = '\0';
	reinforcement_index = -1;
	hotkey = -1;

	num_waves = 1;
	current_wave = 0;
	threshold = 0;

	time_gone = 0;

	wave_count = 0;
	total_arrived_count = 0;
	red_alert_skipped_ships = 0;
	current_count = 0;

	for (int i = 0; i < MAX_SHIPS_PER_WING; i++)
		ship_index[i] = -1;

	total_destroyed = 0;
	total_departed = 0;
	total_vanished = 0;

	special_ship = 0;
	special_ship_ship_info_index = -1;

	arrival_location = ArrivalLocation::AT_LOCATION;
	arrival_distance = 0;
	arrival_anchor = -1;
	arrival_path_mask = 0;
	arrival_cue = -1;
	arrival_delay = 0;

	departure_location = DepartureLocation::AT_LOCATION;
	departure_anchor = -1;
	departure_path_mask = 0;
	departure_cue = -1;
	departure_delay = 0;

	wave_delay_min = 0;
	wave_delay_max = 0;

	// be sure to set the wave arrival timestamp of this wing to pop right away so that the
	// wing could be created if it needs to be
	wave_delay_timestamp = TIMESTAMP::immediate();

	flags.reset();

	// initialize wing goals
	for (int i = 0; i < MAX_AI_GOALS; i++)
		ai_goal_reset(&ai_goals[i]);

	net_signature = 0;

	wing_insignia_texture = -1;

	formation = -1;
	formation_scale = 1.0f;
}

// NOTE: Now that the clear() member function exists, this function only sets the stuff associated with the object and ship class.
static void ship_set(int ship_index, int objnum, int ship_type)
{
	ship		*shipp = &Ships[ship_index];
	object		*objp = &Objects[objnum];
	ship_info	*sip = &(Ship_info[ship_type]);
	ship_weapon	*swp = &shipp->weapons;
	polymodel *pm = model_get(sip->model_num);

	Assert(strlen(shipp->ship_name) <= NAME_LENGTH - 1);
	shipp->ship_info_index = ship_type;
	shipp->objnum = objnum;
	shipp->score = sip->score;

	ai_object_init(objp, shipp->ai_index);
	physics_ship_init(objp);

	if (Fred_running){
		shipp->ship_max_hull_strength = 100.0f;
	} else {
		shipp->ship_max_hull_strength = sip->max_hull_strength;
	}
	objp->hull_strength = shipp->ship_max_hull_strength;

	shipp->max_shield_recharge = sip->max_shield_recharge;

	if (sip->flags[Ship::Info_Flags::Model_point_shields]) {
		shipp->shield_points = pm->shield_points;
		objp->shield_quadrant.resize(pm->shield_points.size());
	}

	if (Fred_running) {
		shipp->ship_max_shield_strength = 100.0f;
		objp->shield_quadrant[0] = 100.0f;
	} else {
		shipp->ship_max_shield_strength = sip->max_shield_strength;
		shield_set_strength(objp, shield_get_max_strength(shipp));
	}

	shipp->orders_accepted = ship_get_default_orders_accepted( sip );

	if (!subsys_set(objnum))
	{
		char err_msg[512]; 
		sprintf (err_msg, "Unable to allocate ship subsystems, which shouldn't be possible anymore. Current allocation is %d (%d in use). No subsystems have been assigned to %s.", Num_ship_subsystems_allocated, Num_ship_subsystems, shipp->ship_name);

		if (Fred_running) 
			os::dialogs::Message(os::dialogs::MESSAGEBOX_ERROR, err_msg);
		else
			Error(LOCATION, "%s", err_msg);
	}

	ets_init_ship(objp);	// init ship fields that are used for the ETS

	shipp->flags.set(Ship_Flags::Engine_sound_on);

	// set certain flags that used to be in ship_info - Goober5000
	if (sip->flags[Ship::Info_Flags::Stealth])
		shipp->flags.set(Ship_Flags::Stealth);
	if (sip->flags[Ship::Info_Flags::Ship_class_dont_collide_invis])
		shipp->flags.set(Ship_Flags::Dont_collide_invis);
	
	auto obj_flags = objp->flags;
	if (sip->flags[Ship::Info_Flags::No_collide])
		obj_flags.remove(Object::Object_Flags::Collides);
	else
		obj_flags.set(Object::Object_Flags::Collides);

	obj_set_flags(objp, obj_flags);

	if (sip->flags[Ship::Info_Flags::No_ets])
		shipp->flags.set(Ship_Flags::No_ets);

	shipp->afterburner_fuel = sip->afterburner_fuel_capacity;

	if (Countermeasures_use_capacity) {
		float cm_cargo_size = Weapon_info[sip->cmeasure_type].cargo_size;
		shipp->cmeasure_count = fl2i(sip->cmeasure_max / cm_cargo_size);
	} else {
		shipp->cmeasure_count = sip->cmeasure_max;
	}
	shipp->current_cmeasure = sip->cmeasure_type;

	if (sip->num_primary_banks == 0 || swp->primary_bank_weapons[0] < 0) {
		swp->current_primary_bank = -1;
		swp->previous_primary_bank = -1;
	}
	if (sip->num_secondary_banks == 0 || swp->secondary_bank_weapons[0] < 0) {
		swp->current_secondary_bank = -1;
		swp->previous_secondary_bank = -1;
	}
	swp->current_tertiary_bank = -1;

	swp->ai_class = Ai_info[shipp->ai_index].ai_class;

	// handle ballistic primaries - kinda hackish; is this actually necessary?
	// because I think it's not needed - when I accidentally left this unreachable
	// it didn't cause any problems - Goober5000
	for (int i = 0; i < sip->num_primary_banks; i++ )
	{
		float weapon_size = Weapon_info[sip->primary_bank_weapons[i]].cargo_size;

		if ( weapon_size > 0.0f )
		{
			if (Fred_running)
				swp->primary_bank_ammo[i] = 100;
			else
				swp->primary_bank_ammo[i] = (int)std::lround(sip->primary_bank_ammo_capacity[i] / weapon_size);
		}
	}

	for (int i = 0; i < sip->num_secondary_banks; i++ )
	{
		if (Weapon_info[swp->secondary_bank_weapons[i]].wi_flags[Weapon::Info_Flags::SecondaryNoAmmo]) {
			swp->secondary_bank_ammo[i] = 0;
			continue;
		}

		float weapon_size = Weapon_info[sip->secondary_bank_weapons[i]].cargo_size;
		Assertion( weapon_size > 0.0f, "Cargo size for secondary weapon %s is invalid, must be greater than 0.\n", Weapon_info[sip->secondary_bank_weapons[i]].name );

		if (Fred_running)
			swp->secondary_bank_ammo[i] = 100;
		else
			swp->secondary_bank_ammo[i] = (int)std::lround(sip->secondary_bank_ammo_capacity[i] / weapon_size);
	}

	// set initial reload percentages... used to be done in ship::clear()
	int max_points_per_bank = 0;
	for (int i = 0; i < sip->num_secondary_banks; ++i)
	{
		int num_slots = pm->missile_banks[i].num_slots; // old variable name was conflicting with qt slots macro
		if (num_slots > max_points_per_bank)
			max_points_per_bank = num_slots;
	}
	shipp->secondary_point_reload_pct.init(sip->num_secondary_banks, max_points_per_bank, 1.0f);

	shipp->armor_type_idx = sip->armor_type_idx;
	shipp->shield_armor_type_idx = sip->shield_armor_type_idx;
	shipp->collision_damage_type_idx =  sip->collision_damage_type_idx;
	shipp->debris_damage_type_idx = sip->debris_damage_type_idx;

	shipp->warpin_params_index = sip->warpin_params_index;
	shipp->warpout_params_index = sip->warpout_params_index;

	// default cues, which will be changed later unless this is created in the lab or by ship-create
	shipp->arrival_cue = Locked_sexp_true;
	shipp->departure_cue = Locked_sexp_false;

	if(pm != NULL && pm->n_view_positions > 0)
		ship_set_eye(objp, 0);
	else
		ship_set_eye(objp, -1);

	sip->shockwave.damage_type_idx = sip->shockwave.damage_type_idx_sav;

	// Team colors
	shipp->team_name.assign( sip->default_team_name);
	shipp->secondary_team_name = "none";

	shipp->autoaim_fov = sip->autoaim_fov;

	for (int i = 0; i < MAX_SHIP_PRIMARY_BANKS; i++) {
		shipp->bank_autoaim_fov[i] = sip->bank_autoaim_fov[i];
	}

	shipp->multi_client_collision_timestamp = TIMESTAMP::immediate();

	shipp->max_shield_regen_per_second = sip->max_shield_regen_per_second;
	shipp->max_weapon_regen_per_second = sip->max_weapon_regen_per_second;
	
	for (size_t i = 0; i < sip->ship_passive_arcs.size(); i++)
		shipp->passive_arc_next_times.push_back(TIMESTAMP::immediate());
}

/**
 * Recalculates the overall strength of subsystems.
 *
 * Needed because several places in FreeSpace change subsystem strength and all 
 * this data needs to be kept up to date.
 */
void ship_recalc_subsys_strength( ship *shipp )
{
	int i;
	ship_subsys *ship_system;

	// fill in the subsys_info fields for all particular types of subsystems
	// make the current strength be 1.0.  If there are initial conditions on the ship, then
	// the mission parse code should take care of setting that.
	for (i = 0; i < SUBSYSTEM_MAX; i++) {
		shipp->subsys_info[i].type_count = 0;
		shipp->subsys_info[i].aggregate_max_hits = 0.0f;
		shipp->subsys_info[i].aggregate_current_hits = 0.0f;
	}

	// count all of the subsystems of a particular type.  For each generic type of subsystem, we store the
	// total count of hits.  (i.e. for 3 engines, we store the sum of the max_hits for each engine)
	for ( ship_system = GET_FIRST(&shipp->subsys_list); ship_system != END_OF_LIST(&shipp->subsys_list); ship_system = GET_NEXT(ship_system) ) {

		if (!(ship_system->flags[Ship::Subsystem_Flags::No_aggregate])) {
			int type = ship_system->system_info->type;
			Assert ( (type >= 0) && (type < SUBSYSTEM_MAX) );

			shipp->subsys_info[type].type_count++;
			shipp->subsys_info[type].aggregate_max_hits += ship_system->max_hits;
			shipp->subsys_info[type].aggregate_current_hits += ship_system->current_hits;
		}

		//Get rid of any persistent sounds on the subsystem
		//This is inefficient + sloppy but there's not really an easy way to handle things
		//if a subsystem is brought back from the dead, other than this
		if(ship_system->current_hits > 0.0f)
		{
            if (ship_system->subsys_snd_flags[Ship::Subsys_Sound_Flags::Dead])
            {
                obj_snd_delete_type(shipp->objnum, ship_system->system_info->dead_snd, ship_system);
                ship_system->subsys_snd_flags.set(Ship::Subsys_Sound_Flags::Dead, false);
            }
            if ((ship_system->system_info->alive_snd.isValid()) && !(ship_system->subsys_snd_flags[Ship::Subsys_Sound_Flags::Alive]))
            {
                obj_snd_assign(shipp->objnum, ship_system->system_info->alive_snd, &ship_system->system_info->pnt, OS_SUBSYS_ALIVE, ship_system);
                ship_system->subsys_snd_flags.set(Ship::Subsys_Sound_Flags::Alive);
            }
            if (!(ship_system->subsys_snd_flags[Ship::Subsys_Sound_Flags::Turret_rotation]))
            {
                if (ship_system->system_info->turret_base_rotation_snd.isValid())
                {
                    obj_snd_assign(shipp->objnum, ship_system->system_info->turret_base_rotation_snd, &ship_system->system_info->pnt, OS_TURRET_BASE_ROTATION, ship_system);
                    ship_system->subsys_snd_flags.set(Ship::Subsys_Sound_Flags::Turret_rotation);
                }
                if (ship_system->system_info->turret_gun_rotation_snd.isValid())
                {
                    obj_snd_assign(shipp->objnum, ship_system->system_info->turret_gun_rotation_snd, &ship_system->system_info->pnt, OS_TURRET_GUN_ROTATION, ship_system);
                    ship_system->subsys_snd_flags.set(Ship::Subsys_Sound_Flags::Turret_rotation);
                }
            }
            if ((ship_system->flags[Ship::Subsystem_Flags::Rotates]) && (ship_system->system_info->rotation_snd.isValid()) && !(ship_system->subsys_snd_flags[Ship::Subsys_Sound_Flags::Rotate]))
            {
                obj_snd_assign(shipp->objnum, ship_system->system_info->rotation_snd, &ship_system->system_info->pnt, OS_SUBSYS_ROTATION, ship_system);
                ship_system->subsys_snd_flags.set(Ship::Subsys_Sound_Flags::Rotate);
            }
		}
		else
		{
            if (ship_system->subsys_snd_flags[Ship::Subsys_Sound_Flags::Alive])
            {
                obj_snd_delete_type(shipp->objnum, ship_system->system_info->alive_snd, ship_system);
                ship_system->subsys_snd_flags.set(Ship::Subsys_Sound_Flags::Alive, false);;
            }
            if (ship_system->subsys_snd_flags[Ship::Subsys_Sound_Flags::Turret_rotation])
            {
                obj_snd_delete_type(shipp->objnum, ship_system->system_info->turret_base_rotation_snd, ship_system);
                obj_snd_delete_type(shipp->objnum, ship_system->system_info->turret_gun_rotation_snd, ship_system);
                ship_system->subsys_snd_flags.set(Ship::Subsys_Sound_Flags::Turret_rotation, false);
            }
            if (ship_system->subsys_snd_flags[Ship::Subsys_Sound_Flags::Rotate])
            {
                obj_snd_delete_type(shipp->objnum, ship_system->system_info->rotation_snd, ship_system);
                ship_system->subsys_snd_flags.set(Ship::Subsys_Sound_Flags::Rotate, false);
            }
            if ((ship_system->system_info->dead_snd.isValid()) && !(ship_system->subsys_snd_flags[Ship::Subsys_Sound_Flags::Dead]))
            {
                obj_snd_assign(shipp->objnum, ship_system->system_info->dead_snd, &ship_system->system_info->pnt, OS_SUBSYS_DEAD, ship_system);
                ship_system->subsys_snd_flags.set(Ship::Subsys_Sound_Flags::Dead, false);
            }
		}
	}

	// set any ship flags which should be set.  unset the flags since we might be repairing a subsystem
	// through sexpressions.
	if ( (shipp->subsys_info[SUBSYSTEM_ENGINE].type_count > 0) && (shipp->subsys_info[SUBSYSTEM_ENGINE].aggregate_current_hits <= 0.0f) ) {
		shipp->flags.set(Ship_Flags::Disabled);
	} else {
		shipp->flags.remove(Ship_Flags::Disabled);
		ship_reset_disabled_physics( &Objects[shipp->objnum], shipp->ship_info_index );
	}
}

/**
 * Fixup the model subsystem information for this ship pointer.
 * Needed when ships share the same model.
 */
static void ship_copy_subsystem_fixup(ship_info *sip)
{
	int model_num;

	model_num = sip->model_num;

	// no point copying the subsystem data if the ship in question has none...
	// mark that the ship (cargo container) has the path fixup done.
	if (sip->n_subsystems == 0) {
		sip->flags.set(Ship::Info_Flags::Path_fixup);
		return;
	}

	// if we need to get information for all our subsystems, we need to find another ship with the same model
	// number as our own and that has the model information
	for ( auto it = Ship_info.cbegin(); it != Ship_info.cend(); ++it ) {
		model_subsystem *source_msp, *dest_msp;

		if ( (it->model_num != model_num) || (&(*it) == sip) ){
			continue;
		}

		// see if this ship has subsystems and a model for the subsystems.  We only need check the first
		// subsystem since previous error checking would have trapped its loading as an error.
		Assert( it->n_subsystems == sip->n_subsystems );

		source_msp = &(it->subsystems[0]);
		dest_msp = &(sip->subsystems[0]);
		if (source_msp->model_num != -1) {
			model_copy_subsystems( sip->n_subsystems, dest_msp, source_msp );
		} else if (dest_msp->model_num != -1) {
			model_copy_subsystems( sip->n_subsystems, source_msp, dest_msp );
		} else {
			// if none were found try finding a another ship to copy the data from
			continue;
		}
		sip->flags.set(Ship::Info_Flags::Path_fixup);
		break;
	}

}

// as with object, don't set next and prev to NULL because they keep the object on the free and used lists
void ship_subsys::clear()
{
	int i;

	system_info = NULL;

	parent_objnum = -1;
	parent_subsys_index = -1;

	sub_name[0] = 0;
	current_hits = max_hits = 0.0f;

    flags.reset();

	subsys_guardian_threshold = 0;
	armor_type_idx = -1;

	turret_last_fire_direction = vmd_zero_vector;
	turret_next_enemy_check_stamp = timestamp(0);
	turret_next_fire_stamp = timestamp(0);
	turret_enemy_objnum = -1;
	turret_enemy_sig = 0;
	turret_next_fire_pos = 0;
	turret_time_enemy_in_range = 0.0f;
	turret_inaccuracy = 0.0f;
	turret_last_fired = TIMESTAMP::never();

	for (i = 0; i < NUM_TURRET_ORDER_TYPES; i++)
		turret_targeting_order[i] = -1;
	optimum_range = 0.0f;
	favor_current_facing = 0.0f;
	targeted_subsys = NULL;
	scripting_target_override = false;
	last_fired_weapon_info_index = -1;
	shared_fire_direction_beam_objnum = -1;

	turret_pick_big_attack_point_timestamp = timestamp(0);
	turret_big_attack_point = vmd_zero_vector;

	turret_animation_position = MA_POS_NOT_SET;
	turret_animation_done_time = 0;

	for (i = 0; i < MAX_TFP; i++)
		turret_swarm_info_index[i] = -1;
	turret_swarm_num = 0;

	awacs_intensity = 0.0f;
	awacs_radius = 0.0f;

    weapons.clear();

	submodel_instance_1 = nullptr;
	submodel_instance_2 = nullptr;

	disruption_timestamp = timestamp(0);

	subsys_cargo_name = 0;
	subsys_cargo_title[0] = '\0';
	time_subsys_cargo_revealed = 0;

	triggered_rotation_index = -1;

	points_to_target = 0.0f;
	base_rotation_rate_pct = 0.0f;
	gun_rotation_rate_pct = 0.0f;

    subsys_snd_flags.reset();

	rotation_timestamp = timestamp(0);

	for (i = 0; i < 32; i++)
		target_priority[i] = -1;
	num_target_priorities = 0;

	next_aim_pos_time = 0;
	last_aim_enemy_pos = vmd_zero_vector;
	last_aim_enemy_vel = vmd_zero_vector;

	rof_scaler = 1.0f;

	turret_max_bomb_ownage = -1; 
	turret_max_target_ownage = -1;

	info_from_server_stamp = TIMESTAMP::never();
}

/**
 * Set subsystem
 *
 * @param objnum				Object number (used as index into Objects[])
 * @param ignore_subsys_info	Default parameter with value of 0.  This is only set to 1 by the save/restore code
 */
static int subsys_set(int objnum, int ignore_subsys_info)
{	
	ship	*shipp = &Ships[Objects[objnum].instance];
	ship_info	*sinfo = &Ship_info[Ships[Objects[objnum].instance].ship_info_index];
	model_subsystem *model_system;
	ship_subsys *ship_system;
	int i, j, k;

	// set up the subsystems for this ship.  walk through list of subsystems in the ship-info array.
	// for each subsystem, get a new ship_subsys instance and set up the pointers and other values
	list_init ( &shipp->subsys_list );								// initialize the ship's list of subsystems
	shipp->subsys_list_indexer.reset();
	shipp->flags.remove(Ship::Ship_Flags::Subsystem_cache_valid);

	// make sure to have allocated the number of subsystems we require
	if (!ship_allocate_subsystems( sinfo->n_subsystems )) {
		return 0;
	}

	// make sure we set up the model instance properly
	// (we need this to have been done already so we can link the submodels with the subsystems)
	Assert(shipp->model_instance_num >= 0);
	polymodel_instance *pmi = model_get_instance(shipp->model_instance_num);
	Assert(pmi->model_num == sinfo->model_num);
	polymodel* pm = model_get(pmi->model_num);

	for ( i = 0; i < sinfo->n_subsystems; i++ )
	{
		model_system = &(sinfo->subsystems[i]);
		if (model_system->model_num < 0) {
			Error(LOCATION, "Invalid subobj_num or model_num in subsystem '%s' on ship type '%s'.\nNot linking into ship!\n\n"
				"This warning means that a subsystem was present in the table entry and not present in the model."
				"It should be removed from the table or added to the model.\n"
				"Ensure subsystem names are spelled correctly, and that submodels or special points intended to be subsystems have '$special=subsystem' in their properties.", model_system->subobj_name, sinfo->name );
			continue;
		}

		// set up the linked list
		ship_system = GET_FIRST( &ship_subsys_free_list );		// get a new element from the ship_subsystem array
		Assert ( ship_system != &ship_subsys_free_list );		// shouldn't have the dummy element
		list_remove( ship_subsys_free_list, ship_system );		// remove the element from the array
		list_append( &shipp->subsys_list, ship_system );		// link the element into the ship
		ship_system->clear();									// initialize it to a known blank slate

		ship_system->system_info = model_system;				// set the system_info pointer to point to the data read in from the model
		ship_system->parent_objnum = objnum;

		// link the submodel instance info
		if (model_system->subobj_num >= 0) {
			ship_system->submodel_instance_1 = &pmi->submodel[model_system->subobj_num];
		}
		if ((model_system->subobj_num != model_system->turret_gun_sobj) && (model_system->turret_gun_sobj >= 0)) {
			ship_system->submodel_instance_2 = &pmi->submodel[model_system->turret_gun_sobj];
		}

		// if the table has set an alt name copy it
		if (ship_system->system_info->alt_sub_name[0] != '\0') {
			strcpy_s(ship_system->sub_name, ship_system->system_info->alt_sub_name);
		}
		else {
			memset(ship_system->sub_name, '\0', sizeof(ship_system->sub_name));
		}

		// copy subsystem target priorities stuff
		ship_system->num_target_priorities = ship_system->system_info->num_target_priorities;
		for (j = 0; j < 32; j++) {
			ship_system->target_priority[j] = ship_system->system_info->target_priority[j];
		}

		ship_system->rof_scaler = ship_system->system_info->turret_rof_scaler;

		// zero flags
        ship_system->flags.reset();
		ship_system->weapons.flags.reset();
        ship_system->subsys_snd_flags.reset();

		// Goober5000
		if (model_system->flags[Model::Subsystem_Flags::Untargetable])
			ship_system->flags.set(Ship::Subsystem_Flags::Untargetable);
		// Wanderer
		if (model_system->flags[Model::Subsystem_Flags::No_ss_targeting])
			ship_system->flags.set(Ship::Subsystem_Flags::No_SS_targeting);
		if ((The_mission.ai_profile->flags[AI::Profile_Flags::Advanced_turret_fov_edge_checks]) || (model_system->flags[Model::Subsystem_Flags::Fov_edge_check]))
			ship_system->flags.set(Ship::Subsystem_Flags::FOV_edge_check);
		if ((The_mission.ai_profile->flags[AI::Profile_Flags::Require_turret_to_have_target_in_fov]) || (model_system->flags[Model::Subsystem_Flags::Fov_required]))
			ship_system->flags.set(Ship::Subsystem_Flags::FOV_Required);

		if (model_system->flags[Model::Subsystem_Flags::No_replace])
			ship_system->flags.set(Ship::Subsystem_Flags::No_replace);
		if (model_system->flags[Model::Subsystem_Flags::No_live_debris])
			ship_system->flags.set(Ship::Subsystem_Flags::No_live_debris);
		if (model_system->flags[Model::Subsystem_Flags::Ignore_if_dead])
			ship_system->flags.set(Ship::Subsystem_Flags::Missiles_ignore_if_dead);
		if (model_system->flags[Model::Subsystem_Flags::Allow_vanishing])
			ship_system->flags.set(Ship::Subsystem_Flags::Vanished);
        if (model_system->flags[Model::Subsystem_Flags::Damage_as_hull])
			ship_system->flags.set(Ship::Subsystem_Flags::Damage_as_hull);
		if (model_system->flags[Model::Subsystem_Flags::No_aggregate])
			ship_system->flags.set(Ship::Subsystem_Flags::No_aggregate);
		if (model_system->flags[Model::Subsystem_Flags::Rotates])
			ship_system->flags.set(Ship::Subsystem_Flags::Rotates);
		if (model_system->flags[Model::Subsystem_Flags::Translates])
			ship_system->flags.set(Ship::Subsystem_Flags::Translates);
		if (model_system->flags[Model::Subsystem_Flags::Player_turret_sound])
			ship_system->flags.set(Ship::Subsystem_Flags::Play_sound_for_player);
		if (model_system->flags[Model::Subsystem_Flags::No_disappear])
			ship_system->flags.set(Ship::Subsystem_Flags::No_disappear);
		if (model_system->flags[Model::Subsystem_Flags::Autorepair_if_disabled])
			ship_system->flags.set(Ship::Subsystem_Flags::Autorepair_if_disabled);
		if (model_system->flags[Model::Subsystem_Flags::No_autorepair_if_disabled])
			ship_system->flags.set(Ship::Subsystem_Flags::No_autorepair_if_disabled);
		if (model_system->flags[Model::Subsystem_Flags::Turret_locked])
			ship_system->weapons.flags.set(Ship::Weapon_Flags::Turret_Lock);
		// check the mission flag to possibly free all beam weapons - Goober5000, taken from SEXP.CPP, and moved to subsys_set() by Asteroth
		if (The_mission.flags[Mission::Mission_Flags::Beam_free_all_by_default]) 
			ship_system->weapons.flags.set(Ship::Weapon_Flags::Beam_Free);

		// Goober5000 - this has to be moved outside back to parse_create_object, because
		// a lot of the ship creation code is duplicated in several points and overwrites
		// previous things... ugh.
		ship_system->max_hits = model_system->max_subsys_strength;	// * shipp->ship_max_hull_strength / sinfo->max_hull_strength;

		if ( !Fred_running ) {
			ship_system->current_hits = ship_system->max_hits;		// set the current hits
		} else {
			ship_system->current_hits = 0.0f;				// Jason wants this to be 0 in Fred.
		}

		ship_system->subsys_guardian_threshold = 0;
		ship_system->armor_type_idx = model_system->armor_type_idx;
		ship_system->turret_next_fire_stamp = timestamp(0);
		ship_system->turret_next_enemy_check_stamp = timestamp(0);
		ship_system->turret_enemy_objnum = -1;
		ship_system->turret_next_fire_stamp = timestamp(Random::next(1, 500));	// next time this turret can fire
		ship_system->turret_last_fire_direction = model_system->turret_norm;
		ship_system->turret_next_fire_pos = 0;
		ship_system->turret_time_enemy_in_range = 0.0f;
		ship_system->disruption_timestamp=timestamp(0);
		ship_system->turret_pick_big_attack_point_timestamp = timestamp(0);
		ship_system->scripting_target_override = false;
		ship_system->last_fired_weapon_info_index = -1;
		ship_system->shared_fire_direction_beam_objnum = -1;

		vm_vec_zero(&ship_system->turret_big_attack_point);
		for(j = 0; j < NUM_TURRET_ORDER_TYPES; j++)
		{
			//WMC - Set targeting order to default.
			ship_system->turret_targeting_order[j] = j;
		}
		ship_system->optimum_range = model_system->optimum_range;
		ship_system->favor_current_facing = model_system->favor_current_facing;
		ship_system->subsys_cargo_name = 0;
		ship_system->subsys_cargo_title[0] = '\0';
		ship_system->time_subsys_cargo_revealed = 0;
		
		j = 0;
		int number_of_weapons = 0;

		for (k=0; k<MAX_SHIP_PRIMARY_BANKS; k++){
			if (model_system->primary_banks[k] != -1) {
				ship_system->weapons.primary_bank_weapons[j] = model_system->primary_banks[k];
				ship_system->weapons.primary_bank_capacity[j] = model_system->primary_bank_capacity[k];	// added by Goober5000
				ship_system->weapons.next_primary_fire_stamp[j] = timestamp(0);
				ship_system->weapons.last_primary_fire_stamp[j++] = -1;
			}
			ship_system->weapons.burst_counter[k] = 0;
		}

		ship_system->weapons.num_primary_banks = j;
		number_of_weapons += j;

		j = 0;
		for (k=0; k<MAX_SHIP_SECONDARY_BANKS; k++){
			if (model_system->secondary_banks[k] != -1) {
				ship_system->weapons.secondary_bank_weapons[j] = model_system->secondary_banks[k];
				ship_system->weapons.secondary_bank_capacity[j] = model_system->secondary_bank_capacity[k];
				ship_system->weapons.next_secondary_fire_stamp[j] = timestamp(0);
				ship_system->weapons.last_secondary_fire_stamp[j++] = -1;
			}
			ship_system->weapons.burst_counter[k + MAX_SHIP_PRIMARY_BANKS] = 0;
		}

		ship_system->weapons.num_secondary_banks = j;
		number_of_weapons += j;
		ship_system->weapons.current_primary_bank = -1;
		ship_system->weapons.current_secondary_bank = -1;
		
		ship_system->next_aim_pos_time = 0;

		ship_system->turret_max_bomb_ownage = model_system->turret_max_bomb_ownage;
		ship_system->turret_max_target_ownage = model_system->turret_max_target_ownage;

		ship_system->info_from_server_stamp = TIMESTAMP::never();

		// Make turret flag checks and warnings
		if ((ship_system->system_info->flags[Model::Subsystem_Flags::Turret_salvo]) && (ship_system->system_info->flags[Model::Subsystem_Flags::Turret_fixed_fp]))
		{
			Warning (LOCATION, "\"salvo mode\" flag used with \"fixed firingpoints\" flag\nsubsystem '%s' on ship type '%s'.\n\"salvo mode\" flag is ignored\n", model_system->subobj_name, sinfo->name );
            ship_system->system_info->flags.remove(Model::Subsystem_Flags::Turret_salvo);
		}

		if ((ship_system->system_info->flags[Model::Subsystem_Flags::Turret_salvo]) && (model_system->turret_num_firing_points < 2))
		{
			Warning (LOCATION, "\"salvo mode\" flag used with turret which has less than two firingpoints\nsubsystem '%s' on ship type '%s'.\n\"salvo mode\" flag is ignored\n", model_system->subobj_name, sinfo->name );
			ship_system->system_info->flags.remove(Model::Subsystem_Flags::Turret_salvo);
		}

		if ((ship_system->system_info->flags[Model::Subsystem_Flags::Turret_fixed_fp]) && (model_system->turret_num_firing_points < 2))
		{
			Warning (LOCATION, "\"fixed firingpoints\" flag used with turret which has less than two firingpoints\nsubsystem '%s' on ship type '%s'.\n\"fixed firingpoints\" flag is ignored\n", model_system->subobj_name, sinfo->name );
			ship_system->system_info->flags.remove(Model::Subsystem_Flags::Turret_fixed_fp);
		}

		if ((ship_system->system_info->flags[Model::Subsystem_Flags::Turret_salvo]) && (ship_system->system_info->flags[Model::Subsystem_Flags::Use_multiple_guns]))
		{
			Warning (LOCATION, "\"salvo mode\" flag used with \"use multiple guns\" flag\nsubsystem '%s' on ship type '%s'.\n\"use multiple guns\" flag is ignored\n", model_system->subobj_name, sinfo->name );
			ship_system->system_info->flags.remove(Model::Subsystem_Flags::Use_multiple_guns);
		}

		if ((ship_system->system_info->flags[Model::Subsystem_Flags::Turret_fixed_fp]) && !(ship_system->system_info->flags[Model::Subsystem_Flags::Use_multiple_guns]))
		{
			Warning (LOCATION, "\"fixed firingpoints\" flag used without \"use multiple guns\" flag\nsubsystem '%s' on ship type '%s'.\n\"use multiple guns\" guns added by default\n", model_system->subobj_name, sinfo->name );
            ship_system->system_info->flags.set(Model::Subsystem_Flags::Use_multiple_guns);
		}

		if ((ship_system->system_info->flags[Model::Subsystem_Flags::Turret_salvo]) && (number_of_weapons > 1))
		{
			mprintf(("\"salvo mode\" flag used with turret which has more than one weapon defined for it\nsubsystem '%s' on ship type '%s'.\nonly single weapon will be used\n", model_system->subobj_name, sinfo->name));
		}

		if ((ship_system->system_info->flags[Model::Subsystem_Flags::Turret_fixed_fp]) && (number_of_weapons > model_system->turret_num_firing_points))
		{
			mprintf(("\"fixed firingpoint\" flag used with turret which has more weapons defined for it than it has firingpoints\nsubsystem '%s' on ship type '%s'.\nweapons will share firingpoints\n", model_system->subobj_name, sinfo->name));
		}

		if ((ship_system->system_info->flags[Model::Subsystem_Flags::Turret_fixed_fp]) && (number_of_weapons < model_system->turret_num_firing_points))
		{
			mprintf(("\"fixed firingpoint\" flag used with turret which has less weapons defined for it than it has firingpoints\nsubsystem '%s' on ship type '%s'.\nsome of the firingpoints will be left unused\n", model_system->subobj_name, sinfo->name));
		}

		if ((ship_system->system_info->flags[Model::Subsystem_Flags::Share_fire_direction]) && (!(ship_system->system_info->flags[Model::Subsystem_Flags::Turret_salvo]) && !(ship_system->system_info->flags[Model::Subsystem_Flags::Use_multiple_guns])))
		{
			Warning(LOCATION, "\"share fire direction\" flag used with turret which does not have the \"salvo mode\" or \"use multiple guns\" flag set\nsubsystem '%s' on ship type '%s'.\n\"share fire direction\" flag is ignored\n", model_system->subobj_name, sinfo->name);
			ship_system->system_info->flags.remove(Model::Subsystem_Flags::Share_fire_direction);
		}

		for (k=0; k<ship_system->weapons.num_secondary_banks; k++) {
			if (Weapon_info[ship_system->weapons.secondary_bank_weapons[k]].wi_flags[Weapon::Info_Flags::SecondaryNoAmmo])
			{
				ship_system->weapons.secondary_bank_ammo[k] = 0;
				ship_system->weapons.secondary_next_slot[k] = 0;
				continue;
			}

			float weapon_size = Weapon_info[ship_system->weapons.secondary_bank_weapons[k]].cargo_size;
			Assertion( weapon_size > 0.0f, "Cargo size for secondary weapon %s is invalid, must be greater than 0.\n", Weapon_info[ship_system->weapons.secondary_bank_weapons[k]].name );
			ship_system->weapons.secondary_bank_ammo[k] = (Fred_running ? 100 : (int)std::lround(ship_system->weapons.secondary_bank_capacity[k] / weapon_size));

			ship_system->weapons.secondary_next_slot[k] = 0;
		}

		// Goober5000
		for (k=0; k<ship_system->weapons.num_primary_banks; k++)
		{
			float weapon_size = Weapon_info[ship_system->weapons.primary_bank_weapons[k]].cargo_size;

			if (weapon_size > 0.0f) {	// Non-ballistic primaries are supposed to have a cargo_size of 0
				ship_system->weapons.primary_bank_ammo[k] = (Fred_running ? 100 : (int)std::lround(ship_system->weapons.primary_bank_capacity[k] / weapon_size));
			}
		}

		ship_system->weapons.remote_detonaters_active = 0;
		ship_system->weapons.detonate_weapon_time = -1;
		ship_system->weapons.ai_class = sinfo->ai_class;  // assume ai class of ship for turret

		// rapid fire (swarm) stuff
		for (k = 0; k < MAX_TFP; k++)
			ship_system->turret_swarm_info_index[k] = -1;

		ship_system->turret_swarm_num = 0;

		// AWACS stuff
		ship_system->awacs_intensity = model_system->awacs_intensity;
		ship_system->awacs_radius = model_system->awacs_radius;
		if (ship_system->awacs_intensity > 0) {
            ship_system->system_info->flags.set(Model::Subsystem_Flags::Awacs);
        }

		// turn_rate, turn_accel
		if (ship_system->submodel_instance_1 != nullptr)
			model_set_submodel_instance_motion_info(&pm->submodel[model_system->subobj_num], ship_system->submodel_instance_1);
	}

	if ( !ignore_subsys_info ) {
		ship_recalc_subsys_strength( shipp );
	}

	return 1;
}

/**
 * Modify the matrix orient by the slew angles a.
 */
void compute_slew_matrix(matrix *orient, angles *a)
{
	matrix	tmp, tmp2;
	angles	t1, t2;

	t1 = t2 = *a;
	t1.h = 0.0f;	t1.b = 0.0f;
	t2.p = 0.0f;	t2.b = 0.0f;

	// put in p & b like normal
	vm_angles_2_matrix(&tmp, &t2 ); // Changed the order of axis rotations. First pitch, then yaw (Swifty)
	vm_matrix_x_matrix( &tmp2, orient, &tmp);

	// Put in heading separately
	vm_angles_2_matrix(&tmp, &t1 );
	vm_matrix_x_matrix( orient, &tmp2, &tmp );

	vm_orthogonalize_matrix(orient);
}

int Ship_shadows = 0;

DCF_BOOL( ship_shadows, Ship_shadows )

MONITOR( NumShipsRend )

int Show_shield_hits = 0;
DCF_BOOL( show_shield_hits, Show_shield_hits )

int Show_tnorms = 0;
DCF_BOOL( show_tnorms, Show_tnorms )

int Show_paths = 0;
DCF_BOOL( show_paths, Show_paths )

int Show_fpaths = 0;
DCF_BOOL( show_fpaths, Show_fpaths )

static void ship_find_warping_ship_helper(object *objp, dock_function_info *infop)
{
	// only check ships
	if (objp->type != OBJ_SHIP)
		return;

	// am I arriving or departing by warp?
	if (Ships[objp->instance].is_arriving(ship::warpstage::BOTH, true) || Ships[objp->instance].flags[Ship_Flags::Depart_warp])
	{
#ifndef NDEBUG
		// in debug builds, make sure only one of the docked objects has these flags set
		if (infop->maintained_variables.bool_value)
		{
			//WMC - This is annoying and triggered in sm2-10
			//Warning(LOCATION, "Ship %s and its docked ship %s are arriving or departing at the same time.\n",
			//Ships[infop->maintained_variables.objp_value->instance].ship_name, Ships[objp->instance].ship_name);
		}
#endif
		// we found someone
		infop->maintained_variables.bool_value = true;
		infop->maintained_variables.objp_value = objp;

#ifdef NDEBUG
		// return early in release builds
		infop->early_return_condition = true;
#endif
	}
}

//WMC - used for FTL and maneuvering thrusters
extern bool Rendering_to_shadow_map;

void ship_render_player_ship(object* objp, const vec3d* cam_offset, const matrix* rot_offset, const fov_t* fov_override) {
	ship* shipp = &Ships[objp->instance];
	ship_info* sip = &Ship_info[shipp->ship_info_index];
	polymodel_instance* pmi = model_get_instance(shipp->model_instance_num);

	const bool hasCockpitModel = sip->cockpit_model_num >= 0;

	const bool renderCockpitModel = (Viewer_mode != VM_TOPDOWN) && hasCockpitModel && !Disable_cockpits;
	const bool renderShipModel = ( 
		sip->flags[Ship::Info_Flags::Show_ship_model])
		&& (!Show_ship_only_if_cockpits_enabled || Cockpit_active)
		&& (!Viewer_mode || (Viewer_mode & VM_PADLOCK_ANY) || (Viewer_mode & VM_OTHER_SHIP) || (Viewer_mode & VM_TRACK)
			|| !(Viewer_mode & VM_EXTERNAL));
	Cockpit_active = renderCockpitModel;

	//Nothing to do
	if (!(renderCockpitModel || renderShipModel)) {
		return;
	}

	//If we aren't sure whether cockpits and external models can share the same worldspace,
	//we need to pre-render the external ship hull without shadows / deferred and give the cockpit precedence,
	//unless this ship has no cockpit at all
	const bool prerenderShipModel = renderShipModel && hasCockpitModel && !Cockpit_shares_coordinate_space;
	const bool deferredRenderShipModel = renderShipModel && !prerenderShipModel;

	gr_reset_clip();

	vec3d eye_pos, eye_offset, leaning_backup = leaning_position;
	matrix eye_orient;
	object_get_eye(&eye_pos, &eye_orient, objp, true, true, false);
	if (cam_offset != nullptr) {
		vec3d offset_local;
		vm_vec_unrotate(&offset_local, cam_offset, &eye_orient);
		leaning_position += offset_local;
	}
	if (rot_offset != nullptr) {
		//matrix m;
		//vm_copy_transpose(&m, rot_offset);
		eye_orient = *rot_offset * eye_orient;
	}

	vm_vec_copy_scale(&eye_offset, &eye_pos, -1.0f);
	if (!Disable_cockpit_sway)
		eye_offset += sip->cockpit_sway_val * objp->phys_info.acceleration;

	fov_t fov_backup = Proj_fov;
	if(fov_override)
		g3_set_fov(*fov_override);
	else
		g3_set_fov(Sexp_fov <= 0.0f ? COCKPIT_ZOOM_DEFAULT : Sexp_fov);

	if (prerenderShipModel) {
		gr_post_process_save_zbuffer();

		gr_set_proj_matrix(Proj_fov, gr_screen.clip_aspect, Min_draw_distance_cockpit, Max_draw_distance);
		gr_set_view_matrix(&leaning_position, &eye_orient);

		model_render_params render_info;
		render_info.set_object_number(OBJ_INDEX(objp));

		// update any replacement and/or team color textures (wookieejedi), then render
		render_info.set_replacement_textures(pmi->texture_replace);

		if (sip->uses_team_colors)
			render_info.set_team_color(shipp->team_name, shipp->secondary_team_name, 0, 0);

		render_info.set_detail_level_lock(0);
		model_render_immediate(&render_info, sip->model_num, shipp->model_instance_num, &objp->orient, &eye_offset, MODEL_RENDER_OPAQUE);
		model_render_immediate(&render_info, sip->model_num, shipp->model_instance_num, &objp->orient, &eye_offset, MODEL_RENDER_TRANS);

		gr_end_view_matrix();
		gr_end_proj_matrix();

		gr_post_process_restore_zbuffer();
	}

	//We only needed to prerender the ship model. This can occur if the cockpit isn't
	//rendered for some reason but a model exists. In this case, we still want to not
	//render the ship model with deferred rendering to keep visuals constant for the ship
	if (!renderCockpitModel && !deferredRenderShipModel) {
		Proj_fov = fov_backup;
		leaning_position = leaning_backup;
		return;
	}

	Lighting_mode = lighting_mode::COCKPIT;

	gr_reset_clip();

	//Deal with the model
	if(renderCockpitModel)
		model_clear_instance(sip->cockpit_model_num);

	gr_post_process_save_zbuffer();

	//Deal with shadow if we have to
	if (shadow_maybe_start_frame(Shadow_disable_overrides.disable_cockpit)) {
		gr_reset_clip();
		Shadow_override = false;

		shadows_start_render(&eye_orient, &leaning_position, Proj_fov, gr_screen.clip_aspect,
			std::get<0>(Shadow_distances_cockpit),
			std::get<1>(Shadow_distances_cockpit),
			std::get<2>(Shadow_distances_cockpit),
			std::get<3>(Shadow_distances_cockpit));

		if (deferredRenderShipModel) {
			model_render_params shadow_render_info;
			shadow_render_info.set_detail_level_lock(0);
			//If we just want to recieve, we still have to write to the color buffer but not to the zbuffer, otherwise shadow recieving breaks
			shadow_render_info.set_flags(MR_NO_TEXTURING | MR_NO_LIGHTING | (Show_ship_casts_shadow ? 0 : MR_NO_ZBUFFER));
			shadow_render_info.set_object_number(OBJ_INDEX(objp));
			model_render_immediate(&shadow_render_info, sip->model_num, shipp->model_instance_num, &objp->orient, &eye_offset, MODEL_RENDER_OPAQUE);
		}
		if (renderCockpitModel) {
			model_render_params shadow_render_info;
			shadow_render_info.set_detail_level_lock(0);
			shadow_render_info.set_flags(MR_NO_TEXTURING | MR_NO_LIGHTING);
			shadow_render_info.set_object_number(OBJ_INDEX(objp));
			vec3d offset = sip->cockpit_offset;
			vm_vec_unrotate(&offset, &offset, &objp->orient);
			if (!Disable_cockpit_sway)
				offset += sip->cockpit_sway_val * objp->phys_info.acceleration;
			model_render_immediate(&shadow_render_info, sip->cockpit_model_num, shipp->cockpit_model_instance, &objp->orient, &offset, MODEL_RENDER_OPAQUE);
		}

		shadows_end_render();
		gr_clear_states();
	}

	gr_set_proj_matrix(Proj_fov, gr_screen.clip_aspect, Min_draw_distance_cockpit, Max_draw_distance);
	gr_set_view_matrix(&leaning_position, &eye_orient);

	Shadow_view_matrix_render = gr_view_matrix;

	if (light_deferredcockpit_enabled()) {
		gr_deferred_lighting_begin(true);

		//When MSAA is enabled, we've just switched to the MS buffer. These still have the Z-Buffer we saved earlier, so clear that too
		if(Cmdline_msaa_enabled > 0)
			gr_zbuffer_clear(true);
	}

	uint64_t render_flags = MR_NORMAL;
	render_flags |= MR_NO_FOGGING;

	if (shipp->flags[Ship::Ship_Flags::Glowmaps_disabled]) {
		render_flags |= MR_NO_GLOWMAPS;
	}

	model_render_params ship_render_info;
	model_render_params cockpit_render_info;
	vec3d cockpit_offset = sip->cockpit_offset;

	//Properly render ship and cockpit model
	if (deferredRenderShipModel) {
		ship_render_info.set_detail_level_lock(0);
		ship_render_info.set_flags(render_flags);
		ship_render_info.set_replacement_textures(pmi->texture_replace);
		ship_render_info.set_object_number(OBJ_INDEX(objp));
		if (sip->uses_team_colors)
			ship_render_info.set_team_color(shipp->team_name, shipp->secondary_team_name, 0, 0);

		model_render_immediate(&ship_render_info, sip->model_num, shipp->model_instance_num, &objp->orient, &eye_offset, MODEL_RENDER_OPAQUE);
		gr_zbuffer_clear(true);
	}
	if (renderCockpitModel) {
		cockpit_render_info.set_detail_level_lock(0);
		cockpit_render_info.set_flags(render_flags);
		cockpit_render_info.set_replacement_textures(Player_cockpit_textures);
		vm_vec_unrotate(&cockpit_offset, &cockpit_offset, &objp->orient);
		if (!Disable_cockpit_sway)
			cockpit_offset += sip->cockpit_sway_val * objp->phys_info.acceleration;
		model_render_immediate(&cockpit_render_info, sip->cockpit_model_num, shipp->cockpit_model_instance, &objp->orient, &cockpit_offset, MODEL_RENDER_OPAQUE);
	}

	if (light_deferredcockpit_enabled()) {

		gr_end_view_matrix();
		gr_end_proj_matrix();

		gr_set_proj_matrix(Proj_fov, gr_screen.clip_aspect, Min_draw_distance_cockpit, Max_draw_distance);
		gr_set_view_matrix(&Eye_position, &Eye_matrix);

		gr_deferred_lighting_msaa();
		gr_deferred_lighting_end();
		gr_deferred_lighting_finish();

		gr_reset_lighting();

		gr_end_view_matrix();
		gr_end_proj_matrix();

		gr_set_proj_matrix(Proj_fov, gr_screen.clip_aspect, Min_draw_distance_cockpit, Max_draw_distance);
		gr_set_view_matrix(&leaning_position, &eye_orient);
	}

	//Transparent stuff has to come after deferred closes
	gr_zbuffer_set(ZBUFFER_TYPE_READ);

	if (deferredRenderShipModel) {
		model_render_immediate(&ship_render_info, sip->model_num, shipp->model_instance_num, &objp->orient, &eye_offset, MODEL_RENDER_TRANS);
	}

	if (renderCockpitModel) {
		model_render_immediate(&cockpit_render_info, sip->cockpit_model_num, shipp->cockpit_model_instance, &objp->orient, &cockpit_offset, MODEL_RENDER_TRANS);
	}

	if (light_deferredcockpit_enabled()) {
		gr_set_lighting();
	}

	Lighting_mode = lighting_mode::NORMAL;

	gr_end_view_matrix();
	gr_end_proj_matrix();

	leaning_position = leaning_backup;
	Proj_fov = fov_backup;

	//Restore the Shadow_override
	shadow_end_frame();

	gr_post_process_restore_zbuffer();
}

void ship_init_cockpit_displays(ship *shipp)
{
	ship_info *sip = &Ship_info[shipp->ship_info_index];

	int cockpit_model_num = sip->cockpit_model_num;

	// don't bother creating cockpit texture replacements if this ship has no cockpit
	if ( cockpit_model_num < 0 ) {
		return;
	}

	//-2 is reserved for cockpits as special PMI objnum, as they are NOT simply handleable as the ships objnum.
	//Functions that know what they are doing can replace the -2 with Player->objnum, otherwise it must be configured as "without object"
	shipp->cockpit_model_instance = model_create_instance(model_objnum_special::OBJNUM_COCKPIT, cockpit_model_num);
	sip->cockpit_animations.initializeMoveables(model_get_instance(shipp->cockpit_model_instance));

	// check if we even have cockpit displays
	if ( sip->displays.empty() ) {
		return;
	}

	if ( Player_cockpit_textures != nullptr) {
		return;
	}

	// ship's cockpit texture replacements haven't been setup yet, so do it.
	Player_cockpit_textures = make_shared<model_texture_replace>();

	for ( auto& display : sip->displays ) {
		ship_add_cockpit_display(&display, cockpit_model_num);
	}

	ship_set_hud_cockpit_targets();
}

void ship_close_cockpit_displays(ship* shipp)
{
	if (shipp && shipp->cockpit_model_instance >= 0) {
		model_delete_instance(shipp->cockpit_model_instance);
	}

	for ( int i = 0; i < (int)Player_displays.size(); i++ ) {
		if ( Player_displays[i].background >= 0 ) {
			bm_release(Player_displays[i].background);
		}

		if ( Player_displays[i].foreground >= 0 ) {
			bm_release(Player_displays[i].foreground);
		}

		if ( Player_displays[i].target >= 0 ) {
			bm_release(Player_displays[i].target);
		}
	}

	Player_displays.clear();
	Player_cockpit_textures.reset();
}

static void ship_add_cockpit_display(cockpit_display_info *display, int cockpit_model_num)
{
	if ( strlen(display->filename) <= 0 ) {
		return;
	}

	if( cockpit_model_num < 0 ) { 
		return;
	}

	int i, tm_num, glow_target = -1, glow_handle = -1, diffuse_handle = -1;
	int w, h;
	cockpit_display new_display;

	// if no texture target has been found yet, find one.
	polymodel *pm = model_get(cockpit_model_num);

	for ( i = 0; i < pm->n_textures; i++ )
	{
		tm_num = pm->maps[i].FindTexture(display->filename);
		if ( tm_num >= 0 ) {
			glow_target = i*TM_NUM_TYPES+TM_GLOW_TYPE;

			diffuse_handle = pm->maps[i].textures[TM_BASE_TYPE].GetTexture();
			glow_handle = pm->maps[i].textures[TM_GLOW_TYPE].GetTexture();
			break;
		}
	}

	// create a render target for this cockpit texture
	auto& glow_texture = (*Player_cockpit_textures)[glow_target];
	if ( glow_texture == -1) {
		bm_get_info(diffuse_handle, &w, &h);
		glow_texture = bm_make_render_target(w, h, BMP_FLAG_RENDER_TARGET_DYNAMIC);

		// if no render target was made, bail
		if ( glow_texture < 0 ) {
			return;
		}
	}

	new_display.background = -1;
	if ( display->bg_filename[0] != '\0' ) {
		new_display.background = bm_load(display->bg_filename);

		if ( new_display.background < 0 ) {
			Warning(LOCATION, "Unable to load background %s for cockpit display %s", display->bg_filename, display->name);
		}
	}

	new_display.foreground = -1;
	if ( display->fg_filename[0] != '\0' ) {
		new_display.foreground = bm_load(display->fg_filename);

		if ( new_display.foreground < 0 ) {
			Warning(LOCATION, "Unable to load background %s for cockpit display %s", display->fg_filename, display->name);
		}
	}
	
	strcpy_s(new_display.name, display->name);
	new_display.offset[0] = display->offset[0];
	new_display.offset[1] = display->offset[1];
	new_display.size[0] = display->size[0];
	new_display.size[1] = display->size[1];
	new_display.source = glow_handle;
	new_display.target = glow_texture;

	Player_displays.push_back(new_display);
}

static void ship_set_hud_cockpit_targets()
{
	if ( !Ship_info[Player_ship->ship_info_index].hud_enabled ) {
		return;
	}

	auto& hud = Ship_info[Player_ship->ship_info_index].hud_gauges;

	for ( int i = 0; i < (int)hud.size(); i++ ) {
		for ( int j = 0; j < (int)Player_displays.size(); j++ ) {
			hud[i]->setCockpitTarget(&Player_displays[j]);
		}
	}
}

int ship_start_render_cockpit_display(size_t cockpit_display_num)
{
	// make sure this thing even has a cockpit
	if ( Ship_info[Player_ship->ship_info_index].cockpit_model_num < 0 ) {
		return -1;
	}

	if ( Player_cockpit_textures == nullptr ) {
		return -1;
	}

	// check sanity of the cockpit display handle
	if ( cockpit_display_num >= Player_displays.size()) {
		return -1;
	}

	cockpit_display* display = &Player_displays[cockpit_display_num];

	if ( display->target < 0 ) {
		return -1;
	}

	if ( !bm_set_render_target(display->target) ) {
		return -1;
	}
	gr_push_debug_group("Render cockpit display");
	
	int cull = gr_set_cull(0);

	gr_clear();
	
	if ( display->source >= 0 ) {
		gr_set_bitmap(display->source);
		gr_bitmap(0, 0, GR_RESIZE_NONE);
	}

	if ( display->background >= 0 ) {
		gr_set_bitmap(display->background);
		gr_bitmap_ex(display->offset[0], display->offset[1], display->size[0], display->size[1], 0, 0, GR_RESIZE_NONE);
	}

	gr_set_cull(cull);

	return display->target;
}

void ship_end_render_cockpit_display(size_t cockpit_display_num)
{
	// make sure this thing even has a cockpit
	if ( Ship_info[Player_ship->ship_info_index].cockpit_model_num < 0 ) {
		return;
	}

	if ( Player_cockpit_textures == nullptr ) {
		return;
	}

	// check sanity of the cockpit display handle
	if ( cockpit_display_num >= Player_displays.size()) {
		return;
	}

	cockpit_display* display = &Player_displays[cockpit_display_num];

	int cull = gr_set_cull(0);
	if ( display->foreground >= 0 ) {
		gr_reset_clip();
		gr_set_bitmap(display->foreground);
		gr_bitmap_ex(display->offset[0], display->offset[1], display->size[0], display->size[1], 0, 0, GR_RESIZE_NONE);
	}

	gr_set_cull(cull);
	bm_set_render_target(-1);

	gr_pop_debug_group();
}

static void ship_subsystems_delete(ship *shipp)
{
	if ( NOT_EMPTY(&shipp->subsys_list) )
	{
		ship_subsys *systemp, *temp;

		systemp = GET_FIRST( &shipp->subsys_list );
		while ( systemp != END_OF_LIST(&shipp->subsys_list) ) {
			temp = GET_NEXT( systemp );								// use temporary since pointers will get screwed with next operation
			list_remove( shipp->subsys_list, systemp );			// remove the element
			list_append( &ship_subsys_free_list, systemp );		// and place back onto free list
			Num_ship_subsystems--;								// subtract from our in-use total
			systemp = temp;												// use the temp variable to move right along
		}

		shipp->subsys_list_indexer.reset();
		shipp->flags.remove(Ship::Ship_Flags::Subsystem_cache_valid);
	}
}

void ship_delete( object * obj )
{
	ship	*shipp;
	int	num, objnum __UNUSED;

	num = obj->instance;
	Assert( num >= 0);

	objnum = OBJ_INDEX(obj);
	Assert( Ships[num].objnum == objnum );

	shipp = &Ships[num];

	if (shipp->ai_index != -1){
		ai_free_slot(shipp->ai_index);
	}	

	// free up the list of subsystems of this ship.  walk through list and move remaining subsystems
	// on ship back to the free list for other ships to use.
	ship_subsystems_delete(&Ships[num]);
	shipp->objnum = -1;

	animation::ModelAnimationSet::stopAnimations(model_get_instance(shipp->model_instance_num));

	// glow point banks
	shipp->glow_point_bank_active.clear();

	if ( shipp->ship_list_index != -1 ) {
		ship_obj_list_remove(shipp->ship_list_index);
		shipp->ship_list_index = -1;
	}

	free_sexp2(shipp->arrival_cue);
	free_sexp2(shipp->departure_cue);

	// call the contrail system
	ct_ship_delete(shipp);
	
	model_delete_instance(shipp->model_instance_num);

	// free up any weapon model instances
	for (int i = 0; i < shipp->weapons.num_primary_banks; ++i)
	{
		if (shipp->weapons.primary_bank_external_model_instance[i] >= 0)
			model_delete_instance(shipp->weapons.primary_bank_external_model_instance[i]);
	}
}

/**
 * Used by ::ship_cleanup which is called if the ship is in a wing.
 *
 * This function updates the ship_index list (i.e. removes its entry in the list)
 * and packs the array accordingly.
 */
void ship_wing_cleanup( int shipnum, wing *wingp )
{
	int i, index = -1, team = Ships[shipnum].team;

	// find this ship's position within its wing
	for (i = 0; i < wingp->current_count; i++)
	{
		if (wingp->ship_index[i] == shipnum)
		{
			index = i;
			break;
		}
	}

	// this can happen in multiplayer (dogfight, ingame join specifically)
	if (index == -1)
		return;

	// compress the ship_index array and mark the last entry with a -1
	for (i = index; i < wingp->current_count - 1; i++)
		wingp->ship_index[i] = wingp->ship_index[i+1];

	wingp->current_count--;
	Assert ( wingp->current_count >= 0 );
	wingp->ship_index[wingp->current_count] = -1;

	// adjust the special ship if necessary
	if (wingp->special_ship > 0 && wingp->special_ship >= index){
		wingp->special_ship--;

		// sorry to have make this a little convoluted, if I put this after this if statement, then I introduce edge case bugs.
		// if there are ships in the wing, and the special ship changed, make sure the special_ship_ship_info_index is updated too
		if (wingp->current_count > 0){
			wingp->special_ship_ship_info_index = Ships[wingp->ship_index[wingp->special_ship]].ship_info_index;
		}
	
	// if the special ship *variable* didn't change, but the wing leader did because index was 0, adjust special_ship_ship_info_index 
	} else if (wingp->current_count > 0 && index == 0) {
			wingp->special_ship_ship_info_index = Ships[wingp->ship_index[0]].ship_info_index;
	}

	// if the current count is 0, check to see if the wing departed or was destroyed.
	if (wingp->current_count == 0)
	{
		// if this wing was ordered to depart by the player, set the current_wave equal to the total
		// waves so we can mark the wing as gone and no other ships arrive
		// Goober5000 - also if it's departing... this is sort of, but not exactly, what :V: did;
		// but it seems to be consistent with how it should behave
		if (wingp->flags[Ship::Wing_Flags::Departing, Ship::Wing_Flags::Departure_ordered])
			wingp->current_wave = wingp->num_waves;

		// Goober5000 - some changes for clarity and closing holes
		// make sure to flag the wing as gone if all of its member ships are gone and no more can arrive
		if ((wingp->current_wave >= wingp->num_waves) && (wingp->total_destroyed + wingp->total_departed + wingp->total_vanished == wingp->total_arrived_count))
		{
			// mark the wing as gone
			wingp->flags.set(Ship::Wing_Flags::Gone);
			wingp->time_gone = Missiontime;

			// if all ships were destroyed, log it as destroyed
			if (wingp->total_destroyed == wingp->total_arrived_count)
			{
				// first, be sure to mark a wing destroyed event if all members of wing were destroyed and on
				// the last wave.  This circumvents a problem where the wing could be marked as departed and
				// destroyed if the last ships were destroyed after the wing's departure cue became true.
				mission_log_add_entry(LOG_WING_DESTROYED, wingp->name, NULL, team);
			}
			// if some ships escaped, log it as departed
			else if (wingp->total_vanished != wingp->total_arrived_count)
			{
				// if the wing wasn't destroyed, and it is departing, then mark it as departed -- in this
				// case, there had better be ships in this wing with departure entries in the log file.  The
				// logfile code checks for this case.  
				mission_log_add_entry(LOG_WING_DEPARTED, wingp->name, NULL, team);
			}

#ifndef NDEBUG
			//WMC - Ships can depart too, besides being destroyed :P
			if ((wingp->total_destroyed + wingp->total_departed + wingp->total_vanished) != wingp->total_arrived_count)
			{
				// apparently, there have been reports of ships still present in the mission when this log
				// entry is written.  Do a sanity check here to find out for sure.
				for (ship_obj *so = GET_FIRST(&Ship_obj_list); so != END_OF_LIST(&Ship_obj_list); so = GET_NEXT(so))
				{
					if (Objects[so->objnum].flags[Object::Object_Flags::Should_be_dead])
						continue;

					// skip the player -- stupid special case.
					if (&Objects[so->objnum] == Player_obj)
						continue;
	
					if ((Game_mode & GM_MULTIPLAYER) && (Net_player->flags & NETINFO_FLAG_INGAME_JOIN))
						continue;
	
					if ((Ships[Objects[so->objnum].instance].wingnum == WING_INDEX(wingp)) && !(Ships[Objects[so->objnum].instance].is_dying_or_departing()))
					{
						// TODO: I think this Int3() is triggered when a wing whose ships are all docked to ships of another
						// wing departs.  It can be reliably seen in TVWP chapter 1 mission 7, when Torino and Iota wing depart.
						// Not sure how to fix this. -- Goober5000
						UNREACHABLE("A ship is still present even though its wing should be gone!");
					}
				}
			}
#endif
		}
	}
}

// functions to do management, like log entries and wing cleanup after a ship has been destroyed

// Goober5000
static void ship_actually_depart_helper(object *objp, dock_function_info *infop)
{
	// do standard departure stuff first
	objp->flags.set(Object::Object_Flags::Should_be_dead);
	if (objp->type == OBJ_SHIP)
		ship_cleanup(objp->instance, infop->parameter_variables.bool_value ? SHIP_VANISHED : SHIP_DEPARTED);

	// do the end-mission stuff if it's the player ship
	if (objp == Player_obj)
		gameseq_post_event(GS_EVENT_PLAYER_WARPOUT_DONE);
}

/**
 * Used to actually remove a ship, plus all the ships it's docked to, from the mission
 */
void ship_actually_depart(int shipnum, int method)
{
	dock_function_info dfi;
	dfi.parameter_variables.bool_value = (method == SHIP_VANISHED ? true:false);
	dock_evaluate_all_docked_objects(&Objects[Ships[shipnum].objnum], &dfi, ship_actually_depart_helper);

	// in a couple of cases we'll need to send a packet to update clients 
	if (MULTIPLAYER_MASTER && ((method == SHIP_DEPARTED_BAY) || (method == SHIP_VANISHED)) ) {
		send_ship_depart_packet(&Objects[Ships[shipnum].objnum], method); 
	}
}

// no destruction effects, not for player destruction and multiplayer, only self-destruction
void ship_destroy_instantly(object *ship_objp, bool with_debris)
{
	Assert(ship_objp->type == OBJ_SHIP);
	Assert(!(ship_objp == Player_obj));

	ship* shipp = &Ships[ship_objp->instance];

	if (scripting::hooks::OnShipDeathStarted->isActive()) {
		// add scripting hook for 'On Ship Death Started' -- Goober5000
		// hook is placed at the beginning of this function to allow the scripter to
		// actually have access to the ship before any death routines (such as mission logging) are executed
		scripting::hooks::OnShipDeathStarted->run(scripting::hooks::ShipDeathConditions{ shipp },
			scripting::hook_param_list(scripting::hook_param("Ship", 'o', ship_objp)));
	}

	// undocking and death preparation
	ship_stop_fire_primary(ship_objp);
	ai_deathroll_start(ship_objp);

	if (with_debris)
		shipfx_blow_up_model(ship_objp, 0, 0, &ship_objp->pos);

	mission_log_add_entry(LOG_SELF_DESTRUCTED, shipp->ship_name, NULL );
	
	// scripting stuff
	if (scripting::hooks::OnDeath->isActive()) {
		scripting::hooks::OnDeath->run(scripting::hooks::ObjectDeathConditions{ ship_objp },
			scripting::hook_param_list(scripting::hook_param("Self", 'o', ship_objp),
			scripting::hook_param("Ship", 'o', ship_objp)));
	}
	if (scripting::hooks::OnShipDeath->isActive()) {
		scripting::hooks::OnShipDeath->run(scripting::hooks::ShipDeathConditions{ shipp },
			scripting::hook_param_list(
			scripting::hook_param("Ship", 'o', ship_objp)));
	}

	ship_objp->flags.set(Object::Object_Flags::Should_be_dead);
	ship_cleanup(ship_objp->instance, SHIP_DESTROYED);
}

// convert the departure int method to a string --wookieejedi
static const char* get_departure_name(int method)
{
	const char* departname;
	switch (method) {
	case SHIP_DEPARTED_WARP: {
		departname = "SHIP_DEPARTED_WARP";
		break;
	};
	case SHIP_DEPARTED_BAY: {
		departname = "SHIP_DEPARTED_BAY";
		break;
	};
	case SHIP_VANISHED: {
		departname = "SHIP_VANISHED";
		break;
	};
	case SHIP_DEPARTED_REDALERT: {
		departname = "SHIP_DEPARTED_REDALERT";
		break;
	};
	// assume SHIP_DEPARTED
	default:
		departname = "SHIP_DEPARTED";
	};
	return departname;
}

/**
 * Merge ship_destroyed and ship_departed and ship_vanished
 */
void ship_cleanup(int shipnum, int cleanup_mode)
{
	Assert(shipnum >= 0 && shipnum < MAX_SHIPS);
	Assert(cleanup_mode &
	       (SHIP_DESTROYED | SHIP_DEPARTED | SHIP_VANISHED | SHIP_DESTROYED_REDALERT | SHIP_DEPARTED_REDALERT));
	Assert(Objects[Ships[shipnum].objnum].type == OBJ_SHIP);
	Assert(Objects[Ships[shipnum].objnum].flags[Object::Object_Flags::Should_be_dead]);

	ship *shipp = &Ships[shipnum];
	object *objp = &Objects[shipp->objnum];
	const char *jumpnode_name = nullptr;

	// this should never happen
	Assertion(Ship_registry_map.find(shipp->ship_name) != Ship_registry_map.end(), "Ship %s was destroyed, but was never stored in the ship registry!", shipp->ship_name);

	// Goober5000 - handle ship registry
	auto entry = &Ship_registry[Ship_registry_map[shipp->ship_name]];
	entry->status = ShipStatus::EXITED;
	entry->objnum = -1;
	entry->shipnum = -1;
	entry->cleanup_mode = cleanup_mode;

	// add the information to the exited ship list
	switch (cleanup_mode) {
	case SHIP_DESTROYED:
		ship_add_exited_ship(shipp, Ship::Exit_Flags::Destroyed);
		break;
	case SHIP_DEPARTED:
	case SHIP_DEPARTED_WARP:
	case SHIP_DEPARTED_BAY:
		ship_add_exited_ship(shipp, Ship::Exit_Flags::Departed);
		break;
	case SHIP_DESTROYED_REDALERT:
	case SHIP_DEPARTED_REDALERT:
		// Ship was removed in previous mission. Mark as "player deleted" for this mission
		ship_add_exited_ship(shipp, Ship::Exit_Flags::Player_deleted);
		break;
	case SHIP_VANISHED:
		// Do nothing
		break;
	default:
		// Can't Happen
		UNREACHABLE("Unknown cleanup_mode '%i' passed to ship_cleanup!", cleanup_mode);
		break;
	}

	// record kill?
	if (cleanup_mode == SHIP_DESTROYED) {
		// determine if we need to count this ship as a kill in counting number of kills per ship type
		// look at the ignore flag for the ship (if not in a wing), or the ignore flag for the wing
		// (if the ship is in a wing), and add to the kill count if the flags are not set
		if ( !(shipp->flags[Ship_Flags::Ignore_count]) || ((shipp->wingnum != -1) && !(Wings[shipp->wingnum].flags[Ship::Wing_Flags::Ignore_count])) )
			ship_add_ship_type_kill_count( shipp->ship_info_index );

		// let the event music system know an enemy was destroyed (important for deciding when to transition from battle to normal music)
		if (Player_ship != NULL && iff_x_attacks_y(Player_ship->team, shipp->team))
			event_music_hostile_ship_destroyed();
	}

	// add mission log entry?
	// (vanished ships and red-alert deleted ships have no log, and destroyed ships are logged in ship_hit_kill)
	if ((cleanup_mode == SHIP_DEPARTED_WARP) || (cleanup_mode == SHIP_DEPARTED_BAY) || (cleanup_mode == SHIP_DEPARTED)) {
		// see if this ship departed within the radius of a jump node -- if so, put the node name into
		// the secondary mission log field
		auto jnp = jumpnode_get_which_in(objp);
		if (jnp)
			jumpnode_name = jnp->GetName();

		mission_log_add_entry(LOG_SHIP_DEPARTED, shipp->ship_name, jumpnode_name, shipp->wingnum);
	}

	// run "On Ship Depart" conditional hook variable that accounts for all departure types
	// the hook is not limited to only warping --wookieejedi
	if ((cleanup_mode == SHIP_DEPARTED_WARP) || (cleanup_mode == SHIP_DEPARTED_BAY) ||
	    (cleanup_mode == SHIP_DEPARTED) || (cleanup_mode == SHIP_DEPARTED_REDALERT) ||
	    (cleanup_mode == SHIP_VANISHED)) {
		const char* departmethod = get_departure_name(cleanup_mode);

		if (scripting::hooks::OnShipDepart->isActive()) {
			scripting::hooks::OnShipDepart->run(scripting::hooks::ShipDepartConditions{ shipp },
				scripting::hook_param_list(
					scripting::hook_param("Ship", 'o', objp),
					scripting::hook_param("Method", 's', departmethod),
					scripting::hook_param("JumpNode", 's', jumpnode_name, jumpnode_name != nullptr)
				));
		}
	}

#ifndef NDEBUG
	// this isn't posted to the mission log, so log it here
	if (cleanup_mode == SHIP_VANISHED) {
		float mission_time = f2fl(Missiontime);
		int minutes = (int)(mission_time / 60);
		int seconds = (int)mission_time % 60;

		nprintf(("missionlog", "MISSION LOG: %s vanished at %02d:%02d\n", shipp->ship_name, minutes, seconds));
	}
#endif

	// update wingman status gauge
	if ( (shipp->wing_status_wing_index >= 0) && (shipp->wing_status_wing_pos >= 0) ) {
		switch (cleanup_mode) {
		case SHIP_DESTROYED:
		case SHIP_DESTROYED_REDALERT:
			hud_set_wingman_status_dead(shipp->wing_status_wing_index, shipp->wing_status_wing_pos);
			break;
		case SHIP_DEPARTED:
		case SHIP_DEPARTED_WARP:
		case SHIP_DEPARTED_BAY:
		case SHIP_DEPARTED_REDALERT:
			hud_set_wingman_status_departed(shipp->wing_status_wing_index, shipp->wing_status_wing_pos);
			break;
		case SHIP_VANISHED:
			hud_set_wingman_status_none(shipp->wing_status_wing_index, shipp->wing_status_wing_pos);
			break;
		default:
			// Can't Happen, but we should've already caught this
			UNREACHABLE("Unknown cleanup_mode '%i' passed to ship_cleanup!", cleanup_mode);
			break;
		}
	}

	// if ship belongs to a wing, do the wing cleanup
	if ( shipp->wingnum != -1 ) {
		wing *wingp = &Wings[shipp->wingnum];

		switch (cleanup_mode) {
		case SHIP_DESTROYED:
		case SHIP_DESTROYED_REDALERT:
			wingp->total_destroyed++;
			break;
		case SHIP_DEPARTED:
		case SHIP_DEPARTED_WARP:
		case SHIP_DEPARTED_BAY:
		case SHIP_DEPARTED_REDALERT:
			wingp->total_departed++;
			break;
		case SHIP_VANISHED:
			wingp->total_vanished++;
			break;
		default:
			// Can't Happen, but we should've already caught this
			UNREACHABLE("Unknown cleanup_mode '%i' passed to ship_cleanup!", cleanup_mode);
			break;
		}
		ship_wing_cleanup(shipnum, wingp);
	}

	// Note, this call to ai_ship_destroy() must come after ship_wing_cleanup for guarded wings to
	// properly note the destruction of a ship in their wing.
	ai_ship_destroy(shipnum);	// Do AI stuff for destruction/leave of ship.

	// Goober5000 - lastly, clear out the dead-docked list, per Mantis #2294
	// (for exploding ships, this list should have already been cleared by now, via
	// do_dying_undock_physics, except in the case of the destroy-instantly sexp)
	dock_dead_undock_all(objp);
}

/**
 * Calculates the blast and damage applied to a ship from another ship blowing up.
 * 
 * @param pos1			ship explosion position
 * @param pos2			other ship position
 * @param inner_rad		distance from ship center for which full damage is applied
 * @param outer_rad		distance from ship center for which no damage is applied
 * @param max_damage	maximum damage applied
 * @param max_blast		maximum impulse applied from blast
 * @param damage		damage applied
 * @param blast			impulse applied from blast
 */
int ship_explode_area_calc_damage( vec3d *pos1, vec3d *pos2, float inner_rad, float outer_rad, float max_damage, float max_blast, float *damage, float *blast )
{
	float dist;

	dist = vm_vec_dist_quick( pos1, pos2 );

	// check outside outer radius
	if ( dist > outer_rad )
		return -1;

	if ( dist < inner_rad ) {
	// check insider inner radius
		*damage = max_damage;
		*blast = max_blast;
	} else {
	// between inner and outer
		float fraction = 1.0f - (dist - inner_rad) / (outer_rad - inner_rad);
		*damage  = fraction * max_damage;
		*blast   = fraction * max_blast;
	}

	return 1;
}

static const float MAX_SHOCK_ANGLE_RANGE = 1.99f * PI;

/**
 * Applies damage to ship close to others when a ship dies and blows up
 *
 * @param exp_objp			ship object pointers
 */
static void ship_blow_up_area_apply_blast( object *exp_objp)
{
	ship *shipp;
	ship_info *sip;
	float	inner_rad, outer_rad, max_damage, max_blast, shockwave_speed;

	//	No area explosion in training missions.
	if (The_mission.game_type & MISSION_TYPE_TRAINING){
		return;
	}

	Assert( exp_objp != NULL );
	Assert( exp_objp->type == OBJ_SHIP );
	Assert( exp_objp->instance >= 0 );

	shipp = &Ships[exp_objp->instance];
	sip = &Ship_info[shipp->ship_info_index];

	Assert( (shipp != NULL) && (sip != NULL) );

	if ((exp_objp->hull_strength <= KAMIKAZE_HULL_ON_DEATH) && (Ai_info[Ships[exp_objp->instance].ai_index].ai_flags[AI::AI_Flags::Kamikaze]) && (shipp->special_exp_damage == -1)) {
		int override = Ai_info[shipp->ai_index].kamikaze_damage;

		inner_rad = exp_objp->radius*2.0f;
		outer_rad = exp_objp->radius*4.0f; // + (override * 0.3f);
		max_damage = i2fl(override);
		max_blast = override * 5.0f;
		shockwave_speed = 100.0f;
	} else {
		if (shipp->use_special_explosion) {
			inner_rad = i2fl(shipp->special_exp_inner);
			outer_rad = i2fl(shipp->special_exp_outer);
			max_damage = i2fl(shipp->special_exp_damage);
			max_blast = i2fl(shipp->special_exp_blast);
			shockwave_speed = i2fl(shipp->special_exp_shockwave_speed);
		} else {
			inner_rad = sip->shockwave.inner_rad;
			outer_rad = sip->shockwave.outer_rad;
			max_damage = sip->shockwave.damage;
			max_blast  = sip->shockwave.blast;
			shockwave_speed = sip->shockwave.speed;
		}
	}

	// account for ships that give no damage when they blow up.
	if ( (max_damage < 0.1f) && (max_blast < 0.1f) ){
		return;
	}

	if ( shockwave_speed > 0 ) {
		shockwave_create_info sci = sip->shockwave;

		sci.inner_rad = inner_rad;
		sci.outer_rad = outer_rad;
		sci.blast = max_blast;
		sci.damage = max_damage;
		sci.speed = shockwave_speed;
		sci.rot_angles.p = frand_range(0.0f, MAX_SHOCK_ANGLE_RANGE);
		sci.rot_angles.b = frand_range(0.0f, MAX_SHOCK_ANGLE_RANGE);
		sci.rot_angles.h = frand_range(0.0f, MAX_SHOCK_ANGLE_RANGE);
		shipfx_do_shockwave_stuff(shipp, &sci);
	} else {
		object *objp;
		float blast = 0.0f;
		float damage = 0.0f;
		for ( objp = GET_FIRST(&obj_used_list); objp !=END_OF_LIST(&obj_used_list); objp = GET_NEXT(objp) ) {
			if (objp->flags[Object::Object_Flags::Should_be_dead])
				continue;

			if ( (objp->type != OBJ_SHIP) && (objp->type != OBJ_ASTEROID) ) {
				continue;
			}
		
			if ( objp == exp_objp ){
				continue;
			}

			// don't blast no-collide or navbuoys
			if ( !objp->flags[Object::Object_Flags::Collides] || (objp->type == OBJ_SHIP && ship_get_SIF(objp->instance)[Ship::Info_Flags::Navbuoy]) ) {
				continue;
			}

			if ( ship_explode_area_calc_damage( &exp_objp->pos, &objp->pos, inner_rad, outer_rad, max_damage, max_blast, &damage, &blast ) == -1 ){
				continue;
			}

			vec3d force, vec_to_impact;
			vm_vec_sub(&vec_to_impact, &objp->pos, &exp_objp->pos);
			vm_vec_copy_normalize(&force, &vec_to_impact);
			vm_vec_scale(&force, blast);

			switch ( objp->type ) {
			case OBJ_SHIP:
				ship_apply_global_damage( objp, exp_objp, &exp_objp->pos, damage, sip->shockwave.damage_type_idx);
				ship_apply_whack( &force, &exp_objp->pos, objp );
				break;
			case OBJ_ASTEROID:
				asteroid_hit(objp, NULL, &exp_objp->pos, damage, &force);
				break;
			default:
				Int3();
				break;
			}
		}	// end for
	}
}

/**
 * Only ever called once for any ship that dies
 *
 * This function relies on the "dead dock" list, which replaces the dock_objnum_when_dead
 * used in retail.
 */
static void do_dying_undock_physics(object *dying_objp, ship *dying_shipp) 
{
	// this function should only be called for an object that was docked...
	// no harm in calling it if it wasn't, but we want to enforce this
	Assert(object_is_dead_docked(dying_objp));

	object *docked_objp;

	float damage;
	float impulse_mag;

	vec3d impulse_norm, impulse_vec, pos;

	// damage applied to each docked object
	damage = 0.2f * dying_shipp->ship_max_hull_strength;

	// Goober5000 - as with ai_deathroll_start, we can't simply iterate through the dock list while we're
	// unlinking things.  So just repeatedly unlink the first object.
	while (object_is_dead_docked(dying_objp))
	{
		docked_objp = dock_get_first_dead_docked_object(dying_objp);
		ship *docked_shipp = &Ships[docked_objp->instance];
		int dockee_index = dock_find_dead_dockpoint_used_by_object(docked_objp, dying_objp);

		// undo all the docking animations for the docked ship only
		ship_info* docked_sip = &Ship_info[docked_shipp->ship_info_index];

		(docked_sip->animations.getAll(model_get_instance(docked_shipp->model_instance_num), animation::ModelAnimationTriggerType::Docked, dockee_index)
			+ docked_sip->animations.getAll(model_get_instance(docked_shipp->model_instance_num), animation::ModelAnimationTriggerType::Docking_Stage3, dockee_index)
			+ docked_sip->animations.getAll(model_get_instance(docked_shipp->model_instance_num), animation::ModelAnimationTriggerType::Docking_Stage2, dockee_index)
			+ docked_sip->animations.getAll(model_get_instance(docked_shipp->model_instance_num), animation::ModelAnimationTriggerType::Docking_Stage1, dockee_index)).start(animation::ModelAnimationDirection::RWD);

		// only consider the mass of these two objects, not the whole assembly
		// (this is inaccurate, but the alternative is a huge mess of extra code for a very small gain in realism)
		float docked_mass = dying_objp->phys_info.mass + docked_objp->phys_info.mass;

		// damage this docked object
		ship_apply_global_damage(docked_objp, dying_objp, &dying_objp->pos, damage, -1);

		// do physics
		vm_vec_sub(&impulse_norm, &docked_objp->pos, &dying_objp->pos);
		vm_vec_normalize(&impulse_norm);
		// set for relative separation velocity of ~30
		impulse_mag = 50.f * docked_objp->phys_info.mass * dying_objp->phys_info.mass / docked_mass;
		vm_vec_copy_scale(&impulse_vec, &impulse_norm, impulse_mag);
		vm_vec_rand_vec_quick(&pos);
		vm_vec_scale(&pos, docked_objp->radius);
		vm_vec_add2(&pos, &docked_objp->pos);
		// apply whack to docked object
		ship_apply_whack(&impulse_vec, &pos, docked_objp);
		// enhance rotation of the docked object
		vm_vec_scale(&docked_objp->phys_info.rotvel, 2.0f);

		// apply whack to dying object
		vm_vec_negate(&impulse_vec);
		vm_vec_rand_vec_quick(&pos);
		vm_vec_scale(&pos, dying_objp->radius);
		vm_vec_add2(&pos, &dying_objp->pos);
		ship_apply_whack(&impulse_vec, &pos, dying_objp);

		// unlink the two objects, since dying_objp has blown up
		dock_dead_undock_objects(dying_objp, docked_objp);
	}
}

/**
 * Do the stuff we do in a frame for a ship that's in its death throes.
 */
static void ship_dying_frame(object *objp, int ship_num)
{
	ship *shipp = &Ships[ship_num];

	if ( shipp->flags[Ship_Flags::Dying] )	{
		ship_info *sip = &Ship_info[shipp->ship_info_index];
		bool knossos_ship = (sip->flags[Ship::Info_Flags::Knossos_device]);

		// bash hull value toward 0 (from self destruct)
		if (objp->hull_strength > 0) {
			int time_left = timestamp_until(shipp->final_death_time);
			float hits_left = objp->hull_strength;

			objp->hull_strength -= hits_left * (1000.0f * flFrametime) / time_left;
		}

		// special case of VAPORIZE
		if (shipp->flags[Ship_Flags::Vaporize]) {
			if (timestamp_elapsed(shipp->final_death_time)) {
				// play death sound
				snd_play_3d( gamesnd_get_game_sound(GameSounds::VAPORIZED), &objp->pos, &View_position, objp->radius, NULL, 0, 1.0f, SND_PRIORITY_MUST_PLAY  );

				// do joystick effect
				if (objp == Player_obj) {
					joy_ff_explode();
				}

				// if dying ship is docked, do damage to docked and physics
				if (object_is_dead_docked(objp))  {
					do_dying_undock_physics(objp, shipp);
				}			

				// do all accounting for respawning client and server side here.
				if (objp == Player_obj) {				
					gameseq_post_event(GS_EVENT_DEATH_BLEW_UP);
				}

				// mark object as dead
				objp->flags.set(Object::Object_Flags::Should_be_dead);

				// Don't blow up model.  Only use debris shards.
				// call ship function to clean up after the ship is destroyed.
				ship_cleanup(ship_num, SHIP_DESTROYED);
				return;
			} else {
				return;
			}
		}

		// bash the desired rotvel
		objp->phys_info.desired_rotvel = shipp->deathroll_rotvel;

		// Do fireballs for Big ship with propagating explostion, but not Kamikaze
		if (!(Ai_info[shipp->ai_index].ai_flags[AI::AI_Flags::Kamikaze]) && ship_get_exp_propagates(shipp) && (sip->death_roll_r_mult > 0.0f)) {
			if ( timestamp_elapsed(shipp->next_fireball))	{
				vec3d outpnt;
				polymodel *pm = model_get(sip->model_num);

				// Get a random point on the surface of a submodel
				vec3d pnt1 = submodel_get_random_point(pm->id, pm->detail[0]);

				model_instance_local_to_global_point(&outpnt, &pnt1, shipp->model_instance_num, pm->detail[0], &objp->orient, &objp->pos );

				float rad = objp->radius*0.1f;
				
				if (sip->death_roll_r_mult != 1.0f)
					rad *= sip->death_roll_r_mult;

				int fireball_type = fireball_ship_explosion_type(sip);
				if(fireball_type < 0) {
					fireball_type = FIREBALL_EXPLOSION_LARGE1 + Random::next(FIREBALL_NUM_LARGE_EXPLOSIONS);
				}
				fireball_create( &outpnt, fireball_type, FIREBALL_LARGE_EXPLOSION, OBJ_INDEX(objp), rad, false, &objp->phys_info.vel );
				// start the next fireball up in the next 50 - 200 ms (2-3 per frame)
				int min_time = 333;
				int max_time = 500;

				if (sip->death_roll_time_mult != 1.0f) {
					min_time = (int) (min_time / sip->death_roll_time_mult);
					max_time = (int) (max_time / sip->death_roll_time_mult);
				}

				shipp->next_fireball = timestamp_rand(min_time,max_time);

				// do sound - maybe start a random sound, if it has played far enough.
				do_sub_expl_sound(objp->radius, &outpnt, shipp->sub_expl_sound_handle.data());
			}
		}

		// create little fireballs for knossos as it dies
		if (knossos_ship) {
			if ( timestamp_elapsed(shipp->next_fireball)) {
				vec3d rand_vec, outpnt; // [0-.7 rad] in plane
				vm_vec_rand_vec_quick(&rand_vec);
				float scale = -vm_vec_dot(&objp->orient.vec.fvec, &rand_vec) * (0.9f + 0.2f * frand());
				vm_vec_scale_add2(&rand_vec, &objp->orient.vec.fvec, scale);
				vm_vec_normalize_quick(&rand_vec);
				scale = objp->radius * frand() * 0.717f;
				vm_vec_scale(&rand_vec, scale);
				vm_vec_add(&outpnt, &objp->pos, &rand_vec);

				float rad = objp->radius*0.2f;

				int fireball_type = fireball_ship_explosion_type(sip);
				if(fireball_type < 0) {
					fireball_type = FIREBALL_EXPLOSION_LARGE1 + Random::next(FIREBALL_NUM_LARGE_EXPLOSIONS);
				}
				fireball_create( &outpnt, fireball_type, FIREBALL_LARGE_EXPLOSION, OBJ_INDEX(objp), rad, false, &objp->phys_info.vel );
				// start the next fireball up in the next 50 - 200 ms (2-3 per frame)
				shipp->next_fireball = timestamp_rand(333,500);

				// emit particles
				auto source = particle::ParticleManager::get()->createSource(sip->knossos_end_particles);

				// For some reason these consider the uvec to be forward...
				matrix newOrient;
				newOrient.vec.fvec = objp->orient.vec.uvec;
				newOrient.vec.uvec = objp->orient.vec.fvec * -1.f;
				newOrient.vec.rvec = objp->orient.vec.rvec;

				auto host = std::make_unique<EffectHostVector>(outpnt, newOrient, objp->phys_info.vel);
				host->setRadius(objp->radius);
				source->setHost(std::move(host));
				source->finishCreation();

				// do sound - maybe start a random sound, if it has played far enough.
				do_sub_expl_sound(objp->radius, &outpnt, shipp->sub_expl_sound_handle.data());
			}
		}

		int time_until_minor_explosions = timestamp_until(shipp->final_death_time);

		// Wait until just before death and set off some explosions
		// If it is less than 1/2 second until large explosion, but there is
		// at least 1/10th of a second left, then create 5 small explosions
		if ( ((time_until_minor_explosions < 500) && (time_until_minor_explosions > 100) && (!shipp->pre_death_explosion_happened))
			// If we're already exploding and missed our chance, then do it anyway; better late than never
			|| ((time_until_minor_explosions <= 0) && (!shipp->pre_death_explosion_happened)) )
		{
			shipp->next_fireball = timestamp(-1);	// never time out again
			shipp->pre_death_explosion_happened=1;		// Mark this event as having occurred

			polymodel *pm = model_get(sip->model_num);
			polymodel_instance *pmi = model_get_instance(shipp->model_instance_num);

			// Start shockwave for ship with propagating explosion, do now for timing
			if ( ship_get_exp_propagates(shipp) ) {
				ship_blow_up_area_apply_blast( objp );
			}

			int zz_max = sip->death_fx_count;

			for (int zz=0; zz<zz_max; zz++ ) {
				// don't make sequence of fireballs for knossos
				if (knossos_ship) {
					break;
				}

				if (sip->death_fx_r_mult <= 0.0f) {
					break;
				}
				// Find two random vertices on the model, then average them
				// and make the piece start there.
				vec3d tmp, outpnt;

				// Gets two random points on the surface of a submodel [KNOSSOS]
				vec3d pnt1 = submodel_get_random_point(pm->id, pm->detail[0]);
				vec3d pnt2 = submodel_get_random_point(pm->id, pm->detail[0]);

				vm_vec_avg( &tmp, &pnt1, &pnt2 );
				model_instance_local_to_global_point(&outpnt, &tmp, pm, pmi, pm->detail[0], &objp->orient, &objp->pos );

				float rad = objp->radius*0.40f;

				rad *= sip->death_fx_r_mult;

				int fireball_type = fireball_ship_explosion_type(sip);
				if(fireball_type < 0) {
					fireball_type = FIREBALL_EXPLOSION_MEDIUM;
				}
				fireball_create( &outpnt, fireball_type, FIREBALL_MEDIUM_EXPLOSION, OBJ_INDEX(objp), rad, false, &objp->phys_info.vel );
			}
		}

		if ( timestamp_elapsed(shipp->final_death_time))	{
			shipp->death_time = shipp->final_death_time;
			shipp->final_death_time = timestamp(-1);	// never time out again
			
			// play ship explosion sound effect, pick appropriate explosion sound
			gamesnd_id sound_index;

			if (ship_has_sound(objp, GameSounds::SHIP_EXPLODE_1))
			{
				sound_index = ship_get_sound(objp, GameSounds::SHIP_EXPLODE_1);
			}
			else
			{
				if (sip->flags[Info_Flags::Capital] || sip->flags[Info_Flags::Knossos_device]) {
					sound_index=GameSounds::CAPSHIP_EXPLODE;
				} else {
					 if ( OBJ_INDEX(objp) & 1 ) {
						sound_index=GameSounds::SHIP_EXPLODE_1;
					} else {
						sound_index=GameSounds::SHIP_EXPLODE_2;
					}
				}
			}

			if (sound_index.isValid())
				snd_play_3d(gamesnd_get_game_sound(sound_index), &objp->pos, &View_position, objp->radius, nullptr, 0, 1.0f, SND_PRIORITY_MUST_PLAY);
			if (objp == Player_obj)
				joy_ff_explode();

			if (shipp->death_roll_snd.isValid()) {
				snd_stop(shipp->death_roll_snd);
				shipp->death_roll_snd = sound_handle::invalid();
			}

			// if dying ship is docked, do damage to docked and physics
			if (object_is_dead_docked(objp))  {
				do_dying_undock_physics(objp, shipp);
			}

			if (!knossos_ship){
				if (sip->regular_end_particles.isValid()) {
					// Previously, this had a branch where legacy-type particles had their orientation changed to point upwards for a nondescript reason.
					// While this likely makes very little difference in practice, we will pretend that the uvec of the ship is the normal, and orient legacy effects using it.

					// Use the new particle effect
					auto source = particle::ParticleManager::get()->createSource(sip->regular_end_particles);

					// Use the position since the ship is going to be invalid soon
					auto host = std::make_unique<EffectHostVector>(objp->pos, objp->orient, objp->phys_info.vel);
					host->setRadius(objp->radius);
					source->setHost(std::move(host));
					source->setNormal(objp->orient.vec.uvec);
					source->finishCreation();
				}
			}

			// If this is a splitting explosion, set it split up.
			if ( sip->explosion_splits_ship )	{
				if (Ai_info[shipp->ai_index].ai_flags[AI::AI_Flags::Kamikaze]) {
					ship_blow_up_area_apply_blast( objp );
				}
				shipfx_large_blowup_init(shipp);
				// need to timeout immediately to keep physics in sync
				shipp->really_final_death_time = timestamp(0);
				polymodel *pm = model_get(sip->model_num);
				shipp->end_death_time = timestamp((int) pm->core_radius);
			} else {
				// else, just a single big fireball
				float big_rad;
				int fireball_objnum, fireball_type, default_fireball_type;
				float explosion_life;
				big_rad = objp->radius*1.75f;

				default_fireball_type = FIREBALL_EXPLOSION_LARGE1 + Random::next(FIREBALL_NUM_LARGE_EXPLOSIONS);
				if (knossos_ship) {
					big_rad = objp->radius * 1.2f;
					default_fireball_type = FIREBALL_EXPLOSION_LARGE1;
				}
				//SUSHI: Option to override radius of big fireball
				if (Ship_info[shipp->ship_info_index].big_exp_visual_rad >= 0)
					big_rad = Ship_info[shipp->ship_info_index].big_exp_visual_rad;

				fireball_type = fireball_ship_explosion_type(sip);
				if(fireball_type < 0) {
					fireball_type = default_fireball_type;
				}
				fireball_objnum = fireball_create( &objp->pos, fireball_type, FIREBALL_LARGE_EXPLOSION, OBJ_INDEX(objp), big_rad, false, &objp->phys_info.vel );
				if ( fireball_objnum >= 0 )	{
					explosion_life = fireball_lifeleft(&Objects[fireball_objnum]);
				} else {
					explosion_life = 0.0f;
				}

				// JAS:  I put in all this code because of an item on my todo list that
				// said that the ship destroyed debris shouldn't pop in until the
				// big explosion is 30% done.  I did this on Oct24 and me & Adam 
				// thought it looked dumb since the explosion didn't move with the
				// ship, so instead of just taking this code out, since we might need
				// it in the future, I disabled it.   You can reenable it by changing
				// the commenting on the following two lines.
				shipp->end_death_time = shipp->really_final_death_time = timestamp( fl2i(explosion_life*1000.0f)/5 );	// Wait till 30% of vclip time before breaking the ship up.
			}

			shipp->flags.set(Ship_Flags::Exploded);

			if ( !(ship_get_exp_propagates(shipp)) ) {
				// apply area of effect blast damage from ship explosion
				ship_blow_up_area_apply_blast( objp );
			}
		}

		if ( timestamp_elapsed(shipp->really_final_death_time))	{
			// Copied from lock all turrets sexp
			// Locks all turrets on ship that is about to split.
			ship_subsys *subsys;
			subsys = GET_FIRST(&shipp->subsys_list);
			while ( subsys != END_OF_LIST(&shipp->subsys_list) ) 
			{
			// just mark all turrets as locked
				if (subsys->system_info->type == SUBSYSTEM_TURRET) {
					subsys->weapons.flags.set(Ship::Weapon_Flags::Turret_Lock);
				}
				subsys = GET_NEXT(subsys);
			}

			// do large_ship_split and explosion
			if ( shipp->large_ship_blowup_index >= 0 ) {
				if ( shipfx_large_blowup_do_frame(shipp, flFrametime) )	{
					// do all accounting for respawning client and server side here.
					if(objp == Player_obj) {				
						gameseq_post_event(GS_EVENT_DEATH_BLEW_UP);
					}

					objp->flags.set(Object::Object_Flags::Should_be_dead);
					
					ship_cleanup(ship_num, SHIP_DESTROYED);		// call ship function to clean up after the ship is destroyed.
				}
				return;
			} 

			shipfx_blow_up_model(objp, 0, sip->generic_debris_spew_num, &objp->pos);

			// do all accounting for respawning client and server side here.
			if(objp == Player_obj) {				
				gameseq_post_event(GS_EVENT_DEATH_BLEW_UP);
			}

			objp->flags.set(Object::Object_Flags::Should_be_dead);
								
			ship_cleanup(ship_num, SHIP_DESTROYED);		// call ship function to clean up after the ship is destroyed.
			shipp->really_final_death_time = timestamp( -1 );	// Never time out again!
		}

		// If a ship is dying (and not a capital or big ship) then stutter the engine sound
		if ( timestamp_elapsed(shipp->next_engine_stutter) ) {
			if ( !(sip->is_big_or_huge()) ) {
				shipp->flags.toggle(Ship_Flags::Engine_sound_on); // toggle state of engines
				shipp->next_engine_stutter = timestamp_rand(50, 250);
			}
		}
	}
}

// Trickle buffered energy from weapon<->shield transfers into the main banks
static void ship_move_ets_transfer_buffers(ship *shipp, object *obj, float frametime)
{
	float delta;
	ship_info *sip;

	if (shipp->flags[Ship_Flags::Dying])
		return;

	sip = &Ship_info[shipp->ship_info_index];

	delta = frametime * shipp->ship_max_shield_strength * sip->weap_shield_speed;

	//	Chase target_shields and target_weapon_energy
	if (shipp->target_shields_delta > 0.0f) {
		if (delta > shipp->target_shields_delta)
			delta = shipp->target_shields_delta;

		shield_add_strength(obj, delta);
		shipp->target_shields_delta -= delta;
	} else if (shipp->target_shields_delta < 0.0f) {
		//if (delta > -shipp->target_shields_delta)
			delta = -shipp->target_shields_delta;

		shield_add_strength(obj, -delta);
		shipp->target_shields_delta += delta;
	}

	delta = frametime * sip->max_weapon_reserve * sip->shield_weap_speed;

	if (shipp->target_weapon_energy_delta > 0.0f) {
		if (delta > shipp->target_weapon_energy_delta)
			delta = shipp->target_weapon_energy_delta;

		shipp->weapon_energy += delta;
		shipp->target_weapon_energy_delta -= delta;
	} else if (shipp->target_weapon_energy_delta < 0.0f) {
		//if (delta > -shipp->target_weapon_energy_delta)
			delta = -shipp->target_weapon_energy_delta;

		shipp->weapon_energy -= delta;
		shipp->target_weapon_energy_delta += delta;
	}

}

static int thruster_glow_anim_load(generic_anim *ga)
{
	if ( !VALID_FNAME(ga->filename) )
		return -1;

	int fps = 15;

	ga->first_frame = bm_load(ga->filename);
	if (ga->first_frame < 0)
	{
		Warning(LOCATION, "Couldn't load thruster glow animation '%s'\nPrimary glow type effect does not accept .EFF or .ANI effects", ga->filename);
		return -1;
	}
	ga->num_frames = NOISE_NUM_FRAMES;

	Assert(fps != 0);
	ga->total_time = i2fl(ga->num_frames)/fps;

	return 0;
}

/**
 * Loads the animations for ship's afterburners
 */
static void ship_init_thrusters()
{
	if ( Thrust_anim_inited == 1 )
		return;

	for (size_t i = 0; i < Species_info.size(); i++)
	{
		species_info *species = &Species_info[i];

		generic_anim_load(&species->thruster_info.flames.normal);
		generic_anim_load(&species->thruster_info.flames.afterburn);

		// Bobboau's extra thruster stuff
		{
			generic_bitmap_load(&species->thruster_secondary_glow_info.normal);
			generic_bitmap_load(&species->thruster_secondary_glow_info.afterburn);
			generic_bitmap_load(&species->thruster_tertiary_glow_info.normal);
			generic_bitmap_load(&species->thruster_tertiary_glow_info.afterburn);
			generic_bitmap_load(&species->thruster_distortion_info.normal);
			generic_bitmap_load(&species->thruster_distortion_info.afterburn);
		}

		// glows are handled a bit strangely
		thruster_glow_anim_load(&species->thruster_info.glow.normal);
		thruster_glow_anim_load(&species->thruster_info.glow.afterburn);
	}

	Thrust_anim_inited = 1;
}


/**
 * Figure out which thruster bitmap will get rendered next time around.  
 *
 * ::ship_render() needs to have shipp->thruster_bitmap set to
 * a valid bitmap number, or -1 if we shouldn't render thrusters.
 */
static void ship_do_thruster_frame( ship *shipp, object *objp, float frametime )
{
	float rate;
	int framenum;
	int secondary_glow_bitmap, tertiary_glow_bitmap, distortion_bitmap;
	generic_anim *flame_anim, *glow_anim;
	ship_info	*sinfo = &Ship_info[shipp->ship_info_index];

	if ( !Thrust_anim_inited ) {
		ship_init_thrusters();
	}

	if (objp->phys_info.flags & PF_AFTERBURNER_ON) {
		flame_anim = &sinfo->thruster_flame_info.afterburn;		// select afterburner flame
		glow_anim = &sinfo->thruster_glow_info.afterburn;			// select afterburner glow
		secondary_glow_bitmap = sinfo->thruster_secondary_glow_info.afterburn.bitmap_id;
		tertiary_glow_bitmap = sinfo->thruster_tertiary_glow_info.afterburn.bitmap_id;
		distortion_bitmap = sinfo->thruster_distortion_info.afterburn.bitmap_id;

		rate = 1.5f;		// go at 1.5x faster when afterburners on
	} else if (objp->phys_info.flags & PF_BOOSTER_ON) {
		flame_anim = &sinfo->thruster_flame_info.afterburn;		// select afterburner flame
		glow_anim = &sinfo->thruster_glow_info.afterburn;			// select afterburner glow
		secondary_glow_bitmap = sinfo->thruster_secondary_glow_info.afterburn.bitmap_id;
		tertiary_glow_bitmap = sinfo->thruster_tertiary_glow_info.afterburn.bitmap_id;
		distortion_bitmap = sinfo->thruster_distortion_info.afterburn.bitmap_id;

		rate = 2.5f;		// go at 2.5x faster when boosters on
	} else {
		flame_anim = &sinfo->thruster_flame_info.normal;			// select normal flame
		glow_anim = &sinfo->thruster_glow_info.normal;				// select normal glow
		secondary_glow_bitmap = sinfo->thruster_secondary_glow_info.normal.bitmap_id;
		tertiary_glow_bitmap = sinfo->thruster_tertiary_glow_info.normal.bitmap_id;
		distortion_bitmap = sinfo->thruster_distortion_info.normal.bitmap_id;

		// If thrust at 0, go at half as fast, full thrust; full framerate
		// so set rate from 0.67 to 1.67, depending on thrust from 0 to 1
		rate = 0.67f * (1.0f + objp->phys_info.linear_thrust.xyz.z);
	}

	Assert( frametime > 0.0f );

	// add primary thruster effects ...

	if (flame_anim->first_frame >= 0) {
		shipp->thruster_frame += frametime * rate;

		framenum = bm_get_anim_frame(flame_anim->first_frame, shipp->thruster_frame, flame_anim->total_time, true);

		// Get the bitmap for this frame
		shipp->thruster_bitmap = flame_anim->first_frame + framenum;
	} else {
		shipp->thruster_frame = 0.0f;
		shipp->thruster_bitmap = -1;
	}

	// primary glows ...
	if (glow_anim->first_frame >= 0) {
		shipp->thruster_glow_frame += frametime * rate;

		framenum = bm_get_anim_frame(glow_anim->first_frame, shipp->thruster_glow_frame, glow_anim->total_time, true);

		// Get the bitmap for this frame
		shipp->thruster_glow_bitmap = glow_anim->first_frame;	// + framenum;
		shipp->thruster_glow_noise = Noise[framenum];
	} else {
		shipp->thruster_glow_frame = 0.0f;
		shipp->thruster_glow_bitmap = -1;
		shipp->thruster_glow_noise = 1.0f;
	}

	// add extra thruster effects
	shipp->thruster_secondary_glow_bitmap = secondary_glow_bitmap;
	shipp->thruster_tertiary_glow_bitmap = tertiary_glow_bitmap;
	shipp->thruster_distortion_bitmap = distortion_bitmap;
}


/**
 * Figure out which thruster bitmap will get rendered next time around.  
 *
 * ship_render needs to have shipp->thruster_bitmap set to
 * a valid bitmap number, or -1 if we shouldn't render thrusters.
 *
 * This does basically the same thing as ship_do_thruster_frame, except it
 * operates on a weapon. This is in the ship code because it needs
 * the same thruster animation info as the ship stuff, and I would
 * rather extern this one function than all the thruster animation stuff.
 */
void ship_do_weapon_thruster_frame( weapon *weaponp, object *objp, float frametime )
{
	float rate;
	int framenum;
	generic_anim *flame_anim, *glow_anim;

	if (!Thrust_anim_inited)
		ship_init_thrusters();

	species_info *species = &Species_info[weaponp->species];
	weapon_info *wip = &Weapon_info[weaponp->weapon_info_index];

	// If thrust at 0, go at half as fast, full thrust; full framerate
	// so set rate from 0.67 to 1.67, depending on thrust from 0 to 1
	rate = 0.67f * (1.0f + objp->phys_info.linear_thrust.xyz.z);

	if (wip->thruster_flame.first_frame >= 0)
		flame_anim = &wip->thruster_flame;
	else
		flame_anim = &species->thruster_info.flames.normal;

	if (wip->thruster_glow.first_frame >= 0)
		glow_anim = &wip->thruster_glow;
	else
		glow_anim  = &species->thruster_info.glow.normal;

	Assert( frametime > 0.0f );

	if (flame_anim->first_frame >= 0) {
		weaponp->thruster_frame += frametime * rate;

		framenum = bm_get_anim_frame(flame_anim->first_frame, weaponp->thruster_frame, flame_anim->total_time, true);
	
		// Get the bitmap for this frame
		weaponp->thruster_bitmap = flame_anim->first_frame + framenum;
	} else {
		weaponp->thruster_frame = 0.0f;
		weaponp->thruster_bitmap = -1;
	}

	// Do it for glow bitmaps
	if (glow_anim->first_frame >= 0) {
		weaponp->thruster_glow_frame += frametime * rate;

		framenum = bm_get_anim_frame(glow_anim->first_frame, weaponp->thruster_glow_frame, glow_anim->total_time, true);
	
		// Get the bitmap for this frame
		weaponp->thruster_glow_bitmap = glow_anim->first_frame;	// + framenum;
		weaponp->thruster_glow_noise = Noise[framenum];
	} else {
		weaponp->thruster_glow_frame = 0.0f;
		weaponp->thruster_glow_bitmap = -1;
		weaponp->thruster_glow_noise = 1.0f;
	}
}



// Repair damaged subsystems for a ship, called for each ship once per frame.
// TODO: optimize by only calling ever N seconds and keeping track of elapsed time
//
// NOTE: need to update current_hits in the sp->subsys_list element, and the sp->subsys_info[]
// element.
#define SHIP_REPAIR_SUBSYSTEM_RATE 0.01f // percent repair per second for a subsystem
static void ship_auto_repair_frame(int shipnum, float frametime)
{
	ship_subsys		*ssp;
	ship_subsys_info	*ssip;
	ship			*sp;
	ship_info		*sip;
	object			*objp;
	float			real_repair_rate;

	#ifndef NDEBUG
	// only repair subsystems if Ship_auto_repair flag is set
	if (!Ship_auto_repair)
		return;
	#endif
	
	Assert( shipnum >= 0 && shipnum < MAX_SHIPS);
	sp = &Ships[shipnum];
	sip = &Ship_info[sp->ship_info_index];
	objp = &Objects[sp->objnum];

	// do not repair if already dead 
	if (sp->flags[Ship::Ship_Flags::Dying])
		return;

	// Repair the hull...or maybe unrepair?
	if (sip->hull_repair_rate != 0.0f)
	{
		float repaired_delta = sp->ship_max_hull_strength * sip->hull_repair_rate * frametime;
		float repair_threshold_strength = sp->ship_max_hull_strength * sip->hull_repair_max;

		if ((objp->hull_strength + repaired_delta) < repair_threshold_strength) {
			objp->hull_strength += repaired_delta;
		} else {
			repaired_delta = repair_threshold_strength - objp->hull_strength;
			CLAMP(repaired_delta, 0.0f, repair_threshold_strength); // this will be negative if it is already way above the threshold
			if (repaired_delta != 0.0f)
				objp->hull_strength += repaired_delta;
		}

		if (objp->hull_strength < 0)
			ship_hit_kill(objp, nullptr, nullptr, 0, true);
	}

	// do not repair if it is a large ship with a default repair rate
	if (!(sip->is_small_ship()) && sip->subsys_repair_rate == -2.0f)
		return;
	
	// determine if repair rate is default or specified
	if (sip->subsys_repair_rate == -2.0f)
		real_repair_rate = SHIP_REPAIR_SUBSYSTEM_RATE;
	else
		real_repair_rate = sip->subsys_repair_rate;

	// do not bother repairing if rate equals 0 --wookieejedi
	if (real_repair_rate == 0.0f)
		return;

	// AL 3-14-98: only allow auto-repair if power output not zero
	if ( sip->power_output <= 0 )
		return;
	
	// iterate through subsystems, repair as needed based on elapsed frametime
	for ( ssp = GET_FIRST(&sp->subsys_list); ssp != END_OF_LIST(&sp->subsys_list); ssp = GET_NEXT(ssp) ) {
		Assert(ssp->system_info->type >= 0 && ssp->system_info->type < SUBSYSTEM_MAX);
		ssip = &sp->subsys_info[ssp->system_info->type];

		if (ssp->current_hits < ssp->max_hits) {

			// only repair those subsystems which can be destroyed
			if ( ssp->max_hits <= 0 )
				continue;

			if (ssp->current_hits <= 0) {
				if (sip->flags[Ship::Info_Flags::Subsys_repair_when_disabled]) {
					if (ssp->flags[Ship::Subsystem_Flags::No_autorepair_if_disabled]) {
						continue;
					}
				} else if (!(ssp->flags[Ship::Subsystem_Flags::Autorepair_if_disabled])) {
					continue;
				}
			}

			// do incremental repair on the subsystem
			// check for overflow of current_hits
			float repaired_delta = ssp->max_hits * real_repair_rate * frametime;
			float repair_threshold_hits = ssp->max_hits * sip->subsys_repair_max;

			if ((ssp->current_hits + repaired_delta) < repair_threshold_hits) {
				ssp->current_hits += repaired_delta;
			} else {
				repaired_delta = repair_threshold_hits - ssp->current_hits;
				CLAMP(repaired_delta, 0.0f, repair_threshold_hits); // this will be negative if it is already way above the threshold
				if (repaired_delta != 0.0f)
					ssp->current_hits += repaired_delta;
			}

			// aggregate repair
			if (!(ssp->flags[Ship::Subsystem_Flags::No_aggregate])) {
				ssip->aggregate_current_hits += ssip->aggregate_max_hits * real_repair_rate * frametime;
				if ( ssip->aggregate_current_hits > ssip->aggregate_max_hits ) {
					ssip->aggregate_current_hits = ssip->aggregate_max_hits;
				}
			}

			// check to see if this subsystem was totally non functional before -- if so, then
			// reset the flags
			if ((ssp->system_info->type == SUBSYSTEM_ENGINE) && (sp->flags[Ship_Flags::Disabled])) {
				sp->flags.remove(Ship_Flags::Disabled);
				ship_reset_disabled_physics(objp, sp->ship_info_index);
			}
		}
	}	// end for
}

// this function checks to see how far the player has strayed from his starting location (should be
// single player only).  Issues a warning at some distance.  Makes mission end if he keeps flying away
// 3 strikes and you're out or too far away
#define PLAYER_MAX_DIST_WARNING			700000			// distance in KM at which player gets warning to return to battle
#define PLAYER_DISTANCE_MAX_WARNINGS	3				// maximum number of warnings player can receive before mission ends
#define PLAYER_MAX_DIST_END				750000			// distance from starting loc at which we end mission
#define PLAYER_WARN_DELTA_TIME			10000			//ms
#define PLAYER_DEATH_DELTA_TIME			5000			//ms

static void ship_check_player_distance_sub(player *p, int multi_target=-1)
{
	// only check distance for ships
	if ( p->control_mode != PCM_NORMAL )	{
		// already warping out... don't bother checking anymore
		return;
	}

	float dist = vm_vec_dist_quick(&Objects[p->objnum].pos, &vmd_zero_vector);

	int give_warning_to_player = 0;
	if ( dist > PLAYER_MAX_DIST_WARNING ) {
		if (p->distance_warning_count == 0) {
			give_warning_to_player = 1;
		} else {
			if (timestamp_elapsed(p->distance_warning_time)) {
				give_warning_to_player = 1;
			}
		}
	}

	if ( give_warning_to_player ) {
		// increase warning count
		p->distance_warning_count++;
		// set timestamp unless player PLAYER_FLAGS_DIST_TO_BE_KILLED flag is set
		if ( !(p->flags & PLAYER_FLAGS_DIST_TO_BE_KILLED) ) {
			p->distance_warning_time = timestamp(PLAYER_WARN_DELTA_TIME);
		}
		// issue up to max warnings
		if (p->distance_warning_count <= PLAYER_DISTANCE_MAX_WARNINGS) {
			message_send_builtin(MESSAGE_STRAY_WARNING, nullptr, nullptr, multi_target, -1);
		}

		if (p->distance_warning_count > PLAYER_DISTANCE_MAX_WARNINGS) {
			p->flags |= PLAYER_FLAGS_DIST_WARNING;
		}
	}

	if ( !(p->flags & PLAYER_FLAGS_FORCE_MISSION_OVER) && ((p->distance_warning_count > PLAYER_DISTANCE_MAX_WARNINGS) || (dist > PLAYER_MAX_DIST_END)) ) {
//		DKA 5/17/99 - DON'T force warpout.  Won't work multiplayer.  Blow up ship.
		if ( !(p->flags & PLAYER_FLAGS_DIST_TO_BE_KILLED) ) {
			message_send_builtin(MESSAGE_STRAY_WARNING_FINAL, nullptr, nullptr, multi_target, -1);
			p->flags |= PLAYER_FLAGS_DIST_TO_BE_KILLED;
			p->distance_warning_time = timestamp(PLAYER_DEATH_DELTA_TIME);
		}

		// get hull strength and blow up
		if ( (p->flags & PLAYER_FLAGS_DIST_TO_BE_KILLED) && (timestamp_elapsed(p->distance_warning_time)) ) {
			p->flags |= PLAYER_FLAGS_FORCE_MISSION_OVER;
			float damage = 10.0f * Objects[p->objnum].hull_strength;
			ship_apply_global_damage(&Objects[p->objnum], &Objects[p->objnum], NULL, damage, -1);
		}
	}

	// see if player has moved back into "bounds"
	if ( (dist < PLAYER_MAX_DIST_WARNING) && (p->flags & PLAYER_FLAGS_DIST_WARNING) && !(p->flags & PLAYER_FLAGS_DIST_TO_BE_KILLED) ) {
		p->flags &= ~PLAYER_FLAGS_DIST_WARNING;
		p->distance_warning_count = 1;
	}
}

static void ship_check_player_distance()
{
	int idx;

	// multiplayer
	if (Game_mode & GM_MULTIPLAYER) {
		// if I'm the server, check all non-observer players including myself
		if (MULTIPLAYER_MASTER) {
			// warn all players
			for (idx=0; idx<MAX_PLAYERS; idx++) {
				if (MULTI_CONNECTED(Net_players[idx]) && !MULTI_STANDALONE(Net_players[idx]) && !MULTI_OBSERVER(Net_players[idx]) && (Objects[Net_players[idx].m_player->objnum].type == OBJ_SHIP) ) {
					// if bad, blow him up
					ship_check_player_distance_sub(Net_players[idx].m_player, idx);
				}
			}
		}
	}
	// single player
	else {
		// maybe blow him up
		ship_check_player_distance_sub(Player);
	}		
}

void observer_process_post(object *objp)
{
	Assert(objp != NULL);

	if (objp == NULL)
		return;

	Assert(objp->type == OBJ_OBSERVER);

	if (Game_mode & GM_MULTIPLAYER) {
		// if I'm just an observer
		if (MULTI_OBSERVER(Net_players[MY_NET_PLAYER_NUM])) {
			float dist = vm_vec_dist_quick(&Player_obj->pos, &vmd_zero_vector);
			// if beyond max dist, reset to 0
			if (dist > PLAYER_MAX_DIST_END) {
				// set me to zero
				if ((Player_obj != NULL) && (Player_obj->type != OBJ_GHOST)) {
					Player_obj->pos = vmd_zero_vector;
				}
			}
		}
	}
}

/**
 * Reset some physics info when ship's engines goes from disabled->enabled
 */
void ship_reset_disabled_physics(object *objp, int ship_class)
{
	Assert(objp != NULL);

	if (objp == NULL)
		return;

	objp->phys_info.flags &= ~(PF_REDUCED_DAMP | PF_DEAD_DAMP);
	objp->phys_info.side_slip_time_const = Ship_info[ship_class].damp;
}

/**
 * Clear/set the subsystem disrupted flags
 */
static void ship_subsys_disrupted_check(ship *sp)
{
	ship_subsys *ss;
	int engines_disabled=0;
	
	if ( sp->subsys_disrupted_flags & (1<<SUBSYSTEM_ENGINE) ) {
		engines_disabled=1;
	}

	sp->subsys_disrupted_flags=0;

	ss = GET_FIRST(&sp->subsys_list);
	while ( ss != END_OF_LIST( &sp->subsys_list ) ) {
		if ( !timestamp_elapsed(ss->disruption_timestamp) ) {
			sp->subsys_disrupted_flags |= (1<<ss->system_info->type);
		}
		ss = GET_NEXT( ss );
	}

	if ( engines_disabled ) {
		if ( !(sp->subsys_disrupted_flags & (1<<SUBSYSTEM_ENGINE)) ) {
			if ( !(sp->flags[Ship_Flags::Disabled]) ) {
				ship_reset_disabled_physics(&Objects[sp->objnum], sp->ship_info_index);
			}
		}
	}
}

/**
 * Maybe check ship subsystems for disruption, and set/clear flags
 */
static void ship_subsys_disrupted_maybe_check(ship *shipp)
{
	if ( timestamp_elapsed(shipp->subsys_disrupted_check_timestamp) ) {
		ship_subsys_disrupted_check(shipp);
		shipp->subsys_disrupted_check_timestamp=timestamp(250);
	}
}

/**
 * Determine if a given subsystem is disrupted (ie inoperable)
 *
 * @param ss	pointer to ship subsystem
 * @return		1 if subsystem is disrupted, 0 if subsystem is not disrupted
 */
int ship_subsys_disrupted(const ship_subsys *ss)
{
	if ( !ss ) {
		Int3();		// should never happen, get Alan if it does.
		return 0;
	}

	if ( timestamp_elapsed(ss->disruption_timestamp) ) {
		return 0;
	} else {
		return 1;
	}
}

/**
 * Disrupt a subsystem (ie make it inoperable for a time)
 *
 * @param ss	pointer to ship subsystem to be disrupted
 * @param time	time in ms that subsystem should be disrupted
 */
void ship_subsys_set_disrupted(ship_subsys *ss, int time)
{
	int time_left=0;

	if ( !ss ) {
		Int3();		// should never happen, get Alan if it does.
		return;
	}

	time_left=timestamp_until(ss->disruption_timestamp);
	if ( time_left < 0 ) {
		time_left=0;
	}

	ss->disruption_timestamp = timestamp(time+time_left);
}

/**
 * Determine if a given type of subsystem is disrupted (i.e. inoperable)
 * 
 * @param sp	pointer to ship containing subsystem
 * @param type	type of subsystem (SUBSYSTEM_*)
 * @return		1 if subsystem is disrupted, 0 if subsystem is not disrupted
 */
int ship_subsys_disrupted(const ship *sp, int type)
{
	Assert ( sp != NULL );
	Assert ( type >= 0 && type < SUBSYSTEM_MAX );
	
	// Bogus pointer to ship to check for disrupted subsystem
	if (sp == NULL)
		return 0;
	
	if ( sp->subsys_disrupted_flags & (1<<type) ) {
		return 1;
	} else {
		return 0;
	}
}

float Decay_rate = 1.0f / 120.0f;
DCF(lethality_decay, "Sets ship lethality_decay, or the time in sec to go from 100 to 0 health (default is 1/120)")
{
	if (dc_optional_string_either("status", "--status") || dc_optional_string_either("?", "--?")) {
		dc_printf("Decay rate is currently %f\n", Decay_rate);
		return;
	}
	
	dc_stuff_float(&Decay_rate);
}

static void lethality_decay(ai_info *aip)
{
	float decay_rate = Decay_rate;
	aip->lethality -= 100.0f * decay_rate * flFrametime;
	aip->lethality = MAX(-10.0f, aip->lethality);

#ifndef NDEBUG
	if (Objects[Ships[aip->shipnum].objnum].flags[Object::Object_Flags::Player_ship]) {
		if (Framecount % 10 == 0) {
			int num_turrets = 0;
			if ((aip->target_objnum != -1) && (Objects[aip->target_objnum].type == OBJ_SHIP)) {
				//TODO: put this where it belongs, this would involve recompiling *everything* right now
				//-WMC
				int num_turrets_attacking(const object *turret_parent, int target_objnum);
				num_turrets = num_turrets_attacking(&Objects[aip->target_objnum], Ships[aip->shipnum].objnum);
			}
			nprintf(("lethality", "Player lethality: %.1f, num turrets targeting player: %d\n", aip->lethality, num_turrets));
		}
	}
#endif
}

// moved out of ship_process_post() so it can be called from either -post() or -pre() depending on Framerate_independent_turning
void ship_evaluate_ai(object* obj, float frametime) {

	int num = obj->instance;
	Assertion(obj->type == OBJ_SHIP, "Non-ship object passed to ship_evaluate_ai");
	Assertion(num >= 0 && num < MAX_SHIPS, "Invalid ship instance num in ship_evaluate_ai");
	ship* shipp = &Ships[num];
	Assertion(shipp->objnum == OBJ_INDEX(obj), "Ship objnum does not match its num in OBJ_INDEX in ship_evaluate_ai");

	// seems important
	if (shipp->ai_index < 0)
		return;

	// this prevents AI stuff, according to original Volition code
	if (physics_paused || ai_paused)
		return;

	// update ship lethality
	lethality_decay(&Ai_info[shipp->ai_index]);

	// if we are a player ship and not under AI control, all we do is process our subobjects
	if ((obj->flags[Object::Object_Flags::Player_ship]) && !Player_use_ai)
	{
		// AL 2-19-98: Fire turret for player if it exists
		//WMC - changed this to call ai_process_subobjects
		ai_process_subobjects(OBJ_INDEX(obj));
	}
	// otherwise we run the full AI pipeline
	else
	{
		ai_process( obj, shipp->ai_index, frametime );
	}
}

// given a particular maneuvering thruster, returns 0-1 how strongly the thruster is activated based on the ship's current phyiscal situation
float ship_get_thruster_status(const object* obj, const rcs_thruster_info *mtp) {
	float val = 0.0f;
	auto pi = &obj->phys_info;

	if (mtp->use_flags.any_set()) {
		if (pi->rotational_thrust.xyz.x < 0 && (mtp->use_flags[Ship::Thruster_Flags::Pitch_up])) {
			val = -pi->rotational_thrust.xyz.x;
		} else if (pi->rotational_thrust.xyz.x > 0 && (mtp->use_flags[Ship::Thruster_Flags::Pitch_down])) {
			val = pi->rotational_thrust.xyz.x;
		} else if (pi->rotational_thrust.xyz.y < 0 && (mtp->use_flags[Ship::Thruster_Flags::Yaw_right])) {
			val = -pi->rotational_thrust.xyz.y;
		} else if (pi->rotational_thrust.xyz.y > 0 && (mtp->use_flags[Ship::Thruster_Flags::Yaw_left])) {
			val = pi->rotational_thrust.xyz.y;
		} else if (pi->rotational_thrust.xyz.z < 0 && (mtp->use_flags[Ship::Thruster_Flags::Bank_right])) {
			val = -pi->rotational_thrust.xyz.z;
		} else if (pi->rotational_thrust.xyz.z > 0 && (mtp->use_flags[Ship::Thruster_Flags::Bank_left])) {
			val = pi->rotational_thrust.xyz.z;
		}

		//Backslash - show thrusters according to thrust amount, not speed
		if (pi->linear_thrust.xyz.x > 0 && (mtp->use_flags[Ship::Thruster_Flags::Slide_right])) {
			val = pi->linear_thrust.xyz.x;
		} else if (pi->linear_thrust.xyz.x < 0 && (mtp->use_flags[Ship::Thruster_Flags::Slide_left])) {
			val = -pi->linear_thrust.xyz.x;
		} else if (pi->linear_thrust.xyz.y > 0 && (mtp->use_flags[Ship::Thruster_Flags::Slide_up])) {
			val = pi->linear_thrust.xyz.y;
		} else if (pi->linear_thrust.xyz.y < 0 && (mtp->use_flags[Ship::Thruster_Flags::Slide_down])) {
			val = -pi->linear_thrust.xyz.y;
		} else if (pi->linear_thrust.xyz.z > 0 && (mtp->use_flags[Ship::Thruster_Flags::Forward])) {
			val = pi->linear_thrust.xyz.z;
		} else if (pi->linear_thrust.xyz.z < 0 && (mtp->use_flags[Ship::Thruster_Flags::Reverse])) {
			val = -pi->linear_thrust.xyz.z;
		}
	} else {
		// calculate it automatically
		vec3d cross;
		vec3d pos = mtp->pos / (obj->radius * 0.5f); // for full activation at half radius, further out will get capped to 1
		vm_vec_cross(&cross, &pos, &pi->rotational_thrust);
		val = vm_vec_dot(&mtp->norm, &cross);
		CLAMP(val, 0.0f, 1.0f);

		val += -vm_vec_dot(&mtp->norm, &vmd_x_vector) * pi->linear_thrust.xyz.x;
		val += -vm_vec_dot(&mtp->norm, &vmd_y_vector) * pi->linear_thrust.xyz.y;
		val += -vm_vec_dot(&mtp->norm, &vmd_z_vector) * pi->linear_thrust.xyz.z;

	}

	CLAMP(val, 0.0f, 1.0f);

	if (pi->flags & PF_GLIDING) {
		//WMC - get us a steady value
		vec3d des_vel;
		vm_vec_rotate(&des_vel, &pi->desired_vel, &obj->orient);

		vec3d glide_dir;
		vm_vec_copy_normalize(&glide_dir, &des_vel);
		val += -vm_vec_dot(&mtp->norm, &glide_dir);
		CLAMP(val, 0.0f, 1.0f);
	}

	return val;
}

// this function doesn't just play sounds but also keeps track of the time the thruster started firing
void ship_do_thruster_sounds(object *obj)
{
	int num = obj->instance;
	ship* shipp = &Ships[num];
	ship_info* sip = &Ship_info[Ships[num].ship_info_index];

	// check whether we can play sounds in general and also on this ship
	bool play_sound = !Cmdline_freespace_no_sound &&
		(obj != Player_obj || Play_thruster_sounds_for_player);

	// make sure there are enough thruster instances
	while (shipp->rcs_activity.size() < sip->rcs_thrusters.size())
		shipp->rcs_activity.emplace_back(TIMESTAMP::invalid(), -1, 0.0f);

	for (size_t i = 0; i < sip->rcs_thrusters.size(); i++)
	{
		auto& rcs = sip->rcs_thrusters[i];
		auto& activity = shipp->rcs_activity[i];

		TIMESTAMP& start_time = std::get<0>(activity);
		int& looping_sound = std::get<1>(activity);
		float& activated = std::get<2>(activity);

		activated = ship_get_thruster_status(obj, &rcs);

		// We are firing a thruster
		if (activated > 0.0f)
		{
			if (!start_time.isValid())
			{
				start_time = _timestamp();

				// we don't need to store a sound reference because it will just play once and be automatically removed
				if (play_sound && rcs.start_snd.isValid())
					obj_snd_assign(OBJ_INDEX(obj), rcs.start_snd, &rcs.pos, OS_MAIN | OS_PLAY_ON_PLAYER | OS_LOOPING_DISABLED);

				//Only assign looping sound if
				//it is specified
				//it isn't assigned already
				//start sound doesn't exist or has finished
				if (play_sound &&
					rcs.loop_snd.isValid()
					&& looping_sound < 0
					&& (!rcs.start_snd.isValid() || (gamesnd_get_max_duration(gamesnd_get_game_sound(rcs.start_snd)) < timestamp_since(start_time)))
					)
				{
					looping_sound = obj_snd_assign(OBJ_INDEX(obj), rcs.loop_snd, &rcs.pos, OS_MAIN | OS_PLAY_ON_PLAYER);
				}
			}
		}
		// We've just stopped firing a thruster
		else if (start_time.isValid())
		{
			start_time = TIMESTAMP::invalid();
			if (looping_sound >= 0)
			{
				obj_snd_delete(obj, looping_sound);
				looping_sound = -1;
			}

			// we don't need to store a sound reference because it will just play once and be automatically removed
			if (play_sound && rcs.stop_snd.isValid())
				obj_snd_assign(OBJ_INDEX(obj), rcs.stop_snd, &rcs.pos, OS_MAIN | OS_PLAY_ON_PLAYER | OS_LOOPING_DISABLED);
		}
	}
}

void ship_process_pre(object *obj, float frametime)
{
	// Cyborg, to enable turrets movement on clients, we need to process them here before  bailing
	if (MULTIPLAYER_CLIENT)
	{
		// But only with Framerate_independent_turning
		if (Framerate_independent_turning)
			ai_process_subobjects(OBJ_INDEX(obj));

		return;
	}

	// If Framerate_independent_turning is false everything following is evaluated in ship_process_post()
	if ( (obj == nullptr) || !frametime || !Framerate_independent_turning)
		return;

	if (obj->type != OBJ_SHIP) {
		nprintf(("AI", "Ignoring non-ship object in ship_process_pre()\n"));
		return;
	}

	int num = obj->instance;
	Assert(num >= 0 && num < MAX_SHIPS);
	Assert(Ships[num].objnum == OBJ_INDEX(obj));
	ship* shipp = &Ships[num];

	if ((!(shipp->is_arriving()) || (Ai_info[shipp->ai_index].mode == AIM_BAY_EMERGE)
		|| ((Warp_params[shipp->warpin_params_index].warp_type == WT_IN_PLACE_ANIM) && (shipp->flags[Ship_Flags::Arriving_stage_2])))
		&& !(shipp->flags[Ship_Flags::Depart_warp]))
	{
		ship_evaluate_ai(obj, frametime);
	}
}

MONITOR( NumShips )

static void ship_radar_process( object * obj, ship * shipp, ship_info * sip ) 
{
	Assert( obj != NULL);
	Assert( shipp != NULL );
	Assert( sip != NULL);

	shipp->radar_last_status = shipp->radar_current_status;

	RadarVisibility visibility = radar_is_visible(obj);

	if (visibility == NOT_VISIBLE)
	{
		if (shipp->radar_last_contact < 0 && shipp->radar_visible_since < 0)
		{
			shipp->radar_visible_since = -1;
			shipp->radar_last_contact = -1;
		}
		else
		{
			shipp->radar_visible_since = -1;
			shipp->radar_last_contact = Missiontime;
		}
	}
	else if (visibility == VISIBLE || visibility == DISTORTED)
	{
		if (shipp->radar_visible_since < 0)
		{
			shipp->radar_visible_since = Missiontime;
		}

		shipp->radar_last_contact = Missiontime;
	}

	shipp->radar_current_status = visibility;
}

void update_firing_sounds(object* objp, ship* shipp)
{
	ship_weapon* swp = &shipp->weapons;
	bool trigger_down = swp->flags[Ship::Weapon_Flags::Primary_trigger_down];

	// much like in ship_fire_primary, these prevent doubling up on the same sound effect for multiple banks triggering at once
	gamesnd_id start_snd_played = gamesnd_id();
	gamesnd_id end_snd_played = gamesnd_id();

	for (int i = 0; i < swp->num_primary_banks; i++) {
		if (swp->primary_bank_weapons[i] < 0)
			continue;

		weapon_info* wip = &Weapon_info[swp->primary_bank_weapons[i]];

		bool primaries_locked = shipp->flags[Ship_Flags::Primaries_locked];
		bool selected = swp->current_primary_bank == i || (shipp->flags[Ship_Flags::Primary_linked] && !wip->wi_flags[Weapon::Info_Flags::Nolink]);
		bool has_resources = shipp->weapon_energy >= wip->energy_consumed && (!wip->wi_flags[Weapon::Info_Flags::Ballistic] || swp->primary_bank_ammo[i] > 0);
		bool burst_only_allowed = !wip->burst_flags[Weapon::Burst_Flags::Burst_only_loop_sounds] || swp->burst_counter[i] > 0;
		bool dying = shipp->flags[Ship::Ship_Flags::Dying];

		// equality comparisons to -1 are correct here, -2 is valid and means a loop is active but the modder didnt specify an actual loop sound

		if (swp->firing_loop_sounds[i] == -1 && trigger_down && !primaries_locked && selected && has_resources && burst_only_allowed && !dying) {
			if (wip->start_firing_snd.isValid() && start_snd_played != wip->start_firing_snd) {
				if (objp == Player_obj)
					snd_play(gamesnd_get_game_sound(wip->start_firing_snd));
				else
					snd_play_3d(gamesnd_get_game_sound(wip->start_firing_snd), &objp->pos, &View_position);

				start_snd_played = wip->start_firing_snd;
			}

			vec3d pos = model_get(Ship_info[shipp->ship_info_index].model_num)->view_positions[0].pnt;

			if (wip->linked_loop_firing_snd.isValid() && shipp->flags[Ship::Ship_Flags::Primary_linked])
				swp->firing_loop_sounds[i] = obj_snd_assign(shipp->objnum, wip->linked_loop_firing_snd, &pos, OS_PLAY_ON_PLAYER);
			else if (wip->loop_firing_snd.isValid())
				swp->firing_loop_sounds[i] = obj_snd_assign(shipp->objnum, wip->loop_firing_snd, &pos, OS_PLAY_ON_PLAYER);
			else
				swp->firing_loop_sounds[i] = -2;
		} 

		if (swp->firing_loop_sounds[i] != -1 && (!trigger_down || primaries_locked || !selected || !has_resources || !burst_only_allowed || dying)) {
			if (wip->end_firing_snd.isValid() && end_snd_played != wip->end_firing_snd) {
				if (objp == Player_obj)
					snd_play(gamesnd_get_game_sound(wip->end_firing_snd));
				else
					snd_play_3d(gamesnd_get_game_sound(wip->end_firing_snd), &objp->pos, &View_position);

				end_snd_played = wip->end_firing_snd;
			}

			if (swp->firing_loop_sounds[i] >= 0) {
				obj_snd_delete(objp, swp->firing_loop_sounds[i]);
			}
			swp->firing_loop_sounds[i] = -1;
		}
	}
}

// This was previously part of obj_move_call_physics(), but secondary_point_reload_pct is only used for rendering and has nothing to do with physics at all.
void update_reload_percent(ship *shipp, float frametime)
{
	if (shipp->weapons.num_secondary_banks > 0) {
		polymodel *pm = model_get(Ship_info[shipp->ship_info_index].model_num);
		Assertion( pm != nullptr, "No polymodel found for ship %s", Ship_info[shipp->ship_info_index].name );
		Assertion( pm->missile_banks != nullptr, "Ship %s has %d secondary banks, but no missile banks could be found.\n", Ship_info[shipp->ship_info_index].name, shipp->weapons.num_secondary_banks );

		for (int i = 0; i < shipp->weapons.num_secondary_banks; i++) {
			//if there are no missles left don't bother
			if (!ship_secondary_has_ammo(&shipp->weapons, i))
				continue;

			int points = pm->missile_banks[i].num_slots;
			int missles_left = shipp->weapons.secondary_bank_ammo[i];
			int next_point = shipp->weapons.secondary_next_slot[i];
			float fire_wait = Weapon_info[shipp->weapons.secondary_bank_weapons[i]].fire_wait;
			float reload_time = (fire_wait == 0.0f) ? 1.0f : 1.0f / fire_wait;

			//ok so...we want to move up missles but only if there is a missle there to be moved up
			//there is a missle behind next_point, and how ever many missles there are left after that

			if (points > missles_left) {
				//there are more slots than missles left, so not all of the slots will have missles drawn on them
				for (int k = next_point; k < next_point+missles_left; k ++) {
					float &s_pct = shipp->secondary_point_reload_pct.get(i, k % points);
					if (s_pct < 1.0)
						s_pct += reload_time * frametime;
					if (s_pct > 1.0)
						s_pct = 1.0f;
				}
			} else {
				//we don't have to worry about such things
				for (int k = 0; k < points; k++) {
					float &s_pct = shipp->secondary_point_reload_pct.get(i, k);
					if (s_pct < 1.0)
						s_pct += reload_time * frametime;
					if (s_pct > 1.0)
						s_pct = 1.0f;
				}
			}
		}
	}
}

/**
 * Player ship uses this code, but does a quick out after doing a few things.
 * 
 * When adding code to this function, decide whether or not a client in a multiplayer game
 * needs to execute the code you are adding.  Code which moves things, creates things, etc
 * probably doesn't need to be called.  If you don't know -- find Allender!!!
 */
void ship_process_post(object * obj, float frametime)
{
	int	num;
	ship	*shipp;
	ship_info *sip;

	if(obj->type != OBJ_SHIP){
		nprintf(("General","Ignoring non-ship object in ship_process_post()\n"));
		return;
	}

	MONITOR_INC( NumShips, 1 );	

	num = obj->instance;
	Assert( num >= 0 && num < MAX_SHIPS);
	Assert( obj->type == OBJ_SHIP );
	Assert( Ships[num].objnum == OBJ_INDEX(obj));	

	shipp = &Ships[num];

	sip = &Ship_info[shipp->ship_info_index];

	shipp->shield_hits = 0;

	update_ets(obj, frametime);

	afterburners_update(obj, frametime);

	ship_subsys_disrupted_maybe_check(shipp);

	update_firing_sounds(obj, shipp);

	update_reload_percent(shipp, frametime);

	ship_dying_frame(obj, num);

	ship_move_ets_transfer_buffers(shipp, obj, frametime);

	ship_do_thruster_sounds(obj);

	// AL 1-6-98: record the initial ammo counts for ships, which is used as the max limit for rearming
	// Goober5000 - added ballistics support
	if ( !(shipp->flags[Ship_Flags::Ammo_count_recorded]) )
	{
		int max_missiles;
		for ( int i=0; i<MAX_SHIP_SECONDARY_BANKS; i++ ) {
			if ( red_alert_mission() )
			{
				max_missiles = get_max_ammo_count_for_bank(shipp->ship_info_index, i, shipp->weapons.secondary_bank_weapons[i]);
				shipp->weapons.secondary_bank_start_ammo[i] = max_missiles;
			}
			else
			{
				shipp->weapons.secondary_bank_start_ammo[i] = shipp->weapons.secondary_bank_ammo[i];
			}
		}

		for ( int i=0; i<MAX_SHIP_PRIMARY_BANKS; i++ )
		{
			if ( red_alert_mission() )
			{
				max_missiles = get_max_ammo_count_for_primary_bank(shipp->ship_info_index, i, shipp->weapons.primary_bank_weapons[i]);
				shipp->weapons.primary_bank_start_ammo[i] = max_missiles;
			}
			else
			{
				shipp->weapons.primary_bank_start_ammo[i] = shipp->weapons.primary_bank_ammo[i];
			}
		}
		
		shipp->flags.set(Ship_Flags::Ammo_count_recorded);
	}

	if(!(Game_mode & GM_STANDALONE_SERVER)) {
		// Plot ship on the radar.  What about multiplayer ships?
		if ( obj != Player_obj && Game_mode & GM_IN_MISSION )			// don't plot myself.
			radar_plot_object( obj );

		// MWA -- move the spark code to before the check for multiplayer master
		//	Do ship sparks.  Don't do sparks on my ship (since I cannot see it).  This
		// code will do sparks on other ships in multiplayer though.
		// JAS: Actually in external view, you can see sparks, so I don't do sparks
		// on the Viewer_obj, not Player_obj.
		if ( (obj != Viewer_obj) && timestamp_elapsed(Ships[num].next_hit_spark) )	{
			shipfx_emit_spark(num,-1);	// -1 means choose random spark location
		}

		if ( obj != Viewer_obj )	{
			shipfx_do_lightning_arcs_frame( shipp );
		}

		// JAS - flicker the thruster bitmaps
		ship_do_thruster_frame(shipp,obj,frametime);		
	}

	ship_auto_repair_frame(num, frametime);

	shipfx_do_lightning_frame(shipp);

	// if the ship has an EMP effect active, process it
	emp_process_ship(shipp);	

	// call the contrail system
	ct_ship_process(shipp);

	// process engine wash
	void engine_wash_ship_process(ship *shipp);
	engine_wash_ship_process(shipp);

	// update TAG info
	if(shipp->tag_left > 0.0f){
		shipp->tag_left -= flFrametime;
		if(shipp->tag_left <= 0.000001f){
			shipp->tag_left = -1.0f;

			mprintf(("Killing TAG for %s\n", shipp->ship_name));
		}
	}
	
	// update level 2 TAG info
	if(shipp->level2_tag_left > 0.0f){
		shipp->level2_tag_left -= flFrametime;
		if(shipp->level2_tag_left <= 0.000001f){
			shipp->level2_tag_left = -1.0f;

			mprintf(("Killing level 2 TAG for %s\n", shipp->ship_name));
		}
	}
	
	if ( shipp->is_arriving(ship::warpstage::BOTH, true) && Ai_info[shipp->ai_index].mode != AIM_BAY_EMERGE )	{
		// JAS -- if the ship is warping in, just move it forward at a speed
		// fast enough to move 2x its radius in SHIP_WARP_TIME seconds.
		shipfx_warpin_frame( obj, frametime );
	} else if ( shipp->flags[Ship_Flags::Depart_warp] ) {
		// JAS -- if the ship is warping out, just move it forward at a speed
		// fast enough to move 2x its radius in SHIP_WARP_TIME seconds.
		shipfx_warpout_frame( obj, frametime );
	} 

	// update radar status of the ship
	ship_radar_process(obj, shipp, sip);

	// update fof cooldowns
	for (int i = 0; i < shipp->weapons.num_primary_banks; i++) {
		if (shipp->weapons.primary_bank_weapons[i] < 0)
			continue;

		weapon_info* wip = &Weapon_info[shipp->weapons.primary_bank_weapons[i]];
		
		if (wip->fof_reset_rate > 0.0f) {
			shipp->weapons.primary_bank_fof_cooldown[i] -= wip->fof_reset_rate * frametime;
			CLAMP(shipp->weapons.primary_bank_fof_cooldown[i], 0.0f, 1.0f);
		}
	}

	if ( (!(shipp->is_arriving()) || (Ai_info[shipp->ai_index].mode == AIM_BAY_EMERGE)
		|| ((Warp_params[shipp->warpin_params_index].warp_type == WT_IN_PLACE_ANIM) && (shipp->flags[Ship_Flags::Arriving_stage_2])) )
		&&	!(shipp->flags[Ship_Flags::Depart_warp]))
	{
		//	Do AI.

		// Cyborg: Mutliplayer clients do not do AI locally.
		if ( MULTIPLAYER_CLIENT ) {

			// But to enable turrets movement on clients, we need to process them here before bailing
			// And this section will do it if Framerate_independent_turning is off
			if (!Framerate_independent_turning)
				ai_process_subobjects(OBJ_INDEX(obj));
			
			return;
		}

		// MWA -- moved the code to maybe fire swarm missiles to after the check for
		// multiplayer master.  Only single player and multi server needs to do this code
		// this code might call ship_fire_secondary which will send the fire packets
		swarm_maybe_fire_missile(num);

		// maybe fire turret swarm missiles
		void turret_swarm_maybe_fire_missile(int num);
		turret_swarm_maybe_fire_missile(num);

		// maybe fire a corkscrew missile (just like swarmers)
		cscrew_maybe_fire_missile(num);

		if (obj == Player_obj) {
			ship_check_player_distance();
		}

		// If Framerate_independent_turning is true this is evaluated in ship_process_pre()
		if (!Framerate_independent_turning)
			ship_evaluate_ai(obj, frametime);
	}
}


/**
 * Set the ship level weapons based on the information contained in the ship info.
 * 
 * Weapon assignments are checked against the model to ensure the models
 * and the ship info weapon data are in synch.
 */
static void ship_set_default_weapons(ship *shipp, ship_info *sip)
{
	int			i;
	polymodel	*pm;
	ship_weapon *swp = &shipp->weapons;
	weapon_info *wip;

	//	Copy primary and secondary weapons from ship_info to ship.
	//	Later, this will happen in the weapon loadout screen.
	for (i=0; i < MAX_SHIP_PRIMARY_BANKS; i++){
		swp->primary_bank_weapons[i] = sip->primary_bank_weapons[i];
		swp->primary_bank_slot_count[i] = 1; // RSAXVC DYN LINK CODE
	}

	for (i=0; i < MAX_SHIP_SECONDARY_BANKS; i++){
		swp->secondary_bank_weapons[i] = sip->secondary_bank_weapons[i];
	}

	// Copy the number of primary and secondary banks to ship, and verify that
	// model is in synch
	pm = model_get( sip->model_num );

	// Primary banks
	if ( pm->n_guns > sip->num_primary_banks ) {
		Assert(pm->n_guns <= MAX_SHIP_PRIMARY_BANKS);
		Error(LOCATION, "There are %d primary banks in the model file,\nbut only %d primary banks specified for %s.\nThis must be fixed, as it will cause crashes.\n", pm->n_guns, sip->num_primary_banks, sip->name);
		for ( i = sip->num_primary_banks; i < pm->n_guns; i++ ) {
			// Make unspecified weapon for bank be a laser
			for (const auto &weapon_id : Player_weapon_precedence) {
				if (Weapon_info[weapon_id].is_primary()) {
					swp->primary_bank_weapons[i] = weapon_id;
					break;
				}
			}
			Assert(swp->primary_bank_weapons[i] >= 0);
		}
		sip->num_primary_banks = pm->n_guns;
	}
	else if ( pm->n_guns < sip->num_primary_banks ) {
		Warning(LOCATION, "There are %d primary banks specified for %s\nbut only %d primary banks in the model\n", sip->num_primary_banks, sip->name, pm->n_guns);
		sip->num_primary_banks = pm->n_guns;
	}

	// Secondary banks
	if ( pm->n_missiles > sip->num_secondary_banks ) {
		Assert(pm->n_missiles <= MAX_SHIP_SECONDARY_BANKS);
		Error(LOCATION, "There are %d secondary banks in the model file,\nbut only %d secondary banks specified for %s.\nThis must be fixed, as it will cause crashes.\n", pm->n_missiles, sip->num_secondary_banks, sip->name);
		for ( i = sip->num_secondary_banks; i < pm->n_missiles; i++ ) {
			// Make unspecified weapon for bank be a missile
			for (const auto &weapon_id : Player_weapon_precedence) {
				if (Weapon_info[weapon_id].is_secondary()) {
					swp->secondary_bank_weapons[i] = weapon_id;
					break;
				}
			}
			Assert(swp->secondary_bank_weapons[i] >= 0);
		}
		sip->num_secondary_banks = pm->n_missiles;
	}
	else if ( pm->n_missiles < sip->num_secondary_banks ) {
		Warning(LOCATION, "There are %d secondary banks specified for %s,\n but only %d secondary banks in the model.\n", sip->num_secondary_banks, sip->name, pm->n_missiles);
		sip->num_secondary_banks = pm->n_missiles;
	}

	// added ballistic primary support - Goober5000
	swp->num_primary_banks = sip->num_primary_banks;
	for ( i = 0; i < swp->num_primary_banks; i++ )
	{
		wip = &Weapon_info[swp->primary_bank_weapons[i]];

		if ( wip->wi_flags[Weapon::Info_Flags::Ballistic] )
		{
			if (Fred_running){
				swp->primary_bank_ammo[i] = 100;
			}
			else
			{
				float capacity, size;
				capacity = (float) sip->primary_bank_ammo_capacity[i];
				size = (float) wip->cargo_size;
				swp->primary_bank_ammo[i] = (int)std::lround(capacity / size);
				swp->primary_bank_start_ammo[i] = swp->primary_bank_ammo[i];
			}
		}

		swp->primary_bank_capacity[i] = sip->primary_bank_ammo_capacity[i];

		swp->primary_firepoint_next_to_fire_index[i] = 0;
		auto &fpi = swp->primary_firepoint_indices[i];
		fpi.clear();
		for (int fp = 0; fp < pm->gun_banks[i].num_slots; fp++) {
			fpi.push_back(fp);
		}
		std::random_device rd;
		std::shuffle(fpi.begin(), fpi.end(), std::mt19937(rd()));
	}

	swp->num_secondary_banks = sip->num_secondary_banks;
	for ( i = 0; i < swp->num_secondary_banks; i++ ) {
		if (Weapon_info[swp->secondary_bank_weapons[i]].wi_flags[Weapon::Info_Flags::SecondaryNoAmmo]) {
			swp->secondary_bank_ammo[i] = 0;
			swp->secondary_bank_start_ammo[i] = swp->secondary_bank_ammo[i];
			swp->secondary_bank_capacity[i] = sip->secondary_bank_ammo_capacity[i];
		}

		if (Fred_running){
			swp->secondary_bank_ammo[i] = 100;
		} else {
			wip = &Weapon_info[swp->secondary_bank_weapons[i]];
			float size = (float) wip->cargo_size;
			swp->secondary_bank_ammo[i] = fl2i(sip->secondary_bank_ammo_capacity[i]/size);
			// Karajorma - Support ships will use the wrong values if we don't set this. 
			swp->secondary_bank_start_ammo[i] = swp->secondary_bank_ammo[i];
		}

		swp->secondary_bank_capacity[i] = sip->secondary_bank_ammo_capacity[i];
	}

	for ( i = 0; i < MAX_SHIP_PRIMARY_BANKS; i++ ){
		swp->next_primary_fire_stamp[i] = timestamp(0);
		swp->last_primary_fire_stamp[i] = -1;
		swp->burst_counter[i] = 0;
		swp->burst_seed[i] = Random::next();
		swp->last_primary_fire_sound_stamp[i] = timestamp(0);
	}

	for ( i = 0; i < MAX_SHIP_SECONDARY_BANKS; i++ ){
		swp->next_secondary_fire_stamp[i] = timestamp(0);
		swp->last_secondary_fire_stamp[i] = -1;
		swp->burst_counter[i + MAX_SHIP_PRIMARY_BANKS] = 0;
		swp->burst_seed[i + MAX_SHIP_PRIMARY_BANKS] = Random::next();
	}

	//Countermeasures
	shipp->current_cmeasure = sip->cmeasure_type;
}


/**
 * Faster version of ship_check_collision that does not do checking at the polygon
 * level.  Just checks to see if a vector will intersect a sphere.
 */
int ship_check_collision_fast( object * obj, object * other_obj, vec3d * hitpos)
{
	Assert( obj->type == OBJ_SHIP );
	Assert( obj->instance >= 0 );

	int num = obj->instance;

	mc_info mc;
	mc.model_instance_num = Ships[num].model_instance_num;
	mc.model_num = Ship_info[Ships[num].ship_info_index].model_num;	// Fill in the model to check
	mc.orient = &obj->orient;					// The object's orient
	mc.pos = &obj->pos;							// The object's position
	mc.p0 = &other_obj->last_pos;			// Point 1 of ray to check
	mc.p1 = &other_obj->pos;					// Point 2 of ray to check
	mc.flags = MC_ONLY_SPHERE;				// flags

	model_collide(&mc);
	if (mc.num_hits)
		*hitpos = mc.hit_point_world;

	return mc.num_hits;
}

/**
 * Ensure create time for ship is unique
 */
static void ship_make_create_time_unique(ship *shipp)
{
	static int last_smctu_initial_time = -1;
	static int last_smctu_final_time = -1;
	int		sanity_counter = 0, collision;
	ship		*compare_shipp;
	ship_obj	*so;
	uint		new_create_time;

	new_create_time = shipp->create_time;

	while (1) {

		collision = 0;

		for ( so = GET_FIRST(&Ship_obj_list); so != END_OF_LIST(&Ship_obj_list); so = GET_NEXT(so) ) {
			// we probably don't need to skip should-be-dead ships
			compare_shipp = &Ships[Objects[so->objnum].instance];

			if ( compare_shipp == shipp ) {
				continue;
			}

			if ( compare_shipp->create_time == new_create_time )
			{
				if((unsigned int)sanity_counter == 0 && (unsigned int)last_smctu_initial_time == shipp->create_time)
				{
					//WMC: If we're creating a whole bunch of ships at once, we can
					//shortcut this process by looking at the last call to this function
					//This fixes a bug when more than 50 ships are created at once.
					new_create_time = last_smctu_final_time + 1;
				}
				else
				{
					new_create_time++;
				}
				collision = 1;
				break;
			}
		}

		if ( !collision ) {
			last_smctu_initial_time = shipp->create_time;
			last_smctu_final_time = new_create_time;
			shipp->create_time = new_create_time;
			break;
		}

		if ( sanity_counter++ > MAX_SHIPS ) {
			Int3();
			break;
		}
	}
}

int	Ship_subsys_hwm = 0;

static void show_ship_subsys_count()
{
	int count = 0;

	for (auto so: list_range(&Ship_obj_list)) {
		auto objp = &Objects[so->objnum];
		if (objp->flags[Object::Object_Flags::Should_be_dead])
			continue;

		count += Ship_info[Ships[objp->instance].ship_info_index].n_subsystems;
	}

	if (count > Ship_subsys_hwm) {
		Ship_subsys_hwm = count;
	}
}

static void ship_init_afterburners(ship *shipp)
{
	Assert( shipp );

	shipp->ab_count = 0;

	if (shipp->ship_info_index < 0) {
		Int3();
		return;
	}

	ship_info *sip = &Ship_info[shipp->ship_info_index];
	Assert( sip->model_num >= 0 );
	polymodel *pm = model_get(sip->model_num);
	Assert( pm != NULL );

	if ( !(sip->flags[Ship::Info_Flags::Afterburner]) ) {
		return;
	}

	if (sip->afterburner_trail.bitmap_id < 0) {
		return;
	}

	for (int i = 0; i < pm->n_thrusters; i++) {
		thruster_bank *bank = &pm->thrusters[i];

		for (int j = 0; j < bank->num_points; j++) {
			// this means you've reached the max # of AB trails for a ship
			if (shipp->ab_count >= MAX_SHIP_CONTRAILS) {
				Int3();
				break;
			}

			trail_info *ci = &shipp->ab_info[shipp->ab_count];

			if (bank->points[j].norm.xyz.z > -0.5f) {
				continue; // only make ab trails for thrusters that are pointing backwards
			}

			ci->pt = bank->points[j].pnt; //offset

			ci->w_start = bank->points[j].radius * sip->afterburner_trail_width_factor;	// width * table loaded width factor
			ci->w_end = 0.05f; //end width

			ci->a_start = sip->afterburner_trail_alpha_factor; // start alpha  * table loaded alpha factor
			ci->a_end = sip->afterburner_trail_alpha_end_factor; //end alpha
			ci->a_decay_exponent = sip->afterburner_trail_alpha_decay_exponent;

			ci->max_life = sip->afterburner_trail_life;	// table loaded max life
			ci->spew_duration = 60;	//spew time???
			ci->spread = sip->afterburner_trail_spread; // table loaded spread speed

			ci->n_fade_out_sections = sip->afterburner_trail_faded_out_sections; // initial fade out

			ci->texture.bitmap_id = sip->afterburner_trail.bitmap_id; // table loaded bitmap used on this ships burner trails
			ci->texture_stretch = sip->afterburner_trail_tex_stretch;

			nprintf(("AB TRAIL", "AB trail point #%d made for '%s'\n", shipp->ab_count, shipp->ship_name));

			shipp->ab_count++;
		}
	}
}

/**
 * Returns object index of ship.
 * @return -1 means failed.
 */
int ship_create(matrix* orient, vec3d* pos, int ship_type, const char* ship_name, bool standalone_ship)
{
	int			i, shipnum, objnum, j, k, t;
	ship_info	*sip;
	ship			*shipp;

	t = ship_get_num_ships();
	
	// The following check caps the number of ships that can be created.  Because Fred needs
	// to create all the ships, regardless of when they arrive/depart, it needs a higher
	// limit than FreeSpace.  On release, however, we will reduce it, thus FreeSpace needs
	// to check against what this limit will be, otherwise testing the missions before
	// release could work fine, yet not work anymore once a release build is made.
	if (Fred_running) {
		if (t >= MAX_SHIPS)
			return -1;

	} else {
		if (t >= SHIPS_LIMIT) {
			Error(LOCATION, XSTR("There is a limit of %d ships in the mission at once.  Please be sure that you do not have more than %d ships present in the mission at the same time.", 1495), SHIPS_LIMIT, SHIPS_LIMIT );
			return -1;
		}
	}

	for (shipnum=0; shipnum<MAX_SHIPS; shipnum++){
		if (Ships[shipnum].objnum == -1){
			break;
		}
	}

	if (shipnum == MAX_SHIPS){
		return -1;
	}

	Assertion((ship_type >= 0) && (ship_type < ship_info_size()), "Invalid ship_type %d passed to ship_create() (expected value in the range 0-%d)\n", ship_type, ship_info_size()-1);
	sip = &(Ship_info[ship_type]);
	shipp = &Ships[shipnum];
	shipp->clear();
	shipp->orders_allowed_against = ship_get_default_orders_against();

	if (!VALID_FNAME(sip->pof_file))
	{
		Error(LOCATION, "Cannot create ship %s; pof file is not valid", sip->name);
		return -1;
	}
	sip->model_num = model_load(sip->pof_file, sip);		// use the highest detail level

	if(VALID_FNAME(sip->cockpit_pof_file))
	{
		sip->cockpit_model_num = model_load(sip->cockpit_pof_file);
	}

	// maybe load an optional hud target model
	if(VALID_FNAME(sip->pof_file_hud)){
		// check to see if a "real" ship uses this model. if so, load it up for him so that subsystems are setup properly
		for(auto it = Ship_info.begin(); it != Ship_info.end(); ++it){
			if(!stricmp(it->pof_file, sip->pof_file_hud)){
				it->model_num = model_load(it->pof_file, &*it);
			}
		}

		// mow load it for me with no subsystems
		sip->model_num_hud = model_load(sip->pof_file_hud);
	}

	if (VALID_FNAME(sip->generic_debris_pof_file)) {
		sip->generic_debris_model_num = model_load(sip->generic_debris_pof_file);
		if (sip->generic_debris_model_num >= 0) {
			polymodel* pm = model_get(sip->generic_debris_model_num);
			sip->generic_debris_num_submodels = pm->n_models;
		}
	}

	polymodel *pm = model_get(sip->model_num);

	ship_copy_subsystem_fixup(sip);
	show_ship_subsys_count();

	if ( sip->num_detail_levels != pm->n_detail_levels )
	{
		if ( !Is_standalone )
		{
			// just log to file for standalone servers
			Warning(LOCATION, "For ship '%s', detail level\nmismatch. Table has %d,\nPOF has %d.", sip->name, sip->num_detail_levels, pm->n_detail_levels );
		}
		else
		{
			nprintf(("Warning",  "For ship '%s', detail level mismatch. Table has %d, POF has %d.", sip->name, sip->num_detail_levels, pm->n_detail_levels ));
		}	
	}		
	for ( i=0; i<pm->n_detail_levels; i++ )
		pm->detail_depth[i] = (i < sip->num_detail_levels) ? i2fl(sip->detail_distance[i]) : 0.0f;

	flagset<Object::Object_Flags> default_ship_object_flags;
	default_ship_object_flags.set(Object::Object_Flags::Renders);
	default_ship_object_flags.set(Object::Object_Flags::Physics);
	// JAS: Nav buoys don't need to do collisions!
	// G5K: Corrected to apply specifically for ships with the no-collide flag.  (In retail, navbuoys already have this flag, so this doesn't break anything.)
	default_ship_object_flags.set(Object::Object_Flags::Collides, !sip->flags[Ship::Info_Flags::No_collide]);

	objnum = obj_create(OBJ_SHIP, -1, shipnum, orient, pos, model_get_radius(sip->model_num), default_ship_object_flags);
	Assert( objnum >= 0 );

	// Init multiplayer interpolation info
	Interp_info[objnum].reset(sip->n_subsystems); 

	shipp->model_instance_num = model_create_instance(objnum, sip->model_num);

	shipp->ai_index = ai_get_slot(shipnum);
	Assert( shipp->ai_index >= 0 );

	// Goober5000 - if no ship name specified, or if specified ship already exists,
	// or if specified ship has exited, use a default name
	// Cyborg17 - The final check here was supposed to prevent duplicate names from being on the mission log and causing chaos,
	// but it breaks multi, so there will just be a warning on debug instead.
	if ((ship_name == nullptr) || (ship_name_lookup(ship_name) >= 0) /*|| (ship_find_exited_ship_by_name(ship_name) >= 0)*/) {
		// regular name, regular suffix
		char base_name[NAME_LENGTH];
		char suffix[NAME_LENGTH];
		strcpy_s(base_name, Ship_info[ship_type].name);
		sprintf(suffix, NOX(" %d"), shipnum);

		// default names shouldn't have a hashed suffix
		end_string_at_first_hash_symbol(base_name);

		// start building name
		strcpy_s(shipp->ship_name, base_name);

		// if generated name will be longer than allowable name, truncate the class section of the name by the overflow
		int char_overflow = static_cast<int>(strlen(base_name) + strlen(suffix)) - (NAME_LENGTH - 1);
		if (char_overflow > 0) {
			shipp->ship_name[strlen(base_name) - static_cast<size_t>(char_overflow)] = '\0';
		}

		// complete building the name by adding suffix number
		strcat_s(shipp->ship_name, suffix);

	} else {
		if (ship_find_exited_ship_by_name(ship_name) >= 0 && !(Game_mode & GM_MULTIPLAYER)) {
			Warning(LOCATION, "Newly-arrived ship %s has been given the same name as a ship previously destroyed in-mission. This can cause unpredictable SEXP behavior. Correct your mission file or scripts to prevent duplicates.", ship_name);
		}
		strcpy_s(shipp->ship_name, ship_name);
	}

	ship_set_default_weapons(shipp, sip);	//	Moved up here because ship_set requires that weapon info be valid.  MK, 4/28/98
	ship_set(shipnum, objnum, ship_type);

	for (auto& fpu : shipp->weapons.primary_firepoint_next_to_fire_index) {
		fpu = 0;
	}

	init_ai_object(objnum);
	ai_clear_ship_goals( &Ai_info[shipp->ai_index] );		// only do this one here.  Can't do it in init_ai because it might wipe out goals in mission file

	// Bump the object radius to ensure that collision detection works right
	// even when spread shields extend outside the model's natural radius
	if (sip->flags[Ship::Info_Flags::Auto_spread_shields]) {
		Objects[objnum].radius += sip->auto_shield_spread;
	}

	// allocate memory for keeping glow point bank status (enabled/disabled)
	{
		bool val = true; // default value, enabled

		if (pm->n_glow_point_banks)
			shipp->glow_point_bank_active.resize( pm->n_glow_point_banks, val );

		// set any default off banks to off
		for (int bank = 0; bank < pm->n_glow_point_banks; bank++) {
			glow_point_bank_override* gpo = nullptr;

			SCP_unordered_map<int, void*>::iterator gpoi = sip->glowpoint_bank_override_map.find(bank);
			if (gpoi != sip->glowpoint_bank_override_map.end()) {
				gpo = (glow_point_bank_override*)sip->glowpoint_bank_override_map[bank];
			}

			if (gpo) {
				if (gpo->default_off) {
					shipp->glow_point_bank_active[bank] = false;
				}
			}
		}
	}

	// fix up references into paths for this ship's model to point to a ship_subsys entry instead
	// of a submodel index.  The ship_subsys entry should be the same for *all* instances of the
	// same ship.
	if (!(sip->flags[Ship::Info_Flags::Path_fixup]))
	{
		for ( i = 0; i < pm->n_paths; i++ )
		{
			for ( j = 0; j < pm->paths[i].nverts; j++ )
			{
				for ( k = 0; k < pm->paths[i].verts[j].nturrets; k++ )
				{
					int ptindex = pm->paths[i].verts[j].turret_ids[k];		// this index is a submodel number (ala bspgen)

					// iterate through the ship_subsystems looking for an id that matches
					int index = 0;
					bool found = false;
					for (auto ss: list_range(&shipp->subsys_list)) {
						if ( ss->system_info->subobj_num == ptindex ) {			// when these are equal, fix up the ref
							pm->paths[i].verts[j].turret_ids[k] = index;				// in path structure to index a ship_subsys
							found = true;
							break;											
						}
						index++;
					}

					if (!found)
						Warning(LOCATION, "Couldn't fix up turret indices in spline path\n\nModel: %s\nPath: %s\nVertex: %d\nTurret model id:%d\n\nThis probably means that the turret was not specified in the ship table(s).", sip->pof_file, pm->paths[i].name, j, ptindex );
				}
			}
		}
		sip->flags.set(Ship::Info_Flags::Path_fixup);
	}

	// this used to be done in parse_create_object_sub
	if (!Fred_running)
		ship_assign_sound(shipp);

	// first try at ABtrails -Bobboau
	ship_init_afterburners(shipp);

	// call the contrail system
	ct_ship_create(shipp);

	animation::anim_set_initial_states(shipp);

	// Add this ship to Ship_obj_list, if it is *not* the standalone ship.  That can cause big time bugs.
	if (!standalone_ship){
		shipp->ship_list_index = ship_obj_list_add(objnum);
	}

	// Goober5000 - update the ship registry
	// (since scripts and sexps can create ships, the entry may not yet exist)
	auto ship_it = Ship_registry_map.find(shipp->ship_name);
	if (ship_it == Ship_registry_map.end())
	{
		ship_registry_entry entry(shipp->ship_name);
		entry.status = ShipStatus::PRESENT;
		entry.objnum = objnum;
		entry.shipnum = shipnum;

		Ship_registry.push_back(entry);
		Ship_registry_map[shipp->ship_name] = static_cast<int>(Ship_registry.size() - 1);
	}
	else
	{
		auto entry = &Ship_registry[ship_it->second];
		entry->status = ShipStatus::PRESENT;
		entry->objnum = objnum;
		entry->shipnum = shipnum;
	}
	
	// Start up stracking for this ship in multi.
	if (Game_mode & (GM_MULTIPLAYER)) {
		multi_rollback_ship_record_add_ship(objnum);
	}

	// Set time when ship is created
	shipp->create_time = timer_get_milliseconds();

	ship_make_create_time_unique(shipp);

	shipp->time_created = Missiontime;

	return objnum;
}

/**
 * Change the ship model for a ship to that for ship class 'ship_type'
 *
 * @param n			index of ship in ::Ships[] array
 * @param ship_type	ship class (index into ::Ship_info vector)
 */
static void ship_model_change(int n, int ship_type)
{
	int			i;
	ship_info	*sip;
	ship			*sp;
	polymodel * pm;
	polymodel_instance * pmi;
	object *objp;

	Assert( n >= 0 && n < MAX_SHIPS );
	sp = &Ships[n];
	sip = &(Ship_info[ship_type]);
	objp = &Objects[sp->objnum];
	pmi = model_get_instance(sp->model_instance_num);

	// get new model
	if (sip->model_num == -1) {
		sip->model_num = model_load(sip->pof_file, sip);
	}

	if ( sip->cockpit_model_num == -1 ) {
		if ( VALID_FNAME(sip->cockpit_pof_file) ) {
			sip->cockpit_model_num = model_load(sip->cockpit_pof_file);
		}
	}

	pm = model_get(sip->model_num);
	Objects[sp->objnum].radius = model_get_radius(pm->id);

	// page in nondims in game
	if ( !Fred_running )
		model_page_in_textures(sip->model_num, ship_type);

	// allocate memory for keeping glow point bank status (enabled/disabled)
	{
		bool val = true; // default value, enabled

		// clear out any old gpb's first, then add new ones if needed
		sp->glow_point_bank_active.clear();

		if (pm->n_glow_point_banks)
			sp->glow_point_bank_active.resize( pm->n_glow_point_banks, val );
		
		// set any default off banks to off
		for (int bank = 0; bank < pm->n_glow_point_banks; bank++) {
			glow_point_bank_override* gpo = nullptr;

			SCP_unordered_map<int, void*>::iterator gpoi = sip->glowpoint_bank_override_map.find(bank);
			if (gpoi != sip->glowpoint_bank_override_map.end()) {
				gpo = (glow_point_bank_override*)sip->glowpoint_bank_override_map[bank];
			}

			if (gpo) {
				if (gpo->default_off) {
					sp->glow_point_bank_active[bank] = false;
				}
			}
		}
	}

	ship_copy_subsystem_fixup(sip);

	if ( sip->num_detail_levels != pm->n_detail_levels )
	{
		if ( !Is_standalone )
		{
			// just log to file for standalone servers
			Warning(LOCATION, "For ship '%s', detail level\nmismatch. Table has %d,\nPOF has %d.", sip->name, sip->num_detail_levels, pm->n_detail_levels );
		}
		else
		{
			nprintf(("Warning",  "For ship '%s', detail level mismatch. Table has %d, POF has %d.", sip->name, sip->num_detail_levels, pm->n_detail_levels ));
		}
	}	
	for ( i=0; i<pm->n_detail_levels; i++ )
		pm->detail_depth[i] = (i < sip->num_detail_levels) ? i2fl(sip->detail_distance[i]) : 0.0f;

	if (sip->flags[Ship::Info_Flags::Model_point_shields]) {
		objp->shield_quadrant.resize(pm->shield_points.size());
		sp->shield_points = pm->shield_points;
	} else {
		objp->shield_quadrant.resize(DEFAULT_SHIELD_SECTIONS);
	}

	// reset texture animations
	sp->base_texture_anim_timestamp = _timestamp();

	for (int bank_i = 0; bank_i < pm->n_guns; bank_i++) {
		sp->weapons.primary_firepoint_next_to_fire_index[bank_i] = 0;
		auto &fpi = sp->weapons.primary_firepoint_indices[bank_i];
		fpi.clear();
		for (int fp = 0; fp < pm->gun_banks[bank_i].num_slots; fp++) {
			fpi.push_back(fp);
		}
		std::random_device rd;
		std::shuffle(fpi.begin(), fpi.end(), std::mt19937(rd()));
	}

	model_delete_instance(sp->model_instance_num);

	// create new model instance data
	// note: this is needed for both subsystem stuff and submodel animation stuff
	sp->model_instance_num = model_create_instance(OBJ_INDEX(objp), sip->model_num);
	pmi = model_get_instance(sp->model_instance_num);

	// Goober5000 - deal with texture replacement by re-applying the same code we used during parsing
	// wookieejedi - replacement textures are loaded in mission parse, so need to load any new textures here
	// Lafiel - this now has to happen last, as the texture replacement stuff is stored in the pmi
	if ( !sip->replacement_textures.empty() ) {

		// clear and reset replacement textures because the new positions may be different
		pmi->texture_replace = make_shared<model_texture_replace>();
		auto& texture_replace_deref = *pmi->texture_replace;

		// now fill them in according to texture name
		for (const auto& tr : sip->replacement_textures) {
			// look for textures
			for (auto j = 0; j < pm->n_textures; j++) {

				texture_map* tmap = &pm->maps[j];
				int tnum = tmap->FindTexture(tr.old_texture);

				if (tnum > -1) {
					// load new texture
					int new_tex = bm_load_either(tr.new_texture);
					if (new_tex > -1) {
						texture_replace_deref[j * TM_NUM_TYPES + tnum] = new_tex;
					}
				}
			}
		}
	}
}

/**
 * Change the ship class on a ship, and changing all required information
 * for consistency (ie textures, subsystems, weapons, physics)
 *
 * @param n			index of ship in ::Ships[] array
 * @param ship_type	ship class (index into ::Ship_info vector)
 * @param by_sexp	SEXP reference
 */
void change_ship_type(int n, int ship_type, int by_sexp)
{
	int i;
	ship_info	*sip;
	ship_info	*sip_orig;
	ship			*sp;
	ship_weapon *swp;
	ship_subsys *ss;
	object		*objp;
	p_object	*p_objp;
	float hull_pct, shield_pct;
	physics_info ph_inf;

	Assert( n >= 0 && n < MAX_SHIPS );
	sp = &Ships[n];

	// do a quick out if we're already using the new ship class
	if (sp->ship_info_index == ship_type)
		return;

	int objnum = sp->objnum;
	auto ship_entry = ship_registry_get(sp->ship_name);

	swp = &sp->weapons;
	sip = &(Ship_info[ship_type]);
	sip_orig = &Ship_info[sp->ship_info_index];
	float orig_sp_max_shield_strength = sp->ship_max_shield_strength;
	objp = &Objects[objnum];
	p_objp = ship_entry ? ship_entry->p_objp_or_null() : nullptr;
	ph_inf = objp->phys_info;

	// if this ship is the wing leader, update the ship info index that the wing keeps track of.
	if (!Fred_running && p_objp != nullptr && p_objp->wingnum > -1 && p_objp->pos_in_wing == 0) {
		Wings[p_objp->wingnum].special_ship_ship_info_index = ship_type;
	}

	// MageKing17 - See if any AIs are doing anything with subsystems of this ship (targeting, goal to destroy)
	// keep track of those subsystems and transfer the target/goal if the subsystem still exists, delete otherwise

	SCP_list<int> target_matches;	// tells us to check this Ai_info index during subsystem traversal

	SCP_list<ivec3> subsystem_matches;	// Ai_info index, goal index (or -1 for targeted), subsystem index
	// not having a subsystem index at first is why we have the target_matches list:
	// while traversing the subsystem list, a match can be compared directly to the
	// subsystem object, and its index added to subsystem_matches.

	SCP_list<weapon*> homing_matches;	// any missiles locked on to a subsystem from this ship

	SCP_list<std::pair<weapon*, int>> homing_subsystem_matches;	// When homing_matches find matches, they put them here

	SCP_list<weapon*> weapon_turret_matches;	// projectiles that were fired from turrets on this ship

	SCP_list<std::pair<weapon*, int>> weapon_turret_subsystem_matches;

	SCP_list<int> last_targeted_matches;

	SCP_list<std::pair<int, int>> last_targeted_subsystem_matches;

	if (!(Fred_running) && (Game_mode & GM_IN_MISSION)) {	// Doing this effort only makes sense in the middle of a mission.
		// Delete ship sparks if the model changed
		if (sip_orig->model_num != sip->model_num) {
			memset(sp->sparks, 0, MAX_SHIP_SPARKS * sizeof(ship_spark));
			sp->num_sparks = 0;
		}

		for (i = 0; i < MAX_AI_INFO; i++) {
			if (Ai_info[i].shipnum > -1) {
				if (Ai_info[i].targeted_subsys && Ai_info[i].targeted_subsys->parent_objnum == objnum) {
					target_matches.push_back(i);
				}
				ai_goal* goals = Ai_info[i].goals;
				for (int j = 0; j < MAX_AI_GOALS; j++) {
					// POSSIBLE OPTIMIZATION: goals[0] should be the active goal, so we might be able to reuse target_subsys_parent for that instead of doing ship_name_lookup()
					if (goals[j].ai_mode == AI_GOAL_DESTROY_SUBSYSTEM && !(goals[j].flags[AI::Goal_Flags::Subsys_needs_fixup])) {	// If the subsystem name hasn't been parsed yet, we're fine.
						int sindex = ship_name_lookup(goals[j].target_name);
						if (sindex > -1 && Ships[sindex].objnum == objnum) {
							ivec3 temp = {i, j, goals[j].ai_submode};
							subsystem_matches.push_back(temp);
						}
					}
				}
			}
		}

		for (i = 0; i < MAX_WEAPONS; i++) {
			if (Weapons[i].objnum == -1) {
				continue;
			}
			weapon* wp = &Weapons[i];
			if (wp->homing_subsys && wp->homing_subsys->parent_objnum == objnum) {
				homing_matches.push_back(wp);
			}
			if (wp->turret_subsys && wp->turret_subsys->parent_objnum == objnum) {
				weapon_turret_matches.push_back(wp);
			}
		}

		for (i = 0; i < MAX_PLAYERS; i++) {
			if (sp->last_targeted_subobject[i]) {
				last_targeted_matches.push_back(i);
			}
		}
	}

	// Goober5000 - we can't copy the ship object because the tree of structs contains at least
	// one class without a copy constructor.  So let's just save the information we need.

	// these are wiped by ets_init_ship
	int orig_wep_rechg_idx = sp->weapon_recharge_index;
	int orig_shd_rechg_idx = sp->shield_recharge_index;
	int orig_eng_rechg_idx = sp->engine_recharge_index;


	// Goober5000 - maintain the original hull, shield, and subsystem percentages... gah
	// ...except when in FRED, because this stuff is handled in the missionparse/missionsave part. The E

	if (!Fred_running) {
		// hull
		if (sp->special_hitpoints) {
			hull_pct = objp->hull_strength / sp->ship_max_hull_strength; 
		} else {
			Assert( Ship_info[sp->ship_info_index].max_hull_strength > 0.0f );
			hull_pct = objp->hull_strength / Ship_info[sp->ship_info_index].max_hull_strength;
		}

		// extra check
		CLAMP(hull_pct, 0.01f, 1.0f);

		// shield
		if (sp->special_shield > 0) {
			shield_pct = shield_get_strength(objp) / shield_get_max_strength(sp);
		} else if (Ship_info[sp->ship_info_index].max_shield_strength > 0.0f) {
			shield_pct = shield_get_strength(objp) / (sip_orig->max_shield_strength * sip_orig->max_shield_recharge);
		} else if (sip_orig->flags[Info_Flags::Intrinsic_no_shields] || orig_sp_max_shield_strength == 0.0f) {
			// Recall, this flag is used to allow switching between both shielded and unshielded craft in loadout,
			// so if that flag is on, then treat shield percent as full instead of empty.
			// This ensures that switching to a ship with shields in loadout
			// will begin the mission with full shield strength (instead of 0).
			shield_pct = 1.0f;
		} else {
			shield_pct = 0.0f;
		}

		// extra check
		Assert(shield_pct >= 0.0f && shield_pct <= 1.0f);
		CLAMP(shield_pct, 0.0f, 1.0f);
	} else {
		shield_pct = hull_pct = 1.0f;
	}

	// subsystems
	int num_saved_subsystems = 0;
	char **subsys_names = new char *[sip_orig->n_subsystems];
	float *subsys_pcts = new float[sip_orig->n_subsystems];

	// prevent crashes in the event of a subsystem mismatch
	for (i = 0; i < sip_orig->n_subsystems; ++i)
	{
		subsys_names[i] = nullptr;
		subsys_pcts[i] = 0.0f;
	}

	ss = GET_FIRST(&sp->subsys_list);
	while ( ss != END_OF_LIST(&sp->subsys_list) )
	{
		if (num_saved_subsystems == sip_orig->n_subsystems)
		{
			Error(LOCATION, "Subsystem mismatch while changing ship class from '%s' to '%s'!", sip_orig->name, sip->name);
			break;
		}

		// MageKing17 - Update subsystem pointers if changing classes mid-mission
		if (!(Fred_running) && (Game_mode & GM_IN_MISSION)) {
			// If any of our AI info objects targeting a subsystem on this ship are targeting this specific
			// subsystem, add them to the subsystem_matches vector and remove them from our "to be checked" list.
			{
				auto it = target_matches.begin();
				while (it != target_matches.end()) {
					ai_info* aip = &Ai_info[*it];
					if (aip->targeted_subsys == ss) {
						ivec3 temp = {*it, -1, num_saved_subsystems};	// -1 for the "goal" index means targeted, not actually a goal
						subsystem_matches.push_back(temp);
						aip->targeted_subsys = NULL;	// Clear this so that aip->last_subsys_target won't point to a subsystem from the old list later
						aip->targeted_subsys_parent = -1;
						auto erasor = it;
						++it;
						target_matches.erase(erasor);
					} else {
						++it;
					}
				}
			}

			// Fix any weapons homing in on this subsystem.
			{
				auto it = homing_matches.begin();
				while (it != homing_matches.end()) {
					if ((*it)->homing_subsys == ss) {
						homing_subsystem_matches.push_back(std::make_pair(*it, num_saved_subsystems));
						auto erasor = it;
						++it;
						homing_matches.erase(erasor);
					} else {
						++it;
					}
				}
			}

			// Fix any projectiles fired from this subsystem.
			{
				auto it = weapon_turret_matches.begin();
				while (it != weapon_turret_matches.end()) {
					if ((*it)->turret_subsys == ss) {
						weapon_turret_subsystem_matches.push_back(std::make_pair(*it, num_saved_subsystems));
						auto erasor = it;
						++it;
						weapon_turret_matches.erase(erasor);
					} else {
						++it;
					}
				}
			}

			// Fix any subsystems in last_targeted_matches[].
			{
				auto it = last_targeted_matches.begin();
				while (it != last_targeted_matches.end()) {
					if (sp->last_targeted_subobject[*it] == ss) {
						last_targeted_subsystem_matches.push_back(std::make_pair(*it, num_saved_subsystems));
						auto erasor = it;
						++it;
						last_targeted_matches.erase(erasor);
					} else {
						++it;
					}
				}
			}
		}

		// save subsys information
		subsys_names[num_saved_subsystems] = new char[NAME_LENGTH];
		strcpy(subsys_names[num_saved_subsystems], ss->system_info->subobj_name);

		if (ss->max_hits > 0.0f)
			subsys_pcts[num_saved_subsystems] = ss->current_hits / ss->max_hits;
		else
			subsys_pcts[num_saved_subsystems] = ss->max_hits;

		// extra check
		Assert(subsys_pcts[num_saved_subsystems] >= 0.0f && subsys_pcts[num_saved_subsystems] <= 1.0f);
		CLAMP(subsys_pcts[num_saved_subsystems], 0.0f, 1.0f);

		num_saved_subsystems++;
		ss = GET_NEXT(ss);
	}
	Assertion(target_matches.empty(), "Failed to find matches for every currently-targeted subsystem on ship %s in change_ship_type(); get a coder!\n", sp->ship_name);
	Assertion(homing_matches.empty(), "Failed to find matches for every subsystem being homed in on ship %s in change_ship_type(); get a coder!\n", sp->ship_name);
	Assertion(weapon_turret_matches.empty(), "Failed to find matches for every turret a projectile was fired from on ship %s in change_ship_type(); get a coder!\n", sp->ship_name);
	Assertion(last_targeted_matches.empty(), "Somehow failed to find every subsystem a player was previously targeting on ship %s in change_ship_type(); get a coder!\n", sp->ship_name);

	// point to new ship data
	ship_model_change(n, ship_type);
	sp->ship_info_index = ship_type;

	// get the before and after models (the new model may have only been loaded in ship_model_change)
	auto pm = model_get(sip->model_num);
	auto pm_orig = model_get(sip_orig->model_num);

	// if we have the same warp parameters as the ship class, we will need to update them to point to the new class
	if (sp->warpin_params_index == sip_orig->warpin_params_index) {
		sp->warpin_params_index = sip->warpin_params_index;
	}
	if (sp->warpout_params_index == sip_orig->warpout_params_index) {
		sp->warpout_params_index = sip->warpout_params_index;
	}

	if (!Fred_running) {
		//WMC - set warp effects
		ship_set_warp_effects(objp);
	}

	// set the correct hull strength
	if (Fred_running) {
		sp->ship_max_hull_strength = 100.0f;
		objp->hull_strength = 100.0f;
	} else {
		if (sp->special_hitpoints > 0) {
			sp->ship_max_hull_strength = (float)sp->special_hitpoints;
		} else {
			sp->ship_max_hull_strength = sip->max_hull_strength;
		}

		objp->hull_strength = hull_pct * sp->ship_max_hull_strength;
	}

	sp->max_shield_recharge = sip->max_shield_recharge;

	// set the correct shield strength
	if (Fred_running) {
		if (sp->ship_max_shield_strength)
			sp->ship_max_shield_strength = 100.0f;
		objp->shield_quadrant[0] = 100.0f;
	} else {
		if (sp->special_shield >= 0) {
			sp->ship_max_shield_strength = (float)sp->special_shield;
		} else {
			sp->ship_max_shield_strength = sip->max_shield_strength;
		}

		shield_set_strength(objp, shield_pct * shield_get_max_strength(sp));
	}

	// Goober5000: div-0 checks
	Assert(sp->ship_max_hull_strength > 0.0f);
	Assert(objp->hull_strength > 0.0f);

	// set regen rates
	sp->max_weapon_regen_per_second = sip->max_weapon_regen_per_second;
	sp->max_shield_regen_per_second = sip->max_shield_regen_per_second;

	sp->autoaim_fov = sip->autoaim_fov;

	// Mantis 2763: moved down to have access to the right ship_max_shield_strength value
	// make sure that shields are disabled/enabled if they need to be - Chief1983
	if (!Fred_running) {
		if ((p_objp != nullptr && p_objp->flags[Mission::Parse_Object_Flags::OF_Force_shields_on]) && (sp->ship_max_shield_strength > 0.0f)) {
			objp->flags.remove(Object::Object_Flags::No_shields);
		}
		else if ((p_objp != nullptr && p_objp->flags[Mission::Parse_Object_Flags::OF_No_shields]) || (sp->ship_max_shield_strength == 0.0f)) {
			objp->flags.set(Object::Object_Flags::No_shields);
		}
		// Since there's not a mission flag set to be adjusting this, see if there was a change from a ship that normally has shields to one that doesn't, and vice versa
		else if (!(sip_orig->flags[Info_Flags::Intrinsic_no_shields]) && (sip->flags[Info_Flags::Intrinsic_no_shields])) {
			objp->flags.set(Object::Object_Flags::No_shields);
		}
		else if (((sip_orig->flags[Info_Flags::Intrinsic_no_shields]) || (orig_sp_max_shield_strength == 0.0f)) && !(sip->flags[Info_Flags::Intrinsic_no_shields]) && (sp->ship_max_shield_strength > 0.0f)) {
			objp->flags.remove(Object::Object_Flags::No_shields);
		}
	}

	// niffiwan: set new armor types
	sp->armor_type_idx = sip->armor_type_idx;
	sp->shield_armor_type_idx = sip->shield_armor_type_idx;
	sp->collision_damage_type_idx = sip->collision_damage_type_idx;
	sp->debris_damage_type_idx = sip->debris_damage_type_idx;

	// subsys stuff done only after hull stuff is set
	// if the subsystem list is not currently empty, then we need to clear it out first.
	ship_subsystems_delete(sp);

	// fix up the subsystems
	subsys_set( sp->objnum );

	// Goober5000 - restore the subsystem percentages
	ss = GET_FIRST(&sp->subsys_list);
	int ss_index = 0;
	while ( ss != END_OF_LIST(&sp->subsys_list) )
	{
		for (i = 0; i < num_saved_subsystems; i++)
		{
			if (!subsystem_stricmp(ss->system_info->subobj_name, subsys_names[i]))
			{
				ss->current_hits = ss->max_hits * subsys_pcts[i];

				// MageKing17 - Every AI doing something with this subsystem must transfer to the new one.
				{
					auto it = subsystem_matches.begin();
					while (it != subsystem_matches.end()) {
						if (it->z == i) {	// the subsystem is a match
							int goalnum = it->y;
							ai_info* aip = &Ai_info[it->x];
							if (goalnum == -1) {
								set_targeted_subsys(aip, ss, n);
							} else {
								aip->goals[goalnum].ai_submode = ss_index;
							}
							auto erasor = it;
							++it;
							subsystem_matches.erase(erasor);
						} else {
							++it;
						}
					}
				}

				// also weapons homing in on this subsystem
				{
					auto it = homing_subsystem_matches.begin();
					while (it != homing_subsystem_matches.end()) {
						if (it->second == i) {	// the subsystem is a match
							it->first->homing_subsys = ss;
							auto erasor = it;
							++it;
							homing_subsystem_matches.erase(erasor);
						} else {
							++it;
						}
					}
				}

				// also weapons fired from this subsystem
				{
					auto it = weapon_turret_subsystem_matches.begin();
					while (it != weapon_turret_subsystem_matches.end()) {
						if (it->second == i) {
							it->first->turret_subsys = ss;
							auto erasor = it;
							++it;
							weapon_turret_subsystem_matches.erase(erasor);
						} else {
							++it;
						}
					}
				}

				// and finally, fix previously-targeted subsystems
				{
					auto it = last_targeted_subsystem_matches.begin();
					while (it != last_targeted_subsystem_matches.end()) {
						if (it->second == i) {	// the subsystem is a match
							sp->last_targeted_subobject[it->first] = ss;
							auto erasor = it;
							++it;
							last_targeted_subsystem_matches.erase(erasor);
						} else {
							++it;
						}
					}
				}

				break;
			}
		}

		ss_index++;
		ss = GET_NEXT(ss);
	}
	ship_recalc_subsys_strength(sp);

	for (auto cit = subsystem_matches.cbegin(); cit != subsystem_matches.cend(); ++cit) {
		int goalnum = cit->y;
		ai_info* aip = &Ai_info[cit->x];
		if (goalnum == -1) {
			if (aip == Player_ai) {
				hud_cease_subsystem_targeting(0);
			} else {
				set_targeted_subsys(aip, nullptr, -1);
			}
		} else {
			ai_remove_ship_goal(aip, goalnum);
		}
	}

	subsystem_matches.clear();	// We don't need these anymore, so may as well clear them now.

	for (auto cit = homing_subsystem_matches.cbegin(); cit != homing_subsystem_matches.cend(); ++cit) {
		cit->first->homing_subsys = nullptr;
	}

	homing_subsystem_matches.clear();

	for (auto cit = weapon_turret_subsystem_matches.cbegin(); cit != weapon_turret_subsystem_matches.cend(); ++cit) {
		cit->first->turret_subsys = nullptr;
	}

	weapon_turret_subsystem_matches.clear();

	for (auto cit = last_targeted_subsystem_matches.cbegin(); cit != last_targeted_subsystem_matches.cend(); ++cit) {
		sp->last_targeted_subobject[cit->first] = nullptr;
	}

	last_targeted_subsystem_matches.clear();

	// now free the memory
	for (i = 0; i < sip_orig->n_subsystems; i++)
		delete[] subsys_names[i];
	
	delete [] subsys_names;
	delete [] subsys_pcts;

	// Goober5000 - collect data about whatever is currently docked and compare the dockpoints on the two models
	auto dock_ptr = objp->dock_list;
	while (dock_ptr != nullptr)
	{
		auto next_ptr = dock_ptr->next;		// since we might remove the dock instance we are iterating on
		int dockpoint_index = dock_ptr->dockpoint_used;
		const char *dockpoint_name = pm_orig->docking_bays[dockpoint_index].name;

		// if it's the same dockpoint index and name, do nothing
		if (dockpoint_index < pm->n_docks && stricmp(dockpoint_name, pm->docking_bays[dockpoint_index].name) == 0)
		{
			dock_ptr = next_ptr;
			continue;
		}

		// see if this dockpoint is found on the new model under a different name
		int new_dockpoint_index = find_item_with_string(pm->docking_bays, pm->n_docks, &dock_bay::name, dockpoint_name);
		if (new_dockpoint_index >= 0)
		{
			dock_ptr->dockpoint_used = new_dockpoint_index;
			dock_ptr = next_ptr;
			continue;
		}

		// it wasn't found, so undock these objects
		ai_do_objects_undocked_stuff(objp, dock_ptr->docked_objp);
		dock_ptr = next_ptr;
	}

	sp->afterburner_fuel = MAX(0.0f, sip->afterburner_fuel_capacity - (sip_orig->afterburner_fuel_capacity - sp->afterburner_fuel));

	// handle countermeasure counts
	if (Countermeasures_use_capacity) {
		float cm_cargo_size = Weapon_info[sp->current_cmeasure].cargo_size;
		int cm_used = fl2i(sip_orig->cmeasure_max / cm_cargo_size) - sp->cmeasure_count;
		sp->cmeasure_count = MAX(0, fl2i(sip->cmeasure_max / cm_cargo_size) - cm_used);
	} else {
		sp->cmeasure_count = MAX(0, sip->cmeasure_max - (sip_orig->cmeasure_max - sp->cmeasure_count));
	}

	ship_set_default_weapons(sp, sip);
	physics_ship_init(&Objects[sp->objnum]);
	ets_init_ship(&Objects[sp->objnum]);

	// Reset physics to previous values
	if (by_sexp) {
		Objects[sp->objnum].phys_info.desired_rotvel = ph_inf.desired_rotvel;
		Objects[sp->objnum].phys_info.desired_vel = ph_inf.desired_vel;
		Objects[sp->objnum].phys_info.fspeed = ph_inf.fspeed;
		Objects[sp->objnum].phys_info.heading = ph_inf.heading;
		Objects[sp->objnum].phys_info.last_rotmat = ph_inf.last_rotmat;
		Objects[sp->objnum].phys_info.prev_ramp_vel = ph_inf.prev_ramp_vel;
		Objects[sp->objnum].phys_info.reduced_damp_decay = ph_inf.reduced_damp_decay;
		Objects[sp->objnum].phys_info.rotvel = ph_inf.rotvel;
		Objects[sp->objnum].phys_info.shockwave_decay = ph_inf.shockwave_decay;
		Objects[sp->objnum].phys_info.shockwave_shake_amp = ph_inf.shockwave_shake_amp;
		Objects[sp->objnum].phys_info.speed = ph_inf.speed;
		Objects[sp->objnum].phys_info.vel = ph_inf.vel;
		Objects[sp->objnum].phys_info.ai_desired_orient = ph_inf.ai_desired_orient;
		Objects[sp->objnum].phys_info.linear_thrust = ph_inf.linear_thrust;
		Objects[sp->objnum].phys_info.rotational_thrust = ph_inf.rotational_thrust;
	}

	ship_set_new_ai_class(sp, sip->ai_class);

	//======================================================

	// Bobboau's thruster stuff again
	if (sip->afterburner_trail.bitmap_id < 0)
		generic_bitmap_load(&sip->afterburner_trail);

	sp->ab_count = 0;
	if (sip->flags[Ship::Info_Flags::Afterburner])
	{
		for (int h = 0; h < pm->n_thrusters; h++)
		{
			for (int j = 0; j < pm->thrusters[h].num_points; j++)
			{
				// this means you've reached the max # of AB trails for a ship
				Assert(sip->ct_count <= MAX_SHIP_CONTRAILS);
	
				trail_info *ci = &sp->ab_info[sp->ab_count];

				// only make ab trails for thrusters that are pointing backwards
				if (pm->thrusters[h].points[j].norm.xyz.z > -0.5)
					continue;

				ci->pt = pm->thrusters[h].points[j].pnt;	//offset
				ci->w_start = pm->thrusters[h].points[j].radius * sip->afterburner_trail_width_factor;	// width * table loaded width factor
	
				ci->w_end = 0.05f;//end width
	
				ci->a_start = sip->afterburner_trail_alpha_factor;	// start alpha  * table loaded alpha factor
	
				ci->a_end = sip->afterburner_trail_alpha_end_factor;//end alpha

				ci->a_decay_exponent = sip->afterburner_trail_alpha_decay_exponent;
	
				ci->max_life = sip->afterburner_trail_life;	// table loaded max life

				ci->spread = sip->afterburner_trail_spread; // table loaded spread speed
	
				ci->spew_duration = 60;	//spew time???

				ci->n_fade_out_sections = sip->afterburner_trail_faded_out_sections; // table loaded n sections to be faded out

				ci->texture.bitmap_id = sip->afterburner_trail.bitmap_id; // table loaded bitmap used on this ships burner trails

				ci->texture_stretch = sip->afterburner_trail_tex_stretch;
				nprintf(("AB TRAIL", "AB trail point #%d made for '%s'\n", sp->ab_count, sp->ship_name));
				sp->ab_count++;
				Assert(MAX_SHIP_CONTRAILS > sp->ab_count);
			}
		}
	}//end AB trails -Bobboau

	// Goober5000 - check other class-specific flags too

	if (sip->flags[Ship::Info_Flags::Stealth])			// changing TO a stealthy ship class
		sp->flags.set(Ship_Flags::Stealth);
	else if (sip_orig->flags[Ship::Info_Flags::Stealth])	// changing FROM a stealthy ship class
		sp->flags.remove(Ship_Flags::Stealth);

	if (sip->flags[Ship::Info_Flags::Ship_class_dont_collide_invis])				// changing TO a don't-collide-invisible ship class
		sp->flags.set(Ship_Flags::Dont_collide_invis);
	else if (sip_orig->flags[Ship::Info_Flags::Ship_class_dont_collide_invis])	// changing FROM a don't-collide-invisible ship class
		sp->flags.remove(Ship_Flags::Dont_collide_invis);

	if (sip->flags[Ship::Info_Flags::No_collide])								// changing TO a no-collision ship class
		obj_set_flags(objp, objp->flags - Object::Object_Flags::Collides);
	else if (sip_orig->flags[Ship::Info_Flags::No_collide])						// changing FROM a no-collision ship class
		obj_set_flags(objp, objp->flags + Object::Object_Flags::Collides);

	if (sip->flags[Ship::Info_Flags::No_ets])
		sp->flags.set(Ship_Flags::No_ets);
	else if (sip_orig->flags[Ship::Info_Flags::No_ets])
		sp->flags.remove(Ship_Flags::No_ets);


	// Chief1983: Make sure that when changing to a new ship with secondaries, you switch to bank 0.  They still won't 
	// fire if the SF2_SECONDARIES_LOCKED flag is on as this should have carried over.
	if ( swp->num_secondary_banks > 0 && swp->current_secondary_bank == -1 ){
		swp->current_secondary_bank = 0;
	}

	// Bobboau's animation fixup
	for( i = 0; i<MAX_SHIP_PRIMARY_BANKS;i++){
			swp->primary_animation_position[i] = MA_POS_NOT_SET;
	}
	for( i = 0; i<MAX_SHIP_SECONDARY_BANKS;i++){
			swp->secondary_animation_position[i] = MA_POS_NOT_SET;
	}
	animation::anim_set_initial_states(sp);

	//Reassign sound stuff
	if (!Fred_running)
		ship_assign_sound(sp);
	
	// Valathil - Reinitialize collision checks
	obj_remove_collider(OBJ_INDEX(objp));
	obj_add_collider(OBJ_INDEX(objp));

	// The E - If we're switching during gameplay, make sure we get valid primary/secondary selections
	if ( by_sexp ) {
		if (sip_orig->num_primary_banks > sip->num_primary_banks) {
			sp->weapons.current_primary_bank = 0;
			sp->weapons.previous_primary_bank = 0;
		}

		if (sip_orig->num_secondary_banks > sip->num_secondary_banks) {
			sp->weapons.current_secondary_bank = 0;
			sp->weapons.previous_secondary_bank = 0;
		}

		// While we're at it, let's copy over the ETS settings too
		set_recharge_rates(&Objects[sp->objnum], orig_shd_rechg_idx, orig_wep_rechg_idx, orig_eng_rechg_idx);
	}

	// zookeeper - If we're switching in the loadout screen, make sure we retain initial velocity set in FRED
	if (!(Game_mode & GM_IN_MISSION) && !(Fred_running) && (p_objp != nullptr)) {
		Objects[sp->objnum].phys_info.speed = p_objp->initial_velocity * sip->max_speed / 100.0f;

		// prev_ramp_vel needs to be in local coordinates
		// set z of prev_ramp_vel to initial velocity
		vm_vec_zero(&Objects[sp->objnum].phys_info.prev_ramp_vel);
		Objects[sp->objnum].phys_info.prev_ramp_vel.xyz.z = Objects[sp->objnum].phys_info.speed;
		// convert to global coordinates and set to ship velocity and desired velocity
		vm_vec_unrotate(&Objects[sp->objnum].phys_info.vel, &Objects[sp->objnum].phys_info.prev_ramp_vel, &Objects[sp->objnum].orient);
		Objects[sp->objnum].phys_info.desired_vel = Objects[sp->objnum].phys_info.vel;
	}

	// Goober5000 - if we're changing to a ship class that has a different default set of orders, update the orders
	// (this avoids wiping the orders if we're e.g. changing between fighter classes)
	if (Fred_running)
	{
		const auto& old_defaults = ship_get_default_orders_accepted(sip_orig);
		const auto& new_defaults = ship_get_default_orders_accepted(sip);

		if (old_defaults != new_defaults)
			sp->orders_accepted = new_defaults;
	}

	if (sip->uses_team_colors)
	{
		// wookieejedi - maintain team color setting if possible
		if (!Fred_running && p_objp != nullptr && !p_objp->team_color_setting.empty()) {
			sp->team_name = p_objp->team_color_setting;
		} else {
			sp->team_name = sip->default_team_name;
		}
	}

	if (!Fred_running) {
		// wookieejedi - set up wingman status index
		// note, p_objp is allowed to be nullptr in hud_wingman_status_set_index 
		// specifically for the situation when there is no parse object, 
		// such as when a ship is created via the create-ship sexp
		hud_wingman_status_set_index(&Wings[sp->wingnum], sp, p_objp);
	}
}

/**
 * Launch countermeasures from object *objp.
 * 
 * @param objp object from which to launch countermeasure
 * @param rand_val is used in multiplayer to ensure that all clients in the game fire countermeasure the same way
 */
int ship_launch_countermeasure(object *objp, int rand_val)
{
	if(!Countermeasures_enabled) {
		return 0;
	}

	int	check_count, cmeasure_count;
	int cobjnum=-1;
	vec3d	pos;
	ship	*shipp;
	ship_info *sip;

	shipp = &Ships[objp->instance];
	sip = &Ship_info[shipp->ship_info_index];

	int arand;
	if(rand_val < 0) {
		arand = Random::next();
	} else {
		arand = rand_val;
	}

	// in the case where the server is an observer, he can launch countermeasures unless we do this.
	if( objp->type == OBJ_OBSERVER){
		return 0;
	}

	if ( !timestamp_elapsed(shipp->cmeasure_fire_stamp) ){
		return 0;
	}

	// Cyborg: Coverity 1523546, check that we have a valid countermeasure before setting the delay. (Could help things work more intuitively if CM is changed mid-mission)
	if (shipp->current_cmeasure > -1){
		shipp->cmeasure_fire_stamp = timestamp(Weapon_info[shipp->current_cmeasure].cmeasure_firewait);	//	Can launch every cmeasure wait
	} else {
		shipp->cmeasure_fire_stamp = timestamp(0);
	}

#ifndef NDEBUG
	if (Weapon_energy_cheat) {
		shipp->cmeasure_count++;
	}
#endif

	// we might check the count of countermeasures left depending on game state.  Multiplayer clients
	// do not need to check any objects other than themselves for the count
	check_count = 1;

	if ( MULTIPLAYER_CLIENT && (objp != Player_obj) ){
		check_count = 0;
	}

	if ( check_count && ((shipp->cmeasure_count <= 0) || (sip->cmeasure_type < 0)) ) {
		if ( objp == Player_obj ) {
			if(sip->cmeasure_max < 1 || sip->cmeasure_type < 0) {
				//TODO: multi-lingual support
				HUD_sourced_printf(HUD_SOURCE_HIDDEN, "%s", XSTR( "Not equipped with countermeasures", 1633));
			} else if(shipp->current_cmeasure < 0) {
				//TODO: multi-lingual support
				HUD_sourced_printf(HUD_SOURCE_HIDDEN, "%s", XSTR( "No countermeasures selected", 1634));
			} else if(shipp->cmeasure_count <= 0) {
				HUD_sourced_printf(HUD_SOURCE_HIDDEN, "%s", XSTR( "No more countermeasure charges.", 485));
			}
			snd_play( gamesnd_get_game_sound(ship_get_sound(Player_obj, GameSounds::OUT_OF_MISSLES)), 0.0f );
		}

		// if we have a player ship, then send the fired packet anyway so that the player
		// who fired will get his 'out of countermeasures' sound
		cmeasure_count = 0;
		if (objp->flags[Object::Object_Flags::Player_ship]){
			// the new way of doing things
			if (Game_mode & GM_MULTIPLAYER){
				send_NEW_countermeasure_fired_packet(objp, cmeasure_count, -1);
			}
			return 0;
		}

		return 0;
	}

	cmeasure_count = shipp->cmeasure_count;
	shipp->cmeasure_count--;

	vm_vec_scale_add(&pos, &objp->pos, &objp->orient.vec.fvec, -objp->radius/2.0f);

	cobjnum = weapon_create(&pos, &objp->orient, shipp->current_cmeasure, OBJ_INDEX(objp));
	if (cobjnum >= 0)
	{
		cmeasure_set_ship_launch_vel(&Objects[cobjnum], objp, arand);
		nprintf(("Network", "Cmeasure created by %s\n", shipp->ship_name));

		// Play sound effect for counter measure launch
		Assert(shipp->current_cmeasure < weapon_info_size());
		if (Player_obj == objp && Weapon_info[shipp->current_cmeasure].cockpit_launch_snd.isValid()) {
			snd_play(gamesnd_get_game_sound(Weapon_info[shipp->current_cmeasure].cockpit_launch_snd));
		} else if ( Weapon_info[shipp->current_cmeasure].launch_snd.isValid() ) {
			snd_play_3d( gamesnd_get_game_sound(Weapon_info[shipp->current_cmeasure].launch_snd), &pos, &View_position );
		}

		// the new way of doing things
		if(Game_mode & GM_MULTIPLAYER){
			send_NEW_countermeasure_fired_packet(objp, cmeasure_count, Objects[cobjnum].net_signature);
		}

		if (OnCountermeasureFireHook->isActive()) {
			// add scripting hook for 'On Countermeasure Fire' --wookieejedi
			OnCountermeasureFireHook->run(scripting::hooks::ShipSourceConditions{ shipp },
				scripting::hook_param_list(scripting::hook_param("Ship", 'o', objp),
					scripting::hook_param("CountermeasuresLeft", 'i', shipp->cmeasure_count),
					scripting::hook_param("Countermeasure", 'o', &Objects[cobjnum])
				));
		}
	}

	return (cobjnum >= 0);		// return 0 if not fired, 1 otherwise
}

/**
 * See if enough time has elapsed to play fail sound again
 */
void ship_maybe_do_primary_fail_sound_hud(bool depleted_energy)
{
	ship_weapon *swp = &Player_ship->weapons;
	int stampval;

	hud_start_flash_weapon(swp->current_primary_bank, depleted_energy);

	if ( timestamp_elapsed(Laser_energy_out_snd_timer) )
	{
		// check timestamp according to ballistics
		if (Weapon_info[swp->primary_bank_weapons[swp->current_primary_bank]].wi_flags[Weapon::Info_Flags::Ballistic])
		{
			stampval = 500;
		}
		else
		{
			stampval = 50;
		}
		Laser_energy_out_snd_timer = timestamp(stampval);
		snd_play( gamesnd_get_game_sound(ship_get_sound(Player_obj, GameSounds::OUT_OF_WEAPON_ENERGY)));
	}
}

/**
 * See if enough time has elapsed to play secondary fail sound again
 */
static int ship_maybe_do_secondary_fail_sound_hud(weapon_info *wip, bool depleted_energy)
{
	hud_start_flash_weapon(Player_ship->weapons.num_primary_banks + Player_ship->weapons.current_secondary_bank, depleted_energy);

	if ( timestamp_elapsed(Missile_out_snd_timer) ) {
		
		if ( wip->wi_flags[Weapon::Info_Flags::Swarm] ) {
			Missile_out_snd_timer = timestamp(500);
		} else {
			Missile_out_snd_timer = timestamp(50);
		}
		snd_play( gamesnd_get_game_sound(ship_get_sound(Player_obj, GameSounds::OUT_OF_MISSLES)) );
		return 1;
	}
	return 0;
}

/**
 * See if weapon for ship can fire based on weapons subystem strength.
 *
 * @return 1 if weapon failed to fire, 0 if weapon can fire
 */
static int ship_weapon_maybe_fail(ship *sp)
{
	int	rval;
	float	weapons_subsys_str;

	// If playing on lowest skill level, weapons will not fail due to subsystem damage
	if ( Game_skill_level == 0 ){
		return 0;
	}

	rval = 0;
	weapons_subsys_str = ship_get_subsystem_strength( sp, SUBSYSTEM_WEAPONS );
	if ( weapons_subsys_str < SUBSYS_WEAPONS_STR_FIRE_FAIL ) {
		rval = 1;
	}
	else if ( weapons_subsys_str < SUBSYS_WEAPONS_STR_FIRE_OK ) {
		// chance to fire depends on weapons subsystem strength
		if ( (frand()-0.2f) > weapons_subsys_str )		
			rval = 1;
	}

	if (!rval) {
		// is subsystem disrupted?
		if ( ship_subsys_disrupted(sp, SUBSYSTEM_WEAPONS) ) {
			rval=1;
		}
	}
		
	return rval;
}

// create a moving tracer based upon a weapon which just fired
float t_rad = 0.5f;
float t_len = 10.0f;
float t_vel = 0.2f;
float t_min = 150.0f;
float t_max = 300.0f;
DCF(t_rad, "Sets weapon tracer radius")
{
	if (dc_optional_string_either("status", "--status") || dc_optional_string_either("?", "--?")) {
		dc_printf("t_rad : %f\n", t_rad);
		return;
	}
	
	dc_stuff_float(&t_rad);
}
DCF(t_len, "Sets weapon tracer length")
{
	if (dc_optional_string_either("status", "--status") || dc_optional_string_either("?", "--?")) {
		dc_printf("t_len : %f\n", t_len);
		return;
	}

	dc_stuff_float(&t_len);
}
DCF(t_vel, "Sets weapon tracer velocity")
{
	if (dc_optional_string_either("status", "--status") || dc_optional_string_either("?", "--?")) {
		dc_printf("t_vel : %f\n", t_vel);
		return;
	}

	dc_stuff_float(&t_vel);
}
/*
 TODO: These two DCF's (and variables) are unused
DCF(t_min, "")
{
	dc_stuff_float(&t_min);
}
DCF(t_max, "")
{
	dc_stuff_float(&t_max);
}
*/

/**
 * Stops a single primary bank
 */
static int ship_stop_fire_primary_bank(object * obj, int bank_to_stop)
{
	ship			*shipp;
	ship_weapon	*swp;

	if(obj == NULL){
		return 0;
	}

	if(obj->type != OBJ_SHIP){
		return 0;
	}

	shipp = &Ships[obj->instance];
	swp = &shipp->weapons;
	
	if(shipp->primary_rotate_rate[bank_to_stop] > 0.0f)
		shipp->primary_rotate_rate[bank_to_stop] -= Weapon_info[swp->primary_bank_weapons[bank_to_stop]].weapon_submodel_rotate_accell*flFrametime;
	if(shipp->primary_rotate_rate[bank_to_stop] < 0.0f)
		shipp->primary_rotate_rate[bank_to_stop] = 0.0f;
	if(Ship_info[shipp->ship_info_index].draw_primary_models[bank_to_stop]){
		shipp->primary_rotate_ang[bank_to_stop] += shipp->primary_rotate_rate[bank_to_stop]*flFrametime;
		if(shipp->primary_rotate_ang[bank_to_stop] > PI2)
			shipp->primary_rotate_ang[bank_to_stop] -= PI2;
		if(shipp->primary_rotate_ang[bank_to_stop] < 0.0f)
			shipp->primary_rotate_ang[bank_to_stop] += PI2;
	}
	
	if(shipp->was_firing_last_frame[bank_to_stop] == 0)
		return 0;

	shipp->was_firing_last_frame[bank_to_stop] = 0;

	return 1;
}


/**
 * Stuff to do when the ship has stoped fireing all primary weapons
 */
int ship_stop_fire_primary(object * obj)
{
	int i, num_primary_banks = 0, bank_to_stop = 0;
	ship			*shipp;
	ship_weapon	*swp;

	if(obj == NULL){
		return 0;
	}

	if(obj->type != OBJ_SHIP){
		return 0;
	}

	shipp = &Ships[obj->instance];
	swp = &shipp->weapons;

	bank_to_stop = swp->current_primary_bank;

	if ( shipp->flags[Ship_Flags::Primary_linked] ) {
		num_primary_banks = swp->num_primary_banks;
	} else {
		num_primary_banks = MIN(1, swp->num_primary_banks);
	}

	for ( i = 0; i < num_primary_banks; i++ ) {	
		// Goober5000 - allow more than two banks
		bank_to_stop = (swp->current_primary_bank+i) % swp->num_primary_banks;
		//only stop if it was fireing last frame
		ship_stop_fire_primary_bank(obj, bank_to_stop);
	}
	for(i = 0; i<swp->num_primary_banks+num_primary_banks;i++)
		ship_stop_fire_primary_bank(obj, i%swp->num_primary_banks);

	return 1;
}



int tracers[MAX_SHIPS][4][4];

/**
 * Checks whether a ship would use autoaim if it were to fire its primaries at
 * the given object, taking lead into account.
 *
 * @param shipp the ship to check
 * @param bank_to_fire the assumed primary bank
 * @param obj the object to check; must be the ship's current target
 */
bool in_autoaim_fov(ship *shipp, int bank_to_fire, object *obj)
{
	// Most of the code of this function has been duplicated from
	// ship_fire_primary(), because it is not easy to encapsulate cleanly.

	ship_info *sip;
	ai_info *aip;
	ship_weapon *swp;

	bool has_autoaim, has_converging_autoaim;
	float autoaim_fov = 0;
	float dist_to_target = 0;

	vec3d plr_to_target_vec;
	vec3d player_forward_vec = Objects[shipp->objnum].orient.vec.fvec;

	vec3d target_position;

	sip = &Ship_info[shipp->ship_info_index];
	aip = &Ai_info[shipp->ai_index];

	swp = &shipp->weapons;
	int weapon_idx = swp->primary_bank_weapons[bank_to_fire];
	if (weapon_idx < 0)
		return false;
	weapon_info* winfo_p = &Weapon_info[weapon_idx];

	// First check our ship/weapon flags
	const bool autoaim_flagged = ((sip->aiming_flags[Object::Aiming_Flags::Autoaim]) || (winfo_p->aiming_flags[Object::Aiming_Flags::Autoaim]));
	const bool autoaim_convergence_flagged = ((sip->aiming_flags[Object::Aiming_Flags::Autoaim_convergence]) || (winfo_p->aiming_flags[Object::Aiming_Flags::Autoaim_convergence]));

	// Now check that we have a target and an FOV value
	has_converging_autoaim = ((autoaim_convergence_flagged || (The_mission.ai_profile->player_autoaim_fov[Game_skill_level] > 0.0f && !( Game_mode & GM_MULTIPLAYER ))) && aip->target_objnum != -1);
	has_autoaim = ((autoaim_flagged || has_converging_autoaim || (sip->bank_autoaim_fov[bank_to_fire] > 0.0f)) && aip->target_objnum != -1);

	if (!has_autoaim) {
		return false;
	}

	autoaim_fov = std::max({shipp->autoaim_fov, The_mission.ai_profile->player_autoaim_fov[Game_skill_level], winfo_p->autoaim_fov, sip->bank_autoaim_fov[bank_to_fire]});

	if (aip->targeted_subsys != nullptr) {
		get_subsystem_world_pos(&Objects[aip->target_objnum], aip->targeted_subsys, &target_position);
	}
	else {
		target_position = obj->pos;
	}

	dist_to_target = vm_vec_dist_quick(&Objects[shipp->objnum].pos, &target_position);

	if (!hud_calculate_lead_pos(&Objects[shipp->objnum].pos, &plr_to_target_vec, &target_position, obj, winfo_p, dist_to_target)) {
		// no ballistic trajectory, also counts for no autoaim
		return false;
	}
	vm_vec_sub2(&plr_to_target_vec, &Objects[shipp->objnum].pos);

	float angle_to_target = vm_vec_delta_ang(&player_forward_vec, &plr_to_target_vec, nullptr);

	if (angle_to_target <= autoaim_fov) {
		return true;
	}
	else {
		return false;
	}
}


// fires a primary weapon for the given object.  It also handles multiplayer cases.
// in multiplayer, the starting network signature, and number of banks fired are sent
// to all the clients in the game. All the info is passed to send_primary at the end of
// the function.  The check_energy parameter (defaults to 1) tells us whether or not
// we should check the energy.  It will be 0 when a multiplayer client is firing an AI
// primary.
int ship_fire_primary(object * obj, int force, bool rollback_shot)
{
	vec3d		gun_point, pnt, firing_pos, target_position, target_velocity_vec;
	int			n = obj->instance;
	ship			*shipp;
	ship_weapon	*swp;
	ship_info	*sip;
	ai_info		*aip;
	int			weapon_idx = -1, i, weapon_objnum;
	int			bank_to_fire, num_fired = 0;	
	int			banks_fired;				// used for multiplayer to help determine whether or not to send packet
	banks_fired = 0;			// used in multiplayer -- bitfield of banks that were fired
	bool has_fired = false;		// used to determine whether we should fire the scripting hook

	gamesnd_id		sound_played;	// used to track what sound is played.  If the player is firing two banks
										// of the same laser, we only want to play one sound
	Assert( obj != NULL );

	if(obj == NULL){
		return 0;
	}

	// in the case where the server is an observer, he can fire (which) would be bad - unless we do this.
	if( obj->type == OBJ_OBSERVER){
		return 0;
	}

	Assert( obj->type == OBJ_SHIP );
	Assert( n >= 0 );
	Assert( Ships[n].objnum == OBJ_INDEX(obj));
	if((obj->type != OBJ_SHIP) || (n < 0) || (n >= MAX_SHIPS) || (Ships[n].objnum != OBJ_INDEX(obj))){
		return 0;
	}
	
	shipp = &Ships[n];
	swp = &shipp->weapons;

	// bogus 
	if((shipp->ship_info_index < 0) || (shipp->ship_info_index >= ship_info_size())){
		return 0;
	}
	if((shipp->ai_index < 0) || (shipp->ai_index >= MAX_AI_INFO)){
		return 0;
	}
	sip = &Ship_info[shipp->ship_info_index];
	aip = &Ai_info[shipp->ai_index];

	if ( swp->num_primary_banks <= 0 ) {
		return 0;
	}

	if ( swp->current_primary_bank < 0 ){
		return 0;
	}	

	// If the primaries have been locked, bail. 
	// Unless we're dealing with the player and their ship has the flag set to allow fail sounds when firing locked primaries.
	if (shipp->flags[Ship_Flags::Primaries_locked] && ! (obj == Player_obj && shipp->flags[Ship_Flags::Fail_sound_locked_primary]))
	{
		return 0;
	}

	swp->flags.set(Ship::Weapon_Flags::Primary_trigger_down);

	sound_played = gamesnd_id();

	// Fire the correct primary bank.  If primaries are linked (SF_PRIMARY_LINKED set), then fire 
	// both primary banks.
	int	num_primary_banks;

	if ( shipp->flags[Ship_Flags::Primary_linked] ) {
		num_primary_banks = swp->num_primary_banks;
	} else {
		num_primary_banks = MIN(1, swp->num_primary_banks);
	}

	Assert(num_primary_banks > 0);
	if (num_primary_banks < 1){
		return 0;
	}

	if(num_primary_banks == 1)
		for(i = 0; i<swp->num_primary_banks; i++){
			if(i!=swp->current_primary_bank)ship_stop_fire_primary_bank(obj, i);
		}

	polymodel* pm = model_get(sip->model_num);

	for ( i = 0; i < num_primary_banks; i++ ) {		
		bool has_autoaim, has_converging_autoaim, needs_target_pos; // used to flag weapon/ship as having autoaim
		float autoaim_fov = 0;                                      // autoaim limit
		float dist_to_target = 0;									// distance to target, for autoaim & automatic convergence

		// Goober5000 - allow more than two banks
		bank_to_fire = (swp->current_primary_bank+i) % swp->num_primary_banks;

		
		weapon_idx = swp->primary_bank_weapons[bank_to_fire];

		// why would a ship try to fire a weapon that doesn't exist?
		Assert( weapon_idx >= 0 && weapon_idx < weapon_info_size());
		if ( (weapon_idx < 0) || (weapon_idx >= weapon_info_size()) ) {
			continue;
		}		

		if (swp->primary_animation_position[bank_to_fire] == MA_POS_SET) {
			if ( timestamp_elapsed(swp->primary_animation_done_time[bank_to_fire]) )
				swp->primary_animation_position[bank_to_fire] = MA_POS_READY;
			else
				continue;
		}

		weapon_info* winfo_p = &Weapon_info[weapon_idx];

		// lets start gun convergence / autoaim code from here - Wanderer
		// in order to do this per weapon, this needs to be moved here from above -Mjn
		
		// This logic is getting a little unweildy.. so let's break it down slightly for readability.
		// First check our ship/weapon flags
		const bool autoaim_flagged = ((sip->aiming_flags[Object::Aiming_Flags::Autoaim]) || (winfo_p->aiming_flags[Object::Aiming_Flags::Autoaim]));
		const bool autoaim_convergence_flagged = ((sip->aiming_flags[Object::Aiming_Flags::Autoaim_convergence]) || (winfo_p->aiming_flags[Object::Aiming_Flags::Autoaim_convergence]));
		const bool auto_convergence_flagged = ((sip->aiming_flags[Object::Aiming_Flags::Auto_convergence]) || (winfo_p->aiming_flags[Object::Aiming_Flags::Auto_convergence]));

		// Now add in other checks like difficulty and game mode as appropriate. Also make sure we have a target and an FOV if necessary.
		has_converging_autoaim = ((autoaim_convergence_flagged || (The_mission.ai_profile->player_autoaim_fov[Game_skill_level] > 0.0f && !( Game_mode & GM_MULTIPLAYER ))) && aip->target_objnum != -1);
		has_autoaim = ((autoaim_flagged || has_converging_autoaim || (sip->bank_autoaim_fov[bank_to_fire] > 0.0f)) && aip->target_objnum != -1);
		needs_target_pos = ((auto_convergence_flagged || has_autoaim) && aip->target_objnum != -1);
		// TODO: check if weapon has launch curves that might need distance to enemy

		if (needs_target_pos) {
			if (has_autoaim) {
				autoaim_fov = std::max({shipp->autoaim_fov, The_mission.ai_profile->player_autoaim_fov[Game_skill_level], winfo_p->autoaim_fov, sip->bank_autoaim_fov[bank_to_fire]});
			}

			// If a subsystem is targeted, fire in that direction instead
			if (aip->targeted_subsys != NULL)
			{
				get_subsystem_world_pos(&Objects[aip->target_objnum], aip->targeted_subsys, &target_position);
			}
			else
			{
				target_position = Objects[aip->target_objnum].pos;
			}

			dist_to_target = vm_vec_dist_quick(&target_position, &obj->pos);

			target_velocity_vec = Objects[aip->target_objnum].phys_info.vel;
			if (The_mission.ai_profile->flags[AI::Profile_Flags::Use_additive_weapon_velocity])
				vm_vec_scale_sub2(&target_velocity_vec, &obj->phys_info.vel, winfo_p->vel_inherit_amount);
		}

		if (winfo_p->weapon_submodel_rotate_vel > 0.0f) {
			if (shipp->primary_rotate_rate[bank_to_fire] < winfo_p->weapon_submodel_rotate_vel)
				shipp->primary_rotate_rate[bank_to_fire] += winfo_p->weapon_submodel_rotate_accell*flFrametime;
			if (shipp->primary_rotate_rate[bank_to_fire] > winfo_p->weapon_submodel_rotate_vel)
				shipp->primary_rotate_rate[bank_to_fire] = winfo_p->weapon_submodel_rotate_vel;
			if (sip->draw_primary_models[bank_to_fire]) {
				shipp->primary_rotate_ang[bank_to_fire] += shipp->primary_rotate_rate[bank_to_fire]*flFrametime;
				if (shipp->primary_rotate_ang[bank_to_fire] > PI2)
					shipp->primary_rotate_ang[bank_to_fire] -= PI2;
				if (shipp->primary_rotate_ang[bank_to_fire] < 0.0f)
					shipp->primary_rotate_ang[bank_to_fire] += PI2;
			}
			if (shipp->primary_rotate_rate[bank_to_fire] < winfo_p->weapon_submodel_rotate_vel)
				continue;
		}
		// if this is a targeting laser, start it up   ///- only targeting laser if it is tag-c, otherwise it's a fighter beam -Bobboau
		if((winfo_p->wi_flags[Weapon::Info_Flags::Beam]) && (winfo_p->tag_level == 3) && (shipp->flags[Ship_Flags::Trigger_down]) && (winfo_p->b_info.beam_type == BeamType::TARGETING) ){
			ship_start_targeting_laser(shipp);
			continue;
		}

		// Cyborg17 - In rollback mode we don't need to worry timestamps, because we are recreating an exact shot, anyway.
		if (!rollback_shot) {
			// only non-multiplayer clients (single, multi-host) need to do timestamp checking
			if ( !timestamp_elapsed(swp->next_primary_fire_stamp[bank_to_fire]) ) {
				continue;
			}
		}

		// if weapons are linked and this is a nolink weapon, skip it
		if (shipp->flags[Ship_Flags::Primary_linked] && winfo_p->wi_flags[Weapon::Info_Flags::Nolink]) {
			continue;
		}

		int num_slots = pm->gun_banks[bank_to_fire].num_slots;
		float target_radius = 0.f;

		if (aip->target_objnum >= 0) {
			target_radius = Objects[aip->target_objnum].radius;
		}

		auto launch_curve_data = WeaponLaunchCurveData {
			num_slots,
			dist_to_target,
			target_radius,
		};

		// do timestamp stuff for next firing time
		float next_fire_delay;
		bool fast_firing = false;
		// reset the burst if applicable
		if (winfo_p->burst_flags[Weapon::Burst_Flags::Resets]) {
			// a bit of an oversimplification but the reset time doesnt have to be super accurate
			int reset_time = (int)(swp->last_primary_fire_stamp[bank_to_fire] + (winfo_p->fire_wait * 1000.f));
			if (timestamp_elapsed(reset_time)) {
				swp->burst_counter[bank_to_fire] = 0;
			}
		}

		int old_burst_counter = swp->burst_counter[bank_to_fire];
		int old_burst_seed = swp->burst_seed[bank_to_fire];

		// should subtract 1 from num_slots to match behavior of winfo_p->burst_shots
		int burst_shots = fl2i(i2fl(winfo_p->burst_flags[Weapon::Burst_Flags::Num_firepoints_burst_shots] ? num_slots - 1 : winfo_p->burst_shots)
			* winfo_p->weapon_launch_curves.get_output(weapon_info::WeaponLaunchCurveOutputs::BURST_SHOTS_MULT, launch_curve_data));
		if (burst_shots > swp->burst_counter[bank_to_fire]) {
			next_fire_delay = winfo_p->burst_delay * 1000.0f * winfo_p->weapon_launch_curves.get_output(weapon_info::WeaponLaunchCurveOutputs::BURST_DELAY_MULT, launch_curve_data);
			swp->burst_counter[bank_to_fire]++;
			if (winfo_p->burst_flags[Weapon::Burst_Flags::Fast_firing])
				fast_firing = true;
		} else {
			if (winfo_p->max_delay != 0.0f && winfo_p->min_delay != 0.0f) // Random fire delay (DahBlount)
				next_fire_delay = frand_range(winfo_p->min_delay, winfo_p->max_delay)
					* 1000.0f
					* winfo_p->weapon_launch_curves.get_output(weapon_info::WeaponLaunchCurveOutputs::FIRE_WAIT_MULT, launch_curve_data);
			else
				next_fire_delay = winfo_p->fire_wait
				* 1000.0f
				* winfo_p->weapon_launch_curves.get_output(weapon_info::WeaponLaunchCurveOutputs::FIRE_WAIT_MULT, launch_curve_data);

			swp->burst_counter[bank_to_fire] = 0;
			swp->burst_seed[bank_to_fire] = Random::next();
			// only used by type 5 beams
			if (swp->burst_counter[bank_to_fire] == 0) {
				swp->per_burst_rot += winfo_p->b_info.t5info.per_burst_rot;
				if (swp->per_burst_rot > PI2)
					swp->per_burst_rot -= PI2;
				else if (swp->per_burst_rot < -PI2)
					swp->per_burst_rot += PI2;
			}
		}
		//doing that time scale thing on enemy fighter is just ugly with beams, especaly ones that have careful timeing
		if (!((obj->flags[Object::Object_Flags::Player_ship]) || fast_firing || winfo_p->wi_flags[Weapon::Info_Flags::Beam])) {
			// When testing weapons fire in the lab, we do not have a player object available.
			if ((Game_mode & GM_LAB) || (Player_obj == nullptr) || (shipp->team == Ships[Player_obj->instance].team)){
				next_fire_delay *= aip->ai_ship_fire_delay_scale_friendly;
			} else {
				next_fire_delay *= aip->ai_ship_fire_delay_scale_hostile;
			}
		}
		
		// Goober5000 (thanks to _argv[-1] for the original idea)
		if ( (num_primary_banks > 1) &&  !(winfo_p->wi_flags[Weapon::Info_Flags::No_linked_penalty]) && !(The_mission.ai_profile->flags[AI::Profile_Flags::Disable_linked_fire_penalty]) )
		{
			int effective_primary_banks = 0;
			for (int it = 0; it < num_primary_banks; it++)
			{
				if ((it == bank_to_fire) || !(Weapon_info[swp->primary_bank_weapons[it]].wi_flags[Weapon::Info_Flags::Nolink, Weapon::Info_Flags::No_linked_penalty]))
					effective_primary_banks++;
			}
			Assert(effective_primary_banks >= 1);

			next_fire_delay *= 1.0f + (effective_primary_banks - 1) * 0.5f;		//	50% time penalty if banks linked
		}

		if (winfo_p->fof_spread_rate > 0.0f) {
			swp->primary_bank_fof_cooldown[bank_to_fire] += winfo_p->fof_spread_rate;
			CLAMP(swp->primary_bank_fof_cooldown[bank_to_fire], 0.0f, 1.0f);
		}

		//	MK, 2/4/98: Since you probably were allowed to fire earlier, but couldn't fire until your frame interval
		//	rolled around, subtract out up to half the previous frametime.
		//	Note, unless we track whether the fire button has been held down, and not tapped, it's hard to
		//	know how much time to subtract off.  It could be this fire is "late" because the user didn't want to fire.
		if ((next_fire_delay > 0.0f)) {
			if (obj->flags[Object::Object_Flags::Player_ship]) {
				int	t = timestamp_since(swp->next_primary_fire_stamp[bank_to_fire]);
				if (t > 0) {
					float	tx;

					tx = (float) t/1000.0f;
					if (tx > flFrametime/2.0f){
						tx = 1000.0f * flFrametime * 0.7f;
					}
					next_fire_delay -= tx;
				}
				
				if ((int) next_fire_delay < 1){
					next_fire_delay = 1.0f;
				}
			}

			swp->next_primary_fire_stamp[bank_to_fire] = timestamp((int)(next_fire_delay));
			swp->last_primary_fire_stamp[bank_to_fire] = timestamp();
		}

		if (sip->flags[Ship::Info_Flags::Dyn_primary_linking] ) {
			Assert(pm->gun_banks[bank_to_fire].num_slots != 0);
			swp->next_primary_fire_stamp[bank_to_fire] = timestamp((int)(next_fire_delay * ( swp->primary_bank_slot_count[ bank_to_fire ] ) / pm->gun_banks[bank_to_fire].num_slots ) );
			swp->last_primary_fire_stamp[bank_to_fire] = timestamp();
		} else if (winfo_p->firing_pattern != FiringPattern::STANDARD) {
			Assert(pm->gun_banks[bank_to_fire].num_slots != 0);
			swp->next_primary_fire_stamp[bank_to_fire] = timestamp((int)(next_fire_delay / pm->gun_banks[bank_to_fire].num_slots));
			swp->last_primary_fire_stamp[bank_to_fire] = timestamp();
			//to maintain balance of fighters with more fire points they will fire faster than ships with fewer points
		}else{
			swp->next_primary_fire_stamp[bank_to_fire] = timestamp((int)(next_fire_delay));
			swp->last_primary_fire_stamp[bank_to_fire] = timestamp();
		}


		// The player is trying to fire primaries which are locked. We've set the Fail_sound_locked_primaries flag, so it should make the fail sound.
		if  (obj == Player_obj && shipp->flags[Ship_Flags::Primaries_locked] && shipp->flags[Ship_Flags::Fail_sound_locked_primary])
		{					
			ship_maybe_do_primary_fail_sound_hud(false);
			ship_stop_fire_primary_bank(obj, bank_to_fire);
			continue;
		}

		// Here is where we check if weapons subsystem is capable of firing the weapon.
		// Note that we can have partial bank firing, if the weapons subsystem is partially
		// functional, which should be cool.  		
		if ( ship_weapon_maybe_fail(shipp) && !force) {
			if ( obj == Player_obj ) {
				ship_maybe_do_primary_fail_sound_hud(false);
			}
			ship_stop_fire_primary_bank(obj, bank_to_fire);
			continue;
		}		

		if ( pm->n_guns > 0 ) {
			vec3d predicted_target_pos, plr_to_target_vec;
			matrix firing_orient = obj->orient;
			if (obj == Player_obj && sip->aims_at_flight_cursor) {
				vm_angles_2_matrix(&firing_orient, &Player_flight_cursor);
				firing_orient = firing_orient * obj->orient;
			}

			bool in_automatic_aim_fov = false;
			float dist_to_aim = 0;

			// more autoaim stuff here - Wanderer
			// needs weapon speed
			if (needs_target_pos) {
				float time_to_target, angle_to_target;
				vec3d last_delta_vec;

				time_to_target = 0.0f;

				if (winfo_p->max_speed != 0)
				{
					time_to_target = dist_to_target / winfo_p->max_speed;
				}

				// This is to circumvent a nine-year-old bug that prevents autoaim from working in multi. In most cases polish_predicted_target_pos needs
				// the Player object, but in multi it needs the shooting object.  Also, additional conditions would need to be added in the future if you 
				// want to apply autoaim to AI, for whatever reason.
				object* object_to_send = (obj->flags[Object::Object_Flags::Player_ship]) ? obj : Player_obj;

				vm_vec_scale_add(&predicted_target_pos, &target_position, &target_velocity_vec, time_to_target);
				polish_predicted_target_pos(winfo_p, &Objects[aip->target_objnum], &target_position, &predicted_target_pos, dist_to_target, &last_delta_vec, 1, object_to_send);
				vm_vec_sub(&plr_to_target_vec, &predicted_target_pos, &obj->pos);

				if (has_autoaim) {
					angle_to_target = vm_vec_delta_ang(&firing_orient.vec.fvec, &plr_to_target_vec, nullptr);
					if (angle_to_target < autoaim_fov)
						in_automatic_aim_fov = true;
				}

				dist_to_aim = vm_vec_mag_quick(&plr_to_target_vec);

				// Use the hightest minimum convergence distance
				const float min_convergence_distance = std::max(sip->minimum_convergence_distance, winfo_p->minimum_convergence_distance);

				// minimum convergence distance
				if (min_convergence_distance > dist_to_aim) {
					float dist_mult;
					dist_mult = min_convergence_distance / dist_to_aim;
					vm_vec_scale_add(&predicted_target_pos, &obj->pos, &plr_to_target_vec, dist_mult);
					dist_to_aim = min_convergence_distance;
				}
			}
			
			if(winfo_p->wi_flags[Weapon::Info_Flags::Beam]){		// the big change I made for fighter beams, if there beams fill out the Fire_Info for a targeting laser then fire it, for each point in the weapon bank -Bobboau				

				int point_count = 0, shot_count = 1;
				FiringPattern firing_pattern;
				if (sip->flags[Ship::Info_Flags::Dyn_primary_linking]) {
					firing_pattern = sip->dyn_firing_patterns_allowed[bank_to_fire][swp->dynamic_firing_pattern[bank_to_fire]];
				} else {
					firing_pattern = winfo_p->firing_pattern;
				}

				// ok if this is a cycling weapon use shots as the number of points to fire from at a time
				// otherwise shots is the number of times all points will be fired (used mostly for the 'shotgun' effect)
				if (sip->flags[Ship::Info_Flags::Dyn_primary_linking]) {
					shot_count = fl2i(i2fl(winfo_p->cycle_multishot) * winfo_p->weapon_launch_curves.get_output(weapon_info::WeaponLaunchCurveOutputs::SHOTS_MULT, launch_curve_data));
					point_count = MIN(num_slots, swp->primary_bank_slot_count[bank_to_fire] );
				} else if (winfo_p->b_info.beam_shots) {
					shot_count = fl2i(i2fl(winfo_p->shots) * winfo_p->weapon_launch_curves.get_output(weapon_info::WeaponLaunchCurveOutputs::SHOTS_MULT, launch_curve_data));
					point_count = MIN(winfo_p->b_info.beam_shots, num_slots);
				} else if (firing_pattern != FiringPattern::STANDARD) {
					shot_count = fl2i(i2fl(winfo_p->cycle_multishot) * winfo_p->weapon_launch_curves.get_output(weapon_info::WeaponLaunchCurveOutputs::SHOTS_MULT, launch_curve_data));
					point_count = MIN(num_slots, winfo_p->shots);
				} else {
					shot_count = fl2i(i2fl(winfo_p->shots) * winfo_p->weapon_launch_curves.get_output(weapon_info::WeaponLaunchCurveOutputs::SHOTS_MULT, launch_curve_data));
					point_count = num_slots;
				}

				bool no_energy = shipp->weapon_energy < point_count * shot_count * winfo_p->energy_consumed * flFrametime;
				if (no_energy || (winfo_p->wi_flags[Weapon::Info_Flags::Ballistic] && shipp->weapons.primary_bank_ammo[bank_to_fire] <= 0))
				{
					swp->next_primary_fire_stamp[bank_to_fire] = timestamp((int)(next_fire_delay));
					if ( obj == Player_obj )
					{
						ship_maybe_do_primary_fail_sound_hud(no_energy);
					}
					ship_stop_fire_primary_bank(obj, bank_to_fire);
					continue;
				}			

				for (int pt_count = 0; pt_count < point_count; pt_count++) {
					int pt;
					if (!sip->flags[Ship::Info_Flags::Dyn_primary_linking] && winfo_p->b_info.beam_shots) {
						pt = swp->primary_firepoint_next_to_fire_index[bank_to_fire]++;
						if (swp->primary_firepoint_next_to_fire_index[bank_to_fire] >= num_slots) {
							swp->primary_firepoint_next_to_fire_index[bank_to_fire] = 0;
						}
					} else {
						switch (firing_pattern) {
							case FiringPattern::CYCLE_FORWARD: {
								pt = swp->primary_firepoint_next_to_fire_index[bank_to_fire]++;
								if (swp->primary_firepoint_next_to_fire_index[bank_to_fire] >= num_slots) {
									swp->primary_firepoint_next_to_fire_index[bank_to_fire] = 0;
								}
								break;
							}
							case FiringPattern::CYCLE_REVERSE: {
								pt = swp->primary_firepoint_next_to_fire_index[bank_to_fire]--;
								if (swp->primary_firepoint_next_to_fire_index[bank_to_fire] < 0) {
									swp->primary_firepoint_next_to_fire_index[bank_to_fire] = num_slots - 1;
								}
								break;
							}
							case FiringPattern::RANDOM_EXHAUSTIVE: {
								pt = swp->primary_firepoint_indices[bank_to_fire][swp->primary_firepoint_next_to_fire_index[bank_to_fire]++];
								if (swp->primary_firepoint_next_to_fire_index[bank_to_fire] >= num_slots) {
									swp->primary_firepoint_next_to_fire_index[bank_to_fire] = 0;
								}
								break;
							}
							case FiringPattern::RANDOM_NONREPEATING: // behaves the same as random repeating here
							case FiringPattern::RANDOM_REPEATING: {
								pt = swp->primary_firepoint_indices[bank_to_fire][pt_count];
								break;
							}
							default:
							case FiringPattern::STANDARD: {
								pt = pt_count;
								break;
							}
						}
					}

					for (int w = 0; w < shot_count; w++) {
						beam_fire_info fbfire_info;
						shipp->beam_sys_info.turret_norm.xyz.x = 0.0f;
				    	shipp->beam_sys_info.turret_norm.xyz.y = 0.0f;
    					shipp->beam_sys_info.turret_norm.xyz.z = 1.0f;
		    			shipp->beam_sys_info.model_num = sip->model_num;
				    	shipp->beam_sys_info.turret_gun_sobj = pm->detail[0];
    					shipp->beam_sys_info.turret_num_firing_points = 1;  // dummy turret info is used per firepoint
		    			shipp->beam_sys_info.turret_fov = -1.0f;

				    	shipp->fighter_beam_turret_data.disruption_timestamp = timestamp(0);
    					shipp->fighter_beam_turret_data.turret_next_fire_pos = 0;
		    			shipp->fighter_beam_turret_data.current_hits = 1.0;
				    	shipp->fighter_beam_turret_data.system_info = &shipp->beam_sys_info;

    					fbfire_info.target_subsys = Ai_info[shipp->ai_index].targeted_subsys;
    					fbfire_info.beam_info_index = shipp->weapons.primary_bank_weapons[bank_to_fire];
    					fbfire_info.beam_info_override = nullptr;
    					fbfire_info.shooter = &Objects[shipp->objnum];

	    				if (aip->target_objnum >= 0) {
    						fbfire_info.target = &Objects[aip->target_objnum];
    					} else {
    						fbfire_info.target = nullptr;
	    				}
	    				fbfire_info.turret = &shipp->fighter_beam_turret_data;
    					fbfire_info.bfi_flags = BFIF_IS_FIGHTER_BEAM;
    					fbfire_info.bank = bank_to_fire;
    					fbfire_info.burst_index = old_burst_counter;
	    				fbfire_info.burst_seed = old_burst_seed;
	    				fbfire_info.per_burst_rotation = swp->per_burst_rot;

						fbfire_info.local_fire_postion = pm->gun_banks[bank_to_fire].pnt[pt];
						shipp->beam_sys_info.pnt = pm->gun_banks[bank_to_fire].pnt[pt];
						shipp->beam_sys_info.turret_firing_point[0] = pm->gun_banks[bank_to_fire].pnt[pt];
						fbfire_info.point = pt;
						fbfire_info.fire_method = BFM_FIGHTER_FIRED;
						beam_fire(&fbfire_info);
						has_fired = true;
						num_fired++;
					}
				}
			}
			else	//if this isn't a fighter beam, do it normally -Bobboau
			{
				int point_count = 0, shot_count = 1;
				FiringPattern firing_pattern;
				if (sip->flags[Ship::Info_Flags::Dyn_primary_linking]) {
					firing_pattern = sip->dyn_firing_patterns_allowed[bank_to_fire][swp->dynamic_firing_pattern[bank_to_fire]];
				} else {
					firing_pattern = winfo_p->firing_pattern;
				}

				// ok if this is a cycling weapon use shots as the number of points to fire from at a time
				// otherwise shots is the number of times all points will be fired (used mostly for the 'shotgun' effect)
				if (sip->flags[Ship::Info_Flags::Dyn_primary_linking]) {
					shot_count = winfo_p->cycle_multishot;
					point_count = MIN(num_slots, swp->primary_bank_slot_count[ bank_to_fire ] );
				} else if (firing_pattern != FiringPattern::STANDARD) {
					shot_count = winfo_p->cycle_multishot;
					point_count = MIN(num_slots, winfo_p->shots);
				} else {
					shot_count = winfo_p->shots;
					point_count = num_slots;
				}

				if (swp->primary_firepoint_indices[bank_to_fire].empty()) {
					auto &fpi = swp->primary_firepoint_indices[bank_to_fire];
					fpi.clear();
					for (int fp = 0; fp < num_slots; fp++) {
						fpi.push_back(fp);
					}
					std::random_device rd;
					std::shuffle(fpi.begin(), fpi.end(), std::mt19937(rd()));
				}

				// The energy-consumption code executes even for ballistic primaries, because
				// there may be a reason why you want to have ballistics consume energy.  Perhaps
				// you can't fire too many too quickly or they'll overheat.  If not, just set
				// the weapon's energy_consumed to 0 and it'll work just fine. - Goober5000

				// fail unless we're forcing (energy based primaries)
				bool no_energy = shipp->weapon_energy < point_count * shot_count * winfo_p->energy_consumed; //was num_slots
				if ( no_energy && !force ) {

					swp->next_primary_fire_stamp[bank_to_fire] = timestamp((int)(next_fire_delay));
					if ( obj == Player_obj )
					{
						ship_maybe_do_primary_fail_sound_hud(no_energy);
					}
					ship_stop_fire_primary_bank(obj, bank_to_fire);
					continue;
				}
				// moved the above to here to use points instead of num_slots for energy consumption check

				// ballistics support for primaries - Goober5000
				if ( winfo_p->wi_flags[Weapon::Info_Flags::Ballistic] )
				{
					// If ship is being repaired/rearmed, it cannot fire ballistics
					if ( aip->ai_flags[AI::AI_Flags::Being_repaired] )
					{
						continue;
					}

					// duplicated from the secondaries firing routine...
					// determine if there is enough ammo left to fire weapons on this bank.  As with primary
					// weapons, we might or might not check ammo counts depending on game mode, who is firing,
					// and if I am a client in multiplayer
					int check_ammo = 1;

					if ( MULTIPLAYER_CLIENT && (obj != Player_obj) )
					{
						check_ammo = 0;
					}

					// not enough ammo
					if ( check_ammo && ( swp->primary_bank_ammo[bank_to_fire] <= 0) )
					{
						if ( obj == Player_obj )
						{
							ship_maybe_do_primary_fail_sound_hud(false);
						}
						else
						{
							// TODO:  AI switch primary weapon / re-arm?
						}
						continue;
					}
					
					// deplete ammo
					if ( !Weapon_energy_cheat )
					{
						swp->primary_bank_ammo[bank_to_fire] -= point_count*shot_count;

						// make sure we don't go below zero; any such error is excusable
						// because it only happens when the bank is depleted in one shot
						if (swp->primary_bank_ammo[bank_to_fire] < 0)
						{
							swp->primary_bank_ammo[bank_to_fire] = 0;
						}
					}
				}

				// now handle the energy as usual
				// deplete the weapon reserve energy by the amount of energy used to fire the weapon	
				// Only subtract the energy amount required for equipment operation once
				shipp->weapon_energy -= point_count*shot_count * winfo_p->energy_consumed;
				// note for later: option for fuel!
				
				// Mark all these weapons as in the same group
				int new_group_id = weapon_create_group_id();

				vec3d total_impulse;
				vec3d *firepoint_list;
				size_t current_firepoint = 0;

				if (winfo_p->wi_flags[Weapon::Info_Flags::Apply_Recoil]){
					firepoint_list = new vec3d[shot_count * point_count];
					vm_vec_zero(&total_impulse);
				} else {
					firepoint_list = nullptr;
				}

				polymodel *weapon_model = nullptr;
				if (sip->draw_primary_models[bank_to_fire] && (winfo_p->external_model_num >= 0)) 
					weapon_model = model_get(winfo_p->external_model_num);

				for (int pt_count = 0; pt_count < point_count; pt_count++) {
					int pt;
					switch (firing_pattern) {
						case FiringPattern::CYCLE_FORWARD: {
							pt = swp->primary_firepoint_next_to_fire_index[bank_to_fire]++;
							if (swp->primary_firepoint_next_to_fire_index[bank_to_fire] >= num_slots) {
								swp->primary_firepoint_next_to_fire_index[bank_to_fire] = 0;
							}
							break;
						}
						case FiringPattern::CYCLE_REVERSE: {
							pt = swp->primary_firepoint_next_to_fire_index[bank_to_fire]--;
							if (swp->primary_firepoint_next_to_fire_index[bank_to_fire] < 0) {
								swp->primary_firepoint_next_to_fire_index[bank_to_fire] = num_slots - 1;
							}
							break;
						}
						case FiringPattern::RANDOM_EXHAUSTIVE: {
							pt = swp->primary_firepoint_indices[bank_to_fire][swp->primary_firepoint_next_to_fire_index[bank_to_fire]++];
							if (swp->primary_firepoint_next_to_fire_index[bank_to_fire] >= num_slots) {
								swp->primary_firepoint_next_to_fire_index[bank_to_fire] = 0;
							}
							break;
						}
						case FiringPattern::RANDOM_NONREPEATING: // behaves the same as random repeating here
						case FiringPattern::RANDOM_REPEATING: {
							pt = swp->primary_firepoint_indices[bank_to_fire][pt_count];
							break;
						}
						default:
						case FiringPattern::STANDARD: {
							pt = pt_count;
							break;
						}
					}

					for (int j = 0; j < shot_count; j++) {
						if (weapon_model)
							if ((weapon_model->n_guns <= swp->external_model_fp_counter[bank_to_fire]) || (swp->external_model_fp_counter[bank_to_fire] < 0))
								swp->external_model_fp_counter[bank_to_fire] = 0;

						int sub_shots = 1;
						// Use 0 instead of bank_to_fire as index when checking the number of external weapon model firingpoints
						if (weapon_model && weapon_model->n_guns)
							if (!(winfo_p->wi_flags[Weapon::Info_Flags::External_weapon_fp]))
								sub_shots = weapon_model->gun_banks[0].num_slots;

						for(int s = 0; s<sub_shots; s++){
							pnt = pm->gun_banks[bank_to_fire].pnt[pt];
							vec3d dir;
							dir = pm->gun_banks[bank_to_fire].norm[pt];
							// Use 0 instead of bank_to_fire as index to external weapon model firingpoints 
							if (weapon_model && weapon_model->n_guns) {
								if (winfo_p->wi_flags[Weapon::Info_Flags::External_weapon_fp]) {
									vm_vec_add2(&pnt, &weapon_model->gun_banks[0].pnt[swp->external_model_fp_counter[bank_to_fire]]);
								} else {
									vm_vec_add2(&pnt, &weapon_model->gun_banks[0].pnt[s]);
								}
							}

							vm_vec_unrotate(&gun_point, &pnt, &obj->orient);
							vm_vec_add(&firing_pos, &gun_point, &obj->pos);

							/*	I AIM autoaim convergence
								II AIM autoaim
								III AIM auto convergence
								IV AIM std convergence
								V SIF convergence
								no convergence or autoaim
							*/
							const bool std_convergence_flagged = (sip->aiming_flags[Object::Aiming_Flags::Std_convergence]) || (winfo_p->aiming_flags[Object::Aiming_Flags::Std_convergence]);

							if (has_autoaim && in_automatic_aim_fov) {
								vec3d firing_vec;

								if (has_converging_autoaim) {
									// converging autoaim
									vm_vec_normalized_dir(&firing_vec, &predicted_target_pos, &firing_pos);
								} else {
									// autoaim
									vm_vec_normalized_dir(&firing_vec, &predicted_target_pos, &obj->pos);
								}

								vm_vector_2_matrix_norm(&firing_orient, &firing_vec, nullptr, nullptr);
							} else if (std_convergence_flagged || (auto_convergence_flagged && (aip->target_objnum != -1))) {
								// std & auto convergence
								vec3d target_vec, firing_vec, convergence_offset;

								// make sure vector is of the set length
								vm_vec_copy_normalize(&target_vec, &firing_orient.vec.fvec);
								if (auto_convergence_flagged && (aip->target_objnum != -1)) {
									// auto convergence
									vm_vec_scale(&target_vec, dist_to_aim);
								} else {
									// std convergence
									// Use the largest convergence distance because default distance is 0.0f and we don't want that!
									const float convergence_distance = std::max(sip->convergence_distance, winfo_p->convergence_distance);
									vm_vec_scale(&target_vec, convergence_distance);
								}

								// if there is convergence offset then make use of it)
								if (sip->aiming_flags[Object::Aiming_Flags::Convergence_offset]) {
									vm_vec_unrotate(&convergence_offset, &sip->convergence_offset, &obj->orient);
									vm_vec_add2(&target_vec, &convergence_offset);
								}

								vm_vec_add2(&target_vec, &obj->pos);
								vm_vec_normalized_dir(&firing_vec, &target_vec, &firing_pos);

								// set orientation
								vm_vector_2_matrix_norm(&firing_orient, &firing_vec, nullptr, nullptr);
							} else if (sip->flags[Ship::Info_Flags::Gun_convergence]) {
								// model file defined convergence
								vec3d firing_vec;
								vm_vec_unrotate(&firing_vec, &pm->gun_banks[bank_to_fire].norm[pt], &obj->orient);
								vm_vector_2_matrix_norm(&firing_orient, &firing_vec, nullptr, nullptr);
							}

							if (winfo_p->wi_flags[Weapon::Info_Flags::Apply_Recoil]){	// Function to add recoil functionality - DahBlount
								vec3d local_impulse = firing_orient.vec.fvec;

								float recoil_force = (winfo_p->mass * winfo_p->max_speed * winfo_p->recoil_modifier * sip->ship_recoil_modifier);

								firepoint_list[current_firepoint++] = firing_pos;

								vm_vec_scale(&local_impulse, (-1 * recoil_force));
								vm_vec_add2(&total_impulse, &local_impulse);
							}

							// create the weapon -- the network signature for multiplayer is created inside
							// of weapon_create							
							weapon_objnum = weapon_create( &firing_pos, &firing_orient, weapon_idx, OBJ_INDEX(obj), new_group_id,
								false, false, swp->primary_bank_fof_cooldown[bank_to_fire], nullptr, launch_curve_data );

							if (weapon_objnum == -1) {
								// Weapon most likely failed to fire
								if (obj == Player_obj) {
									ship_maybe_do_primary_fail_sound_hud(false);
								}
								continue;
							}

							winfo_p = &Weapon_info[Weapons[Objects[weapon_objnum].instance].weapon_info_index];
							has_fired = true;

							weapon_set_tracking_info(weapon_objnum, OBJ_INDEX(obj), aip->target_objnum, aip->current_target_is_locked, aip->targeted_subsys);				

							if (winfo_p->wi_flags[Weapon::Info_Flags::Flak])
							{
								object *target;
								vec3d predicted_pos;
								float flak_range=(winfo_p->lifetime)*(winfo_p->max_speed);
								float range_to_target = flak_range;
								float wepstr=ship_get_subsystem_strength(shipp, SUBSYSTEM_WEAPONS);

								if (aip->target_objnum != -1) {
									target = &Objects[aip->target_objnum];
								} else {
									target = NULL;
								}

								if (target != NULL) {
									set_predicted_enemy_pos(&predicted_pos, obj, &target->pos, &target->phys_info.vel, aip);
									range_to_target=vm_vec_dist(&predicted_pos, &obj->pos);
								}

								//if we have a target and its in range
								if ( (target != NULL) && (range_to_target < flak_range) )
								{
									//set flak range to range of ship
									flak_pick_range(&Objects[weapon_objnum], &firing_pos, &predicted_pos,wepstr);
								}
								else
								{
									flak_set_range(&Objects[weapon_objnum], flak_range - winfo_p->untargeted_flak_range_penalty);
								}
							}
							// create the muzzle flash effect
							shipfx_flash_create( obj, sip->model_num, &pnt, &dir, 1, weapon_idx, weapon_objnum );

							// maybe shudder the ship - if its me
							if((winfo_p->wi_flags[Weapon::Info_Flags::Shudder]) && (obj == Player_obj) && !(Game_mode & GM_STANDALONE_SERVER)){
								// calculate some arbitrary value between 100
								// (mass * velocity) / 10
								game_shudder_apply(500, (winfo_p->mass * winfo_p->max_speed) * 0.1f * sip->ship_shudder_modifier * winfo_p->shudder_modifier);
							}

							num_fired++;

							// maybe add this weapon to the list of those we need to roll forward
							if ((Game_mode & (GM_MULTIPLAYER | GM_STANDALONE_SERVER )) && rollback_shot) {
								multi_ship_record_add_rollback_wep(weapon_objnum);
							}
						}
						swp->external_model_fp_counter[bank_to_fire]++;
					}
				}

				switch (firing_pattern) {
					case FiringPattern::RANDOM_EXHAUSTIVE: {
						if (num_slots < (swp->primary_firepoint_next_to_fire_index[bank_to_fire] + point_count)) {
							std::random_device rd;
							std::shuffle(&swp->primary_firepoint_indices[bank_to_fire][0], &swp->primary_firepoint_indices[bank_to_fire][swp->primary_firepoint_next_to_fire_index[bank_to_fire]-1], std::mt19937(rd())); //NOLINT
						} else if (swp->primary_firepoint_next_to_fire_index[bank_to_fire] < point_count) {
							std::random_device rd;
							std::shuffle(swp->primary_firepoint_indices[bank_to_fire].begin(), swp->primary_firepoint_indices[bank_to_fire].end(), std::mt19937(rd()));
						}
						break;
					}
					case FiringPattern::RANDOM_NONREPEATING: {
						int shuffle_start = MIN(point_count, num_slots - point_count);
						std::random_device rd;
						auto middle_iterator = swp->primary_firepoint_indices[bank_to_fire].begin();
						std::advance(middle_iterator, shuffle_start);
						std::shuffle(middle_iterator, swp->primary_firepoint_indices[bank_to_fire].end(), std::mt19937(rd()));
						std::rotate(swp->primary_firepoint_indices[bank_to_fire].begin(), middle_iterator, swp->primary_firepoint_indices[bank_to_fire].end());
						break;
					}
					case FiringPattern::RANDOM_REPEATING: {
						std::random_device rd;
						std::shuffle(swp->primary_firepoint_indices[bank_to_fire].begin(), swp->primary_firepoint_indices[bank_to_fire].end(), std::mt19937(rd()));
						break;
					}
					default: {
						break;
					}
				}

				if (winfo_p->wi_flags[Weapon::Info_Flags::Apply_Recoil]){
					vec3d avg_firepoint;

					vm_vec_avg_n(&avg_firepoint, (int)current_firepoint, firepoint_list);

					ship_apply_whack(&total_impulse, &avg_firepoint, obj);
					delete[] firepoint_list;
				}
			}

			CLAMP(shipp->weapon_energy, 0.0f, sip->max_weapon_reserve);

			banks_fired |= (1<<bank_to_fire);				// mark this bank as fired.
		}

		
		// Only play the weapon fired sound if it hasn't been played yet.  This is to 
		// avoid playing the same sound multiple times when banks are linked with the
		// same weapon.

		if (!(winfo_p->wi_flags[Weapon::Info_Flags::Beam])){	// not a beam weapon?
			if ( sound_played != winfo_p->launch_snd ) {
				sound_played = winfo_p->launch_snd;
				if ( obj == Player_obj ) {
					weapon_info *wip;
					ship_weapon *sw_pl;

					//Update the last timestamp until continous fire is over, so we have the timestamp of the cease-fire.
					if (shipp->was_firing_last_frame[bank_to_fire] == 1) {
						swp->last_primary_fire_sound_stamp[bank_to_fire] = timestamp();
					}

					//Check for pre-launch sound and play if relevant
					if( (winfo_p->pre_launch_snd.isValid())									//If this weapon type has a pre-fire sound
						&& ((timestamp() - swp->last_primary_fire_sound_stamp[bank_to_fire]) >= winfo_p->pre_launch_snd_min_interval)	//and if we're past our minimum delay from the last cease-fire
						&& (shipp->was_firing_last_frame[bank_to_fire] == 0)				//and if we are at the beginning of a firing stream
					){ 
						snd_play( gamesnd_get_game_sound(winfo_p->pre_launch_snd), 0.0f, 1.0f, SND_PRIORITY_MUST_PLAY); //play it
					} else { //Otherwise, play normal firing sounds
						// HACK
						if(winfo_p->launch_snd == gamesnd_id(GameSounds::AUTOCANNON_SHOT)){
							snd_play( gamesnd_get_game_sound(winfo_p->launch_snd), 0.0f, 1.0f, SND_PRIORITY_TRIPLE_INSTANCE );
						} else if (winfo_p->cockpit_launch_snd.isValid()) {
							snd_play(gamesnd_get_game_sound(winfo_p->cockpit_launch_snd), 0.0f, 1.0f, SND_PRIORITY_MUST_PLAY);
						} else if (winfo_p->launch_snd.isValid()) {
							snd_play( gamesnd_get_game_sound(winfo_p->launch_snd), 0.0f, 1.0f, SND_PRIORITY_MUST_PLAY );
						}
					}
	
					sw_pl = &Player_ship->weapons;
					if (sw_pl->current_primary_bank >= 0)
					{
						wip = &Weapon_info[sw_pl->primary_bank_weapons[sw_pl->current_primary_bank]];
						int force_level = (int) ((wip->armor_factor + wip->shield_factor * 0.2f) * (wip->damage * wip->damage - 7.5f) * 0.45f + 0.6f) * 10 + 2000;

						// modify force feedback for ballistics: make it stronger
						if (wip->wi_flags[Weapon::Info_Flags::Ballistic])
							joy_ff_play_primary_shoot(force_level * 2);
						// no ballistics
						else
							joy_ff_play_primary_shoot(force_level);
					}
				}else {
					if ( winfo_p->launch_snd.isValid() ) {
						snd_play_3d( gamesnd_get_game_sound(winfo_p->launch_snd), &obj->pos, &View_position );
					}	
				}
			}	
		}

		shipp->was_firing_last_frame[bank_to_fire] = 1;
	}	// end for (go to next primary bank)
	
	// if multiplayer and we're client-side firing, send the packet
	if(Game_mode & GM_MULTIPLAYER){
		// if I'm a Host send a primary fired packet packet if it's brand new
		if(MULTIPLAYER_MASTER && !rollback_shot) {
			send_NEW_primary_fired_packet(shipp, banks_fired);
		// or if I'm a client, and it is my ship send it for rollback on the server.
		} else if (MULTIPLAYER_CLIENT && (shipp == Player_ship)) {
				send_non_homing_fired_packet(shipp, banks_fired);
		}		 
	}

   // STATS
   if (obj->flags[Object::Object_Flags::Player_ship]) {
		// in multiplayer -- only the server needs to keep track of the stats.  Call the cool
		// function to find the player given the object *.  It had better return a valid player
		// or our internal structure as messed up.
		if( Game_mode & GM_MULTIPLAYER ) {
			if ( Net_player->flags & NETINFO_FLAG_AM_MASTER ) {
				int player_num;

				player_num = multi_find_player_by_object ( obj );
				Assert ( player_num != -1 );

				Net_players[player_num].m_player->stats.mp_shots_fired += num_fired;
			}
		} else {
			Player->stats.mp_shots_fired += num_fired;
		}
	}

	if (has_fired) {
		object *objp = &Objects[shipp->objnum];
		object* target;
		if (Ai_info[shipp->ai_index].target_objnum != -1)
			target = &Objects[Ai_info[shipp->ai_index].target_objnum];
		else
			target = NULL;
		if (objp == Player_obj && Player_ai->target_objnum != -1)
			target = &Objects[Player_ai->target_objnum]; 

		SCP_vector<int> firedWeapons;

		for (int bank = 0; bank < MAX_SHIP_PRIMARY_BANKS; bank++) {
			if (banks_fired & (1 << bank)) {
				//Start Animation in Forced mode: Always restart it from its initial position rather than just flip it to FWD motion if it was still moving. This is to make it work best for uses like recoil.
				sip->animations.getAll(model_get_instance(shipp->model_instance_num), animation::ModelAnimationTriggerType::PrimaryFired, bank).start(animation::ModelAnimationDirection::FWD, true);
				firedWeapons.emplace_back(shipp->weapons.primary_bank_weapons[bank]);
			}
		}

		if (scripting::hooks::OnWeaponFired->isActive() || scripting::hooks::OnPrimaryFired->isActive()) {
			auto param_list = scripting::hook_param_list(
				scripting::hook_param("User", 'o', objp),
				scripting::hook_param("Target", 'o', target)
			);
			auto conditions = scripting::hooks::WeaponUsedConditions{ shipp, target, std::move(firedWeapons), true };
			scripting::hooks::OnWeaponFired->run(conditions, param_list);
			scripting::hooks::OnPrimaryFired->run(std::move(conditions), std::move(param_list));
		}
	}

	return num_fired;
}

static void ship_start_targeting_laser(ship *shipp)
{	
	int bank0_laser = 0;
	int bank1_laser = 0;

	// determine if either of our banks have a targeting laser
	if((shipp->weapons.primary_bank_weapons[0] >= 0) && (Weapon_info[shipp->weapons.primary_bank_weapons[0]].wi_flags[Weapon::Info_Flags::Beam]) && (Weapon_info[shipp->weapons.primary_bank_weapons[0]].b_info.beam_type == BeamType::TARGETING)){
		bank0_laser = 1;
	}
	if((shipp->weapons.primary_bank_weapons[1] >= 0) && (Weapon_info[shipp->weapons.primary_bank_weapons[1]].wi_flags[Weapon::Info_Flags::Beam]) && (Weapon_info[shipp->weapons.primary_bank_weapons[1]].b_info.beam_type == BeamType::TARGETING)){
		bank1_laser = 1;
	}

	// if primary banks are linked
	if(shipp->flags[Ship_Flags::Primary_linked]){
		if(bank0_laser){
			shipp->targeting_laser_bank = 0;
			return;
		} 
		if(bank1_laser){
			shipp->targeting_laser_bank = 1;
			return;
		}
	}
	// if we only have 1 bank selected
	else {
		if(bank0_laser && (shipp->weapons.current_primary_bank == 0)){
			shipp->targeting_laser_bank = 0;
			return;
		}
		if(bank1_laser && (shipp->weapons.current_primary_bank == 1)){
			shipp->targeting_laser_bank = 1;
			return;
		}
	}
}

static void ship_stop_targeting_laser(ship *shipp)
{
	shipp->targeting_laser_bank = -1;
	shipp->targeting_laser_objnum = -1; // erase old laser obj num if it has any -Bobboau
}

void ship_process_targeting_lasers()
{
	fighter_beam_fire_info fire_info;
	ship_obj *so;
	ship *shipp;	
	polymodel *m;

	// interate over all ships
	for ( so = GET_FIRST(&Ship_obj_list); so != END_OF_LIST(&Ship_obj_list); so = GET_NEXT(so) ) {
		if (Objects[so->objnum].flags[Object::Object_Flags::Should_be_dead])
			continue;

		// sanity checks
		if(Objects[so->objnum].type != OBJ_SHIP){
			continue;
		}
		if(Objects[so->objnum].instance < 0){
			continue;
		}
		shipp = &Ships[Objects[so->objnum].instance];

		// if our trigger is no longer down, switch it off
		if(!(shipp->flags[Ship_Flags::Trigger_down])){
			ship_stop_targeting_laser(shipp);
			continue;
		}		

		// if we have a bank to fire - fire it
		if((shipp->targeting_laser_bank >= 0) && (shipp->targeting_laser_bank < 2)){
			// try and get the model
			m = model_get(Ship_info[shipp->ship_info_index].model_num);
			if(m == NULL){
				continue;
			}

			// fire a targeting laser
			fire_info.life_left = 0.0;					//for fighter beams
			fire_info.life_total = 0.0f;					//for fighter beams
			fire_info.warmdown_stamp = -1;				//for fighter beams
			fire_info.warmup_stamp = -1;				//for fighter beams
			fire_info.accuracy = 0.0f;
			fire_info.beam_info_index = shipp->weapons.primary_bank_weapons[(int)shipp->targeting_laser_bank];
			fire_info.beam_info_override = NULL;
			fire_info.shooter = &Objects[shipp->objnum];
			fire_info.target = NULL;
			fire_info.target_subsys = NULL;
			fire_info.turret = NULL;
			fire_info.local_fire_postion = m->gun_banks[shipp->targeting_laser_bank].pnt[0];			
			shipp->targeting_laser_objnum = beam_fire_targeting(&fire_info);			

			// hmm, why didn't it fire?
			if(shipp->targeting_laser_objnum < 0){
				Int3();
				ship_stop_targeting_laser(shipp);
			}
		}
	}}

/**
 * Maybe detonate secondary weapon that's already out.
 * @return Return true if we detonate it, false if not.
 */
static bool ship_fire_secondary_detonate(object *obj, ship_weapon *swp)
{
	if (swp->remote_detonaters_active > 0) {
		if (timestamp_elapsed(swp->detonate_weapon_time)) {
			missile_obj	*mo;

			// check for currently locked missiles (highest precedence)
			for ( mo = GET_FIRST(&Missile_obj_list); mo != END_OF_LIST(&Missile_obj_list); mo = GET_NEXT(mo) ) {
				object	*mobjp;
				Assert(mo->objnum >= 0 && mo->objnum < MAX_OBJECTS);
				mobjp = &Objects[mo->objnum];
				if (mobjp->flags[Object::Object_Flags::Should_be_dead])
					continue;

				if (mobjp->parent_sig == obj->parent_sig && Weapon_info[Weapons[mobjp->instance].weapon_info_index].wi_flags[Weapon::Info_Flags::Remote]) {
					// dont detonate if this guy just spawned i.e. we just spawned him in a previous iteration of this loop
					if (Missiontime - Weapons[mobjp->instance].creation_time > fl2f(0.01)) {
						weapon_detonate(mobjp);
					}
				}
			}

			return true;
		}
	}

	return false;
}

extern void ai_maybe_announce_shockwave_weapon(object *firing_objp, int weapon_index);

//	Object *obj fires its secondary weapon, if it can.
//	If its most recently fired weapon is a remotely detonatable weapon, detonate it.
//	Returns number of weapons fired.  Note, for swarmers, returns 1 if it is allowed
//	to fire the missiles when allow_swarm is NOT set.  They don't actually get fired on a call here unless allow_swarm is set.
//	When you want to fire swarmers, you call this function with allow_swarm NOT set and frame interval
//	code comes aruond and fires it.
// allow_swarm -> default value is 0... since swarm missiles are fired over several frames,
//                need to avoid firing when normally called
int ship_fire_secondary( object *obj, int allow_swarm, bool rollback_shot )
{
	int			n, weapon_idx, j, bank, bank_adjusted, starting_bank_count = -1, num_fired;
	ushort		starting_sig = 0;
	ship			*shipp;
	ship_weapon *swp;
	ship_info	*sip;
	weapon_info	*wip;
	ai_info		*aip;
	polymodel	*pm;
	vec3d		missile_point, pnt, firing_pos;
	bool has_fired = false;		// Used to determine whether to fire the scripting hook

	Assert( obj != NULL );

	// in the case where the server is an observer, he can fire (which would be bad) - unless we do this.
	if( obj->type == OBJ_OBSERVER ){
		return 0;
	}

	// in the case where the object is a ghost (a delayed fire packet from right before he died, for instance)
	if( (obj->type == OBJ_GHOST) || (obj->type == OBJ_NONE) ){
		return 0;
	}

	Assert( obj->type == OBJ_SHIP );
	if(obj->type != OBJ_SHIP){
		return 0;
	}
	n = obj->instance;
	Assert( n >= 0 && n < MAX_SHIPS );
	if((n < 0) || (n >= MAX_SHIPS)){
		return 0;
	}
	Assert( Ships[n].objnum == OBJ_INDEX(obj));
	if(Ships[n].objnum != OBJ_INDEX(obj)){
		return 0;
	}

	bool in_lab = (gameseq_get_state() == GS_STATE_LAB);
	
	shipp = &Ships[n];
	swp = &shipp->weapons;
	sip = &Ship_info[shipp->ship_info_index];
	aip = &Ai_info[shipp->ai_index];

	// if no secondary weapons are present on ship, return
	if ( swp->num_secondary_banks <= 0 ){
		return 0;
	}

	// If the secondaries have been locked, bail
	if (shipp->flags[Ship_Flags::Secondaries_locked] && !(obj == Player_obj && shipp->flags[Ship_Flags::Fail_sound_locked_secondary]))
	{
		return 0;
	}

	// If ship is being repaired/rearmed, it cannot fire missiles
	if ( aip->ai_flags[AI::AI_Flags::Being_repaired] ) {
		return 0;
	}

	num_fired = 0;		// tracks how many missiles actually fired

	// niffiwan: allow swarm/corkscrew bank to keep firing if current bank changes
	if (shipp->swarm_missile_bank != -1 && allow_swarm) {
		bank = shipp->swarm_missile_bank;
	} else if (shipp->corkscrew_missile_bank != -1 && allow_swarm) {
		bank = shipp->corkscrew_missile_bank;
	} else {
		bank = swp->current_secondary_bank;
	}

	if ( bank < 0 || bank >= sip->num_secondary_banks ) {
		return 0;
	}
	bank_adjusted = MAX_SHIP_PRIMARY_BANKS + bank;

	if (swp->secondary_animation_position[bank] == MA_POS_SET) {
		if ( timestamp_elapsed(swp->secondary_animation_done_time[bank]) )
			swp->secondary_animation_position[bank] = MA_POS_READY;
		else
			return 0;
	}

	weapon_idx = swp->secondary_bank_weapons[bank];

	// It's possible for banks to be empty without issue
	// but indices outside the weapon_info range are a problem
	Assert(swp->secondary_bank_weapons[bank] < weapon_info_size());
	if((swp->secondary_bank_weapons[bank] < 0) || (swp->secondary_bank_weapons[bank] >= weapon_info_size())){
		return 0;
	}

	wip = &Weapon_info[weapon_idx];

	if ( MULTIPLAYER_MASTER ) {
		starting_sig = multi_get_next_network_signature( MULTI_SIG_NON_PERMANENT );
		starting_bank_count = swp->secondary_bank_ammo[bank];
	}

	if (ship_fire_secondary_detonate(obj, swp)) {
		// in multiplayer, master sends a secondary fired packet with starting signature of -1 -- indicates
		// to client code to set the detonate timer to 0.
		if ( MULTIPLAYER_MASTER ) {
			send_secondary_fired_packet( shipp, 0, starting_bank_count, 1, allow_swarm );
		}
	
		//	For all banks, if ok to fire a weapon, make it wait a bit.
		//	Solves problem of fire button likely being down next frame and
		//	firing weapon despite fire causing detonation of existing weapon.
		if (swp->current_secondary_bank >= 0) {
			if (timestamp_elapsed(swp->next_secondary_fire_stamp[bank])){
				swp->next_secondary_fire_stamp[bank] = timestamp(MAX((int) flFrametime*3000, 250));
			}
		}
		return 0;
	}

	// niffiwan: 04/03/12: duplicate of a check approx 100 lines above - not needed?
	if ( bank < 0 ){
		return 0;
	}

	float dist_to_target = 0.f;
	if (aip->target_objnum != -1) {
		dist_to_target = vm_vec_dist_quick(&obj->pos, &Objects[aip->target_objnum].pos);
	};

	if ( !timestamp_elapsed(swp->next_secondary_fire_stamp[bank]) && !allow_swarm) {
		if (timestamp_until(swp->next_secondary_fire_stamp[bank]) > 60000){
			swp->next_secondary_fire_stamp[bank] = timestamp(1000);
		}
		goto done_secondary;
	}

	// Ensure if this is a "require-lock" missile, that a lock actually exists
	if (wip->wi_flags[Weapon::Info_Flags::No_dumbfire] && !in_lab) {
		if (!aip->current_target_is_locked && !ship_lock_present(shipp) && shipp->missile_locks_firing.empty() && aip->ai_missile_locks_firing.empty()) {
			if (obj == Player_obj) {
				if (!Weapon_energy_cheat) {
					float max_dist;

					max_dist = wip->lifetime * wip->max_speed;
					if (wip->wi_flags[Weapon::Info_Flags::Local_ssm]) {
						max_dist = wip->lssm_lock_range;
					}

					if ((aip->target_objnum != -1) &&
						(dist_to_target > max_dist)) {
						HUD_sourced_printf(HUD_SOURCE_HIDDEN, "%s", XSTR("Too far from target to acquire lock", 487));
					} else {
						HUD_sourced_printf(HUD_SOURCE_HIDDEN, XSTR("Cannot fire %s without a lock", 488), wip->get_display_name());
					}

					snd_play(gamesnd_get_game_sound(ship_get_sound(Player_obj, GameSounds::OUT_OF_MISSLES)));
					swp->next_secondary_fire_stamp[bank] = timestamp(800); // to avoid repeating messages
					return 0;
				}
			} else {
				// multiplayer clients should always fire the weapon here, so return only if not
				// a multiplayer client.
				if (!MULTIPLAYER_CLIENT) {
					return 0;
				}
			}
		}
	}

	if (wip->wi_flags[Weapon::Info_Flags::Tagged_only] && !in_lab)
	{
		if (!ship_is_tagged(&Objects[aip->target_objnum]))
		{
			if (obj==Player_obj || (MULTIPLAYER_MASTER && obj->flags[Object::Object_Flags::Player_ship]))
			{
				if ( !Weapon_energy_cheat )
				{
					HUD_sourced_printf(HUD_SOURCE_HIDDEN, NOX("Cannot fire %s if target is not tagged"), wip->get_display_name());
					snd_play( gamesnd_get_game_sound(ship_get_sound(Player_obj, GameSounds::OUT_OF_MISSLES)) );
					swp->next_secondary_fire_stamp[bank] = timestamp(800);	// to avoid repeating messages
					return 0;
				}
			}
			else
			{
				if ( !MULTIPLAYER_CLIENT )
				{
					return 0;
				}
			}
		}
	}

	if ( !allow_swarm && (obj == Player_obj || (MULTIPLAYER_MASTER && obj->flags[Object::Object_Flags::Player_ship] ))) {
		ship_queue_missile_locks(shipp);
	}

	// if trying to fire a swarm missile, make sure being called from right place
	if ( (wip->wi_flags[Weapon::Info_Flags::Swarm]) && !allow_swarm ) {
		Assert(wip->swarm_count > 0);
		if (wip->multi_lock) {
			if (in_lab) {
				shipp->num_swarm_missiles_to_fire = wip->max_seeking;
			} else {
				if (obj == Player_obj || (MULTIPLAYER_MASTER && obj->flags[Object::Object_Flags::Player_ship]))
					shipp->num_swarm_missiles_to_fire = static_cast<int>(shipp->missile_locks_firing.size());
				else // AI ships
					shipp->num_swarm_missiles_to_fire = static_cast<int>(aip->ai_missile_locks_firing.size());
			}
		} else if(wip->swarm_count <= 0){
			shipp->num_swarm_missiles_to_fire = SWARM_DEFAULT_NUM_MISSILES_FIRED;
		} else {
			shipp->num_swarm_missiles_to_fire = wip->swarm_count;
		}

		shipp->swarm_missile_bank = bank;
		return 1;		//	Note: Missiles didn't get fired, but the frame interval code will fire them.
	}

	// if trying to fire a corkscrew missile, make sure being called from right place	
	if ( (wip->wi_flags[Weapon::Info_Flags::Corkscrew]) && !allow_swarm ) {
		//phreak 11-9-02 
		//changed this from 4 to custom number defined in tables
		if (wip->multi_lock) {
			if (in_lab) {
				shipp->num_corkscrew_to_fire = static_cast<ubyte>(wip->max_seeking);
			} else {
				if (obj == Player_obj || (MULTIPLAYER_MASTER && obj->flags[Object::Object_Flags::Player_ship]))
					shipp->num_corkscrew_to_fire = static_cast<ubyte>(shipp->missile_locks_firing.size());
				else // AI ships
					shipp->num_corkscrew_to_fire = static_cast<ubyte>(aip->ai_missile_locks_firing.size());
			}
		} else {
			shipp->num_corkscrew_to_fire = (ubyte)(shipp->num_corkscrew_to_fire + (ubyte)wip->cs_num_fired);
		}
		shipp->corkscrew_missile_bank = bank;
		return 1;		//	Note: Missiles didn't get fired, but the frame interval code will fire them.
	}	

	float t;

	if (Weapon_info[weapon_idx].burst_shots > swp->burst_counter[bank_adjusted]) {
		t = Weapon_info[weapon_idx].burst_delay;
		swp->burst_counter[bank_adjusted]++;
	} else {
		t = Weapon_info[weapon_idx].fire_wait;	// They can fire 5 times a second
		swp->burst_counter[bank_adjusted] = 0;
		swp->burst_seed[bank_adjusted] = Random::next();
	}
	swp->next_secondary_fire_stamp[bank] = timestamp((int)(t * 1000.0f));
	swp->last_secondary_fire_stamp[bank] = timestamp();

	// Here is where we check if weapons subsystem is capable of firing the weapon.
	// do only in single player or if I am the server of a multiplayer game
	if ( !(Game_mode & GM_MULTIPLAYER) || MULTIPLAYER_MASTER ) {
		if (shipp->flags[Ship_Flags::Secondaries_locked] && (obj == Player_obj && shipp->flags[Ship_Flags::Fail_sound_locked_secondary])) {
			ship_maybe_do_secondary_fail_sound_hud(wip, false);
			goto done_secondary;
		}
		if ( ship_weapon_maybe_fail(shipp) ) {
			if ( obj == Player_obj ) 
				if ( ship_maybe_do_secondary_fail_sound_hud(wip, false) ) {
					HUD_sourced_printf(HUD_SOURCE_HIDDEN, XSTR( "Cannot fire %s due to weapons system damage", 489), Weapon_info[weapon_idx].get_display_name());
				}
			goto done_secondary;
		}
	}

	pm = model_get( sip->model_num );
	if ( pm->n_missiles > 0 ) {
		int check_ammo;		// used to tell if we should check ammo counts or not
		int num_slots;

		if ( bank > pm->n_missiles ) {
			nprintf(("WARNING","WARNING ==> Tried to fire bank %d, but ship has only %d banks\n", bank+1, pm->n_missiles));
			return 0;		// we can make a quick out here!!!
		}

		int target_objnum;
		ship_subsys *target_subsys;
		int locked;

		if ( obj == Player_obj || ( MULTIPLAYER_MASTER && obj->flags[Object::Object_Flags::Player_ship] ) ) {
			// use missile lock slots
			if ( !shipp->missile_locks_firing.empty() ) {
				lock_info lock_data = shipp->missile_locks_firing.back();

				/* The conditional here might seem complicated but it is required to ensure that each use case is handled correctly.
				 * While all missiles need to have their locks cleared, swarms and corkscrews need them cleared only after the last missile is going to be
				 * or has been fired. (DahBlount)
				**/
				if ( wip->multi_lock || 
					 !(wip->wi_flags[Weapon::Info_Flags::Corkscrew] || wip->wi_flags[Weapon::Info_Flags::Swarm]) ||
					 (shipp->num_corkscrew_to_fire <= 1 && shipp->num_swarm_missiles_to_fire <= 1) ) {
					shipp->missile_locks_firing.pop_back();
				}

				if (lock_data.obj != nullptr) {
					target_objnum = OBJ_INDEX(lock_data.obj);
					target_subsys = lock_data.subsys;
					locked = 1;
				} else {
					target_objnum = -1;
					target_subsys = nullptr;
					locked = 0;
				}
			} else if (wip->wi_flags[Weapon::Info_Flags::Homing_heat]) {
				target_objnum = aip->target_objnum;
				target_subsys = aip->targeted_subsys;
				locked = aip->current_target_is_locked;
			} else {
				target_objnum = -1;
				target_subsys = nullptr;
				locked = 0;
			}
		} else if (wip->multi_lock && !aip->ai_missile_locks_firing.empty()) {
			target_objnum = aip->ai_missile_locks_firing.back().first;
			target_subsys = aip->ai_missile_locks_firing.back().second;
			locked = 1;
			aip->ai_missile_locks_firing.pop_back();
		} else {
			target_objnum = aip->target_objnum;
			target_subsys = aip->targeted_subsys;
			locked = aip->current_target_is_locked;
		}

		num_slots = pm->missile_banks[bank].num_slots;
		float target_radius = 0.f;

		if (target_objnum >= 0) {
			target_radius = Objects[target_objnum].radius;
		}

		auto launch_curve_data = WeaponLaunchCurveData {
			num_slots,
			dist_to_target,
			target_radius,
		};

		// determine if there is enough ammo left to fire weapons on this bank.  As with primary
		// weapons, we might or might not check ammo counts depending on game mode, who is firing,
		// and if I am a client in multiplayer
		check_ammo = 1;

		if (in_lab || (MULTIPLAYER_CLIENT && (obj != Player_obj))) {
			check_ammo = 0;
		}

		bool no_ammo_needed = Weapon_info[swp->secondary_bank_weapons[bank]].wi_flags[Weapon::Info_Flags::SecondaryNoAmmo];
		bool no_energy = shipp->weapon_energy < wip->energy_consumed;
		if ( check_ammo && ((swp->secondary_bank_ammo[bank] <= 0 && !no_ammo_needed) || no_energy)) {
			if ( shipp->objnum == OBJ_INDEX(Player_obj) ) {
				if (ship_maybe_do_secondary_fail_sound_hud(wip, no_energy) ) {
//					HUD_sourced_printf(HUD_SOURCE_HIDDEN, "No %s missiles left in bank", Weapon_info[swp->secondary_bank_weapons[bank]].name);
				}
			}
			else {
				// TODO:  AI switch secondary weapon / re-arm?
			}
			goto done_secondary;
		}

		// Handle the optional disabling of dual fire
		// dual fire/doublefire can be disabled for individual weapons, for players, or for AIs
		// if any of these apply to the current weapon, unset the dual fire flag on the ship 
		// then proceed as normal.
		// This is only handled at firing time so dualfire isn't lost when cycling through weapons
		if (shipp->flags[Ship_Flags::Secondary_dual_fire] &&
			( wip->wi_flags[Weapon::Info_Flags::No_doublefire] || 
			( The_mission.ai_profile->flags[AI::Profile_Flags::Disable_ai_secondary_doublefire] && 
				shipp->objnum != OBJ_INDEX(Player_obj) ) ||
			( The_mission.ai_profile->flags[AI::Profile_Flags::Disable_player_secondary_doublefire] &&
				shipp->objnum == OBJ_INDEX(Player_obj) ))
			) {
			shipp->flags.remove(Ship_Flags::Secondary_dual_fire);
		}

		int start_slot, end_slot;
		no_energy = shipp->weapon_energy < 2 * wip->energy_consumed; // whether there's enough energy for at least 1 shot was checked above

		if ( shipp->flags[Ship_Flags::Secondary_dual_fire] && num_slots > 1) {
			start_slot = swp->secondary_next_slot[bank];
			// AL 11-19-97: Ensure enough ammo remains when firing linked secondary weapons
			if ( check_ammo && ((swp->secondary_bank_ammo[bank] < 2 && !no_ammo_needed) || no_energy) ) {
				end_slot = start_slot;
			} else {
				end_slot = start_slot+1;
			}
		} else {
			// de-set the flag just in case dual-fire was set but couldn't be used
			// because there's less than two firepoints
			shipp->flags.remove(Ship_Flags::Secondary_dual_fire);
			start_slot = swp->secondary_next_slot[bank];
			end_slot = start_slot;
		}

		int pnt_index=start_slot;
		//If this is a tertiary weapon, only subtract one piece of ammo
		for ( j = start_slot; j <= end_slot; j++ ) {
			int	weapon_num;

			swp->secondary_next_slot[bank]++;
			if ( swp->secondary_next_slot[bank] > (num_slots-1) ){
				swp->secondary_next_slot[bank] = 0;
			}

			if ( pnt_index >= num_slots ){
				pnt_index = 0;
			}
			shipp->secondary_point_reload_pct.set(bank, pnt_index, 0.0f);
			pnt = pm->missile_banks[bank].pnt[pnt_index];
			vec3d dir;
			dir = pm->missile_banks[bank].norm[pnt_index++];

			polymodel *weapon_model = NULL;
			if(wip->external_model_num >= 0){
				weapon_model = model_get(wip->external_model_num);
			}

			if (weapon_model && weapon_model->n_guns) {
				int external_bank = bank + MAX_SHIP_PRIMARY_BANKS;
				if (wip->wi_flags[Weapon::Info_Flags::External_weapon_fp]) {
					if ((weapon_model->n_guns <= swp->external_model_fp_counter[external_bank]) || (swp->external_model_fp_counter[external_bank] < 0))
						swp->external_model_fp_counter[external_bank] = 0;
					vm_vec_add2(&pnt, &weapon_model->gun_banks[0].pnt[swp->external_model_fp_counter[external_bank]]);
					swp->external_model_fp_counter[external_bank]++;
				} else {
					// make it use the 0 index slot
					vm_vec_add2(&pnt, &weapon_model->gun_banks[0].pnt[0]);
				}
			}
			vm_vec_unrotate(&missile_point, &pnt, &obj->orient);
			vm_vec_add(&firing_pos, &missile_point, &obj->pos);

			if ( Game_mode & GM_MULTIPLAYER ) {
				Assert( Weapon_info[weapon_idx].subtype == WP_MISSILE );
			}

			matrix firing_orient;
			if(!(sip->flags[Ship::Info_Flags::Gun_convergence]))
			{
				firing_orient = obj->orient;
			}
			else
			{
				vec3d firing_vec;
				vm_vec_unrotate(&firing_vec, &pm->missile_banks[bank].norm[pnt_index-1], &obj->orient);
				vm_vector_2_matrix_norm(&firing_orient, &firing_vec, nullptr, nullptr);
			}

			// create the weapon -- for multiplayer, the net_signature is assigned inside
			// of weapon_create
			weapon_num = weapon_create( &firing_pos, &firing_orient, weapon_idx, OBJ_INDEX(obj), -1, locked, false, 0.f, nullptr, launch_curve_data);

			if (weapon_num == -1) {
				// Weapon most likely failed to fire
				if (obj == Player_obj) {
					ship_maybe_do_secondary_fail_sound_hud(wip, false);
				}
				continue;
			}

			if (weapon_num >= 0) {
				weapon_idx = Weapons[Objects[weapon_num].instance].weapon_info_index;
				weapon_set_tracking_info(weapon_num, OBJ_INDEX(obj), target_objnum, locked, target_subsys);
				has_fired = true;

				// create the muzzle flash effect
				shipfx_flash_create(obj, sip->model_num, &pnt, &dir, 0, weapon_idx, weapon_num);

				if((wip->wi_flags[Weapon::Info_Flags::Shudder]) && (obj == Player_obj) && !(Game_mode & GM_STANDALONE_SERVER)){
					// calculate some arbitrary value between 100
					// (mass * velocity) / 10
					game_shudder_apply(500, (wip->mass * wip->max_speed) * 0.1f * sip->ship_shudder_modifier * wip->shudder_modifier);
				}

				num_fired++;
				swp->detonate_weapon_time = timestamp((int)(DEFAULT_REMOTE_DETONATE_TRIGGER_WAIT * 1000));;		//	Can detonate 1/2 second later.
				if (Weapon_info[weapon_idx].wi_flags[Weapon::Info_Flags::Remote])
					swp->remote_detonaters_active++;

				// possibly add this to the rollback vector
				if ((Game_mode & (GM_MULTIPLAYER | GM_STANDALONE_SERVER)) && rollback_shot){
					multi_ship_record_add_rollback_wep(weapon_num);
				}

				// subtract the number of missiles fired
				if ( !Weapon_energy_cheat ){
					if(!Weapon_info[swp->secondary_bank_weapons[bank]].wi_flags[Weapon::Info_Flags::SecondaryNoAmmo])
						swp->secondary_bank_ammo[bank]--;

					shipp->weapon_energy -= wip->energy_consumed;
				}
			}
		}
	}

	if ( obj == Player_obj ) {
		if ( Weapon_info[weapon_idx].cockpit_launch_snd.isValid() ) {
			snd_play( gamesnd_get_game_sound(Weapon_info[weapon_idx].cockpit_launch_snd), 0.0f, 1.0f, SND_PRIORITY_MUST_PLAY );
		} else if (Weapon_info[weapon_idx].launch_snd.isValid()) {
			snd_play(gamesnd_get_game_sound(Weapon_info[weapon_idx].launch_snd), 0.0f, 1.0f, SND_PRIORITY_MUST_PLAY);
		}

		swp = &Player_ship->weapons;
		if (bank >= 0) {
			wip = &Weapon_info[swp->secondary_bank_weapons[bank]];
			if (Player_ship->flags[Ship_Flags::Secondary_dual_fire]){
				joy_ff_play_secondary_shoot((int) (wip->cargo_size * 2.0f));
			} else {
				joy_ff_play_secondary_shoot((int) wip->cargo_size);
			}
		}

	} else {
		if ( Weapon_info[weapon_idx].launch_snd.isValid() ) {
			snd_play_3d( gamesnd_get_game_sound(Weapon_info[weapon_idx].launch_snd), &obj->pos, &View_position );
		}
	}

done_secondary:

	if(num_fired > 0){
		// if I am the master of a multiplayer game, send a secondary fired packet along with the
		// first network signatures for the newly created weapons.
		// Cyborg17 - If this is a rollback shot, the server will let the player know within the packet.
		if ( MULTIPLAYER_MASTER ) {			
			Assert(starting_sig != 0);
			send_secondary_fired_packet( shipp, starting_sig, starting_bank_count, num_fired, allow_swarm );			
		}

		// Handle Player only stuff, including stats and client secondary packets
		if (obj->flags[Object::Object_Flags::Player_ship]) {
			// in multiplayer -- only the server needs to keep track of the stats.  Call the cool
			// function to find the player given the object *.  It had better return a valid player
			// or our internal structure as messed up.
			if( Game_mode & GM_MULTIPLAYER ) {
				if ( Net_player->flags & NETINFO_FLAG_AM_MASTER ) {
					int player_num;

					player_num = multi_find_player_by_object ( obj );
					Assert ( player_num != -1 );

					Net_players[player_num].m_player->stats.ms_shots_fired += num_fired;
				} else if (MULTIPLAYER_CLIENT) {
					if (!wip->is_homing())
					send_non_homing_fired_packet(shipp, num_fired, true);
				}
			} else {
				Player->stats.ms_shots_fired += num_fired;
			}
		}
	
		// maybe announce a shockwave weapon
		ai_maybe_announce_shockwave_weapon(obj, weapon_idx);
	}

	// if we are out of ammo in this bank then don't carry over firing swarm/corkscrew
	// missiles to a new bank
	if (!in_lab && (!ship_secondary_has_ammo(swp, bank) || shipp->weapon_energy < wip->energy_consumed)) {
		// NOTE: these are set to 1 since they will get reduced by 1 in the
		//       swarm/corkscrew code once this function returns

		if (shipp->num_swarm_missiles_to_fire > 1) {
			shipp->num_swarm_missiles_to_fire = 1;
			shipp->swarm_missile_bank = -1;
		}

		if (shipp->num_corkscrew_to_fire > 1) {
			shipp->num_corkscrew_to_fire = 1;
			shipp->corkscrew_missile_bank = -1;
		}
	}

	if (has_fired) {
		object *objp = &Objects[shipp->objnum];
		object* target;
		if (Ai_info[shipp->ai_index].target_objnum != -1)
			target = &Objects[Ai_info[shipp->ai_index].target_objnum];
		else
			target = NULL;
		if (objp == Player_obj && Player_ai->target_objnum != -1)
			target = &Objects[Player_ai->target_objnum];

		//Start Animation in Forced mode: Always restart it from its initial position rather than just flip it to FWD motion if it was still moving. This is to make it work best for uses like recoil.
		sip->animations.getAll(model_get_instance(shipp->model_instance_num), animation::ModelAnimationTriggerType::SecondaryFired, bank).start(animation::ModelAnimationDirection::FWD, true);

		if (scripting::hooks::OnWeaponFired->isActive() || scripting::hooks::OnSecondaryFired->isActive()) {
			auto param_list = scripting::hook_param_list(
				scripting::hook_param("User", 'o', objp),
				scripting::hook_param("Target", 'o', target)
			);
			auto conditions = scripting::hooks::WeaponUsedConditions{ shipp, target, SCP_vector<int>{ weapon_idx }, false };
			scripting::hooks::OnWeaponFired->run(conditions, param_list);
			scripting::hooks::OnSecondaryFired->run(std::move(conditions), std::move(param_list));
		}
	}

	// AL 3-7-98: Move to next valid secondary bank if out of ammo
	//

	//21-07-02 01:24 DTP; COMMENTED OUT some of the mistakes
	//this bug was made by AL, when he assumed he had to take the next fire_wait time remaining and add 250 ms of delay to it, 
	//and put it in the next valid bank. for the player to have a 250 ms of penalty
	//
	//what that caused was that the next valid bank inherited the current valid banks FULL fire delay. since he used the Weapon_info struct that has
	// no information / member that stores the next valids banks remaning fire_wait delay.
	//
	//what he should have done was to check of the next valid bank had any fire delay that had elapsed, if it had elapsed, 
	//then it would have no firedelay. and then add 250 ms of delay. in effect, this way there is no penalty if there is any firedelay remaning in
	//the next valid bank. the delay is there to prevent things like Trible/Quad Fire Trebuchets.
	//
	// niffiwan: only try to switch banks if object has multiple banks, and firing bank is the current bank
	if ( (obj->flags[Object::Object_Flags::Player_ship]) && !ship_secondary_has_ammo(swp, bank) && (swp->num_secondary_banks >= 2) && (bank == swp->current_secondary_bank) ) {
		if (ship_select_next_secondary(obj) ) {			//DTP here we switch to the next valid bank, but we can't call weapon_info on next fire_wait

			if ( timestamp_elapsed(shipp->weapons.next_secondary_fire_stamp[shipp->weapons.current_secondary_bank]) ) {	//DTP, this is simply a copy of the manual cycle functions
				shipp->weapons.next_secondary_fire_stamp[shipp->weapons.current_secondary_bank] = timestamp(1000);	//Bumped from 250 to 1000 because some people seem to be to triggerhappy :).
				shipp->weapons.last_secondary_fire_stamp[shipp->weapons.current_secondary_bank] = timestamp();
			}
						
			if ( obj == Player_obj ) {
				snd_play( gamesnd_get_game_sound(ship_get_sound(Player_obj, GameSounds::SECONDARY_CYCLE)) );
			}
		}
	}

	return num_fired;
}

// Goober5000
static bool primary_out_of_ammo(const ship_weapon *swp, int bank)
{
	// true if both ballistic and ammo <= 0,
	// false if not ballistic or if ballistic and ammo > 0
			
	if ( Weapon_info[swp->primary_bank_weapons[bank]].wi_flags[Weapon::Info_Flags::Ballistic] )
	{
		if (swp->primary_bank_ammo[bank] <= 0)
		{
			return true;
		}
	}

	// note: never out of ammo if not ballistic
	return false;
}

/**
 * Return true if a new index gets selected.
 * 
 * @param objp      pointer to object for ship cycling primary
 * @param direction forward == CycleDirection::NEXT, backward == CycleDirection::PREV
 *
 * NOTE: This code can be called for any arbitrary ship.  HUD messages and sounds are only used
 *       for the player ship.
 */
bool ship_select_next_primary(object *objp, CycleDirection direction)
{
	Assert(objp != NULL);
	Assert(objp->type == OBJ_SHIP);
	Assert(objp->instance >= 0 && objp->instance < MAX_SHIPS);

	auto shipp = &Ships[objp->instance];
	auto sip = &Ship_info[shipp->ship_info_index];
	auto swp = &shipp->weapons;

	auto original_bank = swp->current_primary_bank;
	auto original_link_flag = shipp->flags[Ship_Flags::Primary_linked];

	if ( swp->num_primary_banks == 0 )
	{
		if ( objp == Player_obj )
		{
			HUD_sourced_printf(HUD_SOURCE_HIDDEN, "%s", XSTR( "This ship has no primary weapons", 490));
			gamesnd_play_error_beep();
		}
		return false;
	}
	else if ( swp->num_primary_banks == 1 )
	{
		if ( objp == Player_obj )
		{
			HUD_sourced_printf(HUD_SOURCE_HIDDEN, XSTR( "This ship has only one primary weapon: %s", 491),Weapon_info[swp->primary_bank_weapons[swp->current_primary_bank]].get_display_name(), swp->current_primary_bank + 1);
			gamesnd_play_error_beep();
		}
		return false;
	}
	else if ( swp->num_primary_banks > MAX_SHIP_PRIMARY_BANKS )
	{
		UNREACHABLE("The ship %s has more primary banks than the maximum!", shipp->ship_name);
		return false;
	}

	Assert((swp->current_primary_bank >= 0) && (swp->current_primary_bank < swp->num_primary_banks));

	// set parameters based on direction
	// (need the != operator to make this work for both)
	int start, end, increment;
	if (direction == CycleDirection::NEXT)
	{
		start = 0;
		end = swp->num_primary_banks;
		increment = 1;
	}
	else
	{
		start = swp->num_primary_banks - 1;
		end = -1;
		increment = -1;
	}

	// first check if linked
	if (shipp->flags[Ship_Flags::Primary_linked])
	{
		shipp->flags.remove(Ship_Flags::Primary_linked);

		// chose the first possible bank in whatever direction
		bool found = false;
		for (int bank = start; bank != end; bank += increment)
		{
			if (!primary_out_of_ammo(swp, bank))
			{
				swp->current_primary_bank = bank;
				found = true;
				break;
			}
		}
		if (!found)
			swp->current_primary_bank = start;
	}
	// now handle when not linked: cycle and constrain
	else
	{
		int bank = swp->current_primary_bank;

		// see if we can choose a bank in our cycle direction
		bool found = false, looped = false;
		for (int i = 0; i < swp->num_primary_banks; ++i)
		{
			bank += increment;

			if (bank == end)
			{
				looped = true;
				bank = start;
			}

			if (!primary_out_of_ammo(swp, bank))
			{
				found = true;
				break;
			}
		}

		// if there are no valid banks, we can't do anything; a bank must be found to either cycle or link
		if (found)
		{
			// we might want to link
			if (looped && !sip->flags[Ship::Info_Flags::No_primary_linking])
			{
				// check if at least two weapons can be linked
				int num_linkable_weapons = 0;
				for (int j = 0; j < swp->num_primary_banks; ++j)
					if (!primary_out_of_ammo(swp, j) && !Weapon_info[swp->primary_bank_weapons[j]].wi_flags[Weapon::Info_Flags::Nolink])
						num_linkable_weapons++;

				// link them
				if (num_linkable_weapons > 1)
					shipp->flags.set(Ship_Flags::Primary_linked);
				// otherwise just cycle
				else
					swp->current_primary_bank = bank;
			}
			// otherwise just cycle
			else
				swp->current_primary_bank = bank;
		}
	}

	swp->previous_primary_bank = original_bank;

	// check to make sure we actually changed banks (which can fail to happen if e.g. ballistic weapons ran out of ammo)
	if ( (swp->current_primary_bank != original_bank) || ((shipp->flags[Ship_Flags::Primary_linked]) != original_link_flag) )
	{
		if ( objp == Player_obj )
			snd_play( gamesnd_get_game_sound(ship_get_sound(objp, GameSounds::PRIMARY_CYCLE)), 0.0f );

		ship_primary_changed(shipp);

		object* target;
		if (Ai_info[shipp->ai_index].target_objnum != -1)
			target = &Objects[Ai_info[shipp->ai_index].target_objnum];
		else
			target = NULL;

		if (scripting::hooks::OnWeaponSelected->isActive() || scripting::hooks::OnWeaponDeselected->isActive())
		{
			auto param_list = scripting::hook_param_list(
				scripting::hook_param("User", 'o', objp),
				scripting::hook_param("Target", 'o', target)
			);
			scripting::hooks::OnWeaponSelected->run(scripting::hooks::WeaponSelectedConditions{ shipp, swp->current_primary_bank, original_bank, true }, param_list);
			scripting::hooks::OnWeaponDeselected->run(scripting::hooks::WeaponDeselectedConditions{ shipp, swp->current_primary_bank, original_bank, true }, std::move(param_list));
		}

		return true;
	}

	// could not select new weapon:
	if ( objp == Player_obj )
		gamesnd_play_error_beep();

	return false;
}

// ------------------------------------------------------------------------------
// ship_select_next_secondary() selects the next secondary bank with missles
//
//	returns:		1	=> The secondary bank was switched
//					0	=> The secondary bank stayed the same
//
// If a secondary bank has no missles left, it is skipped.
//
// NOTE: This can be called for an arbitrary ship.  HUD messages and sounds are only used
//			for the player ship.
int ship_select_next_secondary(object *objp)
{
	Assert(objp != NULL);
	Assert(objp->type == OBJ_SHIP);
	Assert(objp->instance >= 0 && objp->instance < MAX_SHIPS);

	int	original_bank, new_bank, i;
	ship	*shipp;
	ship_weapon *swp;

	shipp = &Ships[objp->instance];
	swp = &shipp->weapons;

	// redid the switch structure to allow additional seconary banks if added later - Goober5000
	if ( swp->num_secondary_banks == 0 )
	{
		if ( objp == Player_obj )
		{
			HUD_sourced_printf(HUD_SOURCE_HIDDEN, "%s", XSTR( "This ship has no secondary weapons", 492));
			gamesnd_play_error_beep();
		}
		return 0;
	}
	else if ( swp->num_secondary_banks == 1 )
	{
		if ( objp == Player_obj )
		{
			HUD_sourced_printf(HUD_SOURCE_HIDDEN, XSTR( "This ship has only one secondary weapon: %s", 493), Weapon_info[swp->secondary_bank_weapons[swp->current_secondary_bank]].get_display_name(), swp->current_secondary_bank + 1);
			gamesnd_play_error_beep();
		}
		return 0;
	}
	else if ( swp->num_secondary_banks > MAX_SHIP_SECONDARY_BANKS )
	{
		Int3();
		return 0;
	}
	else
	{
		Assert((swp->current_secondary_bank >= 0) && (swp->current_secondary_bank < swp->num_secondary_banks));

		original_bank = swp->current_secondary_bank;

		for ( i = 1; i < swp->num_secondary_banks; i++ ) {
			new_bank = (swp->current_secondary_bank+i) % swp->num_secondary_banks;
			if ( !ship_secondary_has_ammo(swp, new_bank))
				continue;
			swp->current_secondary_bank = new_bank;
			break;
		}

		if ( swp->current_secondary_bank != original_bank )
		{
			swp->previous_secondary_bank = original_bank;
			if ( objp == Player_obj )
			{
				snd_play( gamesnd_get_game_sound(ship_get_sound(Player_obj, GameSounds::SECONDARY_CYCLE)), 0.0f );
			}
			ship_secondary_changed(shipp);

			// Clear missile locks when banks are switched
			for (auto& missile_lock : shipp->missile_locks) {
				ship_clear_lock(&missile_lock);
			}

			shipp->missile_locks_firing.clear();

			objp = &Objects[shipp->objnum];
			object* target;
			if (Ai_info[shipp->ai_index].target_objnum != -1)
				target = &Objects[Ai_info[shipp->ai_index].target_objnum];
			else
				target = NULL;
			if (objp == Player_obj && Player_ai->target_objnum != -1)
				target = &Objects[Player_ai->target_objnum];

			if (scripting::hooks::OnWeaponSelected->isActive() || scripting::hooks::OnWeaponDeselected->isActive()) {
				auto param_list = scripting::hook_param_list(
					scripting::hook_param("User", 'o', objp),
					scripting::hook_param("Target", 'o', target)
				);
				scripting::hooks::OnWeaponSelected->run(scripting::hooks::WeaponSelectedConditions{ shipp, swp->current_secondary_bank, original_bank, false }, param_list);
				scripting::hooks::OnWeaponDeselected->run(scripting::hooks::WeaponDeselectedConditions{ shipp, swp->current_secondary_bank, original_bank, false }, std::move(param_list));
			}

			return 1;
		}
	} // end if

	// If we've reached this point, must have failed
	if ( objp == Player_obj )
	{
		gamesnd_play_error_beep();
	}
	return 0;
}

// Goober5000 - copied from secondary routine
//	Stuff list of weapon indices for object *objp in list *outlist.
//	Return number of weapons in list.
int get_available_primary_weapons(object *objp, int *outlist, int *outbanklist)
{
	int	count = 0;
	int	i;
	ship	*shipp;

	Assert(objp->type == OBJ_SHIP);
	Assert((objp->instance >= 0) && (objp->instance < MAX_SHIPS));
	shipp = &Ships[objp->instance];

	for (i=0; i<shipp->weapons.num_primary_banks; i++)
	{
		if (!primary_out_of_ammo(&(shipp->weapons), i))
		{
			outbanklist[count] = i;
			outlist[count++] = shipp->weapons.primary_bank_weapons[i];
		}
	}

	return count;
}

/**
 * Stuff list of weapon indices for object *objp in list *outlist.
 * @return number of weapons in list.
 */
int get_available_secondary_weapons(object *objp, int *outlist, int *outbanklist)
{
	int	count = 0;
	int	i;
	ship	*shipp;
	weapon_info *wepp;
	Assert(objp->type == OBJ_SHIP);
	Assert((objp->instance >= 0) && (objp->instance < MAX_SHIPS));
	shipp = &Ships[objp->instance];
	Assert(shipp->ai_index >= 0 && shipp->ai_index < MAX_AI_INFO);
	ai_info* aip = &Ai_info[shipp->ai_index];
	
	float target_range, weapon_range_max, weapon_range_min;
	target_range = 0.0f;

	if (The_mission.ai_profile->ai_range_aware_secondary_select_mode!= AI_RANGE_AWARE_SEC_SEL_MODE_RETAIL) {
		vec3d our_position = objp->pos;
		vec3d target_position;
		object *target = &Objects[Ai_info[shipp->ai_index].target_objnum];
		if (target->type == OBJ_SHIP) {
			if (aip->targeted_subsys != nullptr) {
				get_subsystem_pos(&target_position, target, aip->targeted_subsys);
			}
			else if (Ship_info[shipp->ship_info_index].is_big_or_huge() ){
				ai_big_pick_attack_point(target, objp, &target_position, 0.8f);
			}
			else {
				target_position = target->pos;
			}
		}
		else {
			target_position = target->pos;
		}
		target_range = vm_vec_dist_quick(&our_position, &target_position);
	}
	for (i=0; i<shipp->weapons.num_secondary_banks; i++)
		if (ship_secondary_has_ammo(&shipp->weapons, i)) {
			if (The_mission.ai_profile->ai_range_aware_secondary_select_mode != AI_RANGE_AWARE_SEC_SEL_MODE_RETAIL) {
				wepp = &Weapon_info[shipp->weapons.secondary_bank_weapons[i]];
				weapon_range_min = wepp->weapon_min_range;
				weapon_range_max = wepp->weapon_range;
				//If weapon range is not set in the weapon info, derive it
				if (weapon_range_max >= WEAPON_DEFAULT_TABLED_MAX_RANGE) {
					if (wepp->is_beam()) {
						UNREACHABLE("Since when do we have a beam that is a secondary weapon?");
						weapon_range_max = wepp->b_info.range;
					}
					else {
						weapon_range_max = wepp->lifetime * wepp->max_speed;
					}
				}
				if (target_range <= weapon_range_max && target_range >= weapon_range_min) {
					outbanklist[count] = i;
					outlist[count++] = shipp->weapons.secondary_bank_weapons[i];
				}
			}
			else {
				outbanklist[count] = i;
				outlist[count++] = shipp->weapons.secondary_bank_weapons[i];
			}
		}

	return count;
}

void wing_bash_ship_name(char *ship_name, const char *wing_name, int index, bool *needs_display_name)
{
	if (needs_display_name)
		*needs_display_name = false;

	// if wing name has a hash symbol, create the ship name a particular way
	// (but don't do this for names that have the hash as the first or last character)
	const char *p = get_pointer_to_first_hash_symbol(wing_name);
	if ((p != NULL) && (p != wing_name) && (*(p+1) != '\0'))
	{
		size_t len = (p - wing_name);
		strncpy(ship_name, wing_name, len);
		sprintf(ship_name + len, NOX(" %d"), index);
		strcat(ship_name, p);

		if (needs_display_name)
			*needs_display_name = true;
	}
	// most of the time we should create the name the standard retail way
	else
		sprintf(ship_name, NOX("%s %d"), wing_name, index);
}

/**
 * Return the object index of the ship with name *name.
 */
int wing_name_lookup(const char *name, int ignore_count)
{
	int i, wing_limit;

	Assertion(name != nullptr, "NULL name passed to wing_name_lookup");

	if ( Fred_running )
		wing_limit = MAX_WINGS;
	else
		wing_limit = Num_wings;

	if (Fred_running || ignore_count ) {  // current_count not used for Fred..
		for (i=0; i<wing_limit; i++)
			if (Wings[i].wave_count && !stricmp(Wings[i].name, name))
				return i;

	} else {
		for (i=0; i<wing_limit; i++)
			if (Wings[i].current_count && !stricmp(Wings[i].name, name))
				return i;
	}

	return -1;
}

bool wing_has_yet_to_arrive(const wing *wingp)
{
	return (wingp != nullptr) && (wingp->num_waves >= 0) && (wingp->total_arrived_count == 0);
}

/**
 * Needed in addition to wing_name_lookup because it does a straight lookup without
 * caring about how many ships are in the wing, etc.
 */
int wing_lookup(const char *name)
{
	Assertion(name != nullptr, "NULL name passed to wing_lookup");

	for(int idx=0;idx<Num_wings;idx++)
		if(stricmp(Wings[idx].name,name)==0)
		   return idx;

	return -1;
}

int wing_formation_lookup(const char *formation_name)
{
	Assertion(formation_name != nullptr, "NULL formation name passed to wing_formation_lookup");

	for (int idx = 0; idx < (int)Wing_formations.size(); ++idx)
		if (stricmp(Wing_formations[idx].name, formation_name) == 0)
			return idx;

	return -1;
}

/**
 * Return the index of Ship_info[].name that is *token.
 */
static int ship_info_lookup_sub(const char *token)
{
	Assertion(token != nullptr, "NULL token passed to ship_info_lookup_sub");

	for (auto it = Ship_info.cbegin(); it != Ship_info.cend(); ++it)
		if (!stricmp(token, it->name))
			return (int)std::distance(Ship_info.cbegin(), it);

	return -1;
}

/**
 * Return the index of Ship_templates[].name that is *token.
 */
static int ship_template_lookup(const char *token)
{
	Assertion(token != nullptr, "NULL token passed to ship_template_lookup");

	for ( auto it = Ship_templates.cbegin(); it != Ship_templates.cend(); ++it ) {
		if ( !stricmp(token, it->name) ) {
			return (int)std::distance(Ship_templates.cbegin(), it);
		}
	}
	return -1;
}

// Goober5000
int ship_info_lookup(const char *token)
{
	int idx;
	const char *p;
	char name[NAME_LENGTH], temp1[NAME_LENGTH], temp2[NAME_LENGTH];

	Assertion(token != nullptr, "NULL token passed to ship_info_lookup");

	// first try a straightforward lookup
	idx = ship_info_lookup_sub(token);
	if (idx >= 0)
		return idx;

	// we only need to do the rest if we're importing
	if (!Fred_running)
		return -1;

	// ship copy types might be mismatched
	p = get_pointer_to_first_hash_symbol(token);
	if (p == NULL)
		return -1;

	// conversion from FS1 missions
	if (!stricmp(token, "GTD Orion#1 (Galatea)"))
	{
		idx = ship_info_lookup_sub("GTD Orion#Galatea");
		if (idx >= 0)
			return idx;

		idx = ship_info_lookup_sub("GTD Orion (Galatea)");
		if (idx >= 0)
			return idx;

		return -1;
	}
	else if (!stricmp(token, "GTD Orion#2 (Bastion)"))
	{
		idx = ship_info_lookup_sub("GTD Orion#Bastion");
		if (idx >= 0)
			return idx;

		idx = ship_info_lookup_sub("GTD Orion (Bastion)");
		if (idx >= 0)
			return idx;

		return -1;
	}
	else if (!stricmp(token, "SF Dragon#2 (weakened)"))
	{
		idx = ship_info_lookup_sub("SF Dragon#weakened");
		if (idx >= 0)
			return idx;

		idx = ship_info_lookup_sub("SF Dragon (weakened)");
		if (idx >= 0)
			return idx;

		return -1;
	}
	else if (!stricmp(token, "SF Dragon#3 (Player)"))
	{
		idx = ship_info_lookup_sub("SF Dragon#Terrans");
		if (idx >= 0)
			return idx;

		idx = ship_info_lookup_sub("SF Dragon (Terrans)");
		if (idx >= 0)
			return idx;

		return -1;
	}
	else if (!stricmp(token, "GTSC Faustus#2 (big blast)"))
	{
		idx = ship_info_lookup_sub("GTSC Faustus#bigblast");
		if (idx >= 0)
			return idx;

		return -1;
	}
	else if (!stricmp(token, "GTF Loki (stealth)"))
	{
		idx = ship_info_lookup_sub("GTF Loki#stealth");
		if (idx >= 0)
			return idx;

		return -1;
	}

	// sanity check token lengths
	if (strlen(token) > NAME_LENGTH - 1)
		return -1;
	if (strlen(p + 1) > NAME_LENGTH - 1)
		return -1;

	// get first part of new string
	strcpy_s(temp1, token);
	end_string_at_first_hash_symbol(temp1);

	// get second part
	strcpy_s(temp2, p + 1);

	// found a hash
	if (*p == '#')
	{
		// If the below sprintf would exceed NAME_LENGTH (taking \0 terminator into account), return.
		if (strlen(token) > NAME_LENGTH-3)
			return -1;

		// assemble using parentheses
		sprintf_safe(name, "%s (%s)", temp1, temp2);
	}
	// found a parenthesis
	else if (*p == '(')
	{
		// chop off right parenthesis (it exists because otherwise the left wouldn't have been flagged)
		char *p2 = strchr(temp2, ')');
		if (!p2)
			return -1;
		*p2 = '\0';

		// assemble using hash
		sprintf_safe(name, "%s#%s", temp1, temp2);
	}
	// oops
	else
	{
		Warning(LOCATION, "Unrecognized hash symbol.  Contact a programmer!");
		return -1;
	}

	// finally check the new name
	return ship_info_lookup_sub(name);
}

/**
 * Return the ship index of the ship with name *name.
 */
int ship_name_lookup(const char *name, int inc_players)
{
	Assertion(name != nullptr, "NULL name passed to ship_name_lookup");

	for (int i=0; i<MAX_SHIPS; i++){
		if (Ships[i].objnum >= 0){
			if (Objects[Ships[i].objnum].type == OBJ_SHIP || (Objects[Ships[i].objnum].type == OBJ_START && inc_players)){
				if (!stricmp(name, Ships[i].ship_name)){
					return i;
				}
			}
		}
	}
	
	// couldn't find it
	return -1;
}

int ship_type_name_lookup_sub(const char *name)
{
	Assertion(name != nullptr, "NULL name passed to ship_type_name_lookup");

	//Look through Ship_types array
	for (size_t idx = 0; idx < Ship_types.size(); ++idx)
		if (!stricmp(name, Ship_types[idx].name))
			return (int)idx;

	// couldn't find it
	return -1;
}

int ship_type_name_lookup(const char *name)
{
	// try the normal lookup
	auto idx = ship_type_name_lookup_sub(name);
	if (idx >= 0)
		return idx;

	// Goober5000 - in retail FreeSpace, some ship classes were specified differently
	// in ships.tbl and the ship type array; this patches those differences so that
	// the ship type lookup will work properly
	if (!stricmp(name, "sentrygun"))
		name = "sentry gun";
	else if (!stricmp(name, "escapepod"))
		name = "escape pod";
	else if (!stricmp(name, "repair_rearm"))
		name = "support";
	else if (!stricmp(name, "supercap"))
		name = "super cap";
	else if (!stricmp(name, "knossos"))
		name = "knossos device";

	// try it again
	return ship_type_name_lookup_sub(name);
}

// Finds the world position of a subsystem.
// Return true/false for subsystem found/not found.
// Stuff vector *pos with absolute position.
// subsysp is a pointer to the subsystem.
int get_subsystem_pos(vec3d* pos, const object* objp, const ship_subsys* subsysp)
{
	if (subsysp == NULL) {
		*pos = objp->pos;
		return 0;
	}
	Assertion(objp->type == OBJ_SHIP, "Only ships can have subsystems!");

	model_subsystem* mss = subsysp->system_info;

	if (mss->subobj_num == -1) {
		// If it's a special point subsys, we can use its offset directly

		vm_vec_unrotate(pos, &subsysp->system_info->pnt, &objp->orient);
		vm_vec_add2(pos, &objp->pos);
	} else {
		// Submodel subsystems may require a more complicated calculation

		auto pmi = model_get_instance(Ships[objp->instance].model_instance_num);
		auto pm = model_get(pmi->model_num);
		model_instance_local_to_global_point(pos, &vmd_zero_vector, pm, pmi, mss->subobj_num, &objp->orient, &objp->pos);
	}

	return 1;
}

/**
 * Makes sure all submodel instances for this ship are kept in sync
 */
void ship_model_replicate_submodels(object *objp)
{
	model_subsystem	*psub;
	ship		*shipp;
	ship_subsys	*pss;

	flagset<Ship::Subsystem_Flags> empty;

	Assert(objp != NULL);
	Assert(objp->instance >= 0);
	Assert(objp->type == OBJ_SHIP);

	shipp = &Ships[objp->instance];

	polymodel_instance *pmi = model_get_instance(shipp->model_instance_num);
	polymodel *pm = model_get(pmi->model_num);

	// Keep submodels belonging to subsystems in sync
	// (This needs to be done from the subsystem perspective because subsystem flags may affect things)
	for ( pss = GET_FIRST(&shipp->subsys_list); pss != END_OF_LIST(&shipp->subsys_list); pss = GET_NEXT(pss) ) {
		psub = pss->system_info;

		if ( psub->subobj_num >= 0 )	{
			model_replicate_submodel_instance(pm, pmi, psub->subobj_num, pss->flags );
		}

		if ( (psub->subobj_num != psub->turret_gun_sobj) && (psub->turret_gun_sobj >= 0) )		{
			model_replicate_submodel_instance(pm, pmi, psub->turret_gun_sobj, pss->flags );
		}
	}

	// Keep other movable submodels in sync
	model_iterate_submodel_tree(pm, pm->detail[0], [&](int submodel, int /*level*/, bool /*isLeaf*/)
		{
			auto sm = &pm->submodel[submodel];

			// skip submodels that belong to subsystems since we already updated them above
			// (Turrets are tricky because they have two submodels per subsystem, but because the subsystem index is copied
			// when the model is read, both submodels are correctly skipped here.)
			if (sm->subsys_num >= 0)
				return;

			// the only non-subsystem submodels that need to be updated are the ones that can move
			if (!sm->flags[Model::Submodel_flags::Can_move])
				return;

			model_replicate_submodel_instance(pm, pmi, submodel, empty);
		});
}

/**
 * Finds the number of crew points in a ship
 */
int ship_find_num_crewpoints(object *objp)
{
	int n = 0;
	model_subsystem	*psub;
	ship		*shipp;
	ship_subsys	*pss;

	shipp = &Ships[objp->instance];

	// Go through all subsystems and record the model angles for all 
	// the subsystems that need it.
	for ( pss = GET_FIRST(&shipp->subsys_list); pss != END_OF_LIST(&shipp->subsys_list); pss = GET_NEXT(pss) ) {
		psub = pss->system_info;
		switch (psub->type) {
		case SUBSYSTEM_TURRET:
			if ( psub->flags[Model::Subsystem_Flags::Crewpoint] )
				n++; // fall through

		case SUBSYSTEM_RADAR:
		case SUBSYSTEM_NAVIGATION:
		case SUBSYSTEM_COMMUNICATION:
		case SUBSYSTEM_UNKNOWN:
		case SUBSYSTEM_ENGINE:
		case SUBSYSTEM_GAS_COLLECT:
		case SUBSYSTEM_ACTIVATION:
			break;
		default:
			Error(LOCATION, "Illegal subsystem type.\n");
		}
	}
	return n;
}

/**
 * Finds the number of turrets in a ship
 */
int ship_find_num_turrets(object *objp)
{
	int n = 0;
	model_subsystem	*psub;
	ship		*shipp;
	ship_subsys	*pss;

	shipp = &Ships[objp->instance];

	// Go through all subsystems and record the model angles for all 
	// the subsystems that need it.
	for ( pss = GET_FIRST(&shipp->subsys_list); pss != END_OF_LIST(&shipp->subsys_list); pss = GET_NEXT(pss) ) {
		psub = pss->system_info;
		switch (psub->type) {
		case SUBSYSTEM_TURRET:
			n++; // drop through

		case SUBSYSTEM_RADAR:
		case SUBSYSTEM_NAVIGATION:
		case SUBSYSTEM_COMMUNICATION:
		case SUBSYSTEM_UNKNOWN:
		case SUBSYSTEM_ENGINE:
		case SUBSYSTEM_GAS_COLLECT:
		case SUBSYSTEM_ACTIVATION:
			break;
		default:
			Error(LOCATION, "Illegal subsystem type.\n");
		}
	}
	return n;
}

//WMC
static void ship_set_eye( object *obj, int eye_index)
{
	if(obj->type != OBJ_SHIP)
		return;

	ship *shipp = &Ships[obj->instance];

	if(eye_index < 0)
	{
		shipp->current_viewpoint = -1;
		return;
	}

	ship_info *sip = &Ship_info[shipp->ship_info_index];
	if(sip->model_num < 0)
		return;

	polymodel *pm = model_get(sip->model_num);

	if(pm == NULL || eye_index > pm->n_view_positions)
		return;

	shipp->current_viewpoint = eye_index;
}

// Calculates the eye position for this object (which is usually a ship, but doesn't have to be)
// in the global (by default), partial local, or full local reference frame.  Uses the
// view_positions array in the model.  The 0th element is the normal viewing position.
// The position of the eye is returned in the parameter 'eye_pos'.  The orientation of the
// eye is returned in 'eye_orient'.  (NOTE: this is kind of bogus for now since non 0th element
// eyes have no defined up vector)
void object_get_eye(vec3d *eye_pos, matrix *eye_orient, const object *obj, bool do_slew, bool local_pos, bool local_orient)
{
	auto pmi = object_get_model_instance(obj);
	auto pm = object_get_model(obj);

	int current_viewpoint = (obj->type == OBJ_SHIP) ? Ships[obj->instance].current_viewpoint : 0;

	// if no viewpoints, or invalid viewpoint, return the origin
	if (!pm || (pm->n_view_positions <= 0) || (current_viewpoint < 0) || (current_viewpoint >= pm->n_view_positions)) {
		*eye_pos = local_pos ? vmd_zero_vector : obj->pos;
		*eye_orient = local_orient ? vmd_identity_matrix : obj->orient;
		return;
	}

	// eye points are stored in an array -- the normal viewing position for a ship is the current_eye_index (now current_viewpoint) element.
	auto &ep = pm->view_positions[current_viewpoint];

	matrix eye_local_orient_buf;
	const matrix *eye_local_orient;
	if (Use_model_eyepoint_normals) {
		vm_vector_2_matrix_norm(&eye_local_orient_buf, &ep.norm);
		eye_local_orient = &eye_local_orient_buf;
	} else {
		eye_local_orient = &vmd_identity_matrix;
	}
	model_instance_local_to_global_point_orient(eye_pos, eye_orient, &ep.pnt, eye_local_orient, pm, pmi, ep.parent, local_orient ? &vmd_identity_matrix : &obj->orient, local_pos ? &vmd_zero_vector : &obj->pos);

	//	Modify the orientation based on head orientation.
	if (Viewer_obj == obj && do_slew) {
		// Add the cockpit leaning translation offset
		vm_vec_add2(eye_pos, &leaning_position);
		compute_slew_matrix(eye_orient, &Viewer_slew_angles);
	}
}

// of attackers to make this decision.
//
// NOTE: This function takes into account how many ships are attacking a subsystem, and will 
//			prefer an ignored subsystem over a subsystem that is in line of sight, if the in-sight
//			subsystem is attacked by more than MAX_SUBSYS_ATTACKERS
// input:
//				sp					=>		ship pointer to parent of subsystem
//				subsys_type		=>		what kind of subsystem this is
//				attacker_pos	=>		the world coords of the attacker of this subsystem
//
// returns: pointer to subsystem if one found, NULL otherwise
#define MAX_SUBSYS_ATTACKERS 3
ship_subsys *ship_get_best_subsys_to_attack(ship *sp, int subsys_type, const vec3d *attacker_pos)
{
	ship_subsys	*ss;
	ship_subsys *best_in_sight_subsys, *lowest_attacker_subsys, *ss_return;
	int			lowest_num_attackers, lowest_in_sight_attackers, num_attackers;
	vec3d		gsubpos;

	lowest_in_sight_attackers = lowest_num_attackers = 1000;
	ss_return = best_in_sight_subsys = lowest_attacker_subsys = NULL;

	for (ss = GET_FIRST(&sp->subsys_list); ss != END_OF_LIST(&sp->subsys_list); ss = GET_NEXT(ss) ) {
		if ( (ss->system_info->type == subsys_type) && (ss->current_hits > 0) ) {

			// get world pos of subsystem
			vm_vec_unrotate(&gsubpos, &ss->system_info->pnt, &Objects[sp->objnum].orient);
			vm_vec_add2(&gsubpos, &Objects[sp->objnum].pos);
			
			// now find the number of ships attacking this subsystem by iterating through the ships list,
			// and checking if aip->targeted_subsys matches the subsystem we're checking
			num_attackers = 0;
			for (auto sop: list_range(&Ship_obj_list)){
				if (Objects[sop->objnum].flags[Object::Object_Flags::Should_be_dead])
					continue;
				if ( Ai_info[Ships[Objects[sop->objnum].instance].ai_index].targeted_subsys == ss ) {
					num_attackers++;
				}
			}

			if ( num_attackers < lowest_num_attackers ) {
				lowest_num_attackers = num_attackers;
				lowest_attacker_subsys = ss;
			}

			if ( ship_subsystem_in_sight(&Objects[sp->objnum], ss, attacker_pos, &gsubpos) ) {
				if ( num_attackers < lowest_in_sight_attackers ) {
					lowest_in_sight_attackers = num_attackers;
					best_in_sight_subsys = ss;
				}
			}
		}
	}

	if ( best_in_sight_subsys == NULL ) {
		// no subsystems are in sight, so return the subsystem with the lowest # of attackers
		ss_return =  lowest_attacker_subsys;
	} else {
		if ( lowest_in_sight_attackers > MAX_SUBSYS_ATTACKERS ) {
			ss_return = lowest_attacker_subsys;
		} else {
			ss_return =  best_in_sight_subsys;
		}
	}

	return ss_return;
}

/**
 * Returns the first subsystem in the ship's subsystem list with the specified subsystem type.
 * Originally part of ship_get_indexed_subsys.
 * attacker_pos	=> world pos of attacker (default value NULL).  If value is non-NULL, try
 *                 to select the best subsystem to attack of that type (using line-of-sight)
 *                 and based on the number of ships already attacking the subsystem
 */
ship_subsys *ship_find_first_subsys(ship *sp, int subsys_type, const vec3d *attacker_pos)
{
	Assertion(subsys_type > SUBSYSTEM_NONE && subsys_type < SUBSYSTEM_MAX, "Subsys_type %d must refer to a valid subsystem type!", subsys_type);

	// We are looking for one instance of a certain subsystem type, e.g. one of several possible
	// engines or one of several possible turrets.
	if ( sp->subsys_info[subsys_type].aggregate_current_hits <= 0.0f )		// if there are no hits, no subsystem to attack.
		return nullptr;

	if ( attacker_pos != nullptr ) {
		return ship_get_best_subsys_to_attack(sp, subsys_type, attacker_pos);
	} else {
		// next, scan the list of subsystems and search for the first subsystem of the particular
		// type which has > 0 hits remaining.
		for (auto ss : list_range(&sp->subsys_list)) {
			if ( (ss->system_info->type == subsys_type) && (ss->current_hits > 0) )
				return ss;
		}
	}
		
	// maybe we shouldn't get here, but with possible floating point rounding, I suppose we could
	Warning(LOCATION, "Unable to get a nonspecific subsystem of type %d (%s) on ship %s!", subsys_type, Subsystem_types[subsys_type], sp->ship_name);
	return nullptr;
}

/**
 * Create or recreate the subsystem index cache.
 */
void ship_index_subsystems(ship *shipp)
{
	auto sinfo = &Ship_info[shipp->ship_info_index];

	// if the indexer already exists, its size won't change, so don't reallocate it
	if (shipp->subsys_list_indexer.get() == nullptr)
		shipp->subsys_list_indexer.reset(new ship_subsys* [sinfo->n_subsystems]);

	auto ss = GET_FIRST(&shipp->subsys_list);
	for (int index = 0; index < sinfo->n_subsystems; ++index)
	{
		// normal indexing to valid subsystems
		if (ss != END_OF_LIST(&shipp->subsys_list))
		{
			shipp->subsys_list_indexer[index] = ss;
			ss->parent_subsys_index = index;
			ss = GET_NEXT(ss);
		}
		// in the event we run out of subsystems (i.e. if not all subsystems were linked)
		else
		{
			shipp->subsys_list_indexer[index] = nullptr;
		}
	}

	shipp->flags.set(Ship::Ship_Flags::Subsystem_cache_valid);
}

/**
 * Returns the 'nth' ship_subsys structure in a ship's linked list of subsystems.
 */
ship_subsys *ship_get_indexed_subsys(ship *sp, int index)
{
	Assertion(index >= 0, "Index must be positive!  The functionality for negative indexes has been moved to ship_get_first_subsys.");
	Assertion(index < Ship_info[sp->ship_info_index].n_subsystems, "Subsystem index out of range!");

	// might need to refresh the cache
	if (!sp->flags[Ship::Ship_Flags::Subsystem_cache_valid])
		ship_index_subsystems(sp);

	return sp->subsys_list_indexer[index];
}

/**
* Returns the index number of the ship_subsys parameter within its ship's subsytem list
*/
int ship_get_subsys_index(const ship_subsys *subsys)
{
	Assertion(subsys != nullptr, "ship_get_subsys_index was called with a null ship_subsys parameter!");
	if (subsys == nullptr)
		return -1;

	// might need to refresh the cache
	auto sp = &Ships[Objects[subsys->parent_objnum].instance];
	if (!sp->flags[Ship::Ship_Flags::Subsystem_cache_valid])
		ship_index_subsystems(sp);

	Assertion(subsys->parent_subsys_index >= 0, "Somehow a subsystem could not be found in its parent ship %s's subsystem list!", sp->ship_name);
	return subsys->parent_subsys_index;
}

/**
 * Searches for the subsystem with the given name in the ship's linked list of subsystems, and returns its index or -1 if not found.
 */
int ship_find_subsys(const ship *sp, const char *ss_name)
{
	int count;
	ship_subsys *ss;

	count = 0;
	ss = GET_FIRST(&sp->subsys_list);
	while ( ss != END_OF_LIST( &sp->subsys_list ) ) {
		if ( !subsystem_stricmp(ss->system_info->subobj_name, ss_name) )
			return count;
		count++;
		ss = GET_NEXT( ss );
	}

	return -1;
}

// an optimization of the below function that skips the subsystem iteration if we don't care about the exact number
bool ship_subsystems_blown(const ship* shipp, int type, bool skip_dying_check)
{
	Assertion( (type >= 0) && (type < SUBSYSTEM_MAX), "ship_subsystems_blown() subsystem type %d is out of range!", type );

	//	For a dying ship, all subsystem strengths are zero.
	if (shipp->flags[Ship::Ship_Flags::Dying] && !skip_dying_check)
		return true;

	// short circuit 1
	if (shipp->subsys_info[type].aggregate_max_hits <= 0.0f)
		return false;

	// short circuit 0
	if (shipp->subsys_info[type].aggregate_current_hits <= 0.0f)
		return true;

	return false;
}

// routine to return the strength of a subsystem.  We keep a total hit tally for all subsystems
// which are similar (i.e. a total for all engines).  These routines will return a number between
// 0.0 and 1.0 which is the relative combined strength of the given subsystem type.  The number
// calculated for the engines is slightly different.  Once an engine reaches < 15% of its hits, its
// output drops to that %.  A dead engine has no output.
float ship_get_subsystem_strength(const ship *shipp, int type, bool skip_dying_check, bool no_minimum_engine_str)
{
	float strength;
	ship_subsys *ssp;

	Assertion( (type >= 0) && (type < SUBSYSTEM_MAX), "ship_get_subsystem_strength() subsystem type %d is out of range!", type );

	//	For a dying ship, all subsystem strengths are zero.
	if (shipp->flags[Ship::Ship_Flags::Dying] && !skip_dying_check)
		return 0.0f;

	// short circuit 1
	if (shipp->subsys_info[type].aggregate_max_hits <= 0.0f)
		return 1.0f;

	// short circuit 0
	if (shipp->subsys_info[type].aggregate_current_hits <= 0.0f)
		return 0.0f;

	strength = shipp->subsys_info[type].aggregate_current_hits / shipp->subsys_info[type].aggregate_max_hits;
	Assert( strength != 0.0f );

	// if we don't need to enforce a minimum engine contribution ratio, we can just use the regular strength calculation
	if ( (type == SUBSYSTEM_ENGINE) && !no_minimum_engine_str && (strength < 1.0f) ) {
		float percent;

		percent = 0.0f;
		ssp = GET_FIRST(&shipp->subsys_list);
		while ( ssp != END_OF_LIST( &shipp->subsys_list ) ) {

			if ( ssp->system_info->type == SUBSYSTEM_ENGINE ) {
				float ratio;

				ratio = ssp->current_hits / ssp->max_hits;
				if ( ratio < ENGINE_MIN_STR )
					ratio = ENGINE_MIN_STR;

				percent += ratio;
			}
			ssp = GET_NEXT( ssp );
		}
		strength = percent / (float)shipp->subsys_info[type].type_count;
	}

	return strength;
}

/**
 * Set the strength of a subsystem on a given ship.
 *
 * The strength passed as a parameter is between 0.0 and 1.0
 *
 * NOTE: this function was made to be called by the debug function dcf_set_subsys().  If
 * you want to use this, be sure that you test it for all cases.
 */
void ship_set_subsystem_strength( ship *shipp, int type, float strength )
{
	ship_subsys *ssp;

	Assert ( (type >= 0) && (type < SUBSYSTEM_MAX) );
	CLAMP(strength, 0.0f, 1.0f);

	ssp = GET_FIRST(&shipp->subsys_list);
	while ( ssp != END_OF_LIST( &shipp->subsys_list ) ) {

		if ( (ssp->system_info->type == type) && !(ssp->flags[Ship::Subsystem_Flags::No_aggregate]) ) {
			ssp->current_hits = strength * ssp->max_hits;

			// maybe blow up subsys
			if (ssp->current_hits <= 0) {
				do_subobj_destroyed_stuff(shipp, ssp, nullptr);
			}
		}
		ssp = GET_NEXT( ssp );
	}

	// fix up the overall ship subsys status
	ship_recalc_subsys_strength(shipp);
}




/**
 * Calculates approximate time in seconds it would take to rearm and repair object.
 */
float ship_calculate_rearm_duration( object *objp )
{
	ship* sp;
	ship_info* sip;
	ship_subsys* ssp;
	ship_weapon* swp;
	weapon_info* wip;

	float shield_rep_time = 0;
	float subsys_rep_time = 0;
	float hull_rep_time = 0;
	float prim_rearm_time = 0;
	float sec_rearm_time = 0;

	float max_hull_repair;
	float max_subsys_repair;

	int i;
	int num_reloads;

	bool found_first_empty;
	
	Assert(objp->type == OBJ_SHIP);

	sp = &Ships[objp->instance];
	swp = &sp->weapons;
	sip = &Ship_info[sp->ship_info_index];

	//find out time to repair shields
	float max_shields = shield_get_max_strength(sp);
	if(sip->sup_shield_repair_rate > 0.0f && max_shields > 0.0f)
		shield_rep_time = (max_shields - shield_get_strength(objp)) / (max_shields * sip->sup_shield_repair_rate);
	
	max_hull_repair = sp->ship_max_hull_strength * (The_mission.support_ships.max_hull_repair_val * 0.01f);
	if ((The_mission.flags[Mission::Mission_Flags::Support_repairs_hull]) && (max_hull_repair > objp->hull_strength) && (sip->sup_hull_repair_rate > 0.0f))
	{
		hull_rep_time = (max_hull_repair - objp->hull_strength) / (sp->ship_max_hull_strength * sip->sup_hull_repair_rate);
	}

	//calculate subsystem repair time
	ssp = GET_FIRST(&sp->subsys_list);
	while (ssp != END_OF_LIST(&sp->subsys_list))
	{
		max_subsys_repair = ssp->max_hits * (The_mission.support_ships.max_subsys_repair_val * 0.01f);
		if ((max_subsys_repair > ssp->current_hits) && (sip->sup_subsys_repair_rate > 0.0f))
		{
			subsys_rep_time += (max_subsys_repair - ssp->current_hits) / (ssp->max_hits * sip->sup_subsys_repair_rate);
		}

		ssp = GET_NEXT( ssp );
	}

	//now do the primary rearm time
	found_first_empty = false;
	for (i = 0; i < swp->num_primary_banks; i++)
	{
		wip = &Weapon_info[swp->primary_bank_weapons[i]];
		if (wip->wi_flags[Weapon::Info_Flags::Ballistic])
		{
			//check how many full reloads we need
			num_reloads = (swp->primary_bank_start_ammo[i] - swp->primary_bank_ammo[i]) / wip->reloaded_per_batch;

			//take into account a fractional reload
			if ((swp->primary_bank_start_ammo[i] - swp->primary_bank_ammo[i]) % wip->reloaded_per_batch != 0)
			{
				num_reloads++;
			}

			//don't factor in the time it takes for the first reload, since that is loaded instantly
			num_reloads--;

			if (num_reloads < 0) continue;

			if (!found_first_empty && (swp->primary_bank_start_ammo[i] - swp->primary_bank_ammo[i]))
			{
				found_first_empty = true;
				prim_rearm_time += gamesnd_get_max_duration(gamesnd_get_game_sound(GameSounds::MISSILE_START_LOAD)) / 1000.0f;
			}

			prim_rearm_time += num_reloads * wip->rearm_rate;
		}
	}

	//and on to secondary rearm time
	found_first_empty = false;
	for (i = 0; i < swp->num_secondary_banks; i++)
	{
			wip = &Weapon_info[swp->secondary_bank_weapons[i]];
	
			if (wip->wi_flags[Weapon::Info_Flags::SecondaryNoAmmo])
				continue;

			//check how many full reloads we need
		    num_reloads = (swp->secondary_bank_start_ammo[i] - swp->secondary_bank_ammo[i]) / wip->reloaded_per_batch;

			//take into account a fractional reload
		    if ((swp->secondary_bank_start_ammo[i] - swp->secondary_bank_ammo[i]) % wip->reloaded_per_batch != 0)
			{
				num_reloads++;
			}

			//don't factor in the time it takes for the first reload, since that is loaded instantly
			num_reloads--;

			if (num_reloads < 0) continue;

			if (!found_first_empty && (swp->secondary_bank_start_ammo[i] - swp->secondary_bank_ammo[i]))
			{
				found_first_empty = true;
				sec_rearm_time += gamesnd_get_max_duration(gamesnd_get_game_sound(GameSounds::MISSILE_START_LOAD)) / 1000.0f;
			}

			sec_rearm_time += num_reloads * wip->rearm_rate;
	}

	//sum them up and you've got an estimated rearm time.
	//add 1.2 to compensate for release delay
	return shield_rep_time + hull_rep_time + subsys_rep_time + prim_rearm_time + sec_rearm_time + 1.2f;
}



// ==================================================================================
// ship_do_rearm_frame()
//
// function to rearm a ship.  This function gets called from the ai code ai_do_rearm_frame (or
// some function of a similar name).  Returns 1 when ship is fully repaired and rearmed, 0 otherwise
//
int ship_do_rearm_frame( object *objp, float frametime )
{
	int			i, banks_full, primary_banks_full, subsys_type, last_ballistic_idx = -1;
	float			shield_str, max_shield_str = 0.0f, repair_delta, repair_allocated, max_hull_repair = 0, max_subsys_repair;
	ship			*shipp;
	ship_weapon	*swp;
	ship_info	*sip;
	ship_subsys	*ssp;
	ai_info		*aip;

	shipp = &Ships[objp->instance];
	swp = &shipp->weapons;
	sip = &Ship_info[shipp->ship_info_index];
	aip = &Ai_info[shipp->ai_index];

	// AL 10-31-97: Add missing primary weapons to the ship.  This is required since designers
	//              want to have ships that start with no primaries, but can get them through
	//					 rearm/repair
	// plieblang - skip this if the ai profile flag is set
	if ( swp->num_primary_banks < sip->num_primary_banks && !aip->ai_profile_flags[AI::Profile_Flags::Support_dont_add_primaries] ) {
		for ( i = swp->num_primary_banks; i < sip->num_primary_banks; i++ ) {
			swp->primary_bank_weapons[i] = sip->primary_bank_weapons[i];
		}
		swp->num_primary_banks = sip->num_primary_banks;
	}

	// AL 12-30-97: Repair broken warp drive
	if ( shipp->flags[Ship_Flags::Warp_broken] ) {
		// TODO: maybe do something here like informing player warp is fixed?
		// like this? -- Goober5000
		HUD_sourced_printf(HUD_SOURCE_HIDDEN, "%s", XSTR( "Subspace drive repaired.", 1635));
		shipp->flags.remove(Ship_Flags::Warp_broken);
	}

	// AL 1-16-98: Replenish countermeasures
	if (Countermeasures_use_capacity) {
		float cm_cargo_size = Weapon_info[sip->cmeasure_type].cargo_size;
		shipp->cmeasure_count = fl2i(sip->cmeasure_max / cm_cargo_size);
	} else {
		shipp->cmeasure_count = sip->cmeasure_max;
	}

	// Do shield repair here
	if ( !(objp->flags[Object::Object_Flags::No_shields]) )
	{
		shield_str = shield_get_strength(objp);
		max_shield_str = shield_get_max_strength(shipp);
		if ( shield_str < (max_shield_str) ) {
			if ( objp == Player_obj ) {
				player_maybe_start_repair_sound();
			}
			shield_str += shipp->ship_max_shield_strength * frametime * sip->sup_shield_repair_rate; // repair rate is unaffected by $Max Shield Recharge
			if ( shield_str > max_shield_str ) {
				 shield_str = max_shield_str;
			}
			shield_set_strength(objp, shield_str);
		}
	}

	// Repair the ship integrity (subsystems + hull).  This works by applying the repair points
	// to the subsystems.  Ships integrity is stored in objp->hull_strength, so that always is 
	// incremented by repair_allocated

	//	AL 11-24-97: remove increase to hull integrity
	//	Comments removed by PhReAk; Note that this is toggled on/off with a mission flag
	if (The_mission.flags[Mission::Mission_Flags::Support_repairs_hull])
	{
		//Figure out how much of the ship's hull we can repair
		//Don't "reverse-repair" the hull if it's already above the max repair threshold
		max_hull_repair = shipp->ship_max_hull_strength * (The_mission.support_ships.max_hull_repair_val * 0.01f);
		if (objp->hull_strength < max_hull_repair)
		{
			objp->hull_strength += shipp->ship_max_hull_strength * frametime * sip->sup_hull_repair_rate;
			if (objp->hull_strength > max_hull_repair)
				objp->hull_strength = max_hull_repair;
		}
	}

	// figure out repairs for subsystems
	repair_allocated = shipp->ship_max_hull_strength * frametime * sip->sup_subsys_repair_rate;

	// check the subsystems of the ship.
	bool subsys_all_ok = true;
	ssp = GET_FIRST(&shipp->subsys_list);
	while ( ssp != END_OF_LIST( &shipp->subsys_list ) ) {
		//Figure out how much we *can* repair the current subsystem -C
		max_subsys_repair = ssp->max_hits * (The_mission.support_ships.max_subsys_repair_val * 0.01f);

		if ( ssp->current_hits < max_subsys_repair && repair_allocated > 0 ) {
			subsys_all_ok = false;
			subsys_type = ssp->system_info->type;

			if ( objp == Player_obj ) {
				player_maybe_start_repair_sound();
			}
			
			repair_delta = max_subsys_repair - ssp->current_hits;
			if ( repair_delta > repair_allocated ) {
				repair_delta = repair_allocated;
			}
			repair_allocated -= repair_delta;
			Assert(repair_allocated >= 0.0f);

			// add repair to current strength of single subsystem
			ssp->current_hits += repair_delta;
			if ( ssp->current_hits > max_subsys_repair ) {
				ssp->current_hits = max_subsys_repair;
			}

			// add repair to aggregate strength of subsystems of that type
			if (!(ssp->flags[Ship::Subsystem_Flags::No_aggregate])) {
				shipp->subsys_info[subsys_type].aggregate_current_hits += repair_delta;
				if ( shipp->subsys_info[subsys_type].aggregate_current_hits > shipp->subsys_info[subsys_type].aggregate_max_hits )
					shipp->subsys_info[subsys_type].aggregate_current_hits = shipp->subsys_info[subsys_type].aggregate_max_hits;
			}

			// check to see if this subsystem was totally non functional before -- if so, then
			// reset the flags
			if ( (ssp->system_info->type == SUBSYSTEM_ENGINE) && (shipp->flags[Ship_Flags::Disabled]) ) {
				shipp->flags.remove(Ship_Flags::Disabled);
				ship_reset_disabled_physics(objp, shipp->ship_info_index);
			}
			break;
		}
		ssp = GET_NEXT( ssp );
	}

	// now deal with rearming the player.  All secondary weapons have a certain rate at which
	// they can be rearmed.  We can rearm multiple banks at once.
	banks_full = 0;
	primary_banks_full = 0;
	if ( subsys_all_ok )
	{
		for (i = 0; i < swp->num_secondary_banks; i++ )
		{
			// Actual loading of missiles is preceded by a sound effect which is the missile
			// loading equipment moving into place
			if ( aip->rearm_first_missile == TRUE )
			{
				swp->secondary_bank_rearm_time[i] = timestamp((int)gamesnd_get_max_duration(gamesnd_get_game_sound(GameSounds::MISSILE_START_LOAD)));
			}
			
			if ( swp->secondary_bank_ammo[i] < swp->secondary_bank_start_ammo[i] )
			{
				float rearm_time;

				if ( objp == Player_obj )
				{
					hud_gauge_popup_start(HUD_WEAPONS_GAUGE);
				}

				if ( timestamp_elapsed(swp->secondary_bank_rearm_time[i]) )
				{
					rearm_time = Weapon_info[swp->secondary_bank_weapons[i]].rearm_rate;
					swp->secondary_bank_rearm_time[i] = timestamp((int)(rearm_time * 1000.0f));
					
					snd_play_3d( gamesnd_get_game_sound(GameSounds::MISSILE_LOAD), &objp->pos, &View_position );
					if (objp == Player_obj)
						joy_ff_play_reload_effect();

					swp->secondary_bank_ammo[i] += Weapon_info[swp->secondary_bank_weapons[i]].reloaded_per_batch;
					if ( swp->secondary_bank_ammo[i] > swp->secondary_bank_start_ammo[i] ) 
					{
						swp->secondary_bank_ammo[i] = swp->secondary_bank_start_ammo[i]; 
					}
				}
				else
				{
				}
			} 
			else
			{
				banks_full++;
			}

			if ((aip->rearm_first_missile == TRUE) && (i == swp->num_secondary_banks - 1))
			{
				if ((banks_full != swp->num_secondary_banks))
					snd_play_3d( gamesnd_get_game_sound(GameSounds::MISSILE_START_LOAD), &objp->pos, &View_position );

				aip->rearm_first_missile = FALSE;
			}
		}	// end for

		// rearm ballistic primaries - Goober5000
		if ( aip->rearm_first_ballistic_primary == TRUE)
		{
			for (i = 0; i < swp->num_primary_banks; i++ )
			{
				if ( Weapon_info[swp->primary_bank_weapons[i]].wi_flags[Weapon::Info_Flags::Ballistic] )
					last_ballistic_idx = i;
			}
		}

		for (i = 0; i < swp->num_primary_banks; i++ )
		{
			if (Weapon_info[swp->primary_bank_weapons[i]].wi_flags[Weapon::Info_Flags::Ballistic])
			{
				// Actual loading of bullets is preceded by a sound effect which is the bullet
				// loading equipment moving into place
				if ( aip->rearm_first_ballistic_primary == TRUE )
				{
					// Goober5000
					gamesnd_id sound_index;
					if (gamesnd_game_sound_try_load(GameSounds::BALLISTIC_START_LOAD))
						sound_index = GameSounds::BALLISTIC_START_LOAD;
					else
						sound_index = GameSounds::MISSILE_START_LOAD;

					if (sound_index.isValid())
						swp->primary_bank_rearm_time[i] = timestamp((int)gamesnd_get_max_duration(gamesnd_get_game_sound(sound_index)));
					else
						swp->primary_bank_rearm_time[i] = timestamp(0);
				}

				if ( swp->primary_bank_ammo[i] < swp->primary_bank_start_ammo[i] )
				{
					float rearm_time;
	
					if ( objp == Player_obj )
					{
						hud_gauge_popup_start(HUD_WEAPONS_GAUGE);
					}

					if ( timestamp_elapsed(swp->primary_bank_rearm_time[i]) )
					{
						rearm_time = Weapon_info[swp->primary_bank_weapons[i]].rearm_rate;
						swp->primary_bank_rearm_time[i] = timestamp( (int)(rearm_time * 1000.f) );
	
						// Goober5000
						gamesnd_id sound_index;
						if (gamesnd_game_sound_try_load(GameSounds::BALLISTIC_LOAD))
							sound_index = GameSounds::BALLISTIC_LOAD;
						else
							sound_index = GameSounds::MISSILE_LOAD;

						if (sound_index.isValid())
							snd_play_3d( gamesnd_get_game_sound(sound_index), &objp->pos, &View_position );
	
						swp->primary_bank_ammo[i] += Weapon_info[swp->primary_bank_weapons[i]].reloaded_per_batch;
						if ( swp->primary_bank_ammo[i] > swp->primary_bank_start_ammo[i] )
						{
							swp->primary_bank_ammo[i] = swp->primary_bank_start_ammo[i]; 
						}
					}
				}
				else
				{
					primary_banks_full++;
				}
			}
			// if the bank is not a ballistic
			else
			{
				primary_banks_full++;
			}

			if ((aip->rearm_first_ballistic_primary == TRUE) && (i == last_ballistic_idx))
			{
				if (primary_banks_full != swp->num_primary_banks)
				{
					// Goober5000
					gamesnd_id sound_index;
					if (gamesnd_game_sound_try_load(GameSounds::BALLISTIC_START_LOAD))
						sound_index = GameSounds::BALLISTIC_START_LOAD;
					else
						sound_index = GameSounds::MISSILE_START_LOAD;

					if (sound_index.isValid())
						snd_play_3d( gamesnd_get_game_sound(sound_index), &objp->pos, &View_position );
				}

				aip->rearm_first_ballistic_primary = FALSE;
			}
		}	// end for
	} // end if (subsys_all_ok)

	if ( banks_full == swp->num_secondary_banks )
	{
		aip->rearm_first_missile = TRUE;
	}

	if ( primary_banks_full == swp->num_primary_banks )
	{
		aip->rearm_first_ballistic_primary = TRUE;
	}

	int shields_full = false;
	if ( (objp->flags[Object::Object_Flags::No_shields]) ) {
		shields_full = true;
	} else {
		if ( shield_get_strength(objp) >= max_shield_str )
			shields_full = true;
		if (sip->sup_shield_repair_rate == 0.0f)
			shields_full = true;
	}

	bool hull_ok = false;
	if (!(The_mission.flags[Mission::Mission_Flags::Support_repairs_hull])) {
		hull_ok = true;
	} else {
		if (objp->hull_strength >= max_hull_repair)
			hull_ok = true;
		if (sip->sup_hull_repair_rate == 0.0f)
			hull_ok = true;
	}

	// return 1 if at end of subsystem list, hull damage at 0, and shields full and all secondary banks full.
	if ( (subsys_all_ok && shields_full && (The_mission.flags[Mission::Mission_Flags::Support_repairs_hull]) && hull_ok ) || (subsys_all_ok && shields_full && !(The_mission.flags[Mission::Mission_Flags::Support_repairs_hull]) ) )
	{
		if ( objp == Player_obj ) {
			player_stop_repair_sound();
		}

		if (!aip->rearm_release_delay)
			aip->rearm_release_delay = timestamp(1200);

		// check both primary and secondary banks are full
		if ( (banks_full == swp->num_secondary_banks) && (primary_banks_full == swp->num_primary_banks) )
		{
			if ( timestamp_elapsed(aip->rearm_release_delay) )
				return 1;
		}
		else
		{
			aip->rearm_release_delay = timestamp(1200);
		}
	}

	if (objp == Player_obj)
		Player_rearm_eta -= frametime;

	return 0;
}

// Goober5000 - modified the logic to clarify the various states
// function which is used to find a repair ship to repair requester_obj.  the way repair ships will work is:
// if no ships in the mission at all, return 0
// if a ship can immediately satisfy a repair request, return 1 and fill in the pointer
// if no ships can satisfy a request, but we haven't reached either the concurrent or cumulative limit, return 2
// if no ships can satisfy a request, and we've reached the limits, but a request can be queued, return 3 and fill in the pointer
// if no ships can satisfy a request, we've reached the limits, and we can't queue anything, we're out of luck -- return 4
int ship_find_repair_ship( object *requester_obj, object **ship_we_found )
{
	int num_support_ships = 0;
	float min_dist = -1.0f;
	object *nearest_support_ship = NULL;
	float min_time_till_available = -1.0f;
	object *soonest_available_support_ship = NULL;

	Assertion(requester_obj->type == OBJ_SHIP, "requester_obj not a ship. Has type of %08x", requester_obj->type);
	Assertion((requester_obj->instance >= 0) && (requester_obj->instance < MAX_SHIPS),
		"requester_obj does not have a valid pointer to a ship. Pointer is %d, which is smaller than 0 or bigger than %d",
		requester_obj->instance, MAX_SHIPS);

	ship *requester_ship = &Ships[requester_obj->instance];
	for (auto so: list_range(&Ship_obj_list))
	{
		auto objp = &Objects[so->objnum];
		if (!(objp->flags[Object::Object_Flags::Should_be_dead]))
		{
			ship *shipp;
			ship_info *sip;
			float dist;

			Assertion((objp->instance >= 0) && (objp->instance < MAX_SHIPS),
				"objp does not have a valid pointer to a ship. Pointer is %d, which is smaller than 0 or bigger than %d",
				objp->instance, MAX_SHIPS);

			shipp = &Ships[objp->instance];

			if ( shipp->team != requester_ship->team ) {
				continue;
			}

			Assertion((shipp->ship_info_index >= 0) && (shipp->ship_info_index < ship_info_size()),
				"Ship '%s' does not have a valid pointer to a ship class. Pointer is %d, which is smaller than 0 or bigger than %d",
				shipp->ship_name, shipp->ship_info_index, ship_info_size());

			sip = &Ship_info[shipp->ship_info_index];

			if ( !(sip->flags[Ship::Info_Flags::Support]) ) {
				continue;
			}

			// tally how many support ships actually exist
			num_support_ships++;

			// don't deal with dying or departing support ships
			if ( shipp->is_dying_or_departing() ) {
				continue;
			}

			// Ship has been ordered to warpout but has not had a chance to process the order.
			Assertion( (shipp->ai_index >= 0) && (shipp->ai_index < MAX_AI_INFO),
				"Ship '%s' doesn't have a valid ai pointer. Pointer is %d, which is smaller than 0 or larger than %d",
				shipp->ship_name, shipp->ai_index, MAX_AI_INFO);
			ai_info* aip = &(Ai_info[shipp->ai_index]);
			if ( ai_find_goal_index( aip->goals, AI_GOAL_WARP ) != -1 ) {
				continue;
			}

			dist = vm_vec_dist_quick(&objp->pos, &requester_obj->pos);

			if (aip->ai_flags[AI::AI_Flags::Repairing, AI::AI_Flags::Awaiting_repair, AI::AI_Flags::Being_repaired])
			{
				// support ship is already busy, track the one that will be
				// done soonest by estimating how many seconds it will take for the support ship
				// to reach the requester.
				// The estimate is calculated by calculating how many seconds it will take the
				// support ship to travel from its current location to the requester at max velocity
				// We assume that every leg of the support ships journey will take the amount of time
				// for the support ship to fly from its current location to the requester.  This is
				// a bit hacky but it penalizes further away support ships, so a futher support ship
				// will only be called if the closer ones are really busy.  This is just a craps shoot
				// anyway because everything is moving around.
				float howlong = 0;
				for( int i = 0; i < MAX_AI_GOALS; i++ ) {
					if ( aip->goals[i].ai_mode == AI_GOAL_REARM_REPAIR ) {
						howlong += dist * objp->phys_info.max_vel.xyz.z;
					}
				}
				if ( min_time_till_available < 0.0f || howlong < min_time_till_available ) {
					min_time_till_available = howlong;
					soonest_available_support_ship = objp;
				}
			}
			else
			{
				// support ship not already busy, find the closest
				if ( min_dist < 0.0f || dist < min_dist )
				{
					min_dist = dist;
					nearest_support_ship = objp;
				}
			}
		}
	}

	// no ships present?
	// (be advised we may have an Arriving_support_ship in this case)
	if (num_support_ships == 0) {
		return 0;
	}
	// ship available?
	else if (nearest_support_ship != NULL) {
		// the nearest non-busy support ship is to service request
		if (ship_we_found != NULL)
			*ship_we_found = nearest_support_ship;
		return 1;
	}
	// no ships available; are we below the limits?  (can we bring another ship in? -- and btw an Arriving_support_ship counts as being able to bring one in)
	else if ((num_support_ships < The_mission.support_ships.max_concurrent_ships)
		&& (	(Arriving_support_ship == NULL && The_mission.support_ships.tally < The_mission.support_ships.max_support_ships)
			 || (Arriving_support_ship != NULL && The_mission.support_ships.tally <= The_mission.support_ships.max_support_ships) ))
	{
		// We are allowed more support ships in the mission; request another ship
		// to service this request.
		return 2;
	}
	// we're at the limit, but maybe a ship will become available
	else if (soonest_available_support_ship != NULL) {
		// found more support ships than should be in mission, so I can't ask for more,
		// instead I will give the player the ship that will be done soonest
		if (ship_we_found != NULL)
			*ship_we_found = soonest_available_support_ship;
		return 3;
	}
	// none of the above; we're out of luck
	else {
		return 4;
	}
}

/**
 * Called in game_shutdown() to free malloced memory
 *
 * NOTE: do not call this function.  It is only called from ::game_shutdown()
 */
void ship_close()
{
	int i;

	for (i=0; i<MAX_SHIPS; i++ )	{
		ship *shipp = &Ships[i];

		if(shipp->warpin_effect != NULL)
			delete shipp->warpin_effect;
		shipp->warpin_effect = NULL;

		if(shipp->warpout_effect != NULL)
			delete shipp->warpout_effect;
		shipp->warpout_effect = NULL;
	}

	// free this too! -- Goober5000
	ship_clear_subsystems();

	// free info from parsed table data
	Ship_info.clear();

	for (i = 0; i < (int)Ship_types.size(); i++) {
		Ship_types[i].ai_actively_pursues.clear();
		Ship_types[i].ai_actively_pursues_temp.clear();
	}
	Ship_types.clear();
}	

/**
 * Assign object-linked sound to a particular ship
 */
void ship_assign_sound(ship *sp)
{
	ship_info	*sip;	
	object *objp;
	ship_subsys *moveup;
	bool has_engine = false;

	Assert( sp->objnum >= 0 );
	if(sp->objnum < 0){
		return;
	}

	objp = &Objects[sp->objnum];
	sip = &Ship_info[sp->ship_info_index];

	// Do subsystem sounds	
	moveup = GET_FIRST(&sp->subsys_list);
	while(moveup != END_OF_LIST(&sp->subsys_list)) {
		if (!strnicmp(moveup->system_info->name, "engine", 6)) {
			has_engine = true;
		}

		// Check for any engine sounds		
		if(strstr(moveup->system_info->name, "enginelarge")){
			obj_snd_assign(sp->objnum, GameSounds::ENGINE_LOOP_LARGE, &moveup->system_info->pnt);
		} else if(strstr(moveup->system_info->name, "enginehuge")){
			obj_snd_assign(sp->objnum, GameSounds::ENGINE_LOOP_HUGE, &moveup->system_info->pnt);
		}

		//Do any normal subsystem sounds
		if(moveup->current_hits > 0.0f)
		{
			if(moveup->system_info->alive_snd.isValid())
			{
				obj_snd_assign(sp->objnum, moveup->system_info->alive_snd, &moveup->system_info->pnt, OS_SUBSYS_ALIVE, moveup);
                moveup->subsys_snd_flags.set(Ship::Subsys_Sound_Flags::Alive);
			}
			if(moveup->system_info->turret_base_rotation_snd.isValid())
			{
				obj_snd_assign(sp->objnum, moveup->system_info->turret_base_rotation_snd, &moveup->system_info->pnt, OS_TURRET_BASE_ROTATION, moveup);
				moveup->subsys_snd_flags.set(Ship::Subsys_Sound_Flags::Turret_rotation);
			}
			if(moveup->system_info->turret_gun_rotation_snd.isValid())
			{
				obj_snd_assign(sp->objnum, moveup->system_info->turret_gun_rotation_snd, &moveup->system_info->pnt, OS_TURRET_GUN_ROTATION, moveup);
				moveup->subsys_snd_flags.set(Ship::Subsys_Sound_Flags::Turret_rotation);
			}
			if((moveup->system_info->rotation_snd.isValid()) && (moveup->flags[Ship::Subsystem_Flags::Rotates]))
			{
				obj_snd_assign(sp->objnum, moveup->system_info->rotation_snd, &moveup->system_info->pnt, OS_SUBSYS_ROTATION, moveup);
				moveup->subsys_snd_flags.set(Ship::Subsys_Sound_Flags::Rotate);
			}
		} 
		else 
		{
			if(moveup->system_info->dead_snd.isValid())
			{
				obj_snd_assign(sp->objnum, moveup->system_info->dead_snd, &moveup->system_info->pnt, OS_SUBSYS_DEAD, moveup);
				moveup->subsys_snd_flags.set(Ship::Subsys_Sound_Flags::Dead);
			}
		}

		// next
		moveup = GET_NEXT(moveup);
	}

	if (sip->engine_snd.isValid()) {
		vec3d engine_pos;

		// Only put the engine sound near the back of the ship if it has an engine.  Otherwise put it in the center.
		if (has_engine) {
			vm_vec_copy_scale(&engine_pos, &objp->orient.vec.fvec, -objp->radius / 2.0f);
		} else {
			engine_pos = vmd_zero_vector;
		}

		obj_snd_assign(sp->objnum, sip->engine_snd, &engine_pos, OS_MAIN | OS_ENGINE);
	}
}

/**
 * Assign object-linked sounds to all ships currently in the obj_used_list
 */
void ship_assign_sound_all()
{
	object *objp;
	size_t idx;
	bool has_sounds;

	if ( !Sound_enabled )
		return;

	for ( objp = GET_FIRST(&obj_used_list); objp !=END_OF_LIST(&obj_used_list); objp = GET_NEXT(objp) ) {		
		if (objp->flags[Object::Object_Flags::Should_be_dead])
			continue;

		if ( objp->type == OBJ_SHIP && Player_obj != objp) {
			has_sounds = false;

			// check to make sure this guy hasn't got sounds already assigned to him
			for(idx=0; idx<objp->objsnd_num.size(); idx++){
				if(objp->objsnd_num[idx] != -1){
					// skip
					has_sounds = true;
					break;
				}
			}

			// actually assign the sound
			if(!has_sounds){
				ship_assign_sound(&Ships[objp->instance]);
			}
		}
	}
}


/**
 * Debug console function to set the shield for the player ship
 */
DCF(set_shield,"Change player ship shield strength")
{
	float value;

	if (dc_optional_string_either("help", "--help")) {
		dc_printf ("Usage: set_shield [num]\n");
		dc_printf ("[num] --  shield percentage 0.0 -> 1.0 of max\n");
		return;
	}

	if (dc_optional_string_either("status", "--status") || dc_optional_string_either("?", "--?")) {
		dc_printf( "Shields are currently %.2f", shield_get_strength(Player_obj) );
		return;
	}

	dc_stuff_float(&value);

	CLAMP(value, 0.0f, 1.0f);

	shield_set_strength(Player_obj, value * shield_get_max_strength(Player_obj));
	dc_printf("Shields set to %.2f\n", shield_get_strength(Player_obj) );
}

/**
 * Debug console function to set the hull for the player ship
 */
DCF(set_hull, "Change player ship hull strength")
{
	float value;
	
	if (dc_optional_string_either("help", "--help")) {
		dc_printf ("Usage: set_hull [num]\n");
		dc_printf ("[num] --  hull percentage 0.0 -> 1.0 of max\n");
		return;
	}

	if (dc_optional_string_either("status", "--status") || dc_optional_string_either("?", "--?")) {
		dc_printf( "Hull is currently %.2f", Player_obj->hull_strength );
		return;
	}

	dc_stuff_float(&value);

	CLAMP(value, 0.0f, 1.0f);
	Player_obj->hull_strength = value * Player_ship->ship_max_hull_strength;
	dc_printf("Hull set to %.2f\n", Player_obj->hull_strength );
}

/**
 * Debug console function to set the strength of a particular subsystem
 */
//XSTR:OFF
DCF(set_subsys, "Set the strength of a particular subsystem on player ship" )
{
	SCP_string arg;
	int subsystem = SUBSYSTEM_NONE;
	float val_f;
	
	if (dc_optional_string_either("help", "--help")) {
		dc_printf( "Usage: set_subsys <type> [--status] <strength>\n");
		dc_printf("<type> is any of the following:\n");
		dc_printf("\tweapons\n");
		dc_printf("\tengine\n");
		dc_printf("\tsensors\n");
		dc_printf("\tcommunication\n");
		dc_printf("\tnavigation\n");
		dc_printf("\tradar\n\n");

		dc_printf("[--status] will display status of that subsystem\n\n");
		
		dc_printf("<strength> is any value between 0 and 1.0\n");
		return;
	}

	dc_stuff_string_white(arg);

	if (arg == "weapons") {
		subsystem = SUBSYSTEM_WEAPONS;
	
	} else if (arg == "engine") {
		subsystem = SUBSYSTEM_ENGINE;	
	
	} else if (arg == "sensors") {
		subsystem = SUBSYSTEM_SENSORS;

	} else if (arg == "communication") {
		subsystem = SUBSYSTEM_COMMUNICATION;

	} else if (arg == "navigation") {
		subsystem = SUBSYSTEM_NAVIGATION;

	} else if (arg == "radar") {
		subsystem = SUBSYSTEM_RADAR;

	} else if ((arg == "status") || (arg == "--status") || (arg == "?") || (arg == "--?")) {
		dc_printf("Error: Must specify a subsystem.\n");
		return;

	} else {
		dc_printf("Error: Unknown argument '%s'\n", arg.c_str());
		return;
	}

	if (dc_optional_string_either("status", "--status") || dc_optional_string_either("?", "--?")) {
		dc_printf("Subsystem '%s' is at %f strength\n", arg.c_str(), ship_get_subsystem_strength(Player_ship, subsystem));

	} else {
		// Set the subsystem strength
		dc_stuff_float(&val_f);

		CLAMP(val_f, 0.0f, 1.0f);
		ship_set_subsystem_strength( Player_ship, subsystem, val_f );
		
		if (subsystem == SUBSYSTEM_ENGINE) {
			// If subsystem is an engine, set/clear the disabled flag
			Player_ship->flags.set(Ship_Flags::Disabled, (val_f < ENGINE_MIN_STR));
		}
	}
}
//XSTR:ON

// console function to toggle whether auto-repair for subsystems is active
#ifndef NDEBUG
DCF_BOOL( auto_repair, Ship_auto_repair )
#endif

// two functions to keep track of counting ships of particular types.  Maybe we should be rolling this
// thing into the stats section??  The first function adds a ship of a particular type to the overall
// count of ships of that type (called from MissionParse.cpp).  The second function adds to the kill total
// of ships of a particular type.  Note that we use the ship_info flags structure member to determine
// what is happening.

//WMC - ALERT!!!!!!!!!!!
//These two functions did something weird with fighters/bombers. I don't
//think that not doing this will break anything, but it might.
//If it does, get me. OR someone smart.
//G5K - Someone smart to the rescue!  Fixed the functions so they don't
//accidentally overwrite all the information.

void ship_clear_ship_type_counts()
{
	// resize if we need to
	Ship_type_counts.resize(Ship_types.size());

	// clear all the stats
	for (size_t i = 0; i < Ship_type_counts.size(); i++)
	{
		Ship_type_counts[i].killed = 0;
		Ship_type_counts[i].total = 0;
	}
}

void ship_add_ship_type_count( int ship_info_index, int num )
{
	int type = ship_class_query_general_type(ship_info_index);

	//Ship has no type or the vector isn't set up
	if (!SCP_vector_inbounds(Ship_type_counts, type)) {
		return;
	}

	//Add it
	Ship_type_counts[type].total += num;
}

static void ship_add_ship_type_kill_count( int ship_info_index )
{
	int type = ship_class_query_general_type(ship_info_index);

	//Ship has no type or the vector isn't set up
	if (!SCP_vector_inbounds(Ship_type_counts, type)) {
		return;
	}

	//Add it
	Ship_type_counts[type].killed++;
}

int ship_query_general_type(int ship)
{
	return ship_query_general_type(&Ships[ship]);
}

int ship_query_general_type(ship *shipp)
{
	return ship_class_query_general_type(shipp->ship_info_index);
}

int ship_class_query_general_type(int ship_class)
{
	//This is quick
	return Ship_info[ship_class].class_type;
}

/**
 * Returns true
 */
int ship_docking_valid(int  /*docker*/, int  /*dockee*/)
{
	// Goober5000
	// So many people have asked for this function to be extended that it's making less
	// and less sense to keep it around.  We should probably just let any ship type
	// dock with any other ship type and assume the mission designer is smart enough not to
	// mess things up.
	return 1;
}

// function to return a random ship in a starting player wing.  Returns -1 if a suitable
// one cannot be found
// input:	max_dist	=>	OPTIONAL PARAMETER (default value 0.0f) max range ship can be from player
// input:   persona  => OPTIONAL PARAMETER (default to -1) which persona to get
int ship_get_random_player_wing_ship( int flags, float max_dist, int persona_index, int get_first, int multi_team )
{
	const int MAX_SIZE = MAX_SHIPS_PER_WING * MAX_SQUADRON_WINGS;

	int i, j, ship_index, count;
	int slist[MAX_SIZE], which_one;

	// iterate through starting wings of player.  Add ship indices of ships which meet
	// given criteria
	count = 0;
	for (i = 0; i < Num_wings; i++ ) {
		if (count >= MAX_SIZE)
			break;

		int wingnum = -1;

		// multi-team?
		if(multi_team >= 0){
			if( i == TVT_wings[multi_team] ) {
				wingnum = i;
			} else {
				continue;
			}
		} else {
			// first check for a player starting wing
			for ( j = 0; j < MAX_STARTING_WINGS; j++ ) {
				if ( i == Starting_wings[j] ) {
					wingnum = i;
					break;
				}
			}

			// if not found, then check all squad wings (Goober5000)
			if ( wingnum == -1 ) {
				for ( j = 0; j < MAX_SQUADRON_WINGS; j++ ) {
					if ( i == Squadron_wings[j] ) {
						wingnum = i;
						break;
					}
				}
			}

			if ( wingnum == -1 ){
				continue;
			}
		}

		for ( j = 0; j < Wings[wingnum].current_count; j++ ) {
			if (count >= MAX_SIZE)
				break;

			ship_index = Wings[wingnum].ship_index[j];
			Assert( ship_index != -1 );

			if ( Ships[ship_index].flags[Ship_Flags::Dying] ) {
				continue;
			}

			// see if ship meets our criteria
			if ( (flags == SHIP_GET_NO_PLAYERS || flags == SHIP_GET_UNSILENCED) && (Objects[Ships[ship_index].objnum].flags[Object::Object_Flags::Player_ship]) ){
				continue;
			}
			if (flags == SHIP_GET_UNSILENCED) {
				if (Ships[ship_index].flags[Ship_Flags::No_builtin_messages])
					continue;
				if (The_mission.ai_profile->flags[AI::Profile_Flags::Check_comms_for_non_player_ships] && hud_communications_state(&Ships[ship_index]) <= COMM_DAMAGED)
					continue;
			}

			// don't process ships on a different team
			if(multi_team < 0){
				if ( Player_ship->team != Ships[ship_index].team ){
					continue;
				}
			}

			// see if ship is within max_dist units
			if ( (max_dist > 1.0f) && (multi_team < 0) ) {
				float dist;
				dist = vm_vec_dist_quick(&Objects[Ships[ship_index].objnum].pos, &Player_obj->pos);
				if ( dist > max_dist ) {
					continue;
				}
			}

			// if we should be checking persona's, then don't add ships that don't have the proper persona
			if ( persona_index != -1 ) {
				if ( Ships[ship_index].persona_index != persona_index ){
					continue;
				}
			}

			// return the first ship with correct persona
			if (get_first) {
				return ship_index;
			}

			slist[count] = ship_index;
			count++;
		}
	}

	if ( count == 0 ){
		return -1;
	}

	// now get a random one from the list
	which_one = Random::next(count);
	ship_index = slist[which_one];

	Assert ( Ships[ship_index].objnum != -1 );

	return ship_index;
}

// like above function, but returns a random ship in the given wing -- no restrictions
// input:	max_dist	=>	OPTIONAL PARAMETER (default value 0.0f) max range ship can be from player
int ship_get_random_ship_in_wing(int wingnum, int flags, float max_dist, int get_first, int order_id)
{
	int i, ship_index, slist[MAX_SHIPS_PER_WING], count, which_one;

	count = 0;
	for ( i = 0; i < Wings[wingnum].current_count; i++ ) {
		ship_index = Wings[wingnum].ship_index[i];
		Assert( ship_index != -1 );

		if ( Ships[ship_index].flags[Ship_Flags::Dying] ) {
			continue;
		}

		// specific to sending messages, make sure the ship selected can actually do the order
		if (order_id >= 0) {
			if (!hud_squadmsg_ship_order_valid(ship_index, order_id)) {
				continue;
			}
		}

		// see if ship meets our criterea
		if ( (flags == SHIP_GET_NO_PLAYERS || flags == SHIP_GET_UNSILENCED) && (Objects[Ships[ship_index].objnum].flags[Object::Object_Flags::Player_ship]) )
			continue;

		if ( (flags == SHIP_GET_UNSILENCED) && (Ships[ship_index].flags[Ship_Flags::No_builtin_messages]) )
		{
			continue;
		}

		// see if ship is within max_dist units
		if ( max_dist > 0 ) {
			float dist;
			dist = vm_vec_dist_quick(&Objects[Ships[ship_index].objnum].pos, &Player_obj->pos);
			if ( dist > max_dist ) {
				continue;
			}
		}

		// return the first ship in wing
		if (get_first) {
			return ship_index;
		}

		slist[count] = ship_index;
		count++;
	}

	if ( count == 0 ) {
		return -1;
	}

	// now get a random one from the list
	which_one = Random::next(count);
	ship_index = slist[which_one];

	Assert ( Ships[ship_index].objnum != -1 );

	return ship_index;
}


// this function returns a random index into the Ship array of a ship of the given team
// cargo containers are not counted as ships for the purposes of this function.  Why???
// because now it is only used for getting a random ship for a message and cargo containers
// can't send mesages.  This function is an example of kind of bad coding :-(
// input:	max_dist	=>	OPTIONAL PARAMETER (default value 0.0f) max range ship can be from player
int ship_get_random_team_ship(int team_mask, int flags, float max_dist )
{
	int num, which_one;
	object *obj_list[MAX_SHIPS];

	// for any allied, go through the ships list and find all of the ships on that team
	num = 0;
	for (auto so: list_range(&Ship_obj_list)) {
		auto objp = &Objects[so->objnum];
		if (objp->flags[Object::Object_Flags::Should_be_dead])
			continue;

		// series of conditionals one per line for easy reading
		// don't process ships on wrong team
		// don't process cargo's or navbuoys
		// don't process player ships if flags are set
		if (!iff_matches_mask(Ships[objp->instance].team, team_mask))
			continue;
		else if ( !Ship_info[Ships[objp->instance].ship_info_index].is_flyable() )
			continue;
		else if ( (flags == SHIP_GET_NO_PLAYERS) && (objp->flags[Object::Object_Flags::Player_ship]) )
			continue;
		else if ( (flags == SHIP_GET_ONLY_PLAYERS) && !(objp->flags[Object::Object_Flags::Player_ship]) )
			continue;

		if ( Ships[objp->instance].flags[Ship_Flags::Dying] ) {
			continue;
		}

		// see if ship is within max_dist units
		if ( max_dist > 0 ) {
			float dist;
			dist = vm_vec_dist_quick(&objp->pos, &Player_obj->pos);
			if ( dist > max_dist ) {
				continue;
			}
		}

		obj_list[num] = objp;
		num++;
	}

	if ( num == 0 )
		return -1;

	which_one = Random::next(num);
	return obj_list[which_one]->instance;
}

// -----------------------------------------------------------------------
// ship_secondary_bank_has_ammo()
//
// check if currently selected secondary bank has ammo
//
// input:	shipnum	=>	index into Ships[] array for ship to check
//
int ship_secondary_bank_has_ammo(int shipnum)
{
	ship_weapon	*swp;

	Assert(shipnum >= 0 && shipnum < MAX_SHIPS);
	swp = &Ships[shipnum].weapons;
	int current_bank = swp->current_secondary_bank;
	
	if (current_bank == -1 )
		return 0;

	Assert(current_bank >= 0 && current_bank < MAX_SHIP_SECONDARY_BANKS );

	if (swp->secondary_bank_weapons[current_bank] < 0)
		return 0;

	if (Weapon_info[swp->secondary_bank_weapons[current_bank]].wi_flags[Weapon::Info_Flags::SecondaryNoAmmo])
		return 1;

	if ( swp->secondary_bank_ammo[current_bank] <= 0 )
		return 0;

	return 1;
}

// see if there is enough engine power to allow the ship to warp
// returns 1 if ship is able to warp, otherwise return 0
int ship_engine_ok_to_warp(ship *sp)
{
	if (sp->flags[Ship_Flags::Warp_broken] || sp->flags[Ship_Flags::Warp_never])
		return 0;

	// ships which can maneuver can also warp
	if (sp->flags[Ship_Flags::Maneuver_despite_engines])
		return 1;

	// disabled ships can't warp
	if (sp->flags[Ship_Flags::Disabled])
		return 0;

	// since the required strength to warp is above the minimum engine contribution,
	// and we don't otherwise need the exact number, we can use the no_minimum_engine_str flag
	float engine_strength = ship_get_subsystem_strength(sp, SUBSYSTEM_ENGINE, false, true);

	// if at 0% strength, can't warp
	if (engine_strength <= 0.0f)
		return 0;

	// player ships playing above Very Easy can't warp when below a threshold
	if ((sp == Player_ship) && (Game_skill_level > 0) && (engine_strength < SHIP_MIN_ENGINES_TO_WARP))
		return 0;

	// otherwise, warp is allowed
	return 1;
}

// Goober5000
// see if there is enough navigation power to allow the ship to warp
// returns 1 if ship is able to warp, otherwise return 0
int ship_navigation_ok_to_warp(ship *sp)
{
	// if not using the special flag, warp is always allowed
	if (!(The_mission.ai_profile->flags[AI::Profile_Flags::Navigation_subsys_governs_warp]))
		return 1;

	float navigation_strength = ship_get_subsystem_strength(sp, SUBSYSTEM_NAVIGATION);

	// if at 0% strength, can't warp
	if (navigation_strength <= 0.0f)
		return 0;

	// player ships playing above Very Easy can't warp when below a threshold
	if ((sp == Player_ship) && (Game_skill_level > 0) && (navigation_strength < SHIP_MIN_NAV_TO_WARP))
		return 0;

	// otherwise, warp is allowed
	return 1;
}

// wookieejedi
// checks both the warp flags and ship_engine_ok_to_warp() and ship_navigation_ok_to_warp() --wookieejedi
// returns true if ship is able to warp, otherwise return false
bool ship_can_warp_full_check(ship* sp)
{
	if (!(sp->cannot_warp_flags()) && ship_navigation_ok_to_warp(sp) && ship_engine_ok_to_warp(sp)) {
		// there are no warp flags that are preventing us from warping and the nav and engines are good
		// thus warp is allowed
		return true;
	} else {
		// warp is not allowed since one of those conditions was not met
		return false;
	}
}

// wookieejedi
// check to see if a ship can depart via bay 
// takes into account if the ship is in a wing
// returns true if the ship has a bay departure and the mothership is present, false otherwise
bool ship_can_bay_depart(ship* sp)
{
	// if this ship belongs to a wing, then use the wing departure information
	DepartureLocation departure_location;
	int departure_anchor;
	int departure_path_mask;
	if (sp->wingnum >= 0)
	{
		wing *wingp = &Wings[sp->wingnum];
		departure_location = wingp->departure_location;
		departure_anchor = wingp->departure_anchor;
		departure_path_mask = wingp->departure_path_mask;
	} else {
		departure_location = sp->departure_location;
		departure_anchor = sp->departure_anchor;
		departure_path_mask = sp->departure_path_mask;
	}
	
	if ( departure_location == DepartureLocation::TO_DOCK_BAY )
	{
		Assertion( departure_anchor >= 0, "Ship %s must have a valid departure anchor", sp->ship_name );
		auto anchor_ship_entry = ship_registry_get(Parse_names[departure_anchor]);
		if (anchor_ship_entry && anchor_ship_entry->has_shipp() && ship_useful_for_departure(anchor_ship_entry->shipnum, departure_path_mask)) {
			// can bay depart at this time
			return true;
		}
	}

	// cannot bay depart at this time
	return false;
}

// Calculate the normal vector from a subsystem position and its first path point
// input:	sp	=>	pointer to ship that is parent of subsystem
//				ss =>	pointer to subsystem of interest
//				norm	=> output parameter... vector from subsys to first path point
//
//	exit:		0	=>	a valid vector was placed in norm
//				!0	=> an path normal could not be calculated
//				
int ship_return_subsys_path_normal(const ship *shipp, const ship_subsys *ss, const vec3d *gsubpos, vec3d *norm)
{
	if ( ss->system_info->path_num >= 0 ) {
		polymodel	*pm = NULL;
		model_path	*mp;
		vec3d		*path_point;
		vec3d		gpath_point;
		pm = model_get(Ship_info[shipp->ship_info_index].model_num);
		Assert( pm != NULL );

		// possibly a bad model?
		Assertion(ss->system_info->path_num <= pm->n_paths, "Too many paths in '%s'!  Max is %i and the requested path was %i for subsystem '%s'!\n", pm->filename, pm->n_paths, ss->system_info->path_num, ss->system_info->subobj_name);
		if (ss->system_info->path_num > pm->n_paths) 
			return 1;

		mp = &pm->paths[ss->system_info->path_num];
		if ( mp->nverts >= 2 ) {
			path_point = &mp->verts[0].pos;
			// get path point in world coords
			vm_vec_unrotate(&gpath_point, path_point, &Objects[shipp->objnum].orient);
			vm_vec_add2(&gpath_point, &Objects[shipp->objnum].pos);
			// get unit vector pointing from subsys pos to first path point
			vm_vec_normalized_dir(norm, &gpath_point, gsubpos);
			return 0;
		}
	}
	return 1;
}


//	Determine if the subsystem can be viewed from eye_pos.  The method is to check where the
// vector from eye_pos to the subsystem hits the ship.  If distance from the hit position and
// the center of the subsystem is within a range (currently the subsystem radius) it is considered
// in view (return true).  If not in view, return false.
//
// input:	objp		=>		object that is the ship with the subsystem on it
//				subsys	=>		pointer to the subsystem of interest
//				eye_pos	=>		world coord for the eye looking at the subsystem
//				subsys_pos			=>	world coord for the center of the subsystem of interest
//				do_facing_check	=>	OPTIONAL PARAMETER (default value is true), do a dot product check to see if subsystem fvec is facing
//											towards the eye position	
//				dot_out	=>		OPTIONAL PARAMETER, output parameter, will return dot between subsys fvec and subsys_to_eye_vec
//									(only filled in if do_facing_check is true)
//				vec_out	=>		OPTIONAL PARAMETER, vector from eye_pos to absolute subsys_pos.  (only filled in if do_facing_check is true)
bool ship_subsystem_in_sight(const object *objp, const ship_subsys *subsys, const vec3d *eye_pos, const vec3d *subsys_pos, bool do_facing_check, float *dot_out, vec3d *vec_out)
{
	float		dist, dot;
	vec3d	terminus, eye_to_pos, subsys_fvec, subsys_to_eye_vec;

	if (objp->type != OBJ_SHIP)
		return false;

	// See if we are at least facing the subsystem
	if ( do_facing_check ) {
		if ( ship_return_subsys_path_normal(&Ships[objp->instance], subsys, subsys_pos, &subsys_fvec) ) {
			// non-zero return value means that we couldn't generate a normal from path info... so use inaccurate method
			vm_vec_normalized_dir(&subsys_fvec, subsys_pos, &objp->pos);
		}

		vm_vec_normalized_dir(&subsys_to_eye_vec, eye_pos, subsys_pos);
		dot = vm_vec_dot(&subsys_fvec, &subsys_to_eye_vec);
		if ( dot_out ) {
			*dot_out = dot;
		}

		if (vec_out) {
			*vec_out = subsys_to_eye_vec;
			vm_vec_negate(vec_out);
		}

		if ( dot <= 0 )
			return false;
	}

	// See if ray from eye to subsystem actually hits close enough to the subsystem position
	vm_vec_normalized_dir(&eye_to_pos, subsys_pos, eye_pos);
	vm_vec_scale_add(&terminus, eye_pos, &eye_to_pos, 100000.0f);

	mc_info	mc;
	mc.model_instance_num = Ships[objp->instance].model_instance_num;
	mc.model_num = Ship_info[Ships[objp->instance].ship_info_index].model_num;			// Fill in the model to check
	mc.orient = &objp->orient;										// The object's orientation
	mc.pos = &objp->pos;												// The object's position
	mc.p0 = eye_pos;													// Point 1 of ray to check
	mc.p1 = &terminus;												// Point 2 of ray to check
	mc.flags = MC_CHECK_MODEL;	

	model_collide(&mc);

	if ( !mc.num_hits ) {
		return false;
	}	

	// determine if hitpos is close enough to subsystem
	dist = vm_vec_dist(&mc.hit_point_world, subsys_pos);

	return (dist <= subsys->system_info->radius);
}

/**
 * Find a subsystem matching 'type' inside the ship, and that is not destroyed.  
 * @return If cannot find one, return NULL.
 */
ship_subsys *ship_return_next_subsys(ship *shipp, int type, vec3d *attacker_pos)
{
	ship_subsys	*ssp;

	Assert ( type >= 0 && type < SUBSYSTEM_MAX );

	// If aggregate total is 0, that means no subsystem is alive of that type
	if ( shipp->subsys_info[type].aggregate_max_hits <= 0.0f )
		return NULL;

	// loop through all the subsystems, if we find a match that has some strength, return it
	ssp = ship_get_best_subsys_to_attack(shipp, type, attacker_pos);

	return ssp;
}

// Returns the closest subsystem of specified type that is in line of sight.
// Returns null if all subsystems of that type are destroyed or none is in sight.
ship_subsys *ship_get_closest_subsys_in_sight(const ship *sp, int subsys_type, const vec3d *attacker_pos)
{
	Assert ( subsys_type >= 0 && subsys_type < SUBSYSTEM_MAX );

	// If aggregate total is 0, that means no subsystem is alive of that type
	if ( sp->subsys_info[subsys_type].aggregate_max_hits <= 0.0f )
		return NULL;

	ship_subsys	*closest_in_sight_subsys;
	ship_subsys	*ss;
	vec3d		gsubpos;
	float		closest_dist;
	float		ss_dist;

	closest_in_sight_subsys = NULL;
	closest_dist = FLT_MAX;

	for (ss = GET_FIRST(&sp->subsys_list); ss != END_OF_LIST(&sp->subsys_list); ss = GET_NEXT(ss) ) {
		if ( (ss->system_info->type == subsys_type) && (ss->current_hits > 0) ) {

			// get world pos of subsystem
			vm_vec_unrotate(&gsubpos, &ss->system_info->pnt, &Objects[sp->objnum].orient);
			vm_vec_add2(&gsubpos, &Objects[sp->objnum].pos);
			
			if ( ship_subsystem_in_sight(&Objects[sp->objnum], ss, attacker_pos, &gsubpos) ) {
				ss_dist = vm_vec_dist_squared(attacker_pos, &gsubpos);

				if ( ss_dist < closest_dist ) {
					closest_dist = ss_dist;
					closest_in_sight_subsys = ss;
				}
			}
		}
	}

	return closest_in_sight_subsys;
}

const char *ship_subsys_get_name(const ship_subsys *ss)
{
	if( ss->sub_name[0] != '\0' )
		return ss->sub_name;
	else
		return ss->system_info->name;
}

bool ship_subsys_has_instance_name(const ship_subsys *ss)
{
	if( ss->sub_name[0] != '\0' )
		return true;
	else
		return false;
}

void ship_subsys_set_name(ship_subsys* ss, const char* n_name) { strncpy(ss->sub_name, n_name, NAME_LENGTH - 1); }

//from aiturret.cpp. Less include...problems...this way.
extern flagset<Weapon::Info_Flags> turret_weapon_aggregate_flags(const ship_weapon *swp);
extern bool turret_weapon_has_subtype(const ship_weapon *swp, int subtype);

const char *get_turret_subsys_name(const ship_weapon *swp)
{
	Assert(swp != nullptr);	// Goober5000 //WMC

	//WMC - find the first weapon, if there is one
	if (swp->num_primary_banks || swp->num_secondary_banks) {
		// allow the first weapon on the turret to specify the name
		for (int i = 0; i < swp->num_primary_banks; ++i) {
			auto wip = &Weapon_info[swp->primary_bank_weapons[i]];
			if (*(wip->altSubsysName) != '\0') {
				return wip->altSubsysName;
			}
		}
		for (int i = 0; i < swp->num_secondary_banks; ++i) {
			auto wip = &Weapon_info[swp->secondary_bank_weapons[i]];
			if (*(wip->altSubsysName) != '\0') {
				return wip->altSubsysName;
			}
		}

		// otherwise use a general name based on the type of weapon(s) on the turret
		auto flags = turret_weapon_aggregate_flags(swp);

		// check if beam or flak using weapon flags
		if (flags[Weapon::Info_Flags::Beam]) {
			return XSTR("Beam turret", 1567);
		} else if (flags[Weapon::Info_Flags::Flak]) {
			return XSTR("Flak turret", 1566);
		} else {
			if (turret_weapon_has_subtype(swp, WP_MISSILE)) {
				return XSTR("Missile lnchr", 1569);
			} else if (turret_weapon_has_subtype(swp, WP_LASER)) {
				// ballistic too! - Goober5000
				if (flags[Weapon::Info_Flags::Ballistic]) {
					return XSTR("Turret", 1487);
				}
				// the TVWP has some primaries flagged as bombs
				else if (flags[Weapon::Info_Flags::Bomb]) {
					return XSTR("Missile lnchr", 1569);
				} else {
					return XSTR("Laser turret", 1568);
				}
			} else {
				// Mantis #2226: find out if there are any weapons here at all
				if (flags.none_set()) {
					return NOX("Unused");
				} else {
					// Illegal subtype
					static bool Turret_illegal_subtype_warned = false;
					if (!Turret_illegal_subtype_warned) {
						Turret_illegal_subtype_warned = true;
						Warning(LOCATION, "This turret has an illegal subtype!  Trace out and fix!");
					}
					return XSTR("Turret", 1487);
				}
			}
		}
	} else if(swp->num_tertiary_banks) {
		//TODO: add tertiary turret code stuff here
		return NOX("Unknown");
	} else {
		// This should not happen
		return NOX("Unused");
	}
}

const char *ship_subsys_get_name_on_hud(const ship_subsys *ss)
{
	// get turret subsys name
	if (ss->system_info->type == SUBSYSTEM_TURRET && !ship_subsys_has_instance_name(ss))
		return get_turret_subsys_name(&ss->weapons);
	else
		return ship_subsys_get_name(ss);
}

const char *ship_subsys_get_canonical_name(const ship_subsys *ss)
{
	return ss->system_info->subobj_name;
}

/**
 * Return the shield strength of the specified quadrant on hit_objp
 *
 * @param hit_objp object pointer to ship getting hit
 * @param quadrant_num shield quadrant that was hit
 * @return strength of shields in the quadrant that was hit as a percentage, between 0 and 1.0
 */
float ship_quadrant_shield_strength(const object *hit_objp, int quadrant_num)
{
	float			max_quadrant;

	// If ship doesn't have shield mesh, then return
	if ( hit_objp->flags[Object::Object_Flags::No_shields] ) {
		return 0.0f;
	}

	// If shields weren't hit, return 0
	if ( quadrant_num < 0 )
		return 0.0f;

	max_quadrant = shield_get_max_quad(hit_objp);
	if ( max_quadrant <= 0 ) {
		return 0.0f;
	}

	Assertion(quadrant_num < static_cast<int>(hit_objp->shield_quadrant.size()), "ship_quadrant_shield_strength() called with a quadrant of %d on a ship with " SIZE_T_ARG " quadrants; get a coder!\n", quadrant_num, hit_objp->shield_quadrant.size());

	if(hit_objp->shield_quadrant[quadrant_num] > max_quadrant)
		mprintf(("Warning: \"%s\" has shield quadrant strength of %f out of %f\n",
				Ships[hit_objp->instance].ship_name, hit_objp->shield_quadrant[quadrant_num], max_quadrant));

	return hit_objp->shield_quadrant[quadrant_num]/max_quadrant;
}

// Determine if a ship is threatened by any dumbfire projectiles (laser or missile)
// input:	sp	=>	pointer to ship that might be threatened
// exit:		0 =>	no dumbfire threats
//				1 =>	at least one dumbfire threat
//
// NOTE: Currently this function is only called periodically from the HUD code for the 
//       player ship.
int ship_dumbfire_threat(ship *sp)
{
	if ( (Game_mode & GM_MULTIPLAYER) && (Net_player->flags & NETINFO_FLAG_OBSERVER) ) {
		return 0;
	}

	if (ai_endangered_by_weapon(&Ai_info[sp->ai_index]) > 0) {
		return 1;
	} 

	return 0;
}

// Return !0 if there is a missile in the air homing on shipp
static int ship_has_homing_missile_locked(ship *shipp)
{
	object		*locked_objp, *A;
	weapon		*wp;
	weapon_info	*wip;
	missile_obj	*mo;

	Assert(shipp->objnum >= 0 && shipp->objnum < MAX_OBJECTS);
	locked_objp = &Objects[shipp->objnum];

	// check for currently locked missiles (highest precedence)
	for ( mo = GET_NEXT(&Missile_obj_list); mo != END_OF_LIST(&Missile_obj_list); mo = GET_NEXT(mo) ) {
		Assert(mo->objnum >= 0 && mo->objnum < MAX_OBJECTS);
		A = &Objects[mo->objnum];
		if (A->flags[Object::Object_Flags::Should_be_dead])
			continue;

		if (A->type != OBJ_WEAPON)
			continue;

		Assert((A->instance >= 0) && (A->instance < MAX_WEAPONS));
		wp = &Weapons[A->instance];
		wip = &Weapon_info[wp->weapon_info_index];

		if ( wip->subtype != WP_MISSILE )
			continue;

		if ( !(wip->is_homing() ) )
			continue;

		if (wp->homing_object == locked_objp) {
			return 1;
		}
	}	// end for 

	return 0;
}

// Return !0 if there is some ship attempting to lock onto shipp
static int ship_is_getting_locked(ship *shipp)
{
	ship_obj	*so;
	object	*objp;
	ai_info	*aip;

	for ( so = GET_FIRST(&Ship_obj_list); so != END_OF_LIST(&Ship_obj_list); so = GET_NEXT(so) ) {
		objp = &Objects[so->objnum];
		if (objp->flags[Object::Object_Flags::Should_be_dead])
			continue;
		aip = &Ai_info[Ships[objp->instance].ai_index];

		if ( aip->target_objnum == shipp->objnum ) {
			if ( aip->aspect_locked_time > 0.1f && !shipp->flags[Ship::Ship_Flags::Aspect_immune]) {
				float dist, wep_range;
				dist = vm_vec_dist_quick(&objp->pos, &Objects[shipp->objnum].pos);
				wep_range = ship_get_secondary_weapon_range(&Ships[objp->instance]);
				if ( wep_range > dist ) {
					nprintf(("Alan","AI ship is seeking lock\n"));
					return 1;
				}
			}
		}
	}

	return 0;
}

// Determine if a ship is threatened by attempted lock or actual lock
// input:	sp	=>	pointer to ship that might be threatened
// exit:		0 =>	no lock threats of any kind
//				1 =>	at least one attempting lock (no actual locks)
//				2 =>	at least one lock (possible other attempting locks)
//
// NOTE: Currently this function is only called periodically from the HUD code for the 
//       player ship.
int ship_lock_threat(ship *sp)
{
	if ( ship_has_homing_missile_locked(sp) ) {
		return 2;
	}

	if ( ship_is_getting_locked(sp) ) {
		return 1;
	}

	return 0;
}

// converts a bitmask, such as 0x08, into the bit number this would be (3 in this case)
// NOTE: Should move file to something like Math_utils.
int bitmask_2_bitnum(int num)
{
	int i;

	for (i=0; i<32; i++)
		if (num & (1 << i))
			return i;

	return -1;
}

static const char* ship_get_ai_target_display_name(int goal, const char* name)
{
	switch (goal) {
	// These goals refer to ships so we need to retrieve their display name
	case AI_GOAL_FORM_ON_WING:
	case AI_GOAL_CHASE:
	case AI_GOAL_DOCK:
	case AI_GOAL_UNDOCK:
	case AI_GOAL_GUARD:
	case AI_GOAL_DISABLE_SHIP:
	case AI_GOAL_DISABLE_SHIP_TACTICAL:
	case AI_GOAL_DISARM_SHIP:
	case AI_GOAL_DISARM_SHIP_TACTICAL:
	case AI_GOAL_EVADE_SHIP:
	case AI_GOAL_REARM_REPAIR:
	case AI_GOAL_FLY_TO_SHIP:
	case AI_GOAL_DESTROY_SUBSYSTEM: {
		auto ship = ship_name_lookup(name);
		if (ship < 0) {
			return name;
		}
		return Ships[ship].get_display_name();
	}

		// These goals need no special handling
	case AI_GOAL_CHASE_WING:
	case AI_GOAL_CHASE_SHIP_CLASS:
	case AI_GOAL_GUARD_WING:
	case AI_GOAL_WAYPOINTS:
	case AI_GOAL_WAYPOINTS_ONCE:
	default:
		return name;
	}
}

// Get a text description of a ships orders. 
//
//	input:	outbuf	=>		buffer to hold orders string
//				sp			=>		ship pointer to extract orders from
//
// exit:		NULL		=>		printable orders are not applicable
//				non-NULL	=>		pointer to string that was passed in originally
//
// This function is called from HUD code to get a text description
// of what a ship's orders are.  Feel free to use this function if 
// it suits your needs for something.
//
SCP_string ship_return_orders(ship* sp)
{
	ai_info* aip;
	ai_goal* aigp;

	Assert(sp->ai_index >= 0);
	aip = &Ai_info[sp->ai_index];

	// The active goal is always in the first element of aip->goals[]
	aigp = &aip->goals[0];

	auto order_text = Ai_goal_text(aigp->ai_mode, aigp->ai_submode);
	if (order_text == nullptr)
		return SCP_string();

	SCP_string outbuf = order_text;

	const char *target_name;
	if (aigp->target_name) {
		target_name = ship_get_ai_target_display_name(aigp->ai_mode, aigp->target_name);
	} else {
		target_name = "";
	}

	switch (aigp->ai_mode) {
	case AI_GOAL_FORM_ON_WING:
	case AI_GOAL_GUARD_WING:
	case AI_GOAL_CHASE_WING:
		if (aigp->target_name) {
			outbuf += target_name;
			outbuf += XSTR(" wing", 494);
		} else {
			outbuf = XSTR("no orders", 495);
		}
		break;

	case AI_GOAL_CHASE_SHIP_CLASS:
		if (aigp->target_name) {
			outbuf += XSTR("any ", -1);
			outbuf += target_name;
		} else {
			outbuf = XSTR("no orders", 495);
		}
		break;

	case AI_GOAL_CHASE:
	case AI_GOAL_DOCK:
	case AI_GOAL_UNDOCK:
	case AI_GOAL_GUARD:
	case AI_GOAL_DISABLE_SHIP:
	case AI_GOAL_DISABLE_SHIP_TACTICAL:
	case AI_GOAL_DISARM_SHIP:
	case AI_GOAL_DISARM_SHIP_TACTICAL:
	case AI_GOAL_EVADE_SHIP:
	case AI_GOAL_REARM_REPAIR:
	case AI_GOAL_FLY_TO_SHIP:
		if (aigp->target_name) {
			outbuf += target_name;
		} else {
			outbuf = XSTR("no orders", 495);
		}
		break;

	case AI_GOAL_DESTROY_SUBSYSTEM: {
		if (aip->targeted_subsys != nullptr) {
			char subsys_name[NAME_LENGTH];
			strcpy_s(subsys_name, aip->targeted_subsys->system_info->subobj_name);
			hud_targetbox_truncate_subsys_name(subsys_name);
			sprintf(outbuf, XSTR("atk %s %s", 496), target_name, subsys_name);
		} else {
			outbuf = XSTR("no orders", 495);
		}
		break;
	}

	case AI_GOAL_WAYPOINTS:
	case AI_GOAL_WAYPOINTS_ONCE:
	case AI_GOAL_LUA:
		// don't do anything, all info is in order_text
		break;

	default:
		return SCP_string();
	}

	return outbuf;
}

// return the amount of time until ship reaches its goal (in MM:SS format)
//	input:	outbuf	=>		buffer to hold orders string
//				sp			=>		ship pointer to extract orders from
//
// exit:		NULL		=>		printable orders are not applicable
//				non-NULL	=>		pointer to string that was passed in originally
//
// This function is called from HUD code to get a text description
// of what a ship's orders are.  Feel free to use this function if 
// it suits your needs for something.
char *ship_return_time_to_goal(char *outbuf, ship *sp)
{
	int	minutes, seconds;
	int time = ship_return_seconds_to_goal(sp);
	if ( time >= 0 ) {
		minutes = time/60;
		seconds = time%60;
		if ( minutes > 99 ) {
			minutes = 99;
			seconds = 99;
		}
		sprintf(outbuf, NOX("%02d:%02d"), minutes, seconds);

	} else if ( time == -1 ) {
		strcpy( outbuf, XSTR( "Unknown", 497) );

	} else {
		// we don't want to display anything on the HUD
		return nullptr;
	}

	return outbuf;
}

// Returns the estimated time, in seconds, of the given ship to reach its goal
//
// @param[in] sp  Pointer to ship to check
//
// @returns Time of the ship to reach its goal, or
// @returns  0, if it has met its goal, or
// @returns -1, if the ship is not moving or can not move, or
// @returns -2, if the ship does not have a valid goal
//
// @note tcrayford: split out of ship_return_time_to_goal
int ship_return_seconds_to_goal(ship *sp)
{
	ai_info	*aip;
	int		time;
	float		dist = 0.0f;
	object	*objp;	
	float		min_speed, max_speed;

	objp = &Objects[sp->objnum];
	aip = &Ai_info[sp->ai_index];

	min_speed = objp->phys_info.speed;

	// Goober5000 - handle cap
	if (aip->waypoint_speed_cap > 0)
		max_speed = MIN(objp->phys_info.max_vel.xyz.z, i2fl(aip->waypoint_speed_cap));
	else
		max_speed = objp->phys_info.max_vel.xyz.z;

	if ( aip->mode == AIM_WAYPOINTS ) {
		// Is traveling a waypoint path
		min_speed = 0.9f * max_speed;
		auto wp_list = find_waypoint_list_at_index(aip->wp_list_index);
		if (wp_list != nullptr) {
			Assert(aip->wp_index != INVALID_WAYPOINT_POSITION);
			auto& waypoints = wp_list->get_waypoints();

			// distance to current waypoint
			dist += vm_vec_dist_quick(&objp->pos, waypoints[aip->wp_index].get_pos());

			// distance from current waypoint to the end of the list, whichever way we're going
			const vec3d *prev_vec = nullptr;
			if (aip->wp_flags & WPF_BACKTRACK) {
				for (int i = aip->wp_index; i >= 0; --i) {
					if (prev_vec != nullptr) {
						dist += vm_vec_dist_quick(waypoints[i].get_pos(), prev_vec);
					}
					prev_vec = waypoints[i].get_pos();
				}
			} else {
				for (int i = aip->wp_index; i < (int)waypoints.size(); ++i) {
					if (prev_vec != nullptr) {
						dist += vm_vec_dist_quick(waypoints[i].get_pos(), prev_vec);
					}
					prev_vec = waypoints[i].get_pos();
				}
			}
		}

		if ( dist < 1.0f) {
			// Already there
			time = 0;

		} else if ( (Objects[sp->objnum].phys_info.speed <= 0) || (max_speed <= 0.0f) ) {
			// Is not moving or can not move
			time = -1;

		} else {
			// On the way!
			float	speed;

			speed = objp->phys_info.speed;

			if (speed < min_speed)
				speed = min_speed;
			time = fl2i(dist/speed);
		}

	} else if ( (aip->mode == AIM_DOCK) && (aip->submode < AIS_DOCK_4) ) {
		// Is traveling a docking path
		time = hud_get_dock_time( objp );

	} else {
		// Not a waypoint or docking goal
		time = -2;
	}

	return time;
}

/* Karajorma - V decided not to use this function so I've commented it out so it isn't confused with code
+that is actually in use. Someone might want to get it working using AI_Profiles at some point so I didn't
+simply delete it.

// Called to check if any AI ships might reveal the cargo of any cargo containers.
//
// This is called once a frame, but a global timer 'Ship_cargo_check_timer' will limit this
// function to being called every SHIP_CARGO_CHECK_INTERVAL ms.  I think that should be sufficient.
//
// NOTE: This function uses CARGO_REVEAL_DISTANCE from the HUD code... which is a multiple of
//       the ship radius that is used to determine when cargo is detected.  AI ships do not 
//       have to have the ship targeted to reveal cargo.  The player is ignored in this function.
#define SHIP_CARGO_CHECK_INTERVAL	1000
void ship_check_cargo_all()
{
	object	*cargo_objp;
	ship		*cargo_sp, *ship_sp;
	float		dist_squared, limit_squared;

	// I don't want to do this check every frame, so I made a global timer to limit check to
	// every SHIP_CARGO_CHECK_INTERVAL ms.
	if ( !timestamp_elapsed(Ship_cargo_check_timer) ) {
		return;
	} else {
		Ship_cargo_check_timer = timestamp(SHIP_CARGO_CHECK_INTERVAL);
	}

	// Check all friendly fighter/bombers against all non-friendly cargo containers that don't have
	// cargo revealed

	// for now just locate a capital ship on the same team:
	for (auto cargo_so: list_range(&Ship_obj_list)){
		if (Objects[cargo_so->objnum].flags[Object::Object_Flags::Should_be_dead])
			continue;

		cargo_sp = &Ships[Objects[cargo_so->objnum].instance];
		if ( (Ship_info[cargo_sp->ship_info_index].flags[Ship::Info_Flags::Cargo]) && (cargo_sp->team != Player_ship->team) ) {
			
			// If the cargo is revealed, continue on to next hostile cargo
			if ( cargo_sp->flags[Ship::Ship_Flags::Cargo_revealed] ) {
				continue;
			}

			// check against friendly fighter/bombers + cruiser/freighter/transport
			// IDEA: could cull down to fighter/bomber if we want this to run a bit quicker
			for (auto ship_so: list_range(&Ship_obj_list))
			{
				if (Objects[ship_so->objnum].flags[Object::Object_Flags::Should_be_dead])
					continue;

				ship_sp = &Ships[Objects[ship_so->objnum].instance];
				// only consider friendly ships
				if (ship_sp->team != Player_ship->team) {
					continue;
				}

				// ignore the player
				if ( ship_so->objnum == OBJ_INDEX(Player_obj) ) {
					continue;
				}

				// if this ship is a small or big ship
				if ( Ship_info[ship_sp->ship_info_index].flags & (SIF_SMALL_SHIP|SIF_BIG_SHIP) ) {
					cargo_objp = &Objects[cargo_sp->objnum];
					// use square of distance, faster than getting real distance (which will use sqrt)
					dist_squared = vm_vec_dist_squared(&cargo_objp->pos, &Objects[ship_sp->objnum].pos);
					limit_squared = (cargo_objp->radius+CARGO_RADIUS_DELTA)*(cargo_objp->radius+CARGO_RADIUS_DELTA);
					if ( dist_squared <= MAX(limit_squared, CARGO_REVEAL_MIN_DIST*CARGO_REVEAL_MIN_DIST) ) {
						ship_do_cargo_revealed( cargo_sp );
						break;	// break out of for loop, move on to next hostile cargo
					}
				}
			} // end for
		}
	} // end for
}
*/


// Maybe warn player about this attacking ship.  This is called once per frame, and the
// information about the closest attacking ship comes for free, since this function is called
// from HUD code which has already determined the closest enemy attacker and the distance.
//
// input:	enemy_sp	=>	ship pointer to the TEAM_ENEMY ship attacking the player
//				dist		=>	the distance of the enemy to the player
//
// NOTE: there are no filters on enemy_sp, so it could be any ship type
//
#define PLAYER_CHECK_WARN_INTERVAL		300		// how often we check for warnings
#define PLAYER_MIN_WARN_DIST				100		// minimum distance attacking ship can be from player and still allow warning
#define PLAYER_MAX_WARN_DIST				1000		// maximum distance attacking ship can be from plyaer and still allow warning

void ship_maybe_warn_player(ship *enemy_sp, float dist)
{
	float		fdot; //, rdot, udot;
	vec3d	vec_to_target;

	// First check if the player has reached the maximum number of warnings for a mission
	if ((Builtin_messages[MESSAGE_CHECK_6].max_count > -1) && ( Player->warn_count >= Builtin_messages[MESSAGE_CHECK_6].max_count )) {
		return;
	}

	// Check if enough time has elapsed since last warning, if not - leave
	if ( !timestamp_elapsed(Player->allow_warn_timestamp) ) {
		return;
	}

	// Check to see if check timer has elapsed.  Necessary, since we don't want to check each frame
	if ( !timestamp_elapsed(Player->check_warn_timestamp ) ) {
		return;
	}
	Player->check_warn_timestamp = timestamp(PLAYER_CHECK_WARN_INTERVAL);

	// only allow warnings if within a certain distance range
	if ( dist < PLAYER_MIN_WARN_DIST || dist > PLAYER_MAX_WARN_DIST ) {
		return;
	}

	// only warn if a fighter or bomber is attacking the player
	if ( !(Ship_info[enemy_sp->ship_info_index].is_small_ship()) ) {
		return;
	}

	// get vector from player to target
	vm_vec_normalized_dir(&vec_to_target, &Objects[enemy_sp->objnum].pos, &Eye_position);

	// ensure that enemy fighter is oriented towards player
	fdot = vm_vec_dot(&Objects[enemy_sp->objnum].orient.vec.fvec, &vec_to_target);
	if ( fdot > -0.7 ) {
		return;
	}

	fdot = vm_vec_dot(&Player_obj->orient.vec.fvec, &vec_to_target);

	// check if attacking ship is on six.  return if not far enough behind player.
	if ( fdot > -0.7 ) {
		return;
	}

	int ship_index;

	// multiplayer tvt - this is client side.
	if(MULTI_TEAM && (Net_player != NULL)){
		ship_index = ship_get_random_player_wing_ship( SHIP_GET_UNSILENCED, 0.0f, -1, 0, Net_player->p_info.team );
	} else {
		ship_index = ship_get_random_player_wing_ship( SHIP_GET_UNSILENCED );
	}

	if (ship_index >= 0) {
		// multiplayer - make sure I just send to myself
		bool sent;
		if (Game_mode & GM_MULTIPLAYER) {
			sent = message_send_builtin(MESSAGE_CHECK_6, &Ships[ship_index], enemy_sp, MY_NET_PLAYER_NUM, -1);
		} else {
			sent = message_send_builtin(MESSAGE_CHECK_6, &Ships[ship_index], enemy_sp, -1, -1);
		}
		if (sent) {
			Player->allow_warn_timestamp = timestamp(Builtin_messages[MESSAGE_CHECK_6].min_delay);
			Player->warn_count++;
		}
	}
}

// player has just killed a ship, maybe offer send a 'good job' message
void ship_maybe_praise_player(ship *deader_sp)
{
	// First check if the player has reached the maximum number of praises for a mission
	if ((Builtin_messages[MESSAGE_PRAISE].max_count > -1) && (Player->praise_count >= Builtin_messages[MESSAGE_PRAISE].max_count )) {
		return;
	}

	// Check if enough time has elapsed since last praise, if not - leave
	if ( !timestamp_elapsed(Player->allow_praise_timestamp) ) {
		return;
	}

	// make sure player is not a traitor
	if (Player_ship->team == Iff_traitor) {
		return;
	}

	// only praise if killing an enemy!
	if ( deader_sp->team == Player_ship->team ) {
		return;
	}

	// don't praise the destruction of navbuoys, cargo or other non-flyable ship types
    if ((Ship_info[deader_sp->ship_info_index].class_type >= 0) && !(Ship_types[Ship_info[deader_sp->ship_info_index].class_type].flags[Ship::Type_Info_Flags::Praise_destruction])) {
		return;
	}

	// There is already a praise pending
	if ( Player->praise_delay_timestamp ) {
		return;
	}

	// We don't want to praise the player right away.. it is more realistic to wait a moment
	Player->praise_delay_timestamp = timestamp_rand(1000, 2000);
}

void ship_maybe_praise_self(ship *deader_sp, ship *killer_sp)
{
	int j; 
	bool wingman = false;

	if (Game_mode & GM_MULTIPLAYER) {
		return;
	}

	if ((Builtin_messages[MESSAGE_PRAISE_SELF].max_count > -1) && (Player->praise_self_count >= Builtin_messages[MESSAGE_PRAISE_SELF].max_count)) {
		return;
	}

	if (!timestamp_elapsed(Player->praise_self_timestamp)) {
		return;
	}

	// only praise if killing an enemy so check they both attack each other!
	if (!((iff_x_attacks_y(deader_sp->team, killer_sp->team)) && (iff_x_attacks_y(killer_sp->team, deader_sp->team ))) ) {
		return;
	}

	
	// only send messages from the player's wingmen
	if (killer_sp->wingnum == -1) {
		return; 
	}
	for ( j = 0; j < MAX_STARTING_WINGS; j++ ) {
		if ( Starting_wings[j] == killer_sp->wingnum) {
			wingman = true; 
			break;
		}
	}

	if (!wingman) {
		return;
	}

	// don't praise the destruction of navbuoys, cargo or other non-flyable ship types
	if ( (Ship_info[deader_sp->ship_info_index].class_type >= 0) && !(Ship_types[Ship_info[deader_sp->ship_info_index].class_type].flags[Ship::Type_Info_Flags::Praise_destruction]) ) {
		return;
	}

	// ensure the ship isn't silenced
	if ( killer_sp->flags[Ship_Flags::No_builtin_messages] ) {
		return; 
	}
	if (The_mission.ai_profile->flags[AI::Profile_Flags::Check_comms_for_non_player_ships] && hud_communications_state(killer_sp) != COMM_OK) {
		return;
	}

	if (message_send_builtin(MESSAGE_PRAISE_SELF, killer_sp, deader_sp, -1, -1)) {
		Player->praise_self_timestamp = timestamp(Builtin_messages[MESSAGE_PRAISE_SELF].min_delay);
		Player->praise_self_count++;
	}
}

// -----------------------------------------------------------------------------
static void awacs_maybe_ask_for_help(ship *sp, int multi_team_filter)
{
	// Goober5000 - bail if not in main fs2 campaign
	if (stricmp(Campaign.filename, "freespace2") != 0 || !(Game_mode & GM_CAMPAIGN_MODE))
		return;

	object *objp;
	int message = -1;
	objp = &Objects[sp->objnum];

	if ( objp->hull_strength < ( (AWACS_HELP_HULL_LOW + 0.01f *(static_rand(OBJ_INDEX(objp)) & 5)) * sp->ship_max_hull_strength) ) {
		// awacs ship below 25 + (0-4) %
		if (!(sp->awacs_warning_flag[Ship::Awacs_Warning_Flags::Warn_25])) {
			message = MESSAGE_AWACS_25;
            sp->awacs_warning_flag.set(Ship::Awacs_Warning_Flags::Warn_25);
		}
	} else if ( objp->hull_strength < ( (AWACS_HELP_HULL_HI + 0.01f*(static_rand(OBJ_INDEX(objp)) & 5)) * sp->ship_max_hull_strength) ) {
		// awacs ship below 75 + (0-4) %
		if (!(sp->awacs_warning_flag[Ship::Awacs_Warning_Flags::Warn_75])) {
			message = MESSAGE_AWACS_75;
            sp->awacs_warning_flag.set(Ship::Awacs_Warning_Flags::Warn_75);
		}
	}

	if (message >= 0) {
		if (message_send_builtin(message, sp, nullptr, -1, multi_team_filter)) {
			Player->allow_ask_help_timestamp = timestamp(Builtin_messages[MESSAGE_HELP].min_delay);
			Player->ask_help_count++;
		}
	}
}

// -----------------------------------------------------------------------------
void ship_maybe_ask_for_help(ship* sp, ship* attacker)
{
	object *objp;
	int multi_team_filter = -1;
	ship_info* sip = &Ship_info[sp->ship_info_index];

	// First check if the player has reached the maximum number of ask_help's for a mission
	if ((Builtin_messages[MESSAGE_HELP].max_count > -1) && (Player->ask_help_count >= Builtin_messages[MESSAGE_HELP].max_count))
		return;

	// Check if enough time has elapsed since last help request, if not - leave
	if (!timestamp_elapsed(Player->allow_ask_help_timestamp))
		return;

	// make sure player is on their team and not a traitor
	if ((Player_ship->team != sp->team) || (Player_ship->team == Iff_traitor))
		return;

	objp = &Objects[sp->objnum];

	// don't let the player ask for help!
	if (objp->flags[Object::Object_Flags::Player_ship])
		return;

	// determine team filter if TvT
	if (MULTI_TEAM) {
		multi_team_filter = sp->team;
	}

	// handle awacs ship as a special case
	if (Ship_info[sp->ship_info_index].flags[Ship::Info_Flags::Has_awacs]) {
		awacs_maybe_ask_for_help(sp, multi_team_filter);
		return;
	}

	// for now, only have wingman ships request help
	if (!(sp->flags[Ship_Flags::From_player_wing])) {
		return;
	}

	// first check if hull is at a critical level
	if (objp->hull_strength < sip->ask_help_hull_percent * sp->ship_max_hull_strength) {
		goto play_ask_help;
	}

	// check if shields are near critical level
	if (objp->flags[Object::Object_Flags::No_shields]) {
		return;	// no shields on ship, no don't check shield levels
	}

	if (shield_get_strength(objp) > (sip->ask_help_shield_percent * shield_get_max_strength(sp))) {
		return;
	}

play_ask_help:

	if (!(Ship_info[sp->ship_info_index].is_fighter_bomber())) {
		return;
	}

	if (sp->flags[Ship_Flags::No_builtin_messages]) {
		return;
	}

	if (The_mission.ai_profile->flags[AI::Profile_Flags::Check_comms_for_non_player_ships] && hud_communications_state(&Ships[objp->instance]) <= COMM_DAMAGED) {
		return;
	}

	if (message_send_builtin(MESSAGE_HELP, sp, attacker, -1, multi_team_filter)) {
		Player->allow_ask_help_timestamp = timestamp(Builtin_messages[MESSAGE_HELP].min_delay);
		Player->ask_help_count++;

		// prevent overlap with death message
		if (timestamp_until(Player->allow_scream_timestamp) < 15000) {
			Player->allow_scream_timestamp = timestamp(15000);
		}
	}
}

/**
 * The player has just entered death roll, maybe have wingman mourn the loss of the player
 */
void ship_maybe_lament()
{
	int ship_index;

	// no. because in multiplayer, its funny
	if (Game_mode & GM_MULTIPLAYER) {
		return;
	}

	if (Random::next(4) == 0) {
		ship_index = ship_get_random_player_wing_ship(SHIP_GET_UNSILENCED);
		if (ship_index >= 0) {
			message_send_builtin(MESSAGE_PLAYER_DIED, &Ships[ship_index], nullptr, -1, -1);
		}
	}
}

/**
 * Play a death scream for a ship
 */
void ship_scream(ship *sp)
{
	int multi_team_filter = -1;

	// bogus
	if (sp == NULL)
		return;

	// multiplayer tvt
	if (MULTI_TEAM)
		multi_team_filter = sp->team;

	// Bail if the ship is silenced
	if (sp->flags[Ship_Flags::No_builtin_messages])
		return;
	if (The_mission.ai_profile->flags[AI::Profile_Flags::Check_comms_for_non_player_ships] && hud_communications_state(sp, true) <= COMM_DAMAGED)
		return;

	if (message_send_builtin(MESSAGE_WINGMAN_SCREAM, sp, nullptr, -1, multi_team_filter)) {
		Player->allow_scream_timestamp = timestamp(Builtin_messages[MESSAGE_WINGMAN_SCREAM].min_delay);
		Player->scream_count++;

		// prevent overlap with help messages
		if (timestamp_until(Player->allow_ask_help_timestamp) < 15000)
			Player->allow_ask_help_timestamp = timestamp(15000);
	}
	sp->flags.set(Ship_Flags::Ship_has_screamed);
}

/**
 * Ship has just died, maybe play a scream.
 */
void ship_maybe_scream(ship *sp)
{
	// bail if screaming is disabled
	if (sp->flags[Ship_Flags::No_death_scream])
		return;

	// if screaming is enabled, skip all checks
	if (!(sp->flags[Ship_Flags::Always_death_scream]))
	{

		// check if enough time has elapsed since last scream; if not, leave
		if (!timestamp_elapsed(Player->allow_scream_timestamp))
			return;

		// for WCSaga, only do a subset of the checks
		if (!(The_mission.ai_profile->flags[AI::Profile_Flags::Perform_fewer_scream_checks]))
		{
			// bail if this ship isn't from the player wing
			if (!(sp->flags[Ship_Flags::From_player_wing]))
				return;

			// first check if the player has reached the maximum number of screams for a mission
			if ((Builtin_messages[MESSAGE_WINGMAN_SCREAM].max_count > -1) && (Player->scream_count >= Builtin_messages[MESSAGE_WINGMAN_SCREAM].max_count)) {
				return;
			}

			// if on different teams (i.e. team v. team games in multiplayer), no scream
			if (Player_ship->team != sp->team)
				return;
		}
	}

	ship_scream(sp);
}

// maybe tell player that we're running low on ammo
#define PLAYER_LOW_AMMO_MSG_INTERVAL		250000
#define PLAYER_REQUEST_REPAIR_MSG_INTERVAL	240000
#define PLAYER_MAX_LOW_AMMO_MSGS			5

/**
 * This function is only for notifying the player that the ship's ammo is low, without necessarily requesting support (e.g. if support
 * is disallowed for this mission).  It is not called if support was successfully requested in maybe_request_support.
 */
void ship_maybe_tell_about_low_ammo(ship *sp)
{
	weapon_info *wip;
	int i;
	ship_weapon *swp;
	int multi_team_filter = -1;

	// we don't want a ship complaining about low ammo after it has just complained about needing support
	if (!timestamp_elapsed(Player->request_repair_timestamp))
		return;

	if (!timestamp_elapsed(Player->allow_ammo_timestamp))
		return;

	if (Player_ship->team == Iff_traitor)
		return;

	// Silent ships should remain just that
	if (sp->flags[Ship_Flags::No_builtin_messages]) {
		return;
	}
	if (The_mission.ai_profile->flags[AI::Profile_Flags::Check_comms_for_non_player_ships] && hud_communications_state(sp) <= COMM_DAMAGED)
		return;

	// don't mention low ammo if we're docked, for the same reason as in maybe_request_support
	if (object_is_docked(&Objects[sp->objnum]))
		return;

	// for now, each ship can only complain about low ammo once a mission to stop it getting repetitive
	if (sp->ammo_low_complaint_count) {
		return;
	}

	if (Player->low_ammo_complaint_count >= PLAYER_MAX_LOW_AMMO_MSGS) {
		return;
	}

	swp = &sp->weapons;
	
	// stole the code for this from ship_maybe_tell_about_rearm()
	for (i = 0; i < swp->num_primary_banks; i++)
	{
		wip = &Weapon_info[swp->primary_bank_weapons[i]];

		if (wip->wi_flags[Weapon::Info_Flags::Ballistic])
		{
			if (swp->primary_bank_start_ammo[i] > 0)
			{
				if ((float)swp->primary_bank_ammo[i] / (float)swp->primary_bank_start_ammo[i] < 0.3f)
				{
					// multiplayer tvt
					if (MULTI_TEAM) {
						multi_team_filter = sp->team;
					}

					if (message_send_builtin(MESSAGE_PRIMARIES_LOW, sp, nullptr, -1, multi_team_filter)) {
						Player->allow_ammo_timestamp = timestamp(PLAYER_LOW_AMMO_MSG_INTERVAL);
						Player->request_repair_timestamp = timestamp(PLAYER_REQUEST_REPAIR_MSG_INTERVAL);
						Player->low_ammo_complaint_count++;
						sp->ammo_low_complaint_count++;
					}
					break;
				}
			}
		}
	}
}


/**
 * Tell player that we've requested a support ship
 */
void ship_maybe_tell_about_rearm(ship *sp)
{
	weapon_info *wip;

	if (!timestamp_elapsed(Player->request_repair_timestamp))
		return;

	if (Player_ship->team == Iff_traitor)
		return;

	// Silent ships should remain just that
	if (sp->flags[Ship_Flags::No_builtin_messages])
		return;
	if (The_mission.ai_profile->flags[AI::Profile_Flags::Check_comms_for_non_player_ships] && hud_communications_state(sp) <= COMM_DAMAGED)
		return;

	// AL 1-4-98:	If ship integrity is low, tell player you want to get repaired.  Otherwise, tell
	// the player you want to get re-armed.

	int message_type = MESSAGE_NONE;
	int heavily_damaged = (get_hull_pct(&Objects[sp->objnum]) < 0.4);

	if (heavily_damaged || (sp->flags[Ship_Flags::Disabled]))
	{
		message_type = MESSAGE_REPAIR_REQUEST;
	}
	else
	{
		int i;
		ship_weapon *swp;

		swp = &sp->weapons;
		for (i = 0; i < swp->num_secondary_banks; i++)
		{
			if (swp->secondary_bank_start_ammo[i] > 0 && !Weapon_info[swp->secondary_bank_weapons[i]].wi_flags[Weapon::Info_Flags::SecondaryNoAmmo])
			{
				if ((float)swp->secondary_bank_ammo[i] / (float)swp->secondary_bank_start_ammo[i] < 0.5f)
				{
					message_type = MESSAGE_REARM_REQUEST;
					break;
				}
			}
		}

		// also check ballistic primaries - Goober5000
		for (i = 0; i < swp->num_primary_banks; i++)
		{
			wip = &Weapon_info[swp->primary_bank_weapons[i]];

			if (wip->wi_flags[Weapon::Info_Flags::Ballistic])
			{
				if (swp->primary_bank_start_ammo[i] > 0)
				{
					if ((float)swp->primary_bank_ammo[i] / (float)swp->primary_bank_start_ammo[i] < 0.3f)
					{
						message_type = MESSAGE_REARM_PRIMARIES;
						break;
					}
				}
			}
		}
	}

	int multi_team_filter = -1;

	// multiplayer tvt
	if (MULTI_TEAM) {
		multi_team_filter = sp->team;
	}

	if (message_type != MESSAGE_NONE) {
		if (message_send_builtin(message_type, sp, nullptr, -1, multi_team_filter)) {
			Player->request_repair_timestamp = timestamp(PLAYER_REQUEST_REPAIR_MSG_INTERVAL);
		}
	}
}

// The current primary weapon or link status for a ship has changed.. notify clients if multiplayer
//
// input:	sp			=>	pointer to ship that modified primaries
void ship_primary_changed(ship *sp)
{
	int i;
	ship_weapon	*swp;
	swp = &sp->weapons;


	if (sp->flags[Ship_Flags::Primary_linked]) {
		// if we are linked now find any body who is down and flip them up
		for (i = 0; i < MAX_SHIP_PRIMARY_BANKS; i++) {
			if (swp->primary_animation_position[i] == MA_POS_NOT_SET) {
				auto animList = Ship_info[sp->ship_info_index].animations.getAll(model_get_instance(sp->model_instance_num), animation::ModelAnimationTriggerType::PrimaryBank, i);
				if ( animList.start(animation::ModelAnimationDirection::FWD) ) {
					swp->primary_animation_done_time[i] = animList.getTime();
					swp->primary_animation_position[i] = MA_POS_SET;
				} else {
					swp->primary_animation_position[i] = MA_POS_READY;
				}
			}

			if (swp->primary_bank_weapons[i] >= 0) {
				weapon_info* wip = &Weapon_info[swp->primary_bank_weapons[i]];

				// for starting the linked loop sound if there is one
				if (swp->firing_loop_sounds[i] != -1 && wip->linked_loop_firing_snd.isValid()) {
					// there was a valid loop sound before, end it
					if (swp->firing_loop_sounds[i] >= 0) {
						obj_snd_delete(&Objects[sp->objnum], swp->firing_loop_sounds[i]);
					}
					vec3d pos = model_get(Ship_info[sp->ship_info_index].model_num)->view_positions[0].pnt;

					swp->firing_loop_sounds[i] = obj_snd_assign(sp->objnum, wip->linked_loop_firing_snd, &pos, OS_PLAY_ON_PLAYER);
				}
			}
		}
	} else {
		// find anything that is up that shouldn't be
		for (i = 0; i < MAX_SHIP_PRIMARY_BANKS; i++) {
			if (i == swp->current_primary_bank) {
				// if the current bank is down raise it up
				if (swp->primary_animation_position[i] == MA_POS_NOT_SET) {
					auto animList = Ship_info[sp->ship_info_index].animations.getAll(model_get_instance(sp->model_instance_num), animation::ModelAnimationTriggerType::PrimaryBank, i);
					if (animList.start(animation::ModelAnimationDirection::FWD)) {
						swp->primary_animation_done_time[i] = animList.getTime();
						swp->primary_animation_position[i] = MA_POS_SET;
					} else {
						swp->primary_animation_position[i] = MA_POS_READY;
					}
				}

				if (swp->primary_bank_weapons[i] >= 0) {
					weapon_info* wip = &Weapon_info[swp->primary_bank_weapons[i]];

					// for stopping a linked_loop_firing_snd, and starting up the loop_firing_snd again
					// if this weapon doesn't have a linked_loop_firing_snd, there is no reason to do this
					if (swp->firing_loop_sounds[i] != -1 && wip->linked_loop_firing_snd.isValid()) {
						// there was a valid linked loop sound before, end it
						if (swp->firing_loop_sounds[i] >= 0) {
							obj_snd_delete(&Objects[sp->objnum], swp->firing_loop_sounds[i]);
						}
						vec3d pos = model_get(Ship_info[sp->ship_info_index].model_num)->view_positions[0].pnt;

						swp->firing_loop_sounds[i] = obj_snd_assign(sp->objnum, wip->loop_firing_snd, &pos, OS_PLAY_ON_PLAYER);
					}
				}
			} else {
				// everyone else should be down, if they are not make them so
				if (swp->primary_animation_position[i] != MA_POS_NOT_SET) {
					Ship_info[sp->ship_info_index].animations.getAll(model_get_instance(sp->model_instance_num), animation::ModelAnimationTriggerType::PrimaryBank, i).start(animation::ModelAnimationDirection::RWD);
					swp->primary_animation_position[i] = MA_POS_NOT_SET;
				}
			}
		}
	}

#if 0
	// we only need to deal with multiplayer issues for now, so bail it not multiplayer
	if ( !(Game_mode & GM_MULTIPLAYER) )
		return;

	Assert(sp);

	if ( MULTIPLAYER_MASTER )
		send_ship_weapon_change( sp, MULTI_PRIMARY_CHANGED, swp->current_primary_bank, (sp->flags[Ship::Ship_Flags::Primary_linked])?1:0 );
#endif
}

// The current secondary weapon or dual-fire status for a ship has changed.. notify clients if multiplayer
//
// input:	sp					=>	pointer to ship that modified secondaries
void ship_secondary_changed(ship *sp)
{
	Assert( sp != NULL );

	int i;
	ship_weapon	*swp = &sp->weapons;

	// find anything that is up that shouldn't be
	if (timestamp() > 10) {
		for (i = 0; i < MAX_SHIP_SECONDARY_BANKS; i++) {
			if (i == swp->current_secondary_bank) {
				// if the current bank is down raise it up
				if (swp->secondary_animation_position[i] == MA_POS_NOT_SET) {
					auto animList = Ship_info[sp->ship_info_index].animations.getAll(model_get_instance(sp->model_instance_num), animation::ModelAnimationTriggerType::SecondaryBank, i);
					if (animList.start(animation::ModelAnimationDirection::FWD)) {
						swp->secondary_animation_done_time[i] = animList.getTime();
						swp->secondary_animation_position[i] = MA_POS_SET;
					} else {
						swp->secondary_animation_position[i] = MA_POS_READY;
					}
				}
			} else {
				// everyone else should be down, if they are not make them so
				if (swp->secondary_animation_position[i] != MA_POS_NOT_SET) {
					Ship_info[sp->ship_info_index].animations.getAll(model_get_instance(sp->model_instance_num), animation::ModelAnimationTriggerType::SecondaryBank, i).start(animation::ModelAnimationDirection::RWD);
					swp->secondary_animation_position[i] = MA_POS_NOT_SET;
				}
			}
		}
	}

#if 0
	// we only need to deal with multiplayer issues for now, so bail it not multiplayer
	if ( !(Game_mode & GM_MULTIPLAYER) ){
		return;
	}

	Assert(sp);

	if ( MULTIPLAYER_MASTER )
		send_ship_weapon_change( sp, MULTI_SECONDARY_CHANGED, swp->current_secondary_bank, (sp->flags[Ship::Ship_Flags::Secondary_dual_fire])?1:0 );
#endif
}

flagset<Ship::Info_Flags> ship_get_SIF(ship *shipp)
{
	return Ship_info[shipp->ship_info_index].flags;
}

flagset<Ship::Info_Flags> ship_get_SIF(int sh)
{
	return Ship_info[Ships[sh].ship_info_index].flags;
}

int ship_get_by_signature(int signature)
{
	ship_obj *so;
		
	for ( so = GET_FIRST(&Ship_obj_list); so != END_OF_LIST(&Ship_obj_list); so = GET_NEXT(so) ) {		
		if (Objects[so->objnum].flags[Object::Object_Flags::Should_be_dead])
			continue;

		// if we found a matching ship object signature
		if((Objects[so->objnum].signature == signature) && (Objects[so->objnum].type == OBJ_SHIP)){
			return Objects[so->objnum].instance;
		}
	}

	// couldn't find the ship
	return -1;
}

ship_type_info *ship_get_type_info(object *objp)
{
	Assert(objp != NULL);
	Assert(objp->type == OBJ_SHIP);
	Assert(objp->instance > -1);
	Assert(Ships[objp->instance].ship_info_index > -1);

	if (Ship_info[Ships[objp->instance].ship_info_index].class_type < 0)
		return nullptr;

	return &Ship_types[Ship_info[Ships[objp->instance].ship_info_index].class_type];
}

/**
 * Called when the cargo of a ship is revealed.
 *
 * Happens at two different locations (at least when this function was written), one for the player, and one for AI ships.
 * Need to send stuff to clients in multiplayer game.
 */
void ship_do_cargo_revealed( ship *shipp, int from_network )
{
	// don't do anything if we already know the cargo
	if ( shipp->flags[Ship_Flags::Cargo_revealed] ){
		return;
	}
	
	nprintf(("Network", "Revealing cargo for %s\n", shipp->ship_name));

	// send the packet if needed
	if ( (Game_mode & GM_MULTIPLAYER) && !from_network ){
		send_cargo_revealed_packet( shipp );		
	}

	shipp->flags.set(Ship_Flags::Cargo_revealed);
	shipp->time_cargo_revealed = Missiontime;	

	// if the cargo is something other than "nothing", then make a log entry
	if ( stricmp(Cargo_names[shipp->cargo1 & CARGO_INDEX_MASK], NOX("nothing")) != 0 ){
		mission_log_add_entry(LOG_CARGO_REVEALED, shipp->ship_name, NULL, (shipp->cargo1 & CARGO_INDEX_MASK) );
	}	
}

void ship_do_cap_subsys_cargo_revealed( ship *shipp, ship_subsys *subsys, int from_network )
{
	// don't do anything if we already know the cargo
	if (subsys->flags[Ship::Subsystem_Flags::Cargo_revealed]) {
		return;
	}

	nprintf(("Network", "Revealing cap ship subsys cargo for %s\n", shipp->ship_name));

	// send the packet if needed
	if ( (Game_mode & GM_MULTIPLAYER) && !from_network ){
		int subsystem_index = ship_get_subsys_index(subsys);
		send_subsystem_cargo_revealed_packet( shipp, subsystem_index );		
	}

    subsys->flags.set(Ship::Subsystem_Flags::Cargo_revealed);
	subsys->time_subsys_cargo_revealed = Missiontime;

	// if the cargo is something other than "nothing", then make a log entry
	if ( stricmp(Cargo_names[subsys->subsys_cargo_name & CARGO_INDEX_MASK], NOX("nothing")) != 0 ){
		mission_log_add_entry(LOG_CAP_SUBSYS_CARGO_REVEALED, shipp->ship_name, subsys->system_info->subobj_name, (subsys->subsys_cargo_name & CARGO_INDEX_MASK) );
	}	
}

/**
 * alled when the cargo of a ship is hidden by the sexp.  
 *
 * Need to send stuff to clients in multiplayer game.
 */
void ship_do_cargo_hidden( ship *shipp, int from_network )
{
	// don't do anything if the cargo is already hidden
	if ( !(shipp->flags[Ship_Flags::Cargo_revealed]) )
	{
		return;
	}
	
	nprintf(("Network", "Hiding cargo for %s\n", shipp->ship_name));

	// send the packet if needed
	if ( (Game_mode & GM_MULTIPLAYER) && !from_network ){
		send_cargo_hidden_packet( shipp );		
	}

	shipp->flags.remove(Ship_Flags::Cargo_revealed);

	// don't log that the cargo was hidden and don't reset the time cargo revealed
}

void ship_do_cap_subsys_cargo_hidden( ship *shipp, ship_subsys *subsys, int from_network )
{
	// don't do anything if the cargo is already hidden
	if (!(subsys->flags[Ship::Subsystem_Flags::Cargo_revealed]))
	{
		return;
	}

	nprintf(("Network", "Hiding cap ship subsys cargo for %s\n", shipp->ship_name));

	// send the packet if needed
	if ( (Game_mode & GM_MULTIPLAYER) && !from_network ){
		int subsystem_index = ship_get_subsys_index(subsys);
		send_subsystem_cargo_hidden_packet( shipp, subsystem_index );		
	}

    subsys->flags.remove(Ship::Subsystem_Flags::Cargo_revealed);

	// don't log that the cargo was hidden and don't reset the time cargo revealed
}

/**
 * Return the range of the currently selected secondary weapon
 *
 * NOTE: If there is no missiles left in the current bank, range returned is 0
 *
 * @param shipp Pointer to ship from which currently selected secondary weapon will be ranged
 */
float ship_get_secondary_weapon_range(ship *shipp)
{
	float srange=0.0f;

	ship_weapon	*swp;
	swp = &shipp->weapons;
	if ( swp->current_secondary_bank >= 0 ) {
		weapon_info	*wip;
		int bank=swp->current_secondary_bank;
		if (swp->secondary_bank_weapons[bank] >= 0) {
			wip = &Weapon_info[swp->secondary_bank_weapons[bank]];
			if ( swp->secondary_bank_ammo[bank] > 0 || wip->wi_flags[Weapon::Info_Flags::SecondaryNoAmmo]) {
				srange = wip->max_speed * wip->lifetime;
			}
		}
	}

	return srange;
}

/**
 * Determine the number of primary ammo units allowed max for a ship
 */
int get_max_ammo_count_for_primary_bank(int ship_class, int bank, int ammo_type)
{
	Assertion(ship_class < ship_info_size(), "Invalid ship_class of %d is >= Ship_info.size() (%d); get a coder!\n", ship_class, ship_info_size());
	Assertion(bank < MAX_SHIP_PRIMARY_BANKS, "Invalid primary bank of %d (max is %d); get a coder!\n", bank, MAX_SHIP_PRIMARY_BANKS - 1);
	Assertion(ammo_type < weapon_info_size(), "Invalid ammo_type of %d is >= Weapon_info.size() (%d); get a coder!\n", ammo_type, weapon_info_size());

	// Invalid and non-existent weapons, and non-ballistic weapons, have capacities of 0, per ship_weapon::clear()
	if (ship_class < 0 || bank < 0 || ammo_type < 0 || !(Weapon_info[ammo_type].wi_flags[Weapon::Info_Flags::Ballistic]))
		return 0;

	float capacity = (float)Ship_info[ship_class].primary_bank_ammo_capacity[bank];
	float size = (float)Weapon_info[ammo_type].cargo_size;
	Assertion(size > 0.0f, "Weapon cargo size for %s must be greater than 0!", Weapon_info[ammo_type].name);
	return (int)std::lround(capacity / size);
}

/**
 * Determine the number of secondary ammo units (missile/bomb) allowed max for a ship
 */
int get_max_ammo_count_for_bank(int ship_class, int bank, int ammo_type)
{
	Assertion(ship_class < ship_info_size(), "Invalid ship_class of %d is >= Ship_info.size() (%d); get a coder!\n", ship_class, ship_info_size());
	Assertion(bank < MAX_SHIP_SECONDARY_BANKS, "Invalid secondary bank of %d (max is %d); get a coder!\n", bank, MAX_SHIP_SECONDARY_BANKS - 1);
	Assertion(ammo_type < weapon_info_size(), "Invalid ammo_type of %d is >= Weapon_info.size() (%d); get a coder!\n", ammo_type, weapon_info_size());

	// Invalid and non-existent weapons have capacities of 0, per ship_weapon::clear()
	if (ship_class < 0 || bank < 0 || ammo_type < 0)
		return 0;

	float capacity = (float)Ship_info[ship_class].secondary_bank_ammo_capacity[bank];
	float size = (float)Weapon_info[ammo_type].cargo_size;
	Assertion(size > 0.0f, "Weapon cargo size for %s must be greater than 0!", Weapon_info[ammo_type].name);
	return (int)std::lround(capacity / size);
}

/**
 * The same as above, but for a specific turret's bank.
 */
int get_max_ammo_count_for_turret_bank(ship_weapon *swp, int bank, int ammo_type)
{
	float capacity, size;

	Assertion(bank < MAX_SHIP_SECONDARY_BANKS, "Invalid secondary bank of %d (max is %d); get a coder!\n", bank, MAX_SHIP_SECONDARY_BANKS - 1);
	Assertion(ammo_type < weapon_info_size(), "Invalid ammo_type of %d is >= Weapon_info.size() (%d); get a coder!\n", ammo_type, weapon_info_size());

	if (!swp || bank < 0 || ammo_type < 0) {
		return 0;
	} else {
		capacity = (float) swp->secondary_bank_capacity[bank];
		size = (float) Weapon_info[ammo_type].cargo_size;
		return (int) (capacity / size);
	}
}

/**
 * Page in bitmaps for all the ships in this level
 */
void ship_page_in()
{
	TRACE_SCOPE(tracing::ShipPageIn);

	int i, j, k;
	int num_subsystems_needed = 0;

	int *ship_class_used = NULL;

	ship_class_used = new int[Ship_info.size()];

	Verify( ship_class_used != NULL );

	// Mark all ship classes as not used
	memset( ship_class_used, 0, Ship_info.size() * sizeof(int) );

	// Mark any support ship types as used
	for (auto sip = Ship_info.begin(); sip != Ship_info.end(); ++sip) {
		if (sip->flags[Ship::Info_Flags::Support]) {
			nprintf(( "Paging", "Found support ship '%s'\n", sip->name ));
			i = (int)std::distance(Ship_info.begin(), sip);
			ship_class_used[i]++;

			num_subsystems_needed += sip->n_subsystems;

			// load the darn model and page in textures
			sip->model_num = model_load(sip->pof_file, &*sip);

			if (sip->model_num >= 0) {
				model_page_in_textures(sip->model_num, i);
			}
		}
	}
	
	// Mark any ships in the mission as used
	for (i = 0; i < MAX_SHIPS; i++)	{
		if (Ships[i].objnum < 0)
			continue;
	
		nprintf(( "Paging","Found ship '%s'\n", Ships[i].ship_name ));
		ship_class_used[Ships[i].ship_info_index]++;

		// mark any weapons as being used, saves memory and time if we don't load them all
		ship_weapon *swp = &Ships[i].weapons;

		for (j = 0; j < swp->num_primary_banks; j++)
			weapon_mark_as_used(swp->primary_bank_weapons[j]);

		for (j = 0; j < swp->num_secondary_banks; j++)
			weapon_mark_as_used(swp->secondary_bank_weapons[j]);

		// get weapons for all capship subsystems (turrets)
		ship_subsys *ptr = GET_FIRST(&Ships[i].subsys_list);

		while (ptr != END_OF_LIST(&Ships[i].subsys_list)) {
			for (k = 0; k < MAX_SHIP_PRIMARY_BANKS; k++)
				weapon_mark_as_used(ptr->weapons.primary_bank_weapons[k]);

			for (k = 0; k < MAX_SHIP_SECONDARY_BANKS; k++)
				weapon_mark_as_used(ptr->weapons.secondary_bank_weapons[k]);

			ptr = GET_NEXT(ptr);
		}

		ship_info *sip = &Ship_info[Ships[i].ship_info_index];

		// page in all of the textures if the model is already loaded
		if (sip->model_num >= 0) {
			nprintf(( "Paging", "Paging in textures for ship '%s'\n", Ships[i].ship_name ));
			model_page_in_textures(sip->model_num, Ships[i].ship_info_index);
			// need to make sure and do this again, after we are sure that all of the textures are ready
			ship_init_afterburners( &Ships[i] );
		}

		//WMC - Since this is already in-mission, ignore the warpin effect.
		Ships[i].warpout_effect->pageIn();

		// don't need this one anymore, it's already been accounted for
	//	num_subsystems_needed += Ship_info[Ships[i].ship_info_index].n_subsystems;
	}

	// Mark any ships that might warp in in the future as used
	for (p_object *p_objp = GET_FIRST(&Ship_arrival_list); p_objp != END_OF_LIST(&Ship_arrival_list); p_objp = GET_NEXT(p_objp)) {
		nprintf(( "Paging", "Found future arrival ship '%s'\n", p_objp->name ));
		ship_class_used[p_objp->ship_class]++;

		// This will go through Subsys_index[] and grab all weapons: primary, secondary, and turrets
		for (i = p_objp->subsys_index; i < (p_objp->subsys_index + p_objp->subsys_count); i++) {
			for (j = 0; j < MAX_SHIP_PRIMARY_BANKS; j++) {
				if (Subsys_status[i].primary_banks[j] >= 0)
					weapon_mark_as_used(Subsys_status[i].primary_banks[j]);
			}

			for (j = 0; j < MAX_SHIP_SECONDARY_BANKS; j++) {
				if (Subsys_status[i].secondary_banks[j] >= 0)
					weapon_mark_as_used(Subsys_status[i].secondary_banks[j]);
			}
		}

		// page in any replacement textures
		if (Ship_info[p_objp->ship_class].model_num >= 0) {
			nprintf(( "Paging", "Paging in textures for future arrival ship '%s'\n", p_objp->name ));
			model_page_in_textures(Ship_info[p_objp->ship_class].model_num, p_objp->ship_class);
		}

		num_subsystems_needed += Ship_info[p_objp->ship_class].n_subsystems;
	}

	// pre-allocate the subsystems, this really only needs to happen for ships
	// which don't exist yet (ie, ships NOT in Ships[])
	if (!ship_allocate_subsystems(num_subsystems_needed, true)) {
		Error(LOCATION, "Attempt to page in new subsystems subsystems failed, which shouldn't be possible anymore. Currently allocated %d subsystems (%d in use)", Num_ship_subsystems_allocated, Num_ship_subsystems); 
	}

	mprintf(("About to page in ships!\n"));

	// Page in all the ship classes that are used on this level
	int num_ship_types_used = 0;
	int test_id __UNUSED = -1;

	for (auto& fi : Fireball_info)
		fi.fireball_used = false;

	i = 0;
	for (auto sip = Ship_info.begin(); sip != Ship_info.end(); i++, ++sip) {
		if ( !ship_class_used[i] )
			continue;

		int model_previously_loaded = -1;
		int ship_previously_loaded = -1;

		num_ship_types_used++;

		// Page in the small hud icons for each ship
		hud_ship_icon_page_in(&(*sip));

		// See if this model was previously loaded by another ship
		for (auto it = Ship_info.begin(); it != Ship_info.end(); ++it) {
			if ( (it->model_num > -1) && !stricmp(sip->pof_file, it->pof_file) ) {
				// Model already loaded
				model_previously_loaded = it->model_num;

				if ((sip->n_subsystems > 0) && (sip->subsystems[0].model_num > -1)) {
					ship_previously_loaded = (int)std::distance(Ship_info.begin(), it);

					// It is possible in some cases for sip->model_num to change, and for subsystems->model_num
					// to still point to the old model index; this makes sure it doesn't happen. -zookeeper
					for (k = 0; k < sip->n_subsystems; k++) {
						if (sip->model_num != sip->subsystems[k].model_num) {
							mprintf(("Ship %s has model_num %i but its subsystem %s has model_num %i, fixing...\n", sip->name, sip->model_num, sip->subsystems[k].name, sip->subsystems[k].model_num));
							sip->subsystems[k].model_num = sip->model_num;
						}
					}
				}

				// the model should already be loaded so this wouldn't take long, but
				// we need to make sure that the load count for the model is correct
				test_id = model_load(sip->pof_file, &*sip);
				Assert( test_id == model_previously_loaded );

				break;
			}
		}

		// If the model is previously loaded...
		if (model_previously_loaded >= 0) {
			// If previously loaded model isn't the same ship class...)
			if (ship_previously_loaded != i) {
				// update the model number.
				sip->model_num = model_previously_loaded;

				for (j = 0; j < sip->n_subsystems; j++)
					sip->subsystems[j].model_num = -1;

				ship_copy_subsystem_fixup(&(*sip));

#ifndef NDEBUG
				for (j = 0; j < sip->n_subsystems; j++) {
					if (sip->subsystems[j].model_num != sip->model_num) {
						polymodel *sip_pm = (sip->model_num >= 0) ? model_get(sip->model_num) : NULL;
						polymodel *subsys_pm = (sip->subsystems[j].model_num >= 0) ? model_get(sip->subsystems[j].model_num) : NULL;
						Warning(LOCATION, "After ship_copy_subsystem_fixup, ship '%s' does not have subsystem '%s' linked into the model file, '%s'.\n\n(Ship_info model is '%s' and subsystem model is '%s'.)", sip->name, sip->subsystems[j].subobj_name, sip->pof_file, (sip_pm != NULL) ? sip_pm->filename : "NULL", (subsys_pm != NULL) ? subsys_pm->filename : "NULL");
					}
				}
#endif
			} else {
				// Just to be safe (I mean to check that my code works...)
				Assert( sip->model_num >= 0 );
				Assert( sip->model_num == model_previously_loaded );

#ifndef NDEBUG
				for (j = 0; j < sip->n_subsystems; j++) {
					if (sip->subsystems[j].model_num != sip->model_num) {
						polymodel *sip_pm = (sip->model_num >= 0) ? model_get(sip->model_num) : NULL;
						polymodel *subsys_pm = (sip->subsystems[j].model_num >= 0) ? model_get(sip->subsystems[j].model_num) : NULL;
						Warning(LOCATION, "Without ship_copy_subsystem_fixup, ship '%s' does not have subsystem '%s' linked into the model file, '%s'.\n\n(Ship_info model is '%s' and subsystem model is '%s'.)", sip->name, sip->subsystems[j].subobj_name, sip->pof_file, (sip_pm != NULL) ? sip_pm->filename : "NULL", (subsys_pm != NULL) ? subsys_pm->filename : "NULL");
					}
				}
#endif
			}
		} else {
			// Model not loaded, so load it
			sip->model_num = model_load(sip->pof_file, &*sip);

			Assert( sip->model_num >= 0 );

#ifndef NDEBUG
			// Verify that all the subsystem model numbers are updated
			for (j = 0; j < sip->n_subsystems; j++)
				Assertion( sip->subsystems[j].model_num == sip->model_num, "Model reference for subsystem %s (model num: %d) on model %s (model num: %d) is invalid.\n", sip->subsystems[j].name, sip->subsystems[j].model_num, sip->pof_file, sip->model_num );	// JAS
#endif
		}

		// more weapon marking, the weapon info in Ship_info[] is the default
		// loadout which isn't specified by missionparse unless it's different
		for (j = 0; j < sip->num_primary_banks; j++)
			weapon_mark_as_used(sip->primary_bank_weapons[j]);

		for (j = 0; j < sip->num_secondary_banks; j++)
			weapon_mark_as_used(sip->secondary_bank_weapons[j]);

		weapon_mark_as_used(sip->cmeasure_type);

		for (j = 0; j < sip->n_subsystems; j++) {
			model_subsystem *msp = &sip->subsystems[j];

			for (k = 0; k < MAX_SHIP_PRIMARY_BANKS; k++)
				weapon_mark_as_used(msp->primary_banks[k]);

			for (k = 0; k < MAX_SHIP_SECONDARY_BANKS; k++)
				weapon_mark_as_used(msp->secondary_banks[k]);
		}

		// Page in the shockwave stuff. -C
		shockwave_create_info_load(&sip->shockwave);
		if (!sip->explosion_bitmap_anims.empty()) {
			for (int explosion_bitmap_anim: sip->explosion_bitmap_anims) {
				Fireball_info[explosion_bitmap_anim].fireball_used = true;
			}
		} else if (sip->class_type >= 0 && !Ship_types[sip->class_type].explosion_bitmap_anims.empty()) {
			for (int explosion_bitmap_anim: Ship_types[sip->class_type].explosion_bitmap_anims) {
				Fireball_info[explosion_bitmap_anim].fireball_used = true;
			}
		}
	}

	nprintf(( "Paging", "There are %d ship classes used in this mission.\n", num_ship_types_used ));


	// Page in the thruster effects
	// Make sure thrusters are loaded
	if (!Thrust_anim_inited)
		ship_init_thrusters();

	thrust_info *thruster;
	for (i = 0; i < (int)Species_info.size(); i++) {
		thruster = &Species_info[i].thruster_info;

		bm_page_in_texture( thruster->flames.normal.first_frame, thruster->flames.normal.num_frames );
		bm_page_in_texture( thruster->flames.afterburn.first_frame, thruster->flames.afterburn.num_frames );

		// glows are really not anims
		bm_page_in_texture( thruster->glow.normal.first_frame );
		bm_page_in_texture( thruster->glow.afterburn.first_frame );
	}

	// page in insignia bitmaps
	if(Game_mode & GM_MULTIPLAYER){
		for(i=0; i<MAX_PLAYERS; i++){
			if(MULTI_CONNECTED(Net_players[i]) && (Net_players[i].m_player != NULL) && (Net_players[i].m_player->insignia_texture >= 0)){
				bm_page_in_xparent_texture(Net_players[i].m_player->insignia_texture);
			}
		}
	} else {
		if((Player != NULL) && (Player->insignia_texture >= 0)){
			bm_page_in_xparent_texture(Player->insignia_texture);
		}
	}

	// page in wing insignia bitmaps - Goober5000
	for (i = 0; i < MAX_WINGS; i++)
	{
		if (Wings[i].wing_insignia_texture >= 0)
			bm_page_in_xparent_texture(Wings[i].wing_insignia_texture);
	}

	// page in replacement textures - Goober5000
	for (i = 0; i < MAX_SHIPS; i++)
	{
		// is this a valid ship?
		if (Ships[i].objnum >= 0)
		{
			polymodel_instance* pmi = object_get_model_instance(&Objects[Ships[i].objnum]);
			// do we have any textures?
			if (pmi->texture_replace != nullptr)
			{
				// page in replacement textures
				for (const auto& texture : *pmi->texture_replace)
				{
					if (texture >= 0) {
						bm_page_in_texture(texture);
					}
				}
			}
		}
	}

	// should never be NULL, this entire function wouldn't work
	delete[] ship_class_used;
	ship_class_used = NULL;

}

// Goober5000 - called from ship_page_in()
void ship_page_in_textures(int ship_index)
{
	int i;
	ship_info *sip;

	if ( (ship_index < 0) || (ship_index >= ship_info_size()) )
		return;


	sip = &Ship_info[ship_index];

	// afterburner
	if ( !generic_bitmap_load(&sip->afterburner_trail) )
		bm_page_in_texture(sip->afterburner_trail.bitmap_id);

	// Wanderer - just copying over Bobboau's code...
	if ( !generic_anim_load(&sip->thruster_flame_info.normal) )
		bm_page_in_texture(sip->thruster_flame_info.normal.first_frame);

	if ( !generic_anim_load(&sip->thruster_flame_info.afterburn) )
		bm_page_in_texture(sip->thruster_flame_info.afterburn.first_frame);

	// Bobboau's thruster bitmaps
	// the first set has to be loaded a special way
	if ( !thruster_glow_anim_load(&sip->thruster_glow_info.normal) )
		bm_page_in_texture(sip->thruster_glow_info.normal.first_frame);

	if ( !thruster_glow_anim_load(&sip->thruster_glow_info.afterburn) )
		bm_page_in_texture(sip->thruster_glow_info.afterburn.first_frame);

	// everything else is loaded normally
	if ( !generic_bitmap_load(&sip->thruster_secondary_glow_info.normal) )
		bm_page_in_texture(sip->thruster_secondary_glow_info.normal.bitmap_id);

	if ( !generic_bitmap_load(&sip->thruster_secondary_glow_info.afterburn) )
		bm_page_in_texture(sip->thruster_secondary_glow_info.afterburn.bitmap_id);

	if ( !generic_bitmap_load(&sip->thruster_tertiary_glow_info.normal) )
		bm_page_in_texture(sip->thruster_tertiary_glow_info.normal.bitmap_id);

	if ( !generic_bitmap_load(&sip->thruster_tertiary_glow_info.afterburn) )
		bm_page_in_texture(sip->thruster_tertiary_glow_info.afterburn.bitmap_id);
 
	// splodeing bitmap
	if ( VALID_FNAME(sip->splodeing_texture_name) ) {
		sip->splodeing_texture = bm_load(sip->splodeing_texture_name);
		bm_page_in_texture(sip->splodeing_texture);
	}

	// thruster/particle bitmaps
	for (i = 0; i < (int)sip->normal_thruster_particles.size(); i++) {
		generic_anim_load(&sip->normal_thruster_particles[i].thruster_bitmap);
		bm_page_in_texture(sip->normal_thruster_particles[i].thruster_bitmap.first_frame);
	}

	for (i = 0; i < (int)sip->afterburner_thruster_particles.size(); i++) {
		generic_anim_load(&sip->afterburner_thruster_particles[i].thruster_bitmap);
		bm_page_in_texture(sip->afterburner_thruster_particles[i].thruster_bitmap.first_frame);
	}
}


#define PAGE_OUT_TEXTURE(x) {	\
	if ( (x) >= 0 ) {	\
		if (release) {	\
			bm_release( (x) );	\
			(x) = -1;	\
		} else {	\
			bm_unload( (x) );	\
		}	\
	}	\
}

/**
 * Unload all textures for a given ship
 */
void ship_page_out_textures(int ship_index, bool release)
{
	int i;
	ship_info *sip;

	if ( (ship_index < 0) || (ship_index >= ship_info_size()) )
		return;


	sip = &Ship_info[ship_index];

	// afterburner
	PAGE_OUT_TEXTURE(sip->afterburner_trail.bitmap_id);

	// thruster bitmaps
	PAGE_OUT_TEXTURE(sip->thruster_flame_info.normal.first_frame);
	PAGE_OUT_TEXTURE(sip->thruster_flame_info.afterburn.first_frame);
	PAGE_OUT_TEXTURE(sip->thruster_glow_info.normal.first_frame);
	PAGE_OUT_TEXTURE(sip->thruster_glow_info.afterburn.first_frame);
	PAGE_OUT_TEXTURE(sip->thruster_secondary_glow_info.normal.bitmap_id);
	PAGE_OUT_TEXTURE(sip->thruster_secondary_glow_info.afterburn.bitmap_id);
	PAGE_OUT_TEXTURE(sip->thruster_tertiary_glow_info.normal.bitmap_id);
	PAGE_OUT_TEXTURE(sip->thruster_tertiary_glow_info.afterburn.bitmap_id);

	// slodeing bitmap
	PAGE_OUT_TEXTURE(sip->splodeing_texture);

	// thruster/particle bitmaps
	for (i = 0; i < (int)sip->normal_thruster_particles.size(); i++)
		PAGE_OUT_TEXTURE(sip->normal_thruster_particles[i].thruster_bitmap.first_frame);

	for (i = 0; i < (int)sip->afterburner_thruster_particles.size(); i++)
		PAGE_OUT_TEXTURE(sip->afterburner_thruster_particles[i].thruster_bitmap.first_frame);
}

void ship_replace_active_texture(int ship_index, const char* old_name, const char* new_name)
{
	polymodel_instance* pmi = model_get_instance(Ships[ship_index].model_instance_num);
	modelinstance_replace_active_texture(pmi, old_name, new_name);
}

// function to return true if support ships are allowed in the mission for the given object.
//	In single player, must be friendly and not Shivan. (Goober5000 - Shivans can now have support)
//	In multiplayer -- to be coded by Mark Allender after 5/4/98 -- MK, 5/4/98
int is_support_allowed(object *objp, bool do_simple_check)
{
	int result = -1;

	// check updated mission conditions to allow support

	// If running under autopilot support is not allowed
	if ( AutoPilotEngaged )
		return 0;

	// none allowed
	if (The_mission.support_ships.max_support_ships == 0)
		return 0;

	// ship_find_repair_ship is a little expensive, so let's not do it every frame
	if (!do_simple_check)
	{
		// check if all support ships are departing or dying
		result = ship_find_repair_ship(objp);
		if (result == 4) {
			return 0;
		}

		// restricted number allowed
		if (The_mission.support_ships.max_support_ships > 0)
		{
			// if all the allowed ships have been used up, can't rearm unless something's available in-mission or arriving
			if ((The_mission.support_ships.tally >= The_mission.support_ships.max_support_ships))
			{
				// this shouldn't happen because we've reached one of the limits
				Assert(result != 2);

				// nothing arriving and no ships available in mission
				if ((Arriving_support_ship == NULL) && (result == 0 || result == 3))
					return 0;
			}
		}
	}

	ship *shipp = &Ships[objp->instance];

	// this also looks a little more expensive
	if (!do_simple_check)
	{
		// make sure, if exiting from bay, that parent ship is in the mission!
		if ((result == 0 || result == 2) && (The_mission.support_ships.arrival_location == ArrivalLocation::FROM_DOCK_BAY))
		{
			Assert(The_mission.support_ships.arrival_anchor != -1);

			// ensure it's in-mission
			auto anchor_ship_entry = ship_registry_get(Parse_names[The_mission.support_ships.arrival_anchor]);
			if (!anchor_ship_entry || !anchor_ship_entry->has_shipp())
			{
				return 0;
			}

			// make sure it's not leaving or blowing up
			if (anchor_ship_entry->shipp()->is_dying_or_departing())
			{
				return 0;
			}

			// also make sure that parent ship's fighterbay hasn't been destroyed
			if (ship_fighterbays_all_destroyed(anchor_ship_entry->shipp()))
			{
				return 0;
			}
		}
	}

	if (Game_mode & GM_NORMAL)
	{
		if ( !(Iff_info[shipp->team].flags & IFFF_SUPPORT_ALLOWED) )
		{
			return 0;
		}
	}
	else
	{
		// multiplayer version behaves differently.  Depending on mode:
		// 1) coop mode -- only available to friendly
		// 2) team v team mode -- availble to either side
		// 3) dogfight -- never

		if(Netgame.type_flags & NG_TYPE_DOGFIGHT)
		{
			return 0;
		}

		if (IS_MISSION_MULTI_COOP)
		{
			if ( !(Iff_info[shipp->team].flags & IFFF_SUPPORT_ALLOWED) )
			{
				return 0;
			}
		}
	}

	// Goober5000 - extra check for existence of support ship
	if ( (The_mission.support_ships.ship_class < 0) &&
		!(The_mission.support_ships.support_available_for_species & (1 << Ship_info[shipp->ship_info_index].species)) )
	{
		return 0;
	}

	// this is also somewhat expensive
	if (!do_simple_check)
	{
		// Goober5000 - extra check to make sure this guy has a rearming dockpoint
		if (model_find_dock_index(Ship_info[shipp->ship_info_index].model_num, DOCK_TYPE_REARM) < 0)
		{
			static bool warned_about_rearm_dockpoint = false;
			if (!warned_about_rearm_dockpoint)
			{
				Warning(LOCATION, "Support not allowed for %s because its model lacks a rearming dockpoint!", shipp->ship_name);
				warned_about_rearm_dockpoint = true;
			}
			return 0;
		}
	}

	// Goober5000 - if we got this far, we can request support
	return 1;
}

// returns random index of a visible ship
// if no visible ships are generated in num_ships iterations, it returns -1
int ship_get_random_targetable_ship()
{
	int rand_ship;
	int idx = 0, target_list[MAX_SHIPS];
	ship_obj *so;

	for ( so = GET_FIRST(&Ship_obj_list); so != END_OF_LIST(&Ship_obj_list); so = GET_NEXT(so) ) {
		if (Objects[so->objnum].flags[Object::Object_Flags::Should_be_dead])
			continue;

		// make sure the instance is valid
		if ( (Objects[so->objnum].instance < 0) || (Objects[so->objnum].instance >= MAX_SHIPS) )
			continue;

		// skip if we aren't supposed to target it
		if (should_be_ignored(&Ships[Objects[so->objnum].instance]))
			continue;

		if (idx >= MAX_SHIPS) {
			idx = MAX_SHIPS;
			break;
		}

		target_list[idx] = Objects[so->objnum].instance;
		idx++;
	}

	if (idx == 0)
		return -1;

	rand_ship = Random::next(idx);

	return target_list[rand_ship];
}

/**
 * Forcibly jettison cargo from a ship
 */
void object_jettison_cargo(object *objp, object *cargo_objp, float jettison_speed, bool jettison_new)
{
	// make sure we are docked
	Assert((objp != nullptr) && (cargo_objp != nullptr));
	Assert(dock_check_find_direct_docked_object(objp, cargo_objp));

	vec3d impulse, pos;
	ship *shipp = &Ships[objp->instance];
	ship *cargo_shipp = &Ships[cargo_objp->instance];
	int docker_index = dock_find_dockpoint_used_by_object(objp, cargo_objp);
	int dockee_index = dock_find_dockpoint_used_by_object(cargo_objp, objp);

	// undo all the docking animations

	ship_info* sip = &Ship_info[shipp->ship_info_index];

	(sip->animations.getAll(model_get_instance(shipp->model_instance_num), animation::ModelAnimationTriggerType::Docked, docker_index)
		+ sip->animations.getAll(model_get_instance(shipp->model_instance_num), animation::ModelAnimationTriggerType::Docking_Stage3, docker_index)
		+ sip->animations.getAll(model_get_instance(shipp->model_instance_num), animation::ModelAnimationTriggerType::Docking_Stage2, docker_index)
		+ sip->animations.getAll(model_get_instance(shipp->model_instance_num), animation::ModelAnimationTriggerType::Docking_Stage1, docker_index)).start(animation::ModelAnimationDirection::RWD);

	ship_info* cargo_sip = &Ship_info[cargo_shipp->ship_info_index];

	(cargo_sip->animations.getAll(model_get_instance(cargo_shipp->model_instance_num), animation::ModelAnimationTriggerType::Docked, dockee_index)
		+ cargo_sip->animations.getAll(model_get_instance(cargo_shipp->model_instance_num), animation::ModelAnimationTriggerType::Docking_Stage3, dockee_index)
		+ cargo_sip->animations.getAll(model_get_instance(cargo_shipp->model_instance_num), animation::ModelAnimationTriggerType::Docking_Stage2, dockee_index)
		+ cargo_sip->animations.getAll(model_get_instance(cargo_shipp->model_instance_num), animation::ModelAnimationTriggerType::Docking_Stage1, dockee_index)).start(animation::ModelAnimationDirection::RWD);

	// undock the objects
	ai_do_objects_undocked_stuff(objp, cargo_objp);

	// Goober5000 - add log
	mission_log_add_entry(LOG_SHIP_UNDOCKED, shipp->ship_name, cargo_shipp->ship_name);

	// physics stuff
	if (jettison_new)
	{
		// we might not need to do any calculations
		if (whack_below_limit(jettison_speed * cargo_objp->phys_info.mass))
			return;

		// new method uses dockpoint normals and user-specified force
		extern void find_adjusted_dockpoint_info(vec3d *global_dock_point, matrix *dock_orient, object *objp, polymodel *pm, int submodel, int dock_index);
		extern int find_parent_moving_submodel(polymodel *pm, int dock_index);

		int model_num = Ship_info[shipp->ship_info_index].model_num;
		polymodel *pm = model_get(model_num);
		int docker_moving_submodel = find_parent_moving_submodel(pm, docker_index);
		matrix dock_orient;

		find_adjusted_dockpoint_info(&pos, &dock_orient, objp, pm, docker_moving_submodel, docker_index);

		// set for relative separation speed (see also do_dying_undock_physics)
		vm_vec_copy_scale(&impulse, &dock_orient.vec.fvec, jettison_speed * cargo_objp->phys_info.mass);
	}
	else
	{
		// the old method sends cargo in the wrong direction and with an impulse that depends on the size of the ship
		vm_vec_sub(&pos, &cargo_objp->pos, &objp->pos);
		impulse = pos;
		vm_vec_scale(&impulse, 100.0f);
		vm_vec_normalize(&pos);
		vm_vec_add2(&pos, &cargo_objp->pos);
	}

	// whack the ship
	ship_apply_whack(&impulse, &pos, cargo_objp);
}

float ship_get_exp_damage(object* objp)
{
	Assert(objp->type == OBJ_SHIP);
	float damage; 

	ship *shipp = &Ships[objp->instance];

	if (shipp->special_exp_damage >= 0) {
		damage = i2fl(shipp->special_exp_damage);
	} else {
		damage = Ship_info[shipp->ship_info_index].shockwave.damage;
	}

	return damage;
}

static int ship_get_exp_propagates(ship *sp)
{
	return Ship_info[sp->ship_info_index].explosion_propagates;
}

float ship_get_exp_outer_rad(object *ship_objp)
{
	float outer_rad;
	Assert(ship_objp->type == OBJ_SHIP);

	if (Ships[ship_objp->instance].special_exp_outer == -1) {
		outer_rad = Ship_info[Ships[ship_objp->instance].ship_info_index].shockwave.outer_rad;
	} else {
		outer_rad = (float)Ships[ship_objp->instance].special_exp_outer;
	}

	return outer_rad;
}

/**
 * Determine turret status of a given subsystem
 *
 * @return 0 for no turret, 1 for "fixed turret", 2 for "rotating" turret
 */
int ship_get_turret_type(ship_subsys *subsys)
{
	// not a turret at all
	if(subsys->system_info->type != SUBSYSTEM_TURRET){
		return 0;
	}

	// if it rotates
	if(subsys->system_info->turret_turning_rate > 0.0f){
		return 2;
	}

	// if its fixed
	return 1;
}

ship_subsys *ship_get_subsys(const ship *shipp, const char *subsys_name)
{
	// sanity checks
	if ((shipp == NULL) || (subsys_name == NULL)) {
		return NULL;
	}

	ship_subsys *ss = GET_FIRST(&shipp->subsys_list);
	while (ss != END_OF_LIST(&shipp->subsys_list)) {
		// check subsystem name
		if (!subsystem_stricmp(ss->system_info->subobj_name, subsys_name)) {
			return ss;
		}

		// next
		ss = GET_NEXT(ss);
	}

	// didn't find it
	return NULL;
}

int ship_get_num_subsys(const ship *shipp)
{
	Assert(shipp != NULL);

	return Ship_info[shipp->ship_info_index].n_subsystems;
}

// returns 0 if no conflict, 1 if conflict, -1 on some kind of error with wing struct
int wing_has_conflicting_teams(int wing_index)
{
	int first_team, idx;

	// sanity checks
	Assert((wing_index >= 0) && (wing_index < Num_wings) && (Wings[wing_index].wave_count > 0));
	if((wing_index < 0) || (wing_index >= Num_wings) || (Wings[wing_index].wave_count <= 0)){
		return -1;
	}

	// check teams
	Assert(Wings[wing_index].ship_index[0] >= 0);
	if(Wings[wing_index].ship_index[0] < 0){
		return -1;
	}
	first_team = Ships[Wings[wing_index].ship_index[0]].team;
	for(idx=1; idx<Wings[wing_index].wave_count; idx++){
		// more sanity checks
		Assert(Wings[wing_index].ship_index[idx] >= 0);
		if(Wings[wing_index].ship_index[idx] < 0){
			return -1;
		}

		// if we've got a team conflict
		if(first_team != Ships[Wings[wing_index].ship_index[idx]].team){
			return 1;
		}
	}

	// no conflict
	return 0;
}

/**
 * Get the team of a reinforcement item
 */
int ship_get_reinforcement_team(int r_index)
{
	int wing_index;
	p_object *p_objp;

	// sanity checks
	Assert((r_index >= 0) && (r_index < Num_reinforcements));
	if ((r_index < 0) || (r_index >= Num_reinforcements))
		return -1;

	// if the reinforcement is a ship	
	p_objp = mission_parse_get_arrival_ship(Reinforcements[r_index].name);
	if (p_objp != NULL)
		return p_objp->team;

	// if the reinforcement is a ship
	wing_index = wing_lookup(Reinforcements[r_index].name);
	if (wing_index >= 0)
	{		
		// go through the ship arrival list and find any ship in this wing
		for (p_objp = GET_FIRST(&Ship_arrival_list); p_objp != END_OF_LIST(&Ship_arrival_list); p_objp = GET_NEXT(p_objp))
		{
			// check by wingnum			
			if (p_objp->wingnum == wing_index)
				return p_objp->team;
		}
	}

	// no team ?
	return -1;
}

// update artillery lock info
#define CLEAR_ARTILLERY_AND_CONTINUE()	{ if(aip != NULL){ aip->artillery_objnum = -1; aip->artillery_sig = -1;	aip->artillery_lock_time = 0.0f;} continue; } 
float artillery_dist = 10.0f;
DCF(art, "Sets artillery disance")
{
	dc_stuff_float(&artillery_dist);
}

void ship_update_artillery_lock()
{
	ai_info *aip = NULL;
	mc_info *cinfo = NULL;
	int c_objnum;
	vec3d temp, local_hit;
	ship *shipp;
	ship_obj *so;

	// update all ships
	for ( so = GET_FIRST(&Ship_obj_list); so != END_OF_LIST(&Ship_obj_list); so = GET_NEXT(so) ){
		if (Objects[so->objnum].flags[Object::Object_Flags::Should_be_dead])
			continue;

		// get the ship
		if((so->objnum >= 0) && (Objects[so->objnum].type == OBJ_SHIP) && (Objects[so->objnum].instance >= 0)){
			shipp = &Ships[Objects[so->objnum].instance];
		} else {
			continue;
		}		

		// get ai info
		if(shipp->ai_index >= 0){
			aip = &Ai_info[shipp->ai_index];
		}

		// if the ship has no targeting laser firing
		if((shipp->targeting_laser_objnum < 0) || (shipp->targeting_laser_bank < 0)){
			CLEAR_ARTILLERY_AND_CONTINUE();
		}

		// if he didn't hit any objects this frame
		if(beam_get_num_collisions(shipp->targeting_laser_objnum) <= 0){
			CLEAR_ARTILLERY_AND_CONTINUE();
		}

		// get weapon info for the targeting laser he's firing
		Assert((shipp->weapons.current_primary_bank >= 0) && (shipp->weapons.current_primary_bank < 2));
		if((shipp->weapons.current_primary_bank < 0) || (shipp->weapons.current_primary_bank >= 2)){
			continue;
		}
		Assert(shipp->weapons.primary_bank_weapons[shipp->weapons.current_primary_bank] >= 0);
		if(shipp->weapons.primary_bank_weapons[shipp->weapons.current_primary_bank] < 0){
			continue;
		}
		Assert((Weapon_info[shipp->weapons.primary_bank_weapons[shipp->weapons.current_primary_bank]].wi_flags[Weapon::Info_Flags::Beam]) && (Weapon_info[shipp->weapons.primary_bank_weapons[shipp->weapons.current_primary_bank]].b_info.beam_type == BeamType::TARGETING));
		if(!(Weapon_info[shipp->weapons.primary_bank_weapons[shipp->weapons.current_primary_bank]].wi_flags[Weapon::Info_Flags::Beam]) || (Weapon_info[shipp->weapons.primary_bank_weapons[shipp->weapons.current_primary_bank]].b_info.beam_type != BeamType::TARGETING)){
			continue;
		}	

		// get collision info
		if(!beam_get_collision(shipp->targeting_laser_objnum, 0, &c_objnum, &cinfo)){
			CLEAR_ARTILLERY_AND_CONTINUE();
		}
		if((c_objnum < 0) || (cinfo == NULL)){
			CLEAR_ARTILLERY_AND_CONTINUE();
		}

		// get the position we hit this guy with in his local coords
		vm_vec_sub(&temp, &cinfo->hit_point_world, &Objects[c_objnum].pos);
		vm_vec_rotate(&local_hit, &temp, &Objects[c_objnum].orient);

		// if we are hitting a different guy now, reset the lock
		if((c_objnum != aip->artillery_objnum) || (Objects[c_objnum].signature != aip->artillery_sig)){
			aip->artillery_objnum = c_objnum;
			aip->artillery_sig = Objects[c_objnum].signature;
			aip->artillery_lock_time = 0.0f;
			aip->artillery_lock_pos = local_hit;

			// done
			continue;
		}	

		// otherwise we're hitting the same guy. check to see if we've strayed too far
		if(vm_vec_dist_quick(&local_hit, &aip->artillery_lock_pos) > artillery_dist){
			// hmmm. reset lock time, but don't reset the lock itself
			aip->artillery_lock_time = 0.0f;
			continue;
		}

		// finally - just increment the lock time
		aip->artillery_lock_time += flFrametime;

		// TEST CODE
		if(aip->artillery_objnum >= 0 && aip->artillery_lock_time >= 2.0f){
			ssm_create(&Objects[aip->artillery_objnum], &cinfo->hit_point_world, 0, NULL, shipp->team);				

			// reset the artillery			
			aip->artillery_lock_time = 0.0f;			
		}
	}
}

/**
 * Checks if a world point is inside the extended bounding box of a ship
 *
 * May not work if delta box is large and negative (ie, adjusted box crosses over on itself - min > max)
 */
int check_world_pt_in_expanded_ship_bbox(const vec3d *world_pt, const object *objp, float delta_box)
{
	Assert(objp->type == OBJ_SHIP);

	vec3d temp, ship_pt;
	polymodel *pm;
	vm_vec_sub(&temp, world_pt, &objp->pos);
	vm_vec_rotate(&ship_pt, &temp, &objp->orient);

	pm = model_get(Ship_info[Ships[objp->instance].ship_info_index].model_num);

	return (
			(ship_pt.xyz.x > pm->mins.xyz.x - delta_box) && (ship_pt.xyz.x < pm->maxs.xyz.x + delta_box)
		&& (ship_pt.xyz.y > pm->mins.xyz.y - delta_box) && (ship_pt.xyz.y < pm->maxs.xyz.y + delta_box)
		&& (ship_pt.xyz.z > pm->mins.xyz.z - delta_box) && (ship_pt.xyz.z < pm->maxs.xyz.z + delta_box)
	);
}


/**
 * Returns true when objp is ship and is tagged
 */
int ship_is_tagged(object *objp)
{
	ship *shipp;
	if (objp->type == OBJ_SHIP) {
		shipp = &Ships[objp->instance];
		if ( (shipp->tag_left > 0) || (shipp->level2_tag_left > 0) ) {
			return 1;
		}
	}

	return 0;
}

/**
 * Get maximum ship speed (when not warping in or out)
 */
float ship_get_max_speed(ship *shipp)
{
	float max_speed;
	ship_info *sip = &Ship_info[shipp->ship_info_index];

	// Goober5000 - maybe we're using cap-waypoint-speed
	ai_info *aip = &Ai_info[shipp->ai_index];
	if ((aip->mode == AIM_WAYPOINTS || aip->mode == AIM_FLY_TO_SHIP) && aip->waypoint_speed_cap > 0)
		return i2fl(aip->waypoint_speed_cap);

	// max overclock
	max_speed = sip->max_overclocked_speed;

	// normal max speed
	max_speed = MAX(max_speed, sip->max_vel.xyz.z);

	// afterburn if not locked
	if (!(shipp->flags[Ship_Flags::Afterburner_locked])) {
		max_speed = MAX(max_speed, sip->afterburner_max_vel.xyz.z);
	}

	return max_speed;
}

/**
 * Determine warpout speed of ship
 */
float ship_get_warpout_speed(object *objp, ship_info *sip, float half_length, float warping_dist)
{
	Assert(objp != nullptr && objp->type == OBJ_SHIP);

	// certain places in the code don't precalculate these variables
	if (sip == nullptr)
	{
		sip = &Ship_info[Ships[objp->instance].ship_info_index];

		// c.f.  WE_Default::warpStart()
		// determine the half-length and the warping distance (which is actually the full length)
		if (object_is_docked(objp))
		{
			// we need to get the longitudinal radius of our ship, so find the semilatus rectum along the Z-axis
			half_length = dock_calc_max_semilatus_rectum_parallel_to_axis(objp, Z_AXIS);
			warping_dist = 2.0f * half_length;
		}
		else
		{
			warping_dist = ship_class_get_length(sip);
			half_length = 0.5f * warping_dist;
		}
	}

	WarpParams *params = &Warp_params[Ships[objp->instance].warpout_params_index];

	//WMC - Any speed is good for in place anims (aka BSG FTL effect)
	//Asteroth - or for the sweeper (aka Homeworld FTL effect)
	if ((params->warp_type == WT_IN_PLACE_ANIM || params->warp_type == WT_SWEEPER) && params->speed <= 0.0f)
	{
		return objp->phys_info.speed;
	}
	else if (params->warp_type == WT_SWEEPER || params->warp_type == WT_IN_PLACE_ANIM)
	{
		return params->speed;
	}
	else if (params->warp_type == WT_HYPERSPACE)
	{
		if (objp->phys_info.speed > params->speed)
			return objp->phys_info.speed;
		else
			return params->speed;
	}

	return warping_dist / shipfx_calculate_warp_time(objp, WarpDirection::WARP_OUT, half_length, warping_dist);
}

/**
 * Returns true if ship is beginning to speed up in warpout
 */
int ship_is_beginning_warpout_speedup(object *objp)
{
	Assert(objp->type == OBJ_SHIP);

	ai_info *aip;

	aip = &Ai_info[Ships[objp->instance].ai_index];

	if (aip->mode == AIM_WARP_OUT) {
		if ( (aip->submode == AIS_WARP_3) || (aip->submode == AIS_WARP_4) || (aip->submode == AIS_WARP_5) ) {
			return 1;
		}
	}

	return 0;
}

/**
 * Return the length of a ship
 */
float ship_class_get_length(const ship_info *sip)
{
	Assert(sip != nullptr);
	Assert(sip->model_num >= 0);

	polymodel *pm = model_get(sip->model_num);
	Assert(pm != nullptr);

	float length = pm->maxs.xyz.z - pm->mins.xyz.z;
	Assert(length > 0.0f);

	return length;
}

/**
 * Get the offset of the actual center of the ship model for the purposes of warping (which may not be the specified center)
 */
void ship_class_get_actual_center(const ship_info *sip, vec3d *center_pos)
{
	Assert(sip != nullptr && center_pos != nullptr);
	Assert(sip->model_num >= 0);

	polymodel *pm = model_get(sip->model_num);
	center_pos->xyz.x = (pm->maxs.xyz.x + pm->mins.xyz.x) * 0.5f;
	center_pos->xyz.y = (pm->maxs.xyz.y + pm->mins.xyz.y) * 0.5f;
	center_pos->xyz.z = (pm->maxs.xyz.z + pm->mins.xyz.z) * 0.5f;
}

// Goober5000
void ship_set_new_ai_class(ship *shipp, int new_ai_class)
{
	Assert(shipp);
	Assert(new_ai_class >= 0);

	ai_info *aip = &Ai_info[shipp->ai_index];

	// we hafta change a bunch of stuff here...
	aip->ai_class = new_ai_class;
	init_aip_from_class_and_profile(aip, &Ai_classes[new_ai_class], The_mission.ai_profile);

	shipp->weapons.ai_class = new_ai_class;

	// I think that's everything!
}

// Goober5000
void ship_subsystem_set_new_ai_class(ship_subsys *ss, int new_ai_class)
{
	Assert(ss);
	Assert(new_ai_class >= 0);

	// set ai class
	ss->weapons.ai_class = new_ai_class;
}

// Goober5000 - will attempt to load an insignia bitmap and set it as active for the wing
// copied more or less from managepilot.cpp
void wing_load_squad_bitmap(wing *w)
{
	// sanity check
	if(w == NULL)
	{
		return;
	}

	// make sure one is not already set?!?
	Assert (w->wing_insignia_texture == -1);

	// try and set the new one
	if( w->wing_squad_filename[0] != '\0' )
	{
		// load duplicate because it might be the same as the player's squad,
		// and we don't want to overlap and breed nasty errors when we unload
		w->wing_insignia_texture = bm_load_duplicate(w->wing_squad_filename);
		
		// lock is as a transparent texture
		if(w->wing_insignia_texture != -1)
		{
			bm_lock(w->wing_insignia_texture, 16, BMP_TEX_XPARENT);
			bm_unlock(w->wing_insignia_texture);
		}
	}
}

// Goober5000 - needed by new hangar depart code
// check whether this ship has a docking bay
bool ship_has_dock_bay(int shipnum)
{
	Assert(shipnum >= 0 && shipnum < MAX_SHIPS);

	polymodel *pm;
				
	pm = model_get( Ship_info[Ships[shipnum].ship_info_index].model_num );
	Assert( pm );

	return ( pm->ship_bay && (pm->ship_bay->num_paths > 0) );
}

// Goober5000
bool ship_useful_for_departure(int shipnum, int  /*path_mask*/)
{
	Assert(shipnum < MAX_SHIPS);
	if (shipnum < 0 || shipnum >= MAX_SHIPS)
		return false;

	// not valid if dying or departing
	if (Ships[shipnum].is_dying_or_departing())
		return false;

	// no dockbay, can't depart to it
	if (!ship_has_dock_bay(shipnum))
		return false;

	// make sure that the bays are not all destroyed
	if (ship_fighterbays_all_destroyed(&Ships[shipnum]))
		return false;

	// in the future, we might want to check bay paths against destroyed fighterbays,
	// but that capability doesn't currently exist
	// (note, a mask of 0 indicates all paths are valid)
	//if (path_mask != 0)
	//{
	//}

	// ship is valid
	return true;
}

// Goober5000 - needed by new hangar depart code
// get first ship in ship list with docking bay
int ship_get_ship_for_departure(int team)
{
	for (ship_obj *so = GET_FIRST(&Ship_obj_list); so != END_OF_LIST(&Ship_obj_list); so = GET_NEXT(so))
	{
		Assert(so->objnum >= 0);
		if (Objects[so->objnum].flags[Object::Object_Flags::Should_be_dead])
			continue;

		int shipnum = Objects[so->objnum].instance;
		Assert(shipnum >= 0);

		if ( ship_useful_for_departure(shipnum) && (Ships[shipnum].team == team) )
			return shipnum;
	}

	// we didn't find anything
	return -1;
}

// Goober5000 - check if all fighterbays on a ship have been destroyed
bool ship_fighterbays_all_destroyed(ship *shipp)
{
	Assert(shipp);
	ship_subsys *subsys;
	int num_fighterbay_subsystems = 0;

	// check all fighterbay systems
	subsys = GET_FIRST(&shipp->subsys_list);
	while(subsys != END_OF_LIST(&shipp->subsys_list))
	{
		// look for fighterbays
		if (ship_subsys_is_fighterbay(subsys))
		{
			num_fighterbay_subsystems++;

			// if fighterbay doesn't take damage, we're good
			if (!ship_subsys_takes_damage(subsys))
				return false;

			// if fighterbay isn't destroyed, we're good
			if (subsys->current_hits > 0)
				return false;
		}

		// next item
		subsys = GET_NEXT(subsys);
	}

	// if the ship has no fighterbay subsystems at all, it must be an unusual case,
	// like the Faustus, so pretend it's okay...
	if (num_fighterbay_subsystems == 0)
		return false;

	// if we got this far, the ship has at least one fighterbay subsystem,
	// and all the ones it has are destroyed
	return true;
}

// moved here by Goober5000
static bool ship_subsys_is_fighterbay(ship_subsys* ss)
{
	Assert(ss);

	return !strnicmp(NOX("fighter"), ss->system_info->name, 7);
}

// Goober5000
bool ship_subsys_takes_damage(ship_subsys *ss)
{
	Assert(ss);

	return (ss->max_hits > SUBSYS_MAX_HITS_THRESHOLD);
}

// Goober5000
void ship_do_submodel_rotation(ship *shipp, model_subsystem *psub, ship_subsys *pss)
{
	Assert(shipp);
	Assert(psub);
	Assert(pss);

	// check if we actually can rotate
	if ( !(pss->flags[Ship::Subsystem_Flags::Rotates]) ){
		return;
	}

	if (psub->flags[Model::Subsystem_Flags::Triggered]) {
		//Triggered rotation is handled by animation stepping.
		//The flag doesn't do anything at all anymore, except prevent other rotation types
		return;
	}

	// check for rotating artillery
	if ( psub->flags[Model::Subsystem_Flags::Artillery] )
	{
		ship_weapon *swp = &shipp->weapons;

		// rotate only if trigger is down
		if ( !(shipp->flags[Ship_Flags::Trigger_down]) )
			return;

		// check linked
		if ( shipp->flags[Ship_Flags::Primary_linked] )
		{
			int i, ammo_tally = 0;

			// calculate ammo
			for (i=0; i<swp->num_primary_banks; i++)
				ammo_tally += swp->primary_bank_ammo[i];

			// do not rotate if out of ammo
			if (ammo_tally <= 0)
				return;
		}
		// check unlinked
		else
		{
			// do not rotate if this is not the firing bank or if we have no ammo in this bank
			if ((psub->weapon_rotation_pbank != swp->current_primary_bank) || (swp->primary_bank_ammo[swp->current_primary_bank] <= 0))
				return;
		}
	}

	// if we got this far, we can rotate - so choose which method to use
	if (psub->flags[Model::Subsystem_Flags::Stepped_rotate]	) {
		submodel_stepped_rotate(psub, pss->submodel_instance_1);
	} else {
		submodel_rotate(psub, pss->submodel_instance_1 );
	}
}

// Goober5000
void ship_do_submodel_translation(ship *shipp, model_subsystem *psub, ship_subsys *pss)
{
	Assert(shipp);
	Assert(psub);
	Assert(pss);

	// check if we actually can translate
	if ( !(pss->flags[Ship::Subsystem_Flags::Translates]) ){
		return;
	}

	if (psub->flags[Model::Subsystem_Flags::Triggered]) {
		//Triggered translation is handled by animation stepping.
		//The flag doesn't do anything at all anymore, except prevent other translation types
		return;
	}

	// if we got this far, we can translate - so choose which method to use
	if (psub->flags[Model::Subsystem_Flags::Stepped_translate]	) {
		submodel_stepped_translate(psub, pss->submodel_instance_1);
	} else {
		submodel_translate(psub, pss->submodel_instance_1 );
	}
}

void ship_move_subsystems(object *objp)
{
	Assertion(objp->type == OBJ_SHIP, "ship_move_subsystems should only be called for ships!  objp type = %d", objp->type);
	auto shipp = &Ships[objp->instance];
	
	// non-player ships that are playing dead do not process subsystems or turrets
	if ((!(objp->flags[Object::Object_Flags::Player_ship]) || Player_use_ai) && Ai_info[shipp->ai_index].mode == AIM_PLAY_DEAD)
		return;

	for (auto pss = GET_FIRST(&shipp->subsys_list); pss != END_OF_LIST(&shipp->subsys_list); pss = GET_NEXT(pss))
	{
		auto psub = pss->system_info;

		// Don't process destroyed objects (but allow subobjects with hitpoints disabled -nuke) (but also process subobjects that are allowed to rotate)
		if (pss->max_hits > 0 && pss->current_hits <= 0.0f && !(psub->flags[Model::Subsystem_Flags::Destroyed_rotation]))
			continue;

		// do solar/radar/gas/activator rotation here
		ship_do_submodel_rotation(shipp, psub, pss);
		ship_do_submodel_translation(shipp, psub, pss);
	}
}

// Goober5000
int ship_has_energy_weapons(ship *shipp)
{
	// (to avoid round-off errors, weapon reserve is not tested for zero)
	return (Ship_info[shipp->ship_info_index].max_weapon_reserve > WEAPON_RESERVE_THRESHOLD);
}

// Goober5000
int ship_has_engine_power(ship *shipp)
{
	return (Ship_info[shipp->ship_info_index].max_speed > 0 );
}

// Goober5000
int ship_starting_wing_lookup(const char *wing_name)
{
	Assertion(wing_name != nullptr, "NULL wing_name passed to ship_starting_wing_lookup");

	for (int i = 0; i < MAX_STARTING_WINGS; i++)
	{
		if (!stricmp(Starting_wing_names[i], wing_name))
			return i;
	}

	return -1;
}

// Goober5000
int ship_squadron_wing_lookup(const char *wing_name)
{
	Assertion(wing_name != nullptr, "NULL wing_name passed to ship_squadron_wing_lookup");

	// TvT uses a different set of wing names from everything else
	if (MULTI_TEAM)
	{
		for (int i = 0; i < MAX_TVT_WINGS; i++)
		{
			if (!stricmp(TVT_wing_names[i], wing_name))
				return i;
		}
	}
	else 
	{
		// match duplicate wings, such as Epsilon and Epsilon#clone
		auto ch = get_pointer_to_first_hash_symbol(wing_name);
		size_t len = (ch != nullptr) ? (ch - wing_name) : strlen(wing_name);

		for (int i = 0; i < MAX_SQUADRON_WINGS; i++)
		{
			if (!strnicmp(Squadron_wing_names[i], wing_name, std::max(len, strlen(Squadron_wing_names[i]))))
				return i;
		}
	}

	return -1;
}

// Goober5000
int ship_tvt_wing_lookup(const char *wing_name)
{
	Assertion(wing_name != nullptr, "NULL wing_name passed to ship_tvt_wing_lookup");

	for (int i = 0; i < MAX_TVT_WINGS; i++)
	{
		if (!stricmp(TVT_wing_names[i], wing_name))
			return i;
	}

	return -1;
}

// Goober5000
static int ship_class_get_priority(int ship_class)
{
	ship_info *sip = &Ship_info[ship_class];

	// biggest to smallest
	if (sip->flags[Info_Flags::Knossos_device])
		return 1;
	else if (sip->flags[Info_Flags::Supercap])
		return 2;
	else if (sip->flags[Info_Flags::Drydock])
		return 3;
	else if (sip->flags[Info_Flags::Capital])
		return 4;
	else if (sip->flags[Info_Flags::Corvette])
		return 5;
	else if (sip->flags[Info_Flags::Cruiser])
		return 6;
	else if (sip->flags[Info_Flags::Gas_miner])
		return 7;
	else if (sip->flags[Info_Flags::Awacs])
		return 8;
	else if (sip->flags[Info_Flags::Freighter])
		return 9;
	else if (sip->flags[Info_Flags::Transport])
		return 10;
	else if (sip->flags[Info_Flags::Bomber])
		return 11;
	else if (sip->flags[Info_Flags::Fighter] || sip->flags[Info_Flags::Stealth])
		return 12;
	else if (sip->flags[Info_Flags::Escapepod])
		return 13;
	else if (sip->flags[Info_Flags::Sentrygun])
		return 14;
	else if (sip->flags[Info_Flags::Cargo])
		return 15;
	else if (sip->flags[Info_Flags::Navbuoy])
		return 16;

	Warning(LOCATION, "Unknown priority for ship class '%s'!", sip->name);
	return 17 + ship_class;
}

// Goober5000
int ship_class_compare(int ship_class_1, int ship_class_2)
{
	// grab priorities
	int priority1 = ship_class_get_priority(ship_class_1);
	int priority2 = ship_class_get_priority(ship_class_2);

	// standard compare
	if (priority1 < priority2)
		return -1;
	else if (priority1 > priority2)
		return 1;
	else
		return 0;
}

/**
 * Gives the index into the Damage_types[] vector of a specified damage type name
 * @return -1 if not found
 */
static int damage_type_get_idx(const char *name)
{
	//This should never be bigger than INT_MAX anyway
	for(int i = 0; i < (int)Damage_types.size(); i++)
	{
		if(!stricmp(name, Damage_types[i].name))
			return i;
	}

	return -1;
}

/**
 * Either loads a new damage type, or returns the index of one with the same name as given
 */
int damage_type_add(const char *name)
{
	int i = damage_type_get_idx(name);
	if(i != -1)
		return i;

	DamageTypeStruct dts;

	strncpy(dts.name, name, NAME_LENGTH-1);

	if(strlen(name) > NAME_LENGTH - 1)
	{
		Warning(LOCATION, "Damage type name '%s' is too long and has been truncated to '%s'", name, dts.name);
	}

	Damage_types.push_back(dts);
	return (int)(Damage_types.size() - 1);
}

void ArmorDamageType::clear()
{
	DamageTypeIndex = -1;

	Calculations.clear();
	Arguments.clear();
	altArguments.clear();  // Nuke: don't forget to delete it
}

//************
// Wanderer - beam piercing type
//************

flag_def_list	PiercingTypes[] = {
	{	"none",		SADTF_PIERCING_NONE,		0},
	{	"default",	SADTF_PIERCING_DEFAULT,		0},
	{	"retail",	SADTF_PIERCING_RETAIL,		0},
};

const int Num_piercing_effect_types = sizeof(PiercingTypes)/sizeof(flag_def_list);

static int piercing_type_get(char *str)
{
	int i;
	for(i = 0; i < Num_piercing_effect_types; i++)
	{
		if(!stricmp(PiercingTypes[i].name, str))
			return PiercingTypes[i].def;
	}

	// default to retail
	return SADTF_PIERCING_RETAIL;
}

// Nuke: handle difficulty scaling type
flag_def_list	DifficultyScaleTypes[] = {
	{	"first",	ADT_DIFF_SCALE_FIRST,	0},
	{	"last",		ADT_DIFF_SCALE_LAST,	0},
	{	"manual",	ADT_DIFF_SCALE_MANUAL,	0},
};

const int Num_difficulty_scale_types = sizeof(DifficultyScaleTypes)/sizeof(flag_def_list);

static int difficulty_scale_type_get(char *str) {
	int i;
	for(i = 0; i < Num_difficulty_scale_types; i++){
		if (!stricmp(DifficultyScaleTypes[i].name, str))
			return DifficultyScaleTypes[i].def;
	}

	// indicate error
	return ADT_DIFF_SCALE_BAD_VAL;
}

// Nuke: flag list for +constant: values
flag_def_list	ArmorTypeConstants[] = {
	{	"base damage",			AT_CONSTANT_BASE_DMG,		0},
	{	"current damage",		AT_CONSTANT_CURRENT_DMG,	0},
	{	"difficulty factor",	AT_CONSTANT_DIFF_FACTOR,	0},
	{	"random",				AT_CONSTANT_RANDOM,			0},
	{	"pi",					AT_CONSTANT_PI,				0},
};

const int Num_armor_type_constants = sizeof(ArmorTypeConstants)/sizeof(flag_def_list);

static int armor_type_constants_get(char *str){
	int i;
	for (i = 0; i < Num_armor_type_constants; i++){
		if (!stricmp(ArmorTypeConstants[i].name, str))
			return ArmorTypeConstants[i].def;
	}
	// this shouldnt happen, but if it does theirs a define for that
	return AT_CONSTANT_BAD_VAL;
}


//**************************************************************
//WMC - All the extra armor crap

//****************************Calculation type addition
//4 steps to add a new one

//Armor types
//STEP 1: Add a define
#define AT_TYPE_ADDITIVE				0
#define AT_TYPE_MULTIPLICATIVE			1
#define AT_TYPE_EXPONENTIAL				2
#define AT_TYPE_EXPONENTIAL_BASE		3
#define AT_TYPE_CUTOFF					4
#define AT_TYPE_REVERSE_CUTOFF			5
#define AT_TYPE_INSTANT_CUTOFF			6
#define AT_TYPE_INSTANT_REVERSE_CUTOFF	7
// Added by Nuke
#define AT_TYPE_CAP						8
#define AT_TYPE_INSTANT_CAP				9
#define AT_TYPE_SET						10
#define AT_TYPE_STORE					11
#define AT_TYPE_LOAD					12
#define AT_TYPE_RANDOM					13

// Nuke: this is the number of storage locations load/store calculations are allowed to use
#define AT_NUM_STORAGE_LOCATIONS		8

//STEP 2: Add the name string to the array
const char *TypeNames[] = {
	"additive",
	"multiplicative",
	"exponential",
	"exponential base",
	"cutoff",
	"reverse cutoff",
	"instant cutoff",
	"instant reverse cutoff",
	// Added by Nuke
	"cap",
	"instant cap",
	"set",
	"load",
	"store",
	"random"
};

const int Num_armor_calculation_types = sizeof(TypeNames)/sizeof(char*);

int calculation_type_get(char *str)
{
	for(int i = 0; i < Num_armor_calculation_types; i++)
	{
		if(!stricmp(TypeNames[i], str))
			return i;
	}

	return -1;
}

//STEP 3: Add the calculation to the switch statement.
float ArmorType::GetDamage(float damage_applied, int in_damage_type_idx, float diff_dmg_scale, bool is_beam) {
	// Nuke: If the weapon has no damage type, just return damage
	if (in_damage_type_idx < 0) {
		// multiply by difficulty scaler now, since it is no longer done where this is called
		return (damage_applied * diff_dmg_scale);
	}

	//Initialize vars
	ArmorDamageType *adtp = NULL;

	//Find the entry in the weapon that corresponds to the given weapon damage type
	size_t num = DamageTypes.size();
	for(size_t i = 0; i < num; i++)
	{
		if(DamageTypes[i].DamageTypeIndex == in_damage_type_idx)
		{
			adtp = &DamageTypes[i];
			break;
		}
	}

	//curr_arg is the current calculation type value
	float curr_arg;

	//Make sure that we _have_ an armor entry for this damage type
	if(adtp != NULL)
	{
		//How many calculations do we have to do?
		num = adtp->Calculations.size();

		// Used for instant cutoffs/cap, to instantly end the loop
		bool end_now = false;
		// used for load/store operations
		float storage[AT_NUM_STORAGE_LOCATIONS];
		int storage_idx;
		bool using_storage = false;
		// constant related stuff
		float constant_val;
		float base_damage;
		bool using_constant = false;

		// set storage locations to zero
		for (size_t i = 0; i < AT_NUM_STORAGE_LOCATIONS; i++) {
			storage[i]=0.0f;
		}

		// check to see if we need to difficulty scale damage first
		if (adtp->difficulty_scale_type == ADT_DIFF_SCALE_FIRST) {
			damage_applied *= diff_dmg_scale;
		}

		// user may want to use base damage as a constant
		base_damage = damage_applied;
		// LOOP!
		for (size_t i = 0; i < num; i++) {
			storage_idx = adtp->altArguments[i];
			//Set curr_arg
			// use storage index at +Stored Value:
			if ( (storage_idx >= 0) && (storage_idx < AT_NUM_STORAGE_LOCATIONS) ) {
				curr_arg = storage[storage_idx];
				using_storage = true;
			// using +value: (or error cases caught at parse, where this holda a 0.0f)
			} else if (storage_idx == AT_CONSTANT_NOT_USED) { // save time checking all possible constants when most of the time you will be using +value:
				curr_arg = adtp->Arguments[i];
			// maybe handle constants
			} else if (storage_idx == AT_CONSTANT_BASE_DMG) {
				curr_arg = base_damage;
				using_constant = true;
			} else if (storage_idx == AT_CONSTANT_CURRENT_DMG) {
				curr_arg = damage_applied;
				using_constant = true;
			} else if (storage_idx == AT_CONSTANT_DIFF_FACTOR) {
				curr_arg = diff_dmg_scale;
				using_constant = true;
			} else if (storage_idx == AT_CONSTANT_RANDOM) {
				constant_val = frand();
				curr_arg = constant_val;
				using_constant = true;
			} else if (storage_idx == AT_CONSTANT_PI) {
				constant_val = PI;
				curr_arg = constant_val;
				using_constant = true;
			} else { // fail
				constant_val = 0.0f;
				curr_arg = constant_val;
			}

			//face: terrible hack to work consistently with beams and additive damages
			if (is_beam && ( adtp->Calculations[i] == AT_TYPE_ADDITIVE
				|| adtp->Calculations[i] == AT_TYPE_CUTOFF
				|| adtp->Calculations[i] == AT_TYPE_REVERSE_CUTOFF
				|| adtp->Calculations[i] == AT_TYPE_INSTANT_CUTOFF
				|| adtp->Calculations[i] == AT_TYPE_INSTANT_REVERSE_CUTOFF
				|| adtp->Calculations[i] == AT_TYPE_CAP
				|| adtp->Calculations[i] == AT_TYPE_INSTANT_CAP
				|| adtp->Calculations[i] == AT_TYPE_SET
				|| adtp->Calculations[i] == AT_TYPE_RANDOM)) {
				curr_arg = curr_arg * (flFrametime * 1000.0f) / i2fl(BEAM_DAMAGE_TIME);
			}

			// new calcs go here
			switch(adtp->Calculations[i])
			{
				case AT_TYPE_ADDITIVE:
					damage_applied += curr_arg;
					break;
				case AT_TYPE_MULTIPLICATIVE:
					damage_applied *= curr_arg;
					break;
				case AT_TYPE_EXPONENTIAL:
					damage_applied = powf(damage_applied, curr_arg);
					break;
				case AT_TYPE_EXPONENTIAL_BASE:
					damage_applied = powf(curr_arg, damage_applied);
					break;
				case AT_TYPE_CUTOFF:
					if(damage_applied < curr_arg)
						damage_applied = 0;
					break;
				case AT_TYPE_REVERSE_CUTOFF:
					if(damage_applied > curr_arg)
						damage_applied = 0;
					break;
				case AT_TYPE_INSTANT_CUTOFF:
					if(damage_applied < curr_arg)
					{
						damage_applied = 0;
						end_now = true;
					}
					break;
				case AT_TYPE_INSTANT_REVERSE_CUTOFF:
					if(damage_applied > curr_arg)
					{
						damage_applied = 0;
						end_now = true;
					}
					break;
				case AT_TYPE_CAP:
					if (damage_applied > curr_arg)
						damage_applied = curr_arg;
					break;
				case AT_TYPE_INSTANT_CAP:
					if (damage_applied > curr_arg) {
						damage_applied = curr_arg;
						end_now = true;
					}
					break;
				case AT_TYPE_SET:
					damage_applied = curr_arg;
					break;
				case AT_TYPE_STORE:
					if (using_storage || using_constant) {
						Warning(LOCATION, "Cannot use +Stored Value: or +Constant: with +Store:, that would be bad. Skipping calculation.");
					} else {
						storage_idx =  int(floorf(curr_arg));
						// Nuke: idiotproof this, no segfault 4 u
						if ( (storage_idx < 0) || (storage_idx >= AT_NUM_STORAGE_LOCATIONS) ) {
							Warning(LOCATION, "+Value: for +Store: calculation out of range. Should be between 0 and %i. Read: %i, Skipping calculation.", AT_NUM_STORAGE_LOCATIONS, storage_idx);
							storage_idx = 0;
						} else {
							storage[storage_idx] = damage_applied;
						}
					}
					break;
				case AT_TYPE_LOAD:
					if (using_storage || using_constant) {
						Warning(LOCATION, "Cannot use +Stored Value: or +Constant: with +Load:, that would be bad. Skipping calculation.");
					} else {
						storage_idx =  int(floorf(curr_arg));
						// Nuke: idiotproof this, no segfault 4 u
						if ( (storage_idx < 0) || (storage_idx >= AT_NUM_STORAGE_LOCATIONS) ) {
							Warning(LOCATION, "+Value: for +Load: calculation out of range. Should be between 0 and %i. Read: %i, Skipping calculation.", AT_NUM_STORAGE_LOCATIONS, storage_idx);
							storage_idx = 0;
						} else {
							damage_applied = storage[storage_idx];
						}
					}
					break;
				case AT_TYPE_RANDOM:  // Nuke: get a random number between damage_applied and +value:
					if (damage_applied > curr_arg) {
						damage_applied = frand_range( curr_arg, damage_applied );
					} else {
						damage_applied = frand_range( damage_applied, curr_arg );
					}
				break;
			}
			
			if(end_now)
				break;
		}
		// Nuke: check to see if we need to difficulty scale damage last
		if (adtp->difficulty_scale_type == ADT_DIFF_SCALE_LAST)
			damage_applied *= diff_dmg_scale;

		// Face: negative damages should not heal you!!!
		if (damage_applied < 0.0f)
			damage_applied = 0.0f;

		return damage_applied;
	}
	// fail return is fail
	return (damage_applied * diff_dmg_scale);
}

float ArmorType::GetShieldPiercePCT(int damage_type_idx)
{
	if(damage_type_idx < 0)
		return 0.0f;

	//Initialize vars
	ArmorDamageType *adtp = NULL;

	//Find the entry in the weapon that corresponds to the given weapon damage type
	size_t num = DamageTypes.size();
	for(size_t i = 0; i < num; i++)
	{
		if(DamageTypes[i].DamageTypeIndex == damage_type_idx)
		{
			adtp = &DamageTypes[i];
			break;
		}
	}
	if(adtp != NULL){
		return adtp->shieldpierce_pct;
	}

	return 0.0f;
}

int ArmorType::GetPiercingType(int damage_type_idx)
{
	if(damage_type_idx < 0)
		return 0;

	//Initialize vars
	ArmorDamageType *adtp = NULL;

	//Find the entry in the weapon that corresponds to the given weapon damage type
	size_t num = DamageTypes.size();
	for(size_t i = 0; i < num; i++)
	{
		if(DamageTypes[i].DamageTypeIndex == damage_type_idx)
		{
			adtp = &DamageTypes[i];
			break;
		}
	}
	if(adtp != NULL){
		return adtp->piercing_type;
	}

	return 0;
}

float ArmorType::GetPiercingLimit(int damage_type_idx)
{
	if(damage_type_idx < 0)
		return 0.0f;

	//Initialize vars
	ArmorDamageType *adtp = NULL;

	//Find the entry in the weapon that corresponds to the given weapon damage type
	size_t num = DamageTypes.size();
	for(size_t i = 0; i < num; i++)
	{
		if(DamageTypes[i].DamageTypeIndex == damage_type_idx)
		{
			adtp = &DamageTypes[i];
			break;
		}
	}
	if(adtp != NULL){
		return adtp->piercing_start_pct;
	}

	return 0.0f;
}

//***********************************Member functions

ArmorType::ArmorType(const char* in_name)
{
	auto len = strlen(in_name);
	if(len >= NAME_LENGTH) {
		Warning(LOCATION, "Armor name %s is " SIZE_T_ARG " characters too long, and will be truncated", in_name, len - NAME_LENGTH);
	}
	
	strncpy(Name, in_name, NAME_LENGTH-1);
}

void ArmorType::ParseData()
{
	ArmorDamageType adt;
	char buf[NAME_LENGTH];
	float temp_float;
	int temp_int;
	int calc_type = -1;

	//Get the damage types
	required_string("$Damage Type:");
	do
	{
		//Get damage type name
		stuff_string(buf, F_NAME, NAME_LENGTH);
		
		//Clear the struct and set the index
		adt.clear();
		adt.DamageTypeIndex = damage_type_add(buf);
		bool no_content = true;

		//Get calculation and argument
		while (optional_string("+Calculation:")) 
		{
			//+Calculation
			stuff_string(buf, F_NAME, NAME_LENGTH);

			calc_type = calculation_type_get(buf);

			//Make sure we have a valid calculation type
			if(calc_type == -1)
			{
				Warning(LOCATION, "Armor '%s': Armor calculation type '%s' is invalid, and has been skipped", Name, buf);
				// Nuke: guess we need to add this here too
				if (optional_string("+Stored Value:")) {
					stuff_int(&temp_int);
				} else if (optional_string("+Constant:")) {
					stuff_string(buf, F_NAME, NAME_LENGTH);
				} else {
					required_string("+Value:");
					stuff_float(&temp_float);
				}
			}
			else
			{
				adt.Calculations.push_back(calc_type);
				// Nuke: maybe were using a stored location
				if (optional_string("+Stored Value:")) {
					stuff_int(&temp_int);
					// Nuke: idiot-proof
					if ( (temp_int < 0) || (temp_int >= AT_NUM_STORAGE_LOCATIONS) ) {
						Error(LOCATION, "+Stored Value: is out of range. Should be between 0 and %i. Read: %i, Using value 0.", AT_NUM_STORAGE_LOCATIONS-1, temp_int);
						temp_int = AT_CONSTANT_NOT_USED;
					}
					adt.altArguments.push_back(temp_int);
					adt.Arguments.push_back(0.0f); // this isnt used in this case, just take up space so the indices lign up, also a fallback value in case of bad altArguments
				} else if (optional_string("+Constant:")) { // use one of the pre-defined constants
					stuff_string(buf, F_NAME, NAME_LENGTH);
					temp_int = armor_type_constants_get(buf);
					// Nuke: idiot proof some more
					if (temp_int == AT_CONSTANT_BAD_VAL) {
						Error(LOCATION, "Invalid +Constant: name, '%s'. Using value 0.", buf);
						temp_int = AT_CONSTANT_NOT_USED;
					}
					adt.altArguments.push_back(temp_int);
					adt.Arguments.push_back(0.0f); // this isnt used in this case, just take up space so the indices lign up, also a fallback value in case of bad altArguments
				} else { // Nuke: +Value, only required if storage location or constant is not used -nuke
					required_string("+Value:");
					stuff_float(&temp_float);
					adt.altArguments.push_back(AT_CONSTANT_NOT_USED); // set this to AT_CONSTANT_NOT_USED so we know to just use the value from adt.Arguments instead of constants/storage locations
					adt.Arguments.push_back(temp_float);
				}
				no_content = false;
			}
		}

		adt.shieldpierce_pct = 0.0f;

		if(optional_string("+Shield Piercing Percentage:")) {
			stuff_float(&temp_float);
			CLAMP(temp_float, 0.0f, 1.0f);
			adt.shieldpierce_pct = temp_float;
			no_content = false;
		}

		adt.piercing_start_pct = 0.1f;
		adt.piercing_type = -1;

		if(optional_string("+Weapon Piercing Effect Start Limit:")) {
			stuff_float(&temp_float);
			CLAMP(temp_float, 0.0f, 100.0f); 
			temp_float /= 100.0f;
			adt.piercing_start_pct = temp_float;
			no_content = false;
		}

		if(optional_string("+Weapon Piercing Type:")) {
			stuff_string(buf, F_NAME, NAME_LENGTH);
			adt.piercing_type = piercing_type_get(buf);
			no_content = false;
		}

		// Nuke: don't forget to init things
		adt.difficulty_scale_type = ADT_DIFF_SCALE_FIRST;

		if (optional_string("+Difficulty Scale Type:")) {
			stuff_string(buf, F_NAME, NAME_LENGTH);
			temp_int = difficulty_scale_type_get(buf);
			if (temp_int == ADT_DIFF_SCALE_BAD_VAL) {
				Error(LOCATION, "Invalid +Difficulty Scale Type: name: '%s'. Reverting to default behavior.", buf);
				adt.difficulty_scale_type = ADT_DIFF_SCALE_FIRST;
			} else {
				adt.difficulty_scale_type = temp_int;
			}
			no_content = false;
		}

		//If we have calculations in this damage type, add it
		if(!no_content)
		{
			if(adt.Calculations.size() != adt.Arguments.size())
			{
				Warning(LOCATION, "Armor '%s', damage type " SIZE_T_ARG ": Armor has a different number of calculation types than arguments (" SIZE_T_ARG ", " SIZE_T_ARG ")",
						Name, DamageTypes.size(), adt.Calculations.size(), adt.Arguments.size());
			}
			DamageTypes.push_back(adt);
		}
	} while(optional_string("$Damage Type:"));
}

//********************************Global functions

int armor_type_get_idx(const char* name)
{
	auto num = Armor_types.size();
	for(size_t i = 0; i < num; i++)
	{
		if(Armor_types[i].IsName(name))
			return (int)i;
	}
	
	//Didn't find anything.
	return -1;
}

void parse_armor_type()
{
	char name_buf[NAME_LENGTH];
	ArmorType tat("");
	
	required_string("$Name:");
	stuff_string(name_buf, F_NAME, NAME_LENGTH);
	
	tat = ArmorType(name_buf);
	
	//now parse the actual table (damage type/armor type pairs)
	tat.ParseData();

	//rest of the parse data
	if (optional_string("$Flags:"))
		parse_string_flag_list(tat.flags, Armor_flags, Num_armor_flags);
	
	//Add it to global armor types
	Armor_types.push_back(tat);
}

void armor_parse_table(const char *filename)
{
	try
	{
		read_file_text(filename, CF_TYPE_TABLES);
		reset_parse();

		//Enumerate through all the armor types and add them.
		while (optional_string("#Armor Type")) {
			while (required_string_either("#End", "$Name:")) {
				parse_armor_type();
				continue;
			}

			required_string("#End");
		}
	}
	catch (const parse::ParseException& e)
	{
		mprintf(("TABLES: Unable to parse '%s'!  Error message = %s.\n", filename, e.what()));
		return;
	}
}

void armor_init()
{
	if (!Armor_inited) {
		armor_parse_table("armor.tbl");

		parse_modular_table(NOX("*-amr.tbm"), armor_parse_table);

		Armor_inited = true;
	}
}

//**************************************************************
// AI targeting priority functions
//**************************************************************
void parse_ai_target_priorities()
{
	int i, j;
	int n_entries = (int)Ai_tp_list.size();
	SCP_vector <SCP_string> temp_strings;

	bool first_time = false;
	int already_exists = -1;

	if (n_entries == 0)
		first_time = true;

	required_string("$Name:");
	ai_target_priority temp_priority = init_ai_target_priorities();

	stuff_string(temp_priority.name, F_NAME, NAME_LENGTH);
	if (first_time == false) {
		for (i = 0; i < n_entries; i++) {
			if (!strnicmp(temp_priority.name, Ai_tp_list[i].name, NAME_LENGTH)) {
				already_exists = i;
			}
		}
	}

	if (optional_string("+Object Type:") ) {
		char tempname[NAME_LENGTH];
		stuff_string(tempname, F_NAME, NAME_LENGTH);

		for (j = 0; j < num_ai_tgt_objects; j++) {
			if ( !stricmp(ai_tgt_objects[j].name, tempname) ) {
				temp_priority.obj_type = ai_tgt_objects[j].def;
			}
		}
	}

	if (optional_string("+Weapon Class:") ) {
		temp_strings.clear();
		stuff_string_list(temp_strings);

		for(i = 0; i < (int)temp_strings.size(); i++) {
			for(j = 0; j < weapon_info_size(); ++j) {
				if ( !stricmp(Weapon_info[j].name, temp_strings[i].c_str()) ) {
					temp_priority.weapon_class.push_back(j);
					break;
				}
			}
			if (j == weapon_info_size()) {
				Warning(LOCATION, "Unidentified weapon class '%s' set for target priority group '%s'\n", temp_strings[i].c_str(), temp_priority.name);
			}
		}
	}

	if (optional_string("+Object Flags:") ) {
		temp_strings.clear();
		stuff_string_list(temp_strings);

		for (i = 0; i < (int)temp_strings.size(); i++) {
			for (j = 0; j < num_ai_tgt_obj_flags; j++) {
				if ( !stricmp(ai_tgt_obj_flags[j].name, temp_strings[i].c_str()) ) {
					temp_priority.obj_flags |= ai_tgt_obj_flags[j].def;
					break;
				}
			}
			if (j == num_ai_tgt_obj_flags) {
				Warning(LOCATION, "Unidentified object flag '%s' set for target priority group '%s'\n", temp_strings[i].c_str(), temp_priority.name);
			}
		}
	}

	if (optional_string("+Ship Class Flags:")) {
		temp_strings.clear();
		stuff_string_list(temp_strings);

		for (i = 0; i < (int)temp_strings.size(); i++) {
			for (j = 0; j < num_ai_tgt_ship_flags; j++) {
				if (!stricmp(ai_tgt_ship_flags[j].name, temp_strings[i].c_str())) {
					temp_priority.sif_flags.set(ai_tgt_ship_flags[j].def);
					break;
				}
			}
			if (j == num_ai_tgt_ship_flags) {
				Warning(LOCATION, "Unidentified ship class flag '%s' set for target priority group '%s'\n", temp_strings[i].c_str(), temp_priority.name);
			}
		}
	}

	if (optional_string("+Weapon Class Flags:") ) {
		temp_strings.clear();
		stuff_string_list(temp_strings);

		for (i = 0; i < (int)temp_strings.size(); i++) {
			for (j = 0; j < num_ai_tgt_weapon_info_flags; j++) {
				if ( !stricmp(ai_tgt_weapon_flags[j].name, temp_strings[i].c_str()) ) {
					temp_priority.wif_flags |= ai_tgt_weapon_flags[j].def;
					break;
				}
			}
			if (j == num_ai_tgt_weapon_info_flags) {
				if (!stricmp(temp_strings[i].c_str(), "Cycle")) {
					mprintf(("Weapon flag \"Cycle\" is deprecated for target priorities in ships.tbl.\n"));
				} else {
					Warning(LOCATION, "Unidentified weapon class flag '%s' set for target priority group '%s'\n", temp_strings[i].c_str(), temp_priority.name);
				}
			}
		}
	}

	temp_strings.clear();

	if (already_exists == -1) {
		Ai_tp_list.push_back(std::move(temp_priority));
	} else {
		Ai_tp_list[already_exists] = std::move(temp_priority);
	}
}

ai_target_priority init_ai_target_priorities()
{
	ai_target_priority temp_priority;

	//initialize the entries
	temp_priority.obj_flags.reset();
	temp_priority.obj_type = -1;
	temp_priority.ship_class.clear();
	temp_priority.ship_type.clear();
	temp_priority.sif_flags.reset();
	temp_priority.weapon_class.clear();
	temp_priority.wif_flags.reset();
	temp_priority.name[0] = '\0';

	//return the initialized
	return temp_priority;
}

void parse_weapon_targeting_priorities()
{
	char tempname[NAME_LENGTH];

	if (optional_string("$Name:")) {
		stuff_string(tempname, F_NAME, NAME_LENGTH);
		
		int k = weapon_info_lookup(tempname);
		if (k < 0) {
			error_display(0, "Unrecognized weapon '%s' found when setting weapon targeting priorities.\n", tempname);
			if (optional_string("+Target Priority:")) {		// consume the data to avoid parsing errors
				SCP_vector<SCP_string> dummy;
				stuff_string_list(dummy);
			}
		} else {
			// reset the list

			weapon_info *wip = &Weapon_info[k];
			
			wip->num_targeting_priorities = 0;

			if (optional_string("+Target Priority:")) {
				SCP_vector <SCP_string> tgt_priorities;
				stuff_string_list(tgt_priorities);

				if (tgt_priorities.size() > 32)
					tgt_priorities.resize(32);

				auto num_groups = Ai_tp_list.size();

				for(size_t i = 0; i < tgt_priorities.size(); i++) {
					size_t j;
					for(j = 0; j < num_groups; j++) {
						if ( !stricmp(Ai_tp_list[j].name, tgt_priorities[i].c_str()))  {
							wip->targeting_priorities[i] = (int)j;
							wip->num_targeting_priorities++;
							break;
						}
					}
					if(j == num_groups)
						error_display(0, "Unrecognized string '%s' found when setting weapon targeting priorities.\n", tgt_priorities[i].c_str());
				}
			}
		}
	}
}

/* Currently unused
static int ship_get_subobj_model_num(ship_info* sip, char* subobj_name) 
{
	for (int i = 0; i < sip->n_subsystems; i++) {
		if (!subsystem_stricmp(sip->subsystems[i].subobj_name, subobj_name))
			return sip->subsystems[i].subobj_num;
	}

	return -1;
}*/

void init_path_metadata(path_metadata& metadata)
{
	vm_vec_zero(&metadata.departure_rvec);
	vm_vec_zero(&metadata.arrival_rvec);
	metadata.arrive_speed_mult = FLT_MIN;
	metadata.depart_speed_mult = FLT_MIN;
}

gamesnd_id ship_get_sound(object *objp, GameSounds id)
{
	Assert( objp != nullptr);
	Assert( gamesnd_game_sound_valid(gamesnd_id(id)) );

	// It's possible that this gets called when an object (in most cases the player) is dead or an observer
	if (objp->type == OBJ_OBSERVER || objp->type == OBJ_GHOST)
		return gamesnd_id(id);

	Assertion(objp->type == OBJ_SHIP, "Expected a ship, got '%s'.", Object_type_names[objp->type]);

	ship *shipp = &Ships[objp->instance];
	ship_info *sip = &Ship_info[shipp->ship_info_index];

	SCP_map<GameSounds, gamesnd_id>::iterator element = sip->ship_sounds.find(id);

	if (element == sip->ship_sounds.end())
		return gamesnd_id(id);
	else
		return (*element).second;
}

bool ship_has_sound(object *objp, GameSounds id)
{
	Assert( objp != nullptr );
	Assert( gamesnd_game_sound_valid(id) );

	// It's possible that this gets called when an object (in most cases the player) is dead or an observer
	if (objp->type == OBJ_OBSERVER || objp->type == OBJ_GHOST)
		return false;

	Assertion(objp->type == OBJ_SHIP, "Expected a ship, got '%s'.", Object_type_names[objp->type]);

	ship *shipp = &Ships[objp->instance];
	ship_info *sip = &Ship_info[shipp->ship_info_index];

	auto element = sip->ship_sounds.find(id);

	if (element == sip->ship_sounds.end())
		return false;
	else
		return true;
}

/**
 * Given a ship with bounding box and a point, find the closest point on the bbox
 *
 * @param ship_objp Object that has the bounding box (should be a ship)
 * @param start World position of the point being compared
 * @param box_pt OUTPUT PARAMETER: closest point on the bbox to start
 *
 * @return point is inside bbox, TRUE/1
 * @return point is outside bbox, FALSE/0
 */
int get_nearest_bbox_point(const object *ship_objp, const vec3d *start, vec3d *box_pt)
{
	vec3d temp, rf_start;
	polymodel *pm;
	pm = model_get(Ship_info[Ships[ship_objp->instance].ship_info_index].model_num);

	// get start in ship rf
	vm_vec_sub(&temp, start, &ship_objp->pos);
	vm_vec_rotate(&rf_start, &temp, &ship_objp->orient);

	// find box_pt
	int inside = project_point_onto_bbox(&pm->mins, &pm->maxs, &rf_start, &temp);

	// get box_pt in world rf
	vm_vec_unrotate(box_pt, &temp, &ship_objp->orient);
	vm_vec_add2(box_pt, &ship_objp->pos);

	return inside;
}

void ship_set_thruster_info(mst_info *mst, object *obj, ship *shipp, ship_info *sip)
{
	mst->length = obj->phys_info.linear_thrust;

	//	Maybe add noise to thruster geometry.
	if (!(sip->flags[Ship::Info_Flags::No_thruster_geo_noise])) {
		mst->length.xyz.z *= (1.0f + frand()/5.0f - 0.1f);
		mst->length.xyz.y *= (1.0f + frand()/5.0f - 0.1f);
		mst->length.xyz.x *= (1.0f + frand()/5.0f - 0.1f);
	}

	CLAMP(mst->length.xyz.z, -1.0f, 1.0f);
	CLAMP(mst->length.xyz.y, -1.0f, 1.0f);
	CLAMP(mst->length.xyz.x, -1.0f, 1.0f);

	mst->primary_bitmap = shipp->thruster_bitmap;
	mst->primary_glow_bitmap = shipp->thruster_glow_bitmap;
	mst->secondary_glow_bitmap = shipp->thruster_secondary_glow_bitmap;
	mst->tertiary_glow_bitmap = shipp->thruster_tertiary_glow_bitmap;
	mst->distortion_bitmap = shipp->thruster_distortion_bitmap;

	mst->use_ab = (obj->phys_info.flags & PF_AFTERBURNER_ON) || (obj->phys_info.flags & PF_BOOSTER_ON);
	mst->glow_noise = shipp->thruster_glow_noise * sip->thruster_glow_noise_mult;
	mst->rotvel = Objects[shipp->objnum].phys_info.rotvel;

	mst->glow_rad_factor = sip->thruster01_glow_rad_factor;
	mst->secondary_glow_rad_factor = sip->thruster02_glow_rad_factor;
	mst->tertiary_glow_rad_factor = sip->thruster03_glow_rad_factor;
	mst->glow_length_factor = sip->thruster02_glow_len_factor;
	mst->distortion_length_factor = sip->thruster_dist_len_factor;
	mst->distortion_rad_factor = sip->thruster_dist_rad_factor;

	mst->draw_distortion = sip->draw_distortion;
}

void ship_render_batch_thrusters(object *obj)
{
	int num = obj->instance;
	ship *shipp = &Ships[num];
	ship_info *sip = &Ship_info[Ships[num].ship_info_index];

	if ( Rendering_to_shadow_map ) return;

	for (size_t i = 0; i < shipp->rcs_activity.size(); i++)
	{
		const auto mtp = &sip->rcs_thrusters[i];
		const auto& activity = shipp->rcs_activity[i];

		TIMESTAMP start_time = std::get<0>(activity);
		float render_amount = std::get<2>(activity);

		if (!start_time.isValid())
			continue;

		//Don't render small faraway thrusters (more than 1k * radius * length away)
		if ( vm_vec_dist(&Eye_position, &obj->pos) > (1000.0f * mtp->radius * mtp->length) ) {
			render_amount = 0.0f;
		}

		if ( render_amount > 0.0f ) {

			//Draw graphics
			//Skip invalid ones
			if ( mtp->tex_id >= 0 ) {
				float rad = mtp->radius;
				if(rad <= 0.0f)
					rad = 1.0f;

				float len = mtp->length;
				if(len == 0.0f)
					len = rad;

				vec3d start, tmpend, end;
				//Start
				vm_vec_unrotate(&start, &mtp->pos, &obj->orient);
				vm_vec_add2(&start, &obj->pos);

				//End
				vm_vec_scale_add(&tmpend, &mtp->pos, &mtp->norm, len * render_amount);
				vm_vec_unrotate(&end, &tmpend, &obj->orient);
				vm_vec_add2(&end, &obj->pos);

				int bmap_frame = mtp->tex_id;
				if(mtp->tex_nframes > 0)
					bmap_frame += bm_get_anim_frame(mtp->tex_id, i2fl(timestamp_since(start_time)) / MILLISECONDS_PER_SECOND, 0.0f, true);

				//man_thruster_renderer *mtr = man_thruster_get_slot(bmap_frame);
				//mtr->man_batcher.add_allocate(1);
				//mtr->man_batcher.draw_beam(&start, &end, rad, 1.0f);
				batching_add_beam(bmap_frame, &start, &end, rad, 1.0f);
			}
		}
	}
}

void ship_render_weapon_models(model_render_params *ship_render_info, model_draw_list *scene, object *obj, uint64_t render_flags)
{
	int num = obj->instance;
	ship *shipp = &Ships[num];
	ship_info *sip = &Ship_info[Ships[num].ship_info_index];

	if ( !(sip->flags[Ship::Info_Flags::Draw_weapon_models]) || (shipp->flags[Ship_Flags::Cloaked]) || (shipp->flags[Ship_Flags::Render_without_weapons]) ) {
		return;
	}

	int i,k;
	ship_weapon *swp = &shipp->weapons;
	auto ship_pm = model_get(sip->model_num);

	scene->push_transform(&obj->pos, &obj->orient);

	auto ship_render_flags = ship_render_info->get_model_flags();
	render_flags &= ~MR_SHOW_THRUSTERS;
	ship_render_info->set_flags(render_flags);

	//primary weapons
	for ( i = 0; i < swp->num_primary_banks; i++ ) {
		auto wip = &Weapon_info[swp->primary_bank_weapons[i]];

		if ( wip->external_model_num < 0 || !sip->draw_primary_models[i] ) {
			continue;
		}

		// Lazily create the model instance here, if we need to.  The ideal place to put this
		// would be in parse_object_create_sub, but the player can alter the ship loadout
		// after that function runs.
		if (!swp->primary_bank_model_instance_check[i])
		{
			auto pm = model_get(wip->external_model_num);

			// create a model instance only if at least one submodel has gun rotation
			for (int mn = 0; mn < pm->n_models; mn++)
			{
				if (pm->submodel[mn].flags[Model::Submodel_flags::Gun_rotation])
				{
					swp->primary_bank_external_model_instance[i] = model_create_instance(model_objnum_special::OBJNUM_NONE, wip->external_model_num);
					break;
				}
			}

			swp->primary_bank_model_instance_check[i] = true;
		}

		auto bank = &ship_pm->gun_banks[i];

		if ( swp->primary_bank_external_model_instance[i] >= 0 )
		{
			auto pmi = model_get_instance(swp->primary_bank_external_model_instance[i]);
			auto pm = model_get(pmi->model_num);

			// spin the submodels by the gun rotation
			for (int mn = 0; mn < pm->n_models; ++mn)
			{
				if (pm->submodel[mn].flags[Model::Submodel_flags::Gun_rotation])
				{
					angles angs = vmd_zero_angles;
					angs.b = shipp->primary_rotate_ang[i];
					vm_angles_2_matrix(&pmi->submodel[mn].canonical_orient, &angs);
				}
			}
		}

		for ( k = 0; k < bank->num_slots; k++ ) {
			// "Bank" the external model by the angle offset
			angles angs = { 0.0f, bank->external_model_angle_offset[k], 0.0f };
			matrix model_orient;
			vm_angles_2_matrix(&model_orient, &angs);

			model_render_queue(ship_render_info, scene, wip->external_model_num, swp->primary_bank_external_model_instance[i], &model_orient, &bank->pnt[k]);
		}
	}

	//secondary weapons
	int num_secondaries_rendered = 0;
	vec3d secondary_weapon_pos;

	for (i = 0; i < swp->num_secondary_banks; i++) {
		auto wip = &Weapon_info[swp->secondary_bank_weapons[i]];

		if ( wip->external_model_num == -1 || !sip->draw_secondary_models[i] ) {
			continue;
		}

		auto bank = &ship_pm->missile_banks[i];

		if (wip->wi_flags[Weapon::Info_Flags::External_weapon_lnch]) {			
			for(k = 0; k < bank->num_slots; k++) {
				// "Bank" the external model by the angle offset
				angles angs = { 0.0f, bank->external_model_angle_offset[k], 0.0f };
				matrix model_orient;
				vm_angles_2_matrix(&model_orient, &angs);

				model_render_queue(ship_render_info, scene, wip->external_model_num, &model_orient, &bank->pnt[k]);
			}
		} else {
			num_secondaries_rendered = 0;

			for ( k = 0; k < bank->num_slots; k++ ) {
				secondary_weapon_pos = bank->pnt[k];

				if ( num_secondaries_rendered >= shipp->weapons.secondary_bank_ammo[i] ) {
					break;
				}

				if ( shipp->secondary_point_reload_pct.get(i, k) <= 0.0 ) {
					continue;
				}

				num_secondaries_rendered++;

				vm_vec_scale_add2(&secondary_weapon_pos, &vmd_z_vector, -(1.0f-shipp->secondary_point_reload_pct.get(i, k)) * model_get(wip->external_model_num)->rad);

				// "Bank" the external model by the angle offset
				angles angs = { 0.0f, bank->external_model_angle_offset[k], 0.0f };
				matrix model_orient;
				vm_angles_2_matrix(&model_orient, &angs);

				model_render_queue(ship_render_info, scene, wip->external_model_num, &model_orient, &secondary_weapon_pos);
			}
		}
	}

	ship_render_info->set_flags(ship_render_flags);

	scene->pop_transform();
}

int ship_render_get_insignia(object* obj, ship* shipp)
{
	if ( Rendering_to_shadow_map ) {
		return -1;
	}

	if ( Game_mode & GM_MULTIPLAYER ) {
		// if its any player's object
		int np_index = multi_find_player_by_object( obj );
		if ( (np_index >= 0) && (np_index < MAX_PLAYERS) && MULTI_CONNECTED(Net_players[np_index]) && (Net_players[np_index].m_player != NULL) ) {
			return Net_players[np_index].m_player->insignia_texture;
		}
	} 

	// in single player, we want to render model insignias on all ships in alpha beta and gamma
	// Goober5000 - and also on wings that have their logos set

	// if its an object in my squadron
	if ( ship_in_my_squadron(shipp) ) {
		return Player->insignia_texture;
	}

	// maybe it has a wing squad logo - Goober5000
	if ( shipp->wingnum >= 0 ) {
		// don't override the player's wing
		if ( shipp->wingnum != Player_ship->wingnum ) {
			// if we have a logo texture
			if ( Wings[shipp->wingnum].wing_insignia_texture >= 0 ) {
				return Wings[shipp->wingnum].wing_insignia_texture;
			}
		}
	}

	return -1;
}

void ship_render_set_animated_effect(model_render_params *render_info, ship *shipp, uint64_t * /*render_flags*/)
{
	if ( !shipp->shader_effect_timestamp.isValid() || Rendering_to_shadow_map ) {
		return;
	}

	float timer;

	ship_effect* sep = &Ship_effects[shipp->shader_effect_num];
	int elapsed_time = shipp->shader_effect_duration - timestamp_until(shipp->shader_effect_timestamp);
	
	if (shipp->shader_effect_duration > 0) {
		if (sep->invert_timer) {
			timer = 1.0f - (elapsed_time / (float)shipp->shader_effect_duration);
			timer = MAX(timer, 0.0f);
		}
		else {
			timer = (elapsed_time / (float)shipp->shader_effect_duration);
		}

		render_info->set_animated_effect(sep->shader_effect, timer);
	}

	if ( timestamp_elapsed(shipp->shader_effect_timestamp) ) {
		shipp->flags.set(Ship_Flags::Cloaked, sep->disables_rendering);
		shipp->shader_effect_timestamp = TIMESTAMP::invalid();
	} else {
		shipp->flags.remove(Ship_Flags::Cloaked);
	}
}

void ship_render(object* obj, model_draw_list* scene)
{
	int num = obj->instance;
	ship *shipp = &Ships[num];
	ship_info *sip = &Ship_info[Ships[num].ship_info_index];
	ship *warp_shipp = NULL;
	bool is_first_stage_arrival = false;
	bool show_thrusters = (!shipp->flags[Ship_Flags::No_thrusters]) && !Rendering_to_shadow_map;
	dock_function_info dfi;

	MONITOR_INC( NumShipsRend, 1 );

	// look for a warping ship, whether for me or for anybody I'm docked with
	dock_evaluate_all_docked_objects(obj, &dfi, ship_find_warping_ship_helper);

	// if any docked objects are set to stage 1 arrival then set bool
	if ( dfi.maintained_variables.bool_value ) {
		warp_shipp = &Ships[dfi.maintained_variables.objp_value->instance];

		is_first_stage_arrival = warp_shipp->flags[Ship_Flags::Arriving_stage_1];

		// This is a hack to make ships using the hyperspace warpin type to
		// render even in stage 1, which is used for collision detection
		// purposes -zookeeper
		if ( Warp_params[warp_shipp->warpin_params_index].warp_type == WT_HYPERSPACE ) {
			warp_shipp = NULL;
			is_first_stage_arrival = false;
		}
	}

	if ( is_first_stage_arrival ) {
		//WMC - Draw animated warp effect (ie BSG thingy)
		//WMC - based on Bobb's secondary thruster stuff
		//which was in turn based on the beam code.
		//I'm gonna need some serious acid to neutralize this base.
		if(shipp->is_arriving(ship::warpstage::BOTH, true)) {
			shipp->warpin_effect->warpShipRender();
		} else if(shipp->flags[Ship_Flags::Depart_warp]) {
			shipp->warpout_effect->warpShipRender();
		}

		return;
	}

	model_render_params render_info;
	if ( obj == Viewer_obj && !Rendering_to_shadow_map ) {
		if (!(Viewer_mode & VM_TOPDOWN))
		{
			render_info.set_object_number(OBJ_INDEX(obj));
			//To allow the player ship to cast light from first person we must still handle it's glowpoints here.
			model_render_only_glowpoint_lights(&render_info, sip->model_num, -1, &obj->orient, &obj->pos);
			return;
		}
	}

	auto pmi = model_get_instance(shipp->model_instance_num);
	auto pm = model_get(pmi->model_num);

	model_clear_instance(sip->model_num);
	model_instance_clear_arcs(pm, pmi);

	// Only render electrical arcs if within 500m of the eye (for a 10m piece)
	if ( vm_vec_dist_quick( &obj->pos, &Eye_position ) < obj->radius*50.0f && !Rendering_to_shadow_map ) {
		for (auto &arc: shipp->electrical_arcs)	{
			if (arc.timestamp.isValid()) {
				model_instance_add_arc(pm, pmi, -1, &arc.endpoint_1, &arc.endpoint_2, arc.persistent_arc_points.get(), arc.type, &arc.primary_color_1, &arc.primary_color_2, &arc.secondary_color, arc.width, arc.segment_depth);
			}
		}
	}

	uint64_t render_flags = MR_NORMAL;

	if ( shipp->large_ship_blowup_index >= 0 )	{
		shipfx_large_blowup_queue_render(scene, shipp);

		//WMC - Draw animated warp effect (ie BSG thingy)
		//WMC - based on Bobb's secondary thruster stuff
		//which was in turn based on the beam code.
		//I'm gonna need some serious acid to neutralize this base.
		if(shipp->is_arriving(ship::warpstage::BOTH, true)) {
			shipp->warpin_effect->warpShipRender();
		} else if(shipp->flags[Ship_Flags::Depart_warp]) {
			shipp->warpout_effect->warpShipRender();
		}

		return;
	}
		
	ship_render_batch_thrusters(obj);

	
	if ( !(shipp->flags[Ship_Flags::Disabled]) && !ship_subsys_disrupted(shipp, SUBSYSTEM_ENGINE) && show_thrusters) {
		mst_info mst;

		ship_set_thruster_info(&mst, obj, shipp, sip);

		render_info.set_thruster_info(mst);

		render_flags |= MR_SHOW_THRUSTERS;
	}

	if (shipp->flags[Ship_Flags::Render_with_alpha_mult]) {
		render_info.set_alpha_mult(shipp->alpha_mult);
	}

	// Warp_shipp points to the ship that is going through a
	// warp... either this ship or the ship it is docked with.
	if ( warp_shipp != NULL ) {
		if ( warp_shipp->is_arriving(ship::warpstage::BOTH, true) ) {
			warp_shipp->warpin_effect->warpShipClip(&render_info);
		} else if ( warp_shipp->flags[Ship_Flags::Depart_warp] ) {
			warp_shipp->warpout_effect->warpShipClip(&render_info);
		}
	}

	// maybe set squad logo bitmap
	render_info.set_insignia_bitmap(ship_render_get_insignia(obj, shipp));

	// Valathil - maybe do a scripting hook here to do some scriptable effects?
	ship_render_set_animated_effect(&render_info, shipp, &render_flags);

	if ( sip->uses_team_colors && !shipp->flags[Ship_Flags::Render_without_miscmap] ) {
		team_color model_team_color;

		bool team_color_set = model_get_team_color(&model_team_color, shipp->team_name, shipp->secondary_team_name, shipp->team_change_timestamp, shipp->team_change_time);

		if ( team_color_set ) {
			render_info.set_team_color(model_team_color);
		}
	}

	if ( sip->flags[Ship::Info_Flags::No_lighting] ) {
		render_flags |= MR_NO_LIGHTING;
	}

	if ( Rendering_to_shadow_map ) {
		render_flags = MR_NO_TEXTURING | MR_NO_LIGHTING;
	}

	if (shipp->flags[Ship_Flags::Glowmaps_disabled]) {
		render_flags |= MR_NO_GLOWMAPS;
	}

	if (shipp->flags[Ship_Flags::Draw_as_wireframe]) {
		render_flags |= MR_SHOW_OUTLINE_HTL | MR_NO_POLYS | MR_NO_TEXTURING;
		render_info.set_color(Wireframe_color);
	}

	if (shipp->flags[Ship_Flags::Render_full_detail]) {
		render_flags |= MR_FULL_DETAIL;
	}

	if (shipp->flags[Ship_Flags::Render_without_light]) {
		render_flags |= MR_NO_LIGHTING;
	}

	if (shipp->flags[Ship_Flags::No_insignias]) {
		render_flags |= MR_NO_INSIGNIA;		
	}

	uint debug_flags = render_info.get_debug_flags();

	if (shipp->flags[Ship_Flags::Render_without_diffuse]) {
		debug_flags |= MR_DEBUG_NO_DIFFUSE;
	}

	if (shipp->flags[Ship_Flags::Render_without_glowmap]) {
		debug_flags |= MR_DEBUG_NO_GLOW;
	}
	
	if (shipp->flags[Ship_Flags::Render_without_normalmap]) {
		debug_flags |= MR_DEBUG_NO_NORMAL;
	}

	if (shipp->flags[Ship_Flags::Render_without_ambientmap]) {
		debug_flags |= MR_DEBUG_NO_AMBIENT;
	}

	if (shipp->flags[Ship_Flags::Render_without_specmap]) {
		debug_flags |= MR_DEBUG_NO_SPEC;
	}

	if (shipp->flags[Ship_Flags::Render_without_reflectmap]) {
		debug_flags |= MR_DEBUG_NO_REFLECT;
	}

	render_info.set_flags(render_flags);
	render_info.set_debug_flags(debug_flags);

	//draw weapon models
	ship_render_weapon_models(&render_info, scene, obj, render_flags);

	render_info.set_object_number(OBJ_INDEX(obj));
	render_info.set_replacement_textures(pmi->texture_replace);

	// small ships
	if ( !( shipp->flags[Ship_Flags::Cloaked] ) ) {
		if ( ( The_mission.flags[Mission::Mission_Flags::Fullneb] ) && ( sip->is_small_ship() ) ) {			
			// force detail levels, but only in fullneb not volumetric nebula
			float fog_val = neb2_get_fog_visibility(&obj->pos, 1.0f);
			if ( fog_val <= 0.15f ) {
				render_info.set_detail_level_lock(2);
			}
		}

		model_render_queue(&render_info, scene, sip->model_num, &obj->orient, &obj->pos);
	}

	if (shipp->shield_hits && !Rendering_to_shadow_map) {
		create_shield_explosion_all(obj);
		shipp->shield_hits = 0;
	}

	//WMC - Draw animated warp effect (ie BSG thingy)
	//WMC - based on Bobb's secondary thruster stuff
	//which was in turn based on the beam code.
	//I'm gonna need some serious acid to neutralize this base.
	if(shipp->is_arriving(ship::warpstage::BOTH, true)) {
		shipp->warpin_effect->warpShipRender();
	} else if(shipp->flags[Ship_Flags::Depart_warp]) {
		shipp->warpout_effect->warpShipRender();
	}
}

flagset<Ship::Ship_Flags> Ignore_List;
void set_default_ignore_list() {
	Ignore_List.reset();
	Ignore_List.set(Ship::Ship_Flags::Exploded);
	Ignore_List.set(Ship::Ship_Flags::Depart_warp);
	Ignore_List.set(Ship::Ship_Flags::Dying);
	Ignore_List.set(Ship::Ship_Flags::Arriving_stage_1);
	Ignore_List.set(Ship::Ship_Flags::Arriving_stage_1_dock_follower);
	Ignore_List.set(Ship::Ship_Flags::Hidden_from_sensors);
}

void toggle_ignore_list_flag(Ship::Ship_Flags flag) {
	if (Ignore_List[flag])
		Ignore_List.remove(flag);
	else
		Ignore_List.set(flag);
}

ship_subsys* ship_get_subsys_for_submodel(ship* shipp, int submodel)
{
	ship_subsys* subsys;

	if (submodel == -1) {
		return nullptr;
	}

	for (subsys = GET_FIRST(&shipp->subsys_list); subsys != END_OF_LIST(&shipp->subsys_list);
	     subsys = GET_NEXT(subsys)) {
		if (subsys->system_info->subobj_num == submodel) {
			return subsys;
		}
	}

	return nullptr;
}

/**
 * Is the requested type of ship arriving in the requested stage?
 *
 * Dock leaders (plus single ships) & dock followers need to be handled
 * separately such that leaders+singles handle the warp process, but
 * followers need to behave as though they are warping with respect to
 * HUD targeting, drawing thrusters, radar, AI targeting, etc
 *
 * @param stage Check one of stage1, stage2 or both
 * @param dock_leader_or_single check dock-leaders+singles, or dock-followers?
 *
 * @return is the ship arriving, bool
 */
bool ship::is_arriving(ship::warpstage stage, bool dock_leader_or_single) const
{
	if (stage == ship::warpstage::BOTH) {
		if (!dock_leader_or_single) {
			return flags[Ship::Ship_Flags::Arriving_stage_1, Ship::Ship_Flags::Arriving_stage_1_dock_follower, Ship::Ship_Flags::Arriving_stage_2, Ship::Ship_Flags::Arriving_stage_2_dock_follower];
		}
		else {
			return flags[Ship::Ship_Flags::Arriving_stage_1, Ship::Ship_Flags::Arriving_stage_2];
		}
	}
	else if (stage == ship::warpstage::STAGE1) {
		if (!dock_leader_or_single) {
			return flags[Ship::Ship_Flags::Arriving_stage_1, Ship::Ship_Flags::Arriving_stage_1_dock_follower];
		}
		else {
			return flags[Ship::Ship_Flags::Arriving_stage_1];
		}
	}
	if (stage == ship::warpstage::STAGE2) {
		if (!dock_leader_or_single) {
			return flags[Ship::Ship_Flags::Arriving_stage_2, Ship::Ship_Flags::Arriving_stage_2_dock_follower];
		}
		else {
			return flags[Ship::Ship_Flags::Arriving_stage_2];
		}
	}

	// should never reach here
	Assertion(false, "ship::is_arriving didn't handle all possible states; get a coder!");
	return false;
}

void ship_clear_lock(lock_info *slot) {
	vec3d zero_vec = ZERO_VECTOR;

	slot->accumulated_x_pixels = 0;
	slot->accumulated_y_pixels = 0;

	slot->catch_up_distance = 0.0f;

	slot->catching_up = 0;

	slot->current_target_sx = -1;
	slot->current_target_sy = -1;

	slot->dist_to_lock = 0.0f;

	slot->indicator_start_x = -1;
	slot->indicator_start_y = -1;

	slot->indicator_visible = false;

	slot->indicator_x = -1;
	slot->indicator_y = -1;

	slot->last_dist_to_target = 0.0f;

	slot->lock_anim_time_elapsed = 0.0f;
	slot->lock_gauge_time_elapsed = 0.0f;

	slot->maintain_lock_count = 0;

	slot->need_new_start_pos = false;

	slot->target_in_lock_cone = false;

	slot->world_pos = zero_vec;

	slot->locked = false;

	slot->obj = nullptr;
	slot->subsys = nullptr;


	slot->time_to_lock = -1.0f;
}

void ship_queue_missile_locks(ship *shipp)
{
	shipp->missile_locks_firing.clear();

	// queue up valid missile locks
	std::copy_if(shipp->missile_locks.begin(), shipp->missile_locks.end(), std::back_inserter(shipp->missile_locks_firing), [](lock_info lock) { return lock.locked; });
}

bool ship_lock_present(ship *shipp)
{
	return std::any_of(shipp->missile_locks.begin(), shipp->missile_locks.end(), [](lock_info lock) { return lock.locked; });
}

bool ship_start_secondary_fire(object* objp)
{
	Assert( objp != nullptr);

	if ( objp->type != OBJ_SHIP ) {
		return false;
	}

	int n = objp->instance;

	Assert( n >= 0 && n < MAX_SHIPS );
	Assert( Ships[n].objnum == OBJ_INDEX(objp) );

	ship *shipp = &Ships[n];
	ship_info *sip = &Ship_info[shipp->ship_info_index];
	ship_weapon* swp = &shipp->weapons;

	int bank = swp->current_secondary_bank;

	if ( bank < 0 || bank >= sip->num_secondary_banks ) {
		return false;
	}

	// It's possible for banks to be empty without issue
	// but indices outside the weapon_info range are a problem
	Assert( swp->secondary_bank_weapons[bank] < weapon_info_size() );
	if ( (swp->secondary_bank_weapons[bank] < 0) || (swp->secondary_bank_weapons[bank] >= weapon_info_size()) ) {
		return false;
	}

	weapon_info *wip = &Weapon_info[swp->secondary_bank_weapons[bank]];

	if ( wip->trigger_lock ) {
		swp->flags.set(Ship::Weapon_Flags::Secondary_trigger_down);

		return true;
	}

	return false;
}

bool ship_stop_secondary_fire(object* objp)
{
	Assert( objp != nullptr);

	if ( objp->type != OBJ_SHIP ) {
		return false;
	}

	int n = objp->instance;

	if ( n < 0 || n >= MAX_SHIPS ) {
		return false;
	}
	Assert( Ships[n].objnum == OBJ_INDEX(objp) );

	ship *shipp = &Ships[n];
	ship_info *sip = &Ship_info[shipp->ship_info_index];
	ship_weapon *swp = &shipp->weapons;

	int bank = swp->current_secondary_bank;

	if ( bank < 0 || bank >= sip->num_secondary_banks ) {
		return false;
	}

	// It's possible for banks to be empty without issue
	// but indices outside the weapon_info range are a problem
	Assert(swp->secondary_bank_weapons[bank] < weapon_info_size());
	if ((swp->secondary_bank_weapons[bank] < 0) || (swp->secondary_bank_weapons[bank] >= weapon_info_size())) {
		return false;
	}

	weapon_info *wip = &Weapon_info[swp->secondary_bank_weapons[bank]];

	if ( wip->trigger_lock && swp->flags[Ship::Weapon_Flags::Secondary_trigger_down]) {
		swp->flags.remove(Ship::Weapon_Flags::Secondary_trigger_down);

		return true;
	}
	
	return false;
}

bool ship_secondary_has_ammo(ship_weapon* swp, int bank_index) {
	return swp->secondary_bank_ammo[bank_index] > 0 || Weapon_info[swp->secondary_bank_weapons[bank_index]].wi_flags[Weapon::Info_Flags::SecondaryNoAmmo];
}



/**
 * Determine if ship visible on radar
 *
 * @return 0 - not visible
 * @return 1 - marginally targetable (jiggly on radar)
 * @return 2 - fully targetable
 */
int ship_check_visibility(const ship* viewed, ship* viewer)
{
	if (!viewer) {
		// if the second argument is not supplied, default to the player, per retail
		if (Game_mode & GM_MULTIPLAYER) {
			mprintf(("In multiplayer shared_check_ship_visibility must have two arguments!  Defaulting to the first "
					 "player.\n"));

			// to make allowances for buggy missions (such as retail), just pick the first player
			// if we actually have no valid players, viewer_shipp will be NULL, but that's ok
			for (int i = 0; i < MAX_PLAYERS; ++i) {
				int shipnum = multi_get_player_ship(i);
				if (shipnum >= 0) {
					viewer = &Ships[shipnum];
					break;
				}
			}
		} else
			viewer = Player_ship;
	}

	object* viewed_obj = &Objects[viewed->objnum];
	int ship_is_visible = 0;
	//There are cases where the player is not a ship, so the above logic could result in still not having any valid ship pointer.
	if(!viewer)
		return ship_is_visible;
	// get ship's *radar* visiblity
	if (ship_is_visible_by_team(viewed_obj, viewer)) {
		ship_is_visible = 2;
	}

	// only check awacs level if ship is not visible by team
	if (!ship_is_visible) {
		float awacs_level = awacs_get_level(viewed_obj, viewer);
		if (awacs_level >= 1.0f) {
			ship_is_visible = 2;
		} else if (awacs_level > 0) {
			ship_is_visible = 1;
		}
	}

	return ship_is_visible;
}

ship_electrical_arc *ship_find_or_create_electrical_arc_slot(ship *shipp, bool no_create)
{
	size_t i = 0;
	for (auto &ii : shipp->electrical_arcs)
	{
		if (!ii.timestamp.isValid())
			break;
		i++;
	}

	if (i == shipp->electrical_arcs.size())
	{
		if (no_create)
			return nullptr;
		shipp->electrical_arcs.emplace_back();
	}

	return &shipp->electrical_arcs[i];
}
