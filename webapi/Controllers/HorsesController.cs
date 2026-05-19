using Microsoft.AspNetCore.Mvc;
using MongoDB.Driver;

[ApiController]
[Route("horses")]
public class HorsesController(IMongoDatabase db) : ControllerBase
{
    private readonly IMongoCollection<Horse> _horses = db.GetCollection<Horse>("horses");

    [HttpGet]
    public async Task<List<Horse>> GetAll() =>
        await _horses.Find(h => !h.IsArchived).ToListAsync();

    [HttpGet("archived")]
    public async Task<List<Horse>> GetArchived() =>
        await _horses.Find(h => h.IsArchived).ToListAsync();

    [HttpGet("{id}")]
    public async Task<ActionResult<Horse>> GetById(string id)
    {
        var horse = await _horses.Find(h => h.Id == id).FirstOrDefaultAsync();
        return horse is null ? NotFound() : Ok(horse);
    }

    [HttpPost]
    public async Task<ActionResult<Horse>> Create(Horse horse)
    {
        await _horses.InsertOneAsync(horse);
        return CreatedAtAction(nameof(GetById), new { id = horse.Id }, horse);
    }

    [HttpPost("{id}/archive")]
    public async Task<IActionResult> Archive(string id)
    {
        var update = Builders<Horse>.Update.Set(h => h.IsArchived, true);
        var result = await _horses.UpdateOneAsync(h => h.Id == id, update);
        return result.MatchedCount == 0 ? NotFound() : NoContent();
    }

    [HttpPost("{id}/unarchive")]
    public async Task<IActionResult> Unarchive(string id)
    {
        var update = Builders<Horse>.Update.Set(h => h.IsArchived, false);
        var result = await _horses.UpdateOneAsync(h => h.Id == id, update);
        return result.MatchedCount == 0 ? NotFound() : NoContent();
    }
}
