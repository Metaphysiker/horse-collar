<script>
  // @ts-nocheck
  import { onMount, onDestroy } from 'svelte';
  import { pop, push } from 'svelte-spa-router';
  import { horses } from '../services/horses.js';
  import { sensorReadings } from '../services/sensorReadings.js';
  import { deviceStatus } from '../services/deviceStatus.js';
  import { collarConfig } from '../services/collarConfig.js';
  import SensorReadingRow from '../components/SensorReadingRow.svelte';
  import { formatDate, formatDateOnly } from '../utils/formatDate.js';

  let { params = {} } = $props();

  let horse = $state(null);
  let status = $state(null);
  let readings = $state([]);
  let selectedDate = $state(todayString());
  let selectedIds = $state(new Set());
  let baselineStatus = $state(null);

  const selectedReadings = $derived(readings.filter(r => selectedIds.has(r.id)));
  const avgRoll = $derived(
    selectedReadings.length
      ? selectedReadings.reduce((s, r) => s + r.roll, 0) / selectedReadings.length
      : null
  );

  function toggleSelect(id) {
    const next = new Set(selectedIds);
    next.has(id) ? next.delete(id) : next.add(id);
    selectedIds = next;
  }

  function eulerToQuat(pitchDeg, rollDeg, yawDeg) {
    const p = pitchDeg * Math.PI / 360;
    const r = rollDeg  * Math.PI / 360;
    const y = yawDeg   * Math.PI / 360;
    const cp = Math.cos(p), sp = Math.sin(p);
    const cr = Math.cos(r), sr = Math.sin(r);
    const cy = Math.cos(y), sy = Math.sin(y);
    return {
      w: cp*cr*cy - sp*sr*sy,
      x: sp*cr*cy + cp*sr*sy,
      y: cp*sr*cy - sp*cr*sy,
      z: cp*cr*sy + sp*sr*cy,
    };
  }

  function averageQuats(quats) {
    const ref = quats[0];
    const sum = { w: 0, x: 0, y: 0, z: 0 };
    for (const q of quats) {
      const sign = (q.w*ref.w + q.x*ref.x + q.y*ref.y + q.z*ref.z) >= 0 ? 1 : -1;
      sum.w += sign * q.w; sum.x += sign * q.x;
      sum.y += sign * q.y; sum.z += sign * q.z;
    }
    const n = Math.sqrt(sum.w**2 + sum.x**2 + sum.y**2 + sum.z**2);
    return { w: sum.w/n, x: sum.x/n, y: sum.y/n, z: sum.z/n };
  }

  async function setOrientationBaseline() {
    const valid = selectedReadings.filter(r => r.pitch != null && r.roll != null);
    if (valid.length === 0) return;
    const quats = valid.map(r => eulerToQuat(r.pitch, r.roll, r.yaw ?? 0));
    const avg = averageQuats(quats);
    baselineStatus = null;
    try {
      const config = await collarConfig.get(params.id);
      await collarConfig.update(params.id, {
        ...config, refQw: avg.w, refQx: avg.x, refQy: avg.y, refQz: avg.z
      });
      baselineStatus = { ok: true, message: `Orientation baseline set from ${valid.length} readings` };
      selectedIds = new Set();
    } catch {
      baselineStatus = { ok: false, message: 'Failed to set orientation baseline.' };
    }
  }

  async function setBaseline() {
    if (avgRoll == null) return;
    const rounded = Math.round(avgRoll * 10) / 10;
    baselineStatus = null;
    try {
      const config = await collarConfig.get(params.id);
      await collarConfig.update(params.id, { ...config, rollBaseline: rounded });
      baselineStatus = { ok: true, message: `Roll baseline set to ${rounded}°` };
      selectedIds = new Set();
    } catch {
      baselineStatus = { ok: false, message: 'Failed to update baseline.' };
    }
  }

  function todayString() {
    return new Date().toISOString().slice(0, 10);
  }

  let refreshInterval;

  onMount(async () => {
    [horse, status] = await Promise.all([
      horses.getById(params.id),
      deviceStatus.getLatest(params.id).catch(() => null),
    ]);
    await loadReadings();
    refreshInterval = setInterval(async () => {
      if (selectedDate === todayString()) {
        status = await deviceStatus.getLatest(params.id).catch(() => status);
        await loadReadings();
      }
    }, 5000);
  });

  onDestroy(() => clearInterval(refreshInterval));

  async function loadReadings() {
    readings = await sensorReadings.getByHorse(params.id, selectedDate, 200);
  }

  async function remove(id) {
    await sensorReadings.delete(params.id, id);
    readings = readings.filter(r => r.id !== id);
  }

</script>

