#ifndef _VITA_UI_FONT_H_
#define _VITA_UI_FONT_H_

#include <vita2d.h>

/* Small FreeType-backed text renderer for the pack browser.
 *
 * vita2d's font helper keeps one bitmap per glyph in a 512x512 atlas and
 * rescales it afterwards, so mixing the many point sizes this UI needs ends
 * up with blurry, clipped or missing glyphs. This cache keeps a separate
 * bitmap for every (glyph, size) pair instead. */
namespace UiFont
{
	class Font
	{
	public:
		struct Glyph
		{
			int iX, iY;        /* atlas rectangle */
			int iW, iH;
			int iLeft, iTop;   /* bitmap placement relative to the pen */
			int iAdvance;
			bool bValid;
		};

		Font();
		~Font();

		bool Load( const char *szPath );
		bool Ready() const { return m_bReady; }

		/* y is the text baseline. Returns the advanced width in pixels. */
		int Draw( int x, int y, unsigned iPx, unsigned iColor, const char *szText );
		int Width( unsigned iPx, const char *szText );

	private:
		const Glyph *GetGlyph( unsigned iPx, unsigned iIndex );

		void *m_pLib;
		void *m_pFace;
		vita2d_texture *m_pAtlas;
		unsigned m_uiStride;
		unsigned m_uiAtlasW;
		unsigned m_uiAtlasH;
		int m_iPenX;
		int m_iPenY;
		int m_iRowH;
		bool m_bReady;
		bool m_bFull;
		void *m_pGlyphs;
	};
}

#endif
