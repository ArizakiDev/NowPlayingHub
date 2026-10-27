using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Drawing.Text;
using System.Runtime.InteropServices.WindowsRuntime;
using System.Text.RegularExpressions;
using Windows.Graphics.Imaging;
using Windows.Storage.Streams;

namespace NowPlayingHub;

/// <summary>Pochettes (JPEG 120x120) et logos d'appli (JPEG 20x20) pour l'ESP32.</summary>
public static class Images
{
    static readonly Color Bg = Color.FromArgb(14, 14, 20);
    static readonly ImageCodecInfo JpegCodec =
        ImageCodecInfo.GetImageEncoders().First(e => e.FormatID == ImageFormat.Jpeg.Guid);

    /// <summary>JPEG baseline (non progressif : requis par TJpg_Decoder), recompresse jusqu'a tenir dans maxBytes.</summary>
    public static byte[] Encode(Bitmap bmp, int maxBytes)
    {
        byte[] last = Array.Empty<byte>();
        for (int q = 90; q >= 30; q -= 10)
        {
            using var ms = new MemoryStream();
            using var ep = new EncoderParameters(1);
            ep.Param[0] = new EncoderParameter(System.Drawing.Imaging.Encoder.Quality, (long)q);
            bmp.Save(ms, JpegCodec, ep);
            last = ms.ToArray();
            if (last.Length <= maxBytes) break;
        }
        return last;
    }

    static byte[] Make(byte[] raw, int size, int maxBytes)
    {
        using var ms = new MemoryStream(raw);
        using var src = Image.FromStream(ms);
        int side = Math.Min(src.Width, src.Height);
        var srcRect = new Rectangle((src.Width - side) / 2, (src.Height - side) / 2, side, side);
        using var bmp = new Bitmap(size, size, PixelFormat.Format24bppRgb);
        using (var g = Graphics.FromImage(bmp))
        {
            g.InterpolationMode = InterpolationMode.HighQualityBicubic;
            g.PixelOffsetMode = PixelOffsetMode.HighQuality;
            g.SmoothingMode = SmoothingMode.HighQuality;
            g.Clear(Bg);
            g.DrawImage(src, new Rectangle(0, 0, size, size), srcRect, GraphicsUnit.Pixel);
        }
        return Encode(bmp, maxBytes);
    }

    /// <summary>Redimensionne une pochette (recadrage carre centre). Tente GDI+, puis le decodeur Windows (WebP, etc.).</summary>
    /// <summary>Couleur « vive » dominante d'une pochette, au format #RRGGBB (null si pochette sans couleur).</summary>
    public static string? DominantColor(byte[] jpeg)
    {
        using var ms = new MemoryStream(jpeg);
        using var src = Image.FromStream(ms);
        using var bmp = new Bitmap(32, 32, PixelFormat.Format24bppRgb);
        using (var g = Graphics.FromImage(bmp)) g.DrawImage(src, 0, 0, 32, 32);

        const int bins = 24;
        var weight = new double[bins];
        var sumR = new double[bins]; var sumG = new double[bins]; var sumB = new double[bins];
        for (int y = 0; y < 32; y++)
            for (int x = 0; x < 32; x++)
            {
                var c = bmp.GetPixel(x, y);
                double r = c.R / 255.0, g2 = c.G / 255.0, b = c.B / 255.0;
                double max = Math.Max(r, Math.Max(g2, b)), min = Math.Min(r, Math.Min(g2, b));
                double v = max, s = max <= 0 ? 0 : (max - min) / max;
                if (v < 0.25 || s < 0.25) continue;               // ignore noir, blanc, gris
                int bin = (int)(c.GetHue() / 360.0 * bins) % bins;
                double w = s * v;
                weight[bin] += w; sumR[bin] += c.R * w; sumG[bin] += c.G * w; sumB[bin] += c.B * w;
            }
        int best = 0;
        for (int i = 1; i < bins; i++) if (weight[i] > weight[best]) best = i;
        if (weight[best] < 1.0) return null;                       // pochette quasi grise

        var col = Color.FromArgb((int)(sumR[best] / weight[best]), (int)(sumG[best] / weight[best]), (int)(sumB[best] / weight[best]));
        // On force une couleur lisible sur fond sombre : saturation >= 0.6, luminosite >= 0.85
        double h = col.GetHue();
        double mx = Math.Max(col.R, Math.Max(col.G, col.B)) / 255.0, mn = Math.Min(col.R, Math.Min(col.G, col.B)) / 255.0;
        double sat = Math.Max(0.6, mx <= 0 ? 0 : (mx - mn) / mx);
        return FromHsv(h, Math.Min(1, sat), 0.9);
    }

