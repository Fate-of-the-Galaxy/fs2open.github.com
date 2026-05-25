#include "iff_defs/iff_defs.h"
#include "mission/missionparse.h"
#include "mission/missiongoals.h"
#include "mission/missionmessage.h"
#include "missionui/redalert.h"
#include "mod_table/mod_table.h"
#include "nebula/neb.h"
#include "parse/parselo.h"
#include "render/3d.h"
#include "ship/ship.h"
#include "species_defs/species_defs.h"
#include "starfield/starfield.h"
#include "weapon/weapon.h"
#include "utils/string_utils.h"

#include "xwingbrflib.h"
#include "xwinglib.h"
#include "xwingmissionparse.h"

// helper functions from missionparse.cpp
extern int allocate_subsys_status();
extern anchor_t get_anchor(const char *name);

static int Player_flight_group = 0;
static SCP_unordered_set<SCP_string, SCP_string_lcase_hash, SCP_string_lcase_equal_to> Do_not_reposition_wings;

const int MAX_SPACE_OBJECTS = 64; // To match the XWing game engine limit

int get_sexp_xwing(const SCP_string &sexp_buf)
{
	auto sexp_buf_copy = util::unique_copy(sexp_buf.c_str(), false);
	Mp = sexp_buf_copy.get();
	return get_sexp_main();
}

// Locate the player's flight group in the current XWI mission.  Returns 0 if
// none is marked (with a warning) so the static `Player_flight_group` is always
// a valid index for the *current* mission, never a stale value from a previous
// batch-import iteration.
static int xwi_find_player_flight_group(const XWingMission *xwim)
{
	int index = -1;
	for (int i = 0; i < (int)xwim->flightgroups.size(); i++)
	{
		if (xwim->flightgroups[i].playerPos > 0)
		{
			index = i;
			// don't break in case multiple FGs set a player - we will use the last one assigned
		}
	}
	if (index >= 0)
		return index;

	Warning(LOCATION, "Player flight group not found?");
	return 0;
}

// vazor222
void parse_xwi_mission_info(mission *pm, const XWingMission *xwim)
{
	pm->author = "X-Wing";
	strcpy_s(pm->created, "00/00/00 at 00:00:00");

	// Re-derive the player flight group from the current mission.  The static
	// would otherwise hold a stale index from the previous batch-import
	// iteration, and we use it immediately below.
	Player_flight_group = xwi_find_player_flight_group(xwim);
	if (xwim->flightgroups.empty() || Player_flight_group >= (int)xwim->flightgroups.size())
		return;

	// NOTE: Y and Z are swapped and the units are in km
	Parse_viewer_pos.xyz.x = 1000 * xwim->flightgroups[Player_flight_group].start1_x;
	Parse_viewer_pos.xyz.y = 1000 * xwim->flightgroups[Player_flight_group].start1_z + 100;
	Parse_viewer_pos.xyz.z = 1000 * xwim->flightgroups[Player_flight_group].start1_y - 100;
	vm_angle_2_matrix(&Parse_viewer_orient, PI_4, 0);
}

bool is_fighter_or_bomber(const XWMFlightGroup *fg)
{
	switch (fg->flightGroupType)
	{
		case XWMFlightGroupType::fg_X_Wing:
		case XWMFlightGroupType::fg_Y_Wing:
		case XWMFlightGroupType::fg_A_Wing:
		case XWMFlightGroupType::fg_B_Wing:
		case XWMFlightGroupType::fg_TIE_Fighter:
		case XWMFlightGroupType::fg_TIE_Interceptor:
		case XWMFlightGroupType::fg_TIE_Bomber:
		case XWMFlightGroupType::fg_Gunboat:
		case XWMFlightGroupType::fg_TIE_Advanced:
			return true;
		default:
			break;
	}
	return false;
}

bool is_wing(const XWMFlightGroup *fg)
{
	return (fg->numberInWave > 1 || fg->numberOfWaves > 1 || is_fighter_or_bomber(fg));
}

int xwi_flightgroup_lookup(const XWingMission *xwim, const XWMFlightGroup *fg)
{
	for (size_t i = 0; i < xwim->flightgroups.size(); i++)
	{
		if (xwim->flightgroups[i].designation == fg->designation)
			return (int)i;
	}
	return -1;
}

void xwi_add_attack_check(const XWingMission *xwim, const XWMFlightGroup *fg)
{
	char fg_name[NAME_LENGTH] = "";
	char event_name[NAME_LENGTH];
	SCP_string sexp_buf;

	int fg_index = xwi_flightgroup_lookup(xwim, fg);
	Assertion(fg_index >= 0, "Flight Group index must be valid");

	strcpy_s(fg_name, fg->designation.c_str());
	SCP_totitle(fg_name);

	sprintf(event_name, "FG %d Attack Check", fg_index);

	if (mission_event_lookup(event_name) >= 0)
		return;

	Mission_events.emplace_back();
	auto event = &Mission_events.back();
	event->name = event_name;

	if (is_wing(fg))
		sprintf(sexp_buf, "( when ( true ) ( fotg-wing-attacked-init \"%s\" ) )", fg_name);
	else
		sprintf(sexp_buf, "( when ( true ) ( fotg-ship-attacked-init \"%s\" ) )", fg_name);
	event->formula = get_sexp_xwing(sexp_buf);
}

int xwi_determine_arrival_cue(const XWingMission *xwim, const XWMFlightGroup *fg)
{
	const XWMFlightGroup *arrival_fg = nullptr;
	char arrival_fg_name[NAME_LENGTH] = "";
	SCP_string sexp_buf;

	bool check_wing = false;
	if (fg->arrivalFlightGroup >= 0)
	{
		arrival_fg = &xwim->flightgroups[fg->arrivalFlightGroup];
		check_wing = is_wing(arrival_fg);
		strcpy_s(arrival_fg_name, arrival_fg->designation.c_str());
		SCP_totitle(arrival_fg_name);
	}
	else
		return Locked_sexp_true;

	if (fg->arrivalEvent == XWMArrivalEvent::ae_mission_start)
		return Locked_sexp_true;

	if (fg->arrivalEvent == XWMArrivalEvent::ae_afg_arrived)
	{
		sprintf(sexp_buf, "( has-arrived-delay 0 \"%s\" )", arrival_fg_name);
		return get_sexp_xwing(sexp_buf);
	}

	if (fg->arrivalEvent == XWMArrivalEvent::ae_afg_attacked)
	{
		xwi_add_attack_check(xwim, arrival_fg);

		if (check_wing)
			sprintf(sexp_buf, "( fotg-is-wing-attacked \"%s\" )", arrival_fg_name);
		else
			sprintf(sexp_buf, "( fotg-is-ship-attacked \"%s\" )", arrival_fg_name);
		return get_sexp_xwing(sexp_buf);
	}

	if (fg->arrivalEvent == XWMArrivalEvent::ae_afg_captured)
	{
		if (check_wing)
			sprintf(sexp_buf, "( fotg-is-wing-captured \"%s\" )", arrival_fg_name);
		else
			sprintf(sexp_buf, "( fotg-is-ship-captured \"%s\" )", arrival_fg_name);
		return get_sexp_xwing(sexp_buf);
	}

	if (fg->arrivalEvent == XWMArrivalEvent::ae_afg_destroyed)
	{
		// X-Wing treats destruction for arrivals slightly differently
		if (check_wing)
			sprintf(sexp_buf, "( and ( percent-ships-destroyed 1 \"%s\" ) ( destroyed-or-departed-delay 0 \"%s\" ) )", arrival_fg_name, arrival_fg_name);
		else
			sprintf(sexp_buf, "( is-destroyed-delay 0 \"%s\" )", arrival_fg_name);
		return get_sexp_xwing(sexp_buf);
	}

	if (fg->arrivalEvent == XWMArrivalEvent::ae_afg_disabled)
	{
		if (check_wing)
			sprintf(sexp_buf, "( fotg-is-wing-disabled \"%s\" )", arrival_fg_name);
		else
			sprintf(sexp_buf, "( fotg-is-ship-disabled \"%s\" )", arrival_fg_name);
		return get_sexp_xwing(sexp_buf);
	}

	if (fg->arrivalEvent == XWMArrivalEvent::ae_afg_identified)
	{
		if (check_wing)
			sprintf(sexp_buf, "( fotg-is-wing-identified \"%s\" )", arrival_fg_name);
		else
			sprintf(sexp_buf, "( fotg-is-ship-identified \"%s\" )", arrival_fg_name);
		return get_sexp_xwing(sexp_buf);
	}

	return Locked_sexp_true;
}

anchor_t xwi_determine_anchor(const XWingMission *xwim, const XWMFlightGroup *fg)
{
	int mothership_number = fg->mothership;

	if (mothership_number >= 0)
	{
		if (mothership_number < (int)xwim->flightgroups.size())
			return get_anchor(xwim->flightgroups[mothership_number].designation.c_str());
		else
			Warning(LOCATION, "Mothership number %d is out of range for Flight Group %s", mothership_number, fg->designation.c_str());
	}

	return anchor_t::invalid();
}

const char *xwi_determine_formation(const XWMFlightGroup *fg)
{
	switch (fg->formation)
	{
		case XWMFormation::f_Vic:
			return "Double Vic";
		case XWMFormation::f_Finger_Four:
			return "Finger Four";
		case XWMFormation::f_Line_Astern:
			return "Line Astern";
		case XWMFormation::f_Line_Abreast:
			return "Line Abreast";
		case XWMFormation::f_Echelon_Right:
			return "Echelon Right";
		case XWMFormation::f_Echelon_Left:
			return "Echelon Left";
		case XWMFormation::f_Double_Astern:
			return "Double Astern";
		case XWMFormation::f_Diamond:
			return "Diamond";
		case XWMFormation::f_Stacked:
			return "Stacked";
		case XWMFormation::f_Spread:
			return "Spread";
		case XWMFormation::f_Hi_Lo:
			return "Hi-Lo";
		case XWMFormation::f_Spiral:
			return "Spiral";
	}

	return nullptr;
}

const char *xwi_determine_base_ship_class(const XWMFlightGroup *fg)
{
	switch (fg->flightGroupType)
	{
		case XWMFlightGroupType::fg_X_Wing:
			return "T-65c X-wing";
		case XWMFlightGroupType::fg_Y_Wing:
			return "BTL-A4 Y-wing";
		case XWMFlightGroupType::fg_A_Wing:
			return "RZ-1 A-wing";
		case XWMFlightGroupType::fg_TIE_Fighter:
			return "TIE/ln Fighter";
		case XWMFlightGroupType::fg_TIE_Interceptor:
			return "TIE/In Interceptor";
		case XWMFlightGroupType::fg_TIE_Bomber:
			return "TIE/sa Bomber";
		case XWMFlightGroupType::fg_Gunboat:
			return "XG-1 Star Wing";
		case XWMFlightGroupType::fg_Transport:
			return "DX-9 Stormtrooper Transport";
		case XWMFlightGroupType::fg_Shuttle:
			return "Lambda T-4a Shuttle";
		case XWMFlightGroupType::fg_Tug:
			return "DV-3 Freighter";
		case XWMFlightGroupType::fg_Container:
			return "BFF-1 Container";
		case XWMFlightGroupType::fg_Freighter:
			return "BFF-1 Freighter";
		case XWMFlightGroupType::fg_Calamari_Cruiser:
			return "Liberty Type Star Cruiser";
		case XWMFlightGroupType::fg_Nebulon_B_Frigate:
			return "Nebulon-B Frigate";
		case XWMFlightGroupType::fg_Corellian_Corvette:
			return "CR90 Corvette#Reb";
		case XWMFlightGroupType::fg_Imperial_Star_Destroyer:
			return "Imperial Star Destroyer";
		case XWMFlightGroupType::fg_TIE_Advanced:
			return nullptr;
		case XWMFlightGroupType::fg_B_Wing:
			return "ASF-01 B-wing";
		default:
			break;
	}

	return nullptr;
}

