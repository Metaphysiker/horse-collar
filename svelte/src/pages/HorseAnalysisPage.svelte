<script>
  // @ts-nocheck
  import { onMount } from 'svelte';
  import { pop } from 'svelte-spa-router';
  import { horses } from '../services/horses.js';
  import { sensorReadings } from '../services/sensorReadings.js';

  let { params = {} } = $props();

  let horse       = $state(null);
  let loading     = $state(false);
  let allReadings = $state([]);

  // Date range
  function todayString() { return new Date().toISOString().slice(0, 10); }
  function yesterdayString() {
    const d = new Date(); d.setDate(d.getDate() - 1);
    return d.toISOString().slice(0, 10);
  }

  let fromDate = $state(yesterdayString());
  let toDate   = $state(todayString());
  let stillThreshold = $state(0.2);

  onMount(async () => {
    horse = await horses.getById(params.id);
  });

  async function load() {
    loading = true;
    allReadings = [];

    // Fetch all days in range
    const start = new Date(fromDate);
    const end   = new Date(toDate);
    const days  = [];
    for (let d = new Date(start); d <= end; d.setDate(d.getDate() + 1))
      days.push(d.toISOString().slice(0, 10));

    const results = await Promise.all(days.map(d => sensorReadings.getByHorse(params.id, d)));
    allReadings = results.flat();
    loading = false;
  }

  function tilt(r) {
    return (r.pitch != null && r.roll != null) ? Math.sqrt(r.pitch ** 2 + r.roll ** 2) : null;
  }

  function stats(values) {
    const v = values.filter(x => x != null);
    if (!v.length) return null;
    const min = Math.min(...v);
    const max = Math.max(...v);
    const avg = v.reduce((a, b) => a + b, 0) / v.length;
    return { min, max, avg, count: v.length };
  }

  function fmt(v, dec = 1) { return v == null ? '—' : v.toFixed(dec); }

  // ── Computed analyses ────────────────────────────────────────────────────────

  const stillReadings = $derived(
    allReadings.filter(r => r.acceleration != null && r.acceleration < stillThreshold)
  );

  const stillStats = $derived(() => {
    const s = stillReadings;
    if (!s.length) return null;
    return {
      count:  s.length,
      pitch:  stats(s.map(r => r.pitch)),
      roll:   stats(s.map(r => r.roll)),
      tilt:   stats(s.map(r => tilt(r))),
      accel:  stats(s.map(r => r.acceleration)),
    };
  });

  const extremes = $derived(() => {
    if (!allReadings.length) return null;
    const withTilt = allReadings.map(r => ({ ...r, tiltVal: tilt(r) }));
    const pick = (arr, fn) => arr.reduce((best, r) => fn(r) > fn(best) ? r : best);
    const pickMin = (arr, fn) => arr.reduce((best, r) => fn(r) < fn(best) ? r : best);
    return {
      maxTilt:  pick(withTilt.filter(r => r.tiltVal != null), r => r.tiltVal),
      minTilt:  pickMin(withTilt.filter(r => r.tiltVal != null), r => r.tiltVal),
      maxAccel: pick(allReadings.filter(r => r.acceleration != null), r => r.acceleration),
      maxGyro:  pick(allReadings.filter(r => r.angularVelocity != null), r => r.angularVelocity),
      maxPitch: pick(allReadings.filter(r => r.pitch != null), r => r.pitch),
      minPitch: pickMin(allReadings.filter(r => r.pitch != null), r => r.pitch),
      maxRoll:  pick(allReadings.filter(r => r.roll != null), r => r.roll),
      minRoll:  pickMin(allReadings.filter(r => r.roll != null), r => r.roll),
    };
  });

  const activity = $derived(() => {
    if (!allReadings.length) return null;
    const counts = {};
    for (const r of allReadings)
      counts[r.state] = (counts[r.state] ?? 0) + 1;
    const total = allReadings.length;
    return Object.entries(counts)
      .sort((a, b) => b[1] - a[1])
      .map(([state, count]) => ({ state, count, pct: (count / total * 100) }));
  });

  const stateColors = {
    Standing:  '#22c55e',
    Moving:    '#3b82f6',
    LyingDown: '#f59e0b',
    Rolling:   '#ef4444',
    Alert:     '#f97316',
    Emergency: '#dc2626',
  };
