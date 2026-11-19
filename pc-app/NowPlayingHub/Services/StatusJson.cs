using System.Globalization;
using System.Text.Json;

namespace NowPlayingHub;

public static class StatusJson
{
    static readonly JsonSerializerOptions Opt = new() { PropertyNamingPolicy = JsonNamingPolicy.CamelCase };

    static string Iso(DateTime d) => d.ToString("yyyy-MM-dd'T'HH:mm:ss", CultureInfo.InvariantCulture);

    public static string Build(StatusSnapshot s, EspInfo? i, bool online)
    {
        var groups = s.Components.GroupBy(c => c.Def.Group).Select(g => new
        {
            name = g.Key,
            components = g.Select(c => new
            {
                id = c.Def.Id,
                name = c.Def.Name,
                level = LevelText.Key(c.Level),
                text = LevelText.Of(c.Level),
                note = c.Note,
                uptime = c.UptimePct is double u ? Math.Round(u, 2) : (double?)null,
                days = c.Days.Select(d => new { d = d.Date, u = d.Pct is double p ? Math.Round(p, 2) : (double?)null, m = Math.Round(d.DownMin, 1) })
            })
        });

        object? card = null;
        if (online && i != null)
        {
            card = new
            {
                version = i.Version,
                model = i.ChipModel, rev = i.ChipRev, cores = i.Cores, cpuMhz = i.CpuMhz,
                tempC = i.ChipTemp, resetReason = i.ResetReason, uptimeSec = i.UptimeSec, screen = i.Screen,
                heapKB = i.Heap / 1024, minHeapKB = i.HeapMinFree / 1024, maxBlockKB = i.MaxAlloc / 1024,
                heapTotalKB = i.HeapTotal / 1024, fragPct = i.FragPct,
                flashMB = i.FlashSize / 1048576, flashMhz = i.FlashMhz,
                sketchKB = i.SketchSize / 1024, freeSketchKB = i.FreeSketch / 1024,
                rssi = i.Rssi, channel = i.Channel,
                loopAvgMs = Math.Round(i.LoopAvgUs / 1000.0, 2), loopWorstMs = Math.Round(i.LoopWorstUs / 1000.0, 1),
                loopHz = i.LoopHz, loopSlow = i.LoopSlow, loopSlowTotal = i.LoopSlowTotal, stackFreeKB = Math.Round(i.StackFree / 1024.0, 1),
                v19 = i.HasV19
            };
        }

        var payload = new
        {
            generatedAt = Iso(DateTime.Now),
            appVersion = AppInfo.Version,
            sampleSeconds = StatusTracker.SampleSeconds,
            overall = new { level = LevelText.Key(s.Overall), text = LevelText.Banner(s.Overall) },
            groups,
            card,
            history = History.Summary(),
            series = s.Series,
            incidents = s.Incidents.Select(x => new
            {
                start = Iso(x.Start),
                end = x.End is DateTime e ? Iso(e) : null,
                component = x.Component,
                level = LevelText.Key(x.Level),
                detail = x.Detail
            })
        };
        return JsonSerializer.Serialize(payload, Opt);
    }
}
