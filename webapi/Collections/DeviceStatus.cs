using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;

public class DeviceStatus
{
    [BsonId]
    [BsonRepresentation(BsonType.ObjectId)]
    public string? Id { get; set; }

    [BsonRepresentation(BsonType.ObjectId)]
    public string HorseId { get; set; } = string.Empty;

    public DateTime Timestamp { get; set; }

    public float BatteryVoltage { get; set; }
    public int BatteryPercent { get; set; }
    public bool BnoConnected { get; set; } = true;
}
