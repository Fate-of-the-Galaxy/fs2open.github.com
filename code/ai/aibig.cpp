/*
 * Copyright (C) Volition, Inc. 1999.  All rights reserved.
 *
 * All source code herein is the property of Volition, Inc. You may not sell 
 * or otherwise commercially exploit the source or things you created based on the 
 * source.
 *
*/



#include "ai/aibig.h"
#include "ai/aigoals.h"
#include "freespace.h"
#include "globalincs/linklist.h"
#include "iff_defs/iff_defs.h"
#include "io/timer.h"
#include "math/staticrand.h"
#include "mission/missionparse.h"
#include "object/objcollide.h"
#include "object/object.h"
#include "ship/afterburner.h"
#include "ship/ship.h"
#include "utils/Random.h"
#include "weapon/weapon.h"

#ifdef _MSC_VER
#pragma optimize("", off)
#pragma auto_inline(off)
#endif

#define SCAN_FIGHTERS_INTERVAL	2000		// how often an AI fighter/bomber should scan for enemy fighter/bombers
														// if sitting still and pounding on a big ship.  If enemy fighters are
														// close ( < ENTER_STRAFE_THREAT_DIST ), then enter AIM_STRAFE

#define ENTER_STRAFE_THREAT_DIST_SQUARED	360000	// use squared distance, instead of 600

#define MIN_DOT_TO_ATTACK_SUBSYS				0.7f
#define MIN_DOT_TO_ATTACK_MOVING_SUBSYS	0.97f

// AI BIG MAGIC NUMBERS
// Select strafing options are now exposed to modders  --wookieejedi
#define	STRAFE_RETREAT_COLLIDE_TIME	2.0		// when anticipated collision time is less than this, begin retreat
#define	STRAFE_RETREAT_COLLIDE_DIST	100		// when perpendicular distance to *surface* is less than this, begin retreat

#define	EVADE_BOX_BASE_DISTANCE			300		// standard distance to end evade submode
#define	EVADE_BOX_MIN_DISTANCE			200		// minimun distance to end evade submode, after long time

#define	ATTACK_STOP_DISTANCE				150		// when distance to target is less than this, put on brakes

#define	ATTACK_COLLIDE_BASE_DIST		300		// absolute distance at which to begin checking for possible collision
#define	ATTACK_COLLIDE_AVOID_DIST		60			// perpendicular distance to attack surface at which begin avoiding
#define	ATTACK_COLLIDE_AVOID_TIME		1.0		// anticipated collision time at which to begin evade
#define	ATTACK_COLLIDE_SLOW_DIST		150		// perpendicular distance to attack surface at which begin slowing down
#define	ATTACK_COLLIDE_SLOW_TIME		1.5		// anticipated collision time at which to begin slowing down

#define GLIDE_STRAFE_DISTANCE			50.0f	//Distance from the ship to pass when glide strafing
#define GLIDE_STRAFE_MIN_TIME			2	//Minimum amount of time to stay on one glide strafe approach vector
#define GLIDE_STRAFE_MAX_TIME			15	//Maximum amount of time for each glide strafe pass

// forward declarations
void	ai_big_evade_ship();
void	ai_big_chase_attack(ai_info *aip, ship_info *sip, vec3d *enemy_pos, float dist_to_enemy, int modelnum);
void	ai_big_avoid_ship();
bool	ai_big_maybe_follow_subsys_path(bool do_dot_check=true);
void ai_big_strafe_position();
static bool ai_big_strafe_maybe_retreat(const vec3d *target_pos);

extern void compute_desired_rvec(vec3d *rvec, const vec3d *goal_pos, const vec3d *cur_pos);
extern void big_ship_collide_recover_start(const object *objp, const object *big_objp, const vec3d *collision_normal);


//	Called by ai_big_pick_attack_point.
//	Generates a random attack point.
//	If truly_random flag set (haha), then generate a pretty random number.  Otherwise, generate a static rand which
//	tends to not change from frame to frame.
//	Try four times and choose nearest point to increase chance of getting a good point.
void ai_bpap(const object *objp, const vec3d *attacker_objp_pos, const vec3d *attacker_objp_fvec, vec3d *attack_point, vec3d *local_attack_point, float fov, float weapon_travel_dist, vec3d *surface_normal, const ship_subsys *ss)
{
	float		nearest_dist;
	vec3d	result_point, best_point;
	vec3d	rel_point;
	int		num_tries;
	int		i;	
	ship_info *sip = &Ship_info[Ships[objp->instance].ship_info_index];
	polymodel* pm = model_get(sip->model_num);
	model_subsystem *tp = NULL;
	if (ss != NULL)
		tp = ss->system_info;	

	best_point = objp->pos;
	nearest_dist = weapon_travel_dist;

	// attempt anywhere from 16 to 4 times, more if we're closer
	num_tries = (int) (vm_vec_dist(&objp->pos, attacker_objp_pos)/objp->radius);
	if (num_tries >= 4)
		num_tries = 1;
	else
		num_tries = (4 - num_tries) * 4;

	vm_vec_zero(local_attack_point);

	auto collision_tree = model_get_bsp_collision_tree(pm->submodel[pm->detail[0]].collision_tree_index);

	if (!collision_tree->poly_centers.empty()) {
		int num_polies = (int)collision_tree->poly_centers.size();

		if (num_tries > num_polies)
			num_tries = num_polies;

		for (i=0; i<num_tries; i++) {
			int	index;
			float	dist, dot;
			vec3d	v2p;

			index = (int) (frand() * num_polies);

			rel_point = collision_tree->poly_centers[index];
			vm_vec_unrotate(&result_point, &rel_point, &objp->orient);
			vm_vec_add2(&result_point, &objp->pos);

			dist = vm_vec_normalized_dir(&v2p, &result_point, attacker_objp_pos);
			bool in_fov = false;

			dot = vm_vec_dot(&v2p, attacker_objp_fvec);
			if (tp == NULL) {
				if (dot > fov)
					in_fov = true;
			} else {
				in_fov = turret_fov_test(ss, attacker_objp_fvec, &v2p);
			}

			if (in_fov) {
				if (dist < nearest_dist) {
					nearest_dist = dist;
					best_point = result_point;
					*local_attack_point = rel_point;
					Assert( !vm_is_vec_nan(local_attack_point) );
					if (dot > (1.0f + fov)/2.0f)	//	If this point is quite good, quit searching for a better one.
						goto done_1;
				}
			}
		}
	}
done_1:

	*attack_point = best_point;

	//The following raycast tends to make up 10%(!) of total AI-frametime for basically no benefit, especially in the common case for turrets where the normal isn't even queried.
	if (surface_normal == nullptr && Disable_expensive_turret_target_check)
		return;

	// Cast from attack_objp_pos to local_attack_pos and check for nearest collision.
	// If no collision, cast to (0,0,0) [center of big ship]**  [best_point initialized to 000]

	// do in world coords to get attack point, then translate to local for local_attack_point
	vec3d attack_dir, end_point, temp;
	float dist;
	dist = vm_vec_normalized_dir(&attack_dir, attack_point, attacker_objp_pos);

	if (dist > 0.1) {
		vm_vec_scale_add(&end_point, attack_point, &attack_dir, 30.0f);
	} else {
		vm_vec_scale_add(&end_point, attack_point, attacker_objp_fvec, 30.0f);
	}
	
	mc_info mc;
	mc.model_instance_num = Ships[objp->instance].model_instance_num;
	mc.model_num = sip->model_num;
	mc.orient = &objp->orient;
	mc.pos = &objp->pos;
	mc.p0 = attacker_objp_pos;
	mc.p1 = &end_point;
	mc.flags = MC_CHECK_MODEL;
	mc.radius = 0.0f;
	model_collide(&mc);

	if (mc.num_hits > 0) {
		*attack_point = mc.hit_point_world;
		vm_vec_sub(&temp, attack_point, &objp->pos);
		vm_vec_rotate(local_attack_point, &temp, &objp->orient);
		if (surface_normal) {
			vm_vec_unrotate(surface_normal, &mc.hit_normal, &objp->orient);
		}
	} else {
		vm_vec_zero(local_attack_point);
		*attack_point = objp->pos;
		if (surface_normal) {
			vm_vec_zero(surface_normal);
		}
	}
}

//	Stuff a point to attack based on nearest octant.
//	If no points in that octant, leave attack_point unmodified.
//
//	Note: Default value for fov is 1.0f  1.0f means don't use fov parameter.
//	If fov != 1.0f, try up to four times to find a point that's in the field of view.
void ai_big_pick_attack_point_turret(const object *objp, ship_subsys *ssp, const vec3d *gpos, const vec3d *gvec, vec3d *attack_point, float fov, float weapon_travel_dist)
{
	if (!timestamp_elapsed(ssp->turret_pick_big_attack_point_timestamp)) {
		vec3d	result_point;
		vm_vec_unrotate(&result_point, &ssp->turret_big_attack_point, &objp->orient);
		vm_vec_add(attack_point, &result_point, &objp->pos);
	} else {
		vec3d	local_attack_point;
		ssp->turret_pick_big_attack_point_timestamp = timestamp(2000 + (int) (frand()*500.0f));
		ai_bpap(objp, gpos, gvec, attack_point, &local_attack_point, fov, weapon_travel_dist, NULL, ssp);
		ssp->turret_big_attack_point = local_attack_point;
	}
}

//	Stuff a point to attack based on nearest octant.
//	If no points in that octant, leave attack_point unmodified.
//
//	Note: Default value for fov is 1.0f  1.0f means don't use fov parameter.
//	If fov != 1.0f, try up to four times to find a point that's in the field of view.
//	Note, attacker_objp can be a ship or a weapon.
void ai_big_pick_attack_point(const object *objp, const object *attacker_objp, vec3d *attack_point, float fov)
{
	Assert(objp->instance > -1);
	Assert(objp->type == OBJ_SHIP);

	vec3d	local_attack_point;

	switch (attacker_objp->type) {
	case OBJ_SHIP: {
		ai_info	*attacker_aip;
		attacker_aip = &Ai_info[Ships[attacker_objp->instance].ai_index];
		if (!timestamp_elapsed(attacker_aip->pick_big_attack_point_timestamp)) {
			vec3d	result_point;

			vm_vec_unrotate(&result_point, &attacker_aip->big_attack_point, &objp->orient);
			vm_vec_add(attack_point, &result_point, &objp->pos);

			return;
		}
	
		attacker_aip->pick_big_attack_point_timestamp = timestamp(2000 + (int) (frand()*500.0f));
		break;
						}
	case OBJ_WEAPON: {
		weapon	*wp = &Weapons[attacker_objp->instance];

		if (!timestamp_elapsed(wp->pick_big_attack_point_timestamp)) {
			vec3d	result_point;

			vm_vec_unrotate(&result_point, &wp->big_attack_point, &objp->orient);
			vm_vec_add(attack_point, &result_point, &objp->pos);

			return;
		}
		wp->pick_big_attack_point_timestamp = timestamp(2000 + (int) (frand()*500.0f));
	
		break;
						  }
	}

	// checks valid line to target
	vec3d surface_normal;
	ai_bpap(objp, &attacker_objp->pos, &attacker_objp->orient.vec.fvec, attack_point, &local_attack_point, fov, 99999.9f, &surface_normal, NULL);

	switch (attacker_objp->type) {
	case OBJ_SHIP: {
		ai_info	*attacker_aip;
		// if we can't find a new local_attack_point don't change local_attack_point
		if (vm_vec_mag_squared(&local_attack_point) < 1) {
			return;
		}

		attacker_aip = &Ai_info[Ships[attacker_objp->instance].ai_index];
		attacker_aip->big_attack_point = local_attack_point;
		attacker_aip->big_attack_surface_normal = surface_normal;
		break;
						}
	case OBJ_WEAPON: {
		weapon	*wp = &Weapons[attacker_objp->instance];
		wp->big_attack_point = local_attack_point;
		Assert( !vm_is_vec_nan(&wp->big_attack_point) );
		break;
						  }
	}

	return;
}

