/*
 * Copyright (C) Volition, Inc. 1999.  All rights reserved.
 *
 * All source code herein is the property of Volition, Inc. You may not sell 
 * or otherwise commercially exploit the source or things you created based on the 
 * source.
 *
*/ 

#include "ai/aigoals.h"
#include "ai/ailua.h"
#include "gamesnd/gamesnd.h"
#include "globalincs/linklist.h"
#include "hud/hudmessage.h"
#include "hud/hudsquadmsg.h"
#include "hud/hudtargetbox.h"
#include "iff_defs/iff_defs.h"
#include "io/key.h"
#include "io/timer.h"
#include "mission/missionlog.h"
#include "mission/missionmessage.h"
#include "network/multi_pmsg.h"
#include "network/multimsgs.h"
#include "network/multiutil.h"
#include "parse/parselo.h"
#include "parse/sexp.h"
#include "playerman/player.h"
#include "scripting/global_hooks.h"
#include "scripting/api/objs/enums.h"
#include "scripting/api/objs/oswpt.h"
#include "scripting/api/objs/subsystem.h"
#include "ship/ship.h"
#include "ship/subsysdamage.h"
#include "weapon/emp.h"
#include "weapon/weapon.h"

// defines for different modes in the squad messaging system

#define DEFAULT_MSG_TIMEOUT		(8 * 1000)		// number of seconds * 1000 to get milliseconds
#define MSG_KEY_EAT_TIME			(300)

int Squad_msg_mode;							// current mode that the messaging system is in
static int Msg_key_used;								// local variable which tells if the key being processed
															// with the messaging system was actually used
static int Msg_key;                         // global which indicates which key was currently pressed
static bool Msg_key_set_from_scripting; // is true if the key was set from scripting and not from a player keypress
static TIMESTAMP Msg_mode_timestamp;
int Msg_instance;						// variable which holds ship/wing instance to send the message to
int Msg_shortcut_command;			// holds command when using a shortcut key
static int Msg_target_objnum;           // id of the current target of the player
static ship_subsys* Msg_targeted_subsys; // pointer to current subsystem which is targeted
static int Msg_enemies;                  // tells us whether or not to message enemy ships or friendlies

static int Msg_eat_key_timestamp; // used to temporarily "eat" keys

// defined to position the messaging box
int Mbox_item_h[GR_NUM_RESOLUTIONS] = {
	10, 
	10
};
int Mbox_item_xoffset[GR_NUM_RESOLUTIONS] = {
	17,
	17
};

// top of the message box gauge
int Mbox_top_coords[GR_NUM_RESOLUTIONS][2] = {
	{ // GR_640
		445, 5
	},
	{ // GR_1024
		827, 5
	}
};

int Mbox_bmap_coords[GR_NUM_RESOLUTIONS][2] = {
	{ // GR_640
		445, 17
	},
	{ // GR_1024
		827, 17
	}
};

// squadmsg menu pgup and pgdn
int Menu_pgup_coords[GR_NUM_RESOLUTIONS][2] = {
	{ // GR_640
		590, 9
	},
	{ // GR_1024
		937, 9
	}
};
int Menu_pgdn_coords[GR_NUM_RESOLUTIONS][2] = {
	{ // GR_640
		590, 120
	},
	{ // GR_1024
		937, 120
	}
};

// -----------
// following defines/vars are used to build menus that are used in messaging mode

char Squad_msg_title[256] = "";
mmode_item MsgItems[MAX_MENU_ITEMS];
int Num_menu_items = -1; // number of items for a message menu

static int First_menu_item = -1;							// index of first item in the menu. This tracks what element of comms options collection is displayed as first option, and displays the next 9 options. Changes only by +/- MAX_MENU_ITEMS (10)
static int Selected_menu_item = First_menu_item;           //!< index of selected item in the menu. Possible index range: 0 - 9, assuming MAX_MENU_ITEMS == 10, and First_menu_item gets initialized
static bool Display_selector = false;
static SCP_string Lua_sqd_msg_cat;

#define MAX_KEYS_NO_SCROLL	10
#define MAX_KEYS_USED		12		// maximum number of keys used for the messaging system

// next array is the array of MAX_KEYS_USED size which are the keys to use for messaging mode

int keys_used[] = {	KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9, KEY_0,
							KEY_PAGEUP, KEY_PAGEDOWN  };

#define ID1		1
#define ID2		2


SCP_string  Comm_order_types[NUM_COMM_ORDER_TYPES];

int player_order::orderingCounter = 0;

SCP_vector<player_order> Player_orders = {
	player_order("no order",   "No Order",  -1, -1, NO_ORDER_ITEM), //Required to keep defines in sync with array indices
	player_order("attack ship",   "Destroy my target",  299, -1, ATTACK_TARGET_ITEM),
	player_order("disable ship",  "Disable my target",	300, -1, DISABLE_TARGET_ITEM),
	player_order("disarm ship",	  "Disarm my target",	301, -1, DISARM_TARGET_ITEM),
	player_order("disable subsys","Destroy subsystem",	302, -1, DISABLE_SUBSYSTEM_ITEM),
	player_order("guard ship",	  "Protect my target",	303, -1, PROTECT_TARGET_ITEM),
	player_order("ignore ship",   "Ignore my target",	304, -1, IGNORE_TARGET_ITEM),
	player_order("form on wing","Form on my wing",	305, -1, FORMATION_ITEM),
	player_order("cover me",	"Cover me",			306, -1, COVER_ME_ITEM),
	player_order("attack any",	"Engage enemy",		307, -1, ENGAGE_ENEMY_ITEM),
	player_order("dock", "Capture my target",	308, -1, CAPTURE_TARGET_ITEM),
	player_order("rearm me", "Rearm me",			309, -1, REARM_REPAIR_ME_ITEM),
	player_order("abort rearm", "Abort rearm",		310, -1, ABORT_REARM_REPAIR_ITEM),
	player_order("depart", "Depart",				311, -1, DEPART_ITEM),
	player_order("stay near me", "Stay near me",		-1, -1, STAY_NEAR_ME_ITEM),
	player_order("stay near ship", "Stay near my target",-1, -1, STAY_NEAR_TARGET_ITEM),
	player_order("keep safe dist", "Keep safe distance", -1, -1, KEEP_SAFE_DIST_ITEM)
};

const SCP_set<size_t> default_messages{ ATTACK_TARGET_ITEM , DISABLE_TARGET_ITEM , DISARM_TARGET_ITEM , PROTECT_TARGET_ITEM , IGNORE_TARGET_ITEM , FORMATION_ITEM , COVER_ME_ITEM , ENGAGE_ENEMY_ITEM , DEPART_ITEM , DISABLE_SUBSYSTEM_ITEM };
// note: STAY_NEAR_TARGET_ITEM appears in both enemy and friendly sets
const SCP_set<size_t> enemy_target_messages{ ATTACK_TARGET_ITEM , DISABLE_TARGET_ITEM , DISARM_TARGET_ITEM , IGNORE_TARGET_ITEM , STAY_NEAR_TARGET_ITEM , CAPTURE_TARGET_ITEM , DISABLE_SUBSYSTEM_ITEM };
const SCP_set<size_t> friendly_target_messages{ PROTECT_TARGET_ITEM , STAY_NEAR_TARGET_ITEM };
const SCP_set<size_t> target_messages = []() {
	SCP_set<size_t> setunion;
	std::set_union(enemy_target_messages.cbegin(), enemy_target_messages.cend(), friendly_target_messages.cbegin(), friendly_target_messages.cend(), std::inserter(setunion, setunion.end()));
	return setunion;
}();


void hud_init_comm_orders()
{
	int i;

	const char *temp_comm_order_types[] =
	{
		XSTR("Ships", 293),
		XSTR("Wings", 294),
		XSTR("All Fighters", 295),
		XSTR("Reinforcements", 296),
		XSTR("Rearm/Repair Subsys", 297),
		XSTR("Abort Rearm", 298)
	};

	for (i = 0; i < NUM_COMM_ORDER_TYPES; i++)
	{
		Comm_order_types[i] = temp_comm_order_types[i];
	}

	for (auto& order : Player_orders)
		order.localize();

	std::sort(Player_orders.begin(), Player_orders.end(), [](const player_order& o1, const player_order& o2) {
		return o1.legacy_id < o2.legacy_id;
	});
}

// Text to display on the messaging menu when using the shortcut keys
const char *comm_order_get_text(int item)
{
	Assertion(item < (int)Player_orders.size(), "Did not find order with index %d!", item);
	
	return Player_orders[item].localized_name.c_str();
}

SCP_vector<squadmsg_history> Squadmsg_history; 

// forward declarations
void hud_add_issued_order(const char *name, int order);
void hud_update_last_order(const char *target, int order_source, int special_index);
bool hud_squadmsg_is_target_order_valid(size_t order, ai_info *aip = nullptr, bool isWing = false);
bool hud_squadmsg_ship_valid(ship *shipp, object *objp = nullptr);

// function to set up variables needed when messaging mode is started
void hud_squadmsg_start()
{
	if (scripting::hooks::OnHudCommMenuOpened->isActive())
	{
		auto paramList = scripting::hook_param_list(scripting::hook_param("Player", 'o', Player_obj));
		if (scripting::hooks::OnHudCommMenuOpened->isOverride(paramList))
		{
			scripting::hooks::OnHudCommMenuOpened->run(std::move(paramList));
			return;
		}
	}

	Msg_key = -1;
	Msg_key_set_from_scripting = false;

	Num_menu_items = -1;													// reset the menu items
	First_menu_item = 0;
	Selected_menu_item = First_menu_item;                            // make first menu item a selected object
	Display_selector = false;

	Squad_msg_mode = SM_MODE_TYPE_SELECT;							// start off at the base state
	Msg_mode_timestamp = _timestamp(DEFAULT_MSG_TIMEOUT);		// initialize our timer to bogus value
	Msg_shortcut_command = -1;											// assume no shortcut key being used
	Msg_target_objnum = Player_ai->target_objnum;				// save the players target object number
	Msg_targeted_subsys = Player_ai->targeted_subsys;				// save the players currently targted subsystem
#ifndef NDEBUG
	Msg_enemies = 0;														// tells us if we are messaging enemy ships
#endif

	snd_play( gamesnd_get_game_sound(GameSounds::SQUADMSGING_ON) );

	if (scripting::hooks::OnHudCommMenuOpened->isActive())
	{
		auto paramList = scripting::hook_param_list(scripting::hook_param("Player", 'o', Player_obj));
		scripting::hooks::OnHudCommMenuOpened->run(std::move(paramList));
	}
}

// function which will clean things up when messaging mode is done
void hud_squadmsg_end()
{
	if (scripting::hooks::OnHudCommMenuClosed->isActive())
	{
		auto paramList = scripting::hook_param_list(scripting::hook_param("Player", 'o', Player_obj));
		if (scripting::hooks::OnHudCommMenuClosed->isOverride(paramList))
		{
			scripting::hooks::OnHudCommMenuClosed->run(std::move(paramList));
			return;
		}
	}

	if ( message_is_playing() == FALSE )
		snd_play( gamesnd_get_game_sound(GameSounds::SQUADMSGING_OFF) );

	if (scripting::hooks::OnHudCommMenuClosed->isActive())
	{
		auto paramList = scripting::hook_param_list(scripting::hook_param("Player", 'o', Player_obj));
		scripting::hooks::OnHudCommMenuClosed->run(std::move(paramList));
	}

	Squad_msg_title[0] = '\0';
}

// function which returns true if there are fighters/bombers on the players team in the mission
// In debug versions, we will allow messaging to enemies
bool hud_squadmsg_exist_fighters( )
{
	ship_obj *so;
	object *objp;
	ship *shipp;

	for (so = GET_FIRST(&Ship_obj_list); so != END_OF_LIST(&Ship_obj_list); so = GET_NEXT(so))
	{	
		objp = &Objects[so->objnum];
		if (objp->flags[Object::Object_Flags::Should_be_dead])
			continue;

		shipp = &Ships[objp->instance];
		Assertion(shipp->objnum != -1, "hud_squadmsg_exist_fighters() discovered that ship #%d ('%s') has an objnum of -1. Since the ship was retrieved from its object number (%d), this should be impossible; get a coder!\n", objp->instance, shipp->ship_name, so->objnum);

		// ship must be a fighter/bomber
		if (!(Ship_info[shipp->ship_info_index].is_fighter_bomber()))
			continue;

		// this ship satisfies everything
		if (hud_squadmsg_ship_valid(shipp, objp))
			return true;
	}

	return false;
}

// Routine to return true if a ship should be put onto the messaging menu. Analagous to hud_squadmsg_wing_valid() below. -MageKing17
bool hud_squadmsg_ship_valid(ship *shipp, object *objp)
{
	Assertion(shipp != nullptr, "hud_squadmsg_ship_valid() was passed a null shipp pointer; get a coder!\n");
	if (!objp)	// For simplicity's sake, allow it to be called with just the ship pointer
		objp = &Objects[shipp->objnum];

	// ships must be able to receive orders
	if ( Ship_info[shipp->ship_info_index].class_type < 0 || !(Ship_types[Ship_info[shipp->ship_info_index].class_type].flags[Ship::Type_Info_Flags::AI_accept_player_orders]) )
		return false;

	// be sure ship is on correct team
#ifdef NDEBUG
	if (shipp->team != Player_ship->team)
		return false;
#else
	if (!Msg_enemies && (shipp->team != Player_ship->team))
		return false;
#endif

	// departing or dying ships cannot be on list
	if ( shipp->is_dying_or_departing() )
		return false;

	// cannot be my ship or an instructor
	if ((objp == Player_obj) || is_instructor(objp))
		return false;

	// ship must be accepting ship type orders
	if ( shipp->orders_accepted.empty())
		return false;

	// if it is a player ship, we must be in multiplayer
	if ( (objp->flags[Object::Object_Flags::Player_ship]) && !(Game_mode & GM_MULTIPLAYER) )
		return false;

	// if a messaging shortcut, be sure this ship can process the order
	if ( Msg_shortcut_command != -1 ) {
		if (!shipp->orders_accepted.contains(Msg_shortcut_command))
			return false;
		
		else if ( !hud_squadmsg_ship_order_valid(objp->instance, Msg_shortcut_command) )
			return false;
	}

	// maybe check comm system
	if (The_mission.ai_profile->flags[AI::Profile_Flags::Check_comms_for_non_player_ships] && hud_communications_state(shipp) != COMM_OK)
		return false;

	// If we got to this point, the ship must be valid.
	return true;
}

