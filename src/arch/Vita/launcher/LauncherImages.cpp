#include "LauncherImages.h"
#include "LauncherUtil.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csetjmp>
#include <jpeglib.h>
#include <png.h>

namespace
{
	struct JpegErrorManager
	{
		jpeg_error_mgr pub;
		jmp_buf jb;
	};

	/* The build links the bundled libjpeg (6b), which predates jpeg_mem_src;
	 * a tiny memory source manager keeps decoding straight from RAM. */
	struct JpegMemSource
	{
		jpeg_source_mgr pub;
		const unsigned char *pData;
		size_t iSize;
		JOCTET eoi[2];
	};

	void JpegMemInit( j_decompress_ptr cinfo )
	{
		(void)cinfo;
	}

	boolean JpegMemFill( j_decompress_ptr cinfo )
	{
		JpegMemSource *pSrc = (JpegMemSource *)cinfo->src;
		pSrc->eoi[0] = 0xD9;
		pSrc->eoi[1] = 0;
		pSrc->pub.next_input_byte = pSrc->eoi;
		pSrc->pub.bytes_in_buffer = 2;
		return TRUE;
	}

	void JpegMemSkip( j_decompress_ptr cinfo, long iBytes )
	{
		JpegMemSource *pSrc = (JpegMemSource *)cinfo->src;
		if( iBytes <= 0 )
			return;
		const size_t iLeft = pSrc->pub.bytes_in_buffer;
		if( (size_t)iBytes > iLeft )
			pSrc->pub.bytes_in_buffer = 0;
		else
		{
			pSrc->pub.next_input_byte += iBytes;
			pSrc->pub.bytes_in_buffer -= (size_t)iBytes;
		}
	}

	void JpegMemTerm( j_decompress_ptr cinfo )
	{
		(void)cinfo;
	}

	void JpegSetMemSource( j_decompress_ptr cinfo, JpegMemSource *pSrc,
		const unsigned char *pData, size_t iSize )
	{
		pSrc->pub.init_source = JpegMemInit;
		pSrc->pub.fill_input_buffer = JpegMemFill;
		pSrc->pub.skip_input_data = JpegMemSkip;
		pSrc->pub.resync_to_restart = jpeg_resync_to_restart;
		pSrc->pub.term_source = JpegMemTerm;
		pSrc->pub.next_input_byte = pData;
		pSrc->pub.bytes_in_buffer = iSize;
		pSrc->pData = pData;
		pSrc->iSize = iSize;
		cinfo->src = (jpeg_source_mgr *)pSrc;
	}

	void OnJpegError( j_common_ptr cinfo )
	{
		JpegErrorManager *pErr = (JpegErrorManager *)cinfo->err;
		longjmp( pErr->jb, 1 );
	}

	bool StartsWith( const unsigned char *pData, size_t iSize, const char *szMagic, size_t iLen )
	{
		if( iSize < iLen )
			return false;
		return memcmp( pData, szMagic, iLen ) == 0;
	}

	/* Box-filtered downscale in RAM. Runs fine on the loader thread. */
	void ShrinkRgba( LauncherImages::RawImage &img, int iMaxDim )
	{
		if( iMaxDim <= 0 || img.iW <= 0 || img.iH <= 0 )
			return;
		if( img.iW <= iMaxDim && img.iH <= iMaxDim )
			return;

		const float fScale = (float)iMaxDim / (float)( img.iW > img.iH ? img.iW : img.iH );
		int iOutW = (int)( (float)img.iW * fScale );
		int iOutH = (int)( (float)img.iH * fScale );
		if( iOutW < 1 ) iOutW = 1;
		if( iOutH < 1 ) iOutH = 1;

		std::vector<unsigned char> vOut( (size_t)iOutW * (size_t)iOutH * 4 );
		for( int y = 0; y < iOutH; ++y )
		{
			const int iY0 = (int)( (long long)y * img.iH / iOutH );
			int iY1 = (int)( (long long)( y + 1 ) * img.iH / iOutH );
			if( iY1 <= iY0 ) iY1 = iY0 + 1;
			for( int x = 0; x < iOutW; ++x )
			{
				const int iX0 = (int)( (long long)x * img.iW / iOutW );
				int iX1 = (int)( (long long)( x + 1 ) * img.iW / iOutW );
				if( iX1 <= iX0 ) iX1 = iX0 + 1;

				unsigned iSum[4] = { 0, 0, 0, 0 };
				unsigned iCount = 0;
				for( int sy = iY0; sy < iY1; ++sy )
				{
					const unsigned char *pRow = &img.vRgba[(size_t)sy * (size_t)img.iW * 4];
					for( int sx = iX0; sx < iX1; ++sx )
					{
						const unsigned char *pPx = pRow + (size_t)sx * 4;
						iSum[0] += pPx[0];
						iSum[1] += pPx[1];
						iSum[2] += pPx[2];
						iSum[3] += pPx[3];
						++iCount;
					}
				}
				if( iCount == 0 )
					iCount = 1;
				unsigned char *pDst = &vOut[ ( (size_t)y * (size_t)iOutW + (size_t)x ) * 4 ];
				pDst[0] = (unsigned char)( iSum[0] / iCount );
				pDst[1] = (unsigned char)( iSum[1] / iCount );
				pDst[2] = (unsigned char)( iSum[2] / iCount );
				pDst[3] = (unsigned char)( iSum[3] / iCount );
			}
		}

		img.vRgba.swap( vOut );
		img.iW = iOutW;
		img.iH = iOutH;
	}

