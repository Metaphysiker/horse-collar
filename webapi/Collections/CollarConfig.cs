using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;

public class CollarConfig
{
    [BsonId]
    [BsonRepresentation(BsonType.ObjectId)]
    public string? Id { get; set; }

    [BsonRepresentation(BsonType.ObjectId)]
    public string HorseId { get; set; } = string.Empty;

    public float TiltThresholdDegrees { get; set; } = 60f;
    public int LyingConfirmMs { get; set; } = 10000;
    public int HeartbeatMs { get; set; } = 300000;
    public int SampleIntervalMs { get; set; } = 1000;
    public bool Recalibrate { get; set; } = false;
}