// function which counts the number of ships available for messaging.  Used to determine if
// we should grey out a menu or allow a shortcut command to apply.  parameter "flag" is used
// to tell us whether or not we should add the ship to a menu item or not.  We include the
// flag so that we don't have to have conditions for messaging ships/wings in two places.
int hud_squadmsg_count_ships(int add_to_menu)
{
	int count;
	ship_obj *so;
	object *objp;
	ship *shipp;

	count = 0;
	for (so = GET_FIRST(&Ship_obj_list); so != END_OF_LIST(&Ship_obj_list); so = GET_NEXT(so))
	{	
		objp = &Objects[so->objnum];
		if (objp->flags[Object::Object_Flags::Should_be_dead])
			continue;

		shipp = &Ships[objp->instance];
		Assertion(shipp->objnum != -1, "hud_squadmsg_count_ships() discovered that ship #%d ('%s') has an objnum of -1. Since the ship was retrieved from its object number (%d), this should be impossible; get a coder!\n", objp->instance, shipp->ship_name, so->objnum);

		if (!hud_squadmsg_ship_valid(shipp, objp))
			continue;

		// this ship satisfies everything
		count++;
		if (add_to_menu)
		{
			Assert ( Num_menu_items < MAX_MENU_ITEMS );
			MsgItems[Num_menu_items].text = shipp->get_display_name();
			MsgItems[Num_menu_items].instance = SHIP_INDEX(shipp);
			MsgItems[Num_menu_items].active = 1;
			Num_menu_items++;
		}
	}

	return count;
}

// routine to return true if a wing should be put onto the messaging menu
bool hud_squadmsg_wing_valid(wing *wingp)
{
	int idx, ship_num;

	// a couple of special cases to account for before adding to count (or to menu).  Don't count
	// wings that are leaving or left.
	if ( wingp->flags[Ship::Wing_Flags::Gone, Ship::Wing_Flags::Departing] )
		return false;

	// Goober5000 - instead of checking wing leader, let's check all ships in wing;
	// there are several significant cases when e.g. wings contain ships of different IFFs
	for (idx = 0; idx < wingp->current_count; idx++)
	{
		ship_num = wingp->ship_index[idx];
		Assert(ship_num >= 0);

		// if at least one ship in this wing is valid, that's all we need
		if (hud_squadmsg_ship_valid(&Ships[ship_num]))
			return true;
	}

	// no ships in the wing were valid
	return false;
}

// function like above, except for wings
int hud_squadmsg_count_wings( int add_to_menu )
{
	int count, i, j;

	count = 0;

	// add the player starting wings first
	for ( i = 0; i < MAX_STARTING_WINGS; i++ ) {
		int wingnum;

		wingnum = Starting_wings[i];
		if ( wingnum == -1 )
			continue;

		if ( hud_squadmsg_wing_valid(&Wings[wingnum]) ) {
			count++;
			if ( add_to_menu ) {
				Assert ( Num_menu_items < MAX_MENU_ITEMS );
				MsgItems[Num_menu_items].text = Wings[wingnum].name;
				end_string_at_first_hash_symbol(MsgItems[Num_menu_items].text);
				MsgItems[Num_menu_items].instance = wingnum;
				MsgItems[Num_menu_items].active = 1;
				Num_menu_items++;
			}
		}
	}

	for ( i = 0; i < Num_wings; i++ ) {
		// if this wing is a player starting wing, skip it since we added it above
		for ( j = 0; j < MAX_STARTING_WINGS; j++ ) {
			if ( i == Starting_wings[j] )
				break;
		}
		if ( j < MAX_STARTING_WINGS )
			continue;

		if ( hud_squadmsg_wing_valid(&Wings[i]) ) {
			count++;
			if ( add_to_menu ) {
				Assert ( Num_menu_items < MAX_MENU_ITEMS );
				MsgItems[Num_menu_items].text = Wings[i].name;
				end_string_at_first_hash_symbol(MsgItems[Num_menu_items].text); 
				MsgItems[Num_menu_items].instance = i;
				MsgItems[Num_menu_items].active = 1;
				Num_menu_items++;
			}
		}
	}
	return count;
}


// function to set the current submode in message mode -- also resets variables that
// should be reset inbetween submodes
void hud_squadmsg_do_mode( int mode )
{
	Squad_msg_mode = mode;
	Num_menu_items = -1;
	First_menu_item = 0;
}

void hud_squadmsg_page_down()
{
	if ( (First_menu_item + MAX_MENU_DISPLAY) < Num_menu_items ) {
		First_menu_item += MAX_MENU_DISPLAY;
		Assert ( First_menu_item < Num_menu_items );
	}
}

void hud_squadmsg_page_up()
{
	if ( First_menu_item > 0 ) {
		First_menu_item -= MAX_MENU_DISPLAY;
		Assert (First_menu_item >= 0 );
	}
}

//Fuctions that allow selection of specific comms menu items with simple up/down/select buttons
void hud_squadmsg_selection_move_down() {

	//Check if comms menu is up
	if (Player->flags & PLAYER_FLAGS_MSG_MODE)
	{
		//move down
		++Selected_menu_item;
		Display_selector = true;

		//play scrolling sound and reset the comms window timeout timer, so the window doesn't disappear while we select our item
		gamesnd_play_iface(InterfaceSounds::SCROLL);
		Msg_mode_timestamp = _timestamp(DEFAULT_MSG_TIMEOUT);

		//Move to next page if we went outside of current one
		if (Selected_menu_item == MAX_MENU_DISPLAY 
			&& (First_menu_item + MAX_MENU_DISPLAY < Num_menu_items))
		{
			hud_squadmsg_page_down();
			Selected_menu_item = 0;
		}

		//Select the first menu item if we went outside items range, so we can loop around
		if (First_menu_item + Selected_menu_item >= Num_menu_items) 
		{
			First_menu_item = 0;
			Selected_menu_item = First_menu_item;
		}
	}
}

void hud_squadmsg_selection_move_up() {

	//Check if comms menu is up
	if (Player->flags & PLAYER_FLAGS_MSG_MODE)
	{
		//move up
		--Selected_menu_item;
		Display_selector = true;

		//play scrolling sound and reset the comms window timeout timer, so the window doesn't disappear while we select our item
		gamesnd_play_iface(InterfaceSounds::SCROLL);
		Msg_mode_timestamp = _timestamp(DEFAULT_MSG_TIMEOUT);

		//Move to previous page if it exists
		if (Selected_menu_item < 0 && First_menu_item > 0)
		{
			hud_squadmsg_page_up();
			Selected_menu_item = MAX_MENU_DISPLAY - 1; //if we're moving to previous page in the first place, we assume it was already populated to the max
		}

		//Select the last menu item if we went outside items range, so we can loop around
		else if (Selected_menu_item < 0) 
		{
			//Assuming MAX_MENU_DISPLAY = 10, set First_menu_item to the nearest lower multiple of 10
			//So if we have 85 items in comms menu, looping back from 1st page to last would set First_menu_item to 80
			//exactly like pageUp/pageDown does
			First_menu_item = ((Num_menu_items - 1) / MAX_MENU_DISPLAY) * MAX_MENU_DISPLAY;
			Selected_menu_item = Num_menu_items - 1 - First_menu_item;
		}
	}
}

//function that tricks hud_squadmsg_get_key() into thinking player selected a menu item with a num key press
//Yes, this is a pretty much a hack, but it's simple and works with every squadmsg type.
void hud_squadmsg_selection_select() {
	
	//Check if comms menu is up
	if (Player->flags & PLAYER_FLAGS_MSG_MODE)
	{
		//Check if selected option is even active
		if ((MsgItems[Selected_menu_item + First_menu_item].active > 0))
		{
			Msg_key_used = 1;
			Msg_key = Selected_menu_item + 2;	  //+1 because menu items on actual menu start from 1, not 0
												  //Another +1 because methods that use this later do -1. I'm not sure why they do that, but it works
			Selected_menu_item = 0; //Reset this value, so the position will reset at the next window
		}
		else
		{
			gamesnd_play_iface(InterfaceSounds::GENERAL_FAIL);
		}
	}
}

int hud_squadmsg_get_total_keys()
{
	int num_keys_used;

	num_keys_used = MAX_KEYS_NO_SCROLL;
	if ( Num_menu_items > MAX_MENU_DISPLAY )
		num_keys_used = MAX_KEYS_USED;

	return num_keys_used;
}

// function called from high level keyboard read code to give the squadmsg code a key.
// return 1 is the key was used by the messaging code, 0 otherwise
int hud_squadmsg_read_key( int k )
{
	int i, key_found, num_keys_used;

	num_keys_used = hud_squadmsg_get_total_keys();

	if ( !(Player->flags & PLAYER_FLAGS_MSG_MODE) ) {
		// check to see if any messaging keys are still down for some length of time
		// after messaging is over.  Return true for a while.
		if ( !timestamp_elapsed(Msg_eat_key_timestamp) ) {
			for (i = 0; i < num_keys_used; i++ ) {
				if ( key_is_pressed(keys_used[i]) )
					return 1;
			}
		}

		return 0;
	}

	key_found = 0;
	for (i = 0; i < num_keys_used; i++ ) {
		if ( k == keys_used[i] ) {
			if ( key_down_count(k) ) {
				Msg_key = k;
				key_found = 1;
			}

			if ( key_is_pressed(k) ) {
				key_found = 1;
			}

//			key_down_count(k);
			break;
		}
	}

	if ( key_found )
		return 1;

	return 0;
}

// function which reads the keyboard array and determines if a menu key has been hit
int hud_squadmsg_get_key()
{
	int k, i, num_keys_used;

	if ( Msg_key == -1)
		return -1;

	bool lua_selected = Msg_key_set_from_scripting;
	k = Msg_key;

	Msg_key = -1;
	Msg_key_set_from_scripting = false;

	num_keys_used = hud_squadmsg_get_total_keys();

	// if the emp effect is active, never accept keypresses
	if(emp_active_local()){
		return -1;
	}

	if (lua_selected) {
		return k;
	}

	for ( i = 0; i < num_keys_used; i++ ) {
		if ( k == keys_used[i] ) {
			Msg_key_used = 1;						// this variable will extend the timer

			// use a timestamp to prevent top level key code from possibly reprocessing this key
			Msg_eat_key_timestamp = timestamp(MSG_KEY_EAT_TIME);
			if ( k == KEY_PAGEDOWN ) {			// pageup and pagedown scroll the menu -- deal with these separately!!
				hud_squadmsg_page_down();
				return -1;
			} else if ( k == KEY_PAGEUP ) {
				hud_squadmsg_page_up();
				return -1;
			} else if ( k == KEY_ESC ) {
				hud_squadmsg_toggle();
				return -1;
			} else if ( (i < Num_menu_items) && (Squad_msg_mode == SM_MODE_REINFORCEMENTS) )		// return any key if selecting reinforcement
				return i;

			// play general fail sound if inactive item hit.
			else if ((i + First_menu_item < Num_menu_items) && (MsgItems[i + First_menu_item].active == 0)) {
				gamesnd_play_iface(InterfaceSounds::GENERAL_FAIL);
			}

			else if ((i + First_menu_item < Num_menu_items) && (MsgItems[i + First_menu_item].active > 0)) {	// only return keys that are associated with menu items
				return i + First_menu_item;
			}

			else {
				Msg_key_used = 0;					// if no #-key pressed for visible item, break and allow timer to 
				break;								// to continue as if no key was pressed
			}
		}
	}

	return -1;
}

// function to return true or false if the given ship can rearm, or be repaired
int hud_squadmsg_can_rearm( ship *shipp )
{
	// player ships which turns traitor cannot rearm
	if ( (shipp == Player_ship) && (Player_ship->team == Iff_traitor) )
		return 0;

	// 5/6/98 -- MWA  Decided to always be able to call in support.
	return 1;
}

// calls for repair/rearm of the player ship.  Checks for the presence of the support
// ship and does the appropriate action if found
void hud_squadmsg_repair_rearm( int toggle_state, object *objp)
{
	int robjnum;
	object *robjp;
	object *tobj;
	int multi_player_num;
	int multi_player_team; 

	// this is essentially a check for multiplayer server/client mode
	// in multiplayer mode, the server may have to issue this command when received from a client
	if(objp == NULL) {
		tobj = Player_obj;
		multi_player_num = -1;
		multi_player_team = -1;
	} else {
		tobj = objp;
		multi_player_num = multi_find_player_by_object(objp);
		Assert(multi_player_num != -1);
		if (multi_player_num != -1) {
			multi_player_team = Net_players[multi_player_num].p_info.team;
		}
		else {
			multi_player_team = -1;
		}
	}

	// see if player is already scheduled on arriving support ship.  If so, issues appripriate
	// message and bail
	if ( is_support_allowed(tobj) ) {
		if ( mission_is_repair_scheduled( tobj ) ) {
			message_send_builtin(MESSAGE_ALREADY_ON_WAY, nullptr, nullptr, multi_player_num, multi_player_team);
		} else {
			robjnum = hud_support_find_closest( tobj );
			if ( robjnum != -1 ) {
				message_send_builtin(MESSAGE_ALREADY_ON_WAY, &Ships[Objects[robjnum].instance], nullptr, multi_player_num, multi_player_team);
			} else {
				// request a rearm.  Next function returns -1 if ship is warping in, objnum of repair ship otherwise
				robjnum = ai_issue_rearm_request( tobj );
				if ( robjnum != -1) {
					robjp = &Objects[robjnum];
					message_send_builtin(MESSAGE_ON_WAY, &Ships[robjp->instance], nullptr, multi_player_num, multi_player_team);

				} else {
					// if we are in this part of the if statement, a support ship has been warped in to
					// service us.  Issue appropriate message
					message_send_builtin(MESSAGE_REARM_WARP, nullptr, nullptr, multi_player_num, multi_player_team);
				}

				mission_log_add_entry(LOG_PLAYER_CALLED_FOR_REARM, Ships[tobj->instance].ship_name, NULL);
			}
		}
	}

	//if ( multi_player_num == -1 )		// only do the hud display if it is for me!
	//	hud_support_view_start();

	if ( toggle_state )
		hud_squadmsg_toggle();						// take us out of message mode
}

// function which gets called from keyboard code to issues a shortcut command for rearming.
void hud_squadmsg_rearm_shortcut()
{
	if ( !hud_squadmsg_can_rearm(Player_ship) )
		return;

	// multiplayer clients need to send this message to the server
	if ( MULTIPLAYER_CLIENT ) {
		send_player_order_packet(SQUAD_MSG_SHIP, 0, REARM_REPAIR_ME_ITEM);
		return;
	}

	hud_squadmsg_repair_rearm(0);
}

