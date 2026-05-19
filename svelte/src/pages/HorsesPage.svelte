<script>
  // @ts-nocheck
  import { onMount } from 'svelte';
  import { push } from 'svelte-spa-router';
  import { horses } from '../services/horses.js';
  import HorseCard from '../components/HorseCard.svelte';

  let horseList = $state([]);
  let archivedList = $state([]);
  let newHorseName = $state('');
  let showArchived = $state(false);

  onMount(async () => {
    horseList = await horses.getAll();
  });

  async function create() {
    if (!newHorseName.trim()) return;
    const horse = await horses.create({ name: newHorseName.trim() });
    horseList = [...horseList, horse];
    newHorseName = '';
  }

  async function archive(id) {
    await horses.archive(id);
    horseList = horseList.filter(h => h.id !== id);
  }

  async function toggleArchived() {
    showArchived = !showArchived;
    if (showArchived && archivedList.length === 0)
      archivedList = await horses.getArchived();
  }

  async function unarchive(id) {
    await horses.unarchive(id);
    const horse = archivedList.find(h => h.id === id);
    archivedList = archivedList.filter(h => h.id !== id);
    horseList = [...horseList, { ...horse, isArchived: false }];
  }
</script>

<main>
  <h1>Horses</h1>

  <form onsubmit={(e) => { e.preventDefault(); create(); }}>
    <input bind:value={newHorseName} placeholder="Horse name" />
    <button type="submit">Add</button>
  </form>

  <div class="list">
    {#each horseList as horse (horse.id)}
      <HorseCard
        {horse}
        onclick={() => push(`/horses/${horse.id}`)}
        onarchive={() => archive(horse.id)}
      />
    {/each}
  </div>

  <button class="toggle" onclick={toggleArchived}>
    {showArchived ? 'Hide' : 'Show'} archived horses
  </button>

  {#if showArchived}
    <h2>Archived</h2>
    {#if archivedList.length === 0}
      <p>No archived horses.</p>
    {:else}
      <div class="list">
        {#each archivedList as horse (horse.id)}
          <HorseCard
            {horse}
            onclick={() => push(`/horses/${horse.id}`)}
            onunarchive={() => unarchive(horse.id)}
          />
        {/each}
      </div>
    {/if}
  {/if}
</main>

<style>
  main { max-width: 600px; margin: 2rem auto; padding: 0 1rem; }
  form { display: flex; gap: 0.5rem; margin-bottom: 1.5rem; }
  input { flex: 1; padding: 0.5rem; font-size: 1rem; }
  button { padding: 0.5rem 1rem; cursor: pointer; }
  .list { display: flex; flex-direction: column; gap: 0.75rem; }
  .toggle { margin-top: 1.5rem; }
  h2 { margin-top: 1.5rem; }
</style>
