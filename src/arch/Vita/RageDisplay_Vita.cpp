#include "global.h"
#include "RageDisplay_Vita.h"
#include "RageDisplay.h"
#include "RageLog.h"
#include "RageSurface.h"
#include "RageSurfaceUtils.h"
#include "RageMath.h"
#include "RageTypes.h"
#include "RageUtil.h"
#include "DisplaySpec.h"
#include "ModelTypes.h"
#include "arch/LowLevelWindow/LowLevelWindow.h"

#include <cstring>
#include <vector>

#ifdef VITA
#if __has_include(<vitaGL.h>)
#include <vitaGL.h>
#endif
#include <psp2/display.h>
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

	// All uploads are normalized to this format (matches GL_RGBA/UBYTE bytes).
	const std::uint32_t VITA_TEX_RMASK = 0xFF000000;
	const std::uint32_t VITA_TEX_GMASK = 0x00FF0000;
	const std::uint32_t VITA_TEX_BMASK = 0x0000FF00;
	const std::uint32_t VITA_TEX_AMASK = 0x000000FF;

	// Software copy of a mesh for DrawCompiledGeometryInternal.
	struct VitaCompiledGeometry : public RageCompiledGeometry
	{
		struct Mesh
		{
			std::vector<float> pos;   // xyz per vert
			std::vector<float> norm;  // xyz per vert
			std::vector<float> tex;   // uv per vert
			std::vector<std::uint16_t> idx;
		};
		std::vector<Mesh> meshes;

		void Allocate(const std::vector<msMesh> &vMeshes) override
		{
			meshes.resize(vMeshes.size());
			Change(vMeshes);
		}
		void Change(const std::vector<msMesh> &vMeshes) override
		{
			for (std::size_t m = 0; m < vMeshes.size() && m < meshes.size(); ++m)
			{
				const msMesh &src = vMeshes[m];
				Mesh &dst = meshes[m];
				dst.pos.resize(src.Vertices.size() * 3);
				dst.norm.resize(src.Vertices.size() * 3);
				dst.tex.resize(src.Vertices.size() * 2);
				for (std::size_t i = 0; i < src.Vertices.size(); ++i)
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
				for (std::size_t i = 0; i < src.Triangles.size(); ++i)
				{
					dst.idx[i * 3 + 0] = src.Triangles[i].nVertexIndices[0];
					dst.idx[i * 3 + 1] = src.Triangles[i].nVertexIndices[1];
					dst.idx[i * 3 + 2] = src.Triangles[i].nVertexIndices[2];
				}
			}
		}
		void Draw(int iMeshIndex) const override
		{
			// Driven by RageDisplay_Vita::DrawCompiledGeometryInternal.
			(void)iMeshIndex;
		}
	};
}

RageDisplay_Vita::RageDisplay_Vita()
	: m_pWind(nullptr), m_bZWrite(false), m_bZTest(false)
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
	glDisable(GL_ALPHA_TEST);
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
	if (m_pWind == nullptr)
		m_pWind = LowLevelWindow::Create();
	return m_pWind->TryVideoMode(p, bNewDeviceOut);
}

void RageDisplay_Vita::GetDisplaySpecs(DisplaySpecs &out) const
{
	DisplayMode mode{ 960, 544, 60.0 };
	out.insert(DisplaySpec("Vita LCD", "PS Vita LCD", mode));
}

const RageDisplay::RagePixelFormatDesc *RageDisplay_Vita::GetPixelFormatDesc(RagePixelFormat pf) const
{
	ASSERT(pf < NUM_RagePixelFormat);
	return &PIXEL_FORMAT_DESC_VITA[pf];
}

ActualVideoModeParams RageDisplay_Vita::GetActualVideoModeParams() const
{
	return m_pWind->GetActualVideoModeParams();
}

