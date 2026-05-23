
#include <cstring>
#include <string>
#include <vector>
#include "xwingbrflib.h"

#pragma pack(push, 1)
struct xwi_brf_header
{
	short version;
	short icon_count;
	short coordinate_set_count;
};

struct xwi_brf_coord
{
	short x, y, z;
};

struct xwi_brf_icon
{
	short craft_type;
	short iff;
	short number_of_craft;
	short number_of_waves;
	char  name[16];
	char  cargo[16];
	char  special_cargo[16];
	short special_cargo_craft;
	short yaw;
	short pitch;
	short roll;
};

struct xwi_brf_viewport_setting
{
	short top;
	short left;
	short bottom;
	short right;
	short visible;
};

struct xwi_brf_mission_header
{
	short time_limit;
	short end_event;
	short rnd_seed;
	short location;
	char  eom_messages[3][64];
};
#pragma pack(pop)

static bool decode_craft_type(short raw, XWMBrfIconType &out)
{
	switch (raw)
	{
		case 0x00: out = XWMBrfIconType::bi_None;                return true;
		case 0x01: out = XWMBrfIconType::bi_X_Wing;              return true;
		case 0x02: out = XWMBrfIconType::bi_Y_Wing;              return true;
		case 0x03: out = XWMBrfIconType::bi_A_Wing;              return true;
		case 0x04: out = XWMBrfIconType::bi_TIE_Fighter;         return true;
		case 0x05: out = XWMBrfIconType::bi_TIE_Interceptor;     return true;
		case 0x06: out = XWMBrfIconType::bi_TIE_Bomber;          return true;
		case 0x07: out = XWMBrfIconType::bi_Assault_Gunboat;     return true;
		case 0x08: out = XWMBrfIconType::bi_Transport;           return true;
		case 0x09: out = XWMBrfIconType::bi_Shuttle;             return true;
		case 0x0A: out = XWMBrfIconType::bi_Tug;                 return true;
		case 0x0B: out = XWMBrfIconType::bi_Container;           return true;
		case 0x0C: out = XWMBrfIconType::bi_Freighter;           return true;
		case 0x0D: out = XWMBrfIconType::bi_Calamari_Cruiser;    return true;
		case 0x0E: out = XWMBrfIconType::bi_Nebulon_B_Frigate;   return true;
		case 0x0F: out = XWMBrfIconType::bi_Corellian_Corvette;  return true;
		case 0x10: out = XWMBrfIconType::bi_Star_Destroyer;      return true;
		case 0x11: out = XWMBrfIconType::bi_TIE_Advanced;        return true;
		case 0x12: out = XWMBrfIconType::bi_Mine1;               return true;
		case 0x13: out = XWMBrfIconType::bi_Mine2;               return true;
		case 0x14: out = XWMBrfIconType::bi_Mine3;               return true;
		case 0x15: out = XWMBrfIconType::bi_Mine4;               return true;
		case 0x16: out = XWMBrfIconType::bi_Satellite;           return true;
		case 0x17: out = XWMBrfIconType::bi_Nav_Buoy;            return true;
		case 0x18: out = XWMBrfIconType::bi_Probe;               return true;
		case 0x19: out = XWMBrfIconType::bi_B_Wing;              return true;
		case 0x1A: out = XWMBrfIconType::bi_Asteroid1;           return true;
		case 0x1B: out = XWMBrfIconType::bi_Asteroid2;           return true;
		case 0x1C: out = XWMBrfIconType::bi_Asteroid3;           return true;
		case 0x1D: out = XWMBrfIconType::bi_Asteroid4;           return true;
		case 0x1E: out = XWMBrfIconType::bi_Asteroid5;           return true;
		case 0x1F: out = XWMBrfIconType::bi_Asteroid6;           return true;
		case 0x20: out = XWMBrfIconType::bi_Asteroid7;           return true;
		case 0x21: out = XWMBrfIconType::bi_Asteroid8;           return true;
		case 0x22: out = XWMBrfIconType::bi_Rock_World;          return true;
		case 0x23: out = XWMBrfIconType::bi_Gray_Ring_World;     return true;
		case 0x24: out = XWMBrfIconType::bi_Gray_World;          return true;
		case 0x25: out = XWMBrfIconType::bi_Brown_World;         return true;
		case 0x26: out = XWMBrfIconType::bi_Gray_World2;         return true;
		case 0x27: out = XWMBrfIconType::bi_Planet_and_Moon;     return true;
		case 0x28: out = XWMBrfIconType::bi_Gray_Crescent;       return true;
		case 0x29: out = XWMBrfIconType::bi_Orange_Crescent1;    return true;
		case 0x2A: out = XWMBrfIconType::bi_Orange_Crescent2;    return true;
		case 0x2B: out = XWMBrfIconType::bi_Orange_Crescent3;    return true;
		case 0x2C: out = XWMBrfIconType::bi_Orange_Crescent4;    return true;
		case 0x2D: out = XWMBrfIconType::bi_Orange_Crescent5;    return true;
		case 0x2E: out = XWMBrfIconType::bi_Orange_Crescent6;    return true;
		case 0x2F: out = XWMBrfIconType::bi_Orange_Crescent7;    return true;
		case 0x30: out = XWMBrfIconType::bi_Orange_Crescent8;    return true;
		case 0x31: out = XWMBrfIconType::bi_Death_Star;          return true;
		case 0x3A: out = XWMBrfIconType::bi_Training_Platform1;  return true;
		case 0x3B: out = XWMBrfIconType::bi_Training_Platform2;  return true;
		case 0x3C: out = XWMBrfIconType::bi_Training_Platform3;  return true;
		case 0x3D: out = XWMBrfIconType::bi_Training_Platform4;  return true;
		case 0x3E: out = XWMBrfIconType::bi_Training_Platform5;  return true;
		case 0x3F: out = XWMBrfIconType::bi_Training_Platform6;  return true;
		case 0x40: out = XWMBrfIconType::bi_Training_Platform7;  return true;
		case 0x41: out = XWMBrfIconType::bi_Training_Platform8;  return true;
		case 0x42: out = XWMBrfIconType::bi_Training_Platform9;  return true;
		case 0x43: out = XWMBrfIconType::bi_Training_Platform10; return true;
		case 0x44: out = XWMBrfIconType::bi_Training_Platform11; return true;
		case 0x45: out = XWMBrfIconType::bi_Training_Platform12; return true;
		default: return false;
	}
}

