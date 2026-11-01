#include "global.h"
#include "RageDisplay_Vita.h"
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
#ifdef VITA
#include "VitaTouch.h"
#endif

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
#include <psp2/kernel/threadmgr.h> /* sceKernelGetSystemTimeWide */
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

	/* Cap on one glDrawArrays batch (in vertices). */
	const size_t VITA_BATCH_MAX = 6000;

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

	/* Per-texture sampler state slot: 0 = unknown, 1 = on, 2 = off. */
	inline uint8_t &TexParamSlot(std::vector<uint8_t> &v, unsigned iTex)
	{
		if (iTex >= v.size())
			v.resize(iTex + 64, 0);
		return v[iTex];
	}
}

RageDisplay_Vita::RageDisplay_Vita()
	: m_pWind(NULL), m_bZWrite(false), m_bZTest(false),
	m_DrawCalls(0), m_DrawTris(0),
	m_TexUploads(0), m_TexUploadBytes(0), m_TexBinds(0), m_TexToggles(0),
	m_TexOn(false), m_TexOffPending(false), m_GlBoundTex(0), m_BatchFlushes(0),
	m_MatValid(false), m_MatDirty(false), m_ProjDirty(false), m_GLMatReal(-1), m_LightingOn(false),
	m_CurTex(0), m_WrapTex(0), m_WrapVal(0), m_FiltTex(0), m_FiltVal(0),
	m_EnvMode(0), m_EnvModeValid(false),
	m_BlendSrc(0), m_BlendDst(0), m_BlendEq(0), m_BlendValid(false),
	m_ZTestFunc(0), m_ZTestValid(false),
	m_ZWriteMask(0), m_ZWriteValid(false),
	m_ZBiasNear(0), m_ZBiasFar(0), m_ZBiasValid(false),
	m_CullMode(-1), m_CullValid(false),
	m_FrameStartUs(0), m_DrawEmitUs(0), m_PresentUs(0),
	m_DrawSetupUs(0), m_DrawSubmitUs(0)
{
}

RageDisplay_Vita::~RageDisplay_Vita()
{
	delete m_pWind;
}

void RageDisplay_Vita::ResetStateCache()
{
	/* Forget all cached render state so the next Set* call re-forwards to GL.
	 * Used after a fresh device (TryVideoMode), where GL defaults apply. */
	m_CurTex = 0;
	m_WrapTex = 0; m_WrapVal = 0;
	m_FiltTex = 0; m_FiltVal = 0;
	m_TexWrapParam.clear();
	m_TexFiltParam.clear();
	m_EnvModeValid = false;
	m_BlendValid = false;
	m_ZTestValid = false;
	m_ZWriteValid = false;
	m_TexOn = false;
	m_TexOffPending = false;
	m_GlBoundTex = 0;
	m_GLMatReal = -1;
	m_MatDirty = true;
	m_ProjDirty = true;
	m_LightingOn = false;
	m_ZBiasValid = false;
	m_CullValid = false;
	m_Batch.clear();
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

	/* "Untextured" draws (iTexture==0) use glDisable(GL_TEXTURE_2D) with the
	 * per-vertex colour; the toggle is cached (ApplyTextureState) and, with
	 * batching, affects at most one shader variant switch per state group
	 * instead of one per draw. */
	m_TexOffPending = false;
	m_GlBoundTex = 0;
	m_TexOn = true;
	m_GLMatReal = -1;
	m_MatDirty = true;
	m_EnvMode = GL_MODULATE; m_EnvModeValid = true;
	m_BlendSrc = GL_SRC_ALPHA; m_BlendDst = GL_ONE_MINUS_SRC_ALPHA;
	m_BlendEq = GL_FUNC_ADD; m_BlendValid = true;

	LOG->Info("RageDisplay_Vita: initialized (vitaGL fixed-function)");
	return RString();
}