</script>

<main>
  <button onclick={pop}>← Back</button>

  {#if horse}
    <h1>{horse.name} — Analysis</h1>
  {/if}

  <section class="controls">
    <label>
      From
      <input type="date" bind:value={fromDate} max={toDate} />
    </label>
    <label>
      To
      <input type="date" bind:value={toDate} min={fromDate} max={todayString()} />
    </label>
    <label>
      Still threshold (accel &lt;)
      <input type="number" min="0.05" max="1" step="0.05" bind:value={stillThreshold} />
    </label>
    <button class="load-btn" onclick={load} disabled={loading}>
      {loading ? 'Loading…' : 'Load'}
    </button>
  </section>

  {#if allReadings.length > 0}
    <p class="count">{allReadings.length} readings loaded</p>

    <!-- ── Still periods ─────────────────────────────────────────────────── -->
    <section>
      <h2>Still Periods <span class="sub">accel &lt; {stillThreshold}</span></h2>
      {#if stillStats()}
        {@const s = stillStats()}
        <p class="desc">
          {s.count} readings where the horse was barely moving ({(s.count / allReadings.length * 100).toFixed(0)}% of total).
          These are the best readings for determining head orientation.
        </p>
        <table>
          <thead><tr><th>Metric</th><th>Min</th><th>Avg</th><th>Max</th></tr></thead>
          <tbody>
            <tr><td>Pitch (sensor)</td><td>{fmt(s.pitch?.min)}°</td><td>{fmt(s.pitch?.avg)}°</td><td>{fmt(s.pitch?.max)}°</td></tr>
            <tr><td>Roll (sensor)</td><td>{fmt(s.roll?.min)}°</td><td>{fmt(s.roll?.avg)}°</td><td>{fmt(s.roll?.max)}°</td></tr>
            <tr><td>Tilt</td><td>{fmt(s.tilt?.min)}°</td><td>{fmt(s.tilt?.avg)}°</td><td>{fmt(s.tilt?.max)}°</td></tr>
            <tr><td>Acceleration</td><td>{fmt(s.accel?.min, 2)}</td><td>{fmt(s.accel?.avg, 2)}</td><td>{fmt(s.accel?.max, 2)}</td></tr>
          </tbody>
        </table>
      {:else}
        <p>No still readings found.</p>
      {/if}
    </section>

    <!-- ── Extreme points ────────────────────────────────────────────────── -->
    <section>
      <h2>Extreme Points</h2>
      {#if extremes()}
        {@const e = extremes()}
        <table>
          <thead><tr><th>Metric</th><th>Value</th><th>State</th><th>Time</th></tr></thead>
          <tbody>
            <tr><td>Max tilt</td><td>{fmt(e.maxTilt.tiltVal)}°</td><td>{e.maxTilt.state}</td><td>{new Date(e.maxTilt.timestamp).toLocaleTimeString()}</td></tr>
            <tr><td>Min tilt</td><td>{fmt(e.minTilt.tiltVal)}°</td><td>{e.minTilt.state}</td><td>{new Date(e.minTilt.timestamp).toLocaleTimeString()}</td></tr>
            <tr><td>Max acceleration</td><td>{fmt(e.maxAccel.acceleration, 2)}</td><td>{e.maxAccel.state}</td><td>{new Date(e.maxAccel.timestamp).toLocaleTimeString()}</td></tr>
            <tr><td>Max gyro</td><td>{fmt(e.maxGyro.angularVelocity, 2)}</td><td>{e.maxGyro.state}</td><td>{new Date(e.maxGyro.timestamp).toLocaleTimeString()}</td></tr>
            <tr><td>Max pitch</td><td>{fmt(e.maxPitch.pitch)}°</td><td>{e.maxPitch.state}</td><td>{new Date(e.maxPitch.timestamp).toLocaleTimeString()}</td></tr>
            <tr><td>Min pitch</td><td>{fmt(e.minPitch.pitch)}°</td><td>{e.minPitch.state}</td><td>{new Date(e.minPitch.timestamp).toLocaleTimeString()}</td></tr>
            <tr><td>Max roll</td><td>{fmt(e.maxRoll.roll)}°</td><td>{e.maxRoll.state}</td><td>{new Date(e.maxRoll.timestamp).toLocaleTimeString()}</td></tr>
            <tr><td>Min roll</td><td>{fmt(e.minRoll.roll)}°</td><td>{e.minRoll.state}</td><td>{new Date(e.minRoll.timestamp).toLocaleTimeString()}</td></tr>
          </tbody>
        </table>
      {/if}
    </section>

    <!-- ── Activity ──────────────────────────────────────────────────────── -->
    <section>
      <h2>Activity Breakdown</h2>
      {#if activity()}
        <div class="activity-bars">
          {#each activity() as { state, count, pct }}
            <div class="activity-row">
              <span class="state-name">{state}</span>
              <div class="bar-track">
                <div class="bar-fill" style="width:{pct}%; background:{stateColors[state] ?? '#888'}"></div>
              </div>
              <span class="activity-count">{count} <span class="pct">({pct.toFixed(0)}%)</span></span>
            </div>
          {/each}
        </div>
        <p class="desc">~{allReadings.length} seconds of data (~{(allReadings.length / 3600).toFixed(1)} hours at 1s interval)</p>
      {/if}
    </section>

  {:else if !loading}
    <p class="empty">Select a date range and press Load.</p>
  {/if}
</main>

<style>
  main { max-width: 700px; margin: 2rem auto; padding: 0 1rem; }
  h1 { margin-bottom: 1.5rem; }
  h2 { margin: 0 0 0.75rem; font-size: 1.1rem; }
  h2 .sub { font-size: 0.8rem; font-weight: 400; color: #64748b; margin-left: 0.5rem; }
  section { border: 1px solid #ddd; border-radius: 6px; padding: 1.25rem; margin-bottom: 1.25rem; }
  section.controls { display: flex; flex-wrap: wrap; gap: 1rem; align-items: flex-end; }
  label { display: flex; flex-direction: column; gap: 0.25rem; font-size: 0.85rem; font-weight: 600; color: #475569; }
  input { padding: 0.4rem 0.6rem; border: 1px solid #ddd; border-radius: 4px; font-size: 0.95rem; }
  input[type=number] { width: 80px; }
  .load-btn { background: #1e293b; color: white; border: none; border-radius: 4px; padding: 0.5rem 1.25rem; font-size: 0.95rem; cursor: pointer; align-self: flex-end; }
  .load-btn:disabled { opacity: 0.6; cursor: not-allowed; }
  .count { color: #64748b; font-size: 0.85rem; margin: -0.5rem 0 1rem; }
  .desc { font-size: 0.85rem; color: #64748b; margin-bottom: 0.75rem; }
  table { width: 100%; border-collapse: collapse; font-size: 0.9rem; }
  th, td { text-align: left; padding: 0.4rem 0.6rem; border-bottom: 1px solid #eee; }
  th { font-weight: 600; color: #475569; }
  .activity-bars { display: flex; flex-direction: column; gap: 0.6rem; margin-bottom: 0.75rem; }
  .activity-row { display: flex; align-items: center; gap: 0.75rem; }
  .state-name { width: 90px; font-size: 0.85rem; font-weight: 600; }
  .bar-track { flex: 1; height: 18px; background: #f1f5f9; border-radius: 4px; overflow: hidden; }
  .bar-fill { height: 100%; border-radius: 4px; transition: width 0.4s; }
  .activity-count { font-size: 0.85rem; min-width: 80px; text-align: right; }
  .pct { color: #94a3b8; }
  .empty { color: #94a3b8; font-style: italic; }
  button { padding: 0.5rem 1rem; cursor: pointer; margin-bottom: 1rem; }
</style>
