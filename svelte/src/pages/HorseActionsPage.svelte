<script>
  // @ts-nocheck
  import { onMount, onDestroy } from 'svelte';
  import { pop } from 'svelte-spa-router';
  import { horses } from '../services/horses.js';
  import { sensorReadings } from '../services/sensorReadings.js';
  import { collarConfig } from '../services/collarConfig.js';
  import { formatDate } from '../utils/formatDate.js';

  let { params = {} } = $props();

  let horse           = $state(null);
  let config          = $state(null);
  let lastReading     = $state(null);
  let loading         = $state(false);
  let rebootStatus    = $state(null);
  let notifyStatus    = $state(null);
  let secsSinceLast   = $state(null);
  let tickInterval    = null;
  let pollInterval    = null;

  async function refreshLastReading() {
    const today = new Date().toISOString().slice(0, 10);
    const readings = await sensorReadings.getByHorse(params.id, today);
    lastReading = readings.at(-1) ?? null;
  }

  onMount(async () => {
    [horse, config] = await Promise.all([
      horses.getById(params.id),
      collarConfig.get(params.id),
    ]);

    await refreshLastReading();

    // 1s tick for the display counter
    tickInterval = setInterval(() => {
      if (lastReading)
        secsSinceLast = Math.round((Date.now() - new Date(lastReading.timestamp).getTime()) / 1000);
    }, 1000);


    // 5s tick for data refresh
    pollInterval = setInterval(refreshLastReading, 5000);
  });

  onDestroy(() => {
    clearInterval(tickInterval);
    clearInterval(pollInterval);
  });

  async function testNotification() {
    notifyStatus = null;
    try {
      await collarConfig.testNotification(params.id);
      notifyStatus = { ok: true };
    } catch {
      notifyStatus = { ok: false };
    }
  }

  async function triggerReboot() {
    if (!confirm('The collar will restart on the next WiFi connect. Continue?')) return;
    loading = true;
    rebootStatus = null;
    try {
      await collarConfig.triggerReboot(params.id);
      rebootStatus = { ok: true };
    } catch {
      rebootStatus = { ok: false };
    } finally {
      loading = false;
    }
  }

  // Estimated seconds until next WiFi connect based on sendEveryN and sleep interval
  const secsUntilNext = $derived(() => {
    if (!config || secsSinceLast === null) return null;
    const interval = (config.sendEveryN ?? 60) * (config.sleepSeconds ?? 1);
    return Math.max(0, interval - (secsSinceLast % interval));
  });

  function fmt(seconds) {
    if (seconds === null || seconds === undefined) return '--:--';
    const m = Math.floor(seconds / 60).toString().padStart(2, '0');
    const s = (seconds % 60).toString().padStart(2, '0');
    return `${m}:${s}`;
  }
</script>

<main>
  <button onclick={pop}>← Back</button>

  {#if horse}
    <h1>{horse.name} — Device Actions</h1>
  {/if}

  <section>
    <h2>Last Check-in</h2>
    {#if lastReading}
      <p>Last seen: <strong>{formatDate(lastReading.timestamp)}</strong></p>
      <p class="big-timer">{fmt(secsSinceLast)} ago</p>
      {#if secsSinceLast > 300}
        <p class="overdue">No check-in for 5+ minutes — collar may be offline.</p>
      {/if}
      <p class="next-connect">Next WiFi connect in ~<strong>{fmt(secsUntilNext())}</strong></p>
    {:else}
      <p>No readings today — collar has not checked in yet.</p>
    {/if}
  </section>

  <section>
    <h2>Notifications</h2>
    <p>Send a test notification to verify your ntfy setup is working.</p>
    <button class="action" onclick={testNotification}>Send Test Notification</button>
    {#if notifyStatus}
      {#if notifyStatus.ok}
        <div class="status ok">Sent — check your ntfy app.</div>
      {:else}
        <div class="status error">Failed to send. Check if the server is reachable.</div>
      {/if}
    {/if}
  </section>

  <section>
    <h2>Reboot Device</h2>
    <p>
      Forces the collar to restart on its next WiFi connect (~{fmt(secsUntilNext())}). Use this if readings appear frozen or stuck.
    </p>
    <button class="action danger" onclick={triggerReboot} disabled={loading}>
      {loading ? 'Scheduling...' : 'Reboot Collar'}
    </button>
    {#if rebootStatus}
      {#if rebootStatus.ok}
        <div class="status ok">Reboot scheduled — collar will restart on next WiFi connect (~{fmt(secsUntilNext())}).</div>
      {:else}
        <div class="status error">Failed to schedule reboot. Check if the server is reachable.</div>
      {/if}
    {/if}
  </section>

</main>

<style>
  main { max-width: 600px; margin: 2rem auto; padding: 0 1rem; }
  button { padding: 0.5rem 1rem; cursor: pointer; margin-bottom: 1rem; }
  section { border: 1px solid #ddd; border-radius: 6px; padding: 1.5rem; margin-top: 1.5rem; }
  h2 { margin-top: 0; }
  .big-timer { font-size: 3rem; font-weight: 700; margin: 0.25rem 0; }
  .overdue { color: #991b1b; font-weight: 600; }
  .action { background: #3b82f6; color: white; border: none; border-radius: 4px; padding: 0.6rem 1.2rem; font-size: 1rem; }
  .action.danger { background: #dc2626; }
  .action:disabled { opacity: 0.6; cursor: not-allowed; }
  .status { margin-top: 1rem; padding: 0.75rem; border-radius: 4px; }
  .ok    { background: #dcfce7; color: #166534; }
  .error { background: #fee2e2; color: #991b1b; }
  .next-connect { color: #64748b; font-size: 0.9rem; margin-top: 0.5rem; }
</style>