int xwi_determine_ship_class(const XWMFlightGroup *fg)
{
	// base ship class must exist
	auto class_name = xwi_determine_base_ship_class(fg);
	if (class_name == nullptr)
		return -1;

	// let's only look for variant classes on flyable ships
	int base_class = ship_info_lookup(class_name);
	if (base_class >= 0 && Ship_info[base_class].is_fighter_bomber())
	{
		SCP_string variant_name = class_name;
		bool variant = false;

		// see if we have any variants
		if (fg->craftColor == XWMCraftColor::c_Red)
		{
			variant_name += "#red";
			variant = true;
		}
		else if (fg->craftColor == XWMCraftColor::c_Gold)
		{
			variant_name += "#gold";
			variant = true;
		}
		else if (fg->craftColor == XWMCraftColor::c_Blue)
		{
			variant_name += "#blue";
			variant = true;
		}
		else if (fg->craftColor == XWMCraftColor::c_Green)
		{
			variant_name += "#green";
			variant = true;
		}

		if (variant)
		{
			int variant_class = ship_info_lookup(variant_name.c_str());
			if (variant_class >= 0)
				return variant_class;

			Warning(LOCATION, "Could not find variant ship class %s for Flight Group %s.  Using base class instead.", variant_name.c_str(), fg->designation.c_str());
		}
	}

	// no variant, or we're just going with the base class
	return base_class;
}

const char *xwi_determine_team(const XWingMission *xwim, const XWMFlightGroup *fg, const ship_info *sip)
{
	SCP_UNUSED(sip);

	if (fg->craftIFF == XWMCraftIFF::iff_imperial)
			return "Hostile";
	if (fg->craftIFF == XWMCraftIFF::iff_rebel)
			return "Friendly";
	if (fg->craftIFF == XWMCraftIFF::iff_neutral)
		return "Civilian";

	switch (fg->flightGroupType)
	{
		case XWMFlightGroupType::fg_X_Wing:
		case XWMFlightGroupType::fg_Y_Wing:
		case XWMFlightGroupType::fg_A_Wing:
		case XWMFlightGroupType::fg_B_Wing:
		case XWMFlightGroupType::fg_Calamari_Cruiser:
		case XWMFlightGroupType::fg_Nebulon_B_Frigate:
			return "Friendly";
		case XWMFlightGroupType::fg_TIE_Fighter:
		case XWMFlightGroupType::fg_TIE_Interceptor:
		case XWMFlightGroupType::fg_TIE_Bomber:
		case XWMFlightGroupType::fg_Gunboat:
		case XWMFlightGroupType::fg_Imperial_Star_Destroyer:
		case XWMFlightGroupType::fg_TIE_Advanced:
		case XWMFlightGroupType::fg_Transport:
			return "Hostile";
		case XWMFlightGroupType::fg_Shuttle:
		case XWMFlightGroupType::fg_Tug:
		case XWMFlightGroupType::fg_Container:
		case XWMFlightGroupType::fg_Freighter:
		case XWMFlightGroupType::fg_Corellian_Corvette:
			return "Civilian";
		default:
			break;
	}

	return nullptr;
}

int xwi_lookup_cargo(const char *cargo_name)
{
	// empty cargo is the same as Nothing
	if (!*cargo_name)
		return 0;

	int index = string_lookup(cargo_name, Cargo_names, Num_cargo);
	if (index < 0)
	{
		if (Num_cargo == MAX_CARGO)
		{
			Warning(LOCATION, "Can't add any more cargo!");
			return 0;
		}

		index = Num_cargo++;
		strcpy(Cargo_names[index], cargo_name);
		SCP_totitle(Cargo_names[index]);
	}
	return index;
}

const char *xwi_determine_ai_class(const XWMFlightGroup *fg)
{
	// Rookie = Cadet
	// Officer = Officer
	// Veteran = Captain
	// Ace = Commander
	// Top Ace = General

	switch (fg->craftAI)
	{
		case XWMCraftAI::ai_Rookie:
			return "AI_04_Cadet";
		case XWMCraftAI::ai_Officer:
			return "AI_05_Officer";
		case XWMCraftAI::ai_Veteran:
			return "AI_07_Captain";
		case XWMCraftAI::ai_Ace:
			return "AI_09_Commander";
		case XWMCraftAI::ai_Top_Ace:
			return "AI_11_General";
	}

	return nullptr;
}

void xwi_determine_orientation(matrix *orient, const XWMFlightGroup *fg, const vec3d *start1, const vec3d *start2, const vec3d *start3,
	const vec3d *waypoint1, const vec3d *waypoint2, const vec3d *waypoint3, const vec3d *hyperspace)
{
	SCP_UNUSED(start2);
	SCP_UNUSED(start3);
	SCP_UNUSED(waypoint2);
	SCP_UNUSED(waypoint3);
	SCP_UNUSED(hyperspace);
	vec3d fvec;

	// RandomStarfighter says:
	// If WP1 is disabled, it has 45 degree pitch and yaw.
	if (!fg->waypoint1_enabled)
	{
		angles a;
		a.p = PI_4;
		a.b = 0;
		a.h = PI_4;
		vm_angles_2_matrix(orient, &a);
		return;
	}

	// RandomStarfighter says:
	// It arrives from start point and points toward waypoint 1, if waypoint 1 is enabled.
	// This also matches FG Red orientation in STARSNDB
	vm_vec_normalized_dir(&fvec, waypoint1, start1);
	vm_vector_2_matrix_norm(orient, &fvec);
}

void parse_xwi_flightgroup(mission *pm, const XWingMission *xwim, const XWMFlightGroup *fg)
{
	SCP_UNUSED(pm);

	int arrival_cue = xwi_determine_arrival_cue(xwim, fg);

	int number_in_wave = fg->numberInWave;
	if (number_in_wave > MAX_SHIPS_PER_WING)
	{
		Warning(LOCATION, "Too many ships in Flight Group %s.  FreeSpace supports up to a maximum of %d.", fg->designation.c_str(), MAX_SHIPS_PER_WING);
		number_in_wave = MAX_SHIPS_PER_WING;
	}

	// see if this flight group is what FreeSpace would treat as a wing
	wing *wingp = nullptr;
	int wingnum = -1;
	if (is_wing(fg))
	{
		wingnum = Num_wings++;
		wingp = &Wings[wingnum];

		strcpy_s(wingp->name, fg->designation.c_str());
		SCP_totitle(wingp->name);
		wingp->num_waves = fg->numberOfWaves;

		auto formation_name = xwi_determine_formation(fg);
		if (formation_name)
		{
			wingp->formation = wing_formation_lookup(formation_name);
			if (wingp->formation < 0)
				Warning(LOCATION, "Formation %s from Flight Group %s was not found", formation_name, fg->designation.c_str());
		}
		if (wingp->formation >= 0 && !is_fighter_or_bomber(fg))
			wingp->formation_scale = 4.0f;

		wingp->arrival_cue = arrival_cue;
		wingp->arrival_delay = fg->arrivalDelay;
		wingp->arrival_location = fg->arriveByHyperspace ? ArrivalLocation::AT_LOCATION : ArrivalLocation::FROM_DOCK_BAY;
		wingp->arrival_anchor = xwi_determine_anchor(xwim, fg);
		wingp->departure_cue = Locked_sexp_false;
		wingp->departure_location = fg->departByHyperspace ? DepartureLocation::AT_LOCATION : DepartureLocation::TO_DOCK_BAY;
		wingp->departure_anchor = wingp->arrival_anchor;

		// if a wing doesn't have an anchor, make sure it is at-location
		// (flight groups present at mission start will have arriveByHyperspace set to false)
		if (!wingp->arrival_anchor.isValid())
			wingp->arrival_location = ArrivalLocation::AT_LOCATION;
		if (!wingp->departure_anchor.isValid())
			wingp->departure_location = DepartureLocation::AT_LOCATION;

		wingp->wave_count = number_in_wave;
	}

	// all ships in the flight group share a class, so determine that here
	int ship_class = xwi_determine_ship_class(fg);
	if (ship_class < 0)
	{
		Warning(LOCATION, "Unable to determine ship class for Flight Group %s", fg->designation.c_str());
		ship_class = 0;
	}
	auto sip = &Ship_info[ship_class];

	// similarly for the team
	auto team_name = xwi_determine_team(xwim, fg, sip);
	int team = Species_info[sip->species].default_iff;
	if (team_name)
	{
		int index = iff_lookup(team_name);
		if (index >= 0)
			team = index;
		else
			Warning(LOCATION, "Could not find iff %s", team_name);
	}

	// similarly for the AI
	int ai_index = sip->ai_class;
	auto ai_name = xwi_determine_ai_class(fg);
	if (ai_name)
	{
		int index = string_lookup(ai_name, Ai_class_names, Num_ai_classes);
		if (index >= 0)
			ai_index = index;
		else
			Warning(LOCATION, "Could not find AI class %s", ai_name);
	}

	// similarly for any waypoints
	// NOTE: Y and Z are swapped
	auto start1 = vm_vec_new(fg->start1_x, fg->start1_z, fg->start1_y);
	auto start2 = vm_vec_new(fg->start2_x, fg->start2_z, fg->start2_y);
	auto start3 = vm_vec_new(fg->start3_x, fg->start3_z, fg->start3_y);
	auto waypoint1 = vm_vec_new(fg->waypoint1_x, fg->waypoint1_z, fg->waypoint1_y);
	auto waypoint2 = vm_vec_new(fg->waypoint2_x, fg->waypoint2_z, fg->waypoint2_y);
	auto waypoint3 = vm_vec_new(fg->waypoint3_x, fg->waypoint3_z, fg->waypoint3_y);
	auto hyperspace = vm_vec_new(fg->hyperspace_x, fg->hyperspace_z, fg->hyperspace_y);

	// waypoint units are in kilometers (after processing by xwinglib which handles the factor of 160), so scale them up
	vm_vec_scale(&start1, 1000);
	vm_vec_scale(&start2, 1000);
	vm_vec_scale(&start3, 1000);
	vm_vec_scale(&waypoint1, 1000);
	vm_vec_scale(&waypoint2, 1000);
	vm_vec_scale(&waypoint3, 1000);
	vm_vec_scale(&hyperspace, 1000);

	matrix orient;
	xwi_determine_orientation(&orient, fg, &start1, &start2, &start3, &waypoint1, &waypoint2, &waypoint3, &hyperspace);

	// now configure each ship in the flight group
	for (int wing_index = 0; wing_index < number_in_wave; wing_index++)
	{
		p_object pobj;

		if (wingp)
		{
			wing_bash_ship_name(&pobj, wingp, wing_index + 1);
			pobj.wingnum = wingnum;
			pobj.pos_in_wing = wing_index;
			pobj.arrival_cue = Locked_sexp_false;
			pobj.departure_cue = Locked_sexp_false;
		}
		else
		{
			strcpy_s(pobj.name, fg->designation.c_str());
			SCP_totitle(pobj.name);

			pobj.arrival_cue = arrival_cue;
			pobj.arrival_delay = fg->arrivalDelay;
			pobj.arrival_location = fg->arriveByHyperspace ? ArrivalLocation::AT_LOCATION : ArrivalLocation::FROM_DOCK_BAY;
			pobj.arrival_anchor = xwi_determine_anchor(xwim, fg);
			pobj.departure_cue = Locked_sexp_false;
			pobj.departure_location = fg->departByHyperspace ? DepartureLocation::AT_LOCATION : DepartureLocation::TO_DOCK_BAY;
			pobj.departure_anchor = pobj.arrival_anchor;

			// if a ship doesn't have an anchor, make sure it is at-location
			// (flight groups present at mission start will have arriveByHyperspace set to false)
			if (!pobj.arrival_anchor.isValid())
				pobj.arrival_location = ArrivalLocation::AT_LOCATION;
			if (!pobj.departure_anchor.isValid())
				pobj.departure_location = DepartureLocation::AT_LOCATION;
		}

		pobj.ship_class = ship_class;

		// initialize class-specific fields
		pobj.ai_class = ai_index;
		pobj.warpin_params_index = sip->warpin_params_index;
		pobj.warpout_params_index = sip->warpout_params_index;
		pobj.ship_max_shield_strength = sip->max_shield_strength;
		pobj.ship_max_hull_strength = sip->max_hull_strength;
		Assert(pobj.ship_max_hull_strength > 0.0f);	// Goober5000: div-0 check (not shield because we might not have one)
		pobj.max_shield_recharge = sip->max_shield_recharge;
		pobj.replacement_textures = sip->replacement_textures;	// initialize our set with the ship class set, which may be empty
		pobj.score = sip->score;

		pobj.team = team;
		pobj.pos = start1;
		pobj.orient = orient;

		if (wingp && wing_index == fg->specialShipNumber)
			pobj.cargo1 = (char)xwi_lookup_cargo(fg->specialCargo.c_str());
		else
			pobj.cargo1 = (char)xwi_lookup_cargo(fg->cargo.c_str());

		if (fg->craftOrder != XWMCraftOrder::o_Hold_Steady && fg->craftOrder != XWMCraftOrder::o_Starship_Sit_And_Fire)
			pobj.initial_velocity = 100;

		if (fg->playerPos == wing_index + 1)
		{
			// undo any previously set player
			if (Player_starts > 0)
			{
				auto prev_player_pobjp = mission_parse_find_parse_object(Player_start_shipname);
				if (prev_player_pobjp)
				{
					Warning(LOCATION, "This mission specifies multiple player starting ships.  Skipping %s.", Player_start_shipname);
					prev_player_pobjp->flags.remove(Mission::Parse_Object_Flags::OF_Player_start);
					Player_starts--;
				}
				else
					Warning(LOCATION, "Multiple player starts specified, but previous player start %s couldn't be found!", Player_start_shipname);
			}

			strcpy_s(Player_start_shipname, pobj.name);
			pobj.flags.set(Mission::Parse_Object_Flags::OF_Player_start);
			Player_starts++;
		}

		if (fg->craftStatus == XWMCraftStatus::cs_no_shields)
			pobj.flags.set(Mission::Parse_Object_Flags::OF_No_shields);

		if (fg->craftStatus == XWMCraftStatus::cs_no_missiles || fg->craftStatus == XWMCraftStatus::cs_half_missiles)
		{
			// the only subsystem we actually need is Pilot, because everything else uses defaults
			pobj.subsys_index = Subsys_index;
			int this_subsys = allocate_subsys_status();
			pobj.subsys_count++;
			strcpy_s(Subsys_status[this_subsys].name, NOX("Pilot"));

			for (int bank = 0; bank < MAX_SHIP_SECONDARY_BANKS; bank++)
			{
				Subsys_status[this_subsys].secondary_banks[bank] = SUBSYS_STATUS_NO_CHANGE;
				Subsys_status[this_subsys].secondary_ammo[bank] = (fg->craftStatus == XWMCraftStatus::cs_no_missiles) ? 0 : 50;
			}
		}

		Parse_objects.push_back(pobj);
	}
}

