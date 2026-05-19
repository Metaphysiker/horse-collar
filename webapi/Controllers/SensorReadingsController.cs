using Microsoft.AspNetCore.Mvc;
using MongoDB.Driver;

[ApiController]
[Route("horses/{horseId}/readings")]
public class SensorReadingsController(IMongoDatabase db) : ControllerBase
{
    private readonly IMongoCollection<SensorReading> _readings = db.GetCollection<SensorReading>("sensorReadings");

    [HttpGet]
    public async Task<List<SensorReading>> GetByHorse(string horseId) =>
        await _readings.Find(r => r.HorseId == horseId).ToListAsync();

    [HttpPost]
    public async Task<IActionResult> Create(string horseId, SensorReading reading)
    {
        reading.HorseId = horseId;
        await _readings.InsertOneAsync(reading);
        return Created();
    }

    [HttpPost("batch")]
    public async Task<IActionResult> CreateBatch(string horseId, [FromBody] List<SensorReading> readings)
    {
        readings.ForEach(r => r.HorseId = horseId);
        await _readings.InsertManyAsync(readings);
        return Created();
    }
}