// Handler for SM_EVADE submode ( called from ai_big_chase() )
void ai_big_evade_ship()
{
	vec3d	player_pos, enemy_pos;
	float		dist;
	ship		*shipp = &Ships[Pl_objp->instance];
	ai_info	*aip = &Ai_info[shipp->ai_index];
	vec3d	randvec, semi_enemy_pos;

	ai_set_positions(Pl_objp, En_objp, aip, &player_pos, &enemy_pos);

	dist = vm_vec_dist_quick(&player_pos, &enemy_pos);
	vm_vec_rand_vec_quick(&randvec);
	if ((Missiontime>>14) & 1) {
		vm_vec_scale_add(&semi_enemy_pos, &enemy_pos, &randvec, dist/2.0f);
		aip->prev_goal_point = semi_enemy_pos;
	} else {
		semi_enemy_pos = aip->prev_goal_point;
	}
	
	accelerate_ship(aip, 1.0f - ((Missiontime>>8) & 0x3f)/128.0f );
	turn_away_from_point(Pl_objp, &semi_enemy_pos, 0.0f);

	float box_dist;
	vec3d box_vec;
	int is_inside;
	box_dist = get_world_closest_box_point_with_delta(&box_vec, En_objp, &player_pos, &is_inside, EVADE_BOX_BASE_DISTANCE);
	if (box_dist > EVADE_BOX_BASE_DISTANCE) {
		aip->submode = SM_ATTACK;
		aip->submode_start_time = Missiontime;
	} else if ((box_dist > EVADE_BOX_MIN_DISTANCE) && (Missiontime - aip->submode_start_time > i2f(5)) ) {
		aip->submode = SM_ATTACK;
		aip->submode_start_time = Missiontime;
	}

	//TODO TEST

/*	if (dist > 4*En_objp->radius) {
		aip->submode = SM_ATTACK;
		aip->submode_start_time = Missiontime;
	} else if (dist > En_objp->radius) {
		if (Missiontime - aip->submode_start_time > i2f(5)) {
			aip->submode = SM_ATTACK;
			aip->submode_start_time = Missiontime;
		}
	} */
}

// Handler for SM_AVOID submode ( called from ai_big_chase() )
void ai_big_avoid_ship()
{
	ai_big_evade_ship();
}

// reset path following information
void ai_big_subsys_path_cleanup(ai_info *aip)
{
	if ( aip->ai_flags[AI::AI_Flags::On_subsys_path] ) {
		aip->ai_flags.remove(AI::AI_Flags::On_subsys_path);
		aip->path_goal_dist = -1;
		aip->path_start = -1;
		aip->path_cur = -1;
		aip->path_length = 0;
	}
}

// Maybe Pl_objp needs to follow a path to get in line-of-sight to a subsystem
// input:	do_dot_check	=>	default value true, flag to indicate whether check should be done to ensure
//										subsystem is within certain field of view.  We don't want to check fov when
//										strafing, since ship is weaving to avoid turret fire
bool ai_big_maybe_follow_subsys_path(bool do_dot_check)
{
	ai_info	*aip;
	float		dot = 1.0f, min_dot;
	object	*target_objp;

	auto shipp = &Ships[Pl_objp->instance];
	aip = &Ai_info[shipp->ai_index];
    
	if ( aip->target_objnum < 0 )
		return 0;
    
	target_objp = &Objects[aip->target_objnum];

	if ( (aip->targeted_subsys != NULL) && (aip->targeted_subsys->system_info->path_num >= 0) ) {
		int			subsys_path_num, subsys_in_sight, checked_sight;
		float			dist;

		// Get models of both source and target
		polymodel *pm = model_get( Ship_info[shipp->ship_info_index].model_num );
		polymodel_instance *pmi = model_get_instance( shipp->model_instance_num );
		polymodel *pm_t = model_get( Ship_info[Ships[target_objp->instance].ship_info_index].model_num );

		// Necessary sanity check
		Assertion(aip->targeted_subsys->system_info->path_num < pm_t->n_paths, "Invalid Path number %d for subsystem %s on ship %s (Model: %s)\n", aip->targeted_subsys->system_info->path_num, aip->targeted_subsys->system_info->name, Ship_info[Ships[target_objp->instance].ship_info_index].name, pm_t->filename );
		if (aip->targeted_subsys->system_info->path_num >= pm_t->n_paths)
			return 0;

		// If attacking a subsystem, ensure that we have an unobstructed line of sight... if not, then move
		// towards path linked to subsystem
		subsys_in_sight = 0;	// assume Pl_objp doesn't have line of sight to subys

		// only check for subsystem sight every N milliseconds
		checked_sight = 0;
		if ( timestamp_elapsed(aip->path_subsystem_next_check) ) {
			vec3d		geye, gsubpos;
			eye			*ep;

			aip->path_subsystem_next_check = timestamp(1500);

			// get world pos of eye (stored in geye)
			ep = &(pm->view_positions[shipp->current_viewpoint]);
			model_instance_local_to_global_point( &geye, &ep->pnt, pm, pmi, 0, &Pl_objp->orient, &Pl_objp->pos );
			
			// get world pos of subsystem
			vm_vec_unrotate(&gsubpos, &aip->targeted_subsys->system_info->pnt, &En_objp->orient);
			vm_vec_add2(&gsubpos, &En_objp->pos);

			checked_sight = 1;

			// Calc dot between subsys normal (based on using path info), and subsys_to_eye vector.  This is
			// useful later when we want to decide whether we have a realistic line-of-sight to the subsystem.
			vec3d subsys_normal, subsys_to_eye;
			if ( do_dot_check ) {
				if ( !ship_return_subsys_path_normal(&Ships[target_objp->instance], aip->targeted_subsys, &gsubpos, &subsys_normal) ) {
					vm_vec_normalized_dir(&subsys_to_eye, &geye, &gsubpos);
					dot = vm_vec_dot(&subsys_normal, &subsys_to_eye);
				}
			}

			if ( ship_subsystem_in_sight(En_objp, aip->targeted_subsys, &geye, &gsubpos, true) ) {
				subsys_in_sight = 1;
			}
		}

		// check if subsystem not in sight
		min_dot = (target_objp->phys_info.fspeed > 5.0f?MIN_DOT_TO_ATTACK_MOVING_SUBSYS:MIN_DOT_TO_ATTACK_SUBSYS);
		if ( (checked_sight && ((!subsys_in_sight) || (dot < min_dot)) ) )  {

			subsys_path_num = aip->targeted_subsys->system_info->path_num;
			if ( ((aip->path_start) == -1 || (aip->mp_index != subsys_path_num)) && subsys_path_num < pm_t->n_paths ) {
				// maybe create a new path
				if ( subsys_path_num >= 0 ) {
					Assert(aip->target_objnum >= 0);
					ai_find_path(Pl_objp, aip->target_objnum, subsys_path_num, 0, 1);
					if ( aip->path_start >= 0 ) {
						aip->ai_flags.set(AI::AI_Flags::On_subsys_path);
					}
				}
			}
		}

		if ( checked_sight && subsys_in_sight && (dot > min_dot) ) {
			// we've got a clear shot, stay here for a bit
			aip->path_subsystem_next_check = timestamp_rand(5000,8000);
		}

		// If there is a path that we are tracking, see if ship has gotten to within
		// aip->path_goal_dist units.  If so, then ship can stop following the path.  This
		// is required since we don't want to follow the path all the way to the subsystem,
		// and we want ships to stop different distances from their path destination
		if ( aip->path_length > 0 ) {
			int path_done=0;
			int in_view=0;

			Assert(aip->path_length >= 2);
			dist = vm_vec_dist_quick(&Path_points[aip->path_start+aip->path_length-2].pos, &Pl_objp->pos);

			// distance setting --wookieejedi
			if ( aip->path_cur >= (aip->path_start+aip->path_length-1) ) {
				// register that path is done and reset goal distance to default
				aip->path_goal_dist = Default_subsystem_path_pt_dist;
				path_done=1;
			}
			// if not done then set path distances based on specified options
			else {
				bool found_point = false;
				// 1. use radii of path points if specified
				if (aip->ai_profile_flags[AI::Profile_Flags::Use_subsystem_path_point_radii]) {
					// need a valid path and point in model to use pof specified radius
					int path_num = aip->targeted_subsys->system_info->path_num;
					if ((0 <= path_num) && (path_num < pm_t->n_paths)) {
						// need a valid place in the path
						if ((0 <= aip->path_cur) && (aip->path_cur < aip->path_length)) {
							float path_point_radius = pm_t->paths[path_num].verts[aip->path_cur].radius;
							int goal_dist = fl2i(path_point_radius);
							// only set if >= Minimum path distance specified in aibig.h
							if (goal_dist >= Minimum_subsystem_path_pt_dist) {
								aip->path_goal_dist = goal_dist;
								found_point = true;
							}
						}
					}
				}
				// 2. use radii value in ai_profiles table if specified and no valid point found
				if (!found_point && The_mission.ai_profile->subsystem_path_radii) {
					aip->path_goal_dist = The_mission.ai_profile->subsystem_path_radii;
				}
				// 3. use default radii value if no valid point or specified radii
				else {
					aip->path_goal_dist = Default_subsystem_path_pt_dist;
				}
			}

			min_dot = (target_objp->phys_info.fspeed > 5.0f?MIN_DOT_TO_ATTACK_MOVING_SUBSYS:MIN_DOT_TO_ATTACK_SUBSYS);
			if ( (checked_sight && subsys_in_sight) && (dot > min_dot) ) {
				in_view=1;
			}

			// if we've reached the destination, then we can stop following the path
			if ( path_done || ( dist < aip->path_goal_dist ) || in_view ) {
				ai_big_subsys_path_cleanup(aip);
			} else if ( dist > aip->path_goal_dist ) {
				// If we have a path to follow, follow it and return			
				if ( aip->path_start != -1 ) {
					// for now, only follow the path to the first point
					if ( aip->path_cur < (aip->path_start+aip->path_length-1) ) {
						if ( aip->goal_objnum != aip->target_objnum ) {
							aip->previous_mode = aip->mode;
							aip->mode = AIM_NONE;
							aip->submode = -1;
							aip->submode_start_time = Missiontime;
							return 1;
						}
						ai_path();
						return 1;
					}
				}
			}
		}
	}

	return 0;
}

