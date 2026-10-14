#include "global.h"
#include "RageDisplay_Vita.h"
#include "VitaBootSplash.h"
#include "vita_bootstages.h"
#include "vita_bootfont.h"
#include "RageDisplay.h"
#include "RageLog.h"
#include "RageSurface.h"
#include "RageSurfaceUtils.h"
#include "RageMath.h"
#include "RageTypes.h"
#include "RageUtil.h"
#include "DisplayResolutions.h"
#include "ModelTypes.h"
#include "arch/LowLevelWindow/LowLevelWindow.h"

#include <cstring>
#include <cstddef>
#include <ctime>
#include <vector>

#ifdef VITA
#if __has_include(<vitaGL.h>)
#include <vitaGL.h>
#endif
#include <psp2/display.h>
#include <psp2/io/fcntl.h>
#endif
#if defined(VITA) && __has_include("vita_build_number.h")
#include "vita_build_number.h"
#endif

namespace
{
	RageDisplay::RagePixelFormatDesc
	PIXEL_FORMAT_DESC_VITA[NUM_RagePixelFormat] = {
		{ 32, { 0xFF000000, 0x00FF0000, 0x0000FF00, 0x000000FF } }, // R8G8B8A8
		{ 32, { 0x0000FF00, 0x00FF0000, 0xFF000000, 0x000000FF } }, // B8G8R8A8
		{ 16, { 0xF000, 0x0F00, 0x00F0, 0x000F } },                // R4G4B4A4
		{ 16, { 0xF800, 0x07C0, 0x003E, 0x0001 } },                // R5G5B5A1
		{ 16, { 0xF800, 0x07C0, 0x003E, 0x0000 } },                // R5G5B5X1
		{ 24, { 0xFF0000, 0x00FF00, 0x0000FF, 0x000000 } },         // R8G8B8
		{ 8, { 0, 0, 0, 0 } },                                    // Paletted
		{ 24, { 0x0000FF, 0x00FF00, 0xFF0000, 0x000000 } },         // B8G8R8
		{ 16, { 0x7C00, 0x03E0, 0x001F, 0x8000 } },                // A1R5G5B5
		{ 16, { 0x7C00, 0x03E0, 0x001F, 0x0000 } },                // X1R5G5B5
	};

	// All uploads use GL_RGBA/GL_UNSIGNED_BYTE, i.e. raw R,G,B,A byte order
	// in memory. On little-endian ARM that means R in the low byte, which
	// is Swap32() of the RageSurface-style masks, matching upstream
	// RageDisplay_GLES2::FixLittleEndian(). (Uploading with the unflipped
	// masks swaps R<->A and G<->B: transparent-white becomes opaque cyan,
	// text edges lose their alpha, etc.)
	const uint32_t VITA_TEX_RMASK = 0x000000FF;
	const uint32_t VITA_TEX_GMASK = 0x0000FF00;
	const uint32_t VITA_TEX_BMASK = 0x00FF0000;
	const uint32_t VITA_TEX_AMASK = 0xFF000000;

	struct VitaCompiledGeometry : public RageCompiledGeometry
	{
		struct Mesh
		{
			vector<float> pos;
			vector<float> norm;
			vector<float> tex;
			vector<uint16_t> idx;
		};
		vector<Mesh> meshes;

		void Allocate(const vector<msMesh> &vMeshes)
		{
			meshes.resize(vMeshes.size());
			Change(vMeshes);
		}
		void Change(const vector<msMesh> &vMeshes)
		{
			for (size_t m = 0; m < vMeshes.size() && m < meshes.size(); ++m)
			{
				const msMesh &src = vMeshes[m];
				Mesh &dst = meshes[m];
				dst.pos.resize(src.Vertices.size() * 3);
				dst.norm.resize(src.Vertices.size() * 3);
				dst.tex.resize(src.Vertices.size() * 2);
				for (size_t i = 0; i < src.Vertices.size(); ++i)
				{
					dst.pos[i * 3 + 0] = src.Vertices[i].p[0];
					dst.pos[i * 3 + 1] = src.Vertices[i].p[1];
					dst.pos[i * 3 + 2] = src.Vertices[i].p[2];
					dst.norm[i * 3 + 0] = src.Vertices[i].n[0];
					dst.norm[i * 3 + 1] = src.Vertices[i].n[1];
					dst.norm[i * 3 + 2] = src.Vertices[i].n[2];
					dst.tex[i * 2 + 0] = src.Vertices[i].t[0];
					dst.tex[i * 2 + 1] = src.Vertices[i].t[1];
				}
				dst.idx.resize(src.Triangles.size() * 3);
				for (size_t i = 0; i < src.Triangles.size(); ++i)
				{
					dst.idx[i * 3 + 0] = src.Triangles[i].nVertexIndices[0];
					dst.idx[i * 3 + 1] = src.Triangles[i].nVertexIndices[1];
					dst.idx[i * 3 + 2] = src.Triangles[i].nVertexIndices[2];
				}
			}
		}
		void Draw(int iMeshIndex) const
		{
			// Driven by RageDisplay_Vita::DrawCompiledGeometryInternal.
			(void)iMeshIndex;
		}
	};
}

RageDisplay_Vita::RageDisplay_Vita()
	: m_pWind(NULL), m_bZWrite(false), m_bZTest(false),
	m_DrawCalls(0), m_DrawTris(0)
{
}

RageDisplay_Vita::~RageDisplay_Vita()
{
	delete m_pWind;
}

