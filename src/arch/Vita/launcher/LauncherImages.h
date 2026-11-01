#ifndef VITA_LAUNCHER_IMAGES_H
#define VITA_LAUNCHER_IMAGES_H

#include <vita2d.h>
#include <cstddef>
#include <vector>

/* Image helpers for the pack browser. The launcher has no engine image
 * loader, so PNG goes through libpng and JPEG through libjpeg (both are
 * already linked into the game). */
namespace LauncherImages
{
	/* Decoded pixels, top-down, four bytes per pixel in R,G,B,A order. */
	struct RawImage
	{
		int iW;
		int iH;
		std::vector<unsigned char> vRgba;

		RawImage() : iW( 0 ), iH( 0 ) {}
	};

	/* RawImage copies are expensive (about 1 MB at 512 px); hand them over
	 * with std::move. */
	RawImage &NoCopy( RawImage &img );

	/* app0:/ui/*.png style assets. Returns NULL when the file is missing. */
	vita2d_texture *LoadPngFile( const char *szPath );

	/* CPU-only decode, safe to call from a worker thread. iMaxDim limits the
	 * decoded size (0 keeps the original). */
	bool DecodeToRgba( const void *pData, size_t iSize, int iMaxDim, RawImage &out );

	/* Uploads a RawImage. Must run on the thread that owns the GXM context. */
	vita2d_texture *CreateTexture( const RawImage &img );

	/* Decodes a PNG or JPEG held in memory. iMaxDim limits the decoded size
	 * (0 keeps the original); JPEG uses libjpeg's DCT scaling so a big cover
	 * never lands in VRAM at full resolution. */
	vita2d_texture *Decode( const void *pData, size_t iSize, int iMaxDim );
}

#endif