RString RageDisplay_Vita::TryVideoMode(const VideoModeParams &p, bool &bNewDeviceOut)
{
	if (m_pWind == NULL)
		m_pWind = LowLevelWindow::Create();
	m_MatValid = false; /* fresh device: GL matrix state is unknown */
	ResetStateCache();  /* and so is the render-state cache */
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
	FlushBatch(); /* nothing may survive into the cleared back buffer */
#if defined(VITA)
	m_FrameStartUs = (long long)sceKernelGetSystemTimeWide();
#endif
	VideoModeParams avmp = m_pWind->GetActualVideoModeParams();
	glViewport(0, 0, avmp.width, avmp.height);
	glClearColor(0, 0, 0, 0);
	SetZWrite(true);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	return RageDisplay::BeginFrame();
}

void RageDisplay_Vita::EndFrame()
{
	FlushBatch();
#if defined(VITA)
	long long t0 = (long long)sceKernelGetSystemTimeWide();
	if (m_FrameStartUs != 0)
		m_DrawEmitUs = t0 - m_FrameStartUs; /* CPU: submit all draws of this frame */

	/* Touch lanes sit on top of the scene, in screen space. Keep them out of
	 * the "emit" measurement above: this is UI, not gameplay cost. */
	if (VitaTouch::ShouldDrawOverlay())
	{
		const VideoModeParams &vp = m_pWind->GetActualVideoModeParams();
		VitaTouch::DrawOverlay(vp.width, vp.height);
		/* The overlay touched GL state behind the cache's back. */
		ResetStateCache();
	}
#endif
	glFlush();
	FrameLimitBeforeVsync(m_pWind->GetActualVideoModeParams().rate);
	m_pWind->SwapBuffers();
	FrameLimitAfterVsync();
	m_pWind->Update();
#if defined(VITA)
	long long t1 = (long long)sceKernelGetSystemTimeWide();
	m_PresentUs = t1 - t0; /* wall: glFlush + vsync swap + update */
#endif

#if defined(VITA)
	{
		static int s_nFrames = 0;
		static long long s_last = 0;
		static long long s_emitAcc = 0, s_presentAcc = 0;
		static long s_upAcc = 0, s_upBytesAcc = 0;
		static long s_bindAcc = 0, s_togAcc = 0;
		static long s_drawAcc = 0, s_triAcc = 0;
		static long s_flushAcc = 0;
		static long long s_setupAcc = 0, s_submitAcc = 0;
		static int s_logged = 0;
		/* Per-frame period tracking: the 60-frame averages hide the
		 * micro-stutter, so log the worst frame and how many frames in the
		 * window missed the 60Hz (16.67ms) deadline. */
		static long long s_prevT1 = 0;
		static long long s_maxWall = 0;
		static int s_late = 0;
		const long long dtUs = (s_prevT1 != 0) ? (t1 - s_prevT1) : 0;
		s_prevT1 = t1;
		if (dtUs > s_maxWall)
			s_maxWall = dtUs;
		if (dtUs > 17000)
			++s_late;
		/* One-shots are invisible in the 60-frame averages: log the frame
		 * itself so a hitch can be attributed (emit = CPU draw submission,
		 * present = swap/vsync; both low means the stall was elsewhere). */
		if (dtUs > 60000)
			LOG->Info("Vita spike: %lldus emit %lldus present %lldus",
				dtUs, m_DrawEmitUs, m_PresentUs);
		++s_nFrames;
		s_emitAcc += m_DrawEmitUs;
		s_presentAcc += m_PresentUs;
		s_upAcc += m_TexUploads;
		s_upBytesAcc += m_TexUploadBytes;
		s_bindAcc += m_TexBinds;
		s_togAcc += m_TexToggles;
		s_drawAcc += m_DrawCalls;
		s_triAcc += m_DrawTris;
		s_flushAcc += m_BatchFlushes;
		s_setupAcc += m_DrawSetupUs;
		s_submitAcc += m_DrawSubmitUs;
		m_DrawSetupUs = 0;
		m_DrawSubmitUs = 0;
		if (s_last == 0)
			s_last = t1;
		++s_logged;
		if (s_logged >= 60)
		{
			/* wall = whole frame period; emit = CPU submitting draws;
			 * present = glFlush+swap+update (GPU/vsync). Honest breakdown. */
			long long wallUs = t1 - s_last;
			GLenum e = glGetError();
#ifdef VITA_BUILD_NUMBER
			LOG->Info("Vita: b%d f%d wall %lldus max %lldus late %d emit %lldus (setup %lldus submit %lldus) present %lldus; draws %ld tris %ld flush %ld; texup %ld/%ldKB; binds %ld tog %ld; gle=0x%x",
				VITA_BUILD_NUMBER, s_nFrames,
				wallUs / s_logged, s_maxWall, s_late, s_emitAcc / s_logged,
				s_setupAcc / s_logged, s_submitAcc / s_logged, s_presentAcc / s_logged,
				s_drawAcc / s_logged, s_triAcc / s_logged, s_flushAcc / s_logged,
				s_upAcc / s_logged, s_upBytesAcc / s_logged / 1024,
				s_bindAcc / s_logged, s_togAcc / s_logged, (unsigned)e);
#else
			LOG->Info("Vita: f%d wall %lldus max %lldus late %d emit %lldus (setup %lldus submit %lldus) present %lldus; draws %ld tris %ld flush %ld; texup %ld/%ldKB; binds %ld tog %ld; gle=0x%x",
				s_nFrames,
				wallUs / s_logged, s_maxWall, s_late, s_emitAcc / s_logged,
				s_setupAcc / s_logged, s_submitAcc / s_logged, s_presentAcc / s_logged,
				s_drawAcc / s_logged, s_triAcc / s_logged, s_flushAcc / s_logged,
				s_upAcc / s_logged, s_upBytesAcc / s_logged / 1024,
				s_bindAcc / s_logged, s_togAcc / s_logged, (unsigned)e);
#endif
			s_last = t1;
			s_logged = 0;
			s_emitAcc = s_presentAcc = 0;
			s_setupAcc = s_submitAcc = 0;
			m_DrawSetupUs = 0;
			m_DrawSubmitUs = 0;
			s_upAcc = s_upBytesAcc = 0;
			s_bindAcc = s_togAcc = 0;
			s_drawAcc = s_triAcc = 0;
			s_flushAcc = 0;
			s_maxWall = 0;
			s_late = 0;
			m_DrawCalls = 0;
			m_DrawTris = 0;
			m_BatchFlushes = 0;
			m_TexUploads = 0;
			m_TexUploadBytes = 0;
			m_TexBinds = 0;
			m_TexToggles = 0;
		}
	}
#endif

	RageDisplay::EndFrame();
}

