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
        <h2>Timing</h2>

        <label>
          Sleep interval (seconds)
          <span class="hint">How long the collar sleeps between readings — lower = more data, shorter battery</span>
          <div class="presets">
            <button type="button" class:active={config.sleepSeconds === 1}  onclick={() => config.sleepSeconds = 1}>1s — Max</button>
            <button type="button" class:active={config.sleepSeconds === 2}  onclick={() => config.sleepSeconds = 2}>2s — Collection</button>
            <button type="button" class:active={config.sleepSeconds === 10} onclick={() => config.sleepSeconds = 10}>10s — Dense</button>
            <button type="button" class:active={config.sleepSeconds === 30} onclick={() => config.sleepSeconds = 30}>30s — Normal</button>
          </div>
          <input type="number" min="1" max="300" step="1" bind:value={config.sleepSeconds} />
        </label>

        <label>
          Send every N readings
          <span class="hint">Upload a batch after this many wake cycles — higher = less WiFi, better battery</span>
          <div class="presets">
            <button type="button" class:active={config.sendEveryN === 5}   onclick={() => config.sendEveryN = 5}>5 — Collection</button>
            <button type="button" class:active={config.sendEveryN === 20}  onclick={() => config.sendEveryN = 20}>20 — Dense</button>
            <button type="button" class:active={config.sendEveryN === 60}  onclick={() => config.sendEveryN = 60}>60 — Normal</button>
            <button type="button" class:active={config.sendEveryN === 200} onclick={() => config.sendEveryN = 200}>200 — Battery</button>
          </div>
          <input type="number" min="1" max="1000" step="1" bind:value={config.sendEveryN} />
        </label>

      </section>

      <section>
        <h2>BaselineShift Sensitivity</h2>

        <label>
          Accel delta threshold (m/s²)
          <span class="hint">How much acceleration must change from the running average to trigger an alert — lower = more sensitive</span>
          <input type="number" min="0.1" max="10" step="0.1" bind:value={config.changeAccel} />
        </label>

        <label>
          Pitch delta threshold (°)
          <span class="hint">How many degrees pitch must change from baseline</span>
          <input type="number" min="1" max="90" step="1" bind:value={config.changePitch} />
        </label>

        <label>
          Roll delta threshold (°)
          <span class="hint">How many degrees roll must change from baseline</span>
          <input type="number" min="1" max="90" step="1" bind:value={config.changeRoll} />
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
