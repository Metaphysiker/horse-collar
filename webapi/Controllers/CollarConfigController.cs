using Microsoft.AspNetCore.Mvc;
using MongoDB.Driver;

[ApiController]
[Route("horses/{horseId}/config")]
public class CollarConfigController(IMongoDatabase db) : ControllerBase
{
    private readonly IMongoCollection<CollarConfig> _configs = db.GetCollection<CollarConfig>("collarConfigs");

    [HttpGet]
    public async Task<ActionResult<CollarConfig>> Get(string horseId)
    {
        var config = await _configs.Find(c => c.HorseId == horseId).FirstOrDefaultAsync();
        if (config is null)
        {
            config = new CollarConfig { HorseId = horseId };
            await _configs.InsertOneAsync(config);
        }
        return Ok(config);
    }

    [HttpPut]
    public async Task<ActionResult<CollarConfig>> Update(string horseId, CollarConfig updated)
    {
        updated.HorseId = horseId;
        var result = await _configs.FindOneAndReplaceAsync(
            c => c.HorseId == horseId,
            updated,
            new FindOneAndReplaceOptions<CollarConfig> { ReturnDocument = ReturnDocument.After, IsUpsert = true }
        );
        return Ok(result);
    }

    [HttpPost("recalibrate")]
    public async Task<IActionResult> Recalibrate(string horseId)
    {
        var update = Builders<CollarConfig>.Update.Set(c => c.Recalibrate, true);
        await _configs.UpdateOneAsync(c => c.HorseId == horseId, update, new UpdateOptions { IsUpsert = true });
        return NoContent();
    }

    [HttpPost("recalibrate/clear")]
    public async Task<IActionResult> ClearRecalibrate(string horseId, [FromBody] CalibrationResult result)
    {
        var update = Builders<CollarConfig>.Update
            .Set(c => c.Recalibrate, false)
            .Set(c => c.CalibrationStatus, result.Status)
            .Set(c => c.CalibrationTime, DateTime.UtcNow);
        await _configs.UpdateOneAsync(c => c.HorseId == horseId, update);
        return NoContent();
    }

    [HttpPost("reboot")]
    public async Task<IActionResult> Reboot(string horseId)
    {
        var update = Builders<CollarConfig>.Update.Set(c => c.Reboot, true);
        await _configs.UpdateOneAsync(c => c.HorseId == horseId, update, new UpdateOptions { IsUpsert = true });
        return NoContent();
    }

    [HttpPost("reboot/clear")]
    public async Task<IActionResult> ClearReboot(string horseId)
    {
        var update = Builders<CollarConfig>.Update.Set(c => c.Reboot, false);
        await _configs.UpdateOneAsync(c => c.HorseId == horseId, update);
        return NoContent();
    }

    public record CalibrationResult(string Status);
}