bool ai_new_maybe_reposition_attack_subsys() {

	ai_info* aip = &Ai_info[Ships[Pl_objp->instance].ai_index];

	if (aip->target_objnum < 0 || aip->targeted_subsys == nullptr)
		return false;

	object* target_objp = &Objects[aip->target_objnum];

	// dont bother with this if you're still far away
	if (vm_vec_dist(&Pl_objp->pos, &target_objp->pos) > target_objp->radius * 2.0f + 2000.0f)
		return false;

	vec3d		geye, gsubpos;
	eye* ep;
	polymodel* pm = model_get(Ship_info[Ships[Pl_objp->instance].ship_info_index].model_num);

	// get world pos of eye (stored in geye)
	ep = &(pm->view_positions[Ships[Pl_objp->instance].current_viewpoint]);
	model_local_to_global_point(&geye, &ep->pnt, pm, 0, &Pl_objp->orient, &Pl_objp->pos);

	// get world pos of subsystem
	get_subsystem_pos(&gsubpos, target_objp, aip->targeted_subsys);

	// you're in sight! shoot it!
	if (ship_subsystem_in_sight(En_objp, aip->targeted_subsys, &geye, &gsubpos, false))
		return false;

	// not in sight, gotta get there
	vec3d* good_pos = nullptr;
	vec3d world_goal_pos;
	// For performance reasons we only recheck once a second, otherwise we reuse the last found target.
	if (timestamp_elapsed(aip->next_dynamic_path_check_time)) {
		vec3d tgt2pl = Pl_objp->pos - target_objp->pos;
		vec3d tgt2subsys = gsubpos - target_objp->pos;
		float pl_dist = vm_vec_mag(&tgt2pl);
		float subsys_dist = vm_vec_mag(&tgt2subsys) * 1.3f; // add a bit to this, we don't want to go directly at the subsys, we want to go above it

		float angle2subsys = vm_vec_delta_ang(&tgt2pl, &tgt2subsys, nullptr);
		float angle2goal = angle2subsys;
		if (angle2goal > 1.2f) // let's not try to go more than about 70 degrees around the ship
			angle2goal = 1.2f;

		// we'll start by travelling along the sphere at our current distance around the ship
		// remember that goal_pos will be relative to our target_ship (so we can scale in and out easily)
		vec3d goal_pos, cross;
		vm_vec_cross(&cross, &tgt2pl, &tgt2subsys);
		vm_vec_normalize(&cross);
		vm_vec_negate(&cross);
		vm_rot_point_around_line(&goal_pos, &tgt2pl, angle2goal, &vmd_zero_vector, &cross);

		// then scale in (or out) towards our subsys
		float new_radius = ((subsys_dist - pl_dist) * (angle2goal / angle2subsys)) + pl_dist;
		vm_vec_copy_scale(&world_goal_pos, &goal_pos, new_radius / pl_dist);
		world_goal_pos += target_objp->pos;

		if (pp_collide(&Pl_objp->pos, &world_goal_pos, target_objp, Pl_objp->radius * 4.0f)) {
			// ok best case won't work
			if (pl_dist > subsys_dist) {
				//we're farther away than the subsys, let's try at our current distance
				world_goal_pos = goal_pos + target_objp->pos;
				if (!pp_collide(&Pl_objp->pos, &world_goal_pos, target_objp, Pl_objp->radius * 4.0f))
					good_pos = &world_goal_pos;
			}

			// ok, let's start moving it away and see if that's better
			for (float i = 1.0f; i < 4.0f; i += 1.0f) {
				if (good_pos)
					break;

				vm_vec_copy_scale(&world_goal_pos, &goal_pos, (new_radius / pl_dist) * powf(1.3f,i));
				world_goal_pos += target_objp->pos;
				if (!pp_collide(&Pl_objp->pos, &world_goal_pos, target_objp, Pl_objp->radius * 4.0f))
					good_pos = &world_goal_pos;
			}

			// welp this sucks, just run away i guess
			if (!good_pos) {
				vm_vec_copy_normalize(&world_goal_pos, &tgt2pl);
				world_goal_pos *= target_objp->radius + pl_dist;
				world_goal_pos += target_objp->pos;
				good_pos = &world_goal_pos;
			}
		}
		else
			good_pos = &world_goal_pos;
		aip->next_dynamic_path_check_time = timestamp( AI_DYNAMIC_PATH_RECHECK_DELAY );
		aip->last_dynamic_path_goal = *good_pos;

	}
	else{ //Not enough time has passed till recheck.
		good_pos = &aip->last_dynamic_path_goal;
	}

	ai_turn_towards_vector(good_pos, Pl_objp, nullptr, nullptr, 0.0f, 0);
	float subsys_distance = vm_vec_dist(&Pl_objp->pos, &gsubpos);

	// full speed until you're close, then slow down a bit
	accelerate_ship(aip, subsys_distance > 200.0f ? 1.0f : subsys_distance / 200.0f);

	if (!(Ships[Pl_objp->instance].flags[Ship::Ship_Flags::Afterburner_locked])) {
		if (ai_willing_to_afterburn_hard(aip) && subsys_distance > 1000.0f)
			ai_afterburn_hard(Pl_objp, aip);
		else if (subsys_distance > 300.0f) {
			if (ai_maybe_fire_afterburner(Pl_objp, aip)) {
				afterburners_start(Pl_objp);
				aip->afterburner_stop_time = Missiontime + 2 * F1_0;
			}
		}
	} else
		afterburners_stop(Pl_objp);

	return true;

}

// This function is only called from ai_big_chase_attack() when a ship is flying very slowly and
// attacking a big ship.  The ship should scan for enemy fighter/bombers... if any are close, then
// return 1, otherwise return 0;
//
// input: aip	=>	ai_info pointer for Pl_objp
//			 sip	=>	ship_info pointer for Pl_objp
//
// exit:		1	=>	ship should enter strafe mode
//				0	=> ship should not change ai mode, no fighter/bomber threats are near
//
// NOTE: uses SCAN_FIGHTERS_INTERVAL and ENTER_STRAFE_THREAT_DIST_SQUARED which are defined in AiBig.h
int ai_big_maybe_start_strafe(ai_info *aip, ship_info *sip)
{
	// if moving slowly (or stopped), and SIF_SMALL_SHIP, then enter STRAFE mode if enemy fighter/bombers
	// are near
	if ( sip->is_small_ship() ) {
		if ( timestamp_elapsed(aip->scan_for_enemy_timestamp) ) {
			object	*test_objp;
			ship		*test_sp;
			float		dist_squared;
			
			aip->scan_for_enemy_timestamp = timestamp(SCAN_FIGHTERS_INTERVAL);
			// iterate through ships, and see if any fighter/bomber from opposite team are near
			for (auto so: list_range(&Ship_obj_list)) {
				test_objp = &Objects[so->objnum];
				if (test_objp->flags[Object::Object_Flags::Should_be_dead])
					continue;

				test_sp = &Ships[test_objp->instance];

				if ( iff_x_attacks_y(Ships[Pl_objp->instance].team, test_sp->team) ) {
					if ( Ship_info[test_sp->ship_info_index].is_small_ship() ) {
						dist_squared = vm_vec_dist_squared(&Pl_objp->pos, &test_objp->pos);
						if ( dist_squared < ENTER_STRAFE_THREAT_DIST_SQUARED ) {
							return 1;
						}
					}
				}
			}
		}
	}
	// If we've reached here, there are no enemy fighter/bombers near
	return 0;
}

//	ATTACK submode handler for chase mode.
void ai_big_chase_attack(ai_info *aip, ship_info *sip, vec3d *enemy_pos, float dist_to_enemy, int modelnum)
{
	int		start_bank;
	float		dot_to_enemy, time_to_hit;
	polymodel *po = model_get( modelnum );

	//	Maybe evade an incoming weapon.
	if (((time_to_hit = ai_endangered_by_weapon(aip)) < 4.0f) && (time_to_hit >= 0.0f)) {
		aip->submode = SM_EVADE_WEAPON;
		aip->submode_start_time = Missiontime;
		aip->prev_goal_point = En_objp->pos;
	} else {
		//	If moving slowly, maybe evade incoming fire.
		if (Pl_objp->phys_info.speed < 3.0f) {
			object *objp;
			for ( objp = GET_FIRST(&obj_used_list); objp !=END_OF_LIST(&obj_used_list); objp = GET_NEXT(objp) ) {
				if (objp->flags[Object::Object_Flags::Should_be_dead])
					continue;

				if ((objp->type == OBJ_WEAPON) && (iff_x_attacks_y(Ships[Pl_objp->instance].team, Weapons[objp->instance].team)))
					if (Weapon_info[Weapons[objp->instance].weapon_info_index].subtype == WP_LASER) {
						vec3d	in_vec;
						float		dist;

						vm_vec_sub(&in_vec, &objp->pos, &Pl_objp->pos);
						if (vm_vec_dot(&in_vec, &objp->orient.vec.fvec) > 0.0f) {
							dist = vm_vec_normalize(&in_vec);
							if ((dist < 200.0f) && (vm_vec_dot(&in_vec, &objp->orient.vec.fvec) > 0.95f)) {
								if ((Objects[objp->parent].signature == objp->parent_sig) && (vm_vec_dist_quick(&objp->pos, &Objects[objp->parent].pos) < 300.0f)) {
									set_target_objnum(aip, objp->parent);
									aip->submode = SM_ATTACK;
									aip->submode_start_time = Missiontime;
								} else {
									aip->submode = SM_EVADE;
									aip->submode_start_time = Missiontime;
									aip->prev_goal_point = En_objp->pos;
								}
							}
						}
					}
			}

		} // end if ( Pl_objp->phys_info.speed < 3.0f ) 

		// if moving slowly and attacking a large ship, 
		// check if need to enter standard strafe mode
		// including ai_profile flag and if enemy fighters are near
		if (Pl_objp->phys_info.speed < The_mission.ai_profile->standard_strafe_when_below_speed && 
			(The_mission.ai_profile->flags[AI::Profile_Flags::Standard_strafe_used_more] || ai_big_maybe_start_strafe(aip, sip))) {
			aip->previous_mode = aip->mode;
			aip->mode = AIM_STRAFE;
			aip->submode_parm0 = Missiontime;	// use parm0 as time strafe mode entered (i.e. MODE start time)
			ai_big_strafe_position();
			return;
		}

		//Maybe enter glide strafe (check every 8 seconds, on a different schedule for each ship)
		if ((sip->can_glide == true) && !(aip->ai_flags[AI::AI_Flags::Kamikaze]) && static_randf((Missiontime + static_rand(aip->shipnum)) >> 19) < aip->ai_glide_strafe_percent) {
			aip->previous_mode = aip->mode;
			aip->mode = AIM_STRAFE;
			aip->submode_parm0 = Missiontime;	// use parm0 as time strafe mode entered (i.e. MODE start time)
			aip->submode = AIS_STRAFE_GLIDE_ATTACK;
			aip->submode_parm1 = 0;
			aip->submode_start_time = Missiontime;
		}

		// see if Pl_objp needs to reposition to get a good shot at subsystem which is being attacked
		if (aip->ai_profile_flags[AI::Profile_Flags::Improved_subsystem_attack_pathing]) {
			if (ai_new_maybe_reposition_attack_subsys())
				return;
		} else if ( ai_big_maybe_follow_subsys_path() ) {
			if ((Pl_objp->phys_info.flags & PF_AFTERBURNER_ON) && !(aip->ai_flags[AI::AI_Flags::Kamikaze]) && !ai_willing_to_afterburn_hard(aip)) {
				afterburners_stop(Pl_objp);
			}
			return;
		}

		vec3d	*rel_pos;
		vec3d	vec_to_enemy;
		float	weapon_travel_dist;

		start_bank = Ships[aip->shipnum].weapons.current_primary_bank;

		if ((po->n_guns) && (start_bank != -1)) {
			rel_pos = &po->gun_banks[start_bank].pnt[0];
		} else
			rel_pos = NULL;

		dist_to_enemy = vm_vec_normalized_dir(&vec_to_enemy, enemy_pos, &Pl_objp->pos);
		dot_to_enemy = vm_vec_dot(&vec_to_enemy, &Pl_objp->orient.vec.fvec);

		vec3d	rvec_vec, *rvec = &rvec_vec;

		if (dist_to_enemy > 500.0f)
			compute_desired_rvec(rvec, enemy_pos, &Pl_objp->pos);
		else
			rvec = NULL;
		
		ai_turn_towards_vector(enemy_pos, Pl_objp, nullptr, rel_pos, 0.0f, 0, rvec);

		// set an optimal range if applicable
		float optimal_range = 0.0f;
		ship_weapon* weapons = &Ships[Pl_objp->instance].weapons;
		weapon_info* wip = nullptr;
		ship* shipp = &Ships[Pl_objp->instance];

		if (weapons->num_primary_banks >= 1 && weapons->current_primary_bank >= 0 && !shipp->flags[Ship::Ship_Flags::Primaries_locked]) {
			wip = &Weapon_info[weapons->primary_bank_weapons[weapons->current_primary_bank]];
		} else if (weapons->num_secondary_banks >= 1 && weapons->current_secondary_bank >= 0 && !shipp->flags[Ship::Ship_Flags::Secondaries_locked]) {
			wip = &Weapon_info[weapons->secondary_bank_weapons[weapons->current_secondary_bank]];
		}

		if (wip != nullptr && wip->optimum_range > 0)
			optimal_range = wip->optimum_range;

		// calc range of primary weapon
		weapon_travel_dist = ai_get_weapon_dist(&Ships[Pl_objp->instance].weapons);

		if ( aip->targeted_subsys != NULL ) {
			if (dist_to_enemy > (optimal_range > 0.0f ? optimal_range : (weapon_travel_dist-20))) {
				accelerate_ship(aip, 1.0f);
				
				if ((aip->ai_flags[AI::AI_Flags::Free_afterburner_use] || aip->ai_profile_flags[AI::Profile_Flags::Free_afterburner_use]) && !(shipp->flags[Ship::Ship_Flags::Afterburner_locked]) && (dot_to_enemy > 0.75f)) {
					if (ai_maybe_fire_afterburner(Pl_objp, aip)) {
						afterburners_start(Pl_objp);
						aip->afterburner_stop_time = Missiontime + 3*F1_0;
					}
				} else if ((Pl_objp->phys_info.flags & PF_AFTERBURNER_ON) && !(aip->ai_flags[AI::AI_Flags::Kamikaze])) {
					afterburners_stop(Pl_objp);
				}
			} else {
				// AL 12-31-97: Move at least as quickly as your target is moving...
				accelerate_ship(aip, MAX(1.0f - dot_to_enemy, Objects[aip->target_objnum].phys_info.fspeed/sip->max_speed));

				if ((Pl_objp->phys_info.flags & PF_AFTERBURNER_ON) && !(aip->ai_flags[AI::AI_Flags::Kamikaze])) {
					afterburners_stop(Pl_objp);
				}
			}

		} else {
			float accel;
			if (dot_to_enemy < 0.0f) {
				accel = 0.5f;
			} else if (dot_to_enemy < 0.75f) {
				accel = 0.75f;
			} else {
				if (dist_to_enemy > (optimal_range > 0.0f ? optimal_range : (weapon_travel_dist/2.0f))) {
					accel = 1.0f;
				} else {
					accel = 1.0f - dot_to_enemy;
				}
			}

			// use dist normal to enemy here (don't break 50 barrier)
			if (dist_to_enemy < ATTACK_STOP_DISTANCE) {
//				accelerate_ship(aip, accel * 0.5f);
				accelerate_ship(aip, -1.0f);
				if ((Pl_objp->phys_info.flags & PF_AFTERBURNER_ON) && !(aip->ai_flags[AI::AI_Flags::Kamikaze])) {
					afterburners_stop(Pl_objp);
				}
			} else {
				accelerate_ship(aip, accel);

				if (ai_willing_to_afterburn_hard(aip) && accel > 0.99f && !(shipp->flags[Ship::Ship_Flags::Afterburner_locked])) {
					ai_afterburn_hard(Pl_objp, aip);
				} else if ((aip->ai_flags[AI::AI_Flags::Free_afterburner_use] || aip->ai_profile_flags[AI::Profile_Flags::Free_afterburner_use]) && !(shipp->flags[Ship::Ship_Flags::Afterburner_locked]) && (accel > 0.95f)) {
					if (ai_maybe_fire_afterburner(Pl_objp, aip)) {
						afterburners_start(Pl_objp);
						aip->afterburner_stop_time = Missiontime + 3*F1_0;
					}
				} else if ((Pl_objp->phys_info.flags & PF_AFTERBURNER_ON) && !(aip->ai_flags[AI::AI_Flags::Kamikaze])) {
					afterburners_stop(Pl_objp);
				}
			}
		}
	}
}

