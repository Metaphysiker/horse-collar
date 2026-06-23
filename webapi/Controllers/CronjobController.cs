using Microsoft.AspNetCore.Mvc;
using MongoDB.Driver;

[ApiController]
[Route("cronjobs")]
public class CronjobController(IMongoDatabase db, NtfyService ntfy) : ControllerBase
{
    private readonly IMongoCollection<DeviceStatus> _status = db.GetCollection<DeviceStatus>("deviceStatus");
    private readonly IMongoCollection<SensorReadingV2> _readings = db.GetCollection<SensorReadingV2>("sensorReadingsV2");

    private const int HeartbeatOverdueMinutes = 5;
    private const int LyingTooLongMinutes = 5;
    private const int PostureChangeWindowMinutes = 5;
    private const int PostureChangeThreshold = 4; // number of changes within the window

    [HttpPost("check-heartbeat")]
    public async Task<IActionResult> CheckHeartbeat()
    {
        var cutoff = DateTime.UtcNow.AddMinutes(-HeartbeatOverdueMinutes);

        var allLatest = await _status
            .Aggregate()
            .SortByDescending(s => s.Timestamp)
            .Group(s => s.HorseId, g => new { HorseId = g.Key, Latest = g.First() })
            .ToListAsync();

        var overdue = allLatest.Where(x => x.Latest.Timestamp < cutoff).ToList();

        foreach (var item in overdue)
        {
            await ntfy.NotifyAsync(
                item.HorseId,
                AlarmState.Alert,
                $"No heartbeat received for over {HeartbeatOverdueMinutes} minutes."
            );
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
                    await ntfy.NotifyAsync(
                        horseId,
                        AlarmState.Alert,
                        $"Horse has been lying for {lyingMinutes} minutes."
                    );
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
                await ntfy.NotifyAsync(
                    horseId,
                    AlarmState.Alert,
                    $"Unusual posture activity: {changes} posture changes in the last {PostureChangeWindowMinutes} minutes."
                );
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
}
