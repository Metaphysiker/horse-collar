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
  const stateColors = {
    Calm:    '#22c55e',
    Moving:  '#3b82f6',
    Tilted:  '#f59e0b',
    Rolling: '#ef4444',
  };

  /** @type {Record<string, string>} */
  const alarmColors = {
    Alert:     '#f97316',
    Emergency: '#dc2626',
  };

  const color = $derived(stateColors[reading.state] ?? '#888');
  const alarmColor = $derived(alarmColors[reading.alarmState] ?? null);

</script>

<tr in:fly={{ y: -16, duration: 250 }} class:selected class:viewable={!!onview}
    onclick={(e) => { if (!(e.target instanceof Element) || !e.target.closest('input,button')) onview?.(); }}>
  <td><input type="checkbox" checked={selected} onchange={ontoggle} /></td>
  <td>{formatDate(reading.timestamp)}</td>
  <td class="dt" style="color:{dtColor}">{fmtDt(dtSec)}</td>
  <td>
    <span class="badge" style="background:{color}">{reading.state}</span>
    {#if alarmColor}
      <span class="badge" style="background:{alarmColor}">{reading.alarmState}</span>
    {/if}
    {#if reading.alertReason}
      <span class="reason">{reading.alertReason}</span>
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
  <td>{reading.acceleration?.toFixed(2)}</td>
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
    color: #64748b;
    margin-top: 0.1rem;
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