void RageDisplay_Vita::SendCurrentMatrices()
{
	RageMatrix projection;
	RageMatrixMultiply(&projection, GetCentering(), GetProjectionTop());

	RageMatrix modelView;
	RageMatrixMultiply(&modelView, GetViewTop(), GetWorldTop());

	const float *texture = (const float *)GetTextureTop();

	/* The modelview and texture matrices are baked into the batch, so they
	 * may change at any time without splitting it. The projection, however,
	 * is left to GL for the open batch, so the batch must be submitted before
	 * it changes. */
	const bool bProjChanged = !m_MatValid || memcmp(m_LastProj, &projection, sizeof(m_LastProj)) != 0;
	const bool bMVChanged = !m_MatValid || memcmp(m_LastMV, &modelView, sizeof(m_LastMV)) != 0;
	const bool bTexChanged = !m_MatValid || memcmp(m_LastTex, texture, sizeof(m_LastTex)) != 0;
	if (!bProjChanged && !bMVChanged && !bTexChanged)
		return;

	if (bProjChanged)
		FlushBatch();

	memcpy(m_LastProj, &projection, sizeof(m_LastProj));
	if (bMVChanged) memcpy(m_LastMV, &modelView, sizeof(m_LastMV));
	if (bTexChanged) memcpy(m_LastTex, texture, sizeof(m_LastTex));
	m_MatValid = true;
	m_MatDirty = true;
	if (bProjChanged) m_ProjDirty = true;
}

/* Load either the "batch" layout (real projection, identity modelview and
 * texture: batch vertices are baked into eye space already) or the "real"
 * layout (all three matrices) into GL. Cached: glLoadMatrixf is a shader
 * uniform update in vitaGX, so it is only re-issued when it really changed. */
