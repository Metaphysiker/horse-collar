using MongoDB.Driver;

public class HorseService(IMongoDatabase db)
{
    private readonly IMongoCollection<Horse> _horses = db.GetCollection<Horse>("horses");

    public async Task EnsureExistsAsync(string horseId)
    {
        var update = Builders<Horse>.Update
            .SetOnInsert(h => h.Id, horseId)
            .SetOnInsert(h => h.Name, $"Horse {horseId[^4..]}")
            .SetOnInsert(h => h.IsArchived, false);

        await _horses.UpdateOneAsync(
            h => h.Id == horseId,
            update,
            new UpdateOptions { IsUpsert = true }
        );
    }
}