RString RageDisplay_Vita::Init(const VideoModeParams &p, bool bAllowUnacceleratedRenderer)
{
	(void)bAllowUnacceleratedRenderer;
	m_pWind = LowLevelWindow::Create();

	bool bNewDevice = false;
	RString err = m_pWind->TryVideoMode(p, bNewDevice);
	if (!err.empty())
		return err;

	glDisable(GL_LIGHTING);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
#if !defined(VITA)
	/* GLES2 has no alpha test; vitaGL reports GL_INVALID_ENUM (0x500) for
	 * it. Cutout textures are handled by alpha blending instead. */
	glDisable(GL_ALPHA_TEST);
#endif
	glEnable(GL_BLEND);
	glEnable(GL_TEXTURE_2D);
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glEnableClientState(GL_NORMAL_ARRAY);
	glClearColor(0, 0, 0, 0);

	LOG->Info("RageDisplay_Vita: initialized (vitaGL fixed-function)");
	return RString();
}

RString RageDisplay_Vita::TryVideoMode(const VideoModeParams &p, bool &bNewDeviceOut)
{
	if (m_pWind == NULL)
		m_pWind = LowLevelWindow::Create();
	return m_pWind->TryVideoMode(p, bNewDeviceOut);
}

void RageDisplay_Vita::GetDisplayResolutions(DisplayResolutions &out) const
{
	DisplayResolution res;
	res.iWidth = 960;
	res.iHeight = 544;
	res.bStretched = false;
	out.insert(res);
}

const RageDisplay::RagePixelFormatDesc *RageDisplay_Vita::GetPixelFormatDesc(RagePixelFormat pf) const
{
	ASSERT(pf < NUM_RagePixelFormat);
	return &PIXEL_FORMAT_DESC_VITA[pf];
}

VideoModeParams RageDisplay_Vita::GetActualVideoModeParams() const
{
	return m_pWind->GetActualVideoModeParams();
}

bool RageDisplay_Vita::BeginFrame()
{
	VideoModeParams avmp = m_pWind->GetActualVideoModeParams();
	glViewport(0, 0, avmp.width, avmp.height);
	glClearColor(0, 0, 0, 0);
	SetZWrite(true);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	return RageDisplay::BeginFrame();
}

void RageDisplay_Vita::EndFrame()
{
	glFlush();
	FrameLimitBeforeVsync(m_pWind->GetActualVideoModeParams().rate);
	m_pWind->SwapBuffers();
	FrameLimitAfterVsync();
	m_pWind->Update();

#if defined(VITA)
	{
		static int s_nFrames = 0;
		static clock_t s_last = 0;
		++s_nFrames;
		if (s_nFrames == 1 || s_nFrames % 600 == 0)
		{
			/* Avg wall ms per presented frame over the last window: honest
			 * before/after metric for perf work (draws/tris alone lie). */
			clock_t now = clock();
			int ms = 0;
			if (s_last != 0)
				ms = (int)((now - s_last) * 1000 / CLOCKS_PER_SEC / 600);
			s_last = now;
			GLenum e = glGetError();
#ifdef VITA_BUILD_NUMBER
			LOG->Info("Vita: build %d frame %d, %d draws %d tris %dms, glGetError=0x%x",
				VITA_BUILD_NUMBER, s_nFrames, m_DrawCalls / 600, m_DrawTris / 600, ms, (unsigned)e);
#else
			LOG->Info("Vita: frame %d, %d draws %d tris %dms, glGetError=0x%x",
				s_nFrames, m_DrawCalls / 600, m_DrawTris / 600, ms, (unsigned)e);
#endif
			m_DrawCalls = 0;
			m_DrawTris = 0;
		}
	}
#endif

	RageDisplay::EndFrame();
}

void RageDisplay_Vita::SendCurrentMatrices()
{
	RageMatrix projection;
	RageMatrixMultiply(&projection, GetCentering(), GetProjectionTop());
	glMatrixMode(GL_PROJECTION);
	glLoadMatrixf((const float *)&projection);

	RageMatrix modelView;
	RageMatrixMultiply(&modelView, GetViewTop(), GetWorldTop());
	glMatrixMode(GL_MODELVIEW);
	glLoadMatrixf((const float *)&modelView);

	glMatrixMode(GL_TEXTURE);
	glLoadMatrixf((const float *)GetTextureTop());
}

bool RageDisplay_Vita::SupportsTextureFormat(RagePixelFormat /*pixfmt*/, bool /*realtime*/)
{
	// Everything is normalized to RGBA8888 on upload.
	return true;
}

unsigned RageDisplay_Vita::CreateTexture(
	RagePixelFormat /*pixfmt*/,
	RageSurface *img,
	bool /*bGenerateMipMaps*/)
{
	if (img == NULL || img->pixels == NULL)
		return 0;

	RageSurface *conv = CreateSurface(img->w, img->h, 32,
		VITA_TEX_RMASK, VITA_TEX_GMASK, VITA_TEX_BMASK, VITA_TEX_AMASK);
	RageSurfaceUtils::Blit(img, conv, img->w, img->h);

	GLuint tex = 0;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, conv->w, conv->h, 0,
		GL_RGBA, GL_UNSIGNED_BYTE, conv->pixels);
	{
		/* Pinpoint texture-upload GL errors (e.g. the intermittent
		 * GL_INVALID_ENUM seen on busy screens) with dimensions. */
		GLenum e = glGetError();
		if( e != GL_NO_ERROR )
			LOG->Info("RageDisplay_Vita: CreateTexture %dx%d glGetError=0x%x",
				conv->w, conv->h, (unsigned)e);
	}
	delete conv;

	m_Textures.insert(tex);
	return tex;
}

