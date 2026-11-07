param(
    # Paths default to the checked-in font and the project codepoint list, so
    # the script runs from any checkout without editing it.
    [string]$Ttf  = (Join-Path $PSScriptRoot "fonts\NotoSansSC-Regular.ttf"),
    [string]$Code = (Join-Path $PSScriptRoot "..\..\..\..\tools\cjk_codepoints.txt"),
    [string]$Out  = (Join-Path $PSScriptRoot "..\font_atlas.h")
)

Add-Type -AssemblyName System.Drawing
$src = @'
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Drawing.Text;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

public static class FontAtlasGen
{
    public static void Run(string ttf, string codepoints, string header)
    {
        var cps = new List<int>();
        var seen = new HashSet<int>();
        /* Printable ASCII always comes from the same face and the same em box so
         * Latin and CJK share cap height, baseline and weight. */
        for (int cp = 0x20; cp <= 0x7E; cp++) {
            cps.Add(cp);
            seen.Add(cp);
        }
        foreach (var line in File.ReadLines(codepoints)) {
            var s = line.Trim();
            if (s.Length == 0) continue;
            int cp = Convert.ToInt32(s, 16);
            if (seen.Add(cp))
                cps.Add(cp);
        }
        cps.Sort();

        var all = new List<byte>(cps.Count * 128);
        var offs = new List<int>(cps.Count);
        var adv = new List<byte>(cps.Count);
        using (var pfc = new PrivateFontCollection())
        {
            pfc.AddFontFile(ttf);
            using (var bmp = new Bitmap(32, 32, PixelFormat.Format32bppArgb))
            using (var g = Graphics.FromImage(bmp))
            using (var font = new Font(pfc.Families[0], 24, FontStyle.Regular, GraphicsUnit.Pixel))
            using (var sf = new StringFormat(StringFormat.GenericTypographic))
            {
                g.TextRenderingHint = TextRenderingHint.AntiAliasGridFit;
                g.SmoothingMode = SmoothingMode.None;
                sf.FormatFlags |= StringFormatFlags.NoClip | StringFormatFlags.NoWrap;
                var bits = new byte[128];
                foreach (var cp in cps)
                {
                    string ch = char.ConvertFromUtf32(cp);
                    g.Clear(Color.Black);
                    g.DrawString(ch, font, Brushes.White, 0, 0, sf);

                    int a;
                    if (cp >= 0x2E80) {
                        a = 26; /* CJK: fixed advance keeps columns aligned */
                    } else {
                        a = (int)Math.Ceiling(g.MeasureString(ch, font, PointF.Empty, sf).Width);
                        if (a < 4) a = 4;
                        if (a > 30) a = 30;
                        if (cp == 0x20) a = 6;
                    }
                    adv.Add((byte)a);

                    var data = bmp.LockBits(new Rectangle(0, 0, 32, 32), ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
                    var stride = data.Stride;
                    var px = new byte[stride * 32];
                    Marshal.Copy(data.Scan0, px, 0, px.Length);
                    bmp.UnlockBits(data);
                    Array.Clear(bits, 0, bits.Length);
                    for (int y = 0; y < 32; y++) {
                        for (int x = 0; x < 32; x++) {
                            if (px[y * stride + x * 4 + 2] > 96)
                                bits[y * 4 + (x >> 3)] |= (byte)(0x80 >> (x & 7));
                        }
                    }
                    offs.Add(all.Count);
                    all.AddRange(bits);
                }
            }
        }
        /* High-resolution companion atlas for the large clock digits: the same
         * face at 96 px with 8-bit coverage so scale-4 text is drawn 1:1 and
         * scale-2 text is a clean 2x2 downsample instead of a 4x magnification
         * of the 24 px bitmap. */
        var bigCps = new List<int>();
        foreach (char ch in "0123456789:.,-+ ")
            bigCps.Add((int)ch);
        bigCps.Sort();
        var bigAll = new List<byte>(bigCps.Count * 128 * 128);
        var bigOffs = new List<int>(bigCps.Count);
        var bigAdv = new List<byte>(bigCps.Count);
        using (var pfc2 = new PrivateFontCollection())
        {
            pfc2.AddFontFile(ttf);
            using (var bmp2 = new Bitmap(128, 128, PixelFormat.Format32bppArgb))
            using (var g2 = Graphics.FromImage(bmp2))
            using (var font2 = new Font(pfc2.Families[0], 96, FontStyle.Regular, GraphicsUnit.Pixel))
            using (var sf2 = new StringFormat(StringFormat.GenericTypographic))
            {
                g2.TextRenderingHint = TextRenderingHint.AntiAlias;
                g2.SmoothingMode = SmoothingMode.None;
                sf2.FormatFlags |= StringFormatFlags.NoClip | StringFormatFlags.NoWrap;
                foreach (var cp in bigCps)
                {
                    string ch = char.ConvertFromUtf32(cp);
                    g2.Clear(Color.Black);
                    g2.DrawString(ch, font2, Brushes.White, 0, 0, sf2);
                    /* Keep advances exactly 4x the base atlas so mixed sizes line up. */
                    int a24 = 0;
                    for (int i = 0; i < cps.Count; i++) {
                        if (cps[i] == cp) { a24 = adv[i]; break; }
                    }
                    bigAdv.Add((byte)a24);
                    var data = bmp2.LockBits(new Rectangle(0, 0, 128, 128), ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
                    var stride = data.Stride;
                    var px = new byte[stride * 128];
                    Marshal.Copy(data.Scan0, px, 0, px.Length);
                    bmp2.UnlockBits(data);
                    bigOffs.Add(bigAll.Count);
                    for (int y = 0; y < 128; y++)
                        for (int x = 0; x < 128; x++)
                            bigAll.Add(px[y * stride + x * 4 + 2]);
                }
            }
        }

        var sb = new StringBuilder(all.Count * 6 + cps.Count * 24 + 4096);
        sb.AppendLine("/* Generated by components/mqjs_modules/mjs_driver_display/tools/gen_font.ps1");
        sb.AppendLine(" * Every glyph (ASCII and CJK) is rasterized from the same face at the same");
        sb.AppendLine(" * size, so mixed text shares cap height, weight and baseline.");
        sb.AppendLine(" * Noto Sans SC is Copyright 2014-2021 Adobe (http://www.adobe.com/),");
        sb.AppendLine(" * licensed under the SIL Open Font License 1.1. */");
        sb.AppendLine("#pragma once");
        sb.AppendLine("#include <stdint.h>");
        sb.AppendLine("#define FONT_ATLAS_CELL 32");
        sb.AppendLine("#define FONT_ATLAS_PX 24");
        sb.AppendLine("#define FONT_ATLAS_LINE 30");
        sb.AppendFormat("#define FONT_ATLAS_COUNT {0}\n", cps.Count);
        sb.AppendLine("#define FONT_BIG_CELL 128");
        sb.AppendFormat("#define FONT_BIG_COUNT {0}\n", bigCps.Count);
        sb.AppendLine("static const uint32_t font_cp[FONT_ATLAS_COUNT] = {");
        for (int i = 0; i < cps.Count; i++) {
            if (i % 12 == 0) sb.Append("    ");
            sb.AppendFormat("0x{0:X4},", cps[i]);
            if (i % 12 == 11) sb.AppendLine();
        }
        if (cps.Count % 12 != 0) sb.AppendLine();
        sb.AppendLine("};");
        sb.AppendLine("static const uint32_t font_big_cp[FONT_BIG_COUNT] = {");
        for (int i = 0; i < bigCps.Count; i++) {
            if (i % 12 == 0) sb.Append("    ");
            sb.AppendFormat("0x{0:X4},", bigCps[i]);
            if (i % 12 == 11) sb.AppendLine();
        }
        if (bigCps.Count % 12 != 0) sb.AppendLine();
        sb.AppendLine("};");
        sb.AppendLine("static const uint8_t font_adv[FONT_ATLAS_COUNT] = {");
        for (int i = 0; i < adv.Count; i++) {
            if (i % 24 == 0) sb.Append("    ");
            sb.AppendFormat("{0},", adv[i]);
            if (i % 24 == 23) sb.AppendLine();
        }
        if (adv.Count % 24 != 0) sb.AppendLine();
        sb.AppendLine("};");
        sb.AppendLine("static const uint32_t font_off[FONT_ATLAS_COUNT] = {");
        for (int i = 0; i < offs.Count; i++) {
            if (i % 12 == 0) sb.Append("    ");
            sb.AppendFormat("{0},", offs[i]);
            if (i % 12 == 11) sb.AppendLine();
        }
        if (offs.Count % 12 != 0) sb.AppendLine();
        sb.AppendLine("};");
        sb.AppendLine("static const uint8_t font_bits[] = {");
        for (int i = 0; i < all.Count; i++) {
            if (i % 24 == 0) sb.Append("    ");
            sb.AppendFormat("0x{0:X2},", all[i]);
            if (i % 24 == 23) sb.AppendLine();
        }
        if (all.Count % 24 != 0) sb.AppendLine();
        sb.AppendLine("};");
        sb.AppendLine("static const uint8_t font_big_adv[FONT_BIG_COUNT] = {");
        for (int i = 0; i < bigAdv.Count; i++) {
            if (i % 24 == 0) sb.Append("    ");
            sb.AppendFormat("{0},", bigAdv[i]);
            if (i % 24 == 23) sb.AppendLine();
        }
        if (bigAdv.Count % 24 != 0) sb.AppendLine();
        sb.AppendLine("};");
        sb.AppendLine("static const uint32_t font_big_off[FONT_BIG_COUNT] = {");
        for (int i = 0; i < bigOffs.Count; i++) {
            if (i % 12 == 0) sb.Append("    ");
            sb.AppendFormat("{0},", bigOffs[i]);
            if (i % 12 == 11) sb.AppendLine();
        }
        if (bigOffs.Count % 12 != 0) sb.AppendLine();
        sb.AppendLine("};");
        sb.AppendLine("static const uint8_t font_big_bits[] = {");
        for (int i = 0; i < bigAll.Count; i++) {
            if (i % 24 == 0) sb.Append("    ");
            sb.AppendFormat("0x{0:X2},", bigAll[i]);
            if (i % 24 == 23) sb.AppendLine();
        }
        if (bigAll.Count % 24 != 0) sb.AppendLine();
        sb.AppendLine("};");
        File.WriteAllText(header, sb.ToString());
        Console.WriteLine("glyphs=" + cps.Count + " bitmap=" + all.Count + " header=" + sb.Length);
    }
}
'@
Add-Type -TypeDefinition $src -ReferencedAssemblies System.Drawing
[FontAtlasGen]::Run($Ttf, $Code, $Out)
