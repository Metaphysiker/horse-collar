using Microsoft.AspNetCore.Mvc;
using MongoDB.Driver;

[ApiController]
[Route("horses")]
public class HorsesController(IMongoDatabase db) : ControllerBase
{
    private readonly IMongoCollection<Horse> _horses = db.GetCollection<Horse>("horses");

    [HttpGet]
    public async Task<List<Horse>> GetAll() =>
        await _horses.Find(_ => true).ToListAsync();

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

    [HttpDelete("{id}")]
    public async Task<IActionResult> Delete(string id)
    {
        var result = await _horses.DeleteOneAsync(h => h.Id == id);
        return result.DeletedCount == 0 ? NotFound() : NoContent();
    }
}
