#ifndef RAGE_DISPLAY_VITA_H
#define RAGE_DISPLAY_VITA_H

#include "RageDisplay.h"

#include <map>
#include <set>
#include <vector>

// RageDisplay_Vita: minimal fixed-function renderer for PS Vita via vitaGL
// (StepMania 5.0). Same design as the 5.1 port: RGBA8888 normalization,
// CPU triangulation, safe no-op stubs for lights/models/render-targets.
class RageDisplay_Vita : public RageDisplay
{
public:
	RageDisplay_Vita();
	virtual ~RageDisplay_Vita();

	virtual RString Init(const VideoModeParams &p, bool bAllowUnacceleratedRenderer);
	virtual RString GetApiDescription() const { return "Vita (vitaGL)"; }
	virtual void GetDisplayResolutions(DisplayResolutions &out) const;
	virtual const RagePixelFormatDesc *GetPixelFormatDesc(RagePixelFormat pf) const;

	virtual bool BeginFrame();
	virtual void EndFrame();
	virtual VideoModeParams GetActualVideoModeParams() const;
	virtual RString TryVideoMode(const VideoModeParams &p, bool &bNewDeviceOut);

	virtual void SetBlendMode(BlendMode mode);
	virtual bool SupportsTextureFormat(RagePixelFormat pixfmt, bool realtime = false);
	virtual bool SupportsPerVertexMatrixScale() { return false; }
	virtual unsigned CreateTexture(
		RagePixelFormat pixfmt,
		RageSurface *img,
		bool bGenerateMipMaps);
	virtual void UpdateTexture(
		unsigned iTexHandle,
		RageSurface *img,
		int xoffset, int yoffset, int width, int height);
	virtual void DeleteTexture(unsigned iTexHandle);
	virtual void ClearAllTextures();
	virtual void Lock(unsigned iTexHandle, RageSurface *pSurface);
	virtual void Unlock(RageSurface *pSurface, bool bChanged = true);
	virtual int GetNumTextureUnits() { return 1; }
	virtual void SetTexture(TextureUnit tu, unsigned iTexture);
	virtual void SetTextureMode(TextureUnit tu, TextureMode tm);
	virtual void SetTextureWrapping(TextureUnit tu, bool b);
	virtual int GetMaxTextureSize() const { return 2048; }
	virtual void SetTextureFiltering(TextureUnit tu, bool b);

	virtual bool IsZWriteEnabled() const { return m_bZWrite; }
	virtual bool IsZTestEnabled() const { return m_bZTest; }
	virtual void SetZWrite(bool b);
	virtual void SetZTestMode(ZTestMode mode);
	virtual void SetZBias(float f);
	virtual void ClearZBuffer();
	virtual void SetCullMode(CullMode mode);
	virtual void SetAlphaTest(bool b);
	virtual void SetMaterial(
		const RageColor &emissive,
		const RageColor &ambient,
		const RageColor &diffuse,
		const RageColor &specular,
		float shininess);
	virtual void SetLighting(bool b);
	virtual void SetLightOff(int index);
	virtual void SetLightDirectional(
		int index,
		const RageColor &ambient,
		const RageColor &diffuse,
		const RageColor &specular,
		const RageVector3 &dir);
	virtual void SetSphereEnvironmentMapping(TextureUnit tu, bool b);
	virtual void SetCelShaded(int stage);

	virtual void SetLineWidth(float fWidth);
	virtual void SetPolygonMode(PolygonMode pm);

	virtual RageCompiledGeometry *CreateCompiledGeometry();
	virtual void DeleteCompiledGeometry(RageCompiledGeometry *p);

	virtual RageSurface *CreateScreenshot();

protected:
	virtual void DrawQuadsInternal(const RageSpriteVertex v[], int iNumVerts);
	virtual void DrawQuadStripInternal(const RageSpriteVertex v[], int iNumVerts);
	virtual void DrawFanInternal(const RageSpriteVertex v[], int iNumVerts);
	virtual void DrawStripInternal(const RageSpriteVertex v[], int iNumVerts);
	virtual void DrawTrianglesInternal(const RageSpriteVertex v[], int iNumVerts);
	virtual void DrawCompiledGeometryInternal(const RageCompiledGeometry *p, int iMeshIndex);
	virtual void DrawLineStripInternal(const RageSpriteVertex v[], int iNumVerts, float LineWidth);
	virtual void DrawSymmetricQuadStripInternal(const RageSpriteVertex v[], int iNumVerts);

private:
	void SendCurrentMatrices();
	void DrawTris(const RageSpriteVertex v[], const uint16_t *indices, int iNumIndices);
	void ApplyTextureState(unsigned iTexture);
	void EnsureGLMatrices(bool bReal);
	void FlushBatch();

