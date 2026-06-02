<script>
  import { onMount, onDestroy } from 'svelte';
  import * as THREE from 'three';
  import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';

  let { pitch = 0, roll = 0, threshold = 60, tiltDeg = null } = $props();

  let canvas;
  let renderer, scene, camera, controls, curArrow, refArrow, coneGroup, frameId;

  const toRad = d => d * Math.PI / 180;
  const ARROW_LENGTH = 3;

  function buildCone(deg) {
    if (!coneGroup) return;
    coneGroup.clear();
    const coneHeight = ARROW_LENGTH;
    const coneRadius = Math.tan(toRad(Math.min(deg, 89))) * coneHeight;
    const coneMesh = new THREE.Mesh(
      new THREE.ConeGeometry(coneRadius, coneHeight, 64, 1, true),
      new THREE.MeshBasicMaterial({ color: 0xf59e0b, transparent: true, opacity: 0.15, side: THREE.DoubleSide })
    );
    coneMesh.rotation.x = Math.PI;
    coneMesh.position.y = coneHeight / 2;
    coneGroup.add(coneMesh);

    const ringMesh = new THREE.Mesh(
      new THREE.TorusGeometry(coneRadius, 0.04, 8, 64),
      new THREE.MeshBasicMaterial({ color: 0xf59e0b })
    );
    ringMesh.position.y = coneHeight;
    ringMesh.rotation.x = Math.PI / 2;
    coneGroup.add(ringMesh);
  }

  function normalFromAngles(p, r) {
    const px = toRad(p), rz = toRad(-r);
    const v = new THREE.Vector3(0, 1, 0);
    v.applyEuler(new THREE.Euler(px, 0, rz));
    return v.normalize();
  }

  onMount(() => {
    scene = new THREE.Scene();
    scene.background = new THREE.Color(0xf1f5f9);

    camera = new THREE.PerspectiveCamera(40, 400 / 300, 0.1, 100);
    camera.position.set(3, 4, 5);
    camera.lookAt(0, 1, 0);

    renderer = new THREE.WebGLRenderer({ canvas, antialias: true });
    renderer.setSize(400, 300);
    renderer.setPixelRatio(window.devicePixelRatio);

    controls = new OrbitControls(camera, canvas);
    controls.target.set(0, 1, 0);
    controls.enableDamping = true;
    controls.dampingFactor = 0.08;

    scene.add(new THREE.AmbientLight(0xffffff, 0.6));
    const sun = new THREE.DirectionalLight(0xffffff, 1.0);
    sun.position.set(3, 6, 4);
    scene.add(sun);

    scene.add(new THREE.GridHelper(8, 8, 0xcbd5e1, 0xe2e8f0));

    coneGroup = new THREE.Group();
    scene.add(coneGroup);
    buildCone(threshold);

    // reference arrow (green)
    refArrow = new THREE.ArrowHelper(
      normalFromAngles(0, 0), new THREE.Vector3(0, 0, 0), ARROW_LENGTH, 0x22c55e, 0.35, 0.2
    );
    scene.add(refArrow);

    curArrow = new THREE.ArrowHelper(
      normalFromAngles(0, 0), new THREE.Vector3(0, 0, 0), ARROW_LENGTH, 0x3b82f6, 0.35, 0.2
    );
    scene.add(curArrow);

    function animate() {
      frameId = requestAnimationFrame(animate);
      controls.update();
      renderer.render(scene, camera);
    }
    animate();
  });

  onDestroy(() => {
    cancelAnimationFrame(frameId);
    controls?.dispose();
    renderer?.dispose();
  });

  $effect(() => {
    if (!curArrow || !refArrow || !coneGroup) return;
    const refDir = normalFromAngles(0, 0);
    curArrow.setDirection(normalFromAngles(pitch, roll));
    refArrow.setDirection(refDir);
    coneGroup.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), refDir);
  });

  $effect(() => {
    buildCone(threshold);
  });
</script>

<div class="viewer">
  <canvas bind:this={canvas} width="400" height="300"></canvas>
  <div class="legend">
    <span class="ref">▲ Calibrated</span>
    <span class="cur">▲ Current</span>
    <span class="thr">◈ Threshold ({threshold}°)</span>
    {#if tiltDeg !== null}
      <span class="tilt" class:over={tiltDeg > threshold}>Tilt: {tiltDeg.toFixed(1)}°</span>
    {/if}
  </div>
</div>

<style>
  .viewer { display: inline-block; }
  canvas { display: block; border-radius: 8px; border: 1px solid #e2e8f0; cursor: grab; }
  canvas:active { cursor: grabbing; }
  .legend { display: flex; gap: 1.25rem; font-size: 0.82rem; margin-top: 0.4rem; flex-wrap: wrap; }
  .ref { color: #16a34a; font-weight: 600; }
  .cur { color: #2563eb; font-weight: 600; }
  .thr { color: #d97706; font-weight: 600; }
  .tilt { color: #64748b; font-weight: 600; }
  .tilt.over { color: #ef4444; }
</style>