const char *xwi_determine_object_class(const XWMObject *oj)
{
	switch (oj->objectType)
	{
		case XWMObjectType::oj_Mine1:
		case XWMObjectType::oj_Mine2:
		case XWMObjectType::oj_Mine3:
		case XWMObjectType::oj_Mine4:
			return "Defense Mine#Ion";
		case XWMObjectType::oj_Satellite:
			return "Sensor Satellite#Imp";
		case XWMObjectType::oj_Nav_Buoy:
			return "Nav Buoy#real";
		case XWMObjectType::oj_Probe:
			return "Sensor Probe";
		case XWMObjectType::oj_Asteroid1:
			return "Asteroid#Small01";
		case XWMObjectType::oj_Asteroid2:
			return "Asteroid#Small02";
		case XWMObjectType::oj_Asteroid3:
			return "Asteroid#Medium01";
		case XWMObjectType::oj_Asteroid4:
			return "Asteroid#Medium02";
		case XWMObjectType::oj_Asteroid5:
			return "Asteroid#Medium03";
		case XWMObjectType::oj_Asteroid6:
			return "Asteroid#Big01";
		case XWMObjectType::oj_Asteroid7:
			return "Asteroid#Big02";
		case XWMObjectType::oj_Asteroid8:
			return "Asteroid#Big03";
		default:
			break;
	}
	return nullptr;
}

vec3d xwi_determine_mine_formation_position(const XWMObject* oj, float objectPosX, float objectPosY, float objectPosZ, 
	float offsetAxisA, float offsetAxisB)
{
	switch (oj->formation)
	{
		// Y and Z axes must be switched for FSO
		case XWMObjectFormation::ojf_FloorXY:
			return vm_vec_new((objectPosX + offsetAxisA), objectPosZ, (objectPosY + offsetAxisB));
		case XWMObjectFormation::ojf_SideYZ:
			return vm_vec_new(objectPosX, (objectPosZ + offsetAxisA), (objectPosY + offsetAxisB));
		case XWMObjectFormation::ojf_FrontXZ:
			return vm_vec_new((objectPosX + offsetAxisA), (objectPosZ + offsetAxisB), objectPosY);
		case XWMObjectFormation::ojf_Scattered:
			return vm_vec_new(objectPosX, objectPosZ, objectPosY);
		default:
			break;
	}
	return vm_vec_new(objectPosX, objectPosZ, objectPosY);
}

void xwi_determine_object_orient(matrix* orient, const XWMObject* oj)
{
	angles a;
	a.p = oj->object_pitch;
	a.b = oj->object_roll;
	a.h = oj->object_yaw;
	vm_angles_2_matrix(orient, &a);
	return;
}

// Determine the unique name from the object comprised of the object type and suffix. 
// Add the new name to the objectNameSet and return it to parse_xwi_objectgroup
const char *xwi_determine_space_object_name(SCP_set<SCP_string> &objectNameSet, const char *class_name, const int og_index)
{
	char base_name[NAME_LENGTH];
	char suffix[NAME_LENGTH];
	strcpy_s(base_name, class_name);
	end_string_at_first_hash_symbol(base_name);

	// try to make the object group index part of the name too
	if ((strlen(base_name) < NAME_LENGTH - 7) && (og_index < 26*26))
	{
		strcat_s(base_name, " ");

		int offset = og_index;
		if (offset >= 26)
		{
			sprintf(suffix, NOX("%c"), 'A' + (offset / 26) - 1);
			strcat_s(base_name, suffix);
			offset %= 26;
		}

		sprintf(suffix, NOX("%c"), 'A' + offset);
		strcat_s(base_name, suffix);
	}

	// we'll need to try suffixes starting at 1 and going until we find a unique name
	int n = 1;
	char object_name[NAME_LENGTH];
	do {
		sprintf(suffix, NOX(" %d"), n++);

		// start building name
		strcpy_s(object_name, base_name);

		// if generated name will be longer than allowable name, truncate the class section of the name by the overflow
		int char_overflow = static_cast<int>(strlen(base_name) + strlen(suffix)) - (NAME_LENGTH - 1);
		if (char_overflow > 0)
			object_name[strlen(base_name) - static_cast<size_t>(char_overflow)] = '\0';

		// complete building the name by adding suffix number and converting case
		strcat_s(object_name, suffix);
		SCP_totitle(object_name);

		// continue as long as we find the name in our set
	} while (objectNameSet.find(object_name) != objectNameSet.end());

	// name does not yet exist in the set, so it's valid; add it and return
	auto iter = objectNameSet.insert(object_name);
	return iter.first->c_str();
}

void parse_xwi_objectgroup(mission *pm, const XWingMission *xwim, const XWMObject *oj, int &object_count)
{
	SCP_UNUSED(pm);

	auto class_name = xwi_determine_object_class(oj);
	if (class_name == nullptr)
		return;

	int number_of_objects = oj->numberOfObjects;
	if (number_of_objects < 1)
		return;

	// determine which space object this is in our list
	int og_index = static_cast<int>(std::distance(xwim->objects.data(), oj));

	// object position and orientation
	// NOTE: Y and Z are swapped after all operartions are perfomed
    // units are in kilometers (after processing by xwinglib which handles the factor of 160), so scale them up
	float objectPosX = oj->object_x*1000;
	float objectPosY = oj->object_y*1000;
	float objectPosZ = oj->object_z*1000;
	float offsetAxisA = 0;
	float offsetAxisB = 0;

	int mine_dist = 400; // change this to change the distance between the mines
	auto weapon_name = "T&B KX-5#imp";
	int mine_laser_index = weapon_info_lookup(weapon_name); // "Defense Mine#Ion" needs to have its weapon changed to laser
	if (mine_laser_index < 0)
		Warning(LOCATION, "Could not find weapon %s", weapon_name);

	matrix orient;
	xwi_determine_object_orient(&orient, oj);
	
	int ship_class = ship_info_lookup(class_name);
	if (ship_class < 0)
	{
		Warning(LOCATION, "Unable to determine ship class for Object Group with type %s", class_name);
		ship_class = 0;
	}
	auto sip = &Ship_info[ship_class];

	int team = Species_info[sip->species].default_iff;

	switch (oj->objectType)
	{
		case XWMObjectType::oj_Mine1:
		case XWMObjectType::oj_Mine2:
		case XWMObjectType::oj_Mine3:
		case XWMObjectType::oj_Mine4:
		{
			auto team_name = "Hostile";
			int index = iff_lookup(team_name);
			if (index >= 0)
				team = index;
			else
				Warning(LOCATION, "Could not find iff %s", team_name);

			if (number_of_objects > 1)
			{
				offsetAxisA -= (mine_dist / 2 * (number_of_objects - 1)); // (- the distance to centre the grid)
				offsetAxisB -= (mine_dist / 2 * (number_of_objects - 1));
			}
			break;
		}
		default:
			if (number_of_objects > 1)
			{
				Warning(LOCATION, "NumberOfCraft of '%s' was %d but must be 1.", class_name, number_of_objects);
				number_of_objects = 1;
			}
			break;
	}

	// Check that the Xwing game engine object limit is not exceeded with this object group
	if (object_count + (number_of_objects * number_of_objects) > MAX_SPACE_OBJECTS) 
		return;
	object_count += (number_of_objects * number_of_objects);

	// Copy objects in Parse_objects to set for name checking below
	// This only needs to be done fully once per object group then can be added to after each new object
	SCP_set<SCP_string> objectNameSet;
	for (int n = 0; n < (int)Parse_objects.size(); n++)
		objectNameSet.insert(Parse_objects[n].name);

	// Now begin to configure each object in the group (mines multiple)
	for (int a = 0; a < number_of_objects; a++)	 // make an a-b 2d grid from the mines
	{
		for (int b = 0; b < number_of_objects; b++)  // populate the column with mines
		{
			// Convert the mine pos. (a,b) to the relavenat formation pos. ie. (x,y) or (z,y) etc
			auto ojxyz = xwi_determine_mine_formation_position(oj, objectPosX, objectPosY, objectPosZ, offsetAxisA + (mine_dist * a), offsetAxisB + (mine_dist * b));

			p_object pobj;
			strcpy_s(pobj.name, xwi_determine_space_object_name(objectNameSet, class_name, og_index));
			pobj.ship_class = ship_class;

			pobj.arrival_cue = Locked_sexp_true;
			pobj.arrival_location = ArrivalLocation::AT_LOCATION;
			pobj.departure_cue = Locked_sexp_false;
			pobj.departure_location = DepartureLocation::AT_LOCATION;
			
			pobj.ai_class = sip->ai_class;
			pobj.warpin_params_index = sip->warpin_params_index;
			pobj.warpout_params_index = sip->warpout_params_index;
			pobj.ship_max_shield_strength = sip->max_shield_strength;
			pobj.ship_max_hull_strength = sip->max_hull_strength;
			Assert(pobj.ship_max_hull_strength > 0.0f); // Goober5000: div-0 check (not shield because we might not have one)
			pobj.max_shield_recharge = sip->max_shield_recharge;

			switch (oj->objectType)
			{
				case XWMObjectType::oj_Mine1:
				case XWMObjectType::oj_Mine2:
				case XWMObjectType::oj_Mine3:
				case XWMObjectType::oj_Mine4:
				{
					pobj.subsys_index = Subsys_index;
					int this_subsys = allocate_subsys_status();
					pobj.subsys_count++;
					strcpy_s(Subsys_status[this_subsys].name, NOX("Pilot"));

					if (mine_laser_index >= 0)
					{
						for (int n = 0; n < sip->n_subsystems; n++)
						{
							auto subsys = &sip->subsystems[n];
							if (subsys->type == SUBSYSTEM_TURRET)
							{
								this_subsys = allocate_subsys_status();
								pobj.subsys_count++;
								strcpy_s(Subsys_status[this_subsys].name, sip->subsystems[n].name);

								for (int bank = 0; bank < MAX_SHIP_PRIMARY_BANKS; bank++)
								{
									if (subsys->primary_banks[bank] >= 0)
										Subsys_status[this_subsys].primary_banks[bank] = mine_laser_index;
								}
							}
						}
					}
					break;
				}
				default:
					break;
			}

			pobj.replacement_textures = sip->replacement_textures; // initialize our set with the ship class set, which may be empty
			pobj.score = sip->score;

			pobj.team = team;
			pobj.pos = ojxyz;
			pobj.orient = orient;

			pobj.initial_velocity = 0;

			pobj.flags.set(Mission::Parse_Object_Flags::SF_Hide_ship_name);	// space objects in X-Wing don't really have names

			Parse_objects.push_back(pobj);
		}
	}
}	

