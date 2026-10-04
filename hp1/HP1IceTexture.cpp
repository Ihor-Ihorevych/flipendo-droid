#include "Precomp.h"
#include "HP1.h"
#include "Packages/Engine/Resources/Textures/UIceTexture.h"
#include <cmath>

// IceTexture (Fire.dll): the source texture refracted through a "glass" texture. Each output pixel takes a source pixel
// from its own row, shifted sideways by the glass value under it; one of the two layers pans (U/VPosition) according to
// PanningStyle. The output uses the source's palette. SurrealEngine's UIceTexture only copied the source unchanged.
//
// Timing: UTexture::Tick (Engine.dll 0x104217C0) calls ConstantTimeTick every tick when MaxFrameRate is 0, which steps
// the ice by a fixed 1/120 s (TIME_FrameRateSync, the default); TIME_RealTimeScroll steps by the elapsed time instead.
// Here both run once per texture frame (SurrealEngine's UTexture::Update schedules those).

namespace HP1
{
	namespace
	{
		uint8_t* MipData(UTexture* tex, int width, int height)
		{
			if (!tex || tex->UsedMipmaps.empty())
				return nullptr;
			UnrealMipmap& mip = tex->UsedMipmaps.front();
			if (mip.Width != width || mip.Height != height || mip.Data.size() < (size_t)width * height)
				return nullptr;
			return (uint8_t*)mip.Data.data();
		}
	}

	// IDA Fire.dll: ?MoveIcePosition@UIceTexture@@AAEXM@Z [HP1 Fire 0x10505B40]
	static void MoveIcePosition(UIceTexture* ice, float dt)
	{
		float hspeed = (float)(int8_t)(ice->HorizPanSpeed() + 0x80);
		float vspeed = (float)(int8_t)(ice->VertPanSpeed() + 0x80);
		float count = ice->MasterCount() + dt * 120.0f;
		ice->MasterCount() = count;
		ice->UDisplace() -= hspeed * dt + hspeed * dt;
		ice->VDisplace() += vspeed * dt + vspeed * dt;

		float freq = (float)(ice->Frequency() + 1);
		float amp = (float)(ice->Amplitude() + 1);
		switch (ice->PanningStyle())
		{
		case 0: // SLIDE_Linear
			ice->UPosition() = ice->UDisplace();
			ice->VPosition() = ice->VDisplace();
			break;
		case 1: // SLIDE_Circular
			ice->UPosition() = (float)(int)(amp * std::sin(freq * count * 0.0012f)) + ice->UDisplace();
			ice->VPosition() = (float)(int)(amp * std::cos(freq * count * 0.0012f)) + ice->VDisplace();
			break;
		case 2: // SLIDE_Gestation
			ice->UPosition() = (float)(int)(amp * std::sin(freq * count * 0.0012f)) + ice->UDisplace();
			ice->VPosition() = (float)(int)(amp * std::cos(freq * count * 0.0011f)) + ice->VDisplace();
			break;
		case 3: // SLIDE_WavyX
			ice->UPosition() = (float)(int)(amp * 0.5f * std::sin(freq * count * 0.0012f)) + ice->UDisplace();
			ice->VPosition() = ice->VDisplace();
			break;
		case 4: // SLIDE_WavyY
			ice->UPosition() = ice->UDisplace();
			ice->VPosition() = (float)(int)(amp * 0.5f * std::cos(freq * count * 0.0012f)) + ice->VDisplace();
			break;
		}
	}

	// MoveIce: the glass pans, the source stays. dest[y][x] = source[y][(glass[y+V][x+U] + x) & UMask]
	// IDA Fire.dll: ?BlitTexIce@UIceTexture@@AAEXXZ [HP1 Fire 0x10505E90]
	static void BlitTexIce(uint8_t* dest, const uint8_t* source, const uint8_t* glass, int width, int height, int u, int v)
	{
		int umask = width - 1, vmask = height - 1;
		for (int y = 0; y < height; y++)
		{
			const uint8_t* srow = source + y * width;
			const uint8_t* grow = glass + ((y + v) & vmask) * width;
			uint8_t* drow = dest + y * width;
			for (int x = 0; x < width; x++)
				drow[x] = srow[(uint8_t)(grow[(x + u) & umask] + x) & umask];
		}
	}

	// !MoveIce: the source pans, the glass stays. dest[y][x] = source[y+V][(glass[y][x] + x + U) & UMask]
	// IDA Fire.dll: ?BlitIceTex@UIceTexture@@AAEXXZ [HP1 Fire 0x10506210]
	static void BlitIceTex(uint8_t* dest, const uint8_t* source, const uint8_t* glass, int width, int height, int u, int v)
	{
		int umask = width - 1, vmask = height - 1;
		for (int y = 0; y < height; y++)
		{
			const uint8_t* srow = source + ((y + v) & vmask) * width;
			const uint8_t* grow = glass + y * width;
			uint8_t* drow = dest + y * width;
			for (int x = 0; x < width; x++)
				drow[x] = srow[(uint8_t)(grow[x] + x + u) & umask];
		}
	}

	// IDA Fire.dll: ?RenderIce@UIceTexture@@AAEXM@Z [HP1 Fire 0x1050A600]
	// IDA Fire.dll: ?ConstantTimeTick@UIceTexture@@UAEXXZ [HP1 Fire 0x1050A340]
	// IDA Fire.dll: ?Tick@UIceTexture@@UAEXM@Z [HP1 Fire 0x1050A4B0]
	// IDA Fire.dll: ?PostLoad@UIceTexture@@UAEXXZ [HP1 Fire 0x10509F20] (palette from the source; sizes must match.
	//   A source of another size is resampled once into a static LocalSource there; NOT ported, such ice stays as is)
	void UpdateIceTexture(UIceTexture* ice, float frameTime)
	{
		if (ice->UsedMipmaps.empty())
			return;
		UnrealMipmap& mip = ice->UsedMipmaps.front();
		int width = mip.Width, height = mip.Height;
		if (width < 8 || height < 8 || (width & (width - 1)) || (height & (height - 1)))
			return;

		UTexture* sourceTex = ice->SourceTexture();
		UTexture* glassTex = ice->GlassTexture();
		const uint8_t* source = sourceTex != ice ? MipData(sourceTex, width, height) : nullptr;
		const uint8_t* glass = glassTex != ice ? MipData(glassTex, width, height) : nullptr;
		if (!source || !glass)
			return;

		if (sourceTex != ice->OldSourceTex())
		{
			if (UBitmap* bitmap = UObject::TryCast<UBitmap>(sourceTex))
				ice->Palette() = bitmap->Palette();
			ice->ForceRefresh() = 1;
		}
		ice->OldSourceTex() = sourceTex;
		if (glassTex != ice->OldGlassTex())
			ice->ForceRefresh() = 1;
		ice->OldGlassTex() = glassTex;

		MoveIcePosition(ice, ice->TimeMethod() != 0 ? frameTime : 1.0f / 120.0f);

		int u = (int)ice->UPosition(), v = (int)ice->VPosition();
		if (u == ice->OldUDisplace() && v == ice->OldVDisplace() && !ice->ForceRefresh())
			return;
		ice->OldUDisplace() = u;
		ice->OldVDisplace() = v;

		uint8_t* dest = (uint8_t*)mip.Data.data();
		if (ice->MoveIce())
			BlitTexIce(dest, source, glass, width, height, u & (width - 1), v & (height - 1));
		else
			BlitIceTex(dest, source, glass, width, height, u & (width - 1), v & (height - 1));
		ice->ForceRefresh() = 0;
		ice->TextureModified = true;
	}
}
