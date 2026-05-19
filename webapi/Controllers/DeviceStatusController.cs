using Microsoft.AspNetCore.Mvc;
using MongoDB.Driver;

[ApiController]
[Route("horses/{horseId}/status")]
public class DeviceStatusController(IMongoDatabase db) : ControllerBase
{
    private readonly IMongoCollection<DeviceStatus> _status = db.GetCollection<DeviceStatus>("deviceStatus");

    [HttpGet("latest")]
    public async Task<ActionResult<DeviceStatus>> GetLatest(string horseId)
    {
        var status = await _status
            .Find(s => s.HorseId == horseId)
            .SortByDescending(s => s.Timestamp)
            .FirstOrDefaultAsync();

        return status is null ? NotFound() : Ok(status);
    }

    [HttpPost]
    public async Task<IActionResult> Create(string horseId, DeviceStatus status)
    {
        status.HorseId = horseId;
        await _status.InsertOneAsync(status);
        return Created();
    }
}
