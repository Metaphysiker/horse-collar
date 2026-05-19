<script>
  let { horse, onclick, onarchive, onunarchive } = $props();
</script>

<div class="card" role="button" tabindex="0" onclick={onclick} onkeydown={(e) => e.key === 'Enter' && onclick()}>
  <span class="name">{horse.name}</span>
  {#if onunarchive}
    <button onclick={(e) => { e.stopPropagation(); onunarchive(); }}>Unarchive</button>
  {:else}
    <button onclick={(e) => { e.stopPropagation(); if (confirm(`Archive ${horse.name}?`)) onarchive(); }}>Archive</button>
  {/if}
</div>

<style>
  .card {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 0.75rem 1rem;
    border: 1px solid #ddd;
    border-radius: 6px;
    cursor: pointer;
  }
  .card:hover { background: #f9f9f9; }
  .name { font-size: 1.1rem; }
  button { cursor: pointer; }
</style>