// Handler for submode SM_CONTINUOUS_TURN
void ai_big_chase_ct()
{
	ai_chase_ct();
}

extern bool ai_select_secondary_weapon(object *objp, ship_weapon *swp, int priority1 = -1, int priority2 = -1);
extern float set_secondary_fire_delay(ai_info *aip, ship *shipp, weapon_info *swip, bool burst);
extern bool ai_choose_secondary_weapon(object *objp, ai_info *aip, object *en_objp);
extern bool maybe_avoid_big_ship(object *objp, object *ignore_objp, ai_info *aip, vec3d *goal_point, float delta_time, float time_scale = 1.f);

extern void maybe_cheat_fire_synaptic(object *objp);

// Determine if Pl_objp should fire weapons at current target, based on input parameters
//
// dist_to_enemy	=>		distance (in m) to attack point on current target
// dot_to_enemy	=>		dot product between fvec of Pl_objp and vector from Pl_objp to attack point
//
static void ai_big_maybe_fire_weapons(float dist_to_enemy, float dot_to_enemy)
{
	ai_info		*aip;
	ship_weapon	*swp;
	int has_fired = -1;

	aip = &Ai_info[Ships[Pl_objp->instance].ai_index];
	swp = &Ships[Pl_objp->instance].weapons;

	float target_radius = En_objp->radius;

	// normal subsystem attacking uses whether or not its on a path and skips this function entirely to stop "over-eager" firing
	// since the improved method doesnt use paths, refine the target radius to stop over-eager shooting
	if (aip->ai_profile_flags[AI::Profile_Flags::Improved_subsystem_attack_pathing] && aip->targeted_subsys != nullptr)
		target_radius = aip->targeted_subsys->system_info->radius;

	if (dot_to_enemy > 0.95f - 0.5f * target_radius/MAX(1.0f, target_radius + dist_to_enemy)) {
		aip->time_enemy_in_range += flFrametime;
		
		//	Chance of hitting ship is based on dot product of firing ship's forward vector with vector to ship
		//	and also the size of the target relative to distance to target.
		if (dot_to_enemy > std::max(0.5f, 0.90f + aip->ai_accuracy / 10.0f - En_objp->radius / std::max(1.0f, dist_to_enemy))) {

			ship *temp_shipp;
			temp_shipp = &Ships[Pl_objp->instance];
			ship_weapon *tswp = &temp_shipp->weapons;

			if ( tswp->num_primary_banks > 0 ) {
				Assertion(tswp->current_primary_bank < tswp->num_primary_banks, "AI tried to select primary bank %d. Might be a model error\n", tswp->current_primary_bank);
				weapon_info	*wip = &Weapon_info[tswp->primary_bank_weapons[tswp->current_primary_bank]];

				if (dist_to_enemy < MIN((wip->max_speed * wip->lifetime), wip->weapon_range)){
					has_fired = 1;
					if(! ai_fire_primary_weapon(Pl_objp)){
						has_fired = -1;
					//	ship_stop_fire_primary(Pl_objp);
					}
				}
			}

			if (tswp->num_secondary_banks > 0) {
				if ( !(En_objp->flags[Object::Object_Flags::Protected]) || ai_goal_is_disable_or_disarm(aip->goals[0].ai_mode) ) {
					bool valid_secondary = ai_choose_secondary_weapon(Pl_objp, aip, En_objp);
					int current_bank = tswp->current_secondary_bank;
					if (current_bank > -1 && valid_secondary) {
						weapon_info	*swip = &Weapon_info[tswp->secondary_bank_weapons[current_bank]];

						if ( !(En_objp->flags[Object::Object_Flags::Protected]) || (ai_goal_is_disable_or_disarm(aip->goals[0].ai_mode) && swip->wi_flags[Weapon::Info_Flags::Puncture]) ) { //override lockdown on protected ships when using anti subsystem weapons - Valathil
							//	If ship is protected and very low on hits, don't fire missiles.
							if (!(En_objp->flags[Object::Object_Flags::Protected]) || (En_objp->hull_strength > 10*swip->damage)) {
								if (aip->ai_flags[AI::AI_Flags::Unload_secondaries]) {
									if (timestamp_until(swp->next_secondary_fire_stamp[current_bank]) > swip->fire_wait*1000.0f) {
										swp->next_secondary_fire_stamp[current_bank] = timestamp((int) (swip->fire_wait*1000.0f));
									}
								}

								if (timestamp_elapsed(swp->next_secondary_fire_stamp[current_bank]) && weapon_target_satisfies_lock_restrictions(swip, En_objp)) {
									float firing_range;
									if (swip->wi_flags[Weapon::Info_Flags::Local_ssm])
										firing_range=swip->lssm_lock_range;
									else
										firing_range = MIN((swip->max_speed * swip->lifetime), swip->weapon_range);
									// reduce firing range of secondaries in nebula
									extern int Nebula_sec_range;
									if ((The_mission.flags[Mission::Mission_Flags::Fullneb]) && Nebula_sec_range) {
										firing_range *= 0.8f;
									}

									float t = 0.25f;	//	default delay in seconds until next fire.

									if (dist_to_enemy < firing_range*1.0f) {


										//vm_vec_scale_add(&future_enemy_pos, enemy_pos, enemy_vel, dist_to_enemy/swip->max_speed);
										//if (vm_vec_dist_quick(&future_enemy_pos, firing_pos) < firing_range * 0.8f) {
											if (ai_fire_secondary_weapon(Pl_objp)) {

												int current_bank_adjusted = MAX_SHIP_PRIMARY_BANKS + current_bank;

												if ((aip->ai_flags[AI::AI_Flags::Unload_secondaries]) || (swip->burst_flags[Weapon::Burst_Flags::Fast_firing])) {
													if (swip->burst_shots > swp->burst_counter[current_bank_adjusted]) {
														t = swip->burst_delay;
														swp->burst_counter[current_bank_adjusted]++;
													} else {
														t = swip->fire_wait;
														if ((swip->burst_shots > 0) && (swip->burst_flags[Weapon::Burst_Flags::Random_length])) {
															swp->burst_counter[current_bank_adjusted] = Random::next(swip->burst_shots);
														} else {
															swp->burst_counter[current_bank_adjusted] = 0;
														}
														swp->burst_seed[current_bank_adjusted] = Random::next();
													}
												} else {
													if (swip->burst_shots > swp->burst_counter[current_bank_adjusted]) {
														t = set_secondary_fire_delay(aip, temp_shipp, swip, true);
														swp->burst_counter[current_bank_adjusted]++;
													} else {
														t = set_secondary_fire_delay(aip, temp_shipp, swip, false);
														if ((swip->burst_shots > 0) && (swip->burst_flags[Weapon::Burst_Flags::Random_length])) {
															swp->burst_counter[current_bank_adjusted] = Random::next(swip->burst_shots);
														} else {
															swp->burst_counter[current_bank_adjusted] = 0;
														}
														swp->burst_seed[current_bank_adjusted] = Random::next();
													}
												}
												swp->next_secondary_fire_stamp[current_bank] = timestamp((int) (t*1000.0f));
											}
										//}
									}
									swp->next_secondary_fire_stamp[current_bank] = timestamp((int) (t*1000.0f));
								}
							}
						}
					}
				}
			}
		}
	} else {
		if (flFrametime < 1.0f)
			aip->time_enemy_in_range *= (1.0f - flFrametime);
		else
			aip->time_enemy_in_range = 0;
	}

	if(has_fired == -1){	//stuff that hapens when the ship stops fireing
		ship_stop_fire_primary(Pl_objp);
	}

}

