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
        var query = _readings
            .Find(r => r.HorseId == horseId && r.Timestamp >= from && r.Timestamp <= to)
            .SortByDescending(r => r.Timestamp);
        if (limit is > 0)
            query = query.Limit(limit.Value);
        var results = await query.ToListAsync();
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
                await ntfy.NotifyAsync(horseId, reading.State, reading.AlertReason ?? "Unknown");
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
                await ntfy.NotifyAsync(horseId, alert.State, alert.AlertReason ?? "Unknown");
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
