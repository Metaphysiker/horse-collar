<script>
  // @ts-nocheck
  import { onMount } from 'svelte';
  import { pop } from 'svelte-spa-router';
  import { horses } from '../services/horses.js';
  import { collarConfig } from '../services/collarConfig.js';

  let { params = {} } = $props();

  let horse   = $state(null);
  let config  = $state(null);
  let status  = $state(null);
  let loading = $state(false);

  onMount(async () => {
    [horse, config] = await Promise.all([
      horses.getById(params.id),
      collarConfig.get(params.id),
    ]);
  });

  async function save() {
    loading = true;
    status = null;
    try {
      config = await collarConfig.update(params.id, config);
      status = { ok: true, message: 'Saved. Changes will apply on the next heartbeat.' };
    } catch {
      status = { ok: false, message: 'Failed to save. Check if the server is reachable.' };
    } finally {
      loading = false;
    }
  }
</script>

<main>
  <button onclick={pop}>← Back</button>

  {#if horse}
    <h1>{horse.name} — Collar Config</h1>
  {/if}

  {#if config}
    <form onsubmit={(e) => { e.preventDefault(); save(); }}>

      <section>
        <h2>Current Main Settings</h2>

        <label>
          ReportInterval (microseconds)
          <span class="hint">Wie oft der BNO-Sensor reportet.</span>
          <div class="presets">
            <button type="button" class:active={config.reportInterval === 1000000}  onclick={() => config.reportInterval = 1000000}>1 Sekunde</button>
            <button type="button" class:active={config.reportInterval === 2000000}  onclick={() => config.reportInterval = 2000000}>2 Sekunden</button>
          </div>
          <input type="number" min="1" step="1" bind:value={config.reportInterval} />
        </label>

        <label>
          SleepTimerUs (microseconds)
          <span class="hint">Fester Timer, falls Interrupt pin ausgeht. Sollte länger sein als reportInterval</span>
          <div class="presets">
            <button type="button" class:active={config.sleepTimerUs === 1500000}  onclick={() => config.sleepTimerUs = 1500000}>1.5 Sekunde</button>
            <button type="button" class:active={config.sleepTimerUs === 2500000}  onclick={() => config.sleepTimerUs = 2500000}>2.5 Sekunden</button>
          </div>
          <input type="number" min="1" step="1" bind:value={config.sleepTimerUs} />
        </label>

          <label>
          HeartBeatInterval (microseconds)
          <span class="hint">Wie lange zum nächsten Heartbeat.</span>
          <div class="presets">
            <button type="button" class:active={config.heartBeatInterval === 60000000}  onclick={() => config.heartBeatInterval = 60000000}>1 Minute</button>
            <button type="button" class:active={config.heartBeatInterval === 180000000}  onclick={() => config.heartBeatInterval = 180000000}>3 Minuten</button>
          </div>
          <input type="number" min="1" step="1" bind:value={config.heartBeatInterval} />
        </label>

        <label>
          Send every N readings
          <span class="hint">Send a Reading after every N cycles</span>
          <div class="presets">
            <button type="button" class:active={config.sendEveryN === 5}   onclick={() => config.sendEveryN = 5}>5 — Collection</button>
            <button type="button" class:active={config.sendEveryN === 20}  onclick={() => config.sendEveryN = 20}>20 — Dense</button>
            <button type="button" class:active={config.sendEveryN === 60}  onclick={() => config.sendEveryN = 60}>60 — Normal</button>
            <button type="button" class:active={config.sendEveryN === 200} onclick={() => config.sendEveryN = 200}>200 — Battery</button>
          </div>
          <input type="number" min="1" step="1" bind:value={config.sendEveryN} />
        </label>

        <label>
          Roll Enter Deg
          <span class="hint">Ab wie viel Grad Roll Enter kommt.</span>
          <div class="presets">
            <button type="button" class:active={config.rollEnterDeg === 45}   onclick={() => config.rollEnterDeg = 45}>45</button>
            <button type="button" class:active={config.rollEnterDeg === 55}  onclick={() => config.rollEnterDeg = 55}>55</button>
            <button type="button" class:active={config.rollEnterDeg === 65}  onclick={() => config.rollEnterDeg = 65}>65</button>
            <button type="button" class:active={config.rollEnterDeg === 75} onclick={() => config.rollEnterDeg = 75}>75</button>
          </div>
          <input type="number" min="1" step="1" bind:value={config.rollEnterDeg} />
        </label>

        <label>
          Roll Exit Deg
          <span class="hint">Ab wie viel Grad Roll Exit kommt.</span>
          <div class="presets">
            <button type="button" class:active={config.rollExitDeg === 45}   onclick={() => config.rollExitDeg = 45}>45</button>
            <button type="button" class:active={config.rollExitDeg === 55}  onclick={() => config.rollExitDeg = 55}>55</button>
            <button type="button" class:active={config.rollExitDeg === 65}  onclick={() => config.rollExitDeg = 65}>65</button>
            <button type="button" class:active={config.rollExitDeg === 75} onclick={() => config.rollExitDeg = 75}>75</button>
          </div>
          <input type="number" min="1" step="1" bind:value={config.rollExitDeg} />
        </label>


        <label class="toggle-row">
          <span>
            UseTiltForPosture
            <span class="hint">Detect-Modus auswählen</span>
          </span>
          <input type="checkbox" bind:checked={config.useTiltForPosture} />
        </label>

        <label class="toggle-row">
          <span>
            Reboot
            <span class="hint">Reboot</span>
          </span>
          <input type="checkbox" bind:checked={config.reboot} />
        </label>

        <label class="toggle-row">
          <span>
            Recalibrate
            <span class="hint">Recalibrate</span>
          </span>
          <input type="checkbox" bind:checked={config.recalibrate} />
        </label>

        <select bind:value={config.powerMode}>
          <option value="active">active</option>
          <option value="maintenance">maintenance</option>
        </select>

        <label>
  Maintenance Wake Interval
  <span class="hint">Zeitintervall für Wartungs-Wakeup.</span>

  <div class="presets">
    <button
      type="button"
      class:active={config.maintenanceWakeIntervalUs === 15 * 60 * 1_000_000}
      onclick={() => config.maintenanceWakeIntervalUs = 15 * 60 * 1_000_000}
    >
      15 min
    </button>

    <button
      type="button"
      class:active={config.maintenanceWakeIntervalUs === 30 * 60 * 1_000_000}
      onclick={() => config.maintenanceWakeIntervalUs = 30 * 60 * 1_000_000}
    >
      30 min
    </button>

    <button
      type="button"
      class:active={config.maintenanceWakeIntervalUs === 60 * 60 * 1_000_000}
      onclick={() => config.maintenanceWakeIntervalUs = 60 * 60 * 1_000_000}
    >
      60 min
    </button>
  </div>

  <input
    type="number"
    min="1"
    step="1"
    bind:value={config.maintenanceWakeIntervalUs}
  />
</label>

      </section>

      <section>
        <h2>Notifications</h2>

        <label class="toggle-row">
          <span>
            Enable ntfy alerts
            <span class="hint">Send a push notification when an alert is detected. Requires ntfy to be configured on the server.</span>
          </span>
          <input type="checkbox" bind:checked={config.ntfyEnabled} />
        </label>

        <label class="toggle-row">
          <span>
            Lying-down alert
            <span class="hint">Send a push notification when tilt exceeds 90° (horse on its side). Independent of the general ntfy toggle.</span>
          </span>
          <input type="checkbox" bind:checked={config.lyingDownAlertEnabled} />
        </label>

        <label class="toggle-row">
          <span>
            High roll alert
            <span class="hint">Send a push notification when estimated horse roll exceeds 45° to either side.</span>
          </span>
          <input type="checkbox" bind:checked={config.highRollAlertEnabled} />
        </label>

        <label>
          Roll baseline (sensor °)
          <span class="hint">Sensor roll when the horse is standing normally. Check recent Standing readings and enter the average roll. Default 5 (original calibration).</span>
          <input type="number" min="-90" max="90" step="0.5" bind:value={config.rollBaseline} />
        </label>

      </section>

      <button type="submit" class="save" disabled={loading}>
        {loading ? 'Saving...' : 'Save'}
      </button>

      {#if status}
        <p class="status" class:ok={status.ok} class:error={!status.ok}>{status.message}</p>
      {/if}

    </form>
  {/if}
</main>

<style>
  main { max-width: 600px; margin: 2rem auto; padding: 0 1rem; }
  button { padding: 0.5rem 1rem; cursor: pointer; margin-bottom: 1rem; }
  section { border: 1px solid #ddd; border-radius: 6px; padding: 1.5rem; margin-bottom: 1rem; }
  h2 { margin-top: 0; }
  label {
    display: flex;
    flex-direction: column;
    gap: 0.25rem;
    margin-bottom: 1rem;
    font-weight: 600;
  }
  label:last-child { margin-bottom: 0; }
  .hint { font-weight: 400; font-size: 0.85rem; color: #64748b; }
  input { padding: 0.5rem; font-size: 1rem; border: 1px solid #ddd; border-radius: 4px; }
  .presets { display: flex; gap: 0.5rem; margin-bottom: 0.4rem; }
  .presets button { padding: 0.3rem 0.75rem; border: 1px solid #ddd; border-radius: 4px; cursor: pointer; background: white; font-size: 0.85rem; font-weight: 400; }
  .presets button.active { background: #1e293b; color: white; border-color: #1e293b; }
  .save { background: #1e293b; color: white; border: none; border-radius: 4px; padding: 0.6rem 1.5rem; font-size: 1rem; }
  .save:disabled { opacity: 0.6; cursor: not-allowed; }
  .status { padding: 0.75rem; border-radius: 4px; margin-top: 1rem; }
  .ok    { background: #dcfce7; color: #166534; }
  .error { background: #fee2e2; color: #991b1b; }
  .toggle-row { flex-direction: row; justify-content: space-between; align-items: flex-start; }
  .toggle-row input[type="checkbox"] { width: 1.25rem; height: 1.25rem; margin-top: 0.1rem; flex-shrink: 0; }
</style>
