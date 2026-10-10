#ifndef RAGE_DISPLAY_VITA_H
#define RAGE_DISPLAY_VITA_H

#include "RageDisplay.h"

#include <cstdint>
#include <map>
#include <set>
#include <vector>

// RageDisplay_Vita: minimal fixed-function renderer for PS Vita via vitaGL.
//
// The upstream GLES2 renderer was never completed (texture upload and all
// draw paths are empty TODO stubs) and the Legacy OGL renderer needs GLEW,
// ARB shaders, paletted textures and GLU — none of which exist on Vita.
// This class therefore implements the RageDisplay interface directly on top
// of vitaGL's fixed-function pipeline (client arrays + glDrawArrays):
//  - every surface is converted to RGBA8888 on upload (one code path),
//  - all primitives are triangulated in a scratch buffer (no GL_QUADS),
//  - multitexture/lighting/render-targets are stubbed to safe no-ops.
class RageDisplay_Vita : public RageDisplay
{
public:
	RageDisplay_Vita();
	~RageDisplay_Vita() override;

	RString Init(const VideoModeParams &p, bool bAllowUnacceleratedRenderer) override;
	RString GetApiDescription() const override { return "Vita (vitaGL)"; }
	void GetDisplaySpecs(DisplaySpecs &out) const override;
	const RagePixelFormatDesc *GetPixelFormatDesc(RagePixelFormat pf) const override;

	bool BeginFrame() override;
	void EndFrame() override;
	ActualVideoModeParams GetActualVideoModeParams() const override;
	RString TryVideoMode(const VideoModeParams &p, bool &bNewDeviceOut) override;

	void SetBlendMode(BlendMode mode) override;
	bool SupportsTextureFormat(RagePixelFormat pixfmt, bool realtime = false) override;
	bool SupportsPerVertexMatrixScale() override { return false; }
	std::uintptr_t CreateTexture(
		RagePixelFormat pixfmt,
		RageSurface *img,
		bool bGenerateMipMaps) override;
	void UpdateTexture(
		std::uintptr_t iTexHandle,
		RageSurface *img,
		int xoffset, int yoffset, int width, int height) override;
	void DeleteTexture(std::uintptr_t iTexHandle) override;
	void ClearAllTextures() override;
	RageTextureLock *CreateTextureLock() override;
	void Lock(std::uintptr_t iTexHandle, RageSurface *pSurface);
	void Unlock(RageSurface *pSurface, bool bChanged = true);
	int GetNumTextureUnits() override { return 1; }
	void SetTexture(TextureUnit tu, std::uintptr_t iTexture) override;
	void SetTextureMode(TextureUnit tu, TextureMode tm) override;
	void SetTextureWrapping(TextureUnit tu, bool b) override;
	int GetMaxTextureSize() const override { return 2048; }
	void SetTextureFiltering(TextureUnit tu, bool b) override;

	bool IsZWriteEnabled() const override { return m_bZWrite; }
	bool IsZTestEnabled() const override { return m_bZTest; }
	void SetZWrite(bool b) override;
	void SetZTestMode(ZTestMode mode) override;
	void SetZBias(float f) override;
	void ClearZBuffer() override;
	void SetCullMode(CullMode mode) override;
	void SetAlphaTest(bool b) override;
	void SetMaterial(
		const RageColor &emissive,
		const RageColor &ambient,
		const RageColor &diffuse,
		const RageColor &specular,
		float shininess) override;
	void SetLighting(bool b) override;
	void SetLightOff(int index) override;
	void SetLightDirectional(
		int index,
		const RageColor &ambient,
		const RageColor &diffuse,
		const RageColor &specular,
		const RageVector3 &dir) override;
	void SetSphereEnvironmentMapping(TextureUnit tu, bool b) override;
	void SetCelShaded(int stage) override;

	void SetLineWidth(float fWidth) override;
	void SetPolygonMode(PolygonMode pm) override;

	RageCompiledGeometry *CreateCompiledGeometry() override;
	void DeleteCompiledGeometry(RageCompiledGeometry *p) override;

	RageSurface *CreateScreenshot() override;

protected:
	void DrawQuadsInternal(const RageSpriteVertex v[], int iNumVerts) override;
	void DrawQuadStripInternal(const RageSpriteVertex v[], int iNumVerts) override;
	void DrawFanInternal(const RageSpriteVertex v[], int iNumVerts) override;
	void DrawStripInternal(const RageSpriteVertex v[], int iNumVerts) override;
	void DrawTrianglesInternal(const RageSpriteVertex v[], int iNumVerts) override;
	void DrawCompiledGeometryInternal(const RageCompiledGeometry *p, int iMeshIndex) override;
	void DrawLineStripInternal(const RageSpriteVertex v[], int iNumVerts, float LineWidth) override;
	void DrawSymmetricQuadStripInternal(const RageSpriteVertex v[], int iNumVerts) override;

private:
	void SendCurrentMatrices();
	void DrawTris(const RageSpriteVertex v[], const std::uint16_t *indices, int iNumIndices);

	class LowLevelWindow *m_pWind;
	bool m_bZWrite;
	bool m_bZTest;
	std::set<std::uintptr_t> m_Textures;
	std::map<RageSurface *, std::uintptr_t> m_Locked;

	// Scratch triangulation buffers (grown on demand, never shrunk).
	std::vector<float> m_Pos;
	std::vector<float> m_Tex;
	std::vector<float> m_Norm;
	std::vector<std::uint8_t> m_Col;
	std::vector<std::uint16_t> m_Idx;
};

#endif