	class LowLevelWindow *m_pWind;
	bool m_bZWrite;
	bool m_bZTest;
	std::set<unsigned> m_Textures;
	std::map<RageSurface *, unsigned> m_Locked;

	std::vector<float> m_Pos;
	std::vector<float> m_Tex;
	std::vector<float> m_Norm;
	std::vector<uint8_t> m_Col;
	std::vector<uint16_t> m_Idx;
	std::vector<uint8_t> m_UploadBuf;

	int m_DrawCalls;
	int m_DrawTris;

	/* Perf instrumentation (Build 33): wall-clock frame breakdown plus
	 * texture-upload / state-churn counters. Logged every 60 frames by
	 * EndFrame; all reset on log. m_FrameStartUs is stamped in BeginFrame. */
	long m_TexUploads;
	long m_TexUploadBytes;
	int m_TexBinds;
	int m_TexToggles;   /* glEnable/Disable(GL_TEXTURE_2D) transitions */
	bool m_TexOn;
	/* "No texture" (iTexture==0) is rendered by disabling GL_TEXTURE_2D, but
	 * lazily: ClearAllTextures() only sets m_TexOffPending, SetTexture()
	 * clears it and applies the requested state, DrawTris() applies it when
	 * still pending. A run of textured draws (each of which calls
	 * ClearAllTextures first) therefore never toggles the texture stage. */
	bool m_TexOffPending;
	unsigned m_GlBoundTex; /* handle actually bound to GL_TEXTURE_2D */
	int m_BatchFlushes;

	/* Last matrices pushed to GL. 2D UI almost always repeats the same
	 * projection/modelview/texture matrices, so skipping redundant
	 * glLoadMatrixf calls removes several GL calls per draw. */
	float m_LastProj[16];
	float m_LastMV[16];
	float m_LastTex[16];
	bool m_MatValid;
	bool m_MatDirty;   /* logical matrices changed since last GL upload */
	bool m_ProjDirty;  /* projection changed since last GL upload */
	int  m_GLMatReal;  /* -1 unknown, 0 = batch layout, 1 = real matrices */
	bool m_LightingOn;

	/* Vertex batching. DrawTris() bakes the modelview and texture transforms
	 * on the CPU into one interleaved array and submits it with a single
	 * glDrawArrays per state group. vitaGL's immediate mode costs roughly
	 * 250us per draw call, so collapsing the hundreds of two-triangle UI
	 * quads into a handful of calls is the main menu FPS win. While a batch
	 * is open GL keeps the identity modelview/texture matrices and the real
	 * projection (baked vertices are already in eye space). */
	struct BatchVertex
	{
		float x, y, z;
		float u, v;
		unsigned char r, g, b, a;
	};
	std::vector<BatchVertex> m_Batch;

	/* Bake one RageSpriteVertex into the batch: modelview and texture
	 * transforms applied on the CPU (GL keeps identity for them). */
	static void BakeVert(BatchVertex &dst, const RageSpriteVertex &sv,
		const float *mv, const float *tm);

	/* GL render-state cache. The engine re-issues the same states before
	 * every draw (Sprite::DrawPrimitives -> Actor::SetTextureRenderStates ->
	 * SetTextureWrapping/Filtering, plus SetTextureMode/SetBlendMode/Z*). On
	 * vitaGL each redundant call is costly (texenv/texparam can re-patch the
	 * patched shader), so only forward actual changes. Texture params are
	 * remembered together with the texture they were applied to (m_WrapTex /
	 * m_FiltTex): a skip is only valid while that same texture is bound. */
	unsigned m_CurTex;
	unsigned m_WrapTex; int m_WrapVal;
	unsigned m_FiltTex; int m_FiltVal;
	/* Actor::Draw re-issues SetTextureWrapping/Filtering for every actor, which
	 * used to flush the batch and re-send four glTexParameteri calls per actor.
	 * Remember what was applied to each texture object instead; indexed by
	 * texture handle, 0 = unknown. Cleared on device reset/texture deletion. */
	std::vector<uint8_t> m_TexWrapParam;
	std::vector<uint8_t> m_TexFiltParam;
	int m_EnvMode; bool m_EnvModeValid;
	int m_BlendSrc, m_BlendDst, m_BlendEq; bool m_BlendValid;
	int m_ZTestFunc; bool m_ZTestValid;
	int m_ZWriteMask; bool m_ZWriteValid;
	float m_ZBiasNear, m_ZBiasFar; bool m_ZBiasValid;
	int m_CullMode; bool m_CullValid;
	void ResetStateCache();
	long long m_FrameStartUs;
	long long m_DrawEmitUs;   /* BeginFrame -> start of present (CPU emit) */
	long long m_PresentUs;    /* time inside EndFrame present path */
	long long m_DrawSetupUs;  /* wall in DrawTris before glDrawElements */
	long long m_DrawSubmitUs; /* wall inside glDrawElements (GPU back-pressure) */
};

#endif
