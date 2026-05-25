using Microsoft.AspNetCore.Mvc;
using MongoDB.Driver;

[ApiController]
[Route("horses/{horseId}/readings")]
public class SensorReadingsController(IMongoDatabase db, HorseService horseService, NtfyService ntfy) : ControllerBase
{
    private readonly IMongoCollection<SensorReading> _readings = db.GetCollection<SensorReading>("sensorReadings");
    private readonly IMongoCollection<CollarConfig> _configs = db.GetCollection<CollarConfig>("collarConfigs");

    [HttpGet]
    public async Task<List<SensorReading>> GetByHorse(string horseId, [FromQuery] DateOnly? date, [FromQuery] int? limit)
    {
        var day = date ?? DateOnly.FromDateTime(DateTime.UtcNow);
        var from = day.ToDateTime(TimeOnly.MinValue, DateTimeKind.Utc);
        var to   = day.ToDateTime(TimeOnly.MaxValue, DateTimeKind.Utc);
        var find = _readings
            .Find(r => r.HorseId == horseId && r.Timestamp >= from && r.Timestamp <= to)
            .SortByDescending(r => r.Timestamp);
        var results = await (limit is > 0 ? find.Limit(limit.Value) : find).ToListAsync();
        results.Reverse();
        return results;
    }

    [HttpPost]
    public async Task<IActionResult> Create(string horseId, SensorReading reading)
    {
        await horseService.EnsureExistsAsync(horseId);
        reading.HorseId = horseId;
        await _readings.InsertOneAsync(reading);

        if (reading.State is HorseState.Alert or HorseState.Emergency)
        {
            var config = await _configs.Find(c => c.HorseId == horseId).FirstOrDefaultAsync();
            if (config?.NtfyEnabled == true)
                await ntfy.NotifyAsync(horseId, reading.State, reading.AlertReason ?? reading.State.ToString());
        }

        return Created();
    }

    [HttpPost("batch")]
    public async Task<IActionResult> CreateBatch(string horseId, [FromBody] List<SensorReading> readings)
    {
        await horseService.EnsureExistsAsync(horseId);
        readings.ForEach(r => r.HorseId = horseId);
        await _readings.InsertManyAsync(readings);

        var config = await _configs.Find(c => c.HorseId == horseId).FirstOrDefaultAsync();
        if (config?.NtfyEnabled == true)
        {
            var alert = readings.LastOrDefault(r => r.State is HorseState.Alert or HorseState.Emergency);
            if (alert is not null)
                await ntfy.NotifyAsync(horseId, alert.State, alert.AlertReason ?? alert.State.ToString());
        }

        if (config?.LyingDownAlertEnabled == true)
        {
            var lyingDown = readings.FirstOrDefault(r =>
                Math.Sqrt((double)(r.Pitch * r.Pitch + r.Roll * r.Roll)) > 70);
            if (lyingDown is not null)
            {
                var side = lyingDown.Roll > 0 ? "left side" : "right side";
                await ntfy.NotifyAsync(horseId, HorseState.Alert, $"[LyingDown] {side} (tilt {Math.Sqrt((double)(lyingDown.Pitch * lyingDown.Pitch + lyingDown.Roll * lyingDown.Roll)):F0}°)");
            }
        }

        if (config?.HighRollAlertEnabled == true)
        {
            // Sensor roll calibration: standing = +5°, left 90° = +72°, right 90° = -65°
            // Horse roll 45° left  → sensor roll ≈ +38.5°
            // Horse roll 45° right → sensor roll ≈ -30°
            var baseline = config.RollBaseline;
            var highRoll = readings.FirstOrDefault(r => r.Roll > baseline + 33.5f || r.Roll < baseline - 35f);
            if (highRoll is not null)
            {
                var horseRoll = highRoll.Roll >= baseline
                    ? (highRoll.Roll - baseline) / 67f * 90f
                    : (highRoll.Roll - baseline) / 70f * 90f;
                var side = horseRoll > 0 ? "left" : "right";
                await ntfy.NotifyAsync(horseId, HorseState.Alert, $"[HighRoll] {side} side (horse roll ~{Math.Abs(horseRoll):F0}°)");
            }
        }

        return Created();
    }

    [HttpDelete("{id}")]
    public async Task<IActionResult> Delete(string horseId, string id)
    {
        var result = await _readings.DeleteOneAsync(r => r.HorseId == horseId && r.Id == id);
        return result.DeletedCount == 0 ? NotFound() : NoContent();
    }
}