static bool decode_iff(short raw, XWMCraftIFF &out)
{
	switch (raw)
	{
		case 0: out = XWMCraftIFF::iff_default;  return true;
		case 1: out = XWMCraftIFF::iff_rebel;    return true;
		case 2: out = XWMCraftIFF::iff_imperial; return true;
		case 3: out = XWMCraftIFF::iff_neutral;  return true;
		default: return false;
	}
}

static bool decode_opcode(short raw, XWMBrfEventOpcode &out, int &param_count)
{
	switch (raw)
	{
		case 0x00: out = XWMBrfEventOpcode::op_None;          param_count = 0; return true;
		case 0x01: out = XWMBrfEventOpcode::op_WaitForClick;  param_count = 0; return true;
		case 0x0A: out = XWMBrfEventOpcode::op_ClearText;     param_count = 0; return true;
		case 0x0B: out = XWMBrfEventOpcode::op_TitleText;     param_count = 1; return true;
		case 0x0C: out = XWMBrfEventOpcode::op_CaptionText;   param_count = 1; return true;
		case 0x0E: out = XWMBrfEventOpcode::op_CaptionText2;  param_count = 1; return true;
		case 0x0F: out = XWMBrfEventOpcode::op_MoveMap;       param_count = 2; return true;
		case 0x10: out = XWMBrfEventOpcode::op_ZoomMap;       param_count = 2; return true;
		case 0x15: out = XWMBrfEventOpcode::op_ClearFGTags;   param_count = 0; return true;
		case 0x16: out = XWMBrfEventOpcode::op_FGTag1;        param_count = 1; return true;
		case 0x17: out = XWMBrfEventOpcode::op_FGTag2;        param_count = 1; return true;
		case 0x18: out = XWMBrfEventOpcode::op_FGTag3;        param_count = 1; return true;
		case 0x19: out = XWMBrfEventOpcode::op_FGTag4;        param_count = 1; return true;
		case 0x1A: out = XWMBrfEventOpcode::op_ClearTextTags; param_count = 0; return true;
		case 0x1B: out = XWMBrfEventOpcode::op_TextTag1;      param_count = 3; return true;
		case 0x1C: out = XWMBrfEventOpcode::op_TextTag2;      param_count = 3; return true;
		case 0x1D: out = XWMBrfEventOpcode::op_TextTag3;      param_count = 3; return true;
		case 0x1E: out = XWMBrfEventOpcode::op_TextTag4;      param_count = 3; return true;
		case 0x29: out = XWMBrfEventOpcode::op_EndBriefing;   param_count = 0; return true;
		default: return false;
	}
}

