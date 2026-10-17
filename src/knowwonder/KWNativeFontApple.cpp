// macOS system font lookup for KWNativeFont.cpp, kept in its own file without the engine's precompiled header:
// CoreFoundation's MacTypes.h declares global Point/Rect types that collide with the engine's.
#ifdef __APPLE__

#include <CoreText/CoreText.h>
#include <string>

namespace KW
{
	// The font file for a family name. CoreText always returns a font, substituting one it has for a name it doesn't
	// know (MS Sans Serif -> its UI font); with exactFamily only a font of that family counts. index is the font's
	// position in a TrueType collection (.ttc).
	bool FindAppleFontFile(const std::string& name, bool exactFamily, std::string& path, int& index)
	{
		CFStringRef cfname = CFStringCreateWithCString(kCFAllocatorDefault, name.c_str(), kCFStringEncodingUTF8);
		if (!cfname)
			return false;
		CTFontRef font = CTFontCreateWithName(cfname, 32.0, nullptr);
		if (font && exactFamily)
		{
			CFStringRef family = CTFontCopyFamilyName(font);
			bool same = family && CFStringCompare(family, cfname, kCFCompareCaseInsensitive) == kCFCompareEqualTo;
			if (family)
				CFRelease(family);
			if (!same)
			{
				CFRelease(font);
				font = nullptr;
			}
		}
		CFRelease(cfname);
		if (!font)
			return false;

		bool ok = false;
		if (CFURLRef url = (CFURLRef)CTFontCopyAttribute(font, kCTFontURLAttribute))
		{
			char buffer[4096];
			if (CFURLGetFileSystemRepresentation(url, true, (UInt8*)buffer, sizeof(buffer)))
			{
				path = buffer;
				ok = true;
			}
			CFRelease(url);
		}

		// CoreText doesn't expose the collection index directly; find the face by PostScript name.
		index = 0;
		if (ok && path.size() > 4 && (path.ends_with(".ttc") || path.ends_with(".TTC")))
		{
			CFStringRef psname = CTFontCopyPostScriptName(font);
			CFURLRef url = CFURLCreateFromFileSystemRepresentation(kCFAllocatorDefault, (const UInt8*)path.c_str(), path.size(), false);
			if (psname && url)
			{
				if (CFArrayRef faces = CTFontManagerCreateFontDescriptorsFromURL(url))
				{
					for (CFIndex i = 0, n = CFArrayGetCount(faces); i < n; i++)
					{
						auto desc = (CTFontDescriptorRef)CFArrayGetValueAtIndex(faces, i);
						if (CFStringRef facename = (CFStringRef)CTFontDescriptorCopyAttribute(desc, kCTFontNameAttribute))
						{
							bool match = CFStringCompare(facename, psname, 0) == kCFCompareEqualTo;
							CFRelease(facename);
							if (match)
							{
								index = (int)i;
								break;
							}
						}
					}
					CFRelease(faces);
				}
			}
			if (psname)
				CFRelease(psname);
			if (url)
				CFRelease(url);
		}

		CFRelease(font);
		return ok;
	}
}

#endif
