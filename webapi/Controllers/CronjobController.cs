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
    private const int PostureChangeWindowMinutes = 5;
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
        const int HeartbeatGraceSeconds = 30;

        var cutoff = DateTime.UtcNow.AddMinutes(-HeartbeatOverdueMinutes).AddSeconds(HeartbeatGraceSeconds);

        var allLatest = await _status
            .Aggregate()
            .SortByDescending(s => s.Timestamp)
            .Group(s => s.HorseId, g => new { HorseId = g.Key, Latest = g.First() })
            .ToListAsync();

        var overdue = allLatest.Where(x => x.Latest.Timestamp < cutoff).ToList();

        foreach (var item in overdue)
        {
            var config = await _configs.Find(c => c.HorseId == item.HorseId).FirstOrDefaultAsync();
            if (config is not null &&
                string.Equals(config.PowerMode, "maintenance", StringComparison.OrdinalIgnoreCase))
            {
                continue;
            }
            await _alarmService.RaiseAlarmAsync(item.HorseId, AlarmType.Heartbeat, $"No heartbeat received for over {HeartbeatOverdueMinutes} minutes.");

            /*
            await _ntfy.NotifyAsync(
                item.HorseId,
                AlarmState.Alert,
                $"No heartbeat received for over {HeartbeatOverdueMinutes} minutes."
            );
            */
        }

        var result = new
        {
            CheckedCount = allLatest.Count
        };

        return Ok(result);
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
            var windowStart = DateTime.UtcNow.AddMinutes(-PostureChangeWindowMinutes);

            var normalizedFilter = Builders<SensorReadingV2>.Filter.And(
                Builders<SensorReadingV2>.Filter.Eq(r => r.HorseId, horseId),
                Builders<SensorReadingV2>.Filter.Eq(r => r.ReadingType, "Normalized")
            );

            // ---- Check: lying too long ----
            var lastReading = await _readings
                .Find(normalizedFilter)
                .SortByDescending(r => r.Timestamp)
                .FirstOrDefaultAsync();

            if (lastReading is not null)
            {
                var lastPosture = PostureDetector.DetectPostureV3(
                    lastReading.Qw, lastReading.Qx, lastReading.Qy, lastReading.Qz,
                    currentPosture: "standing", // we only care if it's lying, seed as standing
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
            string rollingPosture = "standing";
            foreach (var reading in recentReadings)
            {
                string newPosture = PostureDetector.DetectPostureV3(
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

                await _alarmService.RaiseAlarmAsync(horseId, AlarmType.PostureChanges, $"Unusual posture activity: {changes} posture changes in the last {PostureChangeWindowMinutes} minutes.");
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
