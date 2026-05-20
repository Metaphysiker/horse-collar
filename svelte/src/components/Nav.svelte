<script>
  // @ts-nocheck
  import { link } from 'svelte-spa-router';
  import { onMount, onDestroy } from 'svelte';

  let hash = $state(window.location.hash);

  function onHashChange() { hash = window.location.hash; }

  onMount(() => window.addEventListener('hashchange', onHashChange));
  onDestroy(() => window.removeEventListener('hashchange', onHashChange));

  let horseId = $derived(() => {
    const match = hash.match(/^#\/horses\/([^/]+)/);
    return match ? match[1] : null;
  });
</script>

<nav>
  <span class="brand">🐴 Horse Collar</span>
  <a href="/" use:link>Horses</a>
  {#if horseId()}
    <a href="/horses/{horseId()}/config" use:link>Config</a>
    <a href="/horses/{horseId()}/actions" use:link>Calibrate</a>
  {/if}
</nav>

<style>
  nav {
    display: flex;
    align-items: center;
    gap: 1.5rem;
    padding: 0.75rem 1.5rem;
    background: #1e293b;
    color: white;
  }
  .brand { font-weight: 700; font-size: 1.1rem; margin-right: auto; }
  a { color: #cbd5e1; text-decoration: none; font-size: 0.95rem; }
  a:hover { color: white; }
</style>