// code which is called when a player aborts his rearm request
void hud_squadmsg_repair_rearm_abort( int toggle_state, object *obj)
{
//	ai_info *aip;
//	object *robjp;
	object *tobj;

	// this is essentially a check for multiplayer server/client mode
	// in multiplayer mode, the server may have to issue this command when received from a client
	if(obj == NULL)
		tobj = Player_obj;
	else
		tobj = obj;

	// try to abort the request.  We shoudln't be in this function unless we are actually
	// queued for repair.  Send a message from support ship if the support ship is in the mission
	ai_abort_rearm_request( tobj );

	// move the next statements outside of the above if-statement.  Seems like this place
	// is the right place, since we want to change state of the messaging system regardless
	// of what happened above.
	if ( toggle_state )
		hud_squadmsg_toggle();						// take us out of message mode
}

// Goober5000 - redone and added a bit
// returns true if an order is valid for a ship.  Applies to things like departure when engines are blown, etc.
bool hud_squadmsg_ship_order_valid( int shipnum, int order )
{
	// Goober5000
	Assert( shipnum >= 0 && shipnum < MAX_SHIPS );
	ship *shipp = &Ships[shipnum];

	switch ( order )
	{
		case DEPART_ITEM:
			// disabled ships can't depart.
			if (shipp->flags[Ship::Ship_Flags::Disabled])
				return false;

			// Goober5000: also can't depart if no subspace drives and no valid mothership
			if ( !(ship_can_warp_full_check(shipp)) )
			{
				// function accounts for ship's wing and returns true if ship has bay departure and mothership present, false otherwise
				return ship_can_bay_depart(shipp);
			}

			break;
	}
	return true;
}

// returns true or false if the Players target is valid for the given order
// find_order is true when we need to search the comm_orders array for the order entry.  We have
// to do this action in some cases since all we know is the actual "value" of the order
bool hud_squadmsg_is_target_order_valid(size_t order, ai_info *aip, bool isWing )
{
	int target_objnum;
	ship *shipp, *ordering_shipp;
	object *objp;

	if ( aip == NULL )
		aip = Player_ai;

	target_objnum = aip->target_objnum;
	ordering_shipp = &Ships[aip->shipnum];

	//If it's a lua order, defer to luaai
	if (Player_orders[order].lua_id != -1) {
		return ai_lua_is_valid_ship(Player_orders[order].lua_id, isWing, ordering_shipp) && ai_lua_is_valid_target(Player_orders[order].lua_id, target_objnum, ordering_shipp, order);
	}

	// orders which don't operate on targets are always valid
	if ( target_messages.count(order) == 0 )
		return true;


	// order isn't valid if there is no player target
	if ( target_objnum == -1 ) {
		return false;
	}

	objp = &Objects[target_objnum];


	// target isn't a ship, then return false
	if ( (objp->type != OBJ_SHIP) && (objp->type != OBJ_WEAPON) )
		return false;

	// if it's a weapon, then it needs to be a WIF_BOMB weapon.  Only attack order valid, and only
	// valid on bombs not on the player's team
	// MageKing17: Now also works on WIF3_FIGHTER_INTERCEPTABLE weapons.
	if ( objp->type == OBJ_WEAPON ) {
		
		if (Weapons[objp->instance].lssm_stage==3){
			return false;
		}
		
		if ( order == ATTACK_TARGET_ITEM
			&& ((Weapon_info[Weapons[objp->instance].weapon_info_index].wi_flags[Weapon::Info_Flags::Bomb]) || (Weapon_info[Weapons[objp->instance].weapon_info_index].wi_flags[Weapon::Info_Flags::Fighter_Interceptable]))
			&& (Weapons[objp->instance].team != ordering_shipp->team) ) {
			return true;
		}

		return false;
	}

	Assert( objp->type == OBJ_SHIP );

	shipp = &Ships[objp->instance];

	// if target is a navbouy, return false
	if ( Ship_info[shipp->ship_info_index].flags[Ship::Info_Flags::Navbuoy] ){
		return false;
	}

	// if we are messaging a ship, and that ship is our target, no target type orders are ever active
	if ( (Squad_msg_mode == SM_MODE_SHIP_COMMAND) && (Objects[target_objnum].instance == Msg_instance) ){
		return false;
	}

	// check if this order can be issued against the target
	if (shipp->orders_allowed_against.find(order) == shipp->orders_allowed_against.end()) {
		return false;
	}

	// if the order is a disable order or depart, and the ship is disabled, order isn't active
	if ( (order == DISABLE_TARGET_ITEM) && (shipp->flags[Ship::Ship_Flags::Disabled]) ){
		return false;
	}

	// same as above except for disarmed.
	if ( (order == DISARM_TARGET_ITEM) && ((shipp->subsys_info[SUBSYSTEM_TURRET].type_count == 0) || (shipp->subsys_info[SUBSYSTEM_TURRET].aggregate_current_hits <= 0.0f)) ){
		return false;
	}

	// if order is disable subsystem, and no subsystem targeted or no hits, then order not valid
	if ( (order == DISABLE_SUBSYSTEM_ITEM) && ((aip->targeted_subsys == nullptr) || (aip->targeted_subsys->current_hits <= 0.0f)) ){
		return false;
	}

	// check based on target's and player's team
	if ( (shipp->team == ordering_shipp->team) && (friendly_target_messages.count(order) > 0) ){
		return true;
	} else if ( (shipp->team != ordering_shipp->team) && (enemy_target_messages.count(order) > 0) ){
		return true;
	} else {
		return false;
	}
}

const SCP_string& hud_squadmsg_get_order_name(int command)
{
	Assertion(SCP_vector_inbounds(Player_orders, command), "Order does not exist! Get a coder!");

	return Player_orders[command].hud_name;
}

scripting::api::lua_enum hud_squadmsg_get_order_scripting_enum(int command)
{
	Assertion(SCP_vector_inbounds(Player_orders, command), "Order does not exist! Get a coder!");

	switch (command) {
	case ATTACK_TARGET_ITEM:
		return scripting::api::LE_SQUAD_MESSAGE_ATTACK_TARGET;
	case DISABLE_TARGET_ITEM:
		return scripting::api::LE_SQUAD_MESSAGE_DISABLE_TARGET;
	case DISARM_TARGET_ITEM:
		return scripting::api::LE_SQUAD_MESSAGE_DISARM_TARGET;
	case PROTECT_TARGET_ITEM:
		return scripting::api::LE_SQUAD_MESSAGE_PROTECT_TARGET;
	case IGNORE_TARGET_ITEM:
		return scripting::api::LE_SQUAD_MESSAGE_IGNORE_TARGET;
	case FORMATION_ITEM:
		return scripting::api::LE_SQUAD_MESSAGE_FORMATION;
	case COVER_ME_ITEM:
		return scripting::api::LE_SQUAD_MESSAGE_COVER_ME;
	case ENGAGE_ENEMY_ITEM:
		return scripting::api::LE_SQUAD_MESSAGE_ENGAGE_ENEMY;
	case CAPTURE_TARGET_ITEM:
		return scripting::api::LE_SQUAD_MESSAGE_CAPTURE_TARGET;
	case REARM_REPAIR_ME_ITEM:
		return scripting::api::LE_SQUAD_MESSAGE_REARM_REPAIR_ME;
	case ABORT_REARM_REPAIR_ITEM:
		return scripting::api::LE_SQUAD_MESSAGE_ABORT_REARM_REPAIR;
	case STAY_NEAR_ME_ITEM:
		return scripting::api::LE_SQUAD_MESSAGE_STAY_NEAR_ME;
	case STAY_NEAR_TARGET_ITEM:
		return scripting::api::LE_SQUAD_MESSAGE_STAY_NEAR_TARGET;
	case KEEP_SAFE_DIST_ITEM:
		return scripting::api::LE_SQUAD_MESSAGE_KEEP_SAFE_DIST;
	case DEPART_ITEM:
		return scripting::api::LE_SQUAD_MESSAGE_DEPART;
	case DISABLE_SUBSYSTEM_ITEM:
		return scripting::api::LE_SQUAD_MESSAGE_DISABLE_SUBSYSTEM;
	default:
		return scripting::api::LE_SQUAD_MESSAGE_LUA_AI;
	}
}

// Run the order issued hook. When an order is issued we first check if we should override. If so, then we run the hook and return true
// which will skip the rest of the order code.
bool hud_squadmsg_run_order_issued_hook(int command, ship* sendingShip, ship* recipientShip, wing* recipientWing, ship* targetShip, ship_subsys* subsys)
{
	bool isOverride = false;

	if (scripting::hooks::OnHudCommOrderIssued->isActive()) {
		object_ship_wing_point_team recipient;

		if (recipientShip != nullptr) {
			recipient = recipientShip;
		} else if (recipientWing != nullptr) {
			recipient = recipientWing;
		}

		auto sendingObject = (sendingShip == nullptr) ? nullptr : &Objects[sendingShip->objnum];
		auto targetObject = (targetShip == nullptr) ? nullptr : &Objects[targetShip->objnum];

		auto paramList = scripting::hook_param_list(
				scripting::hook_param("Sender", 'o', sendingObject),
				scripting::hook_param("Recipient", 'o', scripting::api::l_OSWPT.Set(recipient)),
				scripting::hook_param("Target", 'o', targetObject),
				scripting::hook_param("Subsystem", 'o', scripting::api::l_Subsystem.Set(scripting::api::ship_subsys_h(targetObject, subsys))),
				scripting::hook_param("Order", 'o', scripting::api::l_Enum.Set(scripting::api::enum_h(hud_squadmsg_get_order_scripting_enum(command)))),
				scripting::hook_param("Name", 's', hud_squadmsg_get_order_name(command).c_str())
			);
		if (scripting::hooks::OnHudCommOrderIssued->isOverride(
				scripting::hooks::CommOrderConditions{sendingShip, targetObject, &recipient},
				paramList)) {
			isOverride = true;
		}
		scripting::hooks::OnHudCommOrderIssued->run(
			scripting::hooks::CommOrderConditions{sendingShip, targetObject, &recipient},
			paramList);
	}

	return isOverride;
}

// function to send an order to all fighters/bombers.
void hud_squadmsg_send_to_all_fighters( int command, int player_num )
{
	ai_info *aip;
	ship *shipp, *ordering_shipp;
	int i, send_message;

	// quick short circuit here because of actually showing comm menu even though you cannot message.
	// just a safety net.
	if ( (Game_mode & GM_MULTIPLAYER) && (player_num != -1) ) {
		if ( !multi_can_message(&Net_players[player_num]) ) {
			return;
		}
	}

	// check for multiplayer mode
	if(MULTIPLAYER_CLIENT) {
		send_player_order_packet(SQUAD_MSG_ALL, 0, command);
		return;
	}

	send_message = 1;									// internal flag to dictate who sends message
	aip = Player_ai;

	if ( player_num != -1 )
		aip = &Ai_info[Ships[Objects[Net_players[player_num].m_player->objnum].instance].ai_index];

	Assert( aip->shipnum != -1 );
	ordering_shipp = &Ships[aip->shipnum];

	/* Goober5000 - this relies on the weird calling logic that is now disabled
	if ( command == IGNORE_TARGET_ITEM ) {
		// if we were messaging a ship directly, set flag to send no messages.  We will send one
		// specifically from the ship player is ordering
		if ( (Msg_instance != MESSAGE_ALL_FIGHTERS) && (Squad_msg_mode == SM_MODE_SHIP_COMMAND) ) {
			do_ship = 1;
			send_message = 0;
		}
	}
	*/

	for ( i = 0; i < Num_wings; i++ ) {
		int shipnum;

		if ( (Wings[i].flags[Ship::Wing_Flags::Gone]) || (Wings[i].current_count == 0) )
			continue;

		if ( Wings[i].flags[Ship::Wing_Flags::Departing] )
			continue;

		// get the first ship on the wing list and look at its team and then its type
		shipnum = Wings[i].ship_index[Wings[i].special_ship];

		// if special ship isn't valid then just move on
		if (shipnum < 0)
			continue;

		shipp = &Ships[shipnum];

		// can't message if not on players team
		if ( shipp->team != ordering_shipp->team )
			continue;

		// can't message if ship not fighter/bomber if the command isn't to everyone.
		if ( !(Ship_info[Wings[i].special_ship_ship_info_index].is_fighter_bomber()) )
			continue;

		// don't send the command if the "wing" won't accept the command.  We do this by looking at
		// the set of orders accepted for the wing leader
		if (!shipp->orders_accepted.contains(command))
			continue;

		// send the command to the wing
		if ( Wings[i].current_count > 0 ) {
			if (send_message) {
				hud_add_issued_order("All Fighters", command);
				if ( hud_squadmsg_send_wing_command(i, command, send_message, SQUADMSG_HISTORY_UPDATE, player_num) ) {
					send_message = 0;
				}
			}
			else {
				hud_squadmsg_send_wing_command(i, command, send_message, SQUADMSG_HISTORY_NO_UPDATE, player_num);
			}
		}
	}

	// now find any friendly fighter/bomber ships not in wings
	for (auto so: list_range(&Ship_obj_list)) {
		auto objp = &Objects[so->objnum];
		if (objp->flags[Object::Object_Flags::Should_be_dead])
			continue;
		if ( objp->type != OBJ_SHIP )
			continue;

		// don't send messge to ships not on player's team, or that are in a wing.
		shipp = &Ships[objp->instance];
		if ( (shipp->team != ordering_shipp->team) || (shipp->wingnum != -1) )
			continue;

		// don't send message to non fighter wings
		if ( !(Ship_info[shipp->ship_info_index].is_fighter_bomber()) )
			continue;

		// skip departing/dying ships
		if ( shipp->is_dying_or_departing() )
			continue;

		// don't send command if ship won't accept it
		if (!shipp->orders_accepted.contains(command))
			continue;

		if (send_message) {
			hud_add_issued_order("All Fighters", command);
			if ( hud_squadmsg_send_ship_command(objp->instance, command, send_message, SQUADMSG_HISTORY_UPDATE, player_num) ) {
				send_message = 0;
			}
		}
		else {
			hud_squadmsg_send_ship_command(objp->instance, command, send_message, SQUADMSG_HISTORY_NO_UPDATE, player_num);
		}
	}

	// we might send the ship command again if we are ignoring a target, and the guy
	// we ordered directly is a ship -- we want the response to come directly from the
	// guy we orders
	/* Goober5000 - yet again with the weird logic
	if ( do_ship ) {
		Assert( Msg_instance != MESSAGE_ALL_FIGHTERS );
		hud_squadmsg_send_ship_command( Msg_instance, command, 1 );
	}
	*/
}

