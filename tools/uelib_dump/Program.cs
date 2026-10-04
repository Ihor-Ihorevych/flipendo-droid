// Package extractor for the HP games, built on UELib (tools/Unreal-Library).
//
//   uelib_dump scripts <System dir> <out dir>
//       For every *.u package: every class's UnrealScript into <out>/<Package>/Classes/<Class>.uc (the usual
//       `ucc batchexport` layout). Uses the source text embedded in the package (ScriptText, with the original
//       comments); falls back to UELib's bytecode decompiler, marked in the first line, when a class has none.
//   uelib_dump props <package> <out file>
//       Every export of one package (e.g. a .unr map) with its class, outer and serialized properties.
//
// The output is derived from the game's files: write it outside the repo (reference/ is gitignored).
using UELib;
using UELib.Core;

static class Program
{
    static int Main(string[] args)
    {
        if (args.Length != 3 || (args[0] != "scripts" && args[0] != "props"))
        {
            Console.Error.WriteLine("usage: uelib_dump scripts <System dir> <out dir> | props <package> <out file>");
            return 2;
        }
        return args[0] == "scripts" ? Scripts(args[1], args[2]) : Props(args[1], args[2]);
    }

    static UnrealPackage Open(string path)
    {
        var pkg = UnrealLoader.LoadPackage(path);
        pkg.InitializePackage(UnrealPackage.InitFlags.All);
        return pkg;
    }

    static int Scripts(string systemDir, string outDir)
    {
        int classes = 0, decompiled = 0, failed = 0;
        foreach (string path in Directory.GetFiles(systemDir, "*.u").OrderBy(p => p))
        {
            string pkgName = Path.GetFileNameWithoutExtension(path);
            UnrealPackage pkg;
            try { pkg = Open(path); }
            catch (Exception e) { Console.Error.WriteLine($"{pkgName}: cannot load: {e.Message}"); failed++; continue; }

            using (pkg)
            {
                foreach (var cls in pkg.Objects.OfType<UClass>().Where(c => c.ExportTable != null))
                {
                    string text;
                    try
                    {
                        if (cls.DeserializationState == 0) cls.Load();
                        var buffer = cls.ScriptText;
                        if (buffer != null && buffer.DeserializationState == 0) buffer.Load(); // only a reference until loaded
                        text = buffer?.ScriptText ?? "";
                        if (text.Trim().Length == 0) // HP2 strips most source text but keeps a few blank lines
                        {
                            text = "// decompiled by UELib (no source text in the package)\r\n" + cls.Decompile();
                            decompiled++;
                        }
                        else
                        {
                            text = text.TrimEnd() + "\r\n\r\n" + DefaultProperties(cls);
                        }
                    }
                    catch (Exception e)
                    {
                        Console.Error.WriteLine($"{pkgName}.{cls.Name}: {e.Message}");
                        failed++;
                        continue;
                    }
                    string dir = Path.Combine(outDir, pkgName, "Classes");
                    Directory.CreateDirectory(dir);
                    File.WriteAllText(Path.Combine(dir, cls.Name + ".uc"), text);
                    classes++;
                }
            }
        }
        Console.WriteLine($"{classes} classes ({decompiled} decompiled), {failed} failures");
        return failed == 0 ? 0 : 1;
    }

    // The source text has no defaultproperties (they are serialized on the class default object);
    // append them the way ucc's batchexport did.
    static string DefaultProperties(UClass cls)
    {
        var defaults = cls.Default ?? cls;
        if (defaults.DeserializationState == 0) defaults.Load();
        var sb = new System.Text.StringBuilder("defaultproperties\r\n{\r\n");
        if (defaults.Properties != null)
            foreach (var p in defaults.Properties)
            {
                string line;
                try { line = FixColor(cls.Package, p, p.Decompile()); } catch (Exception e) { line = $"// {p.Name}: {e.Message}"; }
                sb.Append("     ").Append(line.Replace("\r\n", "\n").Replace("\n", "\r\n     ")).Append("\r\n");
            }
        return sb.Append("}\r\n").ToString();
    }

    // UELib reads a Color struct as B,G,R,A for package versions > 69 (its own "FIXME: Version, may need adjustments
    // for UE1"), but UE1 kept serializing R,G,B,A: HP1 (76) came out with red and blue swapped
    // (HUD.WhiteColor (R=255,G=128) instead of ucc's (G=128,B=255)). Swap them back for UE1-era packages.
    static string FixColor(UnrealPackage pkg, UDefaultProperty p, string line)
    {
        if (pkg.Version >= 100 || p.StructName?.ToString() != "Color")
            return line;
        var m = System.Text.RegularExpressions.Regex.Match(line, @"R=(\d+),G=(\d+),B=(\d+)");
        return m.Success ? line.Remove(m.Index, m.Length).Insert(m.Index, $"R={m.Groups[3]},G={m.Groups[2]},B={m.Groups[1]}") : line;
    }

    static int Props(string path, string outFile)
    {
        using var pkg = Open(path);
        using var w = new StreamWriter(outFile);
        int n = 0, failed = 0;
        foreach (var obj in pkg.Objects.Where(o => o.ExportTable != null))
        {
            try { if (obj.DeserializationState == 0) obj.Load(); }
            catch (Exception e) { w.WriteLine($"{obj.GetReferencePath()} !! {e.Message}"); failed++; continue; }
            w.WriteLine($"{obj.Class?.Name ?? "Class"} {obj.Name} outer={obj.Outer?.Name}");
            if (obj.Properties != null)
                foreach (var p in obj.Properties)
                {
                    string line;
                    try { line = FixColor(pkg, p, p.Decompile()); } catch (Exception e) { line = $"{p.Name} !! {e.Message}"; failed++; }
                    w.WriteLine("    " + line.Replace("\r\n", "\n").Replace("\n", "\n    "));
                }
            n++;
        }
        Console.WriteLine($"{n} exports, {failed} failures -> {outFile}");
        return 0;
    }
}