void RageDisplay_Vita::UpdateTexture(
	unsigned iTexHandle,
	RageSurface *img,
	int xoffset, int yoffset, int width, int height)
{
	if (iTexHandle == 0 || img == NULL || img->pixels == NULL)
		return;

	// Clamp the subrect to the source surface.
	if (xoffset < 0) { width += xoffset; xoffset = 0; }
	if (yoffset < 0) { height += yoffset; yoffset = 0; }
	if (xoffset + width > img->w) width = img->w - xoffset;
	if (yoffset + height > img->h) height = img->h - yoffset;
	if (width <= 0 || height <= 0)
		return;

	// Fast path: source already matches the GL_RGBA/UBYTE upload layout,
	// so skip the full-surface per-pixel Blit (steady-state cost of dynamic
	// font atlases and movie frames).
	RageSurface *conv = NULL;
	const RageSurface *srcSurf = img;
	const RageSurfaceFormat *fmt = img->format;
	bool bDirect = (fmt != NULL && fmt->BitsPerPixel == 32 &&
		fmt->Mask[0] == VITA_TEX_RMASK && fmt->Mask[1] == VITA_TEX_GMASK &&
		fmt->Mask[2] == VITA_TEX_BMASK && fmt->Mask[3] == VITA_TEX_AMASK &&
		fmt->palette == NULL);
	if (!bDirect)
	{
		conv = CreateSurface(img->w, img->h, 32,
			VITA_TEX_RMASK, VITA_TEX_GMASK, VITA_TEX_BMASK, VITA_TEX_AMASK);
		RageSurfaceUtils::Blit(img, conv, img->w, img->h);
		srcSurf = conv;
	}

	// Pack the subrect tightly: the GL upload reads rows as width*4-byte
	// contiguous (UNPACK_ALIGNMENT 1), while surface rows may be padded.
	const int rowBytes = width * 4;
	if ((int)m_UploadBuf.size() < rowBytes * height)
		m_UploadBuf.resize((size_t)rowBytes * height);
	uint8_t *dst = &m_UploadBuf[0];
	const uint8_t *src = srcSurf->pixels + yoffset * srcSurf->pitch + xoffset * 4;
	for (int y = 0; y < height; ++y)
	{
		memcpy(dst, src, rowBytes);
		dst += rowBytes;
		src += srcSurf->pitch;
	}

	glBindTexture(GL_TEXTURE_2D, (GLuint)iTexHandle);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexSubImage2D(GL_TEXTURE_2D, 0, xoffset, yoffset, width, height,
		GL_RGBA, GL_UNSIGNED_BYTE, &m_UploadBuf[0]);
	{
		GLenum e = glGetError();
		if( e != GL_NO_ERROR )
			LOG->Info("RageDisplay_Vita: UpdateTexture %dx%d+%d+%d glGetError=0x%x",
				width, height, xoffset, yoffset, (unsigned)e);
	}
	delete conv;
}

void RageDisplay_Vita::DeleteTexture(unsigned iTexHandle)
{
	if (iTexHandle == 0)
		return;
	GLuint tex = (GLuint)iTexHandle;
	glDeleteTextures(1, &tex);
	m_Textures.erase(iTexHandle);
}

void RageDisplay_Vita::ClearAllTextures()
{
	// Upstream contract (RageDisplay_Legacy::ClearAllTextures): unbind all
	// texture units so the next draw starts from a clean binding state.
	// Must NOT delete textures here: callers (Sprite, ActorMultiVertex,
	// BitmapText, NoteDisplay, ...) invoke this before EVERY draw and bind
	// their own texture right after. Deleting live textures makes vitaGL
	// evict them under CDRAM pressure; the engine never re-uploads, so the
	// evicted quads sample GPU garbage (rainbow strips, noisy glyphs).
	SetTexture(TextureUnit_1, 0);
}

void RageDisplay_Vita::Lock(unsigned iTexHandle, RageSurface *pSurface)
{
	m_Locked[pSurface] = iTexHandle;
}

void RageDisplay_Vita::Unlock(RageSurface *pSurface, bool bChanged)
{
	std::map<RageSurface *, unsigned>::iterator it = m_Locked.find(pSurface);
	if (it == m_Locked.end())
		return;
	if (bChanged && pSurface != NULL)
		UpdateTexture(it->second, pSurface, 0, 0, pSurface->w, pSurface->h);
	m_Locked.erase(it);
}

void RageDisplay_Vita::SetTexture(TextureUnit tu, unsigned iTexture)
{
	(void)tu; // single texture unit on Vita stage 1
	if (iTexture == 0)
	{
		glDisable(GL_TEXTURE_2D);
		return;
	}
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, (GLuint)iTexture);
}

void RageDisplay_Vita::SetTextureMode(TextureUnit /*tu*/, TextureMode tm)
{
	switch (tm)
	{
	case TextureMode_Modulate:
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		break;
	case TextureMode_Add:
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_ADD);
		break;
	case TextureMode_Glow:
		// No combiner on stage 1: emulate glow via additive blending.
		glBlendFunc(GL_SRC_ALPHA, GL_ONE);
		break;
	default:
		break;
	}
}

void RageDisplay_Vita::SetTextureWrapping(TextureUnit /*tu*/, bool b)
{
	GLint wrap = b ? GL_REPEAT : GL_CLAMP_TO_EDGE;
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
}