void post_parse_arrival_departure_anchor(const char *source_name, anchor_t &anchor)
{
	if (!anchor.isValid())
		return;
	int anchor_val = anchor.value();
	Assertion(anchor_val & ANCHOR_IS_PARSE_NAMES_INDEX, "Anchor %d must be a Parse_names index at this point!", anchor_val);
	anchor_val &= ~ANCHOR_IS_PARSE_NAMES_INDEX;
	Assertion(Parse_names.in_bounds(anchor_val), "Anchor %d is out of bounds.  Get a coder!", anchor_val);
	auto anchor_name = Parse_names[anchor_val].c_str();

	// a bit of an edge case... if an arrival or departure anchor is a wing, not a ship,
	// then change it to the first ship in that wing
	auto wingnum = wing_name_lookup(anchor_name);
	if (wingnum >= 0)
	{
		auto wingp = &Wings[wingnum];

		// since at this point we only have parse objects, find the first parse object that belongs to this wing
		bool found = false;
		for (const auto& pobj : Parse_objects)
		{
			if (pobj.wingnum == wingnum)
			{
				anchor = get_anchor(pobj.name);
				found = true;
				break;
			}
		}

		if (!found)
		{
			Warning(LOCATION, "Unable to find a ship corresponding to wing %s!", wingp->name);
			return;
		}
	}
}

void decrement_anchor(anchor_t &anchor)
{
	auto anchor_val = anchor.value();

	if (anchor_val & ANCHOR_IS_PARSE_NAMES_INDEX)
	{
		anchor_val &= ~ANCHOR_IS_PARSE_NAMES_INDEX;
		anchor_val--;
		anchor_val |= ANCHOR_IS_PARSE_NAMES_INDEX;
	}
	else
		anchor_val--;

	anchor = anchor_t(anchor_val);
}

void post_parse_remove_invalid_anchor_index(anchor_t invalid_anchor)
{
	// 1) make sure nothing actually refers to this anchor
	// 2) since this anchor will be removed, all subsequent anchors need to be reduced by 1
	for (auto &pobj : Parse_objects)
	{
		Assertion(pobj.arrival_anchor != invalid_anchor, "Arrival anchor of parse object %s refers to invalid parse name index %d", pobj.name, invalid_anchor);
		Assertion(pobj.departure_anchor != invalid_anchor, "Departure anchor of parse object %s refers to invalid parse name index %d", pobj.name, invalid_anchor);

		if (pobj.arrival_anchor > invalid_anchor)
			decrement_anchor(pobj.arrival_anchor);
		if (pobj.departure_anchor > invalid_anchor)
			decrement_anchor(pobj.departure_anchor);
	}
	for (int wingnum = 0; wingnum < Num_wings; ++wingnum)
	{
		auto &w = Wings[wingnum];

		Assertion(w.arrival_anchor != invalid_anchor, "Arrival anchor of wing %s refers to invalid parse name index %d", w.name, invalid_anchor);
		Assertion(w.departure_anchor != invalid_anchor, "Departure anchor of wing %s refers to invalid parse name index %d", w.name, invalid_anchor);

		if (w.arrival_anchor > invalid_anchor)
			decrement_anchor(w.arrival_anchor);
		if (w.departure_anchor > invalid_anchor)
			decrement_anchor(w.departure_anchor);
	}
}

void post_parse_validate_anchors()
{
	// first make sure anchors do not refer to wings
	for (auto &pobj : Parse_objects)
	{
		post_parse_arrival_departure_anchor(pobj.name, pobj.arrival_anchor);
		post_parse_arrival_departure_anchor(pobj.name, pobj.departure_anchor);
	}
	for (int wingnum = 0; wingnum < Num_wings; ++wingnum)
	{
		auto &w = Wings[wingnum];
		post_parse_arrival_departure_anchor(w.name, w.arrival_anchor);
		post_parse_arrival_departure_anchor(w.name, w.departure_anchor);
	}

	// now remove the invalid anchors so they don't cause warnings in post-processing
	for (auto it = Parse_names.begin(); it != Parse_names.end(); )
	{
		auto anchor_name = it->c_str();
		auto wingnum = wing_name_lookup(anchor_name);

		// parse names referring to wings are invalid
		if (wingnum >= 0)
		{
			int invalid_parse_index = static_cast<int>(std::distance(Parse_names.begin(), it));
			post_parse_remove_invalid_anchor_index(anchor_t(invalid_parse_index | ANCHOR_IS_PARSE_NAMES_INDEX));

			Parse_names.erase(it);
			it = Parse_names.begin();	// start over from the beginning
		}
		else
			++it;	// iterate as normal
	}
}

bool extract_stem_and_number(const char *name, char *stem, int *num)
{
	Assertion(name && stem && num, "Arguments must be non-null!");

	auto ch = strrchr(name, ' ');
	if (!ch)
		return false;
	if (!can_construe_as_integer(ch + 1))
		return false;

	int len = static_cast<int>(ch - name);
	strncpy(stem, name, len);
	stem[len] = '\0';

	*num = atoi(ch + 1);
	return true;
}

// X-Wing has at least one mission with multiple flight groups named Red 1, Red 2, etc.
void post_parse_consolidate_similar_wings()
{
	char stem[NAME_LENGTH];
	int ship_index;
	SCP_unordered_set<SCP_string, SCP_string_lcase_hash, SCP_string_lcase_equal_to> consolidated_wings;

	// first consolidate the wings
	for (int wingnum = 0; wingnum < Num_wings; ++wingnum)
	{
		auto wingp = &Wings[wingnum];
		if (wingp->wave_count > 1)
			continue;

		// see if this is one of the wings that includes a number
		if (!extract_stem_and_number(wingp->name, stem, &ship_index))
			continue;

		int stem_wingnum = find_item_with_string(Wings, Num_wings, &wing::name, stem);
		auto suffix_pos = strlen(stem);

		// for wings that are the stem and 1, just change the wing names
		if (ship_index == 1)
		{
			if (stem_wingnum >= 0)
			{
				Warning(LOCATION, "Cannot rename %s to %s; stem wing already exists!", wingp->name, stem);
				continue;
			}

			// find this parse object
			int leader_pobj_index = find_item_with_field(Parse_objects, &p_object::wingnum, wingnum);
			Assertion(leader_pobj_index >= 0, "The parse object corresponding to wing %s must exist!", wingp->name);
			auto &leader_pobj = Parse_objects[leader_pobj_index];

			// fix this ship's name
			if (!strcmp(Player_start_shipname, leader_pobj.name))
				strcpy_s(Player_start_shipname, wingp->name);
			int parse_index = string_lookup(leader_pobj.name, Parse_names);
			strcpy_s(leader_pobj.name, wingp->name);
			if (parse_index >= 0)
				Parse_names[parse_index] = leader_pobj.name;

			// fix wing name references
			for (auto ptr : Starting_wing_names)
				if (!strcmp(ptr, wingp->name))
					ptr[suffix_pos] = '\0';
			for (auto ptr : Squadron_wing_names)
				if (!strcmp(ptr, wingp->name))
					ptr[suffix_pos] = '\0';
			for (auto ptr : TVT_wing_names)
				if (!strcmp(ptr, wingp->name))
					ptr[suffix_pos] = '\0';

			// fix this wing's name
			wingp->name[suffix_pos] = '\0';

			// start back at the beginning
			wingnum = -1;
			continue;
		}

		// to merge a wing, the stem wing must exist at this point
		if (stem_wingnum < 0)
			continue;
		auto stem_wingp = &Wings[stem_wingnum];

		// for wings that are the next available number from an existing wing, merge them
		if ((ship_index == stem_wingp->wave_count + 1) && (ship_index <= MAX_SHIPS_PER_WING))
		{
			// find this parse object
			int pobj_index = find_item_with_field(Parse_objects, &p_object::wingnum, wingnum);
			Assertion(pobj_index >= 0, "The parse object corresponding to wing %s must exist!", wingp->name);
			auto &pobj = Parse_objects[pobj_index];

			// fix this ship's name
			if (!strcmp(Player_start_shipname, pobj.name))
				sprintf(Player_start_shipname, "%s %d", stem, ship_index);
			int parse_index = string_lookup(pobj.name, Parse_names);
			sprintf(pobj.name, "%s %d", stem, ship_index);
			if (parse_index >= 0)
				Parse_names[parse_index] = pobj.name;

			// fix wing name references
			for (auto ptr : Starting_wing_names)
				if (!strcmp(ptr, wingp->name))
					ptr[suffix_pos] = '\0';
			for (auto ptr : Squadron_wing_names)
				if (!strcmp(ptr, wingp->name))
					ptr[suffix_pos] = '\0';
			for (auto ptr : TVT_wing_names)
				if (!strcmp(ptr, wingp->name))
					ptr[suffix_pos] = '\0';

			// clear this wing's name since we'll delete it
			*wingp->name = '\0';

			// merge this ship into the stem wing...
			pobj.wingnum = stem_wingnum;
			pobj.pos_in_wing = ship_index - 1;
			++stem_wingp->wave_count;
			consolidated_wings.insert(stem);

			// start back at the beginning
			wingnum = -1;
			continue;
		}
	}

	if (!consolidated_wings.empty())
	{
		Do_not_reposition_wings.insert(consolidated_wings.begin(), consolidated_wings.end());

		SCP_string msg = "The following wings were consolidated from multiple source wings.  Please use the original X-Wing mission to verify that the new wings are correct.";
		for (const auto& name : consolidated_wings)
		{
			msg.append("\n\t");
			msg.append(name);
		}

		Warning(LOCATION, "%s", msg.c_str());
	}

	// and now delete any leftover wings.  We use a while loop (not a for loop)
	// because after sliding wings down past `wingnum`, the wing that slid into
	// position `wingnum` still needs to be checked -- it might itself be a
	// leftover that another iteration of consolidation produced.
	int wingnum = 0;
	while (wingnum < Num_wings)
	{
		// a leftover wing is one that is in range but has a blank name
		auto leftover_wingp = &Wings[wingnum];
		if (*leftover_wingp->name != '\0')
		{
			++wingnum;
			continue;
		}

		free_sexp2(leftover_wingp->arrival_cue);
		free_sexp2(leftover_wingp->departure_cue);

		for (int i = wingnum + 1; i < Num_wings; ++i)
			Wings[i-1] = std::move(Wings[i]);

		Wings[Num_wings-1].clear();
		--Num_wings;

		// Any wing references higher than this wing must be adjusted; any
		// reference equal to this wing should not exist (the merge phase
		// should have re-parented every pobj of a leftover wing to its stem).
		// If one slipped through, log identifying info, orphan it (wingnum =
		// -1 makes it a singleton ship instead of crashing the batch), and
		// continue.  This is defensive: an Assertion crash here aborts the
		// whole batch import, but the underlying mission can usually still be
		// converted with the orphan singleton as a minor inaccuracy.
		for (auto &pobj : Parse_objects)
		{
			if (pobj.wingnum == wingnum)
			{
				Warning(LOCATION,
					"Parse object '%s' (pos_in_wing=%d) still references leftover wing %d after consolidation; setting its wingnum to -1.  "
					"This may indicate a flight-group naming pattern the consolidator didn't expect; please report the source mission.",
					pobj.name, pobj.pos_in_wing, wingnum);
				pobj.wingnum = -1;
			}
			else if (pobj.wingnum > wingnum)
			{
				--pobj.wingnum;
			}
		}

		// Don't advance wingnum -- the slide put a different wing at this slot
		// and it might also be a leftover.
	}
}