// switch ai ship into chase mode
void ai_big_switch_to_chase_mode(ai_info *aip)
{
	aip->previous_mode = aip->mode;
	aip->mode = AIM_CHASE;
	aip->submode = SM_ATTACK;
	aip->submode_start_time = Missiontime;
}

extern weapon_info* ai_get_weapon(ship_weapon* swp);

// Make object Pl_objp chase object En_objp, which is a big ship, not a small ship.
void ai_big_chase()
{
	float			dist_to_enemy, dot_to_enemy;
	vec3d		player_pos, enemy_pos, vec_to_enemy;
	ship_info	*sip = &Ship_info[Ships[Pl_objp->instance].ship_info_index];
	ship			*shipp = &Ships[Pl_objp->instance];	
	ai_info		*aip = &Ai_info[shipp->ai_index];
	vec3d		predicted_enemy_pos;

	Assert(aip->mode == AIM_CHASE);

	maybe_cheat_fire_synaptic(Pl_objp);

	ai_set_positions(Pl_objp, En_objp, aip, &player_pos, &enemy_pos);

	player_pos = Pl_objp->pos;
	ai_big_pick_attack_point(En_objp, Pl_objp, &enemy_pos, 0.8f);

	weapon_info* wip = ai_get_weapon(&shipp->weapons);
	bool ballistic_aiming = !IS_VEC_NULL(&The_mission.gravity) && wip && wip->gravity_const != 0.0f;
	bool valid_ballistic_trajectory = true;

	if (!ballistic_aiming) {
		//	Compute the predicted position of the center of the ship, then add the delta to the goal pos.
		if (En_objp->phys_info.speed > 3.0f) {
			set_predicted_enemy_pos(&predicted_enemy_pos, Pl_objp, &En_objp->pos, &En_objp->phys_info.vel, aip);
			vm_vec_add2(&enemy_pos, &predicted_enemy_pos);
			vm_vec_sub2(&enemy_pos, &En_objp->pos);
		} else
			predicted_enemy_pos = En_objp->pos;

		if (aip->targeted_subsys != NULL) {
			get_subsystem_pos(&enemy_pos, En_objp, aip->targeted_subsys);
		}
	} else {
		if (aip->targeted_subsys != NULL) {
			get_subsystem_pos(&enemy_pos, En_objp, aip->targeted_subsys);
		}

		vec3d gravity = The_mission.gravity * wip->gravity_const;
		vec3d target;
		valid_ballistic_trajectory = 
			physics_lead_ballistic_trajectory(&player_pos, &enemy_pos, &En_objp->phys_info.vel, wip->max_speed, &gravity, &target);

		if (valid_ballistic_trajectory) {
			enemy_pos = Pl_objp->pos + target * vm_vec_dist(&Pl_objp->pos, &enemy_pos);
			predicted_enemy_pos = enemy_pos;
		} else {
			set_predicted_enemy_pos(&predicted_enemy_pos, Pl_objp, &En_objp->pos, &En_objp->phys_info.vel, aip);
		}
	}

	dist_to_enemy = vm_vec_normalized_dir(&vec_to_enemy, &enemy_pos, &player_pos); // - En_objp->radius;
	dot_to_enemy = vm_vec_dot(&vec_to_enemy, &Pl_objp->orient.vec.fvec);

	if (aip->ai_flags[AI::AI_Flags::Target_collision]) {
		if ( ai_big_strafe_maybe_retreat(&enemy_pos) ) {
			aip->mode = AIM_STRAFE;
			aip->submode_parm0 = Missiontime;	// use parm0 as time strafe mode entered (i.e. MODE start time)
			aip->submode = AIS_STRAFE_AVOID;
			aip->submode_start_time = Missiontime;
			if (Pl_objp->phys_info.flags & PF_AFTERBURNER_ON) {
				afterburners_stop(Pl_objp);
			}
			return;
		}
	}

	if (aip->ai_flags[AI::AI_Flags::Kamikaze]) {
		//nprintf(("AI", "Kamikaze: %7.3f %7.3f\n", dot_to_enemy, dist_to_enemy));
		accelerate_ship(aip, 1.0f);
		if ((dist_to_enemy < 400.0f) && ai_maybe_fire_afterburner(Pl_objp, aip)) {
			afterburners_start(Pl_objp);
			aip->afterburner_stop_time = Missiontime + 3*F1_0;
		}
	}

	//	If just acquired target, or target is not in reasonable cone, don't refine believed enemy position.
	if ((dot_to_enemy < 0.25f) || (aip->target_time < 1.0f) || (aip->ai_flags[AI::AI_Flags::Seek_lock])) {
		update_aspect_lock_information(aip, &vec_to_enemy, dist_to_enemy - En_objp->radius, En_objp->radius);
	} else if (aip->targeted_subsys != NULL && !ballistic_aiming) {
		Assert(aip->targeted_subsys != NULL);
		get_subsystem_pos(&enemy_pos, En_objp, aip->targeted_subsys);
		vm_vec_add2(&enemy_pos, &predicted_enemy_pos);
		vm_vec_sub2(&enemy_pos, &En_objp->pos);
		dist_to_enemy = vm_vec_normalized_dir(&vec_to_enemy, &enemy_pos, &player_pos); // - En_objp->radius;
		dot_to_enemy = vm_vec_dot(&vec_to_enemy, &Pl_objp->orient.vec.fvec);
		update_aspect_lock_information(aip, &vec_to_enemy, dist_to_enemy, En_objp->radius);
	} else if (En_objp->flags[Object::Object_Flags::Protected]) {	//	If protected and we're not attacking a subsystem, stop attacking!
		update_aspect_lock_information(aip, &vec_to_enemy, dist_to_enemy - En_objp->radius, En_objp->radius);
		aip->target_objnum = -1;
		if (find_enemy(OBJ_INDEX(Pl_objp), MAX_ENEMY_DISTANCE, The_mission.ai_profile->max_attackers[Game_skill_level]) == -1) {
			ai_do_default_behavior(Pl_objp);
			return;
		}
	} else {
		update_aspect_lock_information(aip, &vec_to_enemy, dist_to_enemy - En_objp->radius, En_objp->radius);
	}

	//	If recently acquired target and not looking at target, just turn a bit.
	switch (aip->submode) {
	case SM_ATTACK:
	case SM_SUPER_ATTACK:
		if (vm_vec_dist_quick(&Pl_objp->pos, &predicted_enemy_pos) > 100.0f + En_objp->radius * 2.0f) {
			if (maybe_avoid_big_ship(Pl_objp, En_objp, aip, &predicted_enemy_pos, 10.0f)) {
				if ((Pl_objp->phys_info.flags & PF_AFTERBURNER_ON) && !(aip->ai_flags[AI::AI_Flags::Kamikaze])) {
					afterburners_stop(Pl_objp);
				}
				return;
			}
		}

		if (!aip->ai_profile_flags[AI::Profile_Flags::No_continuous_turn_on_attack] && aip->target_time < 2.0f)
			if ((dot_to_enemy < 0.9f) || (dist_to_enemy > 300.0f)) {
				aip->submode = SM_CONTINUOUS_TURN;
				aip->submode_start_time = Missiontime - fl2f(2.75f);	//	This backdated start time allows immediate switchout.
				if (dot_to_enemy > 0.0f)
					aip->target_time += flFrametime * dot_to_enemy;
			}
		break;
	}

	//
	//	Set turn and acceleration based on submode.
	//

	if (((aip->submode != SM_ATTACK) && (aip->submode != SM_SUPER_ATTACK) && (aip->submode != SM_ATTACK_FOREVER) && (aip->submode != SM_EVADE_WEAPON)) || (aip->mode == AIM_STRAFE)){
		if ((Pl_objp->phys_info.flags & PF_AFTERBURNER_ON) && !(aip->ai_flags[AI::AI_Flags::Kamikaze])) {
			afterburners_stop(Pl_objp);
		}
	}

	switch (aip->submode) {
	case SM_CONTINUOUS_TURN:
		ai_big_chase_ct();
		break;

	case SM_ATTACK:
	case SM_SUPER_ATTACK:
	case SM_ATTACK_FOREVER:
		ai_big_chase_attack(aip, sip, &enemy_pos, dist_to_enemy, sip->model_num);
		break;

	case SM_EVADE:
		ai_big_evade_ship();
		break;

	case SM_AVOID:
		ai_big_avoid_ship();
		break;

	case SM_EVADE_WEAPON:
		evade_weapon();
		break;

	default:
		aip->last_attack_time = Missiontime;
		aip->submode = SM_ATTACK;
		aip->submode_start_time = Missiontime;
		break;
	}

	// maybe Pl_objp was forced into strafe mode, if so return now
	if ( aip->mode == AIM_STRAFE )
		return;

	//	Maybe choose a new submode.
	if (aip->submode != SM_AVOID && aip->submode != SM_EVADE ) {
		//	If a very long time since attacked, attack no matter what!
		if (aip->submode != SM_SUPER_ATTACK) {
			if (Missiontime - aip->last_attack_time > i2f(6)) {
				aip->submode = SM_SUPER_ATTACK;
				aip->submode_start_time = Missiontime;
				aip->last_attack_time = Missiontime;
			}
		}

		//	If a collision is expected, pull out!
		float dist_normal_to_enemy;

		if (vm_vec_mag_squared(&aip->big_attack_surface_normal) > 0.9) {
			if (aip->ai_profile_flags[AI::Profile_Flags::Improved_subsystem_attack_pathing])
				dist_normal_to_enemy = fl_abs((dist_to_enemy / vm_vec_dot(&vec_to_enemy, &aip->big_attack_surface_normal)));
			else // I'm fairly sure this retail code VVV meant to use / instead of * 
				dist_normal_to_enemy = fl_abs((dist_to_enemy * vm_vec_dot(&vec_to_enemy, &aip->big_attack_surface_normal)));
		} else {
			// don;t have normal so use a conservative value here
			dist_normal_to_enemy = 0.3f * dist_to_enemy;
		}

//		float time_to_enemy = dist_normal_to_enemy / Pl_objp->phys_info.speed * fl_abs(vm_vec_dot(&Pl_objp->phys_info.vel, &aip->big_attack_surface_normal));
//		if (Framecount % 30 == 1) {
//			mprintf(("normal dist; %.1f, time: %.1f\n", dist_normal_to_enemy, time_to_enemy));
//		}
		
		// since we're not in strafe and we may get a bad normal, cap dist_normal_to_enemy as MIN(0.3*dist_to_enemy, self)
		// this will allow us to get closer on a bad normal
		dist_normal_to_enemy = MAX(0.3f*dist_to_enemy, dist_normal_to_enemy);

		if (dist_to_enemy < ATTACK_COLLIDE_BASE_DIST) {
			// within 50m or 1sec
			float time_to_enemy;
			if (vm_vec_mag_squared(&aip->big_attack_surface_normal) > 0.9) {
				if (Pl_objp->phys_info.speed > 0.1) {
					time_to_enemy = dist_normal_to_enemy / fl_abs(vm_vec_dot(&Pl_objp->phys_info.vel, &aip->big_attack_surface_normal));
				} else {
					// a big time
					time_to_enemy = 100.0f;
				}
			} else {
				if (Pl_objp->phys_info.speed > 0.1) {
					time_to_enemy = dist_normal_to_enemy / Pl_objp->phys_info.speed;
				} else {
					// a big time
					time_to_enemy = 100.0f;
				}
			}

			float speed_dist = MAX(0.0f, (Pl_objp->phys_info.speed-50) * 2);
			if ((dist_normal_to_enemy < ATTACK_COLLIDE_AVOID_DIST + speed_dist) || (time_to_enemy < ATTACK_COLLIDE_AVOID_TIME) ) {
				// get away, simulate crsh recovery (don't use avoid)
//				accelerate_ship(aip, -1.0f);
				big_ship_collide_recover_start(Pl_objp, En_objp, nullptr);
//				aip->submode = SM_AVOID;
//				aip->submode_start_time = Missiontime;
			} else if ((dist_normal_to_enemy < ATTACK_COLLIDE_SLOW_DIST) || (time_to_enemy < ATTACK_COLLIDE_SLOW_TIME) ) {
				// slow down
				accelerate_ship(aip, -1.0f);
			}
		}

		/*
		if ((dot_to_enemy > 1.0f - 0.1f * En_objp->radius/(dist_to_enemy + 1.0f)) && (Pl_objp->phys_info.speed > dist_to_enemy/5.0f)) {
			if (might_collide_with_ship(Pl_objp, En_objp, dot_to_enemy, dist_to_enemy, (aip->targeted_subsys == NULL)*2.0f + 1.5f)) {
				if ((Missiontime - aip->last_hit_time > F1_0*4) && (dist_to_enemy < Pl_objp->radius*2 + En_objp->radius*2)) {
					accelerate_ship(aip, -1.0f);
				} else {
					aip->submode = SM_AVOID;
					aip->submode_start_time = Missiontime;
				}
			}
		} */
	}

	switch (aip->submode) {
	case SM_CONTINUOUS_TURN:
		if (Missiontime - aip->submode_start_time > i2f(3)) {
			aip->last_attack_time = Missiontime;
			aip->submode = SM_ATTACK;
			aip->submode_start_time = Missiontime;
		}
		break;

	case SM_ATTACK:
	case SM_ATTACK_FOREVER:
		break;
		
	case SM_EVADE:
		if (dist_to_enemy > 4*En_objp->radius) {
			aip->submode = SM_ATTACK;
			aip->submode_start_time = Missiontime;
			aip->last_attack_time = Missiontime;
		}
		break;

	case SM_SUPER_ATTACK:
		break;

	case SM_AVOID:
		// if outside box by > 300 m
		// DA 4/20/98  can never get here attacking a big ship
		{
		int is_inside;
		float box_dist = get_world_closest_box_point_with_delta(NULL, En_objp, &Pl_objp->pos, &is_inside, 0.0f);
		
		if (box_dist > 300) {
			aip->submode = SM_ATTACK;
			aip->submode_start_time = Missiontime;
			aip->last_attack_time = Missiontime;
		}
		}
		break;

/*		if (dot_to_enemy > -0.2f) {
			aip->submode_start_time = Missiontime;
		} else if (Missiontime - aip->submode_start_time > i2f(1)/2) {
			if (might_collide_with_ship(Pl_objp, En_objp, dot_to_enemy, dist_to_enemy, 3.0f)) {
				aip->submode_start_time = Missiontime;
			} else {
				aip->submode = SM_ATTACK;
				aip->submode_start_time = Missiontime;
				aip->last_attack_time = Missiontime;
			}
		}

		break;*/

	case SM_EVADE_WEAPON:
		if (aip->danger_weapon_objnum == -1) {
			aip->submode = SM_ATTACK;
			aip->submode_start_time = Missiontime;
			aip->last_attack_time = Missiontime;
		}
		break;

	default:
		aip->submode = SM_ATTACK;
		aip->submode_start_time = Missiontime;
		aip->last_attack_time = Missiontime;
	}

	//
	//	Maybe fire primary weapon and update time_enemy_in_range
	//
	//nprintf(("AI", "time_enemy_in_range = %7.3f, dot = %7.3f\n", aip->time_enemy_in_range, dot_to_enemy));

	// AL: add condition that Pl_objp must not be following a path to fire.  This may be too extreme, but
	//     I noticed AI ships firing inappropriately when following a path near a big ship.
	//		 TODO: investigate why ships fire (and aren't close to hitting ship) when following a path near
	//				 a big ship
	if (aip->mode != AIM_EVADE && aip->path_start == -1 && (!ballistic_aiming || valid_ballistic_trajectory)) {
		ai_big_maybe_fire_weapons(dist_to_enemy, dot_to_enemy);
	} else {
		if (flFrametime < 1.0f)
			aip->time_enemy_in_range *= (1.0f - flFrametime);
		else
			aip->time_enemy_in_range = 0;
	}
}

