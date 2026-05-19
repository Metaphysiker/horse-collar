<script>
  import Router from 'svelte-spa-router';
  import HorsesPage from './pages/HorsesPage.svelte';
  import HorseDetailPage from './pages/HorseDetailPage.svelte';

  const routes = {
    '/':           HorsesPage,
    '/horses/:id': HorseDetailPage,
  };
</script>

<Router {routes} />
