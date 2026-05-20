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
        <h2>Detection</h2>

        <label>
          Tilt threshold (degrees)
          <span class="hint">Pitch or roll beyond this angle → horse is tilted</span>
          <input type="number" min="10" max="90" step="1" bind:value={config.tiltThresholdDegrees} />
        </label>

        <label>
          Lying confirm time (ms)
          <span class="hint">Tilt must hold this long before state changes to LyingDown</span>
          <input type="number" min="1000" step="1000" bind:value={config.lyingConfirmMs} />
        </label>
      </section>

      <section>
        <h2>Timing</h2>

        <label>
          Heartbeat interval (ms)
          <span class="hint">How often the collar checks in when no state change occurs</span>
          <input type="number" min="10000" step="1000" bind:value={config.heartbeatMs} />
        </label>

        <label>
          Sample interval (ms)
          <span class="hint">How often the BNO085 is read</span>
          <input type="number" min="100" max="5000" step="100" bind:value={config.sampleIntervalMs} />
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
  .save { background: #1e293b; color: white; border: none; border-radius: 4px; padding: 0.6rem 1.5rem; font-size: 1rem; }
  .save:disabled { opacity: 0.6; cursor: not-allowed; }
  .status { padding: 0.75rem; border-radius: 4px; margin-top: 1rem; }
  .ok    { background: #dcfce7; color: #166534; }
  .error { background: #fee2e2; color: #991b1b; }
</style>