void RageDisplay_Vita::EnsureGLMatrices(bool bReal)
{
	const int want = bReal ? 1 : 0;
	/* Batch mode only depends on the projection; the modelview and texture
	 * matrices it uploads are identity and the vertices are pre-baked. */
	if (m_GLMatReal == want && (bReal ? !m_MatDirty : !m_ProjDirty))
		return;

	glMatrixMode(GL_PROJECTION);
	if (m_MatValid) glLoadMatrixf(m_LastProj); else glLoadIdentity();

	glMatrixMode(GL_MODELVIEW);
	if (bReal && m_MatValid) glLoadMatrixf(m_LastMV); else glLoadIdentity();

	glMatrixMode(GL_TEXTURE);
	if (bReal && m_MatValid) glLoadMatrixf(m_LastTex); else glLoadIdentity();

	m_ProjDirty = false;
	if (bReal)
		m_MatDirty = false;
	m_GLMatReal = want;
}

/* Make GL's texturing match iTexture (0 = untextured). Flushing any open
 * batch first is required: those vertices were meant to be drawn with the
 * previous texture state. */
void RageDisplay_Vita::ApplyTextureState(unsigned iTexture)
{
	if (iTexture == 0)
	{
		if (m_TexOn)
		{
			FlushBatch();
			glDisable(GL_TEXTURE_2D);
			m_TexOn = false;
			++m_TexToggles;
		}
		return;
	}

	if (!m_TexOn)
	{
		FlushBatch();
		glEnable(GL_TEXTURE_2D);
		m_TexOn = true;
		++m_TexToggles;
	}
	if (m_GlBoundTex != iTexture)
	{
		FlushBatch();
		glBindTexture(GL_TEXTURE_2D, (GLuint)iTexture);
		m_GlBoundTex = iTexture;
		++m_TexBinds;
	}
}

/* Submit the accumulated triangles with one glDrawArrays. */
void RageDisplay_Vita::FlushBatch()
{
	if (m_Batch.empty())
		return;

	EnsureGLMatrices(false);

	const BatchVertex *b = &m_Batch[0];
#if defined(VITA)
	const long long tA = (long long)sceKernelGetSystemTimeWide();
#endif
	glVertexPointer(3, GL_FLOAT, (GLsizei)sizeof(BatchVertex), &b->x);
	glColorPointer(4, GL_UNSIGNED_BYTE, (GLsizei)sizeof(BatchVertex), &b->r);
	glTexCoordPointer(2, GL_FLOAT, (GLsizei)sizeof(BatchVertex), &b->u);
#if defined(VITA)
	const long long tB = (long long)sceKernelGetSystemTimeWide();
	m_DrawSetupUs += tB - tA;
#endif
	glDrawArrays(GL_TRIANGLES, 0, (GLsizei)m_Batch.size());
#if defined(VITA)
	m_DrawSubmitUs += (long long)sceKernelGetSystemTimeWide() - tB;
#endif

	m_Batch.clear();
	++m_DrawCalls;
	++m_BatchFlushes;
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
	FlushBatch(); /* the upload rebinds GL_TEXTURE_2D for its own texture */
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	m_WrapTex = (unsigned)tex; m_WrapVal = GL_CLAMP_TO_EDGE;
	m_FiltTex = (unsigned)tex; m_FiltVal = GL_LINEAR;
	m_GlBoundTex = (unsigned)tex;
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glGetError(); /* clear stale flag: attribute only fresh errors */
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
	m_TexUploads++;
	m_TexUploadBytes += (long)conv->w * (long)conv->h * 4;
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

	FlushBatch(); /* the upload rebinds GL_TEXTURE_2D for its own texture */
	glBindTexture(GL_TEXTURE_2D, (GLuint)iTexHandle);
	m_GlBoundTex = iTexHandle;
	m_WrapTex = 0; m_FiltTex = 0; /* params unknown after a rebuild */
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glGetError(); /* clear stale flag: attribute only fresh errors */
	glTexSubImage2D(GL_TEXTURE_2D, 0, xoffset, yoffset, width, height,
		GL_RGBA, GL_UNSIGNED_BYTE, &m_UploadBuf[0]);
	{
		GLenum e = glGetError();
		if( e != GL_NO_ERROR )
			LOG->Info("RageDisplay_Vita: UpdateTexture %dx%d+%d+%d glGetError=0x%x",
				width, height, xoffset, yoffset, (unsigned)e);
	}
	m_TexUploads++;
	m_TexUploadBytes += (long)rowBytes * height;
	delete conv;
}

