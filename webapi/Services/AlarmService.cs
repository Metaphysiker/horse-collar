using MongoDB.Driver;
using webapi.Collections;

namespace webapi.Services;

public class AlarmService
{
    private readonly IMongoCollection<Alarm> _alarms;
    private readonly NtfyService _ntfy;

    public AlarmService(IMongoDatabase db, NtfyService ntfy)
    {
        _alarms = db.GetCollection<Alarm>("alarms");
        _ntfy = ntfy;
    }

    public async Task RaiseAlarmAsync(string horseId, AlarmType type, string message)
    {
        var existing = await _alarms.Find(a =>
                a.HorseId == horseId &&
                a.Type == type &&
                a.IsActive)
            .FirstOrDefaultAsync();

        if (existing != null)
            return;

        await _alarms.InsertOneAsync(new Alarm
        {
            HorseId = horseId,
            Type = type,
            Message = message,
            IsActive = true,
            CreatedAt = DateTime.UtcNow
        });

        await _ntfy.NotifyAsync(horseId, AlarmState.Alert, message);
    }

    public async Task ClearAlarmAsync(string horseId, AlarmType type)
    {
        await _alarms.UpdateOneAsync(
            a => a.HorseId == horseId &&
                 a.Type == type &&
                 a.IsActive,
            Builders<Alarm>.Update
                .Set(a => a.IsActive, false)
                .Set(a => a.ClearedAt, DateTime.UtcNow));
    }

    public async Task<bool> IsActiveAsync(string horseId, AlarmType type)
    {
        return await _alarms.Find(a =>
                a.HorseId == horseId &&
                a.Type == type &&
                a.IsActive)
            .AnyAsync();
    }

    public async Task<List<Alarm>> GetActiveAlarmsAsync()
    {
        return await _alarms
            .Find(a => a.IsActive)
            .ToListAsync();
    }

    public async Task<UpdateResult> ClearAllAsync(FilterDefinition<Alarm> filter, UpdateDefinition<Alarm> update)
    {
        return await _alarms.UpdateManyAsync(filter, update);
    }

    public async Task<Alarm?> GetActiveAlarmAsync(string horseId, AlarmType type)
    {
        return await _alarms
        .Find(a => a.HorseId == horseId &&
                a.Type == type &&
                a.IsActive)
        .FirstOrDefaultAsync();
    }
}
