#include "UiFont.h"
#include "LauncherUtil.h"

#include <freetype2/ft2build.h>
#include FT_FREETYPE_H

#include <map>
#include <cstring>

namespace
{
	const unsigned ATLAS_W = 1024;
	const unsigned ATLAS_H = 1024;

	/* Minimal UTF-8 decoder; the indexes are plain ASCII, but pack names can
	 * carry accented Latin. */
	unsigned ReadUtf8( const char *&pText )
	{
		const unsigned char c = (unsigned char)*pText;
		if( c < 0x80 )
		{
			++pText;
			return c;
		}

		int iLen;
		unsigned iCode;
		if( ( c & 0xE0 ) == 0xC0 )
		{
			iLen = 2;
			iCode = c & 0x1F;
		}
		else if( ( c & 0xF0 ) == 0xE0 )
		{
			iLen = 3;
			iCode = c & 0x0F;
		}
		else if( ( c & 0xF8 ) == 0xF0 )
		{
			iLen = 4;
			iCode = c & 0x07;
		}
		else
		{
			++pText;
			return '?';
		}

		++pText;
		for( int i = 1; i < iLen; ++i )
		{
			const unsigned char cc = (unsigned char)*pText;
			if( ( cc & 0xC0 ) != 0x80 )
				return iCode;
			iCode = ( iCode << 6 ) | ( cc & 0x3F );
			++pText;
		}
		return iCode;
	}
}

namespace UiFont
{
	typedef std::map<unsigned long long, Font::Glyph> GlyphMap;

	Font::Font() : m_pLib( NULL ), m_pFace( NULL ), m_pAtlas( NULL ),
		m_uiStride( 0 ), m_uiAtlasW( ATLAS_W ), m_uiAtlasH( ATLAS_H ),
		m_iPenX( 1 ), m_iPenY( 1 ), m_iRowH( 0 ), m_bReady( false ),
		m_bFull( false ), m_pGlyphs( NULL )
	{
	}

	Font::~Font()
	{
		if( m_pAtlas != NULL )
			vita2d_free_texture( m_pAtlas );
		if( m_pFace != NULL )
			FT_Done_Face( (FT_Face)m_pFace );
		if( m_pLib != NULL )
			FT_Done_FreeType( (FT_Library)m_pLib );
		delete (GlyphMap *)m_pGlyphs;
	}

	bool Font::Load( const char *szPath )
	{
		if( szPath == NULL )
			return false;

		FT_Library pLib = NULL;
		if( FT_Init_FreeType( &pLib ) != 0 )
			return false;

		FT_Face pFace = NULL;
		if( FT_New_Face( pLib, szPath, 0, &pFace ) != 0 )
		{
			FT_Done_FreeType( pLib );
			return false;
		}

		FT_Select_Charmap( pFace, FT_ENCODING_UNICODE );

		m_pAtlas = vita2d_create_empty_texture_format( ATLAS_W, ATLAS_H,
			SCE_GXM_TEXTURE_FORMAT_U8_R111 );
		if( m_pAtlas == NULL )
		{
			FT_Done_Face( pFace );
			FT_Done_FreeType( pLib );
			return false;
		}

		/* Glyphs are blitted 1:1 from the atlas, so point sampling keeps the
		 * edges hard; linear blending would smear the subpixel coverage. */
		vita2d_texture_set_filters( m_pAtlas, SCE_GXM_TEXTURE_FILTER_POINT,
			SCE_GXM_TEXTURE_FILTER_POINT );

		m_uiStride = vita2d_texture_get_stride( m_pAtlas );
		unsigned char *pData = (unsigned char *)vita2d_texture_get_datap( m_pAtlas );
		if( pData != NULL )
			memset( pData, 0, (size_t)m_uiStride * ATLAS_H );

		m_pLib = pLib;
		m_pFace = pFace;
		m_pGlyphs = new GlyphMap();
		m_bReady = true;
		return true;
	}