<main>
  <button onclick={pop}>← Back</button>

  {#if horse}
    <div class="header">
      <h1>{horse.name}</h1>
      <button onclick={() => push(`/horses/${params.id}/analysis`)}>Analysis</button>
      <button onclick={() => push(`/horses/${params.id}/actions`)}>Device Actions</button>
    </div>
    <div class="device-status">
      <span class="status-label">Batterie</span>
      {#if status}
        <span class="battery-bar">
          <span class="battery-fill" style="width: {status.batteryPercent}%; background: {status.batteryPercent < 20 ? '#ef4444' : status.batteryPercent < 50 ? '#f59e0b' : '#22c55e'}"></span>
        </span>
        <span class="battery-text">{status.batteryPercent}% &nbsp;·&nbsp; {status.batteryVoltage.toFixed(2)} V</span>
        {#if status.batteryVoltage < 3.4}
          <span class="badge badge-critical">🔋 Schläft (Batterie kritisch)</span>
        {/if}
        <span class="bno-badge" class:bno-ok={status.bnoConnected !== false} class:bno-err={status.bnoConnected === false}>
          BNO {status.bnoConnected === false ? '✗' : '✓'}
        </span>
        <span class="status-time">Zuletzt gesehen: {formatDate(status.timestamp)}</span>
      {:else}
        <span class="no-device">Noch keine Gerätedaten verfügbar</span>
      {/if}
    </div>
  {/if}

  {#if readings.length > 0}
    {@const latest = readings.at(-1)}
    {#if latest.alarmState === 'Emergency'}
      <div class="alert-banner emergency">
        🚨 Emergency — horse has been lying down for over 2 hours. Check on the animal immediately.
      </div>
    {:else if latest.alarmState === 'Alert'}
      <div class="alert-banner alert">
        ⚠️ Alert — {latest.alertReason ?? 'Unusual activity detected'}. Consider checking in.
      </div>
    {/if}
    <div class="orientation-card">
      <div class="sensor-strip">
        <div class="sensor-stat">
          <span class="stat-label">Pitch</span>
          <span class="stat-value">{latest.pitch?.toFixed(1) ?? '—'}°</span>
        </div>
        <div class="sensor-stat">
          <span class="stat-label">Roll</span>
          <span class="stat-value">{latest.roll?.toFixed(1) ?? '—'}°</span>
        </div>
        <div class="sensor-stat">
          <span class="stat-label">Yaw</span>
          <span class="stat-value">{latest.yaw?.toFixed(1) ?? '—'}°</span>
        </div>
        <div class="sensor-stat">
          <span class="stat-label">Tilt</span>
          <span class="stat-value">{(latest.pitch != null && latest.roll != null) ? Math.sqrt(latest.pitch**2 + latest.roll**2).toFixed(1) : '—'}°</span>
        </div>
        <div class="sensor-stat">
          <span class="stat-label">Accel</span>
          <span class="stat-value">{latest.acceleration?.toFixed(2) ?? '—'} m/s²</span>
        </div>
        <div class="sensor-stat">
          <span class="stat-label">Gyro</span>
          <span class="stat-value">{latest.angularVelocity?.toFixed(2) ?? '—'} rad/s</span>
        </div>
        <div class="sensor-stat">
          <span class="stat-label">Temp</span>
          <span class="stat-value">{latest.temperature != null ? latest.temperature.toFixed(1) + ' °C' : '—'}</span>
        </div>
        {#if latest.alertReason}
          <div class="sensor-stat reason">
            <span class="stat-label">Reason</span>
            <span class="stat-value">{latest.alertReason}</span>
          </div>
        {/if}
      </div>
    </div>
  {/if}

  <div class="toolbar">
    <h2>Sensor Readings</h2>
    <div class="date-picker">
      <span>{formatDateOnly(selectedDate)}</span>
      <input
        type="date"
        bind:value={selectedDate}
        onchange={loadReadings}
        max={todayString()}
      />
    </div>
  </div>

{#if selectedIds.size > 0}
    <div class="baseline-bar">
      <span>{selectedIds.size} readings selected &nbsp;·&nbsp; avg roll: <strong>{avgRoll?.toFixed(1)}°</strong></span>
      <button class="baseline-btn" onclick={setBaseline}>Set roll baseline</button>
      <button class="baseline-btn" onclick={setOrientationBaseline}>Set orientation baseline</button>
      <button class="clear-btn" onclick={() => selectedIds = new Set()}>Clear</button>
    </div>
  {/if}
  {#if baselineStatus}
    <p class="baseline-status" class:ok={baselineStatus.ok} class:error={!baselineStatus.ok}>{baselineStatus.message}</p>
  {/if}

  {#if readings.length === 0}
    <p>No readings for this day.</p>
  {:else}
    <table>
      <thead>
        <tr>
          <th></th>
          <th>Time</th>
          <th>State</th>
          <th>Pitch</th>
          <th>Roll</th>
          <th>Yaw</th>
          <th>Side</th>
          <th>Horse Roll~</th>
          <th>Tilt</th>
          <th>Accel</th>
          <th>Gyro</th>
          <th>Temp</th>
          <th></th>
        </tr>
      </thead>
      <tbody>
        {#each [...readings].reverse() as reading (reading.id)}
          <SensorReadingRow
            {reading}
            selected={selectedIds.has(reading.id)}
            ontoggle={() => toggleSelect(reading.id)}
            ondelete={() => remove(reading.id)}
          />
        {/each}
      </tbody>
    </table>
  {/if}
</main>

<style>
  main { max-width: 900px; margin: 2rem auto; padding: 0 1rem; }
  button { margin-bottom: 1rem; cursor: pointer; }
  .header { display: flex; align-items: center; justify-content: space-between; }
  .device-status {
    display: flex; align-items: center; gap: 0.75rem;
    background: #f8fafc; border: 1px solid #e2e8f0; border-radius: 0.5rem;
    padding: 0.5rem 1rem; margin-bottom: 1.25rem; font-size: 0.9rem;
  }
  .status-label { font-weight: 600; color: #475569; }
  .battery-bar { width: 80px; height: 12px; background: #e2e8f0; border-radius: 6px; overflow: hidden; }
  .battery-fill { display: block; height: 100%; border-radius: 6px; transition: width 0.3s; }
  .battery-text { color: #1e293b; font-weight: 500; }
  .status-time { margin-left: auto; color: #94a3b8; font-size: 0.82rem; }
  .no-device { color: #94a3b8; font-style: italic; }
  .badge { font-size: 0.8rem; font-weight: 600; padding: 0.2rem 0.5rem; border-radius: 4px; }
  .badge-critical { background: #fee2e2; color: #991b1b; }
  .bno-badge { font-size: 0.8rem; font-weight: 600; padding: 0.2rem 0.5rem; border-radius: 4px; }
  .bno-ok  { background: #dcfce7; color: #166534; }
  .bno-err { background: #fee2e2; color: #991b1b; }
  .toolbar { display: flex; align-items: center; justify-content: space-between; margin-bottom: 1rem; }
  .toolbar h2 { margin: 0; }
  .date-picker { display: flex; align-items: center; gap: 0.5rem; }
  .date-picker span { font-size: 0.95rem; color: #475569; }
  input[type="date"] { padding: 0.4rem 0.6rem; font-size: 1rem; }
  .alert-banner { padding: 0.9rem 1.2rem; border-radius: 0.5rem; font-weight: 600; margin-bottom: 1rem; }
  .alert-banner.alert     { background: #fef3c7; color: #92400e; border: 1px solid #fcd34d; }
  .alert-banner.emergency { background: #fee2e2; color: #7f1d1d; border: 1px solid #fca5a5; font-size: 1.05rem; }
  .sensor-strip {
    display: flex; flex-wrap: wrap; gap: 0.5rem;
    padding: 0.75rem 1rem; background: #f8fafc;
    border: 1px solid #e2e8f0; border-top: none; border-radius: 0 0 0.5rem 0.5rem;
  }
  .sensor-stat {
    display: flex; flex-direction: column; align-items: center;
    min-width: 70px; padding: 0.3rem 0.6rem;
    background: white; border: 1px solid #e2e8f0; border-radius: 6px;
  }
  .sensor-stat.reason { min-width: auto; }
  .stat-label { font-size: 0.72rem; color: #94a3b8; font-weight: 600; text-transform: uppercase; letter-spacing: 0.04em; }
  .stat-value { font-size: 0.9rem; font-weight: 600; color: #1e293b; margin-top: 0.1rem; }
  table { width: 100%; border-collapse: collapse; }
  th, :global(td) { text-align: left; padding: 0.4rem 0.5rem; border-bottom: 1px solid #ddd; font-size: 0.9rem; }
  th { font-weight: 600; white-space: nowrap; }
  .baseline-bar {
    display: flex; align-items: center; gap: 0.75rem; flex-wrap: wrap;
    background: #f0f9ff; border: 1px solid #bae6fd; border-radius: 0.5rem;
    padding: 0.6rem 1rem; margin-bottom: 0.75rem; font-size: 0.9rem; color: #0369a1;
  }
  .baseline-btn { background: #0369a1; color: white; border: none; border-radius: 4px; padding: 0.35rem 0.9rem; font-size: 0.85rem; cursor: pointer; margin: 0; }
  .clear-btn { background: none; border: 1px solid #bae6fd; border-radius: 4px; padding: 0.35rem 0.75rem; font-size: 0.85rem; cursor: pointer; color: #0369a1; margin: 0; }
  .baseline-status { padding: 0.5rem 0.75rem; border-radius: 4px; font-size: 0.88rem; margin-bottom: 0.75rem; }
  .baseline-status.ok    { background: #dcfce7; color: #166534; }
  .baseline-status.error { background: #fee2e2; color: #991b1b; }
</style>
