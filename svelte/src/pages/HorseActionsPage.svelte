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
  let status          = $state(null);
  let loading         = $state(false);
  let calibCountdown  = $state(null);
  let heartbeatSecs   = $state(null);
  let tickInterval    = null;
  let pollInterval    = null;

  onMount(async () => {
    [horse, config] = await Promise.all([
      horses.getById(params.id),
      collarConfig.get(params.id),
    ]);

    const today = new Date().toISOString().slice(0, 10);
    const readings = await sensorReadings.getByHorse(params.id, today);
    lastReading = readings.at(-1) ?? null;

    startHeartbeatTick();
  });

  onDestroy(() => {
    clearInterval(tickInterval);
    clearInterval(pollInterval);
  });

  function startHeartbeatTick() {
    clearInterval(tickInterval);
    tickInterval = setInterval(() => {
      updateHeartbeatCountdown();
      if (calibCountdown !== null && calibCountdown > 0) calibCountdown -= 1;
    }, 1000);
    updateHeartbeatCountdown();
  }

  function updateHeartbeatCountdown() {
    if (!lastReading || !config) return;
    const lastMs  = new Date(lastReading.timestamp).getTime();
    const nextMs  = lastMs + config.heartbeatMs;
    const diffSec = Math.round((nextMs - Date.now()) / 1000);
    heartbeatSecs = Math.max(diffSec, 0);
  }

  async function triggerCalibrate() {
    if (!confirm('The collar LED will blink for 10 seconds. Make sure the horse is standing still. Continue?')) return;
    loading = true;
    status = null;
    try {
      await collarConfig.triggerRecalibrate(params.id);
      status = { ok: true, done: false };
      calibCountdown = heartbeatSecs ?? Math.ceil((config?.heartbeatMs ?? 300000) / 1000);
      startPolling();
    } catch {
      status = { ok: false };
    } finally {
      loading = false;
    }
  }

  const calibrationMessages = {
    success:          { ok: true,  text: '✓ Calibration completed successfully.' },
    failed_movement:  { ok: false, text: '✗ Calibration failed — movement detected. Make sure the horse is standing still and try again.' },
    unknown:          { ok: false, text: '✗ Calibration result unknown.' },
  };

  function startPolling() {
    clearInterval(pollInterval);
    pollInterval = setInterval(async () => {
      const latest = await collarConfig.get(params.id);
      if (!latest.recalibrate) {
        const result = calibrationMessages[latest.calibrationStatus] ?? calibrationMessages.unknown;
        status = { ok: result.ok, done: true, message: result.text };
        calibCountdown = null;
        clearInterval(pollInterval);
      }
    }, 10000);
  }

  function fmt(seconds) {
    if (seconds === null) return '--:--';
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
    <h2>Next Heartbeat</h2>
    {#if lastReading}
      <p>Last check-in: <strong>{formatDate(lastReading.timestamp)}</strong></p>
      <p class="big-timer">{fmt(heartbeatSecs)}</p>
      {#if heartbeatSecs === 0}
        <p class="overdue">Overdue — collar may be offline or out of range.</p>
      {/if}
    {:else}
      <p>No readings today — collar has not checked in yet.</p>
    {/if}
  </section>

  <section>
    <h2>Calibration</h2>
    <p>
      Triggers a recalibration on the next heartbeat. The collar LED will blink for 10 seconds —
      keep the horse standing still during that time.
    </p>
    <button class="action" onclick={triggerCalibrate} disabled={loading}>
      {loading ? 'Scheduling...' : 'Trigger Calibration'}
    </button>

    {#if status}
      {#if status.ok}
        {#if status.done}
          <div class="status" class:ok={status.ok} class:error={!status.ok}>
            {status.message}
          </div>
        {:else}
          <div class="status ok">
            <p>Calibration scheduled. Estimated wait:</p>
            <p class="timer">{calibCountdown > 0 ? fmt(calibCountdown) : 'Waiting for confirmation...'}</p>
          </div>
        {/if}
      {:else}
        <p class="status error">Failed to schedule calibration. Check if the server is reachable.</p>
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
  .action:disabled { opacity: 0.6; cursor: not-allowed; }
  .status { margin-top: 1rem; padding: 0.75rem; border-radius: 4px; }
  .ok    { background: #dcfce7; color: #166534; }
  .error { background: #fee2e2; color: #991b1b; }
  .timer { font-size: 1.4rem; margin: 0.25rem 0 0; font-weight: 700; }
</style>