// check to see if any wings have identical names!
void post_parse_disambiguate_wings()
{
	for (int i = 0; i < Num_wings; ++i)
	{
		int index = 1;

		for (auto j = i + 1; j < Num_wings; ++j)
		{
			if (!stricmp(Wings[i].name, Wings[j].name))
			{
				// disambiguate the later name
				sprintf(Wings[j].name, "%s#%d", Wings[i].name, index++);

				// disambiguate all the wingmen
				for (auto &pobj : Parse_objects)
				{
					if (pobj.wingnum == j)
						wing_bash_ship_name(&pobj, &Wings[j], pobj.pos_in_wing + 1);
				}
			}
		}
	}
}

// likewise for ships
void post_parse_disambiguate_ships()
{
	for (auto ii = Parse_objects.begin(); ii != Parse_objects.end(); ++ii)
	{
		int index = 1;

		for (auto jj = ii + 1; jj != Parse_objects.end(); ++jj)
		{
			if (!stricmp(ii->name, jj->name))
			{
				// disambiguate the later name
				sprintf(jj->name, "%s#%d", ii->name, index++);
			}
		}
	}
}

void parse_xwi_mission(mission *pm, const XWingMission *xwim)
{
	SCP_string sexp_buf;

	// Should match what parse_xwi_mission_info already set, but recompute
	// defensively in case _info wasn't called for this mission.
	Player_flight_group = xwi_find_player_flight_group(xwim);

	Do_not_reposition_wings.clear();

	// clear out wings by default
	for (int i = 0; i < MAX_STARTING_WINGS; i++)
		sprintf(Starting_wing_names[i], "Hidden %d", i);
	for (int i = 0; i < MAX_SQUADRON_WINGS; i++)
		sprintf(Squadron_wing_names[i], "Hidden %d", i);
	for (int i = 0; i < MAX_TVT_WINGS; i++)
		sprintf(TVT_wing_names[i], "Hidden %d", i);

	// put the player's flight group in the default spot
	strcpy_s(Starting_wing_names[0], xwim->flightgroups[Player_flight_group].designation.c_str());
	SCP_totitle(Starting_wing_names[0]);
	strcpy_s(Squadron_wing_names[0], Starting_wing_names[0]);
	strcpy_s(TVT_wing_names[0], Starting_wing_names[0]);

	// this seems like a sensible default
	auto command_persona_name = "Flight Computer";
	pm->command_persona = message_persona_name_lookup(command_persona_name);
	if (pm->command_persona >= 0)
	{
		strcpy_s(pm->command_sender, command_persona_name);	// it works as a sender too!
		pm->flags.set(Mission::Mission_Flags::Override_hashcommand);
	}
	else
		Warning(LOCATION, "Unable to find the persona '%s'", command_persona_name);

	// other mission flags
	pm->support_ships.max_support_ships = 0;

	// load flight groups
	for (const auto &fg : xwim->flightgroups)
		parse_xwi_flightgroup(pm, xwim, &fg);

	// load object groups
	int object_count = 0;
	for (const auto& obj : xwim->objects) 
		parse_xwi_objectgroup(pm, xwim, &obj, object_count);

	// post-parse stuff
	post_parse_validate_anchors();
	post_parse_consolidate_similar_wings();

	// X-Wing can have identically named ships and wings, but FSO can't
	post_parse_disambiguate_wings();
	post_parse_disambiguate_ships();


	// indicate we are using X-Wing options...
	sprintf(sexp_buf, "( when ( true ) ( do-nothing ) )");
	int marker_event_formula = get_sexp_xwing(sexp_buf);

	// option 1 - X-Wing identification
	Mission_events.emplace_back();
	auto identification_event = &Mission_events.back();
	identification_event->name = "X-Wing Style Identification";
	identification_event->formula = marker_event_formula;

	// option 2 - X-Wing identification exclusively
	Mission_events.emplace_back();
	auto scan_event = &Mission_events.back();
	scan_event->name = "XWI Set Scanning Behavior";
	sprintf(sexp_buf, "( when ( true ) ( alter-ship-flag \"cannot-perform-scan-hide-cargo\" ( true ) ( false ) \"%s\" ) )", Player_start_shipname);
	scan_event->formula = get_sexp_xwing(sexp_buf);
}

void post_process_xwi_mission(mission *pm, const XWingMission *xwim)
{
	SCP_UNUSED(pm);
	SCP_UNUSED(xwim);

	for (int wingnum = 0; wingnum < Num_wings; wingnum++)
	{
		auto wingp = &Wings[wingnum];
		auto leader_objp = &Objects[Ships[wingp->ship_index[0]].objnum];

		// don't reposition flight groups that have been consolidated
		if (!Do_not_reposition_wings.contains(wingp->name))
		{
			// we need to arrange all the flight groups into their formations, but this can't be done until the FRED objects are created from the parse objects
			for (int i = 1; i < wingp->wave_count; i++)
			{
				auto objp = &Objects[Ships[wingp->ship_index[i]].objnum];

				get_absolute_wing_pos(&objp->pos, leader_objp, wingnum, i, false);
				objp->orient = leader_objp->orient;
			}
		}

		// set the hotkeys for the starting wings
		for (int i = 0; i < MAX_STARTING_WINGS; i++)
		{
			if (!stricmp(wingp->name, Starting_wing_names[i]))
			{
				wingp->hotkey = i;
				break;
			}
		}
	}
}

// ============================================================================
// .BRF briefing -> FSO briefing conversion
// ============================================================================

static bool xwi_brf_is_ship_type(XWMBrfIconType t)
{
	switch (t)
	{
		case XWMBrfIconType::bi_X_Wing:
		case XWMBrfIconType::bi_Y_Wing:
		case XWMBrfIconType::bi_A_Wing:
		case XWMBrfIconType::bi_TIE_Fighter:
		case XWMBrfIconType::bi_TIE_Interceptor:
		case XWMBrfIconType::bi_TIE_Bomber:
		case XWMBrfIconType::bi_Assault_Gunboat:
		case XWMBrfIconType::bi_Transport:
		case XWMBrfIconType::bi_Shuttle:
		case XWMBrfIconType::bi_Tug:
		case XWMBrfIconType::bi_Container:
		case XWMBrfIconType::bi_Freighter:
		case XWMBrfIconType::bi_Calamari_Cruiser:
		case XWMBrfIconType::bi_Nebulon_B_Frigate:
		case XWMBrfIconType::bi_Corellian_Corvette:
		case XWMBrfIconType::bi_Star_Destroyer:
		case XWMBrfIconType::bi_TIE_Advanced:
		case XWMBrfIconType::bi_B_Wing:
			return true;
		default:
			return false;
	}
}

static bool xwi_brf_event_implies_map(XWMBrfEventOpcode op)
{
	switch (op)
	{
		case XWMBrfEventOpcode::op_MoveMap:
		case XWMBrfEventOpcode::op_ZoomMap:
		case XWMBrfEventOpcode::op_ClearFGTags:
		case XWMBrfEventOpcode::op_FGTag1:
		case XWMBrfEventOpcode::op_FGTag2:
		case XWMBrfEventOpcode::op_FGTag3:
		case XWMBrfEventOpcode::op_FGTag4:
		case XWMBrfEventOpcode::op_ClearTextTags:
		case XWMBrfEventOpcode::op_TextTag1:
		case XWMBrfEventOpcode::op_TextTag2:
		case XWMBrfEventOpcode::op_TextTag3:
		case XWMBrfEventOpcode::op_TextTag4:
			return true;
		default:
			return false;
	}
}

static bool is_brf_map_page(const XWingBriefing *xwBrief, const XWMBrfPage &page)
{
	// Primary signal: page's viewport set has the Map viewport (index 4) visible
	if (page.pageType >= 0 && page.pageType < (int)xwBrief->viewportSets.size())
		return xwBrief->viewportSets[page.pageType].viewports[4].visible;

	// Fallback: any map-affecting event present implies a map page
	for (const auto &event : page.events)
	{
		if (xwi_brf_event_implies_map(event.opcode))
			return true;
	}
	return false;
}

static int xwi_find_navbuoy_ship_class()
{
	for (auto it = Ship_info.cbegin(); it != Ship_info.cend(); ++it)
	{
		if (it->flags[Ship::Info_Flags::Navbuoy])
			return (int)std::distance(Ship_info.cbegin(), it);
	}
	return -1;
}

