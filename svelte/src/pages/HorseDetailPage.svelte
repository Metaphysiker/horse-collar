<script>
  // @ts-nocheck
  import { onMount } from 'svelte';
  import { pop, push } from 'svelte-spa-router';
  import { horses } from '../services/horses.js';
  import { sensorReadings } from '../services/sensorReadings.js';
  import SensorReadingRow from '../components/SensorReadingRow.svelte';

  let { params = {} } = $props();

  let horse = $state(null);
  let readings = $state([]);
  let selectedDate = $state(todayString());

  function todayString() {
    return new Date().toISOString().slice(0, 10);
  }

  onMount(async () => {
    horse = await horses.getById(params.id);
    await loadReadings();
  });

  async function loadReadings() {
    readings = await sensorReadings.getByHorse(params.id, selectedDate);
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
      <button onclick={() => push(`/horses/${params.id}/actions`)}>Device Actions</button>
    </div>
  {/if}

  <div class="toolbar">
    <h2>Sensor Readings</h2>
    <input
      type="date"
      bind:value={selectedDate}
      onchange={loadReadings}
      max={todayString()}
    />
  </div>

  {#if readings.length === 0}
    <p>No readings for this day.</p>
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
          <th></th>
        </tr>
      </thead>
      <tbody>
        {#each readings as reading (reading.id)}
          <SensorReadingRow {reading} ondelete={() => remove(reading.id)} />
        {/each}
      </tbody>
    </table>
  {/if}
</main>

<style>
  main { max-width: 900px; margin: 2rem auto; padding: 0 1rem; }
  button { margin-bottom: 1rem; cursor: pointer; }
  .header { display: flex; align-items: center; justify-content: space-between; }
  .toolbar { display: flex; align-items: center; justify-content: space-between; margin-bottom: 1rem; }
  .toolbar h2 { margin: 0; }
  input[type="date"] { padding: 0.4rem 0.6rem; font-size: 1rem; }
  table { width: 100%; border-collapse: collapse; }
  th, :global(td) { text-align: left; padding: 0.5rem; border-bottom: 1px solid #ddd; }
  th { font-weight: 600; }
</style>
