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
    public bool Recalibrate { get; set; } = false;
    public bool NtfyEnabled { get; set; } = false;
    public bool LyingDownAlertEnabled { get; set; } = false;
    public bool HighRollAlertEnabled { get; set; } = false;
    public float RollBaseline { get; set; } = 5.0f;
    public float ChangeAccel { get; set; } = 1.5f;
    public float ChangePitch { get; set; } = 25.0f;
    public float ChangeRoll { get; set; } = 25.0f;

    // Reference quaternion — identity by default (no correction applied)
    public float RefQw { get; set; } = 1.0f;
    public float RefQx { get; set; } = 0.0f;
    public float RefQy { get; set; } = 0.0f;
    public float RefQz { get; set; } = 0.0f;

    public float ReportInterval { get; set;} = 1000000.0f;

    public float RollEnterDeg { get; set; } = 75.0f;
    public float RollExitDeg { get; set; } = 60.0f;

    public float HeartBeatInterval { get; set; } = 1000000.0f;
    public float SleepTimerUs { get; set; } = 1500000.0f;

    public bool AxesCalibrated { get; set; } = false;

    // Body-frame axes in sensor-relative space — set by orientation calibration wizard
    // Roll axis: direction in sensor frame that corresponds to horse tilting left/right
    public float RollAxisX { get; set; } = 0.0f;
    public float RollAxisY { get; set; } = 1.0f;  // default: Y axis
    public float RollAxisZ { get; set; } = 0.0f;
    // Pitch axis: direction in sensor frame that corresponds to horse pitching front/back
    public float PitchAxisX { get; set; } = 1.0f;  // default: X axis
    public float PitchAxisY { get; set; } = 0.0f;
    public float PitchAxisZ { get; set; } = 0.0f;
}
