#include "iff_defs/iff_defs.h"
#include "mission/missionparse.h"
#include "mission/missiongoals.h"
#include "mission/missionmessage.h"
#include "missionui/redalert.h"
#include "nebula/neb.h"
#include "parse/parselo.h"
#include "ship/ship.h"
#include "species_defs/species_defs.h"
#include "starfield/starfield.h"
#include "weapon/weapon.h"

#include "xwingbrflib.h"
#include "xwinglib.h"
#include "xwingmissionparse.h"

extern int allocate_subsys_status();

static int Player_flight_group = 0;
static SCP_unordered_set<SCP_string, SCP_string_lcase_hash, SCP_string_lcase_equal_to> Do_not_reposition_wings;

const int MAX_SPACE_OBJECTS = 64; // To match the XWing game engine limit

// vazor222
void parse_xwi_mission_info(mission *pm, const XWingMission *xwim)
{
	pm->author = "X-Wing";
	strcpy_s(pm->created, "00/00/00 at 00:00:00");

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
	char sexp_buf[NAME_LENGTH + 50];

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
	Mp = sexp_buf;
	event->formula = get_sexp_main();
}

int xwi_determine_arrival_cue(const XWingMission *xwim, const XWMFlightGroup *fg)
{
	const XWMFlightGroup *arrival_fg = nullptr;
	char arrival_fg_name[NAME_LENGTH] = "";
	char sexp_buf[NAME_LENGTH * 2 + 80];

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
		Mp = sexp_buf;
		return get_sexp_main();
	}

	if (fg->arrivalEvent == XWMArrivalEvent::ae_afg_attacked)
	{
		xwi_add_attack_check(xwim, arrival_fg);

		if (check_wing)
			sprintf(sexp_buf, "( fotg-is-wing-attacked \"%s\" )", arrival_fg_name);
		else
			sprintf(sexp_buf, "( fotg-is-ship-attacked \"%s\" )", arrival_fg_name);
		Mp = sexp_buf;
		return get_sexp_main();
	}

	if (fg->arrivalEvent == XWMArrivalEvent::ae_afg_captured)
	{
		if (check_wing)
			sprintf(sexp_buf, "( fotg-is-wing-captured \"%s\" )", arrival_fg_name);
		else
			sprintf(sexp_buf, "( fotg-is-ship-captured \"%s\" )", arrival_fg_name);
		Mp = sexp_buf;
		return get_sexp_main();
	}

	if (fg->arrivalEvent == XWMArrivalEvent::ae_afg_destroyed)
	{
		// X-Wing treats destruction for arrivals slightly differently
		if (check_wing)
			sprintf(sexp_buf, "( and ( percent-ships-destroyed 1 \"%s\" ) ( destroyed-or-departed-delay 0 \"%s\" ) )", arrival_fg_name, arrival_fg_name);
		else
			sprintf(sexp_buf, "( is-destroyed-delay 0 \"%s\" )", arrival_fg_name);
		Mp = sexp_buf;
		return get_sexp_main();
	}

	if (fg->arrivalEvent == XWMArrivalEvent::ae_afg_disabled)
	{
		if (check_wing)
			sprintf(sexp_buf, "( fotg-is-wing-disabled \"%s\" )", arrival_fg_name);
		else
			sprintf(sexp_buf, "( fotg-is-ship-disabled \"%s\" )", arrival_fg_name);
		Mp = sexp_buf;
		return get_sexp_main();
	}

	if (fg->arrivalEvent == XWMArrivalEvent::ae_afg_identified)
	{
		if (check_wing)
			sprintf(sexp_buf, "( fotg-is-wing-identified \"%s\" )", arrival_fg_name);
		else
			sprintf(sexp_buf, "( fotg-is-ship-identified \"%s\" )", arrival_fg_name);
		Mp = sexp_buf;
		return get_sexp_main();
	}

	return Locked_sexp_true;
}