	const Font::Glyph *Font::GetGlyph( unsigned iPx, unsigned iIndex )
	{
		if( !m_bReady )
			return NULL;

		GlyphMap &glyphs = *(GlyphMap *)m_pGlyphs;
		const unsigned long long uiKey = ( (unsigned long long)iPx << 32 ) | iIndex;
		GlyphMap::iterator it = glyphs.find( uiKey );
		if( it != glyphs.end() )
			return it->second.bValid ? &it->second : NULL;

		FT_Face pFace = (FT_Face)m_pFace;
		Glyph g;
		memset( &g, 0, sizeof(g) );

		if( FT_Set_Pixel_Sizes( pFace, 0, iPx ) != 0 ||
			FT_Load_Glyph( pFace, iIndex, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL ) != 0 )
		{
			glyphs[uiKey] = g;
			return NULL;
		}

		FT_GlyphSlot pSlot = pFace->glyph;
		const FT_Bitmap &bitmap = pSlot->bitmap;
		g.iAdvance = (int)( pSlot->advance.x >> 6 );
		g.bValid = true;

		if( bitmap.width > 0 && bitmap.rows > 0 )
		{
			const int iW = (int)bitmap.width;
			const int iH = (int)bitmap.rows;

			if( m_iPenX + iW + 1 > (int)m_uiAtlasW )
			{
				m_iPenX = 1;
				m_iPenY += m_iRowH + 1;
				m_iRowH = 0;
			}
			if( m_iPenY + iH + 1 > (int)m_uiAtlasH )
			{
				if( !m_bFull )
				{
					m_bFull = true;
					LauncherUtil::Log( "font: glyph atlas full at %upx", iPx );
				}
				g.bValid = false;
				glyphs[uiKey] = g;
				return NULL;
			}

			unsigned char *pData = (unsigned char *)vita2d_texture_get_datap( m_pAtlas );
			for( int iRow = 0; iRow < iH; ++iRow )
			{
				const unsigned char *pSrc = bitmap.buffer + (size_t)iRow * bitmap.pitch;
				unsigned char *pDst = pData + (size_t)( m_iPenY + iRow ) * m_uiStride + m_iPenX;
				if( bitmap.pixel_mode == FT_PIXEL_MODE_MONO )
				{
					for( int iCol = 0; iCol < iW; ++iCol )
						pDst[iCol] = ( pSrc[iCol / 8] & ( 0x80 >> ( iCol % 8 ) ) ) ? 0xFF : 0x00;
				}
				else if( bitmap.pixel_mode == FT_PIXEL_MODE_GRAY )
					memcpy( pDst, pSrc, (size_t)iW );
				else
					memset( pDst, 0xFF, (size_t)iW );
			}

			g.iX = m_iPenX;
			g.iY = m_iPenY;
			g.iW = iW;
			g.iH = iH;
			g.iLeft = pSlot->bitmap_left;
			g.iTop = pSlot->bitmap_top;

			m_iPenX += iW + 1;
			if( iH > m_iRowH )
				m_iRowH = iH;
		}

		glyphs[uiKey] = g;
		return g.bValid ? &glyphs[uiKey] : NULL;
	}

	int Font::Draw( int x, int y, unsigned iPx, unsigned iColor, const char *szText )
	{
		if( !m_bReady || szText == NULL )
			return 0;

		FT_Face pFace = (FT_Face)m_pFace;
		int iPen = x;
		const char *pText = szText;
		while( *pText != '\0' )
		{
			const unsigned iChar = ReadUtf8( pText );
			const unsigned iIndex = FT_Get_Char_Index( pFace, iChar );
			const Glyph *pGlyph = GetGlyph( iPx, iIndex );
			if( pGlyph == NULL )
				continue;

			if( pGlyph->iW > 0 && pGlyph->iH > 0 )
			{
				vita2d_draw_texture_tint_part_scale( m_pAtlas,
					(float)( iPen + pGlyph->iLeft ), (float)( y - pGlyph->iTop ),
					(float)pGlyph->iX, (float)pGlyph->iY,
					(float)pGlyph->iW, (float)pGlyph->iH,
					1.0f, 1.0f, iColor );
			}
			iPen += pGlyph->iAdvance;
		}
		return iPen - x;
	}

	int Font::Width( unsigned iPx, const char *szText )
	{
		if( !m_bReady || szText == NULL )
			return 0;

		FT_Face pFace = (FT_Face)m_pFace;
		int iPen = 0;
		const char *pText = szText;
		while( *pText != '\0' )
		{
			const unsigned iChar = ReadUtf8( pText );
			const unsigned iIndex = FT_Get_Char_Index( pFace, iChar );
			const Glyph *pGlyph = GetGlyph( iPx, iIndex );
			if( pGlyph == NULL )
				continue;
			iPen += pGlyph->iAdvance;
		}
		return iPen;
	}
}
