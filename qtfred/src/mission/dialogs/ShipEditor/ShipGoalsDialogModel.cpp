#include "ShipGoalsDialogModel.h"
#include <globalincs/linklist.h>
#include <mission/object.h>
namespace fso {
	namespace fred {
		namespace dialogs {
			ShipGoalsDialogModel::ShipGoalsDialogModel(QObject* parent, EditorViewport* viewport, bool multi, int shipp, int wingp)
				: AbstractDialogModel(parent, viewport)
			{
				initializeData(multi, shipp, wingp);
			}
			void ShipGoalsDialogModel::init_combo_data()
			{
				// don't add more than one of the same string (case-insensitive)
				SCP_unordered_map<SCP_string, size_t, SCP_string_lcase_hash, SCP_string_lcase_equal_to> strings_to_indexes;

				// start by adding "None"
				auto none_str = "None";
				strings_to_indexes.emplace(none_str, 0);
				SCP_set<ai_goal_mode> none_set{ AI_GOAL_NONE };
				m_ai_goal_combo_data.clear();
				m_ai_goal_combo_data.emplace_back(none_str, std::move(none_set));

				// initialize the data used in the combo boxes in the Initial Orders dialog
				for (int i = 0; i < Ai_goal_list_size; ++i)
				{
					if (!valid[i])
						continue;
					auto &entry = Editor::getAi_goal_list()[i];

					// see if we already added the string
					auto ii = strings_to_indexes.find(entry.name);
					if (ii != strings_to_indexes.end())
					{
						// skip adding the string, but add the entry's goal definition to the combo box data at the existing index
						m_ai_goal_combo_data[ii->second].second.insert(entry.def);
					}
					else
					{
						// this string will correspond to the index that is about to be created
						strings_to_indexes[entry.name] = m_ai_goal_combo_data.size();

						// add the entry's goal definition as the first (maybe only) member of the set
						SCP_set<ai_goal_mode> new_set{ entry.def };
						m_ai_goal_combo_data.emplace_back(entry.name, std::move(new_set));
					}
				}
			}
			const SCP_vector<std::pair<const char*, SCP_set<ai_goal_mode>>> &ShipGoalsDialogModel::get_ai_goal_combo_data()
			{
				return m_ai_goal_combo_data;
			};
			ai_goal_mode ShipGoalsDialogModel::get_first_mode_from_combo_box(int which_item)
			{
				// which_item indicates initial goal 1 through MAX_AI_GOALS, so find that behavior...
				int behavior_index = m_behavior[which_item];

				// if we have a superposition of behaviors, bail here
				if (behavior_index < 0)
					return ai_goal_mode::AI_GOAL_SCHROEDINGER;

				// the behavior is the index into the combo box that contains a subset of goals from Ai_goal_list
				const auto &set = m_ai_goal_combo_data[behavior_index].second;

				// just get the first mode in the set, since chase/chase-wing and guard/guard-wing are handled respectively together
				return *(set.begin());
			}
			bool ShipGoalsDialogModel::apply()
			{
				int i;

				if (goalp) {
					for (i = 0; i < ED_MAX_GOALS; i++)
						update_item(i);

					verify_orders();

				}
				else {
					object* ptr;

					ptr = GET_FIRST(&obj_used_list);
					while (ptr != END_OF_LIST(&obj_used_list)) {
						if (((ptr->type == OBJ_SHIP) || (ptr->type == OBJ_START)) && (ptr->flags[Object::Object_Flags::Marked])) {
							goalp = Ai_info[Ships[ptr->instance].ai_index].goals;
							for (i = 0; i < ED_MAX_GOALS; i++) {
								update_item(i);
							}
						}

						ptr = GET_NEXT(ptr);
					}

					ptr = GET_FIRST(&obj_used_list);
					while (ptr != END_OF_LIST(&obj_used_list)) {
						if (((ptr->type == OBJ_SHIP) || (ptr->type == OBJ_START)) && (ptr->flags[Object::Object_Flags::Marked])) {
							self_ship = ptr->instance;
							goalp = Ai_info[Ships[self_ship].ai_index].goals;
							verify_orders(self_ship);
						}

						ptr = GET_NEXT(ptr);
					}
				}

				_editor->missionChanged();
				return true;
			}

