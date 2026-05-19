<script>
  import { onMount } from 'svelte';
  import { pop } from 'svelte-spa-router';
  import { horses } from '../services/horses.js';
  import { sensorReadings } from '../services/sensorReadings.js';
  import SensorReadingRow from '../components/SensorReadingRow.svelte';

  let { params = {} } = $props();

  let horse = $state(null);
  let readings = $state([]);

  onMount(async () => {
    [horse, readings] = await Promise.all([
      horses.getById(params.id),
      sensorReadings.getByHorse(params.id),
    ]);
  });
</script>

<main>
  <button onclick={pop}>← Back</button>

  {#if horse}
    <h1>{horse.name}</h1>
  {/if}

  <h2>Sensor Readings</h2>

  {#if readings.length === 0}
    <p>No readings yet.</p>
  {:else}
    <table>
      <thead>
        <tr>
          <th>Time</th>
          <th>State</th>
          <th>Pitch</th>
          <th>Roll</th>
          <th>Acceleration</th>
          <th>Activity</th>
        </tr>
      </thead>
      <tbody>
        {#each readings as reading (reading.id)}
          <SensorReadingRow {reading} />
        {/each}
      </tbody>
    </table>
  {/if}
</main>

<style>
  main { max-width: 900px; margin: 2rem auto; padding: 0 1rem; }
  button { margin-bottom: 1rem; cursor: pointer; }
  table { width: 100%; border-collapse: collapse; }
  th, td { text-align: left; padding: 0.5rem; border-bottom: 1px solid #ddd; }
  th { font-weight: 600; }
</style>