bool XWingBriefing::load(XWingBriefing *b, const char *data, size_t length)
{
	const char *p = data;
	const char *end = data + length;

	// 1. Header
	if (!has_room(p, end, sizeof(xwi_brf_header)))
		return false;
	auto h = reinterpret_cast<const xwi_brf_header *>(p);
	if (h->version != 2)
		return false;
	const int icon_count = h->icon_count;
	const int coord_set_count = h->coordinate_set_count;
	p += sizeof(xwi_brf_header);

	// 2. Coordinate sets — CoordinateCount sets of IconCount coords each
	b->coordinateSets.resize(coord_set_count);
	for (int set = 0; set < coord_set_count; set++)
	{
		auto &coords = b->coordinateSets[set];
		coords.resize(icon_count);
		for (int i = 0; i < icon_count; i++)
		{
			if (!has_room(p, end, sizeof(xwi_brf_coord)))
				return false;
			auto raw = reinterpret_cast<const xwi_brf_coord *>(p);
			coords[i].x = raw->x / 160.0f;
			coords[i].y = raw->y / 160.0f;
			coords[i].z = raw->z / 160.0f;
			p += sizeof(xwi_brf_coord);
		}
	}

	// 3. Icons
	b->icons.resize(icon_count);
	for (int i = 0; i < icon_count; i++)
	{
		if (!has_room(p, end, sizeof(xwi_brf_icon)))
			return false;
		auto raw = reinterpret_cast<const xwi_brf_icon *>(p);
		auto &icon = b->icons[i];

		if (!decode_craft_type(raw->craft_type, icon.craftType))
			return false;
		if (!decode_iff(raw->iff, icon.craftIFF))
			return false;

		icon.numberInWave = raw->number_of_craft;
		icon.numberOfWaves = raw->number_of_waves;

		icon.designation  = xwi_safe_string(raw->name);
		icon.cargo        = xwi_safe_string(raw->cargo);
		icon.specialCargo = xwi_safe_string(raw->special_cargo);

		icon.specialCargoCraft = raw->special_cargo_craft;
		icon.yaw   = raw->yaw;
		icon.pitch = raw->pitch;
		icon.roll  = raw->roll;

		p += sizeof(xwi_brf_icon);
	}

	// 4. WindowUISettings
	if (!has_room(p, end, sizeof(short)))
		return false;
	short settings_count = *reinterpret_cast<const short *>(p);
	p += sizeof(short);

	b->viewportSets.resize(settings_count);
	for (int s = 0; s < settings_count; s++)
	{
		auto &set = b->viewportSets[s];
		for (size_t v = 0; v < set.viewports.size(); v++)
		{
			if (!has_room(p, end, sizeof(xwi_brf_viewport_setting)))
				return false;
			auto raw = reinterpret_cast<const xwi_brf_viewport_setting *>(p);
			auto &vp = set.viewports[v];
			vp.top     = raw->top;
			vp.left    = raw->left;
			vp.bottom  = raw->bottom;
			vp.right   = raw->right;
			vp.visible = (raw->visible != 0);
			p += sizeof(xwi_brf_viewport_setting);
		}
	}

	// 5. Pages
	if (!has_room(p, end, sizeof(short)))
		return false;
	short page_count = *reinterpret_cast<const short *>(p);
	p += sizeof(short);

	b->pages.resize(page_count);
	for (int pg = 0; pg < page_count; pg++)
	{
		auto &page = b->pages[pg];

		// 8-byte page header: duration, eventsLength, coordinateSet, pageType
		if (!has_room(p, end, 4 * sizeof(short)))
			return false;
		auto page_hdr = reinterpret_cast<const short *>(p);
		page.duration       = page_hdr[0];
		short events_length = page_hdr[1];
		page.coordinateSet  = page_hdr[2];
		page.pageType       = page_hdr[3];
		p += 4 * sizeof(short);

		// Events: stream of shorts totalling events_length, decoded into XWMBrfEvent
		if (!has_room(p, end, events_length * sizeof(short)))
			return false;
		const char *events_end = p + events_length * sizeof(short);
		while (p < events_end)
		{
			if (!has_room(p, events_end, 2 * sizeof(short)))
				return false;
			auto words = reinterpret_cast<const short *>(p);
			short time = words[0];
			short opcode_raw = words[1];
			p += 2 * sizeof(short);

			XWMBrfEvent event;
			event.time = time;
			if (!decode_opcode(opcode_raw, event.opcode, event.paramCount))
				return false;

			if (!has_room(p, events_end, event.paramCount * sizeof(short)))
				return false;
			auto param_words = reinterpret_cast<const short *>(p);
			for (int i = 0; i < event.paramCount; i++)
				event.params[i] = param_words[i];
			for (int i = event.paramCount; i < (int)event.params.size(); i++)
				event.params[i] = 0;
			p += event.paramCount * sizeof(short);

			page.events.push_back(event);
		}
	}

	// 6. MissionHeader (redundant copy of .XWI header)
	if (!has_room(p, end, sizeof(xwi_brf_mission_header)))
		return false;
	// just skip the header; don't do anything with it
	p += sizeof(xwi_brf_mission_header);

	// 7. IconExtraData (unused, fixed 90 bytes per icon)
	const size_t extra_data_size = static_cast<size_t>(icon_count) * 90;
	if (!has_room(p, end, extra_data_size))
		return false;
	p += extra_data_size;

	// 8. Tags — section is prefixed with a short count.  The MikeG spec text only
	// documents the per-entry layout; the leading count is shown in the
	// reference implementation at MikeG621/Platform's xwing/Mission.cs.
	if (!has_room(p, end, sizeof(short)))
		return false;
	short tag_count = *reinterpret_cast<const short *>(p);
	p += sizeof(short);
	if (tag_count < 0)
		return false;

	b->tags.resize(tag_count);
	for (int t = 0; t < tag_count; t++)
	{
		if (!has_room(p, end, sizeof(short)))
			return false;
		short len = *reinterpret_cast<const short *>(p);
		p += sizeof(short);
		if (len < 0)
			return false;
		if (!has_room(p, end, static_cast<size_t>(len)))
			return false;
		b->tags[t].assign(p, len);
		p += len;
	}

	// 9. Strings — likewise prefixed with a short count.
	if (!has_room(p, end, sizeof(short)))
		return false;
	short string_count = *reinterpret_cast<const short *>(p);
	p += sizeof(short);
	if (string_count < 0)
		return false;

	b->strings.resize(string_count);
	for (int s = 0; s < string_count; s++)
	{
		if (!has_room(p, end, sizeof(short)))
			return false;
		short len = *reinterpret_cast<const short *>(p);
		p += sizeof(short);
		if (len < 0)
			return false;
		if (!has_room(p, end, static_cast<size_t>(len) * 2))
			return false;
		b->strings[s].text.assign(p, len);
		p += len;
		b->strings[s].highlight.assign(p, p + len);
		p += len;
	}

	return true;
}
