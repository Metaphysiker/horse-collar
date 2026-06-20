Here is the updated `SensorReadingRowV2.svelte` component.

It is rewritten to handle the flat data items (Option B), features a colored **Channel Badge** indicating whether the row is currently displaying `Raw` or `Normalized` values, and maps all quaternion data to the root properties cleanly.

```html
<script>
  import { fly } from 'svelte/transition';
  import { formatDate } from '../utils/formatDate.js';

  let { reading, config = null, selected = false, dtSec = null, ontoggle, ondelete, onview } = $props();

  function fmtDt(s /** @type {number|null} */) {
    if (s == null) return '—';
    if (s < 60)   return `${s}s`;
    const m = Math.floor(s / 60), r = s % 60;
    return r ? `${m}m ${r}s` : `${m}m`;
  }
  const dtColor = $derived(dtSec == null ? '#94a3b8' : dtSec > 120 ? '#ef4444' : dtSec > 30 ? '#f59e0b' : '#94a3b8');

  /**
   * @param {number} qw
   * @param {number} qx
   * @param {number} qy
   * @param {number} qz
   */
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

  // Calculate dynamic calibration offset using direct root parameters
  const relQ = $derived(
    reading.qw != null
      ? applyBaseline(reading.qw, reading.qx, reading.qy, reading.qz)
      : null
  );

  const horseRoll = $derived.by(() => {
    if (!relQ) return null;
    const ax = config?.rollAxisX ?? 0;
    const ay = config?.rollAxisY ?? 1;
    const az = config?.rollAxisZ ?? 0;
    const rollDeg = 2 * Math.atan2(relQ.x*ax + relQ.y*ay + relQ.z*az, relQ.w) * 180 / Math.PI;
    const abs = Math.abs(rollDeg);
    const side = rollDeg > 0 ? 'Left' : 'Right';
    const deg = rollDeg.toFixed(1);
    if (abs < 22.5) return { label: 'Normal',                      color: '#22c55e', deg };
    if (abs < 45)   return { label: `Slight-${side.toLowerCase()}`, color: '#84cc16', deg };
    if (abs < 67.5) return { label: `Half-${side.toLowerCase()}`,   color: '#f59e0b', deg };
    return                 { label: side,                           color: '#ef4444', deg };
  });

  /** @type {Record<string, string>} */
  const postureColors = {
    standing: '#22c55e',
    lying:    '#f97316'
  };

  const postureColor = $derived(postureColors[reading.posture?.toLowerCase()] ?? '#64748b');

  // Custom design style indicators for channel badges
  const isRaw = $derived(reading.readingType?.toLowerCase() === 'raw');
</script>

<tr in:fly={{ y: -16, duration: 250 }} class:selected class:viewable={!!onview}
    onclick={(e) => { if (!(e.target instanceof Element) || !e.target.closest('input,button')) onview?.(); }}>
  <td><input type="checkbox" checked={selected} onchange={ontoggle} /></td>
  <td>{formatDate(reading.timestamp)}</td>
  <td class="dt" style="color:{dtColor}">{fmtDt(dtSec)}</td>

  <td>
    <span class="channel-badge" class:raw-badge={isRaw} class:norm-badge={!isRaw}>
      {reading.readingType ?? 'Unknown'}
    </span>
  </td>

  <td>
    <span class="badge" style="background:{postureColor}; text-transform: capitalize;">
      {reading.posture ?? 'Unknown'}
    </span>
    {#if reading.alertReason}
      <span class="reason">⚠️ {reading.alertReason}</span>
    {/if}
  </td>
  <td>
    {#if horseRoll}
      <span class="roll-badge" style="background:{horseRoll.color}">{horseRoll.label}</span>
      <span class="roll-deg">{horseRoll.deg}°</span>
    {:else}
      <span class="upright">—</span>
    {/if}
  </td>
  <td>{reading.acceleration?.toFixed(2) ?? '—'}</td>
  <td>{reading.angularVelocity?.toFixed(2) ?? '—'}</td>
  <td>{reading.temperature != null ? reading.temperature.toFixed(1) + '°' : '—'}</td>

  <td class="quat">{reading.qw?.toFixed(3) ?? '—'}</td>
  <td class="quat">{reading.qx?.toFixed(3) ?? '—'}</td>
  <td class="quat">{reading.qy?.toFixed(3) ?? '—'}</td>
  <td class="quat">{reading.qz?.toFixed(3) ?? '—'}</td>

  <td class="quat rel">{relQ?.w.toFixed(3) ?? '—'}</td>
  <td class="quat rel">{relQ?.x.toFixed(3) ?? '—'}</td>
  <td class="quat rel">{relQ?.y.toFixed(3) ?? '—'}</td>
  <td class="quat rel">{relQ?.z.toFixed(3) ?? '—'}</td>
  <td>
    <button onclick={() => { if (confirm(`Delete reading from ${formatDate(reading.timestamp)}?`)) ondelete(); }}>
      ✕
    </button>
  </td>
</tr>

<style>
  .badge {
    display: inline-block;
    padding: 0.15rem 0.45rem;
    border-radius: 4px;
    color: white;
    font-size: 0.8rem;
    font-weight: 600;
  }

  /* Channel Indicator Styling */
  .channel-badge {
    display: inline-block;
    padding: 0.15rem 0.4rem;
    border-radius: 4px;
    font-size: 0.72rem;
    font-weight: 700;
    text-transform: uppercase;
    letter-spacing: 0.03em;
  }
  .raw-badge {
    background: #f3e8ff;
    color: #6b21a8;
    border: 1px solid #d8b4fe;
  }
  .norm-badge {
    background: #ecfdf5;
    color: #065f46;
    border: 1px solid #a7f3d0;
  }

  .viewable { cursor: pointer; }
  .viewable:hover { background: #f8fafc; }
  .dt { font-size: 0.78rem; font-variant-numeric: tabular-nums; white-space: nowrap; }
  .upright { color: #cbd5e1; }
  .roll-deg { font-size: 0.75rem; color: #64748b; margin-left: 0.3rem; }
  .roll-badge {
    display: inline-block;
    padding: 0.15rem 0.45rem;
    border-radius: 4px;
    color: white;
    font-size: 0.8rem;
    font-weight: 600;
    white-space: nowrap;
  }
  .reason {
    display: block;
    font-size: 0.72rem;
    color: #b45309;
    margin-top: 0.1rem;
    font-weight: 500;
  }
  button {
    padding: 0.2rem 0.4rem;
    font-size: 0.8rem;
    cursor: pointer;
    background: none;
    border: 1px solid #ddd;
    border-radius: 4px;
    color: #94a3b8;
  }
  button:hover { color: #ef4444; border-color: #ef4444; }
  .selected { background: #f0f9ff; }
  input[type="checkbox"] { cursor: pointer; }
  :global(.quat) { font-size: 0.78rem; color: #94a3b8; font-family: monospace; }
  :global(.quat.rel) { color: #7c3aed; }
</style>

```
