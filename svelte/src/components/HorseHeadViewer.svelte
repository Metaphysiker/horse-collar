<script>
  import { onMount, onDestroy } from 'svelte';
  import * as THREE from 'three';
  import { formatDate } from '../utils/formatDate.js';

  let { readings = [], config = null } = $props();

  let canvas;
  let renderer, animFrame;
  let currentIdx = $state(0);

  function applyBaseline(qw, qx, qy, qz) {
    const rw =  (config?.refQw ?? 1);
    const rx = -(config?.refQx ?? 0);
    const ry = -(config?.refQy ?? 0);
    const rz = -(config?.refQz ?? 0);
    return new THREE.Quaternion(
      rw*qx + rx*qw + ry*qz - rz*qy,
      rw*qy - rx*qz + ry*qw + rz*qx,
      rw*qz + rx*qy - ry*qx + rz*qw,
      rw*qw - rx*qx - ry*qy - rz*qz,
    );
  }

  function quatForReading(r) {
    if (!r || r.qw == null) return new THREE.Quaternion();
    return applyBaseline(r.qw, r.qx, r.qy, r.qz);
  }

  function buildHorseHead(scene) {
    const head   = new THREE.Group();
    const coat   = new THREE.MeshPhongMaterial({ color: 0x8b6914, shininess: 20 });
    const dark   = new THREE.MeshPhongMaterial({ color: 0x4a3200, shininess: 10 });
    const black  = new THREE.MeshPhongMaterial({ color: 0x0d0d0d });
    const white  = new THREE.MeshPhongMaterial({ color: 0xf0ead6 });

    const skull = new THREE.Mesh(new THREE.BoxGeometry(0.72, 0.80, 1.0), coat);
    skull.position.set(0, 0.08, -0.05);
    head.add(skull);

    const muzzle = new THREE.Mesh(new THREE.BoxGeometry(0.52, 0.46, 0.72), coat);
    muzzle.position.set(0, -0.28, 0.56);
    head.add(muzzle);

    for (const x of [-0.13, 0.13]) {
      const n = new THREE.Mesh(new THREE.CylinderGeometry(0.07, 0.075, 0.06, 10), dark);
      n.rotation.x = Math.PI / 2;
      n.position.set(x, -0.34, 0.93);
      head.add(n);
    }

    for (const x of [-0.37, 0.37]) {
      const eye = new THREE.Mesh(new THREE.SphereGeometry(0.09, 14, 14), black);
      eye.position.set(x, 0.14, 0.35);
      head.add(eye);
      const hl = new THREE.Mesh(new THREE.SphereGeometry(0.03, 8, 8), white);
      hl.position.set(x * 0.85, 0.17, 0.43);
      head.add(hl);
    }

    for (const x of [-0.22, 0.22]) {
      const ear = new THREE.Mesh(new THREE.ConeGeometry(0.10, 0.38, 5), coat);
      ear.position.set(x, 0.64, -0.18);
      ear.rotation.z = x < 0 ? 0.12 : -0.12;
      head.add(ear);
      const inner = new THREE.Mesh(new THREE.ConeGeometry(0.055, 0.26, 5), dark);
      inner.position.set(x, 0.65, -0.16);
      inner.rotation.z = x < 0 ? 0.12 : -0.12;
      head.add(inner);
    }

    for (let i = 0; i < 4; i++) {
      const strand = new THREE.Mesh(new THREE.BoxGeometry(0.10, 0.22 + i * 0.04, 0.08), dark);
      strand.position.set((i - 1.5) * 0.11, 0.68 + i * 0.02, 0.05 - i * 0.04);
      strand.rotation.z = (i - 1.5) * 0.08;
      head.add(strand);
    }

    const neck = new THREE.Mesh(new THREE.CylinderGeometry(0.30, 0.38, 0.55, 8), coat);
    neck.position.set(0, -0.60, -0.22);
    neck.rotation.x = 0.25;
    head.add(neck);

    scene.add(head);
    return head;
  }

  onMount(() => {
    const W = 280;
    renderer = new THREE.WebGLRenderer({ canvas, antialias: true });
    renderer.setSize(W, W);
    renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));

    const scene = new THREE.Scene();
    scene.background = new THREE.Color(0x0f172a);

    const camera = new THREE.PerspectiveCamera(42, 1, 0.1, 100);
    camera.position.set(0, 0.4, 3.2);
    camera.lookAt(0, 0, 0);

    scene.add(new THREE.AmbientLight(0xffffff, 0.5));
    const sun = new THREE.DirectionalLight(0xfff5e0, 1.0);
    sun.position.set(4, 6, 5);
    scene.add(sun);
    const fill = new THREE.DirectionalLight(0x4488cc, 0.3);
    fill.position.set(-4, -2, -4);
    scene.add(fill);

    const grid = new THREE.GridHelper(4, 8, 0x1e3a5f, 0x1e3a5f);
    grid.position.y = -1.2;
    scene.add(grid);

    const head = buildHorseHead(scene);
    const currentQ = new THREE.Quaternion();

    const FRAME_MS = 700;
    let lastAdvance = 0;
    let idx = 0;

    function animate(ts) {
      animFrame = requestAnimationFrame(animate);

      const list = readings;
      if (list.length === 0) { renderer.render(scene, camera); return; }

      if (ts - lastAdvance > FRAME_MS) {
        idx = (idx + 1) % list.length;
        currentIdx = idx;
        lastAdvance = ts;
      }

      currentQ.slerp(quatForReading(list[idx]), 0.12);
      head.quaternion.copy(currentQ);
      renderer.render(scene, camera);
    }
    requestAnimationFrame(animate);
  });

  onDestroy(() => {
    cancelAnimationFrame(animFrame);
    renderer?.dispose();
  });
</script>

<canvas bind:this={canvas} style="width:280px;height:280px;display:block;"></canvas>
{#if readings.length > 0}
  <div class="info">
    <span class="counter">{currentIdx + 1} / {readings.length}</span>
    <span class="ts">{formatDate(readings[currentIdx]?.timestamp)}</span>
  </div>
{/if}

<style>
  .info {
    display: flex; justify-content: space-between; align-items: center;
    padding: 0.35rem 0.75rem;
    background: #0f172a; color: #64748b;
    font-family: monospace; font-size: 0.78rem;
  }
  .counter { color: #0ea5e9; font-weight: 600; }
</style>