void RageDisplay_Vita::DeleteTexture(unsigned iTexHandle)
{
	if (iTexHandle == 0)
		return;
	FlushBatch(); /* must not draw a batch queued against this texture */
	GLuint tex = (GLuint)iTexHandle;
	glDeleteTextures(1, &tex);
	if (m_GlBoundTex == iTexHandle)
		m_GlBoundTex = 0;
	if (m_WrapTex == iTexHandle) m_WrapTex = 0;
	if (m_FiltTex == iTexHandle) m_FiltTex = 0;
	/* The handle may be reused by a different texture object: forget params. */
	if (iTexHandle < m_TexWrapParam.size()) m_TexWrapParam[iTexHandle] = 0;
	if (iTexHandle < m_TexFiltParam.size()) m_TexFiltParam[iTexHandle] = 0;
	m_Textures.erase(iTexHandle);
}

void RageDisplay_Vita::ClearAllTextures()
{
	/* Upstream contract (RageDisplay_Legacy::ClearAllTextures): unbind all
	 * texture units so the next draw starts from a clean binding state.
	 * Must NOT delete textures here: callers (Sprite, ActorMultiVertex,
	 * BitmapText, NoteDisplay, ...) invoke this before EVERY draw and bind
	 * their own texture right after. Deleting live textures makes vitaGL
	 * evict them under CDRAM pressure; the engine never re-uploads, so the
	 * evicted quads sample GPU garbage (rainbow strips, noisy glyphs).
	 *
	 * The "no texture" state is only marked here and applied on the next
	 * SetTexture()/DrawTris(), so an immediately following SetTexture() of
	 * the texture that is already bound does not toggle GL_TEXTURE_2D. */
	m_TexOffPending = true;
	m_CurTex = 0;
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
	/* iTexture==0 means "no texture": disable GL_TEXTURE_2D so the vertex
	 * colour passes through unmodified. */
	m_TexOffPending = false;
	ApplyTextureState(iTexture);
	m_CurTex = iTexture;
}

void RageDisplay_Vita::SetTextureMode(TextureUnit /*tu*/, TextureMode tm)
{
	switch (tm)
	{
	case TextureMode_Modulate:
		if (!m_EnvModeValid || m_EnvMode != GL_MODULATE)
		{
			FlushBatch();
			glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
			m_EnvMode = GL_MODULATE; m_EnvModeValid = true;
		}
		break;
	case TextureMode_Add:
		if (!m_EnvModeValid || m_EnvMode != GL_ADD)
		{
			FlushBatch();
			glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_ADD);
			m_EnvMode = GL_ADD; m_EnvModeValid = true;
		}
		break;
	case TextureMode_Glow:
		// No combiner on stage 1: emulate glow via additive blending.
		FlushBatch();
		glBlendFunc(GL_SRC_ALPHA, GL_ONE);
		m_BlendValid = false; /* changed behind SetBlendMode's back */
		break;
	default:
		break;
	}
}

void RageDisplay_Vita::SetTextureWrapping(TextureUnit /*tu*/, bool b)
{
	if (m_CurTex == 0)
		return; /* untextured draw: sampler state is meaningless */
	int wrap = b ? GL_REPEAT : GL_CLAMP_TO_EDGE;
	const uint8_t st = b ? 1 : 2;
	uint8_t &slot = TexParamSlot(m_TexWrapParam, m_CurTex);
	if (slot == st)
		return; /* this texture object already has these params */
	FlushBatch(); /* texparams apply to already-drawn quads of this texture */
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
	slot = st;
	m_WrapTex = m_CurTex; m_WrapVal = wrap;
}