void RageDisplay_Vita::SetTextureFiltering(TextureUnit /*tu*/, bool b)
{
	// No mipmaps on stage 1.
	GLint f = b ? GL_LINEAR : GL_NEAREST;
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, f);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, f);
}

void RageDisplay_Vita::SetBlendMode(BlendMode mode)
{
	glEnable(GL_BLEND);

	int srcRGB = GL_SRC_ALPHA, dstRGB = GL_ONE_MINUS_SRC_ALPHA;
	switch (mode)
	{
	case BLEND_NORMAL:        srcRGB = GL_SRC_ALPHA; dstRGB = GL_ONE_MINUS_SRC_ALPHA; break;
	case BLEND_ADD:           srcRGB = GL_SRC_ALPHA; dstRGB = GL_ONE; break;
	case BLEND_SUBTRACT:      glBlendEquation(GL_FUNC_REVERSE_SUBTRACT); srcRGB = GL_SRC_ALPHA; dstRGB = GL_ONE_MINUS_SRC_ALPHA; break;
	case BLEND_MODULATE:      srcRGB = GL_ZERO; dstRGB = GL_SRC_COLOR; break;
	case BLEND_COPY_SRC:      srcRGB = GL_ONE; dstRGB = GL_ZERO; break;
	case BLEND_ALPHA_MASK:    srcRGB = GL_ZERO; dstRGB = GL_ONE; break;
	case BLEND_ALPHA_KNOCK_OUT: srcRGB = GL_ZERO; dstRGB = GL_ONE; break;
	case BLEND_ALPHA_MULTIPLY: srcRGB = GL_SRC_ALPHA; dstRGB = GL_ZERO; break;
	case BLEND_WEIGHTED_MULTIPLY: srcRGB = GL_DST_COLOR; dstRGB = GL_SRC_COLOR; break;
	case BLEND_INVERT_DEST:   glBlendEquation(GL_FUNC_SUBTRACT); srcRGB = GL_ONE; dstRGB = GL_ONE; break;
	case BLEND_NO_EFFECT:     srcRGB = GL_ZERO; dstRGB = GL_ONE; break;
	default: break;
	}
	if (mode != BLEND_SUBTRACT && mode != BLEND_INVERT_DEST)
		glBlendEquation(GL_FUNC_ADD);
	glBlendFunc(srcRGB, dstRGB);
}

void RageDisplay_Vita::SetZWrite(bool b)
{
	m_bZWrite = b;
	glDepthMask(b ? GL_TRUE : GL_FALSE);
}

void RageDisplay_Vita::SetZTestMode(ZTestMode mode)
{
	m_bZTest = (mode != ZTEST_OFF);
	glEnable(GL_DEPTH_TEST);
	switch (mode)
	{
	case ZTEST_OFF:           glDepthFunc(GL_ALWAYS); break;
	case ZTEST_WRITE_ON_PASS: glDepthFunc(GL_LEQUAL); break;
	case ZTEST_WRITE_ON_FAIL: glDepthFunc(GL_GREATER); break;
	default: break;
	}
}

void RageDisplay_Vita::SetZBias(float f)
{
	float fNear = SCALE(f, 0.0f, 1.0f, 0.05f, 0.0f);
	float fFar = SCALE(f, 0.0f, 1.0f, 1.0f, 0.95f);
	glDepthRange(fNear, fFar);
}

void RageDisplay_Vita::ClearZBuffer()
{
	glClear(GL_DEPTH_BUFFER_BIT);
}

void RageDisplay_Vita::SetCullMode(CullMode mode)
{
	switch (mode)
	{
	case CULL_BACK:  glEnable(GL_CULL_FACE); glCullFace(GL_BACK); break;
	case CULL_FRONT: glEnable(GL_CULL_FACE); glCullFace(GL_FRONT); break;
	case CULL_NONE:
	default:         glDisable(GL_CULL_FACE); break;
	}
}

void RageDisplay_Vita::SetAlphaTest(bool b)
{
#if defined(VITA)
	/* No alpha test in GLES2 (see Init); alpha blending covers cutouts. */
	(void)b;
#else
	if (b)
	{
		glEnable(GL_ALPHA_TEST);
		glAlphaFunc(GL_GREATER, 0.0f);
	}
	else
	{
		glDisable(GL_ALPHA_TEST);
	}
#endif
}

void RageDisplay_Vita::SetMaterial(
	const RageColor & /*emissive*/,
	const RageColor & /*ambient*/,
	const RageColor & /*diffuse*/,
	const RageColor & /*specular*/,
	float /*shininess*/)
{
}

void RageDisplay_Vita::SetLighting(bool b)
{
	if (b)
		glEnable(GL_LIGHTING);
	else
		glDisable(GL_LIGHTING);
}

void RageDisplay_Vita::SetLightOff(int /*index*/)
{
}

void RageDisplay_Vita::SetLightDirectional(
	int /*index*/,
	const RageColor & /*ambient*/,
	const RageColor & /*diffuse*/,
	const RageColor & /*specular*/,
	const RageVector3 & /*dir*/)
{
}

void RageDisplay_Vita::SetSphereEnvironmentMapping(TextureUnit /*tu*/, bool /*b*/)
{
}

void RageDisplay_Vita::SetCelShaded(int /*stage*/)
{
}

void RageDisplay_Vita::SetLineWidth(float fWidth)
{
	glLineWidth(fWidth);
}

void RageDisplay_Vita::SetPolygonMode(PolygonMode pm)
{
#if defined(VITA)
	/* glPolygonMode does not exist in GLES2; filled rendering only. */
	(void)pm;
#else
	if (pm == POLYGON_FILL)
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	else
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
#endif
}

