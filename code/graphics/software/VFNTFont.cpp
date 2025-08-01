
#include "graphics/software/VFNTFont.h"
#include "graphics/software/font_internal.h"

namespace font
{

	VFNTFont::VFNTFont(font *fnt) : FSFont()
	{
		Assertion(fnt != NULL, "Invalid font passed to constructor of VFNTFont!");

		this->fontPtr = fnt;

		setName(SCP_string(fnt->filename));
	}

	VFNTFont::~VFNTFont()
	{
	}

	FontType VFNTFont::getType() const
	{
		return VFNT_FONT;
	}

	float VFNTFont::getTextHeight() const
	{
		return i2fl(fontPtr->h);
	}

	font *VFNTFont::getFontData()
	{
		return this->fontPtr;
	}

	const SCP_string& VFNTFont::getFamilyName() const
	{
		static const SCP_string volitionFontName = "Volition Font";
		return volitionFontName;
	}

	extern int get_char_width_old(font* fnt, ubyte c1, ubyte c2, int *width, int* spacing);
	void VFNTFont::getStringSize(const char *text, size_t textSize, int /* resize_mode */, float *w1, float *h1, float scaleMultiplier) const
	{
		int longest_width;
		int width, spacing;
		int w, h;

		w = 0;
		h = text == nullptr ? 0 : fl2i(this->getHeight());
		longest_width = 0;

		// Maintain old behavior where textLen was an int
		int textLen = static_cast<int>(textSize);

		bool checkLength = textLen >= 0;

		if (text != nullptr && textLen != 0)
		{
			while (*text)
			{
				// Process one or more 
				while (*text == '\n')
				{
					text++;

					if (checkLength)
					{
						textLen--;
						if (textLen <= 0)
							break;
					}

					if (*text)
					{
						h += fl2i(this->getHeight());
					}

					w = 0;
				}

				if (*text == 0)
				{
					break;
				}

				get_char_width_old(fontPtr, text[0], text[1], &width, &spacing);
				w += spacing;
				if (w > longest_width)
					longest_width = w;

				text++;

				if (checkLength)
				{
					textLen--;
					if (textLen <= 0)
						break;
				}
			}
		}

		float scale_factor = (canScale && !Fred_running) ? get_font_scale_factor() : 1.0f;
		scale_factor *= scaleMultiplier;

		if (h1)
			*h1 = i2fl(h) * scale_factor;

		if (w1)
			*w1 = i2fl(longest_width) * scale_factor;
	}

	font::font() : kern_data(NULL),
		char_data(NULL),
		pixel_data(NULL),
		bm_data(NULL),
		bm_u(NULL),
		bm_v(NULL)
	{
	}

	font::~font()
	{
		if (this->kern_data) {
			delete [] this->kern_data;
			this->kern_data = NULL;
		}

		if (this->char_data) {
			delete [] this->char_data;
			this->char_data = NULL;
		}

		if (this->pixel_data) {
			delete [] this->pixel_data;
			this->pixel_data = NULL;
		}

		if (this->bm_data) {
			delete [] this->bm_data;
			this->bm_data = NULL;
		}

		if (this->bm_u) {
			delete [] this->bm_u;
			this->bm_u = NULL;
		}

		if (this->bm_v) {
			delete [] this->bm_v;
			this->bm_v = NULL;
		}
	}
}