// Pattern lifted from FRED's briefing_editor_dlg::OnMakeIcon (briefingeditordlg.cpp:1102-1144).
static int xwi_pick_briefing_icon_type(int ship_class, int wing_count)
{
	if (ship_class < 0 || ship_class >= (int)Ship_info.size())
		return ICON_WAYPOINT;

	auto sip = &Ship_info[ship_class];
	bool wing = (wing_count > 1);

	if (sip->flags[Ship::Info_Flags::Knossos_device])  return ICON_KNOSSOS_DEVICE;
	if (sip->flags[Ship::Info_Flags::Corvette])        return ICON_CORVETTE;
	if (sip->flags[Ship::Info_Flags::Gas_miner])       return ICON_GAS_MINER;
	if (sip->flags[Ship::Info_Flags::Supercap])        return ICON_SUPERCAP;
	if (sip->flags[Ship::Info_Flags::Sentrygun])       return ICON_SENTRYGUN;
	if (sip->flags[Ship::Info_Flags::Awacs])           return ICON_AWACS;
	if (sip->flags[Ship::Info_Flags::Cargo])           return wing ? ICON_CARGO_WING : ICON_CARGO;
	if (sip->flags[Ship::Info_Flags::Support])         return ICON_SUPPORT_SHIP;
	if (sip->flags[Ship::Info_Flags::Fighter])         return wing ? ICON_FIGHTER_WING : ICON_FIGHTER;
	if (sip->flags[Ship::Info_Flags::Bomber])          return wing ? ICON_BOMBER_WING : ICON_BOMBER;
	if (sip->flags[Ship::Info_Flags::Freighter])       return wing ? ICON_FREIGHTER_WING_NO_CARGO : ICON_FREIGHTER_NO_CARGO;
	if (sip->flags[Ship::Info_Flags::Cruiser])         return wing ? ICON_CRUISER_WING : ICON_CRUISER;
	if (sip->flags[Ship::Info_Flags::Transport])       return wing ? ICON_TRANSPORT_WING : ICON_TRANSPORT;
	if (sip->flags[Ship::Info_Flags::Capital] || sip->flags[Ship::Info_Flags::Drydock]) return ICON_CAPITAL;
	if (sip->flags[Ship::Info_Flags::Navbuoy])         return ICON_WAYPOINT;
	return ICON_ASTEROID_FIELD;
}

// 2D briefing-map coord (in km) -> FSO 3D world position (in meters), with the
// Y/Z swap used throughout this importer.  Per the spec, the BRF Y axis is
// usually inverted from the XWI Y axis, so we negate Y to put briefing icons
// in the same world frame as the .XWI-derived ship positions (which use the
// straight XWI Y -> FSO Z mapping at xwingmissionparse.cpp:534).  BRF Z
// (altitude) is intentionally ignored: the briefing camera is dead overhead,
// so altitude would only affect perspective foreshortening, not screen
// position, and X-Wing renders all icons on a flat map regardless.
static vec3d brf_map_pos(float x_km, float y_km)
{
	auto pos = vm_vec_new(x_km, 0.0f, -y_km);
	vm_vec_scale(&pos, 1000);
	return pos;
}

// Build a "looking straight down at the XZ plane" orientation.  In FSO,
// fvec.y = -sin(pitch) (see sincos_2_matrix at vecmat.cpp:726), so pitch =
// +PI_2 yields fvec = (0, -1, 0) -- camera looking in -Y, which is what we
// want when the camera is positioned at +Y above the icon plane.
static void make_overhead_orient(matrix *orient)
{
	angles a;
	a.p = PI_2;
	a.b = 0;
	a.h = 0;
	vm_angles_2_matrix(orient, &a);
}

// Bounding box of an icon coord set (in km).  Returns false if the set is empty.
static bool brf_icon_bbox(const std::vector<XWMBrfCoordinate> &coords, float *min_x, float *max_x, float *min_y, float *max_y, float *center_x, float *center_y)
{
	if (coords.empty())
		return false;
	*min_x = *max_x = coords[0].x;
	*min_y = *max_y = coords[0].y;
	for (size_t i = 1; i < coords.size(); i++)
	{
		if (coords[i].x < *min_x) *min_x = coords[i].x;
		if (coords[i].x > *max_x) *max_x = coords[i].x;
		if (coords[i].y < *min_y) *min_y = coords[i].y;
		if (coords[i].y > *max_y) *max_y = coords[i].y;
	}
	*center_x = 0.5f * (*min_x + *max_x);
	*center_y = 0.5f * (*min_y + *max_y);
	return true;
}

static void compute_static_overview_camera(vec3d *out_pos, matrix *out_orient, const std::vector<XWMBrfCoordinate> &coords)
{
	float min_x, max_x, min_y, max_y, cx, cy;
	if (!brf_icon_bbox(coords, &min_x, &max_x, &min_y, &max_y, &cx, &cy))
	{
		*out_pos = vm_vec_new(0.0f, 5000.0f, 0.0f);
		make_overhead_orient(out_orient);
		return;
	}

	// Bounding-circle radius from the centroid, plus a 1 km pad and a 10%
	// margin so icons don't sit right at the edge.  A circular bound is
	// independent of the player's screen aspect ratio; matches the
	// vertical-match strategy in compute_map_camera.
	float radius_sq_km = 0.0f;
	for (const auto &c : coords)
	{
		float dx = c.x - cx;
		float dy = c.y - cy;
		float d2 = dx*dx + dy*dy;
		if (d2 > radius_sq_km) radius_sq_km = d2;
	}
	float padded_radius_m = (fl_sqrt(radius_sq_km) + 1.0f) * 1.1f * 1000.0f;

	// For an overhead camera at height H, FSO's vertical ground extent is
	// 2 * H * tan(Proj_fov/2), where Proj_fov = Briefing_window_FOV *
	// PROJ_FOV_FACTOR (see derivation in compute_map_camera).  To fit a
	// circle of radius R into that view we need H >= R / tan(Proj_fov/2).
	// On any aspect >= 1:1 the horizontal extent is wider than vertical, so
	// vertical is the binding constraint.
	float proj_fov = Briefing_window_FOV * PROJ_FOV_FACTOR;
	float height = padded_radius_m / fl_tan(proj_fov * 0.5f);

	if (height < 1000.0f)   height = 1000.0f;
	if (height > 100000.0f) height = 100000.0f;

	*out_pos = brf_map_pos(cx, cy);
	out_pos->xyz.y += height;
	make_overhead_orient(out_orient);
}

// Constants describing X-Wing's native briefing renderer, derived from
// Mission_XWING95.txt and YOGEME's BriefingFormXwing.cs.
//   * Pixel position: pixel = ZoomMap * waypoint / 256 (native game math;
//     YOGEME doubles this for editor display).
//   * Native VGA briefing map viewport is 103 px tall (BriefingUIPage's
//     default map page: top=12, bottom=115).
constexpr float XWING_BRF_RENDER_DIVISOR = 256.0f;
constexpr float XWING_BRF_MAP_HEIGHT_PX  = 103.0f;

// Visible vertical extent at ZoomMap=Z in meters:
//   visible_h_m = 1000 * map_height_px / (Z * units_per_km / render_divisor)
//              = (1000 * 103 * 256 / 160) / Z
//              = 164800 / Z
constexpr float XWING_VISIBLE_HEIGHT_M_NUMERATOR =
	1000.0f * XWING_BRF_MAP_HEIGHT_PX * XWING_BRF_RENDER_DIVISOR / XWING_UNITS_PER_KM;

static void compute_map_camera(vec3d *out_pos, matrix *out_orient, float target_x_km, float target_y_km, short zoom_x, short zoom_y)
{
	// Position the overhead camera so FSO's briefing view shows the same
	// vertical ground extent that the X-Wing native renderer would at this
	// ZoomMap value.  Derivation:
	//
	//   X-Wing visible vertical at ZoomMap Z (meters)
	//     = XWING_VISIBLE_HEIGHT_M_NUMERATOR / Z          (164800 / Z)
	//
	//   FSO Proj_fov = Briefing_window_FOV * PROJ_FOV_FACTOR  (3dsetup.cpp:127-129)
	//   With Window_scale.y = 1.0 and Matrix_scale.y *= 1/tan(Proj_fov/2)
	//   (3dsetup.cpp:96-100, 193-196), Proj_fov is effectively the vertical FOV.
	//   For an overhead camera at height H:  vertical_extent = 2 * H * tan(Proj_fov/2).
	//
	// Setting them equal:
	//   H = (XWING_VISIBLE_HEIGHT_M_NUMERATOR / Z) / (2 * tan(Proj_fov/2))
	//
	// Matching the vertical (rather than horizontal) means the constant is
	// independent of the player's screen aspect ratio.  Side-cropping is
	// possible on narrow aspects when an X-Wing briefing fills its full
	// 2.06:1 map area, but nothing above/below the camera ever clips.
	//
	// For asymmetric (zoom_x != zoom_y) values we use the smaller, yielding
	// a higher camera; nothing the more-zoomed axis would have shown gets
	// cropped (FSO can't reproduce X-Wing's non-square pixels, so under-
	// zooming is safer than over-zooming).
	int min_zoom = std::min(std::abs((int)zoom_x), std::abs((int)zoom_y));
	if (min_zoom < 1)
		min_zoom = 1;

	float proj_fov = Briefing_window_FOV * PROJ_FOV_FACTOR;
	float visible_h_m = XWING_VISIBLE_HEIGHT_M_NUMERATOR / static_cast<float>(min_zoom);
	float height = visible_h_m / (2.0f * fl_tan(proj_fov * 0.5f));

	if (height < 1000.0f)   height = 1000.0f;
	if (height > 100000.0f) height = 100000.0f;

	*out_pos = brf_map_pos(target_x_km, target_y_km);
	out_pos->xyz.y += height;
	make_overhead_orient(out_orient);
}

// Initialize one brief_icon entry for a given BRF icon at the given coord.
// All BRF icons map to something: ship-type icons look up their FSO ship class
// by designation against Parse_objects; everything else (and any orphan ship-
// type icon) renders as ICON_WAYPOINT.
static void make_stage_icon(brief_icon *out, const XWMBrfIcon &brf_icon, const XWMBrfCoordinate &coord, int navbuoy_class)
{
	memset(out, 0, sizeof(brief_icon));

	int ship_class = navbuoy_class;
	int team = 0;
	int icon_type = ICON_WAYPOINT;

	if (xwi_brf_is_ship_type(brf_icon.craftType))
	{
		// find an explicitly named parse object
		auto pobjp = mission_parse_find_parse_object(brf_icon.designation.c_str());
		if (!pobjp)
		{
			// find a parse object from a wing
			int wingnum = wing_name_lookup(brf_icon.designation.c_str());
			if (wingnum >= 0)
			{
				int idx = find_item_with_field(Parse_objects, &p_object::wingnum, wingnum);
				if (idx >= 0)
					pobjp = &Parse_objects[idx];
			}
		}

		if (pobjp)
		{
			ship_class = pobjp->ship_class;
			team = pobjp->team;
			icon_type = xwi_pick_briefing_icon_type(ship_class, brf_icon.numberInWave);
		}
		else
			Warning(LOCATION, "BRF icon '%s' has no matching flight group; rendering as waypoint.", brf_icon.designation.c_str());
	}

	out->ship_class = ship_class;
	out->modelnum = -1;
	out->model_instance_num = -1;
	out->type = icon_type;
	out->team = team;
	out->bitmap_id = -1;
	out->pos = brf_map_pos(coord.x, coord.y);
	out->scale_factor = 1.0f;
}

// Uppercase X-Wing-convention abbreviations for craft types that commonly
// appear inside BRF TextTag strings ("SHU ALPHA", "TRN ...", "FRT BETA", ...).
// Used as a tiebreaker when multiple icons are equally near the tag.  Non-
// craft icon types (mines, planets, etc.) have no entry.
static SCP_vector<const char *> xwi_brf_craft_keywords(XWMBrfIconType t)
{
	switch (t)
	{
		case XWMBrfIconType::bi_X_Wing:             return { "X-W" };
		case XWMBrfIconType::bi_Y_Wing:             return { "Y-W" };
		case XWMBrfIconType::bi_A_Wing:             return { "A-W" };
		case XWMBrfIconType::bi_B_Wing:             return { "B-W" };
		case XWMBrfIconType::bi_TIE_Fighter:        return { "T/F" };
		case XWMBrfIconType::bi_TIE_Interceptor:    return { "T/I" };
		case XWMBrfIconType::bi_TIE_Bomber:         return { "T/B" };
		case XWMBrfIconType::bi_TIE_Advanced:       return { "T/A" };
		case XWMBrfIconType::bi_Assault_Gunboat:    return { "GUN" };
		case XWMBrfIconType::bi_Transport:          return { "TRN" };
		case XWMBrfIconType::bi_Shuttle:            return { "SHU" };
		case XWMBrfIconType::bi_Tug:                return { "TUG" };
		case XWMBrfIconType::bi_Container:          return { "CN/" };
		case XWMBrfIconType::bi_Freighter:          return { "FRT" };
		case XWMBrfIconType::bi_Calamari_Cruiser:   return { "CRS" };
		case XWMBrfIconType::bi_Nebulon_B_Frigate:  return { "FRG" };
		case XWMBrfIconType::bi_Corellian_Corvette: return { "CRV" };
		case XWMBrfIconType::bi_Star_Destroyer:     return { "STD" };
		default: return {};
	}
}