RageCompiledGeometry *RageDisplay_Vita::CreateCompiledGeometry()
{
	return new VitaCompiledGeometry;
}

void RageDisplay_Vita::DeleteCompiledGeometry(RageCompiledGeometry *p)
{
	delete p;
}

RageSurface *RageDisplay_Vita::CreateScreenshot()
{
	VideoModeParams avmp = m_pWind->GetActualVideoModeParams();
	RageSurface *img = CreateSurface(avmp.width, avmp.height, 32,
		VITA_TEX_RMASK, VITA_TEX_GMASK, VITA_TEX_BMASK, VITA_TEX_AMASK);
	glReadPixels(0, 0, avmp.width, avmp.height, GL_RGBA, GL_UNSIGNED_BYTE, img->pixels);
	// GL origin is bottom-left; RageSurface expects top-left: flip rows.
	const int rowBytes = avmp.width * 4;
	vector<uint8_t> row(rowBytes);
	for (int y = 0; y < avmp.height / 2; ++y)
	{
		uint8_t *a = img->pixels + y * rowBytes;
		uint8_t *b = img->pixels + (avmp.height - 1 - y) * rowBytes;
		memcpy(row.data(), a, rowBytes);
		memcpy(a, b, rowBytes);
		memcpy(b, row.data(), rowBytes);
	}
	return img;
}

// --- draw helpers ---