// Check if any enemy ships are in the mission
int hud_squadmsg_enemies_present()
{
	ship		*shipp;
	ship_obj *so;

	for ( so = GET_FIRST(&Ship_obj_list); so != END_OF_LIST(&Ship_obj_list); so = GET_NEXT(so) ) {
		if (Objects[so->objnum].flags[Object::Object_Flags::Should_be_dead])
			continue;

		shipp = &Ships[Objects[so->objnum].instance];
		if ( shipp->team != Player_ship->team )
			return 1;
	}

	return 0;
}

inline bool override_protect_ship_type(ship_info* sip) {
    return sip->is_fighter_bomber() || sip->flags[Ship::Info_Flags::Freighter] || sip->flags[Ship::Info_Flags::Transport];
}

int enemy_message(int message) {
	return hud_squadmsg_enemies_present() ? message : MESSAGE_YESSIR;
}

// function which sends a message to a specific ship.  This routine can be called from one of two
// places.  Either after selecting a ship when using a hotkey, or after selecting a command when
// using the entire messaging menu system
//
// if local and addr are non-null, it means the function is being called by the (multiplayer) server in response to 
// a PLAYER_COMMAND_PACKET
int hud_squadmsg_send_ship_command( int shipnum, int command, int send_message, int update_history, int player_num )
{
	ai_info *ainfo;
	ai_goal_mode ai_mode;			// ai mode...
	int ai_submode;					// ...and submode needed for ship commands
	ship *target = nullptr;
	char *target_shipname;
	int int_data = 0;
	float float_data = 0.0f;
	ai_lua_parameters lua_target;
	int message;
	int target_team, ship_team;				// team id's for the ship getting message and any target the player has
	ship *ordering_shipp;

	int special_index = -1;
	int message_team_filter = -1;
	
	// quick short circuit here because of actually showing comm menu even though you cannot message.
	// just a safety net.
	if ( (Game_mode & GM_MULTIPLAYER) && (player_num != -1) ) {
		if ( !multi_can_message(&Net_players[player_num]) ) {
			return 0;
		}
	}

	// check for multiplayer mode
	if(MULTIPLAYER_CLIENT){
		send_player_order_packet(SQUAD_MSG_SHIP, shipnum, command);
		return 0;
	}

	ai_mode = AI_GOAL_NONE;							// needs to be initialized
	ai_submode = -1234567;
	ainfo = Player_ai;

	if ( player_num != -1 ){
		ainfo = &Ai_info[Ships[Objects[Net_players[player_num].m_player->objnum].instance].ai_index];
		message_team_filter = Net_players[player_num].p_info.team; 
	}

	Assert( ainfo->shipnum != -1 );
	ordering_shipp = &Ships[ainfo->shipnum];

	// a shortcut to save on repetitive coding.  If the order is a 'target' order, make the default
	// mesage be "no target"
	message = MESSAGE_NOSIR;
	if (target_messages.count((size_t)command) > 0 && (ainfo->target_objnum == -1)) {
		message = MESSAGE_NO_TARGET;
	}

	if (hud_squadmsg_is_target_order_valid((size_t) command, ainfo)) {

		target_shipname = nullptr;
		target_team = -1;
		if (ainfo->target_objnum != -1) {
			if (Objects[ainfo->target_objnum].type == OBJ_SHIP) {
				if (ainfo->target_objnum != Ships[shipnum].objnum) {
					target = &Ships[Objects[ainfo->target_objnum].instance];
					target_shipname = Ships[Objects[ainfo->target_objnum].instance].ship_name;
					target_team = Ships[Objects[ainfo->target_objnum].instance].team;
				}
			}
		}

		if (hud_squadmsg_run_order_issued_hook(command, ordering_shipp, &Ships[shipnum], nullptr, target, ainfo->targeted_subsys)) {
			return 0; // No message was sent
		}

		Assert ( ainfo->shipnum != -1 );
		ship_team = Ships[ainfo->shipnum].team;		// team of the ship issuing the message

		switch (command) {
		case ATTACK_TARGET_ITEM:
			if ( Objects[ainfo->target_objnum].type == OBJ_SHIP ) {
				Assert( target_shipname );
				Assert( ship_team != target_team );

				// Orders to override protect
				if (override_protect_ship_type(&Ship_info[Ships[Objects[ainfo->target_objnum].instance].ship_info_index])) {
    			Objects[ainfo->target_objnum].flags.remove(Object::Object_Flags::Protected);
				}


				ai_mode = AI_GOAL_CHASE;
				ai_submode = SM_ATTACK;
			} else if ( Objects[ainfo->target_objnum].type == OBJ_WEAPON ) {
				ai_mode = AI_GOAL_CHASE_WEAPON;
				ai_submode = Objects[ainfo->target_objnum].instance;			// store the instance of the weapon -- ai goals code will deal with it
			} else {
				Int3();
			}
			message = MESSAGE_ATTACK_TARGET;
			break;

		case DISABLE_TARGET_ITEM:
			Assert( target_shipname );
			Assert( ship_team != target_team );

			// Orders to override protect
			if (override_protect_ship_type(&Ship_info[Ships[Objects[ainfo->target_objnum].instance].ship_info_index])) {
				Objects[ainfo->target_objnum].flags.remove(Object::Object_Flags::Protected);
			}

			ai_mode = (The_mission.ai_profile->flags[AI::Profile_Flags::Hudsquadmsg_tactical_disarm_disable]) ? AI_GOAL_DISABLE_SHIP_TACTICAL : AI_GOAL_DISABLE_SHIP;
			ai_submode = -SUBSYSTEM_ENGINE;
			message = MESSAGE_DISABLE_TARGET;
			break;

		case DISARM_TARGET_ITEM:
			Assert( target_shipname );
			Assert( ship_team != target_team );

			// Orders to override protect
			if (override_protect_ship_type(&Ship_info[Ships[Objects[ainfo->target_objnum].instance].ship_info_index])) {
				Objects[ainfo->target_objnum].flags.remove(Object::Object_Flags::Protected);
			}

			ai_mode = (The_mission.ai_profile->flags[AI::Profile_Flags::Hudsquadmsg_tactical_disarm_disable]) ? AI_GOAL_DISARM_SHIP_TACTICAL : AI_GOAL_DISARM_SHIP;
			ai_submode = -SUBSYSTEM_TURRET;
			message = MESSAGE_DISARM_TARGET;
			break;

		case DISABLE_SUBSYSTEM_ITEM:
			Assert( target_shipname );
			Assert( ship_team != target_team );
			Assert( ainfo->targeted_subsys != NULL );
			Assert( ainfo->targeted_subsys->current_hits > 0.0f);

			// Orders to override protect
			if (override_protect_ship_type(&Ship_info[Ships[Objects[ainfo->target_objnum].instance].ship_info_index])) {
				Objects[ainfo->target_objnum].flags.remove(Object::Object_Flags::Protected);
			}

			ai_mode = AI_GOAL_DESTROY_SUBSYSTEM;
			ai_submode = ship_find_subsys( &Ships[Objects[ainfo->target_objnum].instance], ainfo->targeted_subsys->system_info->subobj_name );
			special_index = ai_submode; 
			message = MESSAGE_ATTACK_SUBSYSTEM;
			break;

		case CAPTURE_TARGET_ITEM:
			Assert( target_shipname );
			Assert( ship_team != target_team );

			Assert(ainfo->target_objnum > -1);

			Objects[ainfo->target_objnum].flags.set(Object::Object_Flags::Protected);
            
      ai_mode = AI_GOAL_DOCK;
			ai_submode = AIS_DOCK_0;
			message = MESSAGE_DOCK_YES;
			break;

		case PROTECT_TARGET_ITEM:

			// AL 31-3-98: Can't protect self... this can happen if all fighters
			//					are told to protect another friendly ship
			if ( ainfo->target_objnum == Ships[shipnum].objnum ) {
				return 0;
			}

			Assert( target_shipname );
			Assert( ship_team == target_team );

			ai_mode = AI_GOAL_GUARD;
			ai_submode = AIS_GUARD_PATROL;
			message = enemy_message(MESSAGE_PROTECT_TARGET);
			break;

		case IGNORE_TARGET_ITEM:
			Assert( target_shipname );
			Assert( ship_team != target_team );

			ai_mode = AI_GOAL_IGNORE_NEW;
			ai_submode = 0;
			message = MESSAGE_IGNORE;
			break;
		
		case FORMATION_ITEM:
			message = MESSAGE_FORM_ON_MY_WING;
			target_shipname = ordering_shipp->ship_name;
			ai_mode = AI_GOAL_FORM_ON_WING;
			ai_submode = 0;
			break;
		
		case COVER_ME_ITEM:
			ai_mode = AI_GOAL_GUARD;
			ai_submode = AIS_GUARD_PATROL;
			target_shipname = ordering_shipp->ship_name;
			message = enemy_message(MESSAGE_COVER_ME);
			break;
		
		case ENGAGE_ENEMY_ITEM:
			ai_mode = AI_GOAL_CHASE_ANY;
			ai_submode = SM_ATTACK;
			message = enemy_message(MESSAGE_ENGAGE);
			target_shipname = nullptr;
			break;
		
		case DEPART_ITEM:
			ai_mode = AI_GOAL_WARP;
			ai_submode = -1;
			message = MESSAGE_WARP_OUT;
      Ships[shipnum].flags.set(Ship::Ship_Flags::Departure_ordered);
			break;
		
		// the following are support ship options!!!
		case REARM_REPAIR_ME_ITEM:		
			if (MULTIPLAYER_MASTER && (player_num != -1)) {
				hud_squadmsg_repair_rearm(0,&Objects[Net_players[player_num].m_player->objnum]);
			} else {
				hud_squadmsg_repair_rearm(0);				// note we return right away.  repair/rearm code handles messaging, etc
			}
			// add the order to the squad message history
			hud_add_issued_order(Ships[shipnum].ship_name, command);
			hud_update_last_order(ordering_shipp->ship_name, player_num, special_index); 
			return 0;
		
		case ABORT_REARM_REPAIR_ITEM:
			if (MULTIPLAYER_MASTER && (player_num != -1)) {
				hud_squadmsg_repair_rearm_abort(0,&Objects[Net_players[player_num].m_player->objnum]);
			} else {
				hud_squadmsg_repair_rearm_abort(0);		// note we return right away.  repair/rearm code handles messaging, etc
			}
			// add the order to the squad message history
			hud_add_issued_order(Ships[shipnum].ship_name, command);
			hud_update_last_order(ordering_shipp->ship_name, player_num, special_index); 
			return 0;
		
		case STAY_NEAR_ME_ITEM:
		case STAY_NEAR_TARGET_ITEM:
			ai_mode = AI_GOAL_STAY_NEAR_SHIP;
			ai_submode = -1;
			float_data = 300.0f;	// distance from target ship
			message = MESSAGE_YESSIR;
			if (command == STAY_NEAR_ME_ITEM) {
				target_shipname = ordering_shipp->ship_name;
			}
			break;
		
		case KEEP_SAFE_DIST_ITEM:
			ai_mode = AI_GOAL_KEEP_SAFE_DISTANCE;
			ai_submode = -1;
			message = MESSAGE_YESSIR;
			break;
		
		default:
			Assert(command < (int) Player_orders.size()); // get Allender -- illegal message
			
			ai_mode = AI_GOAL_LUA;
			ai_submode = Player_orders[command].lua_id;
			auto lua_porder = ai_lua_find_player_order(Player_orders[command].lua_id);
			message = lua_porder->ai_message;
			if (ainfo->target_objnum != -1) {
				lua_target = { object_ship_wing_point_team(&Ships[Objects[ainfo->target_objnum].instance]), {} };
			}
			break;

		}

		// handle case of messaging one ship.  Deal with messaging all fighters next.
		if (ai_mode != AI_GOAL_NONE) {
			Assert(ai_submode != -1234567);
			ai_add_ship_goal_player(ai_goal_type::PLAYER_SHIP, ai_mode, ai_submode, target_shipname, &Ai_info[Ships[shipnum].ai_index], int_data, float_data, lua_target);
			if (update_history == SQUADMSG_HISTORY_ADD_ENTRY) {
				hud_add_issued_order(Ships[shipnum].ship_name, command);
				hud_update_last_order(target_shipname, player_num, special_index); 
			} else if (update_history == SQUADMSG_HISTORY_UPDATE) {
				hud_update_last_order(target_shipname, player_num, special_index);
			}
		}
	}

	// if we're in multiplayer mode, and we're the server, determine if this virtual squadmate order should be
	// sent to other players in the game as an actual "order"
	if ((Game_mode & GM_MULTIPLAYER) && (message != MESSAGE_NOSIR)) {
		// if the multi_msg system processed and sent this order to a player, we should not play a response
		if (multi_msg_eval_ship_squadmsg(shipnum, command, ainfo, player_num)) {
			send_message = 0;
		}
	}
	
	// this is the _response_
	if (send_message && (!(Ships[shipnum].flags[Ship::Ship_Flags::No_builtin_messages]))) {
		message_send_builtin(message, &Ships[shipnum], target, player_num, message_team_filter);
	}
	
	return send_message;
}