// Score how well an icon relates to a tag's text.  0 = no relation; higher
// scores beat lower scores when picking between equally-near candidates.
// Designation match (specific) outweighs craft-type keyword match (generic).
static int xwi_score_icon_tag_match(const XWMBrfIcon &icon, const SCP_string &tag_upper)
{
	int score = 0;

	if (!icon.designation.empty())
	{
		SCP_string desig_upper(icon.designation);
		for (auto &c : desig_upper)
			c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
		if (tag_upper.find(desig_upper) != SCP_string::npos)
			score += 10;
	}

	for (const char *kw : xwi_brf_craft_keywords(icon.craftType))
	{
		if (tag_upper.find(kw) != SCP_string::npos)
		{
			score += 5;
			break;
		}
	}

	return score;
}

// Label the stage icon best matching a tag's 2D position and text.  Geometric
// nearest is the default, but if other icons within 2x the nearest distance
// have a designation or craft-type keyword matching the tag text, the highest-
// scoring one wins (distance breaks ties among equal scores).  If the chosen
// icon already has a label, allocate an ICON_WAYPOINT stub at the tag's
// position carrying the label text.
static void assign_tag_label(brief_stage *bs, float tag_x_km, float tag_y_km,
	const char *label_text, int navbuoy_class, int max_icons,
	const std::vector<XWMBrfIcon> &brf_icons)
{
	if (bs->num_icons <= 0)
		return;

	auto tag_pos = brf_map_pos(tag_x_km, tag_y_km);

	// First pass: per-icon squared distance + identify the geometric nearest.
	SCP_vector<float> d2_per_icon(bs->num_icons, 0.0f);
	int nearest = 0;
	float nearest_d2 = std::numeric_limits<float>::infinity();
	for (int i = 0; i < bs->num_icons; i++)
	{
		float dx = bs->icons[i].pos.xyz.x - tag_pos.xyz.x;
		float dz = bs->icons[i].pos.xyz.z - tag_pos.xyz.z;
		d2_per_icon[i] = dx*dx + dz*dz;
		if (d2_per_icon[i] < nearest_d2)
		{
			nearest_d2 = d2_per_icon[i];
			nearest = i;
		}
	}

	// Second pass: prefer a text-matching icon among those within 2x the
	// nearest distance (= 4x in squared form).  Stub icons synthesized by
	// earlier tags in this stage are past brf_icons.size() and ineligible
	// for text scoring (no BRF designation/type).
	int best = nearest;
	int best_score = 0;
	float threshold_d2 = nearest_d2 * 4.0f;

	SCP_string tag_upper(label_text);
	for (auto &c : tag_upper)
		c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

	for (int i = 0; i < bs->num_icons; i++)
	{
		if (d2_per_icon[i] > threshold_d2)
			continue;
		if (i >= (int)brf_icons.size())
			continue;

		int score = xwi_score_icon_tag_match(brf_icons[i], tag_upper);
		// Higher score wins; nearer breaks ties (but only when both scored).
		if (score > best_score || (score == best_score && score > 0 && d2_per_icon[i] < d2_per_icon[best]))
		{
			best_score = score;
			best = i;
		}
	}

	if (bs->icons[best].label[0] == '\0')
	{
		strncpy(bs->icons[best].label, label_text, MAX_LABEL_LEN - 1);
		bs->icons[best].label[MAX_LABEL_LEN - 1] = '\0';
		return;
	}

	if (bs->num_icons >= max_icons)
		return;
	int stub_idx = bs->num_icons++;
	auto &stub = bs->icons[stub_idx];
	memset(&stub, 0, sizeof(brief_icon));
	stub.ship_class = navbuoy_class;
	stub.modelnum = -1;
	stub.model_instance_num = -1;
	stub.type = ICON_WAYPOINT;
	stub.bitmap_id = -1;
	stub.pos = tag_pos;
	stub.scale_factor = 1.0f;
	stub.id = Cur_brief_id++;
	strncpy(stub.label, label_text, MAX_LABEL_LEN - 1);
	stub.label[MAX_LABEL_LEN - 1] = '\0';
}

// Ensure the brief_stage has its icons array available.  FRED preallocates;
// game-side parsing allocates on demand to fit num_icons.
static void ensure_stage_icons_capacity(brief_stage *bs, int capacity)
{
	if (Fred_running || bs->icons != nullptr)
		return;
	bs->icons = (brief_icon *)vm_malloc(sizeof(brief_icon) * capacity);
	Assert(bs->icons != nullptr);
	memset(bs->icons, 0, sizeof(brief_icon) * capacity);
}

// Convert an X-Wing BRF caption string into FSO briefing markup.
//
//   '$' or '\0'       -> line break.
//   leading '>'       -> strip; render the rest of the line in yellow ($y).
//   highlight==1      -> render the run in bright green ($G), unless the line
//                        is already yellow (yellow wins per the import contract).
//   0x02 / 0x01       -> inline highlight start/end markers (rare in X-Wing,
//                        standard in TIE Fighter); honored in addition to the
//                        parallel highlight[] byte array that LEC missions use.
static SCP_string xwi_caption_to_fso(const XWMBrfString &src)
{
	// Step 1: strip inline 0x02/0x01 codes and merge with the parallel
	// highlight[] byte array into a per-character bool mask.
	SCP_string clean;
	SCP_vector<bool> hl;
	clean.reserve(src.text.size());
	hl.reserve(src.text.size());
	bool inline_hl = false;
	for (size_t i = 0; i < src.text.size(); i++)
	{
		char c = src.text[i];
		if (c == 0x02) { inline_hl = true;  continue; }
		if (c == 0x01) { inline_hl = false; continue; }
		bool array_hl = (i < src.highlight.size()) && (src.highlight[i] != 0);
		clean.push_back(c);
		hl.push_back(inline_hl || array_hl);
	}

	// Step 2 + 3: split on '$' / '\0' and process each line.
	// FSO color tags need a trailing space — the colorizer eats one whitespace
	// after the tag, see brief_text_colorize() in missionbriefcommon.cpp.
	SCP_string out;
	size_t line_start = 0;
	auto flush_line = [&](size_t end) {
		bool yellow = (end > line_start && clean[line_start] == '>');
		size_t body_start = yellow ? line_start + 1 : line_start;
		if (yellow) out += "$y{ ";

		size_t i = body_start;
		while (i < end)
		{
			if (hl[i] && !yellow)
			{
				size_t run_end = i;
				while (run_end < end && hl[run_end]) ++run_end;
				out += "$G{ ";
				out.append(clean, i, run_end - i);
				out += " $}";
				i = run_end;
			}
			else
			{
				out.push_back(clean[i]);
				++i;
			}
		}
		if (yellow) out += " $}";
	};

	for (size_t i = 0; i <= clean.size(); i++)
	{
		bool is_break = (i == clean.size()) || clean[i] == '$' || clean[i] == '\0';
		if (is_break)
		{
			flush_line(i);
			if (i < clean.size()) out.push_back('\n');
			line_start = i + 1;
		}
	}
	return out;
}

static void write_placeholder_stage(brief_stage *bs)
{
	bs->text = "Prepare for the next X-Wing mission!";
	strcpy_s(bs->voice, "none.wav");
	vm_vec_zero(&bs->camera_pos);
	bs->camera_orient = IDENTITY_MATRIX;
	bs->camera_time = 500;
	bs->num_lines = 0;
	bs->num_icons = 0;
	bs->flags = 0;
	bs->formula = Locked_sexp_true;
	bs->draw_grid = true;
	bs->grid_color = Color_briefing_grid;
}

/**
 * Set up xwi briefing based on the parsed .brf file.  Walks the first BRF Map
 * page (each CaptionText in the page becomes one FSO stage with the camera/
 * highlight/tag state accumulated since the previous caption), then the BRF
 * Text pages (each becomes an FSO stage showing a static mission overview).
 *
 * The first TitleText event encountered (across all pages) is assigned as the
 * mission title.
 *
 * NOTE: This updates the global Briefings[0] with all the data necessary to
 * drive the FSO briefing screen.
 */