void RageDisplay_Vita::ShowBootSplash()
{
#ifdef VITA
	/* Upload once (textures kept for re-presents; ~2.2 MB VRAM total). */
	static unsigned s_bootTex = 0;
	static unsigned s_stageTex[BOOTSTAGE_COUNT] = { 0 };
	static unsigned s_fontTex = 0;
	static bool s_tried = false;
	if( !s_tried )
	{
		s_tried = true;
		SceUID fd = sceIoOpen( "app0:bootart/bg.raw", SCE_O_RDONLY, 0 );
		if( fd >= 0 )
		{
			RageSurface *img = CreateSurface(960, 544, 32,
				VITA_TEX_RMASK, VITA_TEX_GMASK, VITA_TEX_BMASK, VITA_TEX_AMASK);
			uint8_t *p = img->pixels;
			int left = 960 * 544 * 4;
			while( left > 0 )
			{
				int n = sceIoRead( fd, p, (SceSize)left );
				if( n <= 0 )
					break;
				p += n;
				left -= n;
			}
			sceIoClose( fd );
			if( left == 0 )
				s_bootTex = CreateTexture( RagePixelFormat_RGBA8, img, false );
			delete img;
		}
		for( int i = 0; i < VITA_BOOT_STAGE_COUNT && i < BOOTSTAGE_COUNT; ++i )
		{
			int w = VITA_BOOT_STAGE_ARTS[i].w, h = VITA_BOOT_STAGE_ARTS[i].h;
			SceUID fd2 = sceIoOpen( VITA_BOOT_STAGE_ARTS[i].file, SCE_O_RDONLY, 0 );
			if( fd2 < 0 )
				continue;
			RageSurface *img = CreateSurface(w, h, 32,
				VITA_TEX_RMASK, VITA_TEX_GMASK, VITA_TEX_BMASK, VITA_TEX_AMASK);
			uint8_t *p = img->pixels;
			int left = w * h * 4;
			while( left > 0 )
			{
				int n = sceIoRead( fd2, p, (SceSize)left );
				if( n <= 0 )
					break;
				p += n;
				left -= n;
			}
			sceIoClose( fd2 );
			if( left == 0 )
				s_stageTex[i] = CreateTexture( RagePixelFormat_RGBA8, img, false );
			delete img;
		}
		/* Font atlas: 95 cells of 16x16 in one 1520x16 texture. */
		SceUID fd3 = sceIoOpen( "app0:bootart/font16.raw", SCE_O_RDONLY, 0 );
		if( fd3 >= 0 )
		{
			RageSurface *img = CreateSurface(95 * 16, 16, 32,
				VITA_TEX_RMASK, VITA_TEX_GMASK, VITA_TEX_BMASK, VITA_TEX_AMASK);
			bool ok = true;
			for( int ch = 0; ch < 95 && ok; ++ch )
			{
				for( int r = 0; r < 16 && ok; ++r )
				{
					uint8_t *d = img->pixels + ((size_t)r * 95 * 16 + (size_t)ch * 16) * 4;
					int left = 16 * 4;
					while( left > 0 )
					{
						int n = sceIoRead( fd3, d, (SceSize)left );
						if( n <= 0 ) { ok = false; break; }
						d += n;
						left -= n;
					}
				}
			}
			sceIoClose( fd3 );
			if( ok )
				s_fontTex = CreateTexture( RagePixelFormat_RGBA8, img, false );
			delete img;
		}
	}
	if( s_bootTex == 0 )
		return;

	VideoModeParams avmp = m_pWind->GetActualVideoModeParams();
	glViewport(0, 0, avmp.width, avmp.height);
	glClearColor(0, 0, 0, 0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	/* Direct pixel-space ortho, y-down (engine matrix stacks untouched;
	 * the next engine draw reloads GL matrices via SendCurrentMatrices). */
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, 960, 544, 0, -1, 1);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glMatrixMode(GL_TEXTURE);
	glLoadIdentity();

	int stage = VitaBootSplash::GetStage();
	if( stage < 0 || stage >= BOOTSTAGE_COUNT )
		stage = 0;
	int done = VitaBootSplash::GetDone();
	int total = VitaBootSplash::GetTotal();
	const char *sub = VitaBootSplash::GetSubLine();

	/* --- background --- */
	{
		static const float pos[6 * 3] = {
			0,0,0,  960,0,0,  960,544,0,
			0,0,0,  960,544,0,  0,544,0
		};
		static const float tex[6 * 2] = {
			0,0,  1,0,  1,1,
			0,0,  1,1,  0,1
		};
		glDisable(GL_BLEND);
		glDisableClientState(GL_COLOR_ARRAY);
		glColor4f(1, 1, 1, 1);
		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, (GLuint)s_bootTex);
		glVertexPointer(3, GL_FLOAT, 0, pos);
		glTexCoordPointer(2, GL_FLOAT, 0, tex);
		glDrawArrays(GL_TRIANGLES, 0, 6);
	}

	/* --- flat UI: brown box + edges + bar slot + XP blocks, one batch --- */
	{
		/* Same layout as the framebuffer splash: box y216..328 full
		 * width, bar x230..730 y284..306, 20 blocks (+highlights). */
		float P[44 * 6 * 3];
		uint8_t C[44 * 6 * 4];
		int nq = 0;
		struct Q { float x0, y0, x1, y1; uint8_t r, g, b; };
		Q qs[44];
		qs[nq++] = { 0, 216, 960, 328, 110, 66, 28 };   /* box */
		qs[nq++] = { 0, 216, 960, 219, 58, 33, 12 };    /* edge top */
		qs[nq++] = { 0, 325, 960, 328, 58, 33, 12 };    /* edge bottom */
		qs[nq++] = { 230, 284, 730, 306, 40, 22, 8 };    /* slot */
		int filled = 0;
		if( total > 0 )
		{
			if( done < 0 ) done = 0;
			if( done > total ) done = total;
			filled = (int)((long long)done * 20 / total);
		}
		int avail = 500 - 19 * 4;
		int bw = avail / 20;
		int x = 230 + (avail - bw * 20) / 2;
		for( int i = 0; i < 20 && nq + 2 <= 44; ++i )
		{
			if( i < filled )
			{
				qs[nq++] = { (float)x, 286, (float)(x + bw), 304, 255, 138, 0 };
				qs[nq++] = { (float)x, 286, (float)(x + bw), 289, 255, 190, 90 };
			}
			x += bw + 4;
		}
		int nv = 0;
		for( int q = 0; q < nq; ++q )
		{
			float *pp = &P[nv * 3];
			uint8_t *cc = &C[nv * 4];
			float xs[6] = { qs[q].x0, qs[q].x1, qs[q].x1, qs[q].x0, qs[q].x1, qs[q].x0 };
			float ys[6] = { qs[q].y0, qs[q].y0, qs[q].y1, qs[q].y0, qs[q].y1, qs[q].y1 };
			for( int k = 0; k < 6; ++k )
			{
				pp[k * 3 + 0] = xs[k]; pp[k * 3 + 1] = ys[k]; pp[k * 3 + 2] = 0;
				cc[k * 4 + 0] = qs[q].r; cc[k * 4 + 1] = qs[q].g; cc[k * 4 + 2] = qs[q].b; cc[k * 4 + 3] = 255;
			}
			nv += 6;
		}
		glDisable(GL_TEXTURE_2D);
		glEnableClientState(GL_COLOR_ARRAY);
		glVertexPointer(3, GL_FLOAT, 0, P);
		glColorPointer(4, GL_UNSIGNED_BYTE, 0, C);
		glDrawArrays(GL_TRIANGLES, 0, nv);
	}

	/* --- stage label sprite --- */
	if( s_stageTex[stage] != 0 )
	{
		int w = VITA_BOOT_STAGE_ARTS[stage].w, h = VITA_BOOT_STAGE_ARTS[stage].h;
		float x0 = (960 - w) / 2.0f, y0 = 255 - h;
		float pos[6 * 3] = { x0,y0,0,  x0+w,y0,0,  x0+w,y0+h,0,  x0,y0,0,  x0+w,y0+h,0,  x0,y0+h,0 };
		static const float tex[6 * 2] = { 0,0, 1,0, 1,1, 0,0, 1,1, 0,1 };
		glDisable(GL_BLEND);
		glDisableClientState(GL_COLOR_ARRAY);
		glColor4f(1, 1, 1, 1);
		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, (GLuint)s_stageTex[stage]);
		glVertexPointer(3, GL_FLOAT, 0, pos);
		glTexCoordPointer(2, GL_FLOAT, 0, tex);
		glDrawArrays(GL_TRIANGLES, 0, 6);
	}

	/* --- sub-line via font atlas, one batch --- */
	if( s_fontTex != 0 && sub != NULL && sub[0] != '\0' )
	{
		float P[70 * 6 * 3];
		float T[70 * 6 * 2];
		int nv = 0;
		int totalW = 0;
		const char *t = sub;
		int count = 0;
		while( *t && count < 69 )
		{
			unsigned char c = (unsigned char)*t++;
			if( c < 32 || c > 126 ) c = '?';
			totalW += VITA_BOOTFONT_ADV[c - 32];
			++count;
		}
		float x = (960 - totalW) / 2.0f;
		t = sub;
		count = 0;
		while( *t && count < 69 )
		{
			unsigned char c = (unsigned char)*t++;
			if( c < 32 || c > 126 ) c = '?';
			int adv = VITA_BOOTFONT_ADV[c - 32];
			float u0 = ((c - 32) * 16) / 1520.0f, u1 = ((c - 32) * 16 + 16) / 1520.0f;
			float xs[6] = { x, x + 16, x + 16, x, x + 16, x };
			float ys[6] = { 309, 309, 325, 309, 325, 325 };
			float us[6] = { u0, u1, u1, u0, u1, u0 };
			float vs[6] = { 0, 0, 1, 0, 1, 1 };
			for( int k = 0; k < 6; ++k )
			{
				P[(nv + k) * 3 + 0] = xs[k]; P[(nv + k) * 3 + 1] = ys[k]; P[(nv + k) * 3 + 2] = 0;
				T[(nv + k) * 2 + 0] = us[k]; T[(nv + k) * 2 + 1] = vs[k];
			}
			nv += 6;
			x += adv;
			++count;
		}
		glDisable(GL_BLEND);
		glDisableClientState(GL_COLOR_ARRAY);
		glColor4f(1, 1, 1, 1);
		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, (GLuint)s_fontTex);
		glVertexPointer(3, GL_FLOAT, 0, P);
		glTexCoordPointer(2, GL_FLOAT, 0, T);
		glDrawArrays(GL_TRIANGLES, 0, nv);
	}

	/* Restore Init state for subsequent engine draws. */
	glEnable(GL_BLEND);
	glEnableClientState(GL_COLOR_ARRAY);

	glFlush();
	m_pWind->SwapBuffers();
	m_pWind->Update();

	/* Prove every present in the log: if the screen freezes, the last
	 * line here tells exactly which present ran and what GL said. */
	LOG->Info("VitaBootSplash: present stage=%d (%s) %d/%d bg=%u glGetError=0x%x",
		stage, VITA_BOOT_STAGE_ARTS[stage].id, done, total,
		s_bootTex, (unsigned)glGetError());