// function to send a command to a wing.  Like above function, called from one of two places
//
// if local and addr are non-null, it means the function is being called by the (multiplayer) server in response to 
// a PLAYER_COMMAND_PACKET
//
// returns whether or not a message was sent
int hud_squadmsg_send_wing_command( int wingnum, int command, int send_message, int update_history, int player_num )
{
	ai_info *ainfo;
	ai_goal_mode ai_mode;			// ai mode...
	int ai_submode;					// ...and submode needed for ship commands
	ship *target = nullptr;
	char *target_shipname;
	int int_data = 0;
	float float_data = 0.0f;
	ai_lua_parameters lua_target;
	int message_sent, message;
	int target_team, wing_team;				// team for the wing and the player's target
	ship *ordering_shipp;

	int special_index = -1;
	int message_team_filter = -1;

	// quick short circuit here because of actually showing comm menu even though you cannot message.
	// just a safety net.
	if ((Game_mode & GM_MULTIPLAYER) && (player_num != -1)) {
		if (!multi_can_message(&Net_players[player_num])) {
			return 0;
		}
	}

	// check for multiplayer mode
	if (MULTIPLAYER_CLIENT) {
		send_player_order_packet(SQUAD_MSG_WING, wingnum, command);
		return 0;
	}

	ai_mode = AI_GOAL_NONE;							// needs to be initialized
	ai_submode = -1234567;
	ainfo = Player_ai;

	if (player_num != -1) {
		ainfo = &Ai_info[Ships[Objects[Net_players[player_num].m_player->objnum].instance].ai_index];
		message_team_filter = Net_players[player_num].p_info.team; 
	}

	Assert(ainfo->shipnum != -1);
	ordering_shipp = &Ships[ainfo->shipnum];

	// a shortcut to save on repetative coding
	message = MESSAGE_NOSIR;
	if ((target_messages.count((size_t)command) > 0) && (ainfo->target_objnum == -1)) {
		message = MESSAGE_NO_TARGET;
	}

	if (hud_squadmsg_is_target_order_valid((size_t)command, ainfo, true)) {
		target_shipname = nullptr;
		target_team = -1;
		if (ainfo->target_objnum != -1) {
			if (Objects[ainfo->target_objnum].type == OBJ_SHIP) {
				target = &Ships[Objects[ainfo->target_objnum].instance];
				target_shipname = Ships[Objects[ainfo->target_objnum].instance].ship_name;
				target_team = Ships[Objects[ainfo->target_objnum].instance].team;
			}
		}

		Assert (ainfo->shipnum != -1);
		Assert ((wingnum >= 0) && (wingnum < Num_wings));

		// get the team for the wing
		Assert (Wings[wingnum].ship_index[0] != -1);
		wing_team = Ships[Wings[wingnum].ship_index[0]].team;

		if (hud_squadmsg_run_order_issued_hook(command, ordering_shipp, nullptr, &Wings[wingnum], target, ainfo->targeted_subsys)) {
			return 0; // No message was sent
		}

		switch (command) {
		case ATTACK_TARGET_ITEM:
			if (Objects[ainfo->target_objnum].type == OBJ_SHIP) {
				Assert(target_shipname);
				Assert(wing_team != target_team);
				if ((Ships[Objects[ainfo->target_objnum].instance].wingnum != -1) && (Ships[Objects[ainfo->target_objnum].instance].wingnum == wingnum)) {
					message = MESSAGE_NOSIR;
					ai_mode = AI_GOAL_NONE;
				} else {
					ai_mode = AI_GOAL_CHASE;
					ai_submode = SM_ATTACK;
					message = MESSAGE_ATTACK_TARGET;
				}
			} else if (Objects[ainfo->target_objnum].type == OBJ_WEAPON) {
				ai_mode = AI_GOAL_CHASE_WEAPON;
				ai_submode = Objects[ainfo->target_objnum].instance;			// store the instance of the weapon -- ai goals code will deal with it
				message = MESSAGE_ATTACK_TARGET;
			} else {
				Int3();
			}
			break;

		case DISABLE_TARGET_ITEM:
			Assert(target_shipname);
			Assert(wing_team != target_team);

			ai_mode = (The_mission.ai_profile->flags[AI::Profile_Flags::Hudsquadmsg_tactical_disarm_disable]) ? AI_GOAL_DISABLE_SHIP_TACTICAL : AI_GOAL_DISABLE_SHIP;
			ai_submode = -SUBSYSTEM_ENGINE;
			message = MESSAGE_DISABLE_TARGET;
			break;

		case DISARM_TARGET_ITEM:
			Assert(target_shipname);
			Assert(wing_team != target_team);

			ai_mode = (The_mission.ai_profile->flags[AI::Profile_Flags::Hudsquadmsg_tactical_disarm_disable]) ? AI_GOAL_DISARM_SHIP_TACTICAL : AI_GOAL_DISARM_SHIP;
			ai_submode = -SUBSYSTEM_TURRET;
			message = MESSAGE_DISARM_TARGET;
			break;

		case DISABLE_SUBSYSTEM_ITEM:
			Assert(target_shipname);
			Assert(wing_team != target_team);
			Assert(ainfo->targeted_subsys != NULL);
			Assert(ainfo->targeted_subsys->current_hits > 0.0f);

			ai_mode = AI_GOAL_DESTROY_SUBSYSTEM;
			ai_submode = ship_find_subsys(&Ships[Objects[ainfo->target_objnum].instance], ainfo->targeted_subsys->system_info->subobj_name);
			special_index = ai_submode; 
			message = MESSAGE_ATTACK_SUBSYSTEM;
			break;


		case PROTECT_TARGET_ITEM:
			Assert(target_shipname);
			Assert(wing_team == target_team);

			ai_mode = AI_GOAL_GUARD;
			ai_submode = AIS_GUARD_PATROL;
			message = enemy_message(MESSAGE_PROTECT_TARGET);
			break;

		case IGNORE_TARGET_ITEM:
			Assert(target_shipname);
			Assert(wing_team != target_team);

			ai_mode = AI_GOAL_IGNORE_NEW;
			ai_submode = 0;
			message = MESSAGE_IGNORE;
			break;

		case FORMATION_ITEM:
			message = MESSAGE_FORM_ON_MY_WING;
			target_shipname = ordering_shipp->ship_name;
			ai_mode = AI_GOAL_FORM_ON_WING;
			ai_submode = 0;
			break;

		case COVER_ME_ITEM:
			ai_mode = AI_GOAL_GUARD;
			ai_submode = AIS_GUARD_PATROL;
			target_shipname = ordering_shipp->ship_name;
			message = enemy_message(MESSAGE_COVER_ME);
			break;

		case ENGAGE_ENEMY_ITEM:
			ai_mode = AI_GOAL_CHASE_ANY;
			ai_submode = SM_ATTACK;
			message = enemy_message(MESSAGE_ENGAGE);
			target_shipname = NULL;
			break;

		case DEPART_ITEM:
			ai_mode = AI_GOAL_WARP;
			ai_submode = -1;
			message = MESSAGE_WARP_OUT;
      Wings[wingnum].flags.set(Ship::Wing_Flags::Departure_ordered);
			break;

		case REARM_REPAIR_ME_ITEM:
		case ABORT_REARM_REPAIR_ITEM:
		case STAY_NEAR_ME_ITEM:
		case STAY_NEAR_TARGET_ITEM:
		case KEEP_SAFE_DIST_ITEM:
			return 0;

		default: {
			Assert(command < (int) Player_orders.size()); // get Allender -- illegal message

			ai_mode = AI_GOAL_LUA;
			ai_submode = Player_orders[command].lua_id;
			auto lua_porder = ai_lua_find_player_order(Player_orders[command].lua_id);
			message = lua_porder->ai_message;
			if(ainfo->target_objnum != -1)
				lua_target = { object_ship_wing_point_team(&Ships[Objects[ainfo->target_objnum].instance]), {} };
			break;

		}
		}

		if (ai_mode != AI_GOAL_NONE) {
			Assert(ai_submode != -1234567);
			ai_add_wing_goal_player(ai_goal_type::PLAYER_WING, ai_mode, ai_submode, target_shipname, wingnum, int_data, float_data, lua_target);

			if (update_history == SQUADMSG_HISTORY_ADD_ENTRY) {
				hud_add_issued_order(Wings[wingnum].name, command);
				hud_update_last_order(target_shipname, player_num, special_index);
			} else if (update_history == SQUADMSG_HISTORY_UPDATE) {
				hud_update_last_order(target_shipname, player_num, special_index);
			}
		}
	}

	// if we're in multiplayer mode, and we're the server, determine if this virtual squadmate order should be
	// sent to other players in the game as an actual "order"
	if ((Game_mode & GM_MULTIPLAYER) && (message != MESSAGE_NOSIR)) {
		// if there's at least one ai ship which got the command, let the response come through
		if (multi_msg_eval_wing_squadmsg(wingnum, command, ainfo, player_num)) {
			send_message = 0;
		}
	}

	// this is the _response_
	message_sent = 0;
	if (send_message) {
		int ship_num;

		// get a random ship in the wing to send the message to the player		
		ship_num = ship_get_random_ship_in_wing(wingnum, SHIP_GET_UNSILENCED, 0.0, 0, command);
		
		// in multiplayer, its possible that all ships in a wing are players. so we'll just send from a random ship		
		if (ship_num == -1 && (Game_mode & GM_MULTIPLAYER)) {
			ship_num = ship_get_random_ship_in_wing(wingnum, SHIP_GET_ANY_SHIP, 0.0, 0, command);
		}
		
		// only send message if ship is found.  There appear to be cases where all ships
		// in a wing die in the same frame causing the wing to appear valid in the message
		// menu, but the get_random_ship* functions won't return dying ships.
		// Karajorma - No valid ships will be found if all the remaining ships have been silence either. 
		if (ship_num != -1) {
			message_send_builtin(message, &Ships[ship_num], target, player_num, message_team_filter);
			message_sent = 1;
		}
	}

	return message_sent;
}


// return number of available reinforcements, 0 if none available
int hud_squadmsg_reinforcements_available(int team)
{
	int i, count = 0;

	for (i = 0; i < Num_reinforcements; i++) {
		int wingnum;

		// no more left
		if ( Reinforcements[i].num_uses >= Reinforcements[i].uses ){
			continue;
		}

		// incorrect team
		if ( team != ship_get_reinforcement_team(i) ){
			continue;
		}

		//  check the arrival cue sexpression of the ship/wing of this reinforcement.
		// Goober5000 - if it can't arrive, it doesn't count.  This should check
		// for SEXP_FALSE as well as SEXP_KNOWN_FALSE, otherwise you end up with
		// a reinforcement menu containing no valid selections.
		if ( (wingnum = wing_name_lookup(Reinforcements[i].name, 1)) != -1 ) {
			Assert ( Wings[wingnum].arrival_cue >= 0 );
			if ( Sexp_nodes[Wings[wingnum].arrival_cue].value == SEXP_FALSE
				|| Sexp_nodes[Wings[wingnum].arrival_cue].value == SEXP_KNOWN_FALSE ){
				continue;
			}
		} else {
			p_object *p_objp;

			p_objp = mission_parse_get_arrival_ship( Reinforcements[i].name );
			if ( p_objp != NULL ) {
				if ( Sexp_nodes[p_objp->arrival_cue].value == SEXP_FALSE
					|| Sexp_nodes[p_objp->arrival_cue].value == SEXP_KNOWN_FALSE ){
					continue;
				}
			} else {
				Int3();							// allender says bogus!  reinforcement should be here since it wasn't a wing!
				continue;
			}
		}
		count++;
	}

	return count;
}

// function to put up window in upper right to allow for player to select the type
// of entity to select for a message (i.e. a wing or a ship)
void hud_squadmsg_type_select( )
{
	int k, i;

	int num_order_types = NUM_COMM_ORDER_TYPES;

	int lua_order_count = 0;

	// Now get a list of all lua categories to add. Meow.
	SCP_vector<SCP_string> lua_cat_list = ai_lua_get_general_order_categories();

	num_order_types += (int)lua_cat_list.size();

	// Add the items
	for (i = 0; i < num_order_types; i++)
	{
		if (i < NUM_COMM_ORDER_TYPES) {
			MsgItems[i].text = Comm_order_types[i];
		} else {
			MsgItems[i].text = lua_cat_list[i - NUM_COMM_ORDER_TYPES];
		}
		MsgItems[i].active = 1;						// assume active
	}
	Num_menu_items = num_order_types;


	// check to see if the player is a traitor.  If so, then he will not
	// be able to do anything from this menu
	if ((Player_ship != NULL) && (Player_ship->team == Iff_traitor)) {
		for (i = 0; i < MAX_MENU_ITEMS; i++ )
			MsgItems[i].active = 0;
		goto do_main_menu;
	}

	// based on ship counts, wing counts, shortcut active, grey out possible menu choices
	if ( !hud_squadmsg_count_ships(0) )
		MsgItems[TYPE_SHIP_ITEM].active = 0;

	if ( !hud_squadmsg_count_wings(0) )
		MsgItems[TYPE_WING_ITEM].active = 0;

	// check to be sure that we have some fighters/bombers on the players team that we
	// can message
	if ( !hud_squadmsg_exist_fighters() ){
		MsgItems[TYPE_ALL_FIGHTERS_ITEM].active = 0;
	}

	if ((Player_ship != NULL) && !hud_squadmsg_reinforcements_available(Player_ship->team)) {
		MsgItems[TYPE_REINFORCEMENT_ITEM].active = 0;
	}

	MsgItems[TYPE_REPAIR_REARM_ITEM].active = Hide_main_rearm_items_in_comms_gauge ? -1 : 1;
	MsgItems[TYPE_REPAIR_REARM_ABORT_ITEM].active = Hide_main_rearm_items_in_comms_gauge ? -1 : 0;

	for(const auto& cat : lua_cat_list){
		if (ai_lua_get_general_orders(false, false, cat).size() == 0) {
			MsgItems[NUM_COMM_ORDER_TYPES + lua_order_count].active = 0;
		}
		lua_order_count++;
	}

	// AL: 10/13/97
	// If the player ship communications are severely damaged, then the player
	// will only be able to call for repair/rearm ships
	//
	// also, only allow support ship if this player is not allowed to messaage. 
	if ( (hud_communications_state(Player_ship) != COMM_OK) || ((Game_mode & GM_MULTIPLAYER) && !multi_can_message(Net_player)) ) {
		for ( i = 0; i < MAX_MENU_ITEMS; i++ ){
			MsgItems[i].active = 0;
		}

		MsgItems[TYPE_REPAIR_REARM_ITEM].active = Hide_main_rearm_items_in_comms_gauge ? -1 : 1;
	}

	// check to see if the player is awaiting repair or being repaired.  Active the abort and inactive the repair items
	// check to see if the player is scheduled to be repaired by incoming ship
	if ( Ai_info[Ships[Player_obj->instance].ai_index].ai_flags[AI::AI_Flags::Being_repaired, AI::AI_Flags::Awaiting_repair] ) {
		MsgItems[TYPE_REPAIR_REARM_ITEM].active = 0;
		MsgItems[TYPE_REPAIR_REARM_ABORT_ITEM].active = 1;
	}
	else if ( mission_is_repair_scheduled(Player_obj) ) {
		MsgItems[TYPE_REPAIR_REARM_ITEM].active = 0;
		MsgItems[TYPE_REPAIR_REARM_ABORT_ITEM].active = 1;
	}
	// if no support available, can't call one in
	else if ( !is_support_allowed(Player_obj) ) {
		MsgItems[TYPE_REPAIR_REARM_ITEM].active = Hide_main_rearm_items_in_comms_gauge ? -1 : 0;
		MsgItems[TYPE_REPAIR_REARM_ABORT_ITEM].active = Hide_main_rearm_items_in_comms_gauge ? -1 : 0;
	}

	// de-activate the rearm/repair item if the player has a full load of missiles and
	// all subsystems at full strength.  We will only check if this item hasn't been marked
	// inactive because of some other reason
	if ( MsgItems[TYPE_REPAIR_REARM_ITEM].active > 0 ) {

		if ( !hud_squadmsg_can_rearm(Player_ship) ){
			MsgItems[TYPE_REPAIR_REARM_ITEM].active = 0;
		}
	}

	// if using keyboard shortcut, these items are always inactive or hidden
	if ( Msg_shortcut_command != -1 ) {
		MsgItems[TYPE_REINFORCEMENT_ITEM].active = 0;
		MsgItems[TYPE_REPAIR_REARM_ITEM].active = Hide_main_rearm_items_in_comms_gauge ? -1 : 0;
		MsgItems[TYPE_REPAIR_REARM_ABORT_ITEM].active = Hide_main_rearm_items_in_comms_gauge ? -1 : 0;
	}

do_main_menu:
	strcpy_s(Squad_msg_title, XSTR( "Message What", 316));
	k = hud_squadmsg_get_key();
	if ( k != -1 ) {							// when k != -1, we have a key that associates with menu item
		Assert ( k < Num_menu_items );
		if ( k == TYPE_SHIP_ITEM ){
			hud_squadmsg_do_mode( SM_MODE_SHIP_SELECT );
		} else if ( k == TYPE_WING_ITEM ) {
			hud_squadmsg_do_mode( SM_MODE_WING_SELECT );
		} else if ( k == TYPE_ALL_FIGHTERS_ITEM ) {
			hud_squadmsg_do_mode( SM_MODE_ALL_FIGHTERS );
		}
		
		if ( Msg_shortcut_command == -1 ) {
			if ( k == TYPE_REINFORCEMENT_ITEM ) {
				hud_squadmsg_do_mode( SM_MODE_REINFORCEMENTS );
				player_set_next_all_alone_msg_timestamp();
			} else if (k == TYPE_REPAIR_REARM_ITEM && !Hide_main_rearm_items_in_comms_gauge) {
				hud_squadmsg_do_mode( SM_MODE_REPAIR_REARM );
			} else if (k == TYPE_REPAIR_REARM_ABORT_ITEM && !Hide_main_rearm_items_in_comms_gauge) {
				hud_squadmsg_do_mode( SM_MODE_REPAIR_REARM_ABORT );
			} else if (k >= NUM_COMM_ORDER_TYPES) {
				Lua_sqd_msg_cat = lua_cat_list[k - NUM_COMM_ORDER_TYPES];
				hud_squadmsg_do_mode( SM_MODE_GENERAL );
			}
		}
	}
}

