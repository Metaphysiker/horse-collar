using Microsoft.AspNetCore.Mvc;
using MongoDB.Driver;

[ApiController]
[Route("horses/{horseId}/readings")]
public class SensorReadingsController(IMongoDatabase db, HorseService horseService, NtfyService ntfy) : ControllerBase
{
    private readonly IMongoCollection<SensorReading> _readings = db.GetCollection<SensorReading>("sensorReadings");

    [HttpGet]
    public async Task<List<SensorReading>> GetByHorse(string horseId, [FromQuery] DateOnly? date)
    {
        var day = date ?? DateOnly.FromDateTime(DateTime.UtcNow);
        var from = day.ToDateTime(TimeOnly.MinValue, DateTimeKind.Utc);
        var to   = day.ToDateTime(TimeOnly.MaxValue, DateTimeKind.Utc);
        return await _readings
            .Find(r => r.HorseId == horseId && r.Timestamp >= from && r.Timestamp <= to)
            .SortBy(r => r.Timestamp)
            .ToListAsync();
    }

    [HttpPost]
    public async Task<IActionResult> Create(string horseId, SensorReading reading)
    {
        await horseService.EnsureExistsAsync(horseId);
        reading.HorseId = horseId;


        await _readings.InsertOneAsync(reading);
        return Created();
    }

    [HttpPost("batch")]
    public async Task<IActionResult> CreateBatch(string horseId, [FromBody] List<SensorReading> readings)
    {
        await horseService.EnsureExistsAsync(horseId);
        readings.ForEach(r => r.HorseId = horseId);
        await _readings.InsertManyAsync(readings);
        return Created();
    }

    [HttpDelete("{id}")]
    public async Task<IActionResult> Delete(string horseId, string id)
    {
        var result = await _readings.DeleteOneAsync(r => r.HorseId == horseId && r.Id == id);
        return result.DeletedCount == 0 ? NotFound() : NoContent();
    }
}