// all three parms are output parameters
//	Enemy object is En_objp
// pos	=>		world pos of attack point
// dist	=>		distance from Pl_objp front to attack point
// dot	=>		dot of Pl_objp fvec and vector to attack point
//	fire_pos =>	world pos from which firing
void ai_big_attack_get_data(vec3d *enemy_pos, float *dist_to_enemy, float *dot_to_enemy)
{
	vec3d		player_pos, vec_to_enemy, predicted_enemy_pos;	
	ship			*shipp = &Ships[Pl_objp->instance];	
	ai_info		*aip = &Ai_info[shipp->ai_index];
	ship_info	*esip = &Ship_info[Ships[En_objp->instance].ship_info_index];

	Assert(aip->mode == AIM_STRAFE);

	// ensure that Pl_objp is still targeting a big ship
	if ( !(esip->is_big_or_huge()) ) {
		ai_big_switch_to_chase_mode(aip);
		return;
	}

	ai_set_positions(Pl_objp, En_objp, aip, &player_pos, enemy_pos);

	player_pos = Pl_objp->pos;

	if (aip->targeted_subsys != NULL) {
		Assert(aip->targeted_subsys != NULL);
		get_subsystem_pos(enemy_pos, En_objp, aip->targeted_subsys);
	} else {
		// checks valid line to target
		ai_big_pick_attack_point(En_objp, Pl_objp, enemy_pos, 0.8f);
	}

	// Take player pos to be center of ship + ship_radius
	vm_vec_scale_add2(&player_pos, &Pl_objp->orient.vec.fvec, Pl_objp->radius); 

	//	If seeking lock, try to point directly at ship, else predict position so lasers can hit it.
	//	If just acquired target, or target is not in reasonable cone, don't refine believed enemy position.
	vm_vec_normalized_dir(&vec_to_enemy, enemy_pos, &player_pos);
	*dot_to_enemy=vm_vec_dot(&vec_to_enemy, &Pl_objp->orient.vec.fvec);
	if ((*dot_to_enemy < 0.25f) || (aip->target_time < 1.0f) || (aip->ai_flags[AI::AI_Flags::Seek_lock])) {
		predicted_enemy_pos=*enemy_pos;
	} else {
		vec3d	gun_pos, pnt;
		polymodel *po = model_get( Ship_info[shipp->ship_info_index].model_num );
		float		weapon_speed;

		//	Compute position of gun in absolute space and use that as fire position.
		if (po->n_guns > 0 && !(The_mission.ai_profile->flags[AI::Profile_Flags::Ai_aims_from_ship_center])) {
			pnt = po->gun_banks[0].pnt[0];
			vm_vec_unrotate(&gun_pos, &pnt, &Pl_objp->orient);
			vm_vec_add2(&gun_pos, &Pl_objp->pos);
		} else {
			//Use the convergence offset, if there is one
			vm_vec_copy_scale(&pnt, &Ship_info[shipp->ship_info_index].convergence_offset, 1.0f);	
		}
		weapon_speed = ai_get_weapon_speed(&shipp->weapons);
		
		set_predicted_enemy_pos_turret(&predicted_enemy_pos, &gun_pos, Pl_objp, enemy_pos, &En_objp->phys_info.vel, weapon_speed, aip->time_enemy_in_range);
	}

	*dist_to_enemy = vm_vec_normalized_dir(&vec_to_enemy, &predicted_enemy_pos, &player_pos);
	*dot_to_enemy = vm_vec_dot(&vec_to_enemy, &Pl_objp->orient.vec.fvec);
	update_aspect_lock_information(aip, &vec_to_enemy, *dist_to_enemy, En_objp->radius);

	*enemy_pos = predicted_enemy_pos;
}

// check to see if Pl_objp has gotten too close to attacking point.. if so, break off by entering
// AIS_STRAFE_RETREAT
static bool ai_big_strafe_maybe_retreat(const vec3d *target_pos)
{
	ai_info	*aip;
	aip = &Ai_info[Ships[Pl_objp->instance].ai_index];

	vec3d vec_to_target;
	vm_vec_sub(&vec_to_target, target_pos, &Pl_objp->pos);

	float dist_to_target = vm_vec_mag(&vec_to_target);

	// add distance penalty for going too fast
	float speed_to_dist_penalty = MAX(0.0f, (Pl_objp->phys_info.speed-50));

	bool collide_time;
	bool collide_distance;
	if (The_mission.ai_profile->flags[AI::Profile_Flags::Fix_standard_strafe]) {
		// check if ship facing target, as likely will only collide if facing
		vec3d vec_to_tpos;
		vm_vec_normalized_dir(&vec_to_tpos, target_pos, &Pl_objp->pos);
		float dot = vm_vec_dot(&vec_to_tpos, &Pl_objp->orient.vec.fvec);
		if (dot <= 0.0f) {
			collide_time = false;
			collide_distance = false;
		} else {
			collide_time = (dist_to_target / Pl_objp->phys_info.speed) < STRAFE_RETREAT_COLLIDE_TIME;
			collide_distance = dist_to_target < (STRAFE_RETREAT_COLLIDE_DIST + speed_to_dist_penalty);
		}
	} else {
		float dist_normal_to_target;
		if (vm_vec_mag(&aip->big_attack_surface_normal) > 0.9) {
			dist_normal_to_target =
				MAX(0.2f * dist_to_target, -vm_vec_dot(&vec_to_target, &aip->big_attack_surface_normal));
		} else {
			dist_normal_to_target = 0.2f * dist_to_target;
		}
		collide_time = (dist_normal_to_target / Pl_objp->phys_info.speed) < STRAFE_RETREAT_COLLIDE_TIME;
		collide_distance = dist_normal_to_target < (STRAFE_RETREAT_COLLIDE_DIST + speed_to_dist_penalty);
	}

	//if ((dot_to_enemy > 1.0f - 0.1f * En_objp->radius/(dist_to_enemy + 1.0f)) && (Pl_objp->phys_info.speed > dist_to_enemy/5.0f))

	// Inside 2 sec retreat, setting goal point to box point + strafe_retreat_box_dist
	// If collision, use std collision resolution.
	if ( !(aip->ai_flags[AI::AI_Flags::Kamikaze]) && ((aip->ai_flags[AI::AI_Flags::Target_collision]) || (collide_time) || (collide_distance)) ) {
		if (aip->ai_flags[AI::AI_Flags::Target_collision]) {
			// use standard collision resolution
			aip->ai_flags.remove(AI::AI_Flags::Target_collision);
			big_ship_collide_recover_start(Pl_objp, En_objp, nullptr);
		} else {
			// too close for comfort so fly to box point + strafe_retreat_box_dist
			aip->submode = AIS_STRAFE_RETREAT1;
			aip->submode_start_time = Missiontime;

			int is_inside;
			vec3d goal_point;
			get_world_closest_box_point_with_delta(&goal_point, En_objp, &Pl_objp->pos, &is_inside, The_mission.ai_profile->strafe_retreat_box_dist);

			// set goal point
			aip->goal_point = goal_point;

			if (ai_maybe_fire_afterburner(Pl_objp, aip)) {
				afterburners_start(Pl_objp);
				aip->afterburner_stop_time = Missiontime + 3*F1_0;
			}
		}

		return 1;
	} else {
		return 0;
	}
}

