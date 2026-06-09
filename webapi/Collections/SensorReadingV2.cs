using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;

public class SensorReadingV2
{
    [BsonId]
    [BsonRepresentation(BsonType.ObjectId)]
    public string? Id { get; set; }

    [BsonRepresentation(BsonType.ObjectId)]
    public string HorseId { get; set; } = string.Empty;

    public DateTime Timestamp { get; set; }

    public float Qw { get; set; } = 1.0f;
    public float Qx { get; set; }
    public float Qy { get; set; }
    public float Qz { get; set; }
    public float Acceleration { get; set; }
    public float AngularVelocity { get; set; }
    public string? AlertReason { get; set; }
    public string? ReadingType { get; set; }
}


public enum ReadingType
{
    Raw,
    Normalized

}