			int ShipGoalsDialogModel::verify_orders(const int ship)
			{
				const char* str;
				SCP_string error_message;
				if ((str = _editor->error_check_initial_orders(goalp, self_ship, self_wing)) != nullptr) {
					if (*str == '!')
						return 1;
					else if (*str == '*')
						str++;

					if (ship >= 0)
						sprintf(error_message, "Initial orders error for ship \"%s\"\n\n%s", Ships[ship].ship_name, str);
					else
						error_message = str;
					auto button = _viewport->dialogProvider->showButtonDialog(DialogType::Error,
						"Order Error",
						error_message,
						{ DialogButton::Ok, DialogButton::Cancel });
					if (button != DialogButton::Ok)
						return 1;
				}

				return 0;
			}

			void ShipGoalsDialogModel::update_item(const int item)
			{
				char* docker, * dockee, * subsys;
				ai_goal_mode mode;
				char save[80]{};
				SCP_string error_message;
				waypoint_list* wp_list;

				if (item >= MAX_AI_GOALS)
					return;

				if (!m_multi_edit || m_priority[item] >= 0)
					goalp[item].priority = m_priority[item];

				mode = get_first_mode_from_combo_box(item);
				switch (mode) {
				case AI_GOAL_NONE:
				case AI_GOAL_CHASE_ANY:
				case AI_GOAL_UNDOCK:
				case AI_GOAL_KEEP_SAFE_DISTANCE:
				case AI_GOAL_PLAY_DEAD:
				case AI_GOAL_PLAY_DEAD_PERSISTENT:
				case AI_GOAL_WARP:
					// these goals do not have a target in the dialog box, so let's set the goal and return immediately
					// so that we don't run afoul of the "doesn't have a valid target" code at the bottom of the function
					modify(goalp[item].ai_mode, mode);
					return;

				case AI_GOAL_SCHROEDINGER:
					// return, but don't set the goal
					return;

				case AI_GOAL_WAYPOINTS:
				case AI_GOAL_WAYPOINTS_ONCE:
				case AI_GOAL_DISABLE_SHIP:
				case AI_GOAL_DISABLE_SHIP_TACTICAL:
				case AI_GOAL_DISARM_SHIP:
				case AI_GOAL_DISARM_SHIP_TACTICAL:
				case AI_GOAL_IGNORE:
				case AI_GOAL_IGNORE_NEW:
				case AI_GOAL_EVADE_SHIP:
				case AI_GOAL_STAY_NEAR_SHIP:
				case AI_GOAL_STAY_STILL:
				case AI_GOAL_CHASE_SHIP_CLASS:
					break;

				case AI_GOAL_DESTROY_SUBSYSTEM:
					subsys = nullptr;
					if (!m_multi_edit || (m_object[item] && (m_subsys[item].c_str() != nullptr)))
						subsys = (char*)m_subsys[item].c_str();
					//MODIFY(goalp[item].ai_submode, m_subsys[item] + 1);

					if (!subsys) {
						sprintf(error_message, "Order #%d doesn't have valid subsystem name.  Order will be removed", item + 1);
						_viewport->dialogProvider->showButtonDialog(DialogType::Information,
							"Order Error",
							error_message,
							{ DialogButton::Ok });
						modify(goalp[item].ai_mode, AI_GOAL_NONE);
						return;

					}
					else {
						if (!goalp[item].docker.name || (goalp[item].docker.name && !stricmp(goalp[item].docker.name, subsys)))
							set_modified();

						goalp[item].docker.name = subsys;
					}

					break;

				case AI_GOAL_CHASE:
				case AI_GOAL_CHASE_WING:
					switch (m_object[item] & TYPE_MASK) {
					case TYPE_SHIP:
					case TYPE_PLAYER:
						mode = AI_GOAL_CHASE;
						break;

					case TYPE_WING:
						mode = AI_GOAL_CHASE_WING;
						break;
					}

					break;

				case AI_GOAL_DOCK:
					docker = nullptr;
					if (!m_multi_edit || (m_object[item] && (m_subsys[item].c_str() != nullptr)))
						docker = (char*)m_subsys[item].c_str();

					dockee = nullptr;
					if (!m_multi_edit || (m_object[item] && (m_dock2[item] >= 0)))
						dockee = (char *)m_dock2[item];

					if (docker == (char*)SIZE_MAX)
						docker = nullptr;
					if (dockee == (char*)SIZE_MAX)
						dockee = nullptr;

					if (!docker || !dockee) {
						sprintf(error_message, "Order #%d doesn't have valid docking points.  Order will be removed", item + 1);
						_viewport->dialogProvider->showButtonDialog(DialogType::Information,
							"Order Error",
							error_message,
							{ DialogButton::Ok });
						modify(goalp[item].ai_mode, AI_GOAL_NONE);
						return;

					}
					else {
						if (!goalp[item].docker.name)
							set_modified();
						else if (!stricmp(goalp[item].docker.name, docker))
							set_modified();

						if (!goalp[item].dockee.name)
							set_modified();
						else if (!stricmp(goalp[item].dockee.name, dockee))
							set_modified();

						goalp[item].docker.name = docker;
						goalp[item].dockee.name = dockee;
					}

					break;

				case AI_GOAL_GUARD:
				case AI_GOAL_GUARD_WING:
					switch (m_object[item] & TYPE_MASK) {
					case TYPE_SHIP:
					case TYPE_PLAYER:
						mode = AI_GOAL_GUARD;
						break;

					case TYPE_WING:
						mode = AI_GOAL_GUARD_WING;
						break;
					}

					break;

				default:
					Warning(LOCATION, "Unknown AI_GOAL type 0x%x", mode);
					modify(goalp[item].ai_mode, AI_GOAL_NONE);
					return;
				}
				modify(goalp[item].ai_mode, mode);

				*save = 0;
				if (goalp[item].target_name)
					strcpy_s(save, goalp[item].target_name);

				switch (m_object[item] & TYPE_MASK) {
					int not_used;

				case TYPE_SHIP:
				case TYPE_PLAYER:
					goalp[item].target_name = ai_get_goal_target_name(Ships[m_object[item] & DATA_MASK].ship_name, &not_used);
					break;

				case TYPE_WING:
					goalp[item].target_name = ai_get_goal_target_name(Wings[m_object[item] & DATA_MASK].name, &not_used);
					break;

				case TYPE_PATH:
					wp_list = find_waypoint_list_at_index(m_object[item] & DATA_MASK);
					Assert(wp_list != nullptr);
					goalp[item].target_name = ai_get_goal_target_name(wp_list->get_name(), &not_used);
					break;

				case TYPE_WAYPOINT:
					goalp[item].target_name = ai_get_goal_target_name(object_name(m_object[item] & DATA_MASK), &not_used);
					break;

				case TYPE_SHIP_CLASS:
					goalp[item].target_name = ai_get_goal_target_name(Ship_info[m_object[item] & DATA_MASK].name, &not_used);
					break;

				case 0:
				case (-1 & TYPE_MASK):
					if (m_multi_edit)
						return;

					sprintf(error_message, "Order #%d doesn't have a valid target.  Order will be removed", item + 1);
					_viewport->dialogProvider->showButtonDialog(DialogType::Information,
						"Order Error",
						error_message,
						{ DialogButton::Ok });
					modify(goalp[item].ai_mode, AI_GOAL_NONE);
					return;

				default:
					Error(LOCATION, "Unhandled TYPE_X #define %d in ship goals dialog box", m_object[item] & TYPE_MASK);
				}

				if (stricmp(save, goalp[item].target_name))
					set_modified();
			}

