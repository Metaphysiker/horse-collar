using System.Text;
using System.Text.Json;

public class NtfyService(HttpClient http, IConfiguration config)
{
    private readonly string _url = $"{config["Ntfy:BaseUrl"]}";
    private readonly string _topic = config["Ntfy:Topic"]!;

    public async Task NotifyAsync(string horseId, HorseState state, string? reason = null)
    {
        var reasonText = reason switch
        {
            "BaselineShift"    => "[Firmware] Unusual movement or orientation change detected.",
            "SuddenFall"       => "[Firmware] Sudden fall — was rolling, now tilted and still.",
            "ActivityCollapse" => "[Firmware] Horse stopped moving abruptly after activity.",
            "ConfirmedFall"    => "[Firmware] Fall confirmed — horse went from upright to lying.",
            "ColicRolling"     => "[Firmware] Horse is rolling repeatedly (possible colic).",
            "TestNotification" => "This is a test notification from Horse Collar.",
            _                  => reason ?? "Unknown reason."
        };

        var (title, message, priority, tag) = state switch
        {
            HorseState.Emergency => (
                "🚨 Emergency: Horse down",
                $"{reasonText} Check immediately.",
                5, "rotating_light"),
            HorseState.Alert => (
                "⚠️ Alert: Horse needs attention",
                reasonText,
                4, "warning"),
            _ => throw new ArgumentException("Not an alert state")
        };

        var payload = new { topic = _topic, title, message, priority, tags = new[] { tag } };
        var json = JsonSerializer.Serialize(payload);
        await http.PostAsync(_url, new StringContent(json, Encoding.UTF8, "application/json"));
    }
}
