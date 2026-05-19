<script>
  import { onMount } from 'svelte';
  import { push } from 'svelte-spa-router';
  import { horses } from '../services/horses.js';
  import HorseCard from '../components/HorseCard.svelte';

  let horseList = $state([]);
  let newHorseName = $state('');
  let error = $state(null);

  onMount(async () => {
    horseList = await horses.getAll();
  });

  async function create() {
    if (!newHorseName.trim()) return;
    const horse = await horses.create({ name: newHorseName.trim() });
    horseList = [...horseList, horse];
    newHorseName = '';
  }

  async function remove(id) {
    await horses.delete(id);
    horseList = horseList.filter(h => h.id !== id);
  }
</script>

<main>
  <h1>Horses</h1>

  <form onsubmit={(e) => { e.preventDefault(); create(); }}>
    <input bind:value={newHorseName} placeholder="Horse name" />
    <button type="submit">Add</button>
  </form>

  {#if error}
    <p class="error">{error}</p>
  {/if}

  <div class="list">
    {#each horseList as horse (horse.id)}
      <HorseCard
        {horse}
        onclick={() => push(`/horses/${horse.id}`)}
        ondelete={() => remove(horse.id)}
      />
    {/each}
  </div>
</main>

<style>
  main { max-width: 600px; margin: 2rem auto; padding: 0 1rem; }
  form { display: flex; gap: 0.5rem; margin-bottom: 1.5rem; }
  input { flex: 1; padding: 0.5rem; font-size: 1rem; }
  button { padding: 0.5rem 1rem; cursor: pointer; }
  .list { display: flex; flex-direction: column; gap: 0.75rem; }
  .error { color: red; }
</style>
