<script>
  import { fly } from 'svelte/transition';
  import { formatDate } from '../utils/formatDate.js';

  let { reading, ondelete } = $props();

  const stateColors = {
    Standing:  '#22c55e',
    Moving:    '#3b82f6',
    LyingDown: '#f59e0b',
    Rolling:   '#ef4444',
    Alert:     '#f97316',
    Emergency: '#dc2626',
  };

  const color = $derived(stateColors[reading.state] ?? '#888');

  function horseSide(roll) {
    if (roll == null) return null;
    if (roll > 40)  return { label: 'Left',  color: '#a855f7' };
    if (roll < -30) return { label: 'Right', color: '#ec4899' };
    return null;
  }

  // Linear calibration: sensor +5° = horse 0°, +72° = horse +90°, -65° = horse -90°
  function horseRoll(roll) {
    if (roll == null) return null;
    const deg = roll >= 5
      ? (roll - 5) / 67 * 90
      : (roll - 5) / 70 * 90;
    return Math.max(-90, Math.min(90, deg));
  }
</script>

<tr in:fly={{ y: -16, duration: 250 }}>
  <td>{formatDate(reading.timestamp)}</td>
  <td>
    <span class="badge" style="background:{color}">{reading.state}</span>
    {#if reading.alertReason}
      <span class="reason">{reading.alertReason}</span>
    {/if}
  </td>
  <td>{reading.pitch?.toFixed(1)}°</td>
  <td>{reading.roll?.toFixed(1)}°</td>
  <td>
    {#if horseSide(reading.roll)}
      {@const side = horseSide(reading.roll)}
      <span class="badge" style="background:{side.color}">{side.label}</span>
    {:else}
      <span class="upright">—</span>
    {/if}
  </td>
  <td class="horse-roll">{horseRoll(reading.roll)?.toFixed(0) ?? '—'}°</td>
  <td>{(reading.pitch != null && reading.roll != null) ? Math.sqrt(reading.pitch**2 + reading.roll**2).toFixed(1) : '—'}°</td>
  <td>{reading.acceleration?.toFixed(2)}</td>
  <td>{reading.angularVelocity?.toFixed(2) ?? '—'}</td>
  <td>{reading.temperature != null ? reading.temperature.toFixed(1) + '°' : '—'}</td>
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
  .upright { color: #cbd5e1; }
  .horse-roll { color: #7c3aed; font-weight: 500; }
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
</style>