// function to display a list of ships to send a command to
void hud_squadmsg_ship_select()
{
	int k;

	if ( Num_menu_items == -1 ) {
		Num_menu_items = 0;
		hud_squadmsg_count_ships( 1 );
	}

	strcpy_s(Squad_msg_title, XSTR( "Select Ship", 317));
	k = hud_squadmsg_get_key();
	if ( k != -1 ) {						// if true, we have selected a ship.
		if ( Msg_shortcut_command == -1 ) {
			Msg_instance = MsgItems[k].instance;		// store the instance id in a global
			hud_squadmsg_do_mode( SM_MODE_SHIP_COMMAND );				// and move to a new mode
		} else {
			// we must convert the Msg_shortcut_command value to a value that the message
			// system normally uses to select a command.  Since the menu 
			//Assert( Msg_shortcut_command != IGNORE_TARGET_ITEM );
			hud_squadmsg_send_ship_command( MsgItems[k].instance, Msg_shortcut_command, 1, SQUADMSG_HISTORY_ADD_ENTRY);
			hud_squadmsg_toggle();
		}
	}

}

// function to display a list of wings to send a command to
void hud_squadmsg_wing_select()
{
	int k;

	if ( Num_menu_items == -1 ) {
		Num_menu_items = 0;
		hud_squadmsg_count_wings( 1 );
	}

	strcpy_s(Squad_msg_title, XSTR( "Select Wing", 318));
	k = hud_squadmsg_get_key();
	if ( k != -1 ) {						// if true, we have selected a ship.
		if ( Msg_shortcut_command == -1 ) {									// do normal menu stuff when no hoykey active
			Msg_instance = MsgItems[k].instance;	// store the instance id in a global
			hud_squadmsg_do_mode( SM_MODE_WING_COMMAND );				// and move to a new mode
		} else {
			//Assert( Msg_shortcut_command != IGNORE_TARGET_ITEM );
			hud_squadmsg_send_wing_command( MsgItems[k].instance, Msg_shortcut_command, 1, SQUADMSG_HISTORY_ADD_ENTRY );
			hud_squadmsg_toggle();
		}
	}

}

// code which gives an order to all fighters/bombers.  If there is a message shortcut active, then
// make that order apply to all fighters/bombers.  Otherwise, move to the ship_command menu
void hud_squadmsg_msg_all_fighters()
{
	if ( Msg_shortcut_command == -1 ) {
		Msg_instance = MESSAGE_ALL_FIGHTERS;
		hud_squadmsg_do_mode( SM_MODE_SHIP_COMMAND );
	} else {
		hud_squadmsg_send_to_all_fighters( Msg_shortcut_command );
		hud_squadmsg_toggle();
	}
}

// called to actually bring in a reinforcement.  For single player games, always gets called.
// for multiplayer games, always called on the server side.  Clients should never get here
void hud_squadmsg_call_reinforcement(int reinforcement_num, int  /*player_num*/)
{
	int i, delay;
	reinforcements *rp;
	p_object *p_objp;

	rp = &Reinforcements[reinforcement_num];

	// safety net mainly for multiplayer servers in case some odd data desync occurs between 
	// server and clients
	if ( MULTIPLAYER_MASTER && (rp->num_uses >= rp->uses) ) {
		return;
	}

	// check to see if the reinforcement called was a wing.
	for (i = 0; i < Num_wings; i++ ) {
		if ( !stricmp(rp->name, Wings[i].name) ) {
			// if the wing is currently present, skip this request so we don't waste a "use"
			if (Wings[i].current_count > 0) {
				return;
			}

			// found a wingname.  Call the parse function to create all the ships in this wing
			// we must set the arrival cue of the wing to true, otherwise, this won't work!!
            Wings[i].flags.remove(Ship::Wing_Flags::Reinforcement);
            Wings[i].flags.set(Ship::Wing_Flags::Reset_reinforcement);

			// set up the arrival delay.  If it is 0, then make is some random number of seconds
			delay = rp->arrival_delay;
			if ( delay == 0 )
				delay = (int)(frand() * 3.0) + 3;
			Wings[i].arrival_delay = timestamp(delay * 1000);
			break;
		}
	}

	// if we found no wing name that matched the reinforcement name, then look for a ship
	// of the same name
	if ( i == Num_wings ) {
		p_objp = mission_parse_get_arrival_ship( rp->name );
		if ( p_objp ) {
			// by resetting the reinforcement flag, we will allow code which normally handles arrivals
			// to make this reinforcement arrive.  Doing so keeps the data structures clean.
            p_objp->flags.remove(Mission::Parse_Object_Flags::SF_Reinforcement);

			// set up the arrival delay
			delay = rp->arrival_delay;
			if ( delay == 0 )
				delay = (int)(frand() * 3.0) + 3;		// between 3 and 6 seconds to arrive
			p_objp->arrival_delay = timestamp(delay * 1000);
		} else {
			Int3();				// get allender -- I don't think that this can happen!!!!
			return;
		}
	}

	// increment the number of times this is used.  Incremented here on single player and multiplayer
	// server side only.  Clients keep track of own count when they actually call something in.
	rp->num_uses++;

	// commented out on 9/9/98 because these messages simply are not used
	/*
	// now play a message (if there is one to play) for this reinforcement arrival.  The first for loop
	// determine how many messages there are to play, since the array is packet.  Then, if >= 1 message
	// to play, play one
	for (i = 0; i < MAX_REINFORCEMENT_MESSAGES; i++ )
		if ( !strlen(rp->yes_messages[i]) )
			break;

	//if ( i > 0 )
	//	message_send_to_player( rp->yes_messages[Random::next(i)], rp->name, MESSAGE_PRIORITY_NORMAL, HUD_SOURCE_FRIENDLY );
	*/

	mission_log_add_entry(LOG_PLAYER_CALLED_FOR_REINFORCEMENT, rp->name, NULL);
}

// function to display a list of reinforcements available to the player
void hud_squadmsg_reinforcement_select()
{
	int i, k, wingnum;
	reinforcements *rp;

	if ( Num_menu_items == -1 ) {
		Num_menu_items = 0;
		for (i = 0; i < Num_reinforcements; i++) {
			rp = &Reinforcements[i];
			SCP_string rp_name = rp->name;

			// don't put reinforcements onto the list that have already been used up.
			if ( rp->num_uses >= rp->uses ){
				continue;
			}

			// don't put items which are not on my team
			if((Player_ship != NULL) && (ship_get_reinforcement_team(i) != Player_ship->team)){
				continue;
			}
			
			//  check the arrival cue sexpression of the ship/wing of this reinforcement.
			// Goober5000 - if it can't arrive, it doesn't count.  This should check
			// for SEXP_FALSE as well as SEXP_KNOWN_FALSE, otherwise you end up with
			// a reinforcement menu containing no valid selections.
			if ( (wingnum = wing_name_lookup(rp->name, 1)) != -1 ) {
				Assert ( Wings[wingnum].arrival_cue >= 0 );
				if ( Sexp_nodes[Wings[wingnum].arrival_cue].value == SEXP_FALSE
					|| Sexp_nodes[Wings[wingnum].arrival_cue].value == SEXP_KNOWN_FALSE ){
					continue;
				}

				end_string_at_first_hash_symbol(rp_name);
			} else {
				p_object *p_objp;
				
				p_objp = mission_parse_get_arrival_ship( rp->name );
				if ( p_objp != NULL ) {
					if ( Sexp_nodes[p_objp->arrival_cue].value == SEXP_FALSE
						|| Sexp_nodes[p_objp->arrival_cue].value == SEXP_KNOWN_FALSE ){
						continue;
					}
				} else {
					Int3();							// allender says bogus!  reinforcement should be here since it wasn't a wing!
					continue;
				}

				rp_name = p_objp->get_display_name();	// this will handle getting rid of the hash if necessary
			}

			Assert ( Num_menu_items < MAX_MENU_ITEMS );
			MsgItems[Num_menu_items].text = std::move(rp_name);
			MsgItems[Num_menu_items].instance = i;
			MsgItems[Num_menu_items].active = 0;

			if ( rp->flags & RF_IS_AVAILABLE ) {
				MsgItems[Num_menu_items].active = 1;
			}

			Num_menu_items++;
		}
	}

//	hud_squadmsg_display_menu( "Select Reinforcement" );	
	strcpy_s(Squad_msg_title, XSTR( "Select Ship/Wing", 319)); // AL 11-14-97: Reinforcement didn't fit, so using this for now
	k = hud_squadmsg_get_key();
	if (k != -1) {
		int rnum;

		hud_squadmsg_toggle();						// take us out of message mode

		rnum = MsgItems[k].instance;

		// check to see if trying to call a reinforcement not yet available.  If so, maybe play message, but
		// definately bail
		if ( MsgItems[k].active == 0 ) {						
			return;
		}

		// in single player, or a multiplayer master, call in the reinforcement.  Clients send a packet to the
		// server
		if ( MULTIPLAYER_CLIENT ) {
			Reinforcements[rnum].num_uses++;			// increment this variable here since clients need to maintain a valid count
			send_player_order_packet(SQUAD_MSG_REINFORCEMENT, rnum, 0);
		} else {
			hud_squadmsg_call_reinforcement(rnum);
		}
	}
}

bool hud_order_comparator(size_t a, size_t b) {
	return Player_orders[a].ordering < Player_orders[b].ordering;
}

// function to display list of commands for a ship

void hud_squadmsg_ship_command()
{
	int k;
	std::set<size_t> orders;
	std::set<size_t, decltype(&hud_order_comparator)> default_orders(&hud_order_comparator);

	// when adding ship commands, we must look at the type of ship, and what messages that
	// ship allows.  First, place all messages that are possible onto the menu, then 

	// see if messaging all ships or just one.  Messaging all ships will mean all default orders
	// show on comm menu.
	if ( Msg_instance != MESSAGE_ALL_FIGHTERS ) {
		orders = Ships[Msg_instance].orders_accepted;
		const auto& default_orders_accepted = ship_get_default_orders_accepted(&Ship_info[Ships[Msg_instance].ship_info_index]);
		default_orders.insert(default_orders_accepted.cbegin(), default_orders_accepted.cend());
	} else {
		orders = default_messages;
		default_orders.insert(default_messages.cbegin(), default_messages.cend());
	}

	Num_menu_items = 0;
	for(size_t order_id : default_orders) {
		Assert (Num_menu_items < MAX_MENU_ITEMS);
		MsgItems[Num_menu_items].text = Player_orders[order_id].localized_name;
		MsgItems[Num_menu_items].instance = (int)order_id;
		MsgItems[Num_menu_items].active = 0;

		// check the bit to see if the command is active
		if (orders.find(order_id) != orders.end())
			MsgItems[Num_menu_items].active = 1;

		// if the order cannot be carried out by the ship, then item should be inactive
		if ((Msg_instance != MESSAGE_ALL_FIGHTERS) && !hud_squadmsg_ship_order_valid(Msg_instance, (int)order_id))
			MsgItems[Num_menu_items].active = 0;

		// do some other checks to possibly gray out other items.
		// if no target, remove any items which are associated with the players target
		if (!hud_squadmsg_is_target_order_valid(order_id, nullptr))
			MsgItems[Num_menu_items].active = 0;

		// if messaging all fighters, see if we should gray out the order if no one will accept it,
		// or modify the text if only some of the ships will accept it
		if (Msg_instance == MESSAGE_ALL_FIGHTERS) {
			ship_obj* so;
			ship* shipp;
			bool partial_accept, all_accept;            // value which tells us what to do with menu item

			all_accept = true;
			partial_accept = false;
			for (so = GET_FIRST(&Ship_obj_list); so != END_OF_LIST(&Ship_obj_list); so = GET_NEXT(so)) {
				if (Objects[so->objnum].flags[Object::Object_Flags::Should_be_dead])
					continue;

				// don't send messge to ships not on player's team, or that are in a wing.
				shipp = &Ships[Objects[so->objnum].instance];
				if (shipp->team != Player_ship->team)
					continue;

				// don't send message to non fighter wings
				if (!(Ship_info[shipp->ship_info_index].is_fighter_bomber()))
					continue;

				bool local_accepted = shipp->orders_accepted.contains(order_id);
				all_accept &= local_accepted;        // 'and'ing will either keep this bit set or zero it properly
				partial_accept |= local_accepted;    // 'or'ing will tell us if at least one accepts
			}

			if (!all_accept) {
				// either modify the text if a partial accept, or grey it out if no one accepts
				if (partial_accept) {
					MsgItems[Num_menu_items].text += XSTR("(*)", 320);
				} else {
					MsgItems[Num_menu_items].active = 0;
				}
			}
		}

		Num_menu_items++;
	
	}

	strcpy_s( Squad_msg_title, XSTR( "What Command", 321) );
	k = hud_squadmsg_get_key();

	// when we get a valid goal, we must add the goal to the ai ship's goal list

	if ( k != -1 ) {
		Assert ( k < Num_menu_items );
		// when messaging all fighters or ignoring target, call the send_to_all_fighters routine
		// Goober5000 - ignore no longer sends to all fighters
		if (Msg_instance == MESSAGE_ALL_FIGHTERS)
			hud_squadmsg_send_to_all_fighters(MsgItems[k].instance);
		else
			hud_squadmsg_send_ship_command(Msg_instance, MsgItems[k].instance, 1, SQUADMSG_HISTORY_ADD_ENTRY);

		hud_squadmsg_toggle();
	}
}

