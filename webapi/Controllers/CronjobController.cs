using Microsoft.AspNetCore.Mvc;
using MongoDB.Driver;
using webapi.Collections;
using webapi.Services;

[ApiController]
[Route("cronjobs")]
public class CronjobController : ControllerBase
{
    private readonly IMongoCollection<DeviceStatus> _status;
    private readonly IMongoCollection<SensorReadingV2> _readings;
    private readonly IMongoCollection<CollarConfig> _configs;
    private readonly AlarmService _alarmService;
    private readonly NtfyService _ntfy;
    private const int HeartbeatOverdueMinutes = 5;
    private const int LyingTooLongMinutes = 5;
    private const int PostureChangeThreshold = 4; // number of changes within the window

    public CronjobController(IMongoDatabase db, NtfyService ntfy, AlarmService alarmService)
    {
        _status = db.GetCollection<DeviceStatus>("deviceStatus");
        _readings = db.GetCollection<SensorReadingV2>("sensorReadingsV2");
        _configs = db.GetCollection<CollarConfig>("collarConfigs");
        _alarmService = alarmService;
        _ntfy = ntfy;
    }

    [HttpPost("check-heartbeat")]
    public async Task<IActionResult> CheckHeartbeat()
    {
        var now = DateTime.UtcNow;
        var threshold = TimeSpan.FromMinutes(HeartbeatOverdueMinutes);
        var grace = TimeSpan.FromSeconds(30);

        // Get distinct horse IDs only (cheap operation)
        var horseIds = await _status
            .Distinct<string>("HorseId", Builders<DeviceStatus>.Filter.Empty)
            .ToListAsync();

        var overdueHorses = new List<string>();

        foreach (var horseId in horseIds)
        {
            // Fetch only latest record per horse (indexed sort = fast)
            var latest = await _status
                .Find(x => x.HorseId == horseId)
                .SortByDescending(x => x.Timestamp)
                .FirstOrDefaultAsync();

            if (latest == null)
                continue;

            var age = now - latest.Timestamp;

            if (age <= threshold + grace)
                continue;

            // Skip maintenance mode
            var config = await _configs
                .Find(c => c.HorseId == horseId)
                .FirstOrDefaultAsync();

            if (config != null &&
                string.Equals(config.PowerMode, "maintenance", StringComparison.OrdinalIgnoreCase))
            {
                continue;
            }

            // 🔒 IMPORTANT: prevent alarm spam (deduplication)
            var existingAlarm = await _alarmService.GetActiveAlarmAsync(
                horseId,
                AlarmType.Heartbeat
            );

            if (existingAlarm == null)
            {
                await _alarmService.RaiseAlarmAsync(
                    horseId,
                    AlarmType.Heartbeat,
                    $"No heartbeat received for over {HeartbeatOverdueMinutes} minutes."
                );
            }

            overdueHorses.Add(horseId);
        }

        return Ok(new
        {
            CheckedHorses = horseIds.Count,
            Overdue = overdueHorses.Count,
            OverdueHorses = overdueHorses
        });
    }