int xwi_determine_anchor(const XWingMission *xwim, const XWMFlightGroup *fg)
{
	int mothership_number = fg->mothership;

	if (mothership_number >= 0)
	{
		if (mothership_number < (int)xwim->flightgroups.size())
			return get_parse_name_index(xwim->flightgroups[mothership_number].designation.c_str());
		else
			Warning(LOCATION, "Mothership number %d is out of range for Flight Group %s", mothership_number, fg->designation.c_str());
	}

	return -1;
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

	switch (fg->flightGroupType) {
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
		if (wingp->arrival_anchor < 0)
			wingp->arrival_location = ArrivalLocation::AT_LOCATION;
		if (wingp->departure_anchor < 0)
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
			wing_bash_ship_name(pobj.name, wingp->name, wing_index + 1, nullptr);
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
			if (pobj.arrival_anchor < 0)
				pobj.arrival_location = ArrivalLocation::AT_LOCATION;
			if (pobj.departure_anchor < 0)
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
					prev_player_pobjp->flags.remove(Mission::Parse_Object_Flags::SF_Cargo_known);
					prev_player_pobjp->flags.remove(Mission::Parse_Object_Flags::SF_Cannot_perform_scan);
					Player_starts--;
				}
				else
					Warning(LOCATION, "Multiple player starts specified, but previous player start %s couldn't be found!", Player_start_shipname);
			}

			strcpy_s(Player_start_shipname, pobj.name);
			pobj.flags.set(Mission::Parse_Object_Flags::OF_Player_start);
			pobj.flags.set(Mission::Parse_Object_Flags::SF_Cargo_known);
			pobj.flags.set(Mission::Parse_Object_Flags::SF_Cannot_perform_scan);
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
	switch (oj->objectType) {
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
	switch (oj->formation) {  // Y and Z axes must be switched for FSO
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
	if ((strlen(base_name) < NAME_LENGTH - 7) && (og_index < 26*26)) {
		strcat_s(base_name, " ");

		int offset = og_index;
		if (offset >= 26) {
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
		if (char_overflow > 0) {
			object_name[strlen(base_name) - static_cast<size_t>(char_overflow)] = '\0';
		}

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
	if (ship_class < 0) {
		Warning(LOCATION, "Unable to determine ship class for Object Group with type %s", class_name);
		ship_class = 0;
	}
	auto sip = &Ship_info[ship_class];

	int team = Species_info[sip->species].default_iff;

	switch (oj->objectType) {
	case XWMObjectType::oj_Mine1:
	case XWMObjectType::oj_Mine2:
	case XWMObjectType::oj_Mine3:
	case XWMObjectType::oj_Mine4: {
		auto team_name = "Hostile";
		int index = iff_lookup(team_name);
		if (index >= 0)
			team = index;
		else
			Warning(LOCATION, "Could not find iff %s", team_name);

		if (number_of_objects > 1) {
			offsetAxisA -= (mine_dist / 2 * (number_of_objects - 1)); // (- the distance to centre the grid)
			offsetAxisB -= (mine_dist / 2 * (number_of_objects - 1));
		}
		break;
	}
	default:
		if (number_of_objects > 1) {
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
	for (int n = 0; n < (int)Parse_objects.size(); n++) {
		objectNameSet.insert(Parse_objects[n].name);
	}

	// Now begin to configure each object in the group (mines multiple)
	for (int a = 0; a < number_of_objects; a++) { // make an a-b 2d grid from the mines
		for (int b = 0; b < number_of_objects; b++) {  // populate the column with mines

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

			switch (oj->objectType) {
			case XWMObjectType::oj_Mine1:
			case XWMObjectType::oj_Mine2:
			case XWMObjectType::oj_Mine3:
			case XWMObjectType::oj_Mine4: {
				pobj.subsys_index = Subsys_index;
				int this_subsys = allocate_subsys_status();
				pobj.subsys_count++;
				strcpy_s(Subsys_status[this_subsys].name, NOX("Pilot"));

				if (mine_laser_index >= 0) {
					for (int n = 0; n < sip->n_subsystems; n++) {
						auto subsys = &sip->subsystems[n];
						if (subsys->type == SUBSYSTEM_TURRET) {
							this_subsys = allocate_subsys_status();
							pobj.subsys_count++;
							strcpy_s(Subsys_status[this_subsys].name, sip->subsystems[n].name);

							for (int bank = 0; bank < MAX_SHIP_PRIMARY_BANKS; bank++) {
								if (subsys->primary_banks[bank] >= 0) {
									Subsys_status[this_subsys].primary_banks[bank] = mine_laser_index;
								}
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

void post_parse_arrival_departure_anchor(const char *source_name, int &anchor)
{
	if (anchor < 0)
		return;
	auto anchor_name = Parse_names[anchor].c_str();

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
				anchor = get_parse_name_index(pobj.name);
				anchor_name = pobj.name;
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

void post_parse_remove_invalid_anchor_index(int invalid_anchor)
{
	// 1) make sure nothing actually refers to this anchor
	// 2) since this anchor will be removed, all subsequent anchors need to be reduced by 1
	for (auto &pobj : Parse_objects)
	{
		Assertion(pobj.arrival_anchor != invalid_anchor, "Arrival anchor of parse object %s refers to invalid parse name index %d", pobj.name, invalid_anchor);
		Assertion(pobj.departure_anchor != invalid_anchor, "Departure anchor of parse object %s refers to invalid parse name index %d", pobj.name, invalid_anchor);

		if (pobj.arrival_anchor > invalid_anchor)
			--pobj.arrival_anchor;
		if (pobj.departure_anchor > invalid_anchor)
			--pobj.departure_anchor;
	}
	for (int wingnum = 0; wingnum < Num_wings; ++wingnum)
	{
		auto &w = Wings[wingnum];

		Assertion(w.arrival_anchor != invalid_anchor, "Arrival anchor of wing %s refers to invalid parse name index %d", w.name, invalid_anchor);
		Assertion(w.departure_anchor != invalid_anchor, "Departure anchor of wing %s refers to invalid parse name index %d", w.name, invalid_anchor);

		if (w.arrival_anchor > invalid_anchor)
			--w.arrival_anchor;
		if (w.departure_anchor > invalid_anchor)
			--w.departure_anchor;
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
			int invalid_anchor = static_cast<int>(std::distance(Parse_names.begin(), it));
			post_parse_remove_invalid_anchor_index(invalid_anchor);

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

	// and now delete any leftover wings
	for (int wingnum = 0; wingnum < Num_wings; ++wingnum)
	{
		// a leftover wing is one that is in range but has a blank name
		auto leftover_wingp = &Wings[wingnum];
		if (*leftover_wingp->name != '\0')
			continue;

		free_sexp2(leftover_wingp->arrival_cue);
		free_sexp2(leftover_wingp->departure_cue);

		for (int i = wingnum + 1; i < Num_wings; ++i)
			Wings[i-1] = std::move(Wings[i]);

		Wings[Num_wings-1].clear();
		--Num_wings;

		// any wing references higher than this wing should be adjusted
		for (auto &pobj : Parse_objects)
		{
			Assertion(pobj.wingnum != wingnum, "There should not be any more references to leftover wing %d!", wingnum);
			if (pobj.wingnum > wingnum)
				--pobj.wingnum;
		}
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
					{
						bool needs_display_name;
						wing_bash_ship_name(pobj.name, Wings[j].name, pobj.pos_in_wing + 1, &needs_display_name);

						// set up display name if we need to
						if (needs_display_name)
						{
							pobj.display_name = pobj.name;
							end_string_at_first_hash_symbol(pobj.display_name);
							pobj.flags.set(Mission::Parse_Object_Flags::SF_Has_display_name);
						}
					}
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
	int index = -1;
	char sexp_buf[35];

	// find player flight group
	for (int i = 0; i < (int)xwim->flightgroups.size(); i++)
	{
		if (xwim->flightgroups[i].playerPos > 0)
		{
			index = i;
			// don't break in case multiple FGs set a player - we will use the last one assigned
		}
	}
	if (index >= 0)
		Player_flight_group = index;
	else
	{
		Warning(LOCATION, "Player flight group not found?");
		Player_flight_group = 0;
	}

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

	// indicate we are using X-Wing options
	Mission_events.emplace_back();
	auto config_event = &Mission_events.back();
	config_event->name = "XWI Import";

	sprintf(sexp_buf, "( when ( true ) ( do-nothing ) )");
	Mp = sexp_buf;
	config_event->formula = get_sexp_main();

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

/**
* Set up xwi briefing based on assumed .brf file in the same folder. If .brf is not there, 
* just use minimal xwi briefing. 
*
* NOTE: This updates the global Briefing struct with all the data necessary to drive the briefing
*/
void parse_xwi_briefing(mission *pm, const XWingBriefing *xwBrief)
{
	SCP_UNUSED(pm);
	SCP_UNUSED(xwBrief);

	auto bp = &Briefings[0];
	bp->num_stages = 1;  // xwing briefings only have one stage
	auto bs = &bp->stages[0];

	/*
	if (xwBrief != NULL)
	{
		bs->text = xwBrief->message1;  // this?
		bs->text = xwBrief->ships[2].designation;  // or this?
		bs->num_icons = xwBrief->header.icon_count;  // VZTODO is this the right place to store this?
	}
	else
	*/
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
	}
}