void hud_squadmsg_msg_general()
{
	int k;

	Num_menu_items = 0;
	for (size_t order_id = 0; order_id < Player_orders.size(); order_id++) {
		Assert(Num_menu_items < MAX_MENU_ITEMS);

		if (Player_orders[order_id].lua_id <= 0) {
			continue;
		}

		auto lua_porder = ai_lua_find_player_order(Player_orders[order_id].lua_id);

		//If it's not a general order then do not add it.
		if (!lua_porder->generalOrder) {
			continue;
		}

		//If it's not part of the selected category then do not add it.
		if (lua_porder->category != Lua_sqd_msg_cat) {
			continue;
		}

		//Only add it if it is enabled for the mission
		if (lua_porder->cur_enabled) {

			MsgItems[Num_menu_items].text = Player_orders[order_id].localized_name;
			MsgItems[Num_menu_items].instance = Player_orders[order_id].lua_id;
			MsgItems[Num_menu_items].active = (int)lua_porder->cur_valid;

			// do some other checks to possibly gray out other items.
			// if no target, remove any items which are associated with the players target
			if (!hud_squadmsg_is_target_order_valid(order_id, nullptr))
				MsgItems[Num_menu_items].active = 0;

			Num_menu_items++;
		}
	}

	strcpy_s(Squad_msg_title, XSTR("What Command", 321));
	k = hud_squadmsg_get_key();

	// when we get a valid goal, we must add the goal to the ai ship's goal list

	if (k != -1) {
		Assert(k < Num_menu_items);
		
		ai_lua_start_general(MsgItems[k].instance, Player_ai->target_objnum);

		hud_squadmsg_toggle();
	}
}

// function to display list of command for a wing
void hud_squadmsg_wing_command()
{
	int k;
	wing *wingp;
	int shipnum;

	std::set<size_t> orders;
	std::set<size_t, decltype(&hud_order_comparator)> default_orders(&hud_order_comparator);
	
	// when adding commands for wings, we will look at all of the ships in the wing, and create
	// the order list from that set of ships.  In the future, we may want to do something else....

	wingp = &Wings[Msg_instance];

	// or together all of the orders for all the ships in the wing
	for (int i = 0; i < wingp->current_count; i++ ) {
		const auto& setAdd = ship_get_default_orders_accepted( &Ship_info[Ships[wingp->ship_index[i]].ship_info_index] );
		default_orders.insert(setAdd.begin(), setAdd.end());
	}
	
	default_orders.erase(CAPTURE_TARGET_ITEM); // we cannot capture any target with a wing.

	Num_menu_items = 0;
	shipnum = wingp->ship_index[wingp->special_ship];
	Assertion(shipnum >= 0, "Special ship (%d) for wing '%s' has a negative ship_index (%d). This should not happen; get a coder!\n", wingp->special_ship, wingp->name, shipnum);
	orders = Ships[shipnum].orders_accepted;		// get the orders that the wing leader will accept
	
	for ( size_t order_id : default_orders ) {
		// add the set of default orders to the comm menu.  We will currently allow all messages
		// to be available in the wing.
		Assert ( Num_menu_items < MAX_MENU_ITEMS );
		MsgItems[Num_menu_items].text = Player_orders[order_id].localized_name;
		MsgItems[Num_menu_items].instance = (int)order_id;
		MsgItems[Num_menu_items].active = 0;

		// possibly grey out the menu item depending on whether or not the "wing" will accept this order
		// the "wing" won't accept the order if the first ship in the wing doesn't accept it.
		if ( orders.find(order_id) != orders.end() )
			MsgItems[Num_menu_items].active = 1;

		// do some other checks to possibly gray out other items.
		// if no target, remove any items which are associated with the players target
		if ( !hud_squadmsg_is_target_order_valid((int)order_id, 0, true) )
			MsgItems[Num_menu_items].active = 0;

		// if no ship in the wing can depart then gray out the departure order
		if (order_id == DEPART_ITEM) {
			if (MsgItems[Num_menu_items].active > 0) {
				int active = 0;
				for (int i = 0; i < wingp->current_count; i++) {
					if (hud_squadmsg_ship_order_valid(wingp->ship_index[i], (int)order_id)) {
						active = 1;
						break;
					}
				}
				MsgItems[Num_menu_items].active = active;
			}
		}

		Num_menu_items++;
	
	}

	
	strcpy_s(Squad_msg_title, XSTR( "What Command", 321) );
	k = hud_squadmsg_get_key();
	if ( k != -1 ) {

		// ignore target gets sent to everyone.
		/* Goober5000 - not anymore
		if (MsgItems[k].instance == IGNORE_TARGET_ITEM)
			hud_squadmsg_send_to_all_fighters(MsgItems[k].instance);
		else */
			hud_squadmsg_send_wing_command(Msg_instance, MsgItems[k].instance, 1);

		hud_squadmsg_toggle();
	}
}



//----------------------------------------------------------
// external entry points below!!!!

// function is called once per mission start.  Initializes those values
// which only need to be inited once per mission.
void hud_init_squadmsg( void ) 
{
	Msg_eat_key_timestamp = timestamp(0);
	Squadmsg_history.clear();
}

// external entry point into code which changes the messaging mode based on the
// previous player flag value.  I thought it better to isolate all system changes
// in this code.
void hud_squadmsg_toggle()
{
	// if the emp effect is active, always ignore this
	if(emp_active_local()){
		return;
	}

	// if entering this mode, must setup messaging system.  Don't start squadmessging if 
	// the player is dead.
	if ( !(Player->flags & PLAYER_FLAGS_MSG_MODE) ) {
		if ( Game_mode & GM_DEAD ){
			return;
		}

		if ( (Game_mode & GM_MULTIPLAYER) && NETPLAYER_IS_OBSERVER(Net_player) ){
			return;
		}

		hud_squadmsg_start();
	} else {
		hud_squadmsg_end();
	}

	Player->flags ^= PLAYER_FLAGS_MSG_MODE;
}

// extern entry point to allow messaging of enemies
void hud_enemymsg_toggle()
{
	hud_squadmsg_toggle();
	// if we just entered message mode, turn on var that says to message enemies
	if ( Player->flags & PLAYER_FLAGS_MSG_MODE )
		Msg_enemies = 1;
}

// external entry point into code when a keyboard shortcut is used for a command
// we are passed in an ID for the command to set internal variables.  This command
// will be used in place of the last menu in the messaging code
void hud_squadmsg_shortcut( int command )
{
	// check if the communications system is capable of sending a message
	if ( (hud_communications_state(Player_ship) != COMM_OK) && (command != REARM_REPAIR_ME_ITEM) ) {
		HUD_sourced_printf(HUD_SOURCE_HIDDEN, "%s", XSTR( "Messaging is restricted due to communications damage", 331));
		return;
	}

	// observers in multiplayer games cannot have this active either
	if ( (Game_mode & GM_MULTIPLAYER) && NETPLAYER_IS_OBSERVER(Net_player) )
		return;

	// in multiplayer and I cannot message, don't allow anything except calling in for rearm
	if ( (Game_mode & GM_MULTIPLAYER) && !multi_can_message(Net_player) && (command != REARM_REPAIR_ME_ITEM) )
		gamesnd_play_error_beep();

	// player ships which turn traitor cannot rearm
	if ( Player_ship->team == Iff_traitor )
		return;

	if ( Player->flags & PLAYER_FLAGS_MSG_MODE )		// we are already in messaging mode -- maybe do sometime more interesting?
		return;
	hud_squadmsg_toggle();
	Msg_shortcut_command = command;									// save the command for later use

	if (Msg_shortcut_command == CAPTURE_TARGET_ITEM)			// some commands only apply to wings or ships
		Squad_msg_mode = SM_MODE_SHIP_SELECT;						//  -- don't offer choice

	/* Goober5000 - again with the stupid logic
	else if ( Msg_shortcut_command == IGNORE_TARGET_ITEM ) {	//  ignoring target applied to all ships
		hud_squadmsg_toggle();											// turns off mode which was turned on above
		hud_squadmsg_send_to_all_fighters( Msg_shortcut_command );
	}
	*/
}

// external entry point which is called when the player hits a selection key (1-0) while in messaging
// mode.  If we are in messaging mode, send the shortcut command to the ships that are part of the
// passed in selection set.  If there is no shortcut command, do nothing.  Returns 1 if the key
// was used, else 0.  This return value is used to tell the key control system that it should
// call the normal targeting selection stuff.
int hud_squadmsg_hotkey_select( int k )
{
	htarget_list *hitem, *plist;
	int send_message;
	object *objp;

	Assert ( Player->flags & PLAYER_FLAGS_MSG_MODE );

	if ( Msg_shortcut_command == -1 )
		return 0;

	Assert ( (k >= 0) && (k < MAX_KEYED_TARGETS) );
	plist = &(Player->keyed_targets[k]);

	if ( EMPTY(plist) )		// be sure that we have at least one ship in the list
		return 0;

	send_message = 1;
	// for each ship in the selection set list, send the shortcut command that the player
	// previously entered.  Be sure to check that we are not trying to send a command to
	// an enemy ship.
	for ( hitem = GET_FIRST(plist); hitem != END_OF_LIST(plist); hitem = GET_NEXT(hitem) ) {
		objp = hitem->objp;
		Assert ( objp->type == OBJ_SHIP );
		if ( Ships[objp->instance].team != Player_ship->team )
			continue;

		// be sure that this ship can accept this command
		if (!Ships[objp->instance].orders_accepted.contains(Msg_shortcut_command))
			continue;

		hud_squadmsg_send_ship_command( objp->instance, Msg_shortcut_command, send_message, SQUADMSG_HISTORY_ADD_ENTRY );
		send_message  = 0;
	}

	hud_squadmsg_toggle();			// turn off messaging mode
	return 1;
}


// the next function is called once a frame when the player is messaging someone
// in his squad.  After a period of 5 seconds of inactivity (i.e. no keypress to
// select something in the menu), the menu will disappear.  This function will only
// get called if the player flag PLAYER_FLAG_MSG_MODE is set.  Parameter is the key
// that was hit this frame

int hud_squadmsg_do_frame( )
{
	int target_changed;

	Assert ( Player->flags & PLAYER_FLAGS_MSG_MODE );		// be sure that messaging mode is set!!!

	// if the player is now dead, or the timestamp elapsed, then get out of messaging mode.
	if ( (Game_mode & GM_DEAD) || timestamp_elapsed(Msg_mode_timestamp) ) {
		hud_squadmsg_toggle();
		return 0;
	}

	Msg_key_used = 0;

	// check the player's current target.  Change in target resets the timer
	target_changed = 0;
	if ( Msg_target_objnum != Player_ai->target_objnum ) {
		Msg_target_objnum = Player_ai->target_objnum;
		target_changed = 1;
	}

	if ( Msg_targeted_subsys != Player_ai->targeted_subsys ) {
		Msg_targeted_subsys = Player_ai->targeted_subsys;
		target_changed = 1;
	}

	// check for multiplayer mode - this is really a special case checker for support ship requesting and aborting
	if(MULTIPLAYER_CLIENT && (Squad_msg_mode == SM_MODE_REPAIR_REARM || Squad_msg_mode == SM_MODE_REPAIR_REARM_ABORT)){		
		// send the correct packet
		if(Squad_msg_mode == SM_MODE_REPAIR_REARM)		
			send_player_order_packet(SQUAD_MSG_SHIP, 0, REARM_REPAIR_ME_ITEM);
		else
			send_player_order_packet(SQUAD_MSG_SHIP, 0, ABORT_REARM_REPAIR_ITEM);

		// make sure to toggle the mode off
		hud_squadmsg_toggle();
		
		return 1;
	}

	switch( Squad_msg_mode ) {

	case SM_MODE_TYPE_SELECT:
		hud_squadmsg_type_select();
		break;

	case SM_MODE_SHIP_SELECT:
		hud_squadmsg_ship_select();
		break;

	case SM_MODE_WING_SELECT:
		hud_squadmsg_wing_select();
		break;

	case SM_MODE_SHIP_COMMAND:
		hud_squadmsg_ship_command();
		break;

	case SM_MODE_WING_COMMAND:
		hud_squadmsg_wing_command();
		break;

	case SM_MODE_REINFORCEMENTS:
		hud_squadmsg_reinforcement_select();
		break;		
		
	case SM_MODE_REPAIR_REARM:
        hud_squadmsg_repair_rearm(1);				// note we return right away.  repair/rearm code handles messaging, etc
		break;

	case SM_MODE_REPAIR_REARM_ABORT:
        hud_squadmsg_repair_rearm_abort(1);		// note we return right away.  repair/rearm code handles messaging, etc
		break;

	case SM_MODE_ALL_FIGHTERS:
		hud_squadmsg_msg_all_fighters();
		break;

	case SM_MODE_GENERAL:
		hud_squadmsg_msg_general();
		break;

	default:
		Int3();		// get allender -- invalid mode in messaging system
		break;

	}

	// be sure to reset the clip region
	HUD_reset_clip();		// JAS: Is this needed?

	if ( Msg_key_used || target_changed ) {
		Msg_mode_timestamp = _timestamp(DEFAULT_MSG_TIMEOUT);
		return 1;
	} else
		return 0;
}