			void ShipGoalsDialogModel::reject() {}

			void ShipGoalsDialogModel::initializeData(const bool multi, const int shipp, const int wingp)
			{
				int i, j, z;
				object* ptr;
				for (i = 0; i < ED_MAX_GOALS; i++) {
					m_behavior[i] = -1;
					m_object[i] = -1;
					m_priority[i] = 0;
					m_subsys[i] = "";
					m_dock2[i] = -1;
					// m_data[i] = 0;
				}
				goalp = nullptr;
				m_multi_edit = multi;
				self_ship = shipp;
				self_wing = wingp;
				Assert(Ai_goal_list_size <= MAX_VALID);


				// start off with all goals available
				for (i = 0; i < Ai_goal_list_size; i++) {
					valid[i] = 1;
				}

				if (self_ship >= 0) { // editing orders for just one ship
					for (i = 0; i < Ai_goal_list_size; i++) {
						if (!(ai_query_goal_valid(self_ship, Editor::getAi_goal_list()[i].def))) {
							valid[i] = 0;
						}
					}
				}
				else if (self_wing >= 0) { // editing orders for just one wing
					for (i = 0; i < Wings[self_wing].wave_count; i++) {
						for (j = 0; j < Ai_goal_list_size; j++) {
							if (!ai_query_goal_valid(Wings[self_wing].ship_index[i], Editor::getAi_goal_list()[j].def)) {
								valid[j] = 0;
							}
						}
					}
					for (i = 0; i < Ai_goal_list_size; i++) {
						if (Editor::getAi_goal_list()[i].def == AI_GOAL_DOCK) { // a whole wing can't dock with one object..
							valid[i] = 0;
						}
					}
				}
				else { // editing orders for all marked ships
					ptr = GET_FIRST(&obj_used_list);
					while (ptr != END_OF_LIST(&obj_used_list)) {
						if (((ptr->type == OBJ_SHIP) || (ptr->type == OBJ_START)) && (ptr->flags[Object::Object_Flags::Marked])) {
							for (i = 0; i < Ai_goal_list_size; i++) {
								if (!ai_query_goal_valid(ptr->instance, Editor::getAi_goal_list()[i].def)) {
									valid[i] = 0;
								}
							}
						}

						ptr = GET_NEXT(ptr);
					}
				}
				if (Waypoint_lists.empty()) {
					for (i = 0; i < Ai_goal_list_size; i++) {
						switch (Editor::getAi_goal_list()[i].def) {
						case AI_GOAL_WAYPOINTS:
						case AI_GOAL_WAYPOINTS_ONCE:
							// case AI_GOAL_WARP:
							valid[i] = 0;
							break;
						default:
							break;
						}
					}
				}

				z = 0;
				ptr = GET_FIRST(&obj_used_list);
				while (ptr != END_OF_LIST(&obj_used_list)) {
					if ((ptr->type == OBJ_SHIP) || (ptr->type == OBJ_START)) {
						i = ptr->instance;

						if ((self_ship > 0) && (self_ship != i) && ship_docking_valid(self_ship, i)) {
							z = 1;
						}
					}
					ptr = GET_NEXT(ptr);
				}

				if (!z) {
					for (i = 0; i < Ai_goal_list_size; i++) {
						if (Editor::getAi_goal_list()[i].def == AI_GOAL_DOCK) {
							valid[i] = 0;
						}
					}
				}

				// initialize the data for the behavior boxes (they remain constant while the dialog is open)
				init_combo_data();

				if (self_ship >= 0) {
					initialize(Ai_info[Ships[self_ship].ai_index].goals, self_ship);
				}
				else if (self_wing >= 0) {
					initialize(Wings[self_wing].ai_goals, _editor->cur_ship);
				}
				else {
					initialize_multi();
				}
				modelChanged();
			}
			void ShipGoalsDialogModel::initialize(ai_goal* goals, int ship)
			{
				int i, item, num, inst, flag;
				ai_goal_mode mode;
				object* ptr;
				SCP_vector<SCP_string> docks;

				// note that the flag variable is a bitfield:
				// 1 = ships
				// 2 = wings
				// 4 = waypoint paths
				// 8 = individual waypoints
				// 16 = ship classes

				goalp = goals;
				for (item = 0; item < ED_MAX_GOALS; item++) {
					flag = 1;
					//m_data[item] = 0;
					m_priority[item] = 0;
					mode = AI_GOAL_NONE;

					if (item < MAX_AI_GOALS) {
						m_priority[item] = goalp[item].priority;
						mode = goalp[item].ai_mode;
					}

					if (m_priority[item] < 0 || m_priority[item] > MAX_EDITOR_GOAL_PRIORITY) {
						m_priority[item] = 50;
					}

					m_behavior[item] = 0;
					if (mode != AI_GOAL_NONE) {
						i = static_cast<int>(m_ai_goal_combo_data.size());
						while (i-- > 0) {
							const auto &set = m_ai_goal_combo_data[i].second;
							if (set.find(mode) != set.end()) {
								m_behavior[item] = i;
								break;
							}
						}
					}

					switch (mode) {
					case AI_GOAL_NONE:
					case AI_GOAL_CHASE_ANY:
					case AI_GOAL_UNDOCK:
					case AI_GOAL_KEEP_SAFE_DISTANCE:
					case AI_GOAL_PLAY_DEAD:
					case AI_GOAL_PLAY_DEAD_PERSISTENT:
					case AI_GOAL_WARP:
						continue;

					case AI_GOAL_CHASE_SHIP_CLASS:
						flag = 16; // target is a ship class
						break;

					case AI_GOAL_STAY_STILL:
						flag = 9; // target is a ship or a waypoint
						break;

					case AI_GOAL_CHASE:
					case AI_GOAL_GUARD:
					case AI_GOAL_DISABLE_SHIP:
					case AI_GOAL_DISABLE_SHIP_TACTICAL:
					case AI_GOAL_DISARM_SHIP:
					case AI_GOAL_DISARM_SHIP_TACTICAL:
					case AI_GOAL_IGNORE:
					case AI_GOAL_IGNORE_NEW:
					case AI_GOAL_EVADE_SHIP:
					case AI_GOAL_STAY_NEAR_SHIP:
						break;

					case AI_GOAL_WAYPOINTS:
					case AI_GOAL_WAYPOINTS_ONCE:
						flag = 4; // target is a waypoint
						break;

					case AI_GOAL_DESTROY_SUBSYSTEM:
						num = ship_name_lookup(goalp[item].target_name, 1);
						if (num != -1)
							m_subsys[item] = ship_find_subsys(&Ships[num], goalp[item].docker.name);

						break;

					case AI_GOAL_DOCK:
						m_subsys[item] = -1;
						docks = _editor->get_docking_list(Ship_info[Ships[ship].ship_info_index].model_num);
						for (i = 0; unsigned(i) < docks.size(); i++) {
							if (!stricmp(goalp[item].docker.name, docks[i].c_str())) {
								m_subsys[item] = i;
								break;
							}
						}

						break;

					case AI_GOAL_CHASE_WING:
					case AI_GOAL_GUARD_WING:
						flag = 2; // target is a wing
						break;

					default:
						Error(LOCATION, "Unhandled AI_GOAL_X #define %d in ship goals dialog box", mode);
					}

					if (flag & 0x1) {
						ptr = GET_FIRST(&obj_used_list);
						while (ptr != END_OF_LIST(&obj_used_list)) {
							if ((ptr->type == OBJ_SHIP) || (ptr->type == OBJ_START)) {
								inst = ptr->instance;
								if (ptr->type == OBJ_SHIP) {
									Assert(inst >= 0 && inst < MAX_SHIPS);
									if (!stricmp(goalp[item].target_name, Ships[inst].ship_name)) {
										m_object[item] = inst | TYPE_SHIP;
										break;
									}

								}
								else {
									Assert(inst >= 0 && inst < MAX_SHIPS);
									if (!stricmp(goalp[item].target_name, Ships[inst].ship_name)) {
										m_object[item] = inst | TYPE_PLAYER;
										break;
									}
								}
							}

							ptr = GET_NEXT(ptr);
						}
					}

					if (flag & 0x2) {
						for (i = 0; i < MAX_WINGS; i++) {
							if (Wings[i].wave_count) {
								if (!stricmp(goalp[item].target_name, Wings[i].name)) {
									m_object[item] = i | TYPE_WING;
									break;
								}
							}
						}
					}

					if (flag & 0x4) { // data is a waypoint path name
						SCP_vector<waypoint_list>::iterator ii;
						for (i = 0, ii = Waypoint_lists.begin(); ii != Waypoint_lists.end(); ++i, ++ii) {
							if (!stricmp(goalp[item].target_name, ii->get_name())) {
								m_object[item] = i | TYPE_PATH;
								break;
							}
						}
					}

					if (flag & 0x8) { // data is a waypoint name
						waypoint* wpt = find_matching_waypoint(goalp[item].target_name);
						if (wpt != nullptr)
							m_object[item] = wpt->get_objnum() | TYPE_WAYPOINT;
					}

					if (flag & 0x10) { // data is a ship class
						for (i = 0; i < ship_info_size(); i++) {
							if (!stricmp(goalp[item].target_name, Ship_info[i].name)) {
								m_object[item] = i | TYPE_SHIP_CLASS;
								break;
							}
						}
					}

					switch (mode) {
					case AI_GOAL_DOCK:
						m_dock2[item] = -1;
						if (m_object[item]) {
							docks =
								_editor->get_docking_list(Ship_info[Ships[m_object[item] & DATA_MASK].ship_info_index].model_num);
							for (i = 0; unsigned(i) < docks.size(); i++) {
								Assert(goalp[item].dockee.name);
								Assert(goalp[item].dockee.index != -1);
								if (!stricmp(goalp[item].dockee.name, docks[i].c_str())) {
									m_dock2[item] = i;
									break;
								}
							}
						}
						break;
					default:
						break;
					}

					//		Assert(m_data[item]);
				}
			}
			void ShipGoalsDialogModel::initialize_multi()
			{
				int i, flag = 0;
				object* ptr;
				int behavior[ED_MAX_GOALS]{};
				int priority[ED_MAX_GOALS]{};
				SCP_string subsys[ED_MAX_GOALS]{};
				int dock2[ED_MAX_GOALS]{};
				int data[ED_MAX_GOALS]{};

				ptr = GET_FIRST(&obj_used_list);
				while (ptr != END_OF_LIST(&obj_used_list)) {
					if (((ptr->type == OBJ_SHIP) || (ptr->type == OBJ_START)) && (ptr->flags[Object::Object_Flags::Marked])) {
						initialize(Ai_info[Ships[ptr->instance].ai_index].goals, ptr->instance);
						if (!flag) {
							flag = 1;
							for (i = 0; i < ED_MAX_GOALS; i++) {
								behavior[i] = m_behavior[i];
								priority[i] = m_priority[i];
								subsys[i] = m_subsys[i];
								dock2[i] = m_dock2[i];
								data[i] = m_object[i];
							}

						}
						else {
							for (i = 0; i < ED_MAX_GOALS; i++) {
								if (behavior[i] != m_behavior[i]) {
									behavior[i] = -1;
									data[i] = -1;
								}

								if (data[i] != m_object[i]) {
									data[i] = -1;
									subsys[i] = -1;
									dock2[i] = -1;
								}

								if (priority[i] != m_priority[i]) {
									priority[i] = -1;
								}
								if (subsys[i] != m_subsys[i]) {
									subsys[i] = -1;
								}
								if (dock2[i] != m_dock2[i]) {
									dock2[i] = -1;
								}
							}
						}
					}

					ptr = GET_NEXT(ptr);
				}

				goalp = nullptr;
				for (i = 0; i < ED_MAX_GOALS; i++) {
					m_behavior[i] = behavior[i];
					m_priority[i] = priority[i];
					m_subsys[i] = subsys[i];
					m_dock2[i] = dock2[i];
					m_object[i] = data[i];
				}
			}
			void ShipGoalsDialogModel::setShip(const int ship)
			{
				self_ship = ship;
			}
			int ShipGoalsDialogModel::getShip() const 
			{
				return self_ship;
			}
			void ShipGoalsDialogModel::setWing(const int data)
			{
				modify(self_wing, data);
			}
			int ShipGoalsDialogModel::getWing() const 
			{
				return self_wing;
			}
			ai_goal* ShipGoalsDialogModel::getGoal() const 
			{
				return goalp;
			}
			int ShipGoalsDialogModel::getValid(const int pos) const 
			{
				return valid[pos];
			}
			const ai_goal_list* ShipGoalsDialogModel::getGoalTypes()
			{
				return Editor::getAi_goal_list();
			}
			int ShipGoalsDialogModel::getGoalsSize() const 
			{
				return Ai_goal_list_size;
			}
			void ShipGoalsDialogModel::setBehavior(const int pos, const int data)
			{
				modify(m_behavior[pos], data);
			}
			int ShipGoalsDialogModel::getBehavior(const int pos) const 
			{
				return m_behavior[pos];
			}
			void ShipGoalsDialogModel::setObject(const int pos, const int data)
			{
				modify(m_object[pos], data);
			}
			int ShipGoalsDialogModel::getObject(const int pos) const 
			{
				return m_object[pos];
			}
			void ShipGoalsDialogModel::setSubsys(const int pos, const SCP_string& data)
			{
				modify(m_subsys[pos], data);
			}
			SCP_string ShipGoalsDialogModel::getSubsys(const int pos) const 
			{
				return m_subsys[pos];
			}
			void ShipGoalsDialogModel::setDock(const int pos, const long long data)
			{
				modify(m_dock2[pos], data);
			}
			int ShipGoalsDialogModel::getDock(const int pos) const 
			{
				return m_dock2[pos];
			}
			void ShipGoalsDialogModel::setPriority(const int pos, const int data)
			{
				modify(m_priority[pos], data);
			}
			int ShipGoalsDialogModel::getPriority(const int pos) const 
			{
				return m_priority[pos];
			}
		} // namespace dialogs
	} // namespace fred
} // namespace fso