#endif
}

void RageDisplay_Vita::DrawTris(const RageSpriteVertex v[], const uint16_t *indices, int iNumIndices)
{
	SendCurrentMatrices();

	// RageSpriteVertex is tightly packed (p[3f] n[3f] c[BGRA bytes] t[2f],
	// 36 bytes); feed position/normal/texcoords to GL directly with a stride
	// and glDrawElements instead of expanding a vertex soup per draw call.
	// Only colors are expanded (BGRA struct order -> RGBA for GL).
	COMPILE_ASSERT(sizeof(RageSpriteVertex) == 36);
	COMPILE_ASSERT(offsetof(RageSpriteVertex, p) == 0);
	COMPILE_ASSERT(offsetof(RageSpriteVertex, n) == 12);
	COMPILE_ASSERT(offsetof(RageSpriteVertex, c) == 24);
	COMPILE_ASSERT(offsetof(RageSpriteVertex, t) == 28);

	m_DrawCalls++;
	m_DrawTris += iNumIndices / 3;

	const GLsizei stride = (GLsizei)sizeof(RageSpriteVertex);
	const uint8_t *base = reinterpret_cast<const uint8_t *>(v);
	glVertexPointer(3, GL_FLOAT, stride, base + offsetof(RageSpriteVertex, p));
	glNormalPointer(GL_FLOAT, stride, base + offsetof(RageSpriteVertex, n));
	glTexCoordPointer(2, GL_FLOAT, stride, base + offsetof(RageSpriteVertex, t));

	size_t need = (size_t)iNumIndices;
	if (m_Col.size() < need * 4)
		m_Col.resize(need * 4);
	for (int i = 0; i < iNumIndices; ++i)
	{
		const RageSpriteVertex &sv = v[indices[i]];
		m_Col[i * 4 + 0] = sv.c.r; m_Col[i * 4 + 1] = sv.c.g; m_Col[i * 4 + 2] = sv.c.b; m_Col[i * 4 + 3] = sv.c.a;
	}
	glColorPointer(4, GL_UNSIGNED_BYTE, 0, &m_Col[0]);
	glDrawElements(GL_TRIANGLES, iNumIndices, GL_UNSIGNED_SHORT, indices);
}

void RageDisplay_Vita::DrawQuadsInternal(const RageSpriteVertex v[], int iNumVerts)
{
	if (m_Idx.size() < (size_t)iNumVerts * 3 / 2)
		m_Idx.resize((size_t)iNumVerts * 3 / 2 + 6);
	int n = 0;
	for (int q = 0; q + 3 < iNumVerts + 1; q += 4)
	{
		m_Idx[n++] = (uint16_t)(q + 0);
		m_Idx[n++] = (uint16_t)(q + 1);
		m_Idx[n++] = (uint16_t)(q + 2);
		m_Idx[n++] = (uint16_t)(q + 0);
		m_Idx[n++] = (uint16_t)(q + 2);
		m_Idx[n++] = (uint16_t)(q + 3);
	}
	DrawTris(v, &m_Idx[0], n);
}

void RageDisplay_Vita::DrawQuadStripInternal(const RageSpriteVertex v[], int iNumVerts)
{
	if (m_Idx.size() < (size_t)iNumVerts * 3)
		m_Idx.resize((size_t)iNumVerts * 3);
	int n = 0;
	for (int i = 0; i + 3 < iNumVerts + 1; i += 2)
	{
		m_Idx[n++] = (uint16_t)(i + 0);
		m_Idx[n++] = (uint16_t)(i + 1);
		m_Idx[n++] = (uint16_t)(i + 2);
		m_Idx[n++] = (uint16_t)(i + 1);
		m_Idx[n++] = (uint16_t)(i + 3);
		m_Idx[n++] = (uint16_t)(i + 2);
	}
	DrawTris(v, &m_Idx[0], n);
}

