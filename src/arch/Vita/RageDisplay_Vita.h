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
};

#endif