void RageDisplay_Vita::SetTextureFiltering(TextureUnit /*tu*/, bool b)
{
	if (m_CurTex == 0)
		return; /* untextured draw: sampler state is meaningless */
	// No mipmaps on stage 1.
	int f = b ? GL_LINEAR : GL_NEAREST;
	const uint8_t st = b ? 1 : 2;
	uint8_t &slot = TexParamSlot(m_TexFiltParam, m_CurTex);
	if (slot == st)
		return; /* this texture object already has these params */
	FlushBatch(); /* texparams apply to already-drawn quads of this texture */
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, f);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, f);
	slot = st;
	m_FiltTex = m_CurTex; m_FiltVal = f;
}

void RageDisplay_Vita::SetBlendMode(BlendMode mode)
{
	int srcRGB = GL_SRC_ALPHA, dstRGB = GL_ONE_MINUS_SRC_ALPHA;
	int eq = GL_FUNC_ADD;
	switch (mode)
	{
	case BLEND_NORMAL:        srcRGB = GL_SRC_ALPHA; dstRGB = GL_ONE_MINUS_SRC_ALPHA; break;
	case BLEND_ADD:           srcRGB = GL_SRC_ALPHA; dstRGB = GL_ONE; break;
	case BLEND_SUBTRACT:      eq = GL_FUNC_REVERSE_SUBTRACT; srcRGB = GL_SRC_ALPHA; dstRGB = GL_ONE_MINUS_SRC_ALPHA; break;
	case BLEND_MODULATE:      srcRGB = GL_ZERO; dstRGB = GL_SRC_COLOR; break;
	case BLEND_COPY_SRC:      srcRGB = GL_ONE; dstRGB = GL_ZERO; break;
	case BLEND_ALPHA_MASK:    srcRGB = GL_ZERO; dstRGB = GL_ONE; break;
	case BLEND_ALPHA_KNOCK_OUT: srcRGB = GL_ZERO; dstRGB = GL_ONE; break;
	case BLEND_ALPHA_MULTIPLY: srcRGB = GL_SRC_ALPHA; dstRGB = GL_ZERO; break;
	case BLEND_WEIGHTED_MULTIPLY: srcRGB = GL_DST_COLOR; dstRGB = GL_SRC_COLOR; break;
	case BLEND_INVERT_DEST:   eq = GL_FUNC_SUBTRACT; srcRGB = GL_ONE; dstRGB = GL_ONE; break;
	case BLEND_NO_EFFECT:     srcRGB = GL_ZERO; dstRGB = GL_ONE; break;
	default: break;
	}

	if (m_BlendValid && m_BlendSrc == srcRGB && m_BlendDst == dstRGB && m_BlendEq == eq)
		return; /* identical state: skip glEnable/glBlendEquation/glBlendFunc */

	FlushBatch();
	glEnable(GL_BLEND);
	if (!m_BlendValid || m_BlendEq != eq)
		glBlendEquation(eq);
	glBlendFunc(srcRGB, dstRGB);
	m_BlendSrc = srcRGB; m_BlendDst = dstRGB; m_BlendEq = eq; m_BlendValid = true;
}

void RageDisplay_Vita::SetZWrite(bool b)
{
	m_bZWrite = b;
	int mask = b ? 1 : 0;
	if (m_ZWriteValid && m_ZWriteMask == mask)
		return;
	FlushBatch();
	glDepthMask(b ? GL_TRUE : GL_FALSE);
	m_ZWriteMask = mask; m_ZWriteValid = true;
}

void RageDisplay_Vita::SetZTestMode(ZTestMode mode)
{
	m_bZTest = (mode != ZTEST_OFF);
	int func = GL_ALWAYS;
	switch (mode)
	{
	case ZTEST_OFF:           func = GL_ALWAYS; break;
	case ZTEST_WRITE_ON_PASS: func = GL_LEQUAL; break;
	case ZTEST_WRITE_ON_FAIL: func = GL_GREATER; break;
	default: break;
	}
	if (m_ZTestValid && m_ZTestFunc == func)
		return;
	FlushBatch();
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(func);
	m_ZTestFunc = func; m_ZTestValid = true;
}