// attack directly to the turret and fire weapons 
void ai_big_strafe_attack()
{
	ai_info	*aip;
	vec3d	target_pos;
	vec3d	rand_vec;
	float	accel, t;
	float target_dist = 0.0f;
	float target_dot = 0.0f;

	aip = &Ai_info[Ships[Pl_objp->instance].ai_index];

	if (aip->ai_profile_flags[AI::Profile_Flags::Improved_subsystem_attack_pathing]) {
		if (ai_new_maybe_reposition_attack_subsys())
			return;
	} else if (ai_big_maybe_follow_subsys_path(false))
		return;

	ai_big_attack_get_data(&target_pos, &target_dist, &target_dot);
	if ( ai_big_strafe_maybe_retreat(&target_pos) )
		return;

	if (aip->ai_flags[AI::AI_Flags::Kamikaze]) {
		if (target_dist < 1200.0f) {
			ai_turn_towards_vector(&target_pos, Pl_objp, nullptr, nullptr, 0.0f, 0);
			accelerate_ship(aip, 1.0f);
			if ((target_dist < 400.0f) && ai_maybe_fire_afterburner(Pl_objp, aip)) {
				afterburners_start(Pl_objp);
				aip->afterburner_stop_time = Missiontime + 3*F1_0;
			}
			return;
		}
	}

	if (!(aip->ai_flags[AI::AI_Flags::Seek_lock]) || (aip->aspect_locked_time < 2.0f)) {
		t  = ai_endangered_by_weapon(aip);
		if ( t > 0.0f && t < 1.5f ) {
			// set up goal_point for avoid path to turn towards
			aip->goal_point = Pl_objp->pos;
			switch(Random::next(4)) {
			case 0:
				vm_vec_scale_add(&rand_vec, &Pl_objp->orient.vec.fvec, &Pl_objp->orient.vec.uvec, 2.0f);
				break;
			case 1:
				vm_vec_scale_add(&rand_vec, &Pl_objp->orient.vec.fvec, &Pl_objp->orient.vec.uvec, -2.0f);
				break;
			case 2:
				vm_vec_scale_add(&rand_vec, &Pl_objp->orient.vec.fvec, &Pl_objp->orient.vec.rvec, 2.0f);
				break;
			case 3:
				vm_vec_scale_add(&rand_vec, &Pl_objp->orient.vec.fvec, &Pl_objp->orient.vec.rvec, -2.0f);
				break;
			} // end switch

			vm_vec_scale(&rand_vec, 1000.0f);
			vm_vec_add2(&aip->goal_point, &rand_vec);

			aip->submode = AIS_STRAFE_AVOID;
			aip->submode_start_time = Missiontime;

			if (ai_maybe_fire_afterburner(Pl_objp, aip)) {
				afterburners_start(Pl_objp);
				aip->afterburner_stop_time = Missiontime + fl2f(0.5f);
			}
		}
	}

	// maybe fire weapons at target
	ai_big_maybe_fire_weapons(target_dist, target_dot);
	turn_towards_point(Pl_objp, &target_pos, NULL, 0.0f);

	fix last_hit = Missiontime - aip->last_hit_time;
	if ( aip->ai_profile_flags[AI::Profile_Flags::Ai_can_slow_down_attacking_big_ships] ) {
		// Slow down if we've not been hit for a while
		if ( target_dist > 1200 || last_hit < F1_0*6) {
			accel = 1.0f;
		} else {
			float attack_time;
			attack_time = f2fl(Missiontime - aip->submode_start_time);
			if ( attack_time > 15 ) {
				accel = 0.2f;
			} else if ( attack_time > 10 ) {
				accel = 0.4f;
			} else if ( attack_time > 8 ) {
				accel = 0.6f;
			} else if ( attack_time > 5 ) {
				accel = 0.8f;
			} else {
				accel = 1.0f;
			}
		}
	} else {
		accel = 1.0f;
	}

	accelerate_ship(aip, accel);

	// if haven't been hit in quite a while, leave strafe mode
	fix long_enough = fl2f(The_mission.ai_profile->strafe_max_unhit_time);
	if ( (last_hit > long_enough) && ( (Missiontime - aip->submode_parm0) > long_enough) ) {
		ai_big_switch_to_chase_mode(aip);
	}
}

// SUSHI: Strafe attack using glide
//submode_parm1 encodes the stage we're in.
//0: Choosing goal point to fly towards
//1: Accelerating towards the goal point.
//2: Raining down fire and brimstone on the target.
//3: Dummy stage used when resetting or exiting the mode
void ai_big_strafe_glide_attack()
{
	ai_info *aip;
	vec3d target_pos, vec_to_goal, predicted_enemy_pos, norm_vel_vec;
	float target_dist, target_dot, target_ship_dist, dot_to_goal, flight_dot_to_enemy;
	object *target_objp;

	aip = &Ai_info[Ships[Pl_objp->instance].ai_index];
	target_objp = &Objects[aip->target_objnum];

	vm_vec_sub(&vec_to_goal, &aip->goal_point, &Pl_objp->pos);
	vm_vec_normalize(&vec_to_goal);

	if (vm_vec_mag(&Pl_objp->phys_info.vel) > 0.0f)
		vm_vec_copy_normalize(&norm_vel_vec, &Pl_objp->phys_info.vel);
	else
		vm_vec_copy_normalize(&norm_vel_vec, &Pl_objp->orient.vec.fvec);

	dot_to_goal = vm_vec_dot(&vec_to_goal, &norm_vel_vec);	//Angle between flight path and goal point
	flight_dot_to_enemy = vm_vec_dot(&target_objp->pos, &norm_vel_vec);	//Angle between flight path and target ship

	//If we're following a path, then we shouldn't be doing glide strafe
	if (aip->ai_profile_flags[AI::Profile_Flags::Improved_subsystem_attack_pathing]) {
		if (ai_new_maybe_reposition_attack_subsys())
			return;
	} else if (ai_big_maybe_follow_subsys_path(false))
		return;

	//Gets a point on the target ship to attack, as well as distance and the angle between the nose of the attacker and that point.
	ai_big_attack_get_data(&target_pos, &target_dist, &target_dot);	
	target_ship_dist = vm_vec_dist(&target_objp->pos, &Pl_objp->pos);

	//If we haven't chosen the goal point yet, do so now
	if (aip->submode_parm1 == 0) {
		//Pick goal point. This should be a random point that passes through the circle whose normal is the vector between attacker and target
		vec3d targetToAttackerDir;
		vm_vec_normalized_dir(&targetToAttackerDir, &Pl_objp->pos, &target_objp->pos);
		matrix orient;
		vm_vector_2_matrix_norm(&orient, &targetToAttackerDir, nullptr, nullptr);
		vec3d tangentPoint;
		vm_vec_random_in_circle(&tangentPoint, &target_objp->pos, &orient, target_objp->radius + GLIDE_STRAFE_DISTANCE, true);
		//Get tangent point in coords relative to ship, scale it up so the actual goal point is far away, then put back in world coords
		vm_vec_sub2(&tangentPoint, &Pl_objp->pos);
		vm_vec_scale(&tangentPoint, 1000.0f);
		vm_vec_add(&aip->goal_point, &tangentPoint, &Pl_objp->pos);
		aip->submode_parm1 = 1;
	}
	if (aip->submode_parm1 == 1) {
		//Stay in acceleration stage?
		//Accelerate towards goal point until:
		//	Mostly up to speed AND
		//	Moving in the correct direction
		if ((Pl_objp->phys_info.fspeed > (Pl_objp->phys_info.max_vel.xyz.z * 0.85f)) && (dot_to_goal > 0.99f)) 
			aip->submode_parm1 = 2;
	}
	if (aip->submode_parm1 == 2) {
		//Should we stay in the fire stage?
		//Keep going until we are too far away.
		//If we are still on approach but too far away, this will still trigger. This will allow us to reposition the target
		//point and allow for a "jinking" effect.
		if (target_ship_dist > (The_mission.ai_profile->strafe_retreat_box_dist + target_objp->radius) &&
			Missiontime - aip->submode_start_time > i2f(GLIDE_STRAFE_MIN_TIME)) {
			//This checks whether we are moving toward the target or away from it.  If moving towards, we reset the stage so that we
			//pick a new attack vector (jinking). If moving away, we're at the end of a run so do a full reset (possibly allowing a 
			//switch back to conventional strafe).
			if (flight_dot_to_enemy > 0.0f) {
				aip->submode_parm1 = 0;
			}
			else {
				aip->submode_parm1 = 3;
				aip->submode = AIS_STRAFE_POSITION;
			}
			aip->submode_start_time = Missiontime;
		}
	}
	//Time limit: if we've taken too long, reset
	if (Missiontime - aip->submode_start_time > i2f(GLIDE_STRAFE_MAX_TIME)) {
			aip->submode_parm1 = 3;
			aip->submode = AIS_STRAFE_POSITION;
			aip->submode_start_time = Missiontime;
	}

	//Acceleration stage
	if (aip->submode_parm1 == 1) {
		accelerate_ship(aip, 1.0f);
		//Use afterburners if we have them and are pointed the right way
		if (dot_to_goal > 0.99f) {
			afterburners_start(Pl_objp);
			aip->afterburner_stop_time = Missiontime + 3*F1_0;
		}

		turn_towards_point(Pl_objp, &aip->goal_point, NULL, 0.0f);
	}
	//Fire stage
	if (aip->submode_parm1 == 2) {
		Pl_objp->phys_info.flags |= PF_GLIDING;
		accelerate_ship(aip, 0.0f);
		afterburners_stop(Pl_objp);

		//Compensate for motion of ship and target
		set_predicted_enemy_pos(&predicted_enemy_pos, Pl_objp, &En_objp->pos, &En_objp->phys_info.vel, aip);
		vm_vec_add2(&target_pos, &predicted_enemy_pos);
		vm_vec_sub2(&target_pos, &En_objp->pos);

		turn_towards_point(Pl_objp, &target_pos, NULL, 0.0f);
		ai_big_maybe_fire_weapons(target_dist, target_dot);
	}

	// if haven't been hit in quite a while, leave strafe mode
	// (same as ai_big_strafe_attack)
	fix long_enough = fl2f(The_mission.ai_profile->strafe_max_unhit_time);
	if ( (Missiontime - aip->last_hit_time > long_enough) && ( (Missiontime - aip->submode_parm0) > long_enough) ) {
		ai_big_switch_to_chase_mode(aip);
	}
}

// pick a new attack point when entering this state, and keep using it
void ai_big_strafe_avoid()
{
	ai_info	*aip;
	vec3d	target_pos;
	float		target_dist, target_dot;
	fix		mode_time;

	aip = &Ai_info[Ships[Pl_objp->instance].ai_index];

	mode_time = Missiontime - aip->submode_start_time;

	ai_big_attack_get_data(&target_pos, &target_dist, &target_dot);
	if ( ai_big_strafe_maybe_retreat(&target_pos) )
		return;

	if ( mode_time > fl2f(0.5)) {
		ai_big_strafe_position();
	}

	turn_towards_point(Pl_objp, &aip->goal_point, NULL, 0.0f);
	accelerate_ship(aip, 1.0f);
}

