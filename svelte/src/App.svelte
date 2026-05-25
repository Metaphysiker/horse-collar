<script>
  import Router from 'svelte-spa-router';
  import Nav from './components/Nav.svelte';
  import HorsesPage from './pages/HorsesPage.svelte';
  import HorseDetailPage from './pages/HorseDetailPage.svelte';
  import HorseActionsPage from './pages/HorseActionsPage.svelte';
  import HorseConfigPage from './pages/HorseConfigPage.svelte';
  import HorseAnalysisPage from './pages/HorseAnalysisPage.svelte';

  const routes = {
    '/':                    HorsesPage,
    '/horses/:id':          HorseDetailPage,
    '/horses/:id/actions':  HorseActionsPage,
    '/horses/:id/config':   HorseConfigPage,
    '/horses/:id/analysis': HorseAnalysisPage,
  };
</script>

<Nav />
<Router {routes} />