void parse_xwi_briefing(mission *pm, const XWingBriefing *xwBrief)
{
	auto bp = &Briefings[0];
	bp->num_stages = 0;

	if (xwBrief == nullptr || xwBrief->icons.empty() ||
		xwBrief->coordinateSets.empty() || xwBrief->pages.empty())
	{
		bp->num_stages = 1;
		write_placeholder_stage(&bp->stages[0]);
		return;
	}

	// Assign the mission title from the first TitleText event found across all
	// pages.  If the title text begins with '>' (the X-Wing yellow-centered-line
	// marker), skip that character.
	bool title_found = false;
	for (const auto &page : xwBrief->pages)
	{
		for (const auto &event : page.events)
		{
			if (event.opcode == XWMBrfEventOpcode::op_TitleText)
			{
				int s_idx = event.params[0];
				if (s_idx >= 0 && s_idx < (int)xwBrief->strings.size())
				{
					const auto &raw = xwBrief->strings[s_idx].text;
					const char *start = raw.c_str();
					if (*start == '>')
						++start;
					pm->name = start;
				}
				title_found = true;
				break;
			}
		}
		if (title_found)
			break;
	}

	int navbuoy_class = xwi_find_navbuoy_ship_class();

	// Step 1: classify pages.  Honor every Text page; honor only the first Map page.
	std::vector<const XWMBrfPage *> text_pages;
	const XWMBrfPage *map_page = nullptr;
	int map_page_count = 0;
	for (const auto &page : xwBrief->pages)
	{
		if (is_brf_map_page(xwBrief, page))
		{
			++map_page_count;
			if (map_page == nullptr)
				map_page = &page;
		}
		else
		{
			text_pages.push_back(&page);
		}
	}
	if (map_page_count > 1)
		Warning(LOCATION, "Briefing has %d map pages; only the first will be used.", map_page_count);

	// Step 2: precompute the static overview camera shared by all Text-page stages.
	vec3d static_camera_pos;
	matrix static_camera_orient;
	compute_static_overview_camera(&static_camera_pos, &static_camera_orient, xwBrief->coordinateSets[0]);

	// Helper: commit one stage with the given parameters.  Returns the next stage
	// index, or -1 if we hit MAX_BRIEF_STAGES.
	auto emit_stage = [&](int stage_idx, const SCP_string &text, const vec3d &cam_pos,
		const matrix &cam_orient, int coord_set_idx, const SCP_set<int> &highlighted,
		const std::vector<std::tuple<int, short, short>> &text_tags, int camera_time_ms) -> int
	{
		if (stage_idx >= MAX_BRIEF_STAGES)
		{
			Warning(LOCATION, "Briefing exceeded the FSO stage limit of %d; truncating.", MAX_BRIEF_STAGES);
			return -1;
		}
		auto bs = &bp->stages[stage_idx];

		bs->text = text;
		strcpy_s(bs->voice, "none.wav");
		bs->camera_pos = cam_pos;
		bs->camera_orient = cam_orient;
		bs->camera_time = camera_time_ms;
		bs->flags = 0;
		bs->formula = Locked_sexp_true;
		bs->draw_grid = true;
		bs->grid_color = Color_briefing_grid;
		bs->num_lines = 0;
		bs->num_icons = 0;

		ensure_stage_icons_capacity(bs, MAX_STAGE_ICONS);

		if (coord_set_idx < 0 || coord_set_idx >= (int)xwBrief->coordinateSets.size())
			coord_set_idx = 0;
		const auto &coords = xwBrief->coordinateSets[coord_set_idx];

		int icon_limit = std::min((int)xwBrief->icons.size(), MAX_STAGE_ICONS);
		for (int i = 0; i < icon_limit; i++)
		{
			const auto &brf_icon = xwBrief->icons[i];
			XWMBrfCoordinate fallback_coord{0.0f, 0.0f, 0.0f};
			const auto &coord = (i < (int)coords.size()) ? coords[i] : fallback_coord;
			auto &dst = bs->icons[bs->num_icons];
			make_stage_icon(&dst, brf_icon, coord, navbuoy_class);
			dst.id = i + 1;
			if (highlighted.find(i) != highlighted.end())
				dst.flags |= BI_HIGHLIGHT;
			++bs->num_icons;
		}
		if ((int)xwBrief->icons.size() > MAX_STAGE_ICONS)
		{
			Warning(LOCATION, "Briefing stage has %d icons; truncating to MAX_STAGE_ICONS = %d.",
				(int)xwBrief->icons.size(), MAX_STAGE_ICONS);
		}

		for (const auto &tag : text_tags)
		{
			int tag_idx = std::get<0>(tag);
			short tx = std::get<1>(tag);
			short ty = std::get<2>(tag);

			if (tag_idx < 0 || tag_idx >= (int)xwBrief->tags.size())
				continue;
			const std::string &tag_text = xwBrief->tags[tag_idx];
			if (tag_text.empty())
				continue;

			float tx_km = tx / XWING_UNITS_PER_KM;
			float ty_km = ty / XWING_UNITS_PER_KM;
			assign_tag_label(bs, tx_km, ty_km, tag_text.c_str(), navbuoy_class, MAX_STAGE_ICONS, xwBrief->icons);
		}

		return stage_idx + 1;
	};

	int stage_idx = 0;
	SCP_set<int> empty_set;
	std::vector<std::tuple<int, short, short>> empty_tags;

	// Step 3: emit Map-page stages first (one per CaptionText event).
	if (map_page != nullptr && stage_idx < MAX_BRIEF_STAGES)
	{
		std::vector<int> caption_event_indices;
		for (int e = 0; e < (int)map_page->events.size(); e++)
		{
			auto op = map_page->events[e].opcode;
			if (op == XWMBrfEventOpcode::op_CaptionText || op == XWMBrfEventOpcode::op_CaptionText2)
				caption_event_indices.push_back(e);
		}

		if (caption_event_indices.empty())
		{
			Warning(LOCATION, "Map page has no caption events; no map-page stages will be emitted.");
		}
		else
		{
			// Page-initial camera state: icon centroid, default zoom.
			int page_coord_set = map_page->coordinateSet;
			if (page_coord_set < 0 || page_coord_set >= (int)xwBrief->coordinateSets.size())
				page_coord_set = 0;
			const auto &page_coords = xwBrief->coordinateSets[page_coord_set];

			float min_x, max_x, min_y, max_y, init_cx, init_cy;
			if (!brf_icon_bbox(page_coords, &min_x, &max_x, &min_y, &max_y, &init_cx, &init_cy))
				init_cx = init_cy = 0.0f;
			const short init_zoom_x = 100;
			const short init_zoom_y = 100;

			// Camera / highlight state persists across caption windows; tags reset per window.
			float current_target_x = init_cx;
			float current_target_y = init_cy;
			short current_zoom_x  = init_zoom_x;
			short current_zoom_y  = init_zoom_y;
			SCP_set<int> highlighted;

			// Track the first MoveMap and ZoomMap of the page (as raw BRF shorts).
			// X-Wing's loop-back at end-of-page returns the camera to whatever the
			// FIRST MoveMap established, not to the icon centroid -- so this is the
			// correct reference for detecting the loop-back event in the final
			// caption window.  See STARSNDB.BRF page 0: opens with MoveMap(-900,
			// 670), ends with MoveMap(-900, 670), perfect loop signature.
			bool   first_move_seen   = false;
			short  first_move_x_raw  = 0;
			short  first_move_y_raw  = 0;
			bool   first_zoom_seen   = false;
			short  first_zoom_x_raw  = 0;
			short  first_zoom_y_raw  = 0;

			for (size_t cap = 0; cap < caption_event_indices.size(); cap++)
			{
				int window_start = caption_event_indices[cap];
				int window_end = (cap + 1 < caption_event_indices.size())
					? caption_event_indices[cap + 1]
					: (int)map_page->events.size();
				bool is_final_window = (cap + 1 == caption_event_indices.size());

				std::vector<std::tuple<int, short, short>> text_tags;
				SCP_string text;

				// Snapshot pre-window camera state for the loop-back rollback case.
				float pre_target_x = current_target_x;
				float pre_target_y = current_target_y;
				short pre_zoom_x  = current_zoom_x;
				short pre_zoom_y  = current_zoom_y;

				// (is_move, x_or_zoomX, y_or_zoomY)
				std::vector<std::tuple<bool, short, short>> window_camera_events;

				for (int e = window_start; e < window_end; e++)
				{
					const auto &event = map_page->events[e];
					switch (event.opcode)
					{
						case XWMBrfEventOpcode::op_CaptionText:
						case XWMBrfEventOpcode::op_CaptionText2:
						{
							int s_idx = event.params[0];
							if (s_idx >= 0 && s_idx < (int)xwBrief->strings.size())
							{
								if (!text.empty()) text += "\n";
								text += xwi_caption_to_fso(xwBrief->strings[s_idx]);
							}
							break;
						}
						case XWMBrfEventOpcode::op_MoveMap:
							if (!first_move_seen)
							{
								first_move_x_raw = event.params[0];
								first_move_y_raw = event.params[1];
								first_move_seen = true;
							}
							current_target_x = event.params[0] / XWING_UNITS_PER_KM;
							current_target_y = event.params[1] / XWING_UNITS_PER_KM;
							window_camera_events.emplace_back(true, event.params[0], event.params[1]);
							break;
						case XWMBrfEventOpcode::op_ZoomMap:
							if (!first_zoom_seen)
							{
								first_zoom_x_raw = event.params[0];
								first_zoom_y_raw = event.params[1];
								first_zoom_seen = true;
							}
							current_zoom_x = event.params[0];
							current_zoom_y = event.params[1];
							window_camera_events.emplace_back(false, event.params[0], event.params[1]);
							break;
						case XWMBrfEventOpcode::op_ClearFGTags:
							highlighted.clear();
							break;
						case XWMBrfEventOpcode::op_FGTag1:
						case XWMBrfEventOpcode::op_FGTag2:
						case XWMBrfEventOpcode::op_FGTag3:
						case XWMBrfEventOpcode::op_FGTag4:
							highlighted.insert((int)event.params[0]);
							break;
						case XWMBrfEventOpcode::op_ClearTextTags:
							text_tags.clear();
							break;
						case XWMBrfEventOpcode::op_TextTag1:
						case XWMBrfEventOpcode::op_TextTag2:
						case XWMBrfEventOpcode::op_TextTag3:
						case XWMBrfEventOpcode::op_TextTag4:
							text_tags.emplace_back(event.params[0], event.params[1], event.params[2]);
							break;
						default:
							break;
					}
				}

				// Final-window loop-back fixup: if the last camera event in the final
				// window returns the camera to the position established by the FIRST
				// camera event of the page (X-Wing briefings loop, so the last
				// MoveMap typically resets to the opening shot), drop that last
				// camera event and use the prior state.  Comparing on raw shorts
				// avoids floating-point fuzz; an exact match is the loop-back
				// signature.
				if (is_final_window && window_camera_events.size() >= 2)
				{
					bool is_move;
					short last_x, last_y;
					std::tie(is_move, last_x, last_y) = window_camera_events.back();

					bool returns_to_initial = false;
					if (is_move && first_move_seen)
						returns_to_initial = (last_x == first_move_x_raw && last_y == first_move_y_raw);
					else if (!is_move && first_zoom_seen)
						returns_to_initial = (last_x == first_zoom_x_raw && last_y == first_zoom_y_raw);

					if (returns_to_initial)
					{
						current_target_x = pre_target_x;
						current_target_y = pre_target_y;
						current_zoom_x  = pre_zoom_x;
						current_zoom_y  = pre_zoom_y;
						for (size_t i = 0; i + 1 < window_camera_events.size(); i++)
						{
							bool im;
							short cx, cy;
							std::tie(im, cx, cy) = window_camera_events[i];
							if (im)
							{
								current_target_x = cx / XWING_UNITS_PER_KM;
								current_target_y = cy / XWING_UNITS_PER_KM;
							}
							else
							{
								current_zoom_x = cx;
								current_zoom_y = cy;
							}
						}
					}
				}

				if (text.empty())
					text = "(empty caption)";

				vec3d cam_pos;
				matrix cam_orient;
				compute_map_camera(&cam_pos, &cam_orient, current_target_x, current_target_y, current_zoom_x, current_zoom_y);

				int next = emit_stage(stage_idx, text, cam_pos, cam_orient, page_coord_set, highlighted, text_tags, 1500);
				if (next < 0) break;
				stage_idx = next;
			}
		}
	}

	// Step 4: emit Text-page stages last (static overview view, no highlights).
	for (const auto *page : text_pages)
	{
		// Some BRF authoring tools emit consecutive identical CaptionText events
		// (LEIA.BRF's Text page fires CaptionText(string 5) at both t=0 and t=1,
		// which would otherwise concatenate the same paragraph twice in the same
		// stage).  Dedupe a caption against the most recently appended one.  A
		// ClearText resets the "last seen" so a re-display after a clear is
		// honored.
		SCP_string text;
		int last_caption_idx = -1;
		for (const auto &event : page->events)
		{
			if (event.opcode == XWMBrfEventOpcode::op_ClearText)
			{
				text.clear();
				last_caption_idx = -1;
			}
			else if (event.opcode == XWMBrfEventOpcode::op_CaptionText || event.opcode == XWMBrfEventOpcode::op_CaptionText2)
			{
				int s_idx = event.params[0];
				if (s_idx < 0 || s_idx >= (int)xwBrief->strings.size())
					continue;
				if (s_idx == last_caption_idx)
					continue;
				if (last_caption_idx >= 0 && xwBrief->strings[s_idx].text == xwBrief->strings[last_caption_idx].text)
					continue;
				if (!text.empty()) text += "\n";
				text += xwi_caption_to_fso(xwBrief->strings[s_idx]);
				last_caption_idx = s_idx;
			}
			// TitleText ignored: FSO renders the mission title separately.
			// WaitForClick, EndBriefing, None: no-ops.
			// Map events shouldn't appear in text pages by construction.
		}
		if (text.empty())
			text = "(empty briefing page)";

		int next = emit_stage(stage_idx, text, static_camera_pos, static_camera_orient, 0, empty_set, empty_tags, 500);
		if (next < 0) break;
		stage_idx = next;
	}

	bp->num_stages = stage_idx;

	if (bp->num_stages == 0)
	{
		bp->num_stages = 1;
		write_placeholder_stage(&bp->stages[0]);
	}
}