    [HttpPost("check-posture")]
    public async Task<IActionResult> CheckPosture()
    {
        var horses = await _readings
            .Distinct<string>("HorseId", Builders<SensorReadingV2>.Filter.Empty)
            .ToListAsync();

        var results = new List<object>();

        foreach (var horseId in horses)
        {

            var config = await _configs
                .Find(c => c.HorseId == horseId)
                .FirstOrDefaultAsync();

            if (config == null)
                continue;

            var heartbeatWindow = TimeSpan.FromMicroseconds(config.HeartBeatInterval);
            var windowStart = DateTime.UtcNow - heartbeatWindow;

            double windowMinutes = heartbeatWindow.TotalMinutes;

            var normalizedFilter = Builders<SensorReadingV2>.Filter.And(
                Builders<SensorReadingV2>.Filter.Eq(r => r.HorseId, horseId),
                Builders<SensorReadingV2>.Filter.Eq(r => r.ReadingType, "Normalized")
            );

            // ---- Check: lying too long ----
            var lastReading = await _readings
                .Find(normalizedFilter)
                .SortByDescending(r => r.Timestamp)
                .FirstOrDefaultAsync();

            string? lastPosture = null;

            if (lastReading is not null)
            {
                lastPosture = PostureDetector.DetectPostureV3(
                    lastReading.Qw, lastReading.Qx, lastReading.Qy, lastReading.Qz,
                    currentPosture: "standing",
                    rollEnterDeg: 75f,
                    rollExitDeg: 60f
                );

                var lyingCutoff = DateTime.UtcNow.AddMinutes(-LyingTooLongMinutes);
                if (lastPosture == "lying" && lastReading.Timestamp <= lyingCutoff)
                {
                    var lyingMinutes = (int)(DateTime.UtcNow - lastReading.Timestamp).TotalMinutes;

                    await _alarmService.RaiseAlarmAsync(horseId, AlarmType.LyingTooLong, $"Horse has been lying for {lyingMinutes} minutes.");

                    /*
                                        await _ntfy.NotifyAsync(
                                            horseId,
                                            AlarmState.Alert,
                                            $"Horse has been lying for {lyingMinutes} minutes."
                                        );
                                        */
                }
            }

            // ---- Check: excessive posture changes ----
            var recentReadings = await _readings
                .Find(r => r.HorseId == horseId
                        && r.ReadingType == "Normalized"
                        && r.Timestamp >= windowStart)
                .SortBy(r => r.Timestamp)
                .ToListAsync();

            // Replay posture state across the window to count transitions
            int changes = 0;
            string rollingPosture;

            if (recentReadings.Count == 0)
                continue;

            rollingPosture = PostureDetector.DetectPostureV3(
                recentReadings[0].Qw,
                recentReadings[0].Qx,
                recentReadings[0].Qy,
                recentReadings[0].Qz,
                currentPosture: "standing",
                rollEnterDeg: 75f,
                rollExitDeg: 60f
            );

            for (int i = 1; i < recentReadings.Count; i++)
            {
                var reading = recentReadings[i];

                var newPosture = PostureDetector.DetectPostureV3(
                    reading.Qw, reading.Qx, reading.Qy, reading.Qz,
                    currentPosture: rollingPosture,
                    rollEnterDeg: 75f,
                    rollExitDeg: 60f
                );

                if (newPosture != rollingPosture)
                {
                    changes++;
                    rollingPosture = newPosture;
                }
            }

            if (changes >= PostureChangeThreshold)
            {

                await _alarmService.RaiseAlarmAsync(horseId, AlarmType.PostureChanges, $"Unusual posture activity: {changes} posture changes in the last {windowMinutes:F1} minutes.");
                /*
                                await _ntfy.NotifyAsync(
                                    horseId,
                                    AlarmState.Alert,
                                    $"Unusual posture activity: {changes} posture changes in the last {PostureChangeWindowMinutes} minutes."
                                );
                                */
            }

            results.Add(new
            {
                horseId,
                changes,
                lastPosture = lastReading is null ? null :
                PostureDetector.DetectPostureV3(
                    lastReading.Qw, lastReading.Qx, lastReading.Qy, lastReading.Qz,
                    currentPosture: "standing",
                    rollEnterDeg: 75f,
                    rollExitDeg: 60f
                )
            });
        }

        return Ok(results);
    }

    [HttpPost("notify-active-alarms")]
    public async Task<IActionResult> NotifyActiveAlarms()
    {
        var activeAlarms = await _alarmService.GetActiveAlarmsAsync();

        var grouped = activeAlarms
            .GroupBy(a => a.HorseId)
            .ToList();

        foreach (var group in grouped)
        {
            var horseId = group.Key;

            var messages = group
                .Select(a => a.Message)
                .ToList();

            var payload =
                $"Active alarms ({messages.Count}):\n" +
                string.Join("\n", messages);

            await _ntfy.NotifyAsync(
                horseId,
                AlarmState.Alert,
                payload
            );
        }

        return Ok(new
        {
            ActiveAlarmCount = activeAlarms.Count,
            HorsesNotified = grouped.Count
        });
    }

    [HttpPost("clear-all-alarms")]
    public async Task<IActionResult> ClearAllAlarms()
    {
        var activeAlarms = await _alarmService.GetActiveAlarmsAsync();

        if (activeAlarms.Count == 0)
        {
            return Ok(new
            {
                Cleared = 0,
                Message = "No active alarms to clear."
            });
        }

        var now = DateTime.UtcNow;

        var filter = Builders<Alarm>.Filter.Eq(a => a.IsActive, true);

        var update = Builders<Alarm>.Update
            .Set(a => a.IsActive, false)
            .Set(a => a.ClearedAt, now);

        var result = await _alarmService.ClearAllAsync(filter, update);

        return Ok(new
        {
            Cleared = result.ModifiedCount
        });
    }
}