void RageDisplay_Vita::SetZBias(float f)
{
	/* Actor::SetGlobalRenderStates() re-issues the bias for every draw, so
	 * cache it: an unconditional glDepthRange would flush every batch. */
	float fNear = SCALE(f, 0.0f, 1.0f, 0.05f, 0.0f);
	float fFar = SCALE(f, 0.0f, 1.0f, 1.0f, 0.95f);
	if (m_ZBiasValid && m_ZBiasNear == fNear && m_ZBiasFar == fFar)
		return;
	FlushBatch();
	glDepthRange(fNear, fFar);
	m_ZBiasNear = fNear; m_ZBiasFar = fFar; m_ZBiasValid = true;
}

void RageDisplay_Vita::ClearZBuffer()
{
	FlushBatch();
	glClear(GL_DEPTH_BUFFER_BIT);
}

void RageDisplay_Vita::SetCullMode(CullMode mode)
{
	/* Also re-issued for every draw (see SetZBias): cache it. */
	if (m_CullValid && m_CullMode == (int)mode)
		return;
	FlushBatch();
	switch (mode)
	{
	case CULL_BACK:  glEnable(GL_CULL_FACE); glCullFace(GL_BACK); break;
	case CULL_FRONT: glEnable(GL_CULL_FACE); glCullFace(GL_FRONT); break;
	case CULL_NONE:
	default:         glDisable(GL_CULL_FACE); break;
	}
	m_CullMode = (int)mode; m_CullValid = true;
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
	if (b == m_LightingOn)
		return;
	FlushBatch();
	if (b)
		glEnable(GL_LIGHTING);
	else
		glDisable(GL_LIGHTING);
	m_LightingOn = b;
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

inline void RageDisplay_Vita::BakeVert(BatchVertex &dst, const RageSpriteVertex &sv,
	const float *mv, const float *tm)
{
	const float px = sv.p[0], py = sv.p[1], pz = sv.p[2];
	const float tx = sv.t[0], ty = sv.t[1];
	dst.x = px * mv[0] + py * mv[4] + pz * mv[8]  + mv[12];
	dst.y = px * mv[1] + py * mv[5] + pz * mv[9]  + mv[13];
	dst.z = px * mv[2] + py * mv[6] + pz * mv[10] + mv[14];
	dst.u = tx * tm[0] + ty * tm[4] + tm[12];
	dst.v = tx * tm[1] + ty * tm[5] + tm[13];
	dst.r = sv.c.r; dst.g = sv.c.g; dst.b = sv.c.b; dst.a = sv.c.a;
}

void RageDisplay_Vita::DrawTris(const RageSpriteVertex v[], const uint16_t *indices, int iNumIndices)
{
	// RageSpriteVertex is tightly packed (p[3f] n[3f] c[BGRA bytes] t[2f],
	// 36 bytes). Instead of one glDrawElements per primitive - which costs
	// roughly 250us per call in vitaGL's immediate mode - every triangle is
	// baked into an interleaved array (modelview and texture transform
	// applied on the CPU, GL keeps identity for them) and the whole run is
	// submitted by FlushBatch() as a single glDrawArrays.
	COMPILE_ASSERT(sizeof(RageSpriteVertex) == 36);
	COMPILE_ASSERT(offsetof(RageSpriteVertex, p) == 0);
	COMPILE_ASSERT(offsetof(RageSpriteVertex, n) == 12);
	COMPILE_ASSERT(offsetof(RageSpriteVertex, c) == 24);
	COMPILE_ASSERT(offsetof(RageSpriteVertex, t) == 28);

	if (iNumIndices <= 0)
		return;

	m_DrawTris += iNumIndices / 3;

	SendCurrentMatrices(); /* submits a stale batch when the matrices changed */

	if (m_TexOffPending)
	{
		ApplyTextureState(0);
		m_TexOffPending = false;
	}

	if (m_Batch.size() + (size_t)iNumIndices > VITA_BATCH_MAX)
		FlushBatch();

	const float *mv = m_LastMV;
	const float *tm = m_LastTex;

	size_t at = m_Batch.size();
	m_Batch.resize(at + (size_t)iNumIndices);
	BatchVertex *dst = &m_Batch[at];
	for (int i = 0; i < iNumIndices; ++i)
		BakeVert(dst[i], v[indices[i]], mv, tm);
}

void RageDisplay_Vita::DrawQuadsInternal(const RageSpriteVertex v[], int iNumVerts)
{
	/* One quad is 4 source vertices but 6 batched vertices (0,1,2,0,2,3).
	 * Baking the shared corners twice wastes a third of the CPU transform:
	 * bake the 4 corners once, then reuse them for the two shared slots. */
	const int iNumQuads = iNumVerts / 4;
	if (iNumQuads <= 0)
		return;

	m_DrawTris += iNumQuads * 2;

	SendCurrentMatrices();

	if (m_TexOffPending)
	{
		ApplyTextureState(0);
		m_TexOffPending = false;
	}

	const size_t iNumIndices = (size_t)iNumQuads * 6;
	if (m_Batch.size() + iNumIndices > VITA_BATCH_MAX)
		FlushBatch();

	const float *mv = m_LastMV;
	const float *tm = m_LastTex;

	size_t at = m_Batch.size();
	m_Batch.resize(at + iNumIndices);
	BatchVertex *dst = &m_Batch[at];
	for (int q = 0, o = 0; q < iNumQuads; ++q, o += 6)
	{
		const RageSpriteVertex *sv = &v[q * 4];
		BatchVertex *b = &dst[o];
		BakeVert(b[0], sv[0], mv, tm);
		BakeVert(b[1], sv[1], mv, tm);
		BakeVert(b[2], sv[2], mv, tm);
		BakeVert(b[5], sv[3], mv, tm);
		b[3] = b[0]; /* tri 1: 0,1,2 */
		b[4] = b[2]; /* tri 2: 0,2,3 */
	}
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
	if (m_TexOffPending)
	{
		ApplyTextureState(0);
		m_TexOffPending = false;
	}
	FlushBatch();
	SendCurrentMatrices();
	EnsureGLMatrices(true);
	glVertexPointer(3, GL_FLOAT, 0, &m.pos[0]);
	glTexCoordPointer(2, GL_FLOAT, 0, &m.tex[0]);
	glNormalPointer(GL_FLOAT, 0, &m.norm[0]);
	glDisableClientState(GL_COLOR_ARRAY);
	glColor4f(1, 1, 1, 1);
	glDrawElements(GL_TRIANGLES, (GLsizei)m.idx.size(), GL_UNSIGNED_SHORT, &m.idx[0]);
	glEnableClientState(GL_COLOR_ARRAY);
	++m_DrawCalls;
}

void RageDisplay_Vita::DrawLineStripInternal(const RageSpriteVertex v[], int iNumVerts, float LineWidth)
{
	// Lines are untextured and drawn with the real matrices (they are not
	// routed through the batch, which bakes the modelview on the CPU).
	FlushBatch();
	SendCurrentMatrices();
	EnsureGLMatrices(true);
	ApplyTextureState(0);
	m_CurTex = 0;

	const GLsizei stride = (GLsizei)sizeof(RageSpriteVertex);
	const uint8_t *base = reinterpret_cast<const uint8_t *>(v);
	if (m_Col.size() < (size_t)iNumVerts * 4) { m_Col.resize((size_t)iNumVerts * 4); }
	for (int i = 0; i < iNumVerts; ++i)
	{
		m_Col[i * 4 + 0] = v[i].c.r; m_Col[i * 4 + 1] = v[i].c.g; m_Col[i * 4 + 2] = v[i].c.b; m_Col[i * 4 + 3] = v[i].c.a;
	}
	glLineWidth(LineWidth);
	static const float s_zeroTex[2] = { 0.0f, 0.0f };
	glTexCoordPointer(2, GL_FLOAT, 0, s_zeroTex);
	glVertexPointer(3, GL_FLOAT, stride, base + offsetof(RageSpriteVertex, p));
	glColorPointer(4, GL_UNSIGNED_BYTE, 0, &m_Col[0]);
	glDrawArrays(GL_LINE_STRIP, 0, iNumVerts);
	++m_DrawCalls;
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
