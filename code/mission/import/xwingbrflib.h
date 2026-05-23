#pragma once
#include <array>
#include <string>
#include <vector>
#include "xwinglib.h"

// Unified BRF icon type table per the X-Wing 95 format spec.  This is NOT the
// same as XWMFlightGroupType from the .XWI file: 0x12 here means Mine1 (not
// B-Wing), the BRF has a dedicated B-Wing icon at 0x19, and the table extends
// well past the flight-group range to include space objects, planets, and
// training platforms.  Values 0x32-0x39 are reserved/undefined in the spec.
enum class XWMBrfIconType : short
{
	bi_None                = 0x00,
	bi_X_Wing              = 0x01,
	bi_Y_Wing              = 0x02,
	bi_A_Wing              = 0x03,
	bi_TIE_Fighter         = 0x04,
	bi_TIE_Interceptor     = 0x05,
	bi_TIE_Bomber          = 0x06,
	bi_Assault_Gunboat     = 0x07,
	bi_Transport           = 0x08,
	bi_Shuttle             = 0x09,
	bi_Tug                 = 0x0A,
	bi_Container           = 0x0B,
	bi_Freighter           = 0x0C,
	bi_Calamari_Cruiser    = 0x0D,
	bi_Nebulon_B_Frigate   = 0x0E,
	bi_Corellian_Corvette  = 0x0F,
	bi_Star_Destroyer      = 0x10,
	bi_TIE_Advanced        = 0x11,
	bi_Mine1               = 0x12,
	bi_Mine2               = 0x13,
	bi_Mine3               = 0x14,
	bi_Mine4               = 0x15,
	bi_Satellite           = 0x16,
	bi_Nav_Buoy            = 0x17,
	bi_Probe               = 0x18,
	bi_B_Wing              = 0x19,
	bi_Asteroid1           = 0x1A,
	bi_Asteroid2           = 0x1B,
	bi_Asteroid3           = 0x1C,
	bi_Asteroid4           = 0x1D,
	bi_Asteroid5           = 0x1E,
	bi_Asteroid6           = 0x1F,
	bi_Asteroid7           = 0x20,
	bi_Asteroid8           = 0x21,
	bi_Rock_World          = 0x22,
	bi_Gray_Ring_World     = 0x23,
	bi_Gray_World          = 0x24,
	bi_Brown_World         = 0x25,
	bi_Gray_World2         = 0x26,
	bi_Planet_and_Moon     = 0x27,
	bi_Gray_Crescent       = 0x28,
	bi_Orange_Crescent1    = 0x29,
	bi_Orange_Crescent2    = 0x2A,
	bi_Orange_Crescent3    = 0x2B,
	bi_Orange_Crescent4    = 0x2C,
	bi_Orange_Crescent5    = 0x2D,
	bi_Orange_Crescent6    = 0x2E,
	bi_Orange_Crescent7    = 0x2F,
	bi_Orange_Crescent8    = 0x30,
	bi_Death_Star          = 0x31,
	bi_Training_Platform1  = 0x3A,
	bi_Training_Platform2  = 0x3B,
	bi_Training_Platform3  = 0x3C,
	bi_Training_Platform4  = 0x3D,
	bi_Training_Platform5  = 0x3E,
	bi_Training_Platform6  = 0x3F,
	bi_Training_Platform7  = 0x40,
	bi_Training_Platform8  = 0x41,
	bi_Training_Platform9  = 0x42,
	bi_Training_Platform10 = 0x43,
	bi_Training_Platform11 = 0x44,
	bi_Training_Platform12 = 0x45
};

enum class XWMBrfEventOpcode : short
{
	op_None          = 0x00,
	op_WaitForClick  = 0x01,
	op_ClearText     = 0x0A,
	op_TitleText     = 0x0B,
	op_CaptionText   = 0x0C,
	op_CaptionText2  = 0x0E,
	op_MoveMap       = 0x0F,
	op_ZoomMap       = 0x10,
	op_ClearFGTags   = 0x15,
	op_FGTag1        = 0x16,
	op_FGTag2        = 0x17,
	op_FGTag3        = 0x18,
	op_FGTag4        = 0x19,
	op_ClearTextTags = 0x1A,
	op_TextTag1      = 0x1B,
	op_TextTag2      = 0x1C,
	op_TextTag3      = 0x1D,
	op_TextTag4      = 0x1E,
	op_EndBriefing   = 0x29
};

class XWMBrfCoordinate
{
public:
	float x, y, z;
};

class XWMBrfIcon
{
public:
	std::string designation;
	std::string cargo;
	std::string specialCargo;

	XWMBrfIconType craftType;
	XWMCraftIFF craftIFF;

	int numberInWave;
	int numberOfWaves;
	int specialCargoCraft;

	short yaw;
	short pitch;
	short roll;
};

class XWMBrfViewport
{
public:
	short top;
	short left;
	short bottom;
	short right;
	bool visible;
};

// Five viewports per set in spec order: Title, Caption, Unused1, Unused2, Map
class XWMBrfViewportSet
{
public:
	std::array<XWMBrfViewport, 5> viewports;
};

class XWMBrfEvent
{
public:
	short time;
	XWMBrfEventOpcode opcode;
	std::array<short, 3> params;
	int paramCount;
};

class XWMBrfPage
{
public:
	short duration;       // in ticks; 8 ticks per second
	short coordinateSet;  // index into XWingBriefing::coordinateSets
	short pageType;       // index into XWingBriefing::viewportSets
	std::vector<XWMBrfEvent> events;
};

class XWMBrfString
{
public:
	std::string text;
	std::vector<char> highlight;  // parallel to text; 1 = highlighted, 0 = not
};

class XWingBriefing
{
public:
	static bool load(XWingBriefing *b, const char *data, size_t length);

	std::vector<std::vector<XWMBrfCoordinate>> coordinateSets;  // [set][icon]
	std::vector<XWMBrfIcon>                    icons;
	std::vector<XWMBrfViewportSet>             viewportSets;
	std::vector<XWMBrfPage>                    pages;
	std::vector<std::string>                   tags;
	std::vector<XWMBrfString>                  strings;
};
