<script>
  import { formatDate } from '../utils/formatDate.js';

  let { reading } = $props();

  const stateColors = {
    Standing: '#22c55e',
    Moving:   '#3b82f6',
    LyingDown:'#f59e0b',
    Rolling:  '#ef4444',
  };

  const color = stateColors[reading.state] ?? '#888';
</script>

<tr>
  <td>{formatDate(reading.timestamp)}</td>
  <td><span class="badge" style="background:{color}">{reading.state}</span></td>
  <td>{reading.pitch?.toFixed(1)}°</td>
  <td>{reading.roll?.toFixed(1)}°</td>
  <td>{reading.acceleration?.toFixed(2)} m/s²</td>
  <td>{reading.activity}</td>
</tr>

<style>
  .badge {
    display: inline-block;
    padding: 0.2rem 0.5rem;
    border-radius: 4px;
    color: white;
    font-size: 0.85rem;
    font-weight: 600;
  }
</style>