bool RageDisplay_Vita::BeginFrame()
{
	ActualVideoModeParams avmp = m_pWind->GetActualVideoModeParams();
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
	// Frame heartbeat: distinguishes "loop dead" from "loop alive, static
	// content". Logs every 600 presented frames (~10s at 60fps).
	{
		static int s_nFrames = 0;
		++s_nFrames;
		if (s_nFrames == 1 || s_nFrames % 600 == 0)
		{
			GLenum e = glGetError();
			LOG->Info("Vita: frame %d, glGetError=0x%x", s_nFrames, (unsigned)e);
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

std::uintptr_t RageDisplay_Vita::CreateTexture(
	RagePixelFormat /*pixfmt*/,
	RageSurface *img,
	bool /*bGenerateMipMaps*/)
{
	if (img == nullptr || img->pixels == nullptr)
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
	delete conv;

	m_Textures.insert((std::uintptr_t)tex);
	return (std::uintptr_t)tex;
}

void RageDisplay_Vita::UpdateTexture(
	std::uintptr_t iTexHandle,
	RageSurface *img,
	int xoffset, int yoffset, int width, int height)
{
	if (iTexHandle == 0 || img == nullptr || img->pixels == nullptr)
		return;

	RageSurface *conv = CreateSurface(img->w, img->h, 32,
		VITA_TEX_RMASK, VITA_TEX_GMASK, VITA_TEX_BMASK, VITA_TEX_AMASK);
	RageSurfaceUtils::Blit(img, conv, img->w, img->h);

	glBindTexture(GL_TEXTURE_2D, (GLuint)iTexHandle);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexSubImage2D(GL_TEXTURE_2D, 0, xoffset, yoffset, width, height,
		GL_RGBA, GL_UNSIGNED_BYTE, conv->pixels);
	delete conv;
}

void RageDisplay_Vita::DeleteTexture(std::uintptr_t iTexHandle)
{
	if (iTexHandle == 0)
		return;
	GLuint tex = (GLuint)iTexHandle;
	glDeleteTextures(1, &tex);
	m_Textures.erase(iTexHandle);
}

void RageDisplay_Vita::ClearAllTextures()
{
	for (std::uintptr_t h : m_Textures)
	{
		GLuint tex = (GLuint)h;
		glDeleteTextures(1, &tex);
	}
	m_Textures.clear();
	m_Locked.clear();
}

void RageDisplay_Vita::Lock(std::uintptr_t iTexHandle, RageSurface *pSurface)
{
	m_Locked[pSurface] = iTexHandle;
}

void RageDisplay_Vita::Unlock(RageSurface *pSurface, bool bChanged)
{
	auto it = m_Locked.find(pSurface);
	if (it == m_Locked.end())
		return;
	if (bChanged && pSurface != nullptr)
		UpdateTexture(it->second, pSurface, 0, 0, pSurface->w, pSurface->h);
	m_Locked.erase(it);
}

namespace
{
	struct RageTextureLock_Vita : public RageTextureLock
	{
		RageDisplay_Vita *m_pDisplay;
		std::uintptr_t m_iHandle;
		RageTextureLock_Vita(RageDisplay_Vita *pDisplay) : m_pDisplay(pDisplay), m_iHandle(0) {}
		void Lock(std::uintptr_t iTexHandle, RageSurface *pSurface) override
		{
			m_iHandle = iTexHandle;
			m_pDisplay->Lock(iTexHandle, pSurface);
		}
		void Unlock(RageSurface *pSurface, bool bChanged = true) override
		{
			m_pDisplay->Unlock(pSurface, bChanged);
		}
	};
}

RageTextureLock *RageDisplay_Vita::CreateTextureLock()
{
	return new RageTextureLock_Vita(this);
}

void RageDisplay_Vita::SetTexture(TextureUnit tu, std::uintptr_t iTexture)
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
	int srcA = GL_ONE, dstA = GL_ONE_MINUS_SRC_ALPHA;
	switch (mode)
	{
	case BLEND_NORMAL:        srcRGB = GL_SRC_ALPHA; dstRGB = GL_ONE_MINUS_SRC_ALPHA; break;
	case BLEND_ADD:           srcRGB = GL_SRC_ALPHA; dstRGB = GL_ONE; break;
	case BLEND_SUBTRACT:      glBlendEquation(GL_FUNC_REVERSE_SUBTRACT); srcRGB = GL_SRC_ALPHA; dstRGB = GL_ONE_MINUS_SRC_ALPHA; break;
	case BLEND_MODULATE:      srcRGB = GL_ZERO; dstRGB = GL_SRC_COLOR; break;
	case BLEND_COPY_SRC:      srcRGB = GL_ONE; dstRGB = GL_ZERO; srcA = GL_ONE; dstA = GL_ZERO; break;
	case BLEND_ALPHA_MASK:    srcRGB = GL_ZERO; dstRGB = GL_ONE; srcA = GL_ZERO; dstA = GL_SRC_ALPHA; break;
	case BLEND_ALPHA_KNOCK_OUT: srcRGB = GL_ZERO; dstRGB = GL_ONE; srcA = GL_ZERO; dstA = GL_ONE_MINUS_SRC_ALPHA; break;
	case BLEND_ALPHA_MULTIPLY: srcRGB = GL_SRC_ALPHA; dstRGB = GL_ZERO; break;
	case BLEND_WEIGHTED_MULTIPLY: srcRGB = GL_DST_COLOR; dstRGB = GL_SRC_COLOR; break;
	case BLEND_INVERT_DEST:   glBlendEquation(GL_FUNC_SUBTRACT); srcRGB = GL_ONE; dstRGB = GL_ONE; break;
	case BLEND_NO_EFFECT:     srcRGB = GL_ZERO; dstRGB = GL_ONE; srcA = GL_ZERO; dstA = GL_ONE; break;
	default: break;
	}
	if (mode != BLEND_SUBTRACT && mode != BLEND_INVERT_DEST)
		glBlendEquation(GL_FUNC_ADD);
	glBlendFunc(srcRGB, dstRGB);
	(void)srcA; (void)dstA;
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
	if (b)
	{
		glEnable(GL_ALPHA_TEST);
		glAlphaFunc(GL_GREATER, 0.0f);
	}
	else
	{
		glDisable(GL_ALPHA_TEST);
	}
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
	if (pm == POLYGON_FILL)
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	else
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
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
	ActualVideoModeParams avmp = m_pWind->GetActualVideoModeParams();
	RageSurface *img = CreateSurface(avmp.width, avmp.height, 32,
		VITA_TEX_RMASK, VITA_TEX_GMASK, VITA_TEX_BMASK, VITA_TEX_AMASK);
	glReadPixels(0, 0, avmp.width, avmp.height, GL_RGBA, GL_UNSIGNED_BYTE, img->pixels);
	// GL origin is bottom-left; RageSurface expects top-left: flip rows.
	const int rowBytes = avmp.width * 4;
	std::vector<std::uint8_t> row(rowBytes);
	for (int y = 0; y < avmp.height / 2; ++y)
	{
		std::uint8_t *a = img->pixels + y * rowBytes;
		std::uint8_t *b = img->pixels + (avmp.height - 1 - y) * rowBytes;
		memcpy(row.data(), a, rowBytes);
		memcpy(a, b, rowBytes);
		memcpy(b, row.data(), rowBytes);
	}
	return img;
}

// --- draw helpers ---

void RageDisplay_Vita::DrawTris(const RageSpriteVertex v[], const std::uint16_t *indices, int iNumIndices)
{
	SendCurrentMatrices();

	// Expand indexed triangles into a flat vertex soup (simplest correct path;
	// typical UI batches are small).
	std::size_t need = (std::size_t)iNumIndices;
	if (m_Pos.size() < need * 3) { m_Pos.resize(need * 3); m_Tex.resize(need * 2); m_Norm.resize(need * 3); m_Col.resize(need * 4); }
	for (int i = 0; i < iNumIndices; ++i)
	{
		const RageSpriteVertex &sv = v[indices[i]];
		m_Pos[i * 3 + 0] = sv.p[0]; m_Pos[i * 3 + 1] = sv.p[1]; m_Pos[i * 3 + 2] = sv.p[2];
		m_Tex[i * 2 + 0] = sv.t[0]; m_Tex[i * 2 + 1] = sv.t[1];
		m_Norm[i * 3 + 0] = sv.n[0]; m_Norm[i * 3 + 1] = sv.n[1]; m_Norm[i * 3 + 2] = sv.n[2];
		m_Col[i * 4 + 0] = sv.c.r; m_Col[i * 4 + 1] = sv.c.g; m_Col[i * 4 + 2] = sv.c.b; m_Col[i * 4 + 3] = sv.c.a;
	}
	glVertexPointer(3, GL_FLOAT, 0, m_Pos.data());
	glTexCoordPointer(2, GL_FLOAT, 0, m_Tex.data());
	glNormalPointer(GL_FLOAT, 0, m_Norm.data());
	glColorPointer(4, GL_UNSIGNED_BYTE, 0, m_Col.data());
	glDrawArrays(GL_TRIANGLES, 0, iNumIndices);
}

void RageDisplay_Vita::DrawQuadsInternal(const RageSpriteVertex v[], int iNumVerts)
{
	if (m_Idx.size() < (std::size_t)iNumVerts * 3 / 2)
		m_Idx.resize((std::size_t)iNumVerts * 3 / 2 + 6);
	int n = 0;
	for (int q = 0; q + 3 < iNumVerts + 1; q += 4)
	{
		m_Idx[n++] = (std::uint16_t)(q + 0);
		m_Idx[n++] = (std::uint16_t)(q + 1);
		m_Idx[n++] = (std::uint16_t)(q + 2);
		m_Idx[n++] = (std::uint16_t)(q + 0);
		m_Idx[n++] = (std::uint16_t)(q + 2);
		m_Idx[n++] = (std::uint16_t)(q + 3);
	}
	DrawTris(v, m_Idx.data(), n);
}

void RageDisplay_Vita::DrawQuadStripInternal(const RageSpriteVertex v[], int iNumVerts)
{
	if (m_Idx.size() < (std::size_t)iNumVerts * 3)
		m_Idx.resize((std::size_t)iNumVerts * 3);
	int n = 0;
	for (int i = 0; i + 3 < iNumVerts + 1; i += 2)
	{
		m_Idx[n++] = (std::uint16_t)(i + 0);
		m_Idx[n++] = (std::uint16_t)(i + 1);
		m_Idx[n++] = (std::uint16_t)(i + 2);
		m_Idx[n++] = (std::uint16_t)(i + 1);
		m_Idx[n++] = (std::uint16_t)(i + 3);
		m_Idx[n++] = (std::uint16_t)(i + 2);
	}
	DrawTris(v, m_Idx.data(), n);
}

void RageDisplay_Vita::DrawFanInternal(const RageSpriteVertex v[], int iNumVerts)
{
	if (m_Idx.size() < (std::size_t)iNumVerts * 3)
		m_Idx.resize((std::size_t)iNumVerts * 3);
	int n = 0;
	for (int i = 1; i + 1 < iNumVerts + 1; ++i)
	{
		m_Idx[n++] = 0;
		m_Idx[n++] = (std::uint16_t)i;
		m_Idx[n++] = (std::uint16_t)(i + 1);
	}
	DrawTris(v, m_Idx.data(), n);
}

void RageDisplay_Vita::DrawStripInternal(const RageSpriteVertex v[], int iNumVerts)
{
	if (m_Idx.size() < (std::size_t)iNumVerts * 3)
		m_Idx.resize((std::size_t)iNumVerts * 3);
	int n = 0;
	for (int i = 0; i + 2 < iNumVerts + 1; ++i)
	{
		if (i & 1)
		{
			m_Idx[n++] = (std::uint16_t)(i + 1);
			m_Idx[n++] = (std::uint16_t)(i + 0);
			m_Idx[n++] = (std::uint16_t)(i + 2);
		}
		else
		{
			m_Idx[n++] = (std::uint16_t)(i + 0);
			m_Idx[n++] = (std::uint16_t)(i + 1);
			m_Idx[n++] = (std::uint16_t)(i + 2);
		}
	}
	DrawTris(v, m_Idx.data(), n);
}

void RageDisplay_Vita::DrawTrianglesInternal(const RageSpriteVertex v[], int iNumVerts)
{
	// Already triangles: identity index.
	if (m_Idx.size() < (std::size_t)iNumVerts)
		m_Idx.resize((std::size_t)iNumVerts);
	for (int i = 0; i < iNumVerts; ++i)
		m_Idx[i] = (std::uint16_t)i;
	DrawTris(v, m_Idx.data(), iNumVerts);
}

void RageDisplay_Vita::DrawCompiledGeometryInternal(const RageCompiledGeometry *p, int iMeshIndex)
{
	const VitaCompiledGeometry *g = static_cast<const VitaCompiledGeometry *>(p);
	if (g == nullptr || iMeshIndex < 0 || (std::size_t)iMeshIndex >= g->meshes.size())
		return;
	const VitaCompiledGeometry::Mesh &m = g->meshes[(std::size_t)iMeshIndex];
	if (m.idx.empty())
		return;
	SendCurrentMatrices();
	glVertexPointer(3, GL_FLOAT, 0, m.pos.data());
	glTexCoordPointer(2, GL_FLOAT, 0, m.tex.data());
	glNormalPointer(GL_FLOAT, 0, m.norm.data());
	glDisableClientState(GL_COLOR_ARRAY);
	glColor4f(1, 1, 1, 1);
	glDrawElements(GL_TRIANGLES, (GLsizei)m.idx.size(), GL_UNSIGNED_SHORT, m.idx.data());
	glEnableClientState(GL_COLOR_ARRAY);
}

void RageDisplay_Vita::DrawLineStripInternal(const RageSpriteVertex v[], int iNumVerts, float LineWidth)
{
	SendCurrentMatrices();
	if (m_Pos.size() < (std::size_t)iNumVerts * 3) { m_Pos.resize((std::size_t)iNumVerts * 3); m_Col.resize((std::size_t)iNumVerts * 4); }
	for (int i = 0; i < iNumVerts; ++i)
	{
		m_Pos[i * 3 + 0] = v[i].p[0]; m_Pos[i * 3 + 1] = v[i].p[1]; m_Pos[i * 3 + 2] = v[i].p[2];
		m_Col[i * 4 + 0] = v[i].c.r; m_Col[i * 4 + 1] = v[i].c.g; m_Col[i * 4 + 2] = v[i].c.b; m_Col[i * 4 + 3] = v[i].c.a;
	}
	glLineWidth(LineWidth);
	glDisable(GL_TEXTURE_2D);
	glVertexPointer(3, GL_FLOAT, 0, m_Pos.data());
	glColorPointer(4, GL_UNSIGNED_BYTE, 0, m_Col.data());
	glDrawArrays(GL_LINE_STRIP, 0, iNumVerts);
	glEnable(GL_TEXTURE_2D);
}

void RageDisplay_Vita::DrawSymmetricQuadStripInternal(const RageSpriteVertex v[], int iNumVerts)
{
	// Same index pattern as the Legacy renderer ({1,3,0} {1,4,3} ...).
	int iNumPieces = (iNumVerts - 3) / 3;
	if (iNumPieces <= 0)
		return;
	if (m_Idx.size() < (std::size_t)iNumPieces * 12)
		m_Idx.resize((std::size_t)iNumPieces * 12);
	int n = 0;
	for (int i = 0; i < iNumPieces; ++i)
	{
		const std::uint16_t base[12] = {
			(std::uint16_t)(i * 3 + 1), (std::uint16_t)(i * 3 + 3), (std::uint16_t)(i * 3 + 0),
			(std::uint16_t)(i * 3 + 1), (std::uint16_t)(i * 3 + 4), (std::uint16_t)(i * 3 + 3),
			(std::uint16_t)(i * 3 + 1), (std::uint16_t)(i * 3 + 5), (std::uint16_t)(i * 3 + 4),
			(std::uint16_t)(i * 3 + 1), (std::uint16_t)(i * 3 + 2), (std::uint16_t)(i * 3 + 5) };
		for (int k = 0; k < 12; ++k)
			m_Idx[n++] = base[k];
	}
	DrawTris(v, m_Idx.data(), n);
}