	bool DecodePngRgba( const unsigned char *pData, size_t iSize,
		LauncherImages::RawImage &out )
	{
		png_image image;
		memset( &image, 0, sizeof(image) );
		image.version = PNG_IMAGE_VERSION;
		if( !png_image_begin_read_from_memory( &image, pData, iSize ) )
			return false;

		image.format = PNG_FORMAT_RGBA;
		out.iW = (int)image.width;
		out.iH = (int)image.height;
		out.vRgba.resize( (size_t)image.width * (size_t)image.height * 4 );
		if( !png_image_finish_read( &image, NULL, &out.vRgba[0], 0, NULL ) )
		{
			png_image_free( &image );
			out.vRgba.clear();
			out.iW = out.iH = 0;
			return false;
		}
		png_image_free( &image );
		return true;
	}

	bool DecodeJpegRgba( const unsigned char *pData, size_t iSize, int iMaxDim,
		LauncherImages::RawImage &out )
	{
		jpeg_decompress_struct cinfo;
		JpegErrorManager jerr;
		JpegMemSource src;

		memset( &cinfo, 0, sizeof(cinfo) );
		memset( &src, 0, sizeof(src) );
		cinfo.err = jpeg_std_error( &jerr.pub );
		jerr.pub.error_exit = OnJpegError;
		if( setjmp( jerr.jb ) != 0 )
		{
			jpeg_destroy_decompress( &cinfo );
			return false;
		}

		jpeg_create_decompress( &cinfo );
		JpegSetMemSource( &cinfo, &src, pData, iSize );
		jpeg_read_header( &cinfo, TRUE );
		cinfo.out_color_space = JCS_RGB;

		/* 1/2, 1/4 ... scaling keeps a 1500px cover from eating VRAM. */
		if( iMaxDim > 0 )
		{
			while( cinfo.scale_denom < 8 &&
				( (int)cinfo.image_width / (int)cinfo.scale_denom > iMaxDim * 2 ||
				  (int)cinfo.image_height / (int)cinfo.scale_denom > iMaxDim * 2 ) )
				cinfo.scale_denom *= 2;
		}

		jpeg_start_decompress( &cinfo );

		const int iW = (int)cinfo.output_width;
		const int iH = (int)cinfo.output_height;
		const int iComp = cinfo.output_components;
		if( iW <= 0 || iH <= 0 || iComp < 3 )
		{
			jpeg_destroy_decompress( &cinfo );
			return false;
		}

		out.iW = iW;
		out.iH = iH;
		out.vRgba.resize( (size_t)iW * (size_t)iH * 4 );

		JSAMPARRAY pRow = (*cinfo.mem->alloc_sarray)( (j_common_ptr)&cinfo, JPOOL_IMAGE,
			(JDIMENSION)( iW * iComp ), 1 );
		int y = 0;
		while( cinfo.output_scanline < cinfo.output_height )
		{
			jpeg_read_scanlines( &cinfo, pRow, 1 );
			const unsigned char *pSrc = pRow[0];
			unsigned char *pOut = &out.vRgba[(size_t)y * (size_t)iW * 4];
			for( int x = 0; x < iW; ++x )
			{
				pOut[0] = pSrc[0];
				pOut[1] = pSrc[1];
				pOut[2] = pSrc[2];
				pOut[3] = 0xFF;
				pSrc += iComp;
				pOut += 4;
			}
			++y;
		}

		jpeg_finish_decompress( &cinfo );
		jpeg_destroy_decompress( &cinfo );
		return true;
	}
}

namespace LauncherImages
{
	vita2d_texture *LoadPngFile( const char *szPath )
	{
		if( szPath == NULL )
			return NULL;
		vita2d_texture *pTex = vita2d_load_PNG_file( szPath );
		if( pTex == NULL )
			LauncherUtil::Log( "images: could not load %s", szPath );
		return pTex;
	}

	bool DecodeToRgba( const void *pData, size_t iSize, int iMaxDim, RawImage &out )
	{
		out = RawImage();
		if( pData == NULL || iSize < 8 )
			return false;

		const unsigned char *pBytes = (const unsigned char *)pData;
		bool bOk = false;
		if( StartsWith( pBytes, iSize, "\xFF\xD8\xFF", 3 ) )
			bOk = DecodeJpegRgba( pBytes, iSize, iMaxDim, out );
		else if( StartsWith( pBytes, iSize, "\x89PNG\r\n\x1a\n", 8 ) )
			bOk = DecodePngRgba( pBytes, iSize, out );

		if( !bOk )
		{
			out = RawImage();
			return false;
		}

		/* PNG has no cheap 1/2, 1/4 scaling here, so it is decoded at full
		 * size and then box-filtered down. */
		ShrinkRgba( out, iMaxDim );
		return true;
	}

	vita2d_texture *CreateTexture( const RawImage &img )
	{
		if( img.iW <= 0 || img.iH <= 0 || img.vRgba.empty() )
			return NULL;

		vita2d_texture *pTex = vita2d_create_empty_texture_format( (unsigned)img.iW,
			(unsigned)img.iH, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR );
		if( pTex == NULL )
			return NULL;

		unsigned char *pDst = (unsigned char *)vita2d_texture_get_datap( pTex );
		const unsigned iStride = vita2d_texture_get_stride( pTex );
		const unsigned iRow = (unsigned)img.iW * 4;
		const unsigned char *pSrc = &img.vRgba[0];
		for( int y = 0; y < img.iH; ++y )
		{
			memcpy( pDst, pSrc, iRow );
			if( iStride > iRow )
				memset( pDst + iRow, 0xFF, iStride - iRow );
			pDst += iStride;
			pSrc += iRow;
		}
		return pTex;
	}

	vita2d_texture *Decode( const void *pData, size_t iSize, int iMaxDim )
	{
		RawImage img;
		if( !DecodeToRgba( pData, iSize, iMaxDim, img ) )
			return NULL;
		return CreateTexture( img );
	}
}
