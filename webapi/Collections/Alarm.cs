namespace webapi.Collections;

public class Alarm
{
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