void RageDisplay_Vita::DrawFanInternal(const RageSpriteVertex v[], int iNumVerts)
{
	if (m_Idx.size() < (size_t)iNumVerts * 3)
		m_Idx.resize((size_t)iNumVerts * 3);
	int n = 0;
	for (int i = 1; i + 1 < iNumVerts + 1; ++i)
	{
		m_Idx[n++] = 0;
		m_Idx[n++] = (uint16_t)i;
		m_Idx[n++] = (uint16_t)(i + 1);
	}
	DrawTris(v, &m_Idx[0], n);
}

void RageDisplay_Vita::DrawStripInternal(const RageSpriteVertex v[], int iNumVerts)
{
	if (m_Idx.size() < (size_t)iNumVerts * 3)
		m_Idx.resize((size_t)iNumVerts * 3);
	int n = 0;
	for (int i = 0; i + 2 < iNumVerts + 1; ++i)
	{
		if (i & 1)
		{
			m_Idx[n++] = (uint16_t)(i + 1);
			m_Idx[n++] = (uint16_t)(i + 0);
			m_Idx[n++] = (uint16_t)(i + 2);
		}
		else
		{
			m_Idx[n++] = (uint16_t)(i + 0);
			m_Idx[n++] = (uint16_t)(i + 1);
			m_Idx[n++] = (uint16_t)(i + 2);
		}
	}
	DrawTris(v, &m_Idx[0], n);
}

void RageDisplay_Vita::DrawTrianglesInternal(const RageSpriteVertex v[], int iNumVerts)
{
	// Already triangles: identity index.
	if (m_Idx.size() < (size_t)iNumVerts)
		m_Idx.resize((size_t)iNumVerts);
	for (int i = 0; i < iNumVerts; ++i)
		m_Idx[i] = (uint16_t)i;
	DrawTris(v, &m_Idx[0], iNumVerts);
}

void RageDisplay_Vita::DrawCompiledGeometryInternal(const RageCompiledGeometry *p, int iMeshIndex)
{
	const VitaCompiledGeometry *g = static_cast<const VitaCompiledGeometry *>(p);
	if (g == NULL || iMeshIndex < 0 || (size_t)iMeshIndex >= g->meshes.size())
		return;
	const VitaCompiledGeometry::Mesh &m = g->meshes[(size_t)iMeshIndex];
	if (m.idx.empty())
		return;
	SendCurrentMatrices();
	glVertexPointer(3, GL_FLOAT, 0, &m.pos[0]);
	glTexCoordPointer(2, GL_FLOAT, 0, &m.tex[0]);
	glNormalPointer(GL_FLOAT, 0, &m.norm[0]);
	glDisableClientState(GL_COLOR_ARRAY);
	glColor4f(1, 1, 1, 1);
	glDrawElements(GL_TRIANGLES, (GLsizei)m.idx.size(), GL_UNSIGNED_SHORT, &m.idx[0]);
	glEnableClientState(GL_COLOR_ARRAY);
}

void RageDisplay_Vita::DrawLineStripInternal(const RageSpriteVertex v[], int iNumVerts, float LineWidth)
{
	SendCurrentMatrices();
	// Positions feed GL directly with a stride (see DrawTris); only colors
	// are expanded (BGRA struct order -> RGBA for GL).
	const GLsizei stride = (GLsizei)sizeof(RageSpriteVertex);
	const uint8_t *base = reinterpret_cast<const uint8_t *>(v);
	if (m_Col.size() < (size_t)iNumVerts * 4) { m_Col.resize((size_t)iNumVerts * 4); }
	for (int i = 0; i < iNumVerts; ++i)
	{
		m_Col[i * 4 + 0] = v[i].c.r; m_Col[i * 4 + 1] = v[i].c.g; m_Col[i * 4 + 2] = v[i].c.b; m_Col[i * 4 + 3] = v[i].c.a;
	}
	glLineWidth(LineWidth);
	glDisable(GL_TEXTURE_2D);
	glVertexPointer(3, GL_FLOAT, stride, base + offsetof(RageSpriteVertex, p));
	glColorPointer(4, GL_UNSIGNED_BYTE, 0, &m_Col[0]);
	glDrawArrays(GL_LINE_STRIP, 0, iNumVerts);
	glEnable(GL_TEXTURE_2D);
}

void RageDisplay_Vita::DrawSymmetricQuadStripInternal(const RageSpriteVertex v[], int iNumVerts)
{
	// Same index pattern as the Legacy renderer ({1,3,0} {1,4,3} ...).
	int iNumPieces = (iNumVerts - 3) / 3;
	if (iNumPieces <= 0)
		return;
	if (m_Idx.size() < (size_t)iNumPieces * 12)
		m_Idx.resize((size_t)iNumPieces * 12);
	int n = 0;
	for (int i = 0; i < iNumPieces; ++i)
	{
		const uint16_t base[12] = {
			(uint16_t)(i * 3 + 1), (uint16_t)(i * 3 + 3), (uint16_t)(i * 3 + 0),
			(uint16_t)(i * 3 + 1), (uint16_t)(i * 3 + 4), (uint16_t)(i * 3 + 3),
			(uint16_t)(i * 3 + 1), (uint16_t)(i * 3 + 5), (uint16_t)(i * 3 + 4),
			(uint16_t)(i * 3 + 1), (uint16_t)(i * 3 + 2), (uint16_t)(i * 3 + 5) };
		for (int k = 0; k < 12; ++k)
			m_Idx[n++] = base[k];
	}
	DrawTris(v, &m_Idx[0], n);
}
