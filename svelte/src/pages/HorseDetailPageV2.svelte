Here is your complete, fully integrated `HorseDetailPageV2.svelte` file rewritten for Svelte 5 using Option B.

It handles your flat database collection schema safely, avoids `undefined` crashes from legacy records, and adds a clean toggle interface to jump back and forth between **Raw** and **Normalized** data streams instantly.

```html
<script>
  // @ts-nocheck
  import { onMount, onDestroy } from 'svelte';
  import { pop, push } from 'svelte-spa-router';
  import { horses } from '../services/horses.js';
  import { sensorReadingsV2 } from '../services/sensorReadingsV2.js';
  import { deviceStatus } from '../services/deviceStatus.js';
  import { collarConfig } from '../services/collarConfig.js';
  import SensorReadingRow from '../components/SensorReadingRow.svelte';
  import HorseHeadViewer from '../components/HorseHeadViewer.svelte';
  import { formatDate, formatDateOnly } from '../utils/formatDate.js';

  let { params = {} } = $props();

  let horse = $state(null);
  let status = $state(null);
  let config = $state(null);
  let readings = $state([]); // Flat rows directly from Option B database collection
  let viewMode = $state('normalized'); // Active channel modifier: 'normalized' or 'raw'
  let selectedDate = $state(todayString());
  let selectedIds = $state(new Set());
  let baselineStatus = $state(null);
  let viewReading = $state(null);
  let viewCount = $state(10);

  // Filter the flat database array reactively based on your chosen display mode
  const filteredReadings = $derived(
    readings.filter(r => r && r.readingType?.toLowerCase() === viewMode.toLowerCase())
  );

  // Derive historical window frames targets cleanly from active view array
  const viewReadings = $derived.by(() => {
    if (!viewReading) return [];
    const idx = filteredReadings.findIndex(r => r.id === viewReading.id);
    if (idx < 0) return [viewReading];
    const start = Math.max(0, idx - viewCount);
    return filteredReadings.slice(start, idx + 1);
  });

  const viewMax = $derived.by(() => {
    if (!viewReading) return 100;
    const idx = filteredReadings.findIndex(r => r.id === viewReading.id);
    return idx > 0 ? idx : 1;
  });

  const selectedReadings = $derived(filteredReadings.filter(r => selectedIds.has(r.id)));

  // Calculate live average roll over whichever stream you are looking at
  const avgRoll = $derived.by(() => {
    if (!selectedReadings.length) return null;
    const sum = selectedReadings.reduce((acc, r) => {
      if (r.qw == null) return acc;
      const gx = 2.0 * (r.qx * r.qz - r.qy * r.qw);
      const roll = Math.asin(Math.max(-1, Math.min(1, Math.abs(gx)))) * 180 / Math.PI;
      return acc + roll;
    }, 0);
    return sum / selectedReadings.length;
  });

  function toggleSelect(id) {
    const next = new Set(selectedIds);
    next.has(id) ? next.delete(id) : next.add(id);
    selectedIds = next;
  }

  // ── Calibration wizard ───────────────────────────────────────────────────────

  let calibOpen = $state(false);
  let calibSlots = $state({ left: null, right: null, front: null, back: null });
  let calibStatus = $state(null);

  const baselineSet = $derived(
    config != null && (config.refQx !== 0 || config.refQy !== 0 || config.refQz !== 0 || config.refQw !== 1)
  );
  const calibReady = $derived(
    calibSlots.left && calibSlots.right && calibSlots.front && calibSlots.back && baselineSet
  );

  function applyBaseline(qw, qx, qy, qz) {
    const rw =  (config?.refQw ?? 1);
    const rx = -(config?.refQx ?? 0);
    const ry = -(config?.refQy ?? 0);
    const rz = -(config?.refQz ?? 0);
    return {
      w: rw*qw - rx*qx - ry*qy - rz*qz,
      x: rw*qx + rx*qw + ry*qz - rz*qy,
      y: rw*qy - rx*qz + ry*qw + rz*qx,
      z: rw*qz + rx*qy - ry*qx + rz*qw,
    };
  }

  // Live calculation handles whichever telemetry block stream is currently active
  const liveRelQ = $derived.by(() => {
    if (!baselineSet) return null;
    const latestReading = filteredReadings.at(-1);
    if (!latestReading?.qw) return null;
    return applyBaseline(latestReading.qw, latestReading.qx, latestReading.qy, latestReading.qz);
  });

  const liveAngle = $derived.by(() => {
    if (!liveRelQ) return null;
    return 2 * Math.acos(Math.min(1, Math.abs(liveRelQ.w))) * 180 / Math.PI;
  });

  function captureSlot(slot) {
    if (!liveRelQ) return;
    calibSlots = { ...calibSlots, [slot]: { ...liveRelQ } };
    selectedIds = new Set();
  }

  function normalize3(x, y, z) {
    const len = Math.sqrt(x*x + y*y + z*z);
    return len > 0 ? { x: x/len, y: y/len, z: z/len } : { x: 0, y: 1, z: 0 };
  }

  async function computeAndSaveAxes() {
    const { left, right, front, back } = calibSlots;
    const rollAxis  = normalize3(left.x - right.x, left.y - right.y, left.z - right.z);
    const pitchAxis = normalize3(front.x - back.x, front.y - back.y, front.z - back.z);
    calibStatus = null;
    try {
      config = await collarConfig.update(params.id, {
        ...config,
        axesCalibrated: true,
        rollAxisX:  rollAxis.x,  rollAxisY:  rollAxisY,  rollAxisZ:  rollAxis.z,
        pitchAxisX: pitchAxis.x, pitchAxisY: pitchAxis.y, pitchAxisZ: pitchAxis.z,
      });
      calibStatus = { ok: true, message: 'Axes saved.' };
      calibSlots = { left: null, right: null, front: null, back: null };
    } catch {
      calibStatus = { ok: false, message: 'Failed to save axes.' };
    }
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
    const valid = selectedReadings.filter(r => r.qw != null);
    if (valid.length === 0) return;
    const quats = valid.map(r => ({ w: r.qw, x: r.qx, y: r.qy, z: r.qz }));
    const avg = averageQuats(quats);
    baselineStatus = null;
    try {
      config = await collarConfig.update(params.id, {
        ...config, refQw: avg.w, refQx: avg.x, refQy: avg.y, refQz: avg.z
      });
      baselineStatus = { ok: true, message: `Orientation baseline set from ${valid.length} (${viewMode}) readings` };
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
      config = await collarConfig.update(params.id, { ...config, rollBaseline: rounded });
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
    [horse, status, config] = await Promise.all([
      horses.getById(params.id),
      deviceStatus.getLatest(params.id).catch(() => null),
      collarConfig.get(params.id).catch(() => null),
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
    readings = await sensorReadingsV2.getByHorse(params.id, selectedDate, 400); // Expanded boundary limit since double rows fetch per step
  }

  async function remove(id) {
    await sensorReadingsV2.delete(params.id, id);
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

  {#if filteredReadings.length > 0}
    {@const latest = filteredReadings.at(-1)}
    {#if latest.alertReason}
      <div class="alert-banner alert">
        ⚠️ Alert — {latest.alertReason}. Viewing {viewMode} stream context.
      </div>
    {/if}
    <div class="orientation-card">
      <div class="sensor-strip">
        <div class="sensor-stat">
          <span class="stat-label">Active Channel</span>
          <span class="stat-value highlight-text">{viewMode}</span>
        </div>
        <div class="sensor-stat">
          <span class="stat-label">Accel</span>
          <span class="stat-value">{latest.acceleration?.toFixed(2) ?? '—'} m/s²</span>
        </div>
        <div class="sensor-stat">
          <span class="stat-label">Gyro</span>
          <span class="stat-value">{latest.angularVelocity?.toFixed(2) ?? '—'} rad/s</span>
        </div>
        <div class="sensor-stat quat-block">
          <span class="stat-label">Quaternion (W,X,Y,Z)</span>
          <span class="stat-value text-muted">
            {latest.qw?.toFixed(2)}, {latest.qx?.toFixed(2)}, {latest.qy?.toFixed(2)}, {latest.qz?.toFixed(2)}
          </span>
        </div>
      </div>
    </div>
  {/if}

  <div class="toolbar">
    <h2>Sensor Readings</h2>

    <div class="toggle-group">
      <button class:active={viewMode === 'normalized'} onclick={() => viewMode = 'normalized'}>Normalized</button>
      <button class:active={viewMode === 'raw'} onclick={() => viewMode = 'raw'}>Raw Data</button>
    </div>

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
      <span>{selectedIds.size} items selected ({viewMode}) &nbsp;·&nbsp; avg roll: <strong>{avgRoll?.toFixed(1)}°</strong></span>
      <button class="baseline-btn" onclick={setBaseline}>Set roll baseline</button>
      <button class="baseline-btn" onclick={setOrientationBaseline}>Set orientation baseline</button>
      <button class="clear-btn" onclick={() => selectedIds = new Set()}>Clear</button>
    </div>
  {/if}
  {#if baselineStatus}
    <p class="baseline-status" class:ok={baselineStatus.ok} class:error={!baselineStatus.ok}>{baselineStatus.message}</p>
  {/if}

  <div class="calib-toggle">
    <button class="calib-open-btn" onclick={() => calibOpen = !calibOpen}>
      {calibOpen ? '▲' : '▼'} Calibrate orientation axes
    </button>
  </div>

  {#if calibOpen}
    <div class="calib-panel">
      <div class="calib-step">
        <span class="calib-step-label">Step 1 — Flat baseline</span>
        <span class="calib-check" class:done={baselineSet}>
          {baselineSet ? '✓ Set' : '⚠ Not set — select flat readings above and click "Set orientation baseline"'}
        </span>
      </div>

      {#if baselineSet}
        <div class="calib-live">
          <span class="calib-step-label">Live angle from baseline ({viewMode} orientation)</span>
          <div class="angle-row">
            <span class="angle-value">{liveAngle?.toFixed(1) ?? '—'}°</span>
            <div class="angle-track">
              <div class="angle-fill" style="width:{Math.min(100, ((liveAngle ?? 0) / 90) * 100)}%;
                background:{!liveAngle ? '#e2e8f0' : liveAngle < 45 ? '#94a3b8' : liveAngle < 70 ? '#f59e0b' : '#22c55e'}">
              </div>
              <span class="angle-mark">90°</span>
            </div>
            <span class="angle-hint">
              {#if !liveRelQ}no quaternion data{:else if liveAngle < 45}tilt more{:else if liveAngle < 70}keep going…{:else}✓ good — click a slot{/if}
            </span>
          </div>
        </div>

        <div class="calib-step">
          <span class="calib-step-label">Step 2 — Roll axis</span>
          <p class="calib-desc">Tilt 90° to the left, then click <strong>Left</strong>. Then tilt 90° to the right and click <strong>Right</strong>.</p>
          <div class="calib-row">
            <button class="calib-btn" onclick={() => captureSlot('left')}  disabled={!liveRelQ || liveAngle < 45}>Set Left</button>
            <span class="calib-check" class:done={calibSlots.left}>{calibSlots.left  ? `✓ ${(2*Math.acos(Math.min(1,Math.abs(calibSlots.left.w)))*180/Math.PI).toFixed(0)}°` : '—'}</span>
            <button class="calib-btn" onclick={() => captureSlot('right')} disabled={!liveRelQ || liveAngle < 45}>Set Right</button>
            <span class="calib-check" class:done={calibSlots.right}>{calibSlots.right ? `✓ ${(2*Math.acos(Math.min(1,Math.abs(calibSlots.right.w)))*180/Math.PI).toFixed(0)}°` : '—'}</span>
          </div>
        </div>

        <div class="calib-step">
          <span class="calib-step-label">Step 3 — Pitch axis</span>
          <p class="calib-desc">Tilt 90° forward (nose down), then click <strong>Front</strong>. Then tilt 90° backward and click <strong>Back</strong>.</p>
          <div class="calib-row">
            <button class="calib-btn" onclick={() => captureSlot('front')} disabled={!liveRelQ || liveAngle < 45}>Set Front</button>
            <span class="calib-check" class:done={calibSlots.front}>{calibSlots.front ? `✓ ${(2*Math.acos(Math.min(1,Math.abs(calibSlots.front.w)))*180/Math.PI).toFixed(0)}°` : '—'}</span>
            <button class="calib-btn" onclick={() => captureSlot('back')}  disabled={!liveRelQ || liveAngle < 45}>Set Back</button>
            <span class="calib-check" class:done={calibSlots.back}>{calibSlots.back  ? `✓ ${(2*Math.acos(Math.min(1,Math.abs(calibSlots.back.w)))*180/Math.PI).toFixed(0)}°` : '—'}</span>
          </div>
        </div>

        <div class="calib-row">
          <button class="baseline-btn" onclick={computeAndSaveAxes} disabled={!calibReady}>Compute & Save Axes</button>
          {#if calibStatus}
            <span class="calib-check" class:done={calibStatus.ok}>{calibStatus.message}</span>
          {/if}
        </div>
      {/if}
    </div>
  {/if}

  {#if filteredReadings.length === 0}
    <p>No active {viewMode} channel data for this day.</p>
  {:else}
    <table>
      <thead>
        <tr>
          <th></th>
          <th>Time</th>
          <th>Δt</th>
          <th>Channel</th>
          <th>Roll Status</th>
          <th>Accel</th>
          <th>Gyro</th>
          <th class="quat-head">Qw</th>
          <th class="quat-head">Qx</th>
          <th class="quat-head">Qy</th>
          <th class="quat-head">Qz</th>
          <th class="quat-head">rQw</th>
          <th class="quat-head">rQx</th>
          <th class="quat-head">rQy</th>
          <th class="quat-head">rQz</th>
          <th></th>
        </tr>
      </thead>
      <tbody>
        {#each [...filteredReadings].reverse() as reading, i (reading.id || i)}
          {@const prev = filteredReadings[filteredReadings.length - 2 - i]}
          {@const dtSec = prev ? Math.round((new Date(reading.timestamp) - new Date(prev.timestamp)) / 1000) : null}
          <SensorReadingRow
            {reading}
            {config}
            {dtSec}
            selected={selectedIds.has(reading.id)}
            ontoggle={() => toggleSelect(reading.id)}
            ondelete={() => remove(reading.id)}
            onview={() => viewReading = reading}
          />
        {/each}
      </tbody>
    </table>
  {/if}

  {#if viewReading}
    <div class="modal-backdrop" role="dialog" onclick={() => viewReading = null}
        onkeydown={(e) => e.key === 'Escape' && (viewReading = null)}>
      <div class="modal-box" onclick={(e) => e.stopPropagation()}>
        <div class="modal-header">
          <span>{formatDate(viewReading.timestamp)} ({viewReading.readingType})</span>
          <button class="modal-close" onclick={() => viewReading = null}>✕</button>
        </div>
        <HorseHeadViewer readings={viewReadings} {config} />
        <div class="modal-slider">
          <label for="viewCount">Loop over last <strong>{viewCount}</strong> items</label>
          <input id="viewCount" type="range" min="1" max={viewMax} bind:value={viewCount} />
        </div>
      </div>
    </div>
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

  .toolbar { display: flex; align-items: center; justify-content: space-between; margin-bottom: 1rem; gap: 1rem; }
  .toolbar h2 { margin: 0; }

  /* Toggle Styles */
  .toggle-group { display: flex; border: 1px solid #cbd5e1; border-radius: 6px; overflow: hidden; }
  .toggle-group button { margin: 0; padding: 0.4rem 0.9rem; background: white; color: #475569; border: none; border-radius: 0; font-size: 0.85rem; font-weight: 500; }
  .toggle-group button.active { background: #0ea5e9; color: white; }

  .date-picker { display: flex; align-items: center; gap: 0.5rem; }
  .date-picker span { font-size: 0.95rem; color: #475569; }
  input[type="date"] { padding: 0.4rem 0.6rem; font-size: 1rem; }
  .alert-banner { padding: 0.9rem 1.2rem; border-radius: 0.5rem; font-weight: 600; margin-bottom: 1rem; }
  .alert-banner.alert { background: #fef3c7; color: #92400e; border: 1px solid #fcd34d; }

  .sensor-strip {
    display: flex; flex-wrap: wrap; gap: 0.5rem;
    padding: 0.75rem 1rem; background: #f8fafc;
    border: 1px solid #e2e8f0; border-radius: 0.5rem; margin-bottom: 1rem;
  }
  .sensor-stat {
    display: flex; flex-direction: column; align-items: center;
    min-width: 70px; padding: 0.3rem 0.6rem;
    background: white; border: 1px solid #e2e8f0; border-radius: 6px;
  }
  .quat-block { min-width: 180px; }
  .highlight-text { color: #6d28d9 !important; text-transform: capitalize; }
  .text-muted { color: #64748b; font-family: monospace; font-size: 0.85rem; }

  .stat-label { font-size: 0.72rem; color: #94a3b8; font-weight: 600; text-transform: uppercase; letter-spacing: 0.04em; }
  .stat-value { font-size: 0.9rem; font-weight: 600; color: #1e293b; margin-top: 0.1rem; }
  table { width: 100%; border-collapse: collapse; }
  :global(.quat-head) { color: #94a3b8; font-size: 0.78rem; }
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
  .calib-toggle { margin-bottom: 0.5rem; }
  .calib-open-btn { background: none; border: 1px solid #e2e8f0; border-radius: 4px; padding: 0.3rem 0.75rem; font-size: 0.85rem; cursor: pointer; color: #475569; margin: 0; }
  .calib-open-btn:hover { background: #f8fafc; }
  .calib-panel {
    background: #f8fafc; border: 1px solid #e2e8f0; border-radius: 0.5rem;
    padding: 0.9rem 1rem; margin-bottom: 0.75rem;
    display: flex; flex-direction: column; gap: 0.75rem; font-size: 0.88rem;
  }
  .calib-step { display: flex; flex-direction: column; gap: 0.4rem; }
  .calib-step-label { font-weight: 600; color: #1e293b; }
  .calib-row { display: flex; align-items: center; gap: 0.6rem; flex-wrap: wrap; }
  .calib-btn { background: #475569; color: white; border: none; border-radius: 4px; padding: 0.3rem 0.8rem; font-size: 0.82rem; cursor: pointer; margin: 0; }
  .calib-btn:disabled { opacity: 0.4; cursor: not-allowed; }
  .calib-check { font-size: 0.82rem; color: #94a3b8; }
  .calib-check.done { color: #16a34a; font-weight: 600; }
  .calib-live { display: flex; flex-direction: column; gap: 0.4rem; padding: 0.6rem 0.8rem; background: white; border: 1px solid #e2e8f0; border-radius: 6px; }
  .angle-row { display: flex; align-items: center; gap: 0.75rem; }
  .angle-value { font-size: 1.3rem; font-weight: 700; color: #1e293b; min-width: 3.5rem; font-variant-numeric: tabular-nums; }
  .angle-track { flex: 1; height: 10px; background: #e2e8f0; border-radius: 5px; position: relative; }
  .angle-fill { height: 100%; border-radius: 5px; transition: width 0.4s, background 0.4s; }
  .angle-mark { position: absolute; right: 0; top: -1.2rem; font-size: 0.7rem; color: #94a3b8; }
  .angle-hint { font-size: 0.8rem; color: #64748b; min-width: 8rem; }
  .calib-desc { font-size: 0.82rem; color: #64748b; margin: 0 0 0.35rem; }
  .modal-backdrop { position: fixed; inset: 0; background: #00000088; display: flex; align-items: center; justify-content: center; z-index: 100; }
  .modal-box { background: #0f172a; border-radius: 12px; box-shadow: 0 8px 40px #0008; overflow: hidden; }
  .modal-header { display: flex; align-items: center; justify-content: space-between; padding: 0.6rem 0.9rem; background: #1e293b; color: #e2e8f0; font-size: 0.85rem; font-family: monospace; }
  .modal-close { background: none; border: none; color: #94a3b8; font-size: 1rem; cursor: pointer; padding: 0 0.2rem; margin: 0; }
  .modal-slider { padding: 0.6rem 1rem 0.8rem; background: #1e293b; display: flex; flex-direction: column; gap: 0.4rem; }
  .modal-slider label { font-size: 0.8rem; color: #94a3b8; font-family: monospace; }
  .modal-slider input[type=range] { width: 100%; accent-color: #0ea5e9; }
</style>

```
