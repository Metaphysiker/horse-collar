using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;

public class SensorReading
{
    [BsonId]
    [BsonRepresentation(BsonType.ObjectId)]
    public string? Id { get; set; }

    [BsonRepresentation(BsonType.ObjectId)]
    public string HorseId { get; set; } = string.Empty;

    public DateTime Timestamp { get; set; }

    public float Pitch { get; set; }
    public float Roll { get; set; }
    public float Yaw { get; set; }
    public float Qw { get; set; } = 1.0f;
    public float Qx { get; set; }
    public float Qy { get; set; }
    public float Qz { get; set; }
    public float TiltDeg { get; set; }
    public float Acceleration { get; set; }
    public float AngularVelocity { get; set; }
    public float? Temperature { get; set; }
    public string? AlertReason { get; set; }
    public string Activity { get; set; } = string.Empty;
    public HorseState State { get; set; }
    public AlarmState AlarmState { get; set; }
}

public enum HorseState
{
    Calm,
    Tilted,
    Moving,
    Rolling
}

public enum AlarmState
{
    None,
    Alert,
    Emergency
}
