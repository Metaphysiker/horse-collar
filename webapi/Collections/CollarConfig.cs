using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;

[BsonIgnoreExtraElements]
public class CollarConfig
{
    [BsonId]
    [BsonRepresentation(BsonType.ObjectId)]
    public string? Id { get; set; }

    [BsonRepresentation(BsonType.ObjectId)]
    public string HorseId { get; set; } = string.Empty;

    public int SleepSeconds { get; set; } = 1;
    public int SendEveryN { get; set; } = 60;
    public bool Reboot { get; set; } = false;
    public bool NtfyEnabled { get; set; } = false;
    public float ChangeAccel { get; set; } = 1.5f;
    public float ChangePitch { get; set; } = 25.0f;
    public float ChangeRoll { get; set; } = 25.0f;
}
