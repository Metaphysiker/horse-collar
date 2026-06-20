using Microsoft.AspNetCore.Mvc;
using MongoDB.Driver;

[ApiController]
[Route("v2/horses/{horseId}/readings")]
public class SensorReadingsV2Controller(IMongoDatabase db, HorseService horseService, NtfyService ntfy) : ControllerBase
{
    private readonly IMongoCollection<SensorReadingV2> _readings = db.GetCollection<SensorReadingV2>("sensorReadingsV2");
    private readonly IMongoCollection<CollarConfig> _configs = db.GetCollection<CollarConfig>("collarConfigs");

    [HttpGet]
    public async Task<List<SensorReadingV2>> GetByHorse(string horseId, [FromQuery] DateOnly? date, [FromQuery] int? limit)
    {
        var day = date ?? DateOnly.FromDateTime(DateTime.UtcNow);

        var from = DateTime.SpecifyKind(day.ToDateTime(TimeOnly.MinValue), DateTimeKind.Utc);
        var to = DateTime.SpecifyKind(day.ToDateTime(TimeOnly.MaxValue), DateTimeKind.Utc);

        // Explicitly enforce strong typing across properties
        var filter = Builders<SensorReadingV2>.Filter.And(
            Builders<SensorReadingV2>.Filter.Eq(r => r.HorseId, horseId),
            Builders<SensorReadingV2>.Filter.Gte(r => r.Timestamp, from),
            Builders<SensorReadingV2>.Filter.Lte(r => r.Timestamp, to)
        );

        var find = _readings
            .Find(filter)
            .SortByDescending(r => r.Timestamp);

        var results = await (limit is > 0 ? find.Limit(limit.Value) : find).ToListAsync();
        results.Reverse(); // Chronological order
        return results;
    }

    [HttpPost("posture-change")]
    public async Task<IActionResult> PostureChange(
    [FromRoute] string horseId,
    [FromBody] SensorReadingV2Dto readingDto)
    {

        await ntfy.NotifyAsync(
            horseId,
            AlarmState.Alert,
            string.IsNullOrWhiteSpace(readingDto?.posture) ? "Empty" : readingDto.posture
        );

        await horseService.EnsureExistsAsync(horseId);
        if (readingDto.rawReading is not null)
        {
            var raw = readingDto.rawReading;
            raw.HorseId = horseId;
            raw.ReadingType = raw.ReadingType ?? ReadingType.Raw.ToString();

            var timestampUtc = DateTime.SpecifyKind(raw.Timestamp, DateTimeKind.Utc);


            await _readings.InsertOneAsync(new SensorReadingV2
            {
                HorseId = raw.HorseId,
                Timestamp = timestampUtc,
                Qw = raw.Qw,
                Qx = raw.Qx,
                Qy = raw.Qy,
                Qz = raw.Qz,
                Acceleration = raw.Acceleration,
                AngularVelocity = raw.AngularVelocity,
                AlertReason = raw.AlertReason,
                ReadingType = raw.ReadingType,
            });
        }
        if (readingDto.normalizedReading is not null)
        {
            var norm = readingDto.normalizedReading;
            norm.HorseId = horseId;
            norm.ReadingType = norm.ReadingType ?? ReadingType.Normalized.ToString();

            var timestampUtc = DateTime.SpecifyKind(norm.Timestamp, DateTimeKind.Utc);

            await _readings.InsertOneAsync(new SensorReadingV2
            {
                HorseId = norm.HorseId,
                Timestamp = timestampUtc,
                Qw = norm.Qw,
                Qx = norm.Qx,
                Qy = norm.Qy,
                Qz = norm.Qz,
                Acceleration = norm.Acceleration,
                AngularVelocity = norm.AngularVelocity,
                AlertReason = norm.AlertReason,
                ReadingType = norm.ReadingType
            });
        }

        return Ok();
    }

    [HttpPost("from-dto")]
    public async Task<IActionResult> CreateFromDto(
    [FromRoute] string horseId,
    [FromBody] SensorReadingV2Dto readingDto)
    {
        await horseService.EnsureExistsAsync(horseId);
        if (readingDto.rawReading is not null)
        {
            var raw = readingDto.rawReading;
            raw.HorseId = horseId;
            raw.ReadingType = raw.ReadingType ?? ReadingType.Raw.ToString();

            var timestampUtc = DateTime.SpecifyKind(raw.Timestamp, DateTimeKind.Utc);

            await _readings.InsertOneAsync(new SensorReadingV2
            {
                HorseId = raw.HorseId,
                Timestamp = timestampUtc,
                Qw = raw.Qw,
                Qx = raw.Qx,
                Qy = raw.Qy,
                Qz = raw.Qz,
                Acceleration = raw.Acceleration,
                AngularVelocity = raw.AngularVelocity,
                AlertReason = raw.AlertReason,
                ReadingType = raw.ReadingType,
            });
        }
        if (readingDto.normalizedReading is not null)
        {
            var norm = readingDto.normalizedReading;
            norm.HorseId = horseId;
            norm.ReadingType = norm.ReadingType ?? ReadingType.Normalized.ToString();

            var timestampUtc = DateTime.SpecifyKind(norm.Timestamp, DateTimeKind.Utc);

            await _readings.InsertOneAsync(new SensorReadingV2
            {
                HorseId = norm.HorseId,
                Timestamp = timestampUtc,
                Qw = norm.Qw,
                Qx = norm.Qx,
                Qy = norm.Qy,
                Qz = norm.Qz,
                Acceleration = norm.Acceleration,
                AngularVelocity = norm.AngularVelocity,
                AlertReason = norm.AlertReason,
                ReadingType = norm.ReadingType
            });
        }
        return Created();
    }
}