// move towards aip->goal_point in an evasive manner
void ai_big_strafe_retreat1()
{
	float dist;
	ai_info	*aip;
	vec3d	rand_vec;

	aip = &Ai_info[Ships[Pl_objp->instance].ai_index];

	dist = vm_vec_dist_quick(&Pl_objp->pos, &aip->goal_point);
	if ( dist < 70 ) {
		aip->submode = AIS_STRAFE_POSITION;
		aip->submode_start_time = Missiontime;
//		nprintf(("Alan","Ship %s entering AIS_STRAFE_POSITION\n", Ships[aip->shipnum].ship_name));
		return;
	}

	if (Missiontime - aip->submode_start_time > fl2f(1.50f)) {
		// set up goal_point for avoid path to turn towards
		aip->prev_goal_point = Pl_objp->pos;
		switch(Random::next(4)) {
		case 0:
			vm_vec_add(&rand_vec, &Pl_objp->orient.vec.fvec, &Pl_objp->orient.vec.uvec);
			break;
		case 1:
			vm_vec_scale_add(&rand_vec, &Pl_objp->orient.vec.fvec, &Pl_objp->orient.vec.uvec, -1.0f);
			break;
		case 2:
			vm_vec_add(&rand_vec, &Pl_objp->orient.vec.fvec, &Pl_objp->orient.vec.rvec);
			break;
		case 3:
			vm_vec_scale_add(&rand_vec, &Pl_objp->orient.vec.fvec, &Pl_objp->orient.vec.rvec, -1.0f);
			break;
		} // end switch

		//vm_vec_scale(&rand_vec, 200.0f);
		//vm_vec_add2(&aip->prev_goal_point, &rand_vec);
		vm_vec_scale_add(&aip->prev_goal_point, &aip->goal_point, &rand_vec, 200.0f);

		aip->submode = AIS_STRAFE_RETREAT2;
		aip->submode_start_time = Missiontime;
	}

	turn_towards_point(Pl_objp, &aip->goal_point, NULL, 0.0f);
	accelerate_ship(aip, 1.0f);
}

void ai_big_strafe_retreat2()
{
	float dist;
	ai_info	*aip;

	aip = &Ai_info[Ships[Pl_objp->instance].ai_index];

	if (Pl_objp->phys_info.flags & PF_AFTERBURNER_ON ) {
		if (Missiontime > aip->afterburner_stop_time) {
			//nprintf(("AI", "Frame %i, turning off afterburner.\n", AI_FrameCount));
			afterburners_stop(Pl_objp);
		}
	}

	dist = vm_vec_dist_quick(&Pl_objp->pos, &aip->goal_point);
	if ( dist < 70 ) {
		aip->submode = AIS_STRAFE_POSITION;
		aip->submode_start_time = Missiontime;
//		nprintf(("Alan","Ship %s entering AIS_STRAFE_POSITION\n", Ships[aip->shipnum].ship_name));
		return;
	}

	if (Missiontime - aip->submode_start_time > fl2f(0.70f)) {
		aip->submode = AIS_STRAFE_RETREAT1;
		aip->submode_start_time = Missiontime;

		if ( (Missiontime - aip->last_hit_time) < F1_0*5 ) {
			if (ai_maybe_fire_afterburner(Pl_objp, aip)) {
				afterburners_start(Pl_objp);
				aip->afterburner_stop_time = Missiontime + F1_0;
			}
		}
	}

	turn_towards_point(Pl_objp, &aip->prev_goal_point, NULL, 0.0f);
	accelerate_ship(aip, 1.0f);
}

// reposition self to begin another strafing run
void ai_big_strafe_position()
{
	ship_info *sip;
	ai_info	*aip;
	sip = &Ship_info[Ships[Pl_objp->instance].ship_info_index];
	aip = &Ai_info[Ships[Pl_objp->instance].ai_index];

	//Maybe use AIS_STRAFE_GLIDE_ATTACK
	if ((sip->can_glide == true) && !(aip->ai_flags[AI::AI_Flags::Kamikaze]) && (frand() < aip->ai_glide_strafe_percent)) {
		aip->submode = AIS_STRAFE_GLIDE_ATTACK;
		aip->submode_parm1 = 0;
	} else {
		aip->submode = AIS_STRAFE_ATTACK;
	}
	aip->submode_start_time = Missiontime;
}

//	--------------------------------------------------------------------------
// #define	AIS_STRAFE_ATTACK		201	// fly towards target and attack
// #define	AIS_STRAFE_AVOID		202	// fly evasive vector to avoid incoming fire
// #define	AIS_STRAFE_RETREAT1	203	// fly away from attack point (directly)
// #define	AIS_STRAFE_RETREAT1	204	// fly away from attack point (on an avoid vector)
// #define	AIS_STRAFE_POSITION	205	// re-position to resume strafing attack
//
void ai_big_strafe()
{
	ai_info	*aip;

	aip = &Ai_info[Ships[Pl_objp->instance].ai_index];

	Assert(aip->mode == AIM_STRAFE);

/*
	if ( aip->goal_objnum != aip->target_objnum ) {
		UNREACHABLE("The goal objnum does not match the target objnum in ai_big_strafe(), please report to the SCP!");	// what is going on here? - Get Alan
		aip->mode = AIM_NONE;
		return;
	}
*/
	// check if target is still a big ship... if not enter chase mode
	if ( !(Ship_info[Ships[En_objp->instance].ship_info_index].is_big_or_huge()) ) {
		ai_big_switch_to_chase_mode(aip);
		return;
	}
	
	switch (aip->submode) {
	case AIS_STRAFE_ATTACK:
		ai_big_strafe_attack();
		break;
	case AIS_STRAFE_AVOID:
		ai_big_strafe_avoid();
		break;
	case AIS_STRAFE_RETREAT1:
		ai_big_strafe_retreat1();
		break;
	case AIS_STRAFE_RETREAT2:
		ai_big_strafe_retreat2();
		break;
	case AIS_STRAFE_POSITION:
		ai_big_strafe_position();
		break;
	case AIS_STRAFE_GLIDE_ATTACK:
		ai_big_strafe_glide_attack();
		break;
	default:

		Error(LOCATION, "Ai_big_strafe() just tried to use an illegal submode. Go tell a coder!");		
		break;
	}

	//Maybe apply random sidethrust, depending on the current submode
	//The following are valid targets for random sidethrust (circle strafe uses it too, but that is handled separately)
	if (aip->submode == AIS_STRAFE_ATTACK ||
		aip->submode == AIS_STRAFE_AVOID ||
		aip->submode == AIS_STRAFE_RETREAT1 ||
		aip->submode == AIS_STRAFE_RETREAT2 ||
		aip->submode == AIS_STRAFE_POSITION)
	{
		//Re-roll for random sidethrust every 2 seconds
		if (static_randf((Missiontime + static_rand(aip->shipnum)) >> 17) < aip->ai_random_sidethrust_percent) {
			do_random_sidethrust(aip, &Ship_info[Ships[aip->shipnum].ship_info_index]);
		}
	}
}

// See if Pl_objp should enter strafe mode (This is called from maybe_evade_dumbfire_weapon(), and the
// weapon_objnum is for a weapon that is about to collide with Pl_objp
//
// Check if weapon_objnum was fired by Pl_objp's target, and whether Pl_objp's target is a big ship, if
// so, enter AIM_STRAFE
int ai_big_maybe_enter_strafe_mode(const object *pl_objp, int weapon_objnum)
{
	ai_info		*aip;
	ship_info	*sip;
	object		*weapon_objp, *parent_objp;

	aip = &Ai_info[Ships[pl_objp->instance].ai_index];
	Assert(aip->mode != AIM_STRAFE);		// can't happen

	// if Pl_objp has no target, then we can't enter strafe mode
	if ( aip->target_objnum < 0 ) {
		return 0;
	}

	// if target is not a ship, stafe mode is not possible
	if ( Objects[aip->target_objnum].type != OBJ_SHIP ) {
		return 0;
	}

	sip = &Ship_info[Ships[Objects[aip->target_objnum].instance].ship_info_index];

	// if Pl_objp's target is not a big/capital ship, then cannot enter strafe mode
	// AL 12-31-97: Even though transports are considered big ships, don't enter strafe mode on them
	if ( !(sip->is_big_or_huge()) || (sip->flags[Ship::Info_Flags::Transport]) ) {
		return 0;
	}

	//	If Pl_objp not a fighter or bomber, don't enter strafe mode. -- MK, 11/11/97.
	if ( !(Ship_info[Ships[pl_objp->instance].ship_info_index].is_fighter_bomber()) ) {
		return 0;
	}

	Assert(weapon_objnum >= 0 && weapon_objnum < MAX_OBJECTS);
	weapon_objp = &Objects[weapon_objnum];
	Assert(weapon_objp->type == OBJ_WEAPON);

	if (weapon_objp->parent < 0) {
		return 0;
	}
	Assert(weapon_objp->parent < MAX_OBJECTS);
	parent_objp = &Objects[weapon_objp->parent];
	if ( (parent_objp->signature != weapon_objp->parent_sig) || (parent_objp->type != OBJ_SHIP) ) {
		return 0;
	}

	// Maybe the ship which fired the weapon isn't the current target
	if ( OBJ_INDEX(parent_objp) != aip->target_objnum ) {

//JAS IMPOSSIBLE		if (1) { // consider_target_only ) {
//JAS IMPOSSIBLE			return 0;
//JAS IMPOSSIBLE		} else {
			// switch targets
			sip = &Ship_info[Ships[parent_objp->instance].ship_info_index];
			if ( !(sip->is_big_or_huge()) || (sip->flags[Ship::Info_Flags::Transport]) ) {
				return 0;
			}
			set_target_objnum(aip, OBJ_INDEX(parent_objp));
//JAS IMPOSSIBLE		}
	}

	ai_big_strafe_maybe_attack_turret(pl_objp, weapon_objp);

	// if we've got this far, the weapon must have come from the player's target, and it is a 
	// big/capital ship... so enter strafe mode
	aip->previous_mode = aip->mode;
	aip->mode = AIM_STRAFE;
	aip->submode_parm0 = Missiontime;	// use parm0 as time strafe mode entered (i.e. MODE start time)
	aip->submode = AIS_STRAFE_AVOID;
	aip->submode_start_time = Missiontime;
//	nprintf(("Alan","%s Accepted strafe mode\n", Ships[pl_objp->instance].ship_name));

	return 1;
}

// Consider attacking a turret, if a turret actually fired the weapon
// input:	ship_objp	=>	ship that will attack the turret
//				weapon_objp	=>	
void ai_big_strafe_maybe_attack_turret(const object *ship_objp, const object *weapon_objp)
{
	ai_info	*aip;
	object	*parent_objp;

	Assert(ship_objp->type == OBJ_SHIP);
	aip = &Ai_info[Ships[ship_objp->instance].ai_index];

	// Make decision to attack turret based on AI class.  The better AI ships will realize that
	// it is better to take out the turrets first on a big ship.
	if ( (frand()*100) > (aip->ai_courage-15) )
		return;

	// If ship is already attacking a subsystem, don't switch
	if ( aip->targeted_subsys != NULL ) {
		return;
	}

	// If the weapon is not from a turret, return
	if ( Weapons[weapon_objp->instance].turret_subsys == NULL ) {
		return;
	}

	Assert(weapon_objp->parent >= 0 && weapon_objp->parent < MAX_OBJECTS);
	parent_objp = &Objects[weapon_objp->parent];
	
	// UnknownPlayer : Decide whether or not this weapon was a beam, in which case it might be a good
	// idea to go after it even if its not on the current target. This is randomized so
	// the ai will not always go after different ships firing beams at them.
	// Approx 1/4 chance we'll go after the other ship's beam.

	bool attack_turret_on_different_ship = (aip->ai_profile_flags[AI::Profile_Flags::Big_ships_can_attack_beam_turrets_on_untargeted_ships])
		&& (Weapon_info[Weapons[weapon_objp->instance].weapon_info_index].wi_flags[Weapon::Info_Flags::Beam]) && (frand()*100 < 25.0f);

	// unless we're making an exception, we should only attack a turret if it sits on the current target
	if ( !attack_turret_on_different_ship )
	{
		if ( (parent_objp->signature != weapon_objp->parent_sig) || (parent_objp->type != OBJ_SHIP) ) {
			return;
		}
		
		if ( aip->target_objnum != OBJ_INDEX(parent_objp) ) {
			return;
		}
	}

	// attack the turret
	set_targeted_subsys(aip, Weapons[weapon_objp->instance].turret_subsys, OBJ_INDEX(parent_objp));
}
