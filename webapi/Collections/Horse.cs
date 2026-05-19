using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;

public class Horse
{
    [BsonId]
    [BsonRepresentation(BsonType.ObjectId)]
    public string? Id { get; set; }

    public string Name { get; set; } = string.Empty;
    public bool IsArchived { get; set; } = false;
}