void hud_add_issued_order(const char *name, int order)
{  
	squadmsg_history *latest_order = new squadmsg_history(); 

	if (!strcmp(name, "All Fighters")) {
		latest_order->order_to  = -1; 
	}
	else {
		latest_order->order_to = get_parse_name_index(name);
	}
	latest_order->order = order;
	latest_order->order_time = Missiontime;
	
	//stick it in history
	Squadmsg_history.push_back(*latest_order); 
	delete latest_order;
}

void hud_update_last_order(const char *target, int order_source, int special_index)
{
	squadmsg_history *latest_order = &Squadmsg_history.back(); 
	if (target) {
		latest_order->target = get_parse_name_index(target);
	}
	if (MULTIPLAYER_MASTER && order_source != -1) {
		latest_order->order_from = Objects[Net_players[order_source].m_player->objnum].instance;
	}
	else if (order_source == -1) {
		latest_order->order_from = Player_obj->instance; 
	}

	latest_order->special_index = special_index;
}

int hud_query_order_issued(const char *to, const char *order_name, const char *target_name, int timestamp, const char *from, const char *special_argument)
{
	int i, order = -1, ship_or_wing = -1, target = -1, source = -1; 
	
	// if the desired order was not sent to all fighters 
	if (strcmp(to, "<all fighters>") != 0) {
		ship_or_wing = get_parse_name_index(to);
	}

	// get the target ship
	if (target_name != NULL) {
		target = get_parse_name_index(target_name); 
	}

	if (MULTIPLAYER_MASTER && (from != NULL)) {
		source =  ship_name_lookup(from, 1); 
	}

	
	for (i = 0; i < (int) Player_orders.size(); i++)
		if (!stricmp(order_name, Player_orders[i].hud_name.c_str())) {
			order = i;
			break;
		}

	// Goober5000 - if not found, check compatibility
	if (order == -1)
	{
		if (!stricmp(order_name, "Attack my target"))
		{
			order = ATTACK_TARGET_ITEM;
		}
	}

	Assert(order != -1);
	for (i = 0; i < (int)Squadmsg_history.size(); i++) {
		if (Squadmsg_history[i].order == order) {
			if (ship_or_wing == Squadmsg_history[i].order_to) {
				if (target == -1 || Squadmsg_history[i].target == target) {
					if ((!timestamp) || (Squadmsg_history[i].order_time + i2f(timestamp) >= Missiontime) ) {	
						// In multiplayer games we may wish to check who sent the order
						if (MULTIPLAYER_MASTER) {
							if ((source != -1) && (Squadmsg_history[i].order_from != source)) {
								continue;
							}
						}
						
						// some orders will have additional arguments
						if (order & DISABLE_SUBSYSTEM_ITEM) {
							if (special_argument == NULL) {
								continue;
							}
							
							int target_ship = ship_name_lookup(target_name);
                            
							if(target_ship<0) {
								continue;
							}
                            
							int subsys_index = ship_find_subsys(&Ships[target_ship], special_argument); 
							// if the order is for s different subsystem
							if (Squadmsg_history[i].special_index != subsys_index) {
								continue;
							}
						}

						// if we make it this far then the orders match
						return 1;
					}
				}
			}
		}
	}

	return 0;
}

void Hud_set_lua_key(int selection) {
	Msg_key = selection;
	Msg_key_set_from_scripting = true;
}

HudGaugeSquadMessage::HudGaugeSquadMessage():
HudGauge(HUD_OBJECT_SQUAD_MSG, HUD_MESSAGE_BOX, false, false, (VM_EXTERNAL | VM_DEAD_VIEW | VM_WARP_CHASE | VM_PADLOCK_ANY | VM_OTHER_SHIP), 255, 255, 255)
{
}

void HudGaugeSquadMessage::initialize()
{
	flash_timer[0] = timestamp(1);
	flash_timer[1] = timestamp(1);
	flash_flag = false;

	HudGauge::initialize();
}

void HudGaugeSquadMessage::initHeaderOffsets(int x, int y)
{
	Header_offsets[0] = x;
	Header_offsets[1] = y;
}

void HudGaugeSquadMessage::initItemStartOffsets(int x, int y)
{
	Item_start_offsets[0] = x;
	Item_start_offsets[1] = y;
}

void HudGaugeSquadMessage::initMiddleFrameStartOffsetY(int y)
{
	Middle_frame_start_offset_y = y;
}

void HudGaugeSquadMessage::initBottomBgOffset(int offset)
{
	bottom_bg_offset = offset;
}

void HudGaugeSquadMessage::initItemHeight(int h)
{
	Item_h = h;
}

void HudGaugeSquadMessage::initItemOffsetX(int x)
{
	Item_offset_x = x;
}

void HudGaugeSquadMessage::initPgUpOffsets(int x, int y)
{
	Pgup_offsets[0] = x;
	Pgup_offsets[1] = y;
}

void HudGaugeSquadMessage::initPgDnOffsets(int x, int y)
{
	Pgdn_offsets[0] = x;
	Pgdn_offsets[1] = y;
}

void HudGaugeSquadMessage::initShipNameMaxWidth(int w)
{
	Ship_name_max_width = w;
}

void HudGaugeSquadMessage::initBitmaps(char *fname_top, char *fname_middle, char *fname_bottom)
{
	Mbox_gauge[0].first_frame = bm_load_animation(fname_top, &Mbox_gauge[0].num_frames);
	if ( Mbox_gauge[0].first_frame == -1 ) {
		Warning(LOCATION, "Could not load in ani: %s\n", fname_top);
	}
	
	Mbox_gauge[1].first_frame = bm_load_animation(fname_middle, &Mbox_gauge[1].num_frames);
	if ( Mbox_gauge[1].first_frame == -1 ) {
		Warning(LOCATION, "Could not load in ani: %s\n", fname_middle);
	}

	Mbox_gauge[2].first_frame = bm_load_animation(fname_bottom, &Mbox_gauge[2].num_frames);
	if ( Mbox_gauge[2].first_frame == -1 ) {
		Warning(LOCATION, "Could not load in ani: %s\n", fname_bottom);
	}
}

void HudGaugeSquadMessage::startFlashPageScroll(int duration)
{
	flash_timer[0] = timestamp(duration);
}

bool HudGaugeSquadMessage::maybeFlashPageScroll(bool flash_fast)
{
	bool draw_bright = false;

	if(!timestamp_elapsed(flash_timer[0])) {
		if(timestamp_elapsed(flash_timer[1])) {
			if(flash_fast) {
				flash_timer[1] = timestamp(fl2i(TBOX_FLASH_INTERVAL/2.0f));
			} else {
				flash_timer[1] = timestamp(TBOX_FLASH_INTERVAL);
			}
			flash_flag = !flash_flag;
		}

		if(flash_flag) {
			draw_bright = true;
		} 
	}

	return draw_bright;
}

bool HudGaugeSquadMessage::canRender() const
{
	if(hud_disabled_except_messages() && !message_gauge) {
		return false;
	}

	if (hud_disabled()) {
		return false;
	}
	
	if ( !(Game_detail_flags & DETAIL_FLAG_HUD) ) {
		return false;
	}

	if ((Viewer_mode & disabled_views)) {
		return false;
	}

	if (!( Player->flags & PLAYER_FLAGS_MSG_MODE )) {
		return false;
	}

	if (scripting_render_override) {
		return false;
	}

	return true;
}

void HudGaugeSquadMessage::render(float  /*frametime*/, bool config)
{
	const char* title = Squad_msg_title;
	if (config) {
		title = XSTR("Message What", 316);
	}

	int x = position[0];
	int y = position[1];
	float scale = 1.0;

	if (config) {
		std::tie(x, y, scale) = hud_config_convert_coord_sys(position[0], position[1], base_w, base_h);
	}

	// setup color/font info 
	setGaugeColor(HUD_C_NONE, config);

	// draw top of frame
	if ( Mbox_gauge[0].first_frame >= 0 ) {
		renderBitmap(Mbox_gauge[0].first_frame, x, y, scale, config);
	}

	// hud_set_bright_color();
	setGaugeColor(HUD_C_BRIGHT, config);
	if ( title ) {
		renderString(x + Header_offsets[0], y + fl2i(Header_offsets[1] * scale), title, scale, config);
	}

	int nitems;
	if (!config) {
		if (Num_menu_items < MAX_MENU_DISPLAY)
			nitems = Num_menu_items;
		else {
			if (First_menu_item == 0) // First_menu_item == 0 means first page of items
				nitems = MAX_MENU_DISPLAY;
			else if ((Num_menu_items - First_menu_item) <= MAX_MENU_DISPLAY) // check if remaining items fit on one page
				nitems = Num_menu_items - First_menu_item;
			else {
				nitems = MAX_MENU_DISPLAY;
			}
		}
	} else {
		nitems = 6;
	}

	int sx = x + fl2i(Item_start_offsets[0] * scale);
	int sy = y + fl2i(Item_start_offsets[1] * scale);
	int bx = x;	// global x-offset where bitmap gets drawn
	int by = y + fl2i(Middle_frame_start_offset_y * scale); // global y-offset where bitmap gets drawn

	bool none_valid = true;		// variable to tell us whether all items in the menu are valid or not

	// use another variable to tell us whether we can message or not.
	bool messaging_allowed = true;

	if (!config && (Game_mode & GM_MULTIPLAYER) && !multi_can_message(Net_player) ){
		messaging_allowed = false;
	}

	for (int i = 0; i < nitems; i++ ) {
		int item_num;
		bool isSelectedItem = (i == Selected_menu_item);
		char text[255];

		if (!config) {
			strcpy_s(text, MsgItems[First_menu_item + i].text.c_str());
		} else {
			// in config mode, so create just the first page of the Comms Menu
			// as other functions, such as hud_squadmsg_type_select() will not be run in config mode
			const char* temp_comm_order_types[] = {XSTR("Ships", 293),
				XSTR("Wings", 294),
				XSTR("All Fighters", 295),
				XSTR("Reinforcements", 296),
				XSTR("Rearm/Repair Subsys", 297),
				XSTR("Abort Rearm", 298)
			};
			strcpy_s(text, temp_comm_order_types[i]);
			if (Hide_main_rearm_items_in_comms_gauge && (i == TYPE_REPAIR_REARM_ITEM || i == TYPE_REPAIR_REARM_ABORT_ITEM)) {
				MsgItems[First_menu_item + i].active = -1;
			}
		}

		// blit the background
		setGaugeColor(HUD_C_NONE, config);
		if ( Mbox_gauge[1].first_frame >= 0 ) {
			renderBitmap(Mbox_gauge[1].first_frame, bx, by, scale, config);
		}
		by += fl2i(Item_h * scale);

		// set the text color
		if (!config && (MsgItems[First_menu_item + i].active > 0)) {
			setGaugeColor(HUD_C_BRIGHT, config);
		}
		else if (isSelectedItem) {
			setGaugeColor(HUD_C_NORMAL, config);
		}
		else {
			setGaugeColor(HUD_C_DIM, config);
		}

		if (MsgItems[First_menu_item + i].active >= 0) {
			// first print an icon to indicate selected item
			item_num = (i + 1) % MAX_MENU_DISPLAY;
			if (isSelectedItem && Display_selector) {
				renderPrintfWithGauge(sx, sy, EG_SQ1 + i, scale, config, XSTR(">>", 1887), item_num); //allow modders to change string and add number
			}
			// or do the number
			else {
				renderPrintfWithGauge(sx, sy, EG_SQ1 + i, scale, config, XSTR("%1d.", 1886), item_num);
			}

			// then the text
			font::force_fit_string(text, 255, fl2i(Ship_name_max_width * scale), scale);
			renderString(sx + fl2i(Item_offset_x * scale), sy, EG_SQ1 + i, text, scale, config);
			sy += fl2i(Item_h * scale);
		}

		// if we have at least one item active, then set the variable so we don't display any
		// message about no active items
		if (config || (MsgItems[First_menu_item+i].active > 0) )
			none_valid = false;
	}

	// maybe draw an extra line in to make room for [pgdn], or for the 'no active items'
	// display
	if (!config && (!messaging_allowed || none_valid || ((First_menu_item + nitems) < Num_menu_items) || (Msg_shortcut_command != -1))) {
		// blit the background
		setGaugeColor(HUD_C_NONE, config);
		if ( Mbox_gauge[1].first_frame >= 0 ) {		
			renderBitmap(Mbox_gauge[1].first_frame, bx, by, scale, config);
		}
		by += fl2i(Item_h * scale);
	}

	// draw the bottom of the frame
	setGaugeColor(HUD_C_NONE, config);
	if ( Mbox_gauge[2].first_frame >= 0 ) {
	
		renderBitmap(Mbox_gauge[2].first_frame, bx, by + fl2i(bottom_bg_offset * scale), scale, config);
	}

	// determine if we should put the text "[more]" at top or bottom to indicate you can page up or down
	if (!config) {
		startFlashPageScroll();
		maybeFlashPageScroll();
	}
	if ( First_menu_item > 0 ) {
		renderPrintf(x + fl2i(Pgup_offsets[0] * scale), y + fl2i(Pgup_offsets[1] * scale), scale, config, "%s", XSTR( "[pgup]", 312) );
	}

	if ( (First_menu_item + nitems) < Num_menu_items ) {
		renderPrintf(x + fl2i(Pgdn_offsets[0] * scale), y + fl2i(Pgdn_offsets[1] * scale), scale, config, "%s", XSTR( "[pgdn]", 313));
	}

	if ( messaging_allowed ) {
		if ( none_valid ){
			renderPrintf(sx, by - fl2i(Item_h * scale) + 2, scale, config, "%s", XSTR("No valid items", 314));
		} else if (!config && Msg_shortcut_command != -1){
			renderPrintf( sx, by - fl2i(Item_h * scale) + 2, scale, config, "%s", comm_order_get_text(Msg_shortcut_command));
		}
	} else {
		// if this player is not allowed to message, then display message saying so
		renderPrintf(sx, by - fl2i(Item_h * scale) + 2, scale, config, "%s", XSTR("Not allowed to message", 315));
	}

	if (config) {
		int bmw, bmh;
		bm_get_info(Mbox_gauge[2].first_frame, &bmw, &bmh);
		hud_config_set_mouse_coords(gauge_config_id, x, bx + fl2i(bmw * scale), y, by + fl2i((bmh + bottom_bg_offset) * scale));
	}

}

void HudGaugeSquadMessage::pageIn() 
{
	for (int i = 0; i < NUM_MBOX_FRAMES; i++ ) {
		bm_page_in_aabitmap( Mbox_gauge[i].first_frame, Mbox_gauge[i].num_frames );
	}
}