    static string FromHsv(double h, double s, double v)
    {
        double c = v * s, x = c * (1 - Math.Abs(h / 60 % 2 - 1)), m = v - c;
        double r, g, b;
        if (h < 60) (r, g, b) = (c, x, 0); else if (h < 120) (r, g, b) = (x, c, 0);
        else if (h < 180) (r, g, b) = (0, c, x); else if (h < 240) (r, g, b) = (0, x, c);
        else if (h < 300) (r, g, b) = (x, 0, c); else (r, g, b) = (c, 0, x);
        return $"#{(int)((r + m) * 255):X2}{(int)((g + m) * 255):X2}{(int)((b + m) * 255):X2}";
    }

    public static async Task<byte[]?> CoverJpegAsync(byte[] raw, int size, int maxBytes)
    {
        try { return Make(raw, size, maxBytes); }
        catch (Exception e) { Log.Write("cover GDI+: " + e.Message); }
        try
        {
            var png = await TranscodeToPngAsync(raw);
            return Make(png, size, maxBytes);
        }
        catch (Exception e) { Log.Write("cover WinRT: " + e.Message); return null; }
    }

    static async Task<byte[]> TranscodeToPngAsync(byte[] raw)
    {
        using var input = new InMemoryRandomAccessStream();
        await input.WriteAsync(raw.AsBuffer());
        input.Seek(0);
        var dec = await BitmapDecoder.CreateAsync(input);
        using var sb = await dec.GetSoftwareBitmapAsync(BitmapPixelFormat.Bgra8, BitmapAlphaMode.Premultiplied);
        using var output = new InMemoryRandomAccessStream();
        var enc = await BitmapEncoder.CreateAsync(BitmapEncoder.PngEncoderId, output);
        enc.SetSoftwareBitmap(sb);
        await enc.FlushAsync();
        output.Seek(0);
        var buf = new byte[output.Size];
        await output.ReadAsync(buf.AsBuffer(), (uint)output.Size, InputStreamOptions.None);
        return buf;
    }

    // ------------------------- Logo de l'application -------------------------
    static IEnumerable<string> Candidates(string appId)
    {
        var s = appId.Split('!')[0].Split('_')[0];
        if (s.EndsWith(".exe", StringComparison.OrdinalIgnoreCase)) s = s[..^4];
        var list = new List<string> { s };
        list.AddRange(s.Split('.'));
        return list.Where(x => x.Length >= 3).Distinct(StringComparer.OrdinalIgnoreCase);
    }

    static Icon? TryExtract(string appId)
    {
        Process[] procs = Array.Empty<Process>();
        try
        {
            procs = Process.GetProcesses();
            foreach (var cand in Candidates(appId))
            {
                foreach (var p in procs)
                {
                    try
                    {
                        var pn = p.ProcessName;
                        if (!pn.Equals(cand, StringComparison.OrdinalIgnoreCase) &&
                            !pn.Contains(cand, StringComparison.OrdinalIgnoreCase)) continue;
                        var path = p.MainModule?.FileName;
                        if (string.IsNullOrEmpty(path)) continue;
                        var ico = Icon.ExtractAssociatedIcon(path);
                        if (ico != null) return ico;
                    }
                    catch { /* acces refuse (appli elevee / WindowsApps) : on essaie la suivante */ }
                }
            }
        }
        catch (Exception e) { Log.Write("icon: " + e.Message); }
        finally { foreach (var p in procs) p.Dispose(); }
        return null;
    }

    /// <summary>Logo 20x20 sur le fond de l'ecran. Repli : pastille avec l'initiale de l'appli.</summary>
    public static Bitmap AppIcon(string appId, string name)
    {
        var bmp = new Bitmap(20, 20, PixelFormat.Format24bppRgb);
        using var g = Graphics.FromImage(bmp);
        g.InterpolationMode = InterpolationMode.HighQualityBicubic;
        g.SmoothingMode = SmoothingMode.AntiAlias;
        g.Clear(Bg);
        var ico = TryExtract(appId);
        if (ico != null)
        {
            using (ico)
            using (var b = ico.ToBitmap()) g.DrawImage(b, 0, 0, 20, 20);
        }
        else
        {
            using var br = new SolidBrush(Color.FromArgb(250, 45, 72));
            g.FillEllipse(br, 0, 0, 19, 19);
            g.TextRenderingHint = TextRenderingHint.AntiAliasGridFit;
            using var font = new Font("Segoe UI", 9f, FontStyle.Bold);
            using var fmt = new StringFormat { Alignment = StringAlignment.Center, LineAlignment = StringAlignment.Center };
            var letter = (name.Length > 0 ? name[0].ToString() : "?").ToUpperInvariant();
            g.DrawString(letter, font, Brushes.White, new RectangleF(0, 0, 20, 20), fmt);
        }
        return bmp;
    }

    public static byte[] IconJpeg(string appId, string name)
    {
        using var bmp = AppIcon(appId, name);
        return Encode(bmp, 3500);
    }

    public static byte[] IconPng(string appId, string name)
    {
        using var bmp = AppIcon(appId, name);
        using var ms = new MemoryStream();
        bmp.Save(ms, ImageFormat.Png);
        return ms.ToArray();
    }
}
