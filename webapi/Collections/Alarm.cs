using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;

namespace webapi.Collections;

public class Alarm
{
    [BsonId]
    [BsonRepresentation(BsonType.ObjectId)]
    public string? Id { get; set; }

    [BsonRepresentation(BsonType.ObjectId)]
    public string HorseId { get; set; } = string.Empty;

    public AlarmType Type { get; set; }

    public string Message { get; set; } = string.Empty;

    public bool IsActive { get; set; } = true;

    public DateTime CreatedAt { get; set; } = DateTime.UtcNow;

    public DateTime? ClearedAt { get; set; }
}

public enum AlarmType
{
    Heartbeat,
    LyingTooLong,
    PostureChanges
}
