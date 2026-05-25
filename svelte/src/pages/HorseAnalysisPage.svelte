<script>
  // @ts-nocheck
  import { onMount } from 'svelte';
  import { pop } from 'svelte-spa-router';
  import { horses } from '../services/horses.js';
  import { sensorReadings } from '../services/sensorReadings.js';

  let { params = {} } = $props();

  let horse       = $state(null);
  let loading     = $state(false);
  let allReadings = $state([]);

  // Date range
  function todayString() { return new Date().toISOString().slice(0, 10); }
  function yesterdayString() {
    const d = new Date(); d.setDate(d.getDate() - 1);
    return d.toISOString().slice(0, 10);
  }

  let fromDate = $state(yesterdayString());
  let fromTime = $state('00:00');
  let toDate   = $state(todayString());
  let toTime   = $state('23:59');
  let stillThreshold   = $state(0.2);
  let extremeSigma     = $state(3);

  onMount(async () => {
    horse = await horses.getById(params.id);
  });

  async function load() {
    loading = true;
    allReadings = [];

    // Fetch all days in range
    const start = new Date(fromDate);
    const end   = new Date(toDate);
    const days  = [];
    for (let d = new Date(start); d <= end; d.setDate(d.getDate() + 1))
      days.push(d.toISOString().slice(0, 10));

    const results = await Promise.all(days.map(d => sensorReadings.getByHorse(params.id, d)));
    // Parse as local time so the user always enters what they see on the clock
    const fromDt = new Date(`${fromDate}T${fromTime}:00`);
    const toDt   = new Date(`${toDate}T${toTime}:59`);
    allReadings = results.flat().filter(r => {
      const t = new Date(r.timestamp);
      return t >= fromDt && t <= toDt;
    });
    loading = false;
  }

  function tilt(r) {
    return (r.pitch != null && r.roll != null) ? Math.sqrt(r.pitch ** 2 + r.roll ** 2) : null;
  }

  function percentile(sorted, p) {
    const idx = (p / 100) * (sorted.length - 1);
    const lo = Math.floor(idx), hi = Math.ceil(idx);
    return sorted[lo] + (sorted[hi] - sorted[lo]) * (idx - lo);
  }

  function stats(values) {
    const v = values.filter(x => x != null).sort((a, b) => a - b);
    if (!v.length) return null;
    const avg = v.reduce((a, b) => a + b, 0) / v.length;
    const stddev = Math.sqrt(v.reduce((a, b) => a + (b - avg) ** 2, 0) / v.length);
    return {
      min:   v[0],
      max:   v[v.length - 1],
      avg,
      stddev,
      p5:    percentile(v, 5),
      p95:   percentile(v, 95),
      count: v.length,
    };
  }

  function fmt(v, dec = 1) { return v == null ? '—' : v.toFixed(dec); }

  function histogram(values, binSize = 5) {
    const v = values.filter(x => x != null);
    if (!v.length) return [];
    const mn = Math.floor(Math.min(...v) / binSize) * binSize;
    const mx = Math.ceil(Math.max(...v) / binSize) * binSize;
    const bins = [];
    for (let lo = mn; lo < mx; lo += binSize) {
      const count = v.filter(x => x >= lo && x < lo + binSize).length;
      bins.push({ lo, hi: lo + binSize, count });
    }
    return bins;
  }

  // ── Computed analyses ────────────────────────────────────────────────────────

  const stillReadings = $derived(
    allReadings.filter(r => r.acceleration != null && r.acceleration < stillThreshold)
  );

  const stillStats = $derived(() => {
    const s = stillReadings;
    if (!s.length) return null;
    return {
      count:  s.length,
      pitch:  stats(s.map(r => r.pitch)),
      roll:   stats(s.map(r => r.roll)),
      tilt:   stats(s.map(r => tilt(r))),
      accel:  stats(s.map(r => r.acceleration)),
    };
  });

  const extremes = $derived(() => {
    if (!allReadings.length) return null;
    const withTilt = allReadings.map(r => ({ ...r, tiltVal: tilt(r) }));
    const pick = (arr, fn) => arr.reduce((best, r) => fn(r) > fn(best) ? r : best);
    const pickMin = (arr, fn) => arr.reduce((best, r) => fn(r) < fn(best) ? r : best);
    return {
      maxTilt:  pick(withTilt.filter(r => r.tiltVal != null), r => r.tiltVal),
      minTilt:  pickMin(withTilt.filter(r => r.tiltVal != null), r => r.tiltVal),
      maxAccel: pick(allReadings.filter(r => r.acceleration != null), r => r.acceleration),
      maxGyro:  pick(allReadings.filter(r => r.angularVelocity != null), r => r.angularVelocity),
      maxPitch: pick(allReadings.filter(r => r.pitch != null), r => r.pitch),
      minPitch: pickMin(allReadings.filter(r => r.pitch != null), r => r.pitch),
      maxRoll:  pick(allReadings.filter(r => r.roll != null), r => r.roll),
      minRoll:  pickMin(allReadings.filter(r => r.roll != null), r => r.roll),
    };
  });

  const allStats = $derived(() => {
    if (!allReadings.length) return null;
    return {
      pitch: stats(allReadings.map(r => r.pitch)),
      roll:  stats(allReadings.map(r => r.roll)),
      tilt:  stats(allReadings.map(r => tilt(r))),
    };
  });

  const activity = $derived(() => {
    if (!allReadings.length) return null;
    const counts = {};
    for (const r of allReadings)
      counts[r.state] = (counts[r.state] ?? 0) + 1;
    const total = allReadings.length;
    return Object.entries(counts)
      .sort((a, b) => b[1] - a[1])
      .map(([state, count]) => ({ state, count, pct: (count / total * 100) }));
  });

  const stateColors = {
    Standing:  '#22c55e',
    Moving:    '#3b82f6',
    LyingDown: '#f59e0b',
    Rolling:   '#ef4444',
    Alert:     '#f97316',
    Emergency: '#dc2626',
  };
</script>

<main>
  <button onclick={pop}>← Back</button>

  {#if horse}
    <h1>{horse.name} — Analysis</h1>
  {/if}

  <section class="controls">
    <label>
      From
      <input type="date" bind:value={fromDate} max={toDate} />
    </label>
    <label>
      Time
      <input type="time" bind:value={fromTime} />
    </label>
    <label>
      To
      <input type="date" bind:value={toDate} min={fromDate} max={todayString()} />
    </label>
    <label>
      Time
      <input type="time" bind:value={toTime} />
    </label>
    <label>
      Still threshold (accel &lt;)
      <input type="number" min="0.05" max="1" step="0.05" bind:value={stillThreshold} />
    </label>
    <button class="load-btn" onclick={load} disabled={loading}>
      {loading ? 'Loading…' : 'Load'}
    </button>
  </section>

  {#if allReadings.length > 0}
    <p class="count">{allReadings.length} readings loaded</p>

    <!-- ── Still periods ─────────────────────────────────────────────────── -->
    <section>
      <h2>Still Periods <span class="sub">accel &lt; {stillThreshold}</span></h2>
      {#if stillStats()}
        {@const s = stillStats()}
        <p class="desc">
          {s.count} readings where the horse was barely moving ({(s.count / allReadings.length * 100).toFixed(0)}% of total).
          These are the best readings for determining head orientation.
        </p>
        <table>
          <thead><tr><th>Metric</th><th>5th %ile</th><th>Avg</th><th>95th %ile</th><th>Stddev</th></tr></thead>
          <tbody>
            <tr><td>Pitch (sensor)</td><td>{fmt(s.pitch?.p5)}°</td><td>{fmt(s.pitch?.avg)}°</td><td>{fmt(s.pitch?.p95)}°</td><td>±{fmt(s.pitch?.stddev)}°</td></tr>
            <tr><td>Roll (sensor)</td><td>{fmt(s.roll?.p5)}°</td><td>{fmt(s.roll?.avg)}°</td><td>{fmt(s.roll?.p95)}°</td><td>±{fmt(s.roll?.stddev)}°</td></tr>
            <tr><td>Tilt</td><td>{fmt(s.tilt?.p5)}°</td><td>{fmt(s.tilt?.avg)}°</td><td>{fmt(s.tilt?.p95)}°</td><td>±{fmt(s.tilt?.stddev)}°</td></tr>
            <tr><td>Acceleration</td><td>{fmt(s.accel?.p5, 2)}</td><td>{fmt(s.accel?.avg, 2)}</td><td>{fmt(s.accel?.p95, 2)}</td><td>±{fmt(s.accel?.stddev, 2)}</td></tr>
          </tbody>
        </table>

        <div class="range-chart">
          {#each [
            { label: 'Pitch (sensor)', st: s.pitch, unit: '°', color: '#3b82f6' },
            { label: 'Roll (sensor)',  st: s.roll,  unit: '°', color: '#f59e0b' },
            { label: 'Tilt',          st: s.tilt,  unit: '°', color: '#22c55e' },
          ] as { label, st, unit, color }}
            {#if st}
              {@const span = st.max - st.min || 1}
              {@const p5pct  = (st.p5  - st.min) / span * 100}
              {@const p95pct = (st.p95 - st.min) / span * 100}
              {@const avgpct = (st.avg - st.min) / span * 100}
              <div class="range-row">
                <span class="range-label">{label}</span>
                <div class="range-track">
                  <div class="range-fill" style="left:{p5pct}%; width:{p95pct - p5pct}%; background:{color}"></div>
                  <div class="range-avg"  style="left:{avgpct}%"></div>
                </div>
                <span class="range-ends">{fmt(st.p5)}{unit} – {fmt(st.p95)}{unit}</span>
              </div>
            {/if}
          {/each}
          <p class="range-legend">
            <span class="legend-bar" style="background:#94a3b8"></span> 5th–95th percentile &nbsp;
            <span class="legend-tick"></span> avg
          </p>
        </div>
      {:else}
        <p>No still readings found.</p>
      {/if}
    </section>

    <!-- ── Baseline vs Outliers ───────────────────────────────────────────── -->
    {#if stillStats() && allStats() && extremes()}
      {@const ss = stillStats()}
      {@const as = allStats()}
      {@const e  = extremes()}
      <section>
        <h2>Baseline vs Outliers</h2>
        <p class="desc">
          Green band = still-period baseline (5th–95th %). Dots = extreme values across all readings. Track = full observed range.
        </p>
        {#each [
          { label: 'Pitch',  color: '#3b82f6', baseline: ss.pitch, all: as.pitch,
            vals: allReadings.map(r => r.pitch),
            outliers: [{ val: e.maxPitch.pitch, label: 'max' }, { val: e.minPitch.pitch, label: 'min' }] },
          { label: 'Roll',   color: '#f59e0b', baseline: ss.roll,  all: as.roll,
            vals: allReadings.map(r => r.roll),
            outliers: [{ val: e.maxRoll.roll,   label: 'max' }, { val: e.minRoll.roll,   label: 'min' }] },
          { label: 'Tilt',   color: '#22c55e', baseline: ss.tilt,  all: as.tilt,
            vals: allReadings.map(r => tilt(r)),
            outliers: [{ val: e.maxTilt.tiltVal, label: 'max' }, { val: e.minTilt.tiltVal, label: 'min' }] },
        ] as row}
          {#if row.baseline && row.all}
            {@const span = row.all.max - row.all.min || 1}
            {@const pct  = v => ((v - row.all.min) / span * 100).toFixed(2)}
            {@const bw   = pct(row.baseline.p95) - pct(row.baseline.p5)}
            {@const nOut = row.vals.filter(v => v != null && (v < row.baseline.p5 || v > row.baseline.p95)).length}
            {@const nTotal = row.vals.filter(v => v != null).length}
            <div class="ol-row">
              <span class="ol-label">{row.label}</span>
              <div class="ol-track">
                <div class="ol-band" style="left:{pct(row.baseline.p5)}%; width:{bw}%; background:{row.color}"></div>
                <div class="ol-avg" style="left:{pct(row.baseline.avg)}%"></div>
                {#each row.outliers as o}
                  {#if o.val != null}
                    <div class="ol-dot" style="left:{pct(o.val)}%" title="{o.label}: {fmt(o.val)}°">
                      <span class="ol-dot-label">{fmt(o.val)}°</span>
                    </div>
                  {/if}
                {/each}
              </div>
              <div class="ol-ends">
                <span>{fmt(row.all.min)}°</span>
                <span>{fmt(row.all.max)}°</span>
              </div>
              <span class="ol-count" title="readings outside baseline band">
                {nOut} <span class="ol-count-pct">({(nOut/nTotal*100).toFixed(0)}%)</span>
              </span>
            </div>
          {/if}
        {/each}
        <p class="ol-legend">
          <span class="legend-band"></span> baseline (still, p5–p95) &nbsp;
          <span class="legend-avgline"></span> avg &nbsp;
          <span class="legend-dot"></span> min/max &nbsp;
          <span style="color:#dc2626; font-weight:600;">count</span> = readings outside baseline band
        </p>
      </section>
    {/if}

    <!-- ── Outlier distribution ───────────────────────────────────────────── -->
    {#if stillStats()}
      {@const ss = stillStats()}
      <section>
        <h2>Outlier Distribution</h2>
        <p class="desc">All readings bucketed in 5° bins. Red = outside still-period baseline. Gray = within baseline.</p>
        {#each [
          { label: 'Pitch', vals: allReadings.map(r => r.pitch),  baseline: ss.pitch, unit: '°' },
          { label: 'Roll',  vals: allReadings.map(r => r.roll),   baseline: ss.roll,  unit: '°' },
          { label: 'Tilt',  vals: allReadings.map(r => tilt(r)),  baseline: ss.tilt,  unit: '°' },
        ] as row}
          {#if row.baseline}
            {@const bins = histogram(row.vals, 5)}
            {@const maxCount = Math.max(...bins.map(b => b.count))}
            <div class="histo-group">
              <div class="histo-title">{row.label}</div>
              {#each bins as bin}
                {#if bin.count > 0}
                  {@const isOutlier = bin.hi <= row.baseline.p5 || bin.lo >= row.baseline.p95}
                  <div class="histo-row">
                    <span class="histo-label">{bin.lo >= 0 ? '+' : ''}{bin.lo}° to {bin.hi >= 0 ? '+' : ''}{bin.hi}°</span>
                    <div class="histo-track">
                      <div class="histo-bar" style="width:{bin.count / maxCount * 100}%; background:{isOutlier ? '#dc2626' : '#94a3b8'}"></div>
                    </div>
                    <span class="histo-count" style="color:{isOutlier ? '#dc2626' : '#64748b'}">{bin.count}</span>
                  </div>
                {/if}
              {/each}
            </div>
          {/if}
        {/each}
      </section>
    {/if}

    <!-- ── Outlier timeline ───────────────────────────────────────────────── -->
    {#if stillStats() && allReadings.length > 1}
      {@const ss = stillStats()}
      {@const t0 = new Date(allReadings[0].timestamp).getTime()}
      {@const t1 = new Date(allReadings[allReadings.length - 1].timestamp).getTime()}
      {@const tspan = t1 - t0 || 1}
      {@const tpct = r => ((new Date(r.timestamp).getTime() - t0) / tspan * 100).toFixed(3)}
      {@const fmtTime = ts => new Date(ts).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' })}
      <section>
        <h2>Outlier Timeline</h2>
        <p class="desc">Each tick = one reading outside the still-period baseline. Clusters mean repeated outliers close together in time.</p>
        {#each [
          { label: 'Pitch', baseline: ss.pitch, getVal: r => r.pitch },
          { label: 'Roll',  baseline: ss.roll,  getVal: r => r.roll  },
          { label: 'Tilt',  baseline: ss.tilt,  getVal: r => tilt(r) },
        ] as row}
          {#if row.baseline}
            {@const outs = allReadings.filter(r => {
              const v = row.getVal(r);
              return v != null && (v < row.baseline.p5 || v > row.baseline.p95);
            })}
            <div class="zt-row">
              <span class="zt-label">{row.label}</span>
              <div class="zt-track">
                {#each outs as r}
                  <div class="zt-tick" style="left:{tpct(r)}%"
                    title="{new Date(r.timestamp).toLocaleTimeString()} — {fmt(row.getVal(r))}°"></div>
                {/each}
              </div>
              <span class="zt-count">{outs.length}</span>
            </div>
          {/if}
        {/each}
        <div class="zt-axis">
          <span>{fmtTime(allReadings[0].timestamp)}</span>
          <span>{fmtTime(allReadings[allReadings.length - 1].timestamp)}</span>
        </div>
      </section>
    {/if}

    <!-- ── Extreme outlier timeline ──────────────────────────────────────── -->
    {#if stillStats() && allReadings.length > 1}
      {@const ss = stillStats()}
      {@const t0 = new Date(allReadings[0].timestamp).getTime()}
      {@const t1 = new Date(allReadings[allReadings.length - 1].timestamp).getTime()}
      {@const tspan = t1 - t0 || 1}
      {@const tpct = r => ((new Date(r.timestamp).getTime() - t0) / tspan * 100).toFixed(3)}
      {@const fmtTime = ts => new Date(ts).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' })}
      <section>
        <h2>Extreme Outlier Timeline</h2>
        <div class="sigma-control">
          <label>
            Threshold: <strong>{extremeSigma}σ</strong>
            = avg ± {extremeSigma}× stddev
          </label>
          <input type="range" min="1" max="6" step="0.5" bind:value={extremeSigma} />
          <span class="sigma-hint">← more &nbsp; fewer →</span>
        </div>
        {#each [
          { label: 'Pitch', baseline: ss.pitch, getVal: r => r.pitch },
          { label: 'Roll',  baseline: ss.roll,  getVal: r => r.roll  },
          { label: 'Tilt',  baseline: ss.tilt,  getVal: r => tilt(r) },
        ] as row}
          {#if row.baseline}
            {@const lo = row.baseline.avg - extremeSigma * row.baseline.stddev}
            {@const hi = row.baseline.avg + extremeSigma * row.baseline.stddev}
            {@const outs = allReadings.filter(r => {
              const v = row.getVal(r);
              return v != null && (v < lo || v > hi);
            })}
            <div class="zt-row">
              <span class="zt-label">{row.label}</span>
              <div class="zt-track">
                {#each outs as r}
                  <div class="zt-tick zt-tick-extreme" style="left:{tpct(r)}%"
                    title="{new Date(r.timestamp).toLocaleTimeString()} — {fmt(row.getVal(r))}°"></div>
                {/each}
              </div>
              <span class="zt-count">{outs.length}</span>
            </div>
            <div class="sigma-range">avg {fmt(row.baseline.avg)}° ± {fmt(extremeSigma * row.baseline.stddev)}° → outside [{fmt(lo)}°, {fmt(hi)}°]</div>
          {/if}
        {/each}
        <div class="zt-axis">
          <span>{fmtTime(allReadings[0].timestamp)}</span>
          <span>{fmtTime(allReadings[allReadings.length - 1].timestamp)}</span>
        </div>
      </section>
    {/if}

    <!-- ── Extreme points ────────────────────────────────────────────────── -->
    <section>
      <h2>Extreme Points</h2>
      {#if extremes()}
        {@const e = extremes()}
        <table>
          <thead><tr><th>Metric</th><th>Value</th><th>State</th><th>Time</th></tr></thead>
          <tbody>
            <tr><td>Max tilt</td><td>{fmt(e.maxTilt.tiltVal)}°</td><td>{e.maxTilt.state}</td><td>{new Date(e.maxTilt.timestamp).toLocaleTimeString()}</td></tr>
            <tr><td>Min tilt</td><td>{fmt(e.minTilt.tiltVal)}°</td><td>{e.minTilt.state}</td><td>{new Date(e.minTilt.timestamp).toLocaleTimeString()}</td></tr>
            <tr><td>Max acceleration</td><td>{fmt(e.maxAccel.acceleration, 2)}</td><td>{e.maxAccel.state}</td><td>{new Date(e.maxAccel.timestamp).toLocaleTimeString()}</td></tr>
            <tr><td>Max gyro</td><td>{fmt(e.maxGyro.angularVelocity, 2)}</td><td>{e.maxGyro.state}</td><td>{new Date(e.maxGyro.timestamp).toLocaleTimeString()}</td></tr>
            <tr><td>Max pitch</td><td>{fmt(e.maxPitch.pitch)}°</td><td>{e.maxPitch.state}</td><td>{new Date(e.maxPitch.timestamp).toLocaleTimeString()}</td></tr>
            <tr><td>Min pitch</td><td>{fmt(e.minPitch.pitch)}°</td><td>{e.minPitch.state}</td><td>{new Date(e.minPitch.timestamp).toLocaleTimeString()}</td></tr>
            <tr><td>Max roll</td><td>{fmt(e.maxRoll.roll)}°</td><td>{e.maxRoll.state}</td><td>{new Date(e.maxRoll.timestamp).toLocaleTimeString()}</td></tr>
            <tr><td>Min roll</td><td>{fmt(e.minRoll.roll)}°</td><td>{e.minRoll.state}</td><td>{new Date(e.minRoll.timestamp).toLocaleTimeString()}</td></tr>
          </tbody>
        </table>
      {/if}
    </section>

    <!-- ── Activity ──────────────────────────────────────────────────────── -->
    <section>
      <h2>Activity Breakdown</h2>
      {#if activity()}
        <div class="activity-bars">
          {#each activity() as { state, count, pct }}
            <div class="activity-row">
              <span class="state-name">{state}</span>
              <div class="bar-track">
                <div class="bar-fill" style="width:{pct}%; background:{stateColors[state] ?? '#888'}"></div>
              </div>
              <span class="activity-count">{count} <span class="pct">({pct.toFixed(0)}%)</span></span>
            </div>
          {/each}
        </div>
        <p class="desc">~{allReadings.length} seconds of data (~{(allReadings.length / 3600).toFixed(1)} hours at 1s interval)</p>
      {/if}
    </section>

  {:else if !loading}
    <p class="empty">Select a date range and press Load.</p>
  {/if}
</main>

<style>
  main { max-width: 700px; margin: 2rem auto; padding: 0 1rem; }
  h1 { margin-bottom: 1.5rem; }
  h2 { margin: 0 0 0.75rem; font-size: 1.1rem; }
  h2 .sub { font-size: 0.8rem; font-weight: 400; color: #64748b; margin-left: 0.5rem; }
  section { border: 1px solid #ddd; border-radius: 6px; padding: 1.25rem; margin-bottom: 1.25rem; }
  section.controls { display: flex; flex-wrap: wrap; gap: 1rem; align-items: flex-end; }
  label { display: flex; flex-direction: column; gap: 0.25rem; font-size: 0.85rem; font-weight: 600; color: #475569; }
  input { padding: 0.4rem 0.6rem; border: 1px solid #ddd; border-radius: 4px; font-size: 0.95rem; }
  input[type=number] { width: 80px; }
  .load-btn { background: #1e293b; color: white; border: none; border-radius: 4px; padding: 0.5rem 1.25rem; font-size: 0.95rem; cursor: pointer; align-self: flex-end; }
  .load-btn:disabled { opacity: 0.6; cursor: not-allowed; }
  .count { color: #64748b; font-size: 0.85rem; margin: -0.5rem 0 1rem; }
  .desc { font-size: 0.85rem; color: #64748b; margin-bottom: 0.75rem; }
  table { width: 100%; border-collapse: collapse; font-size: 0.9rem; }
  th, td { text-align: left; padding: 0.4rem 0.6rem; border-bottom: 1px solid #eee; }
  th { font-weight: 600; color: #475569; }
  .activity-bars { display: flex; flex-direction: column; gap: 0.6rem; margin-bottom: 0.75rem; }
  .activity-row { display: flex; align-items: center; gap: 0.75rem; }
  .state-name { width: 90px; font-size: 0.85rem; font-weight: 600; }
  .bar-track { flex: 1; height: 18px; background: #f1f5f9; border-radius: 4px; overflow: hidden; }
  .bar-fill { height: 100%; border-radius: 4px; transition: width 0.4s; }
  .activity-count { font-size: 0.85rem; min-width: 80px; text-align: right; }
  .pct { color: #94a3b8; }
  .empty { color: #94a3b8; font-style: italic; }
  button { padding: 0.5rem 1rem; cursor: pointer; margin-bottom: 1rem; }
  .ol-row { display: flex; align-items: center; gap: 0.75rem; margin-bottom: 1.25rem; }
  .ol-label { width: 50px; font-size: 0.82rem; font-weight: 600; color: #475569; flex-shrink: 0; }
  .ol-track { flex: 1; height: 20px; background: #f1f5f9; border-radius: 10px; position: relative; }
  .ol-band { position: absolute; top: 0; height: 100%; border-radius: 10px; opacity: 0.35; }
  .ol-avg { position: absolute; top: -4px; width: 3px; height: 28px; background: #1e293b; border-radius: 2px; transform: translateX(-50%); }
  .ol-dot { position: absolute; top: 50%; transform: translate(-50%, -50%); width: 12px; height: 12px; background: #dc2626; border: 2px solid white; border-radius: 50%; cursor: default; }
  .ol-dot-label { position: absolute; top: -20px; left: 50%; transform: translateX(-50%); font-size: 0.7rem; color: #dc2626; white-space: nowrap; font-weight: 600; }
  .ol-ends { display: flex; justify-content: space-between; width: 90px; font-size: 0.75rem; color: #94a3b8; flex-shrink: 0; }
  .ol-count { font-size: 0.82rem; font-weight: 600; color: #dc2626; min-width: 70px; text-align: right; flex-shrink: 0; }
  .ol-count-pct { font-weight: 400; color: #94a3b8; }
  .ol-legend { font-size: 0.75rem; color: #94a3b8; display: flex; align-items: center; gap: 0.4rem; margin-top: 0.5rem; flex-wrap: wrap; }
  .legend-band { display: inline-block; width: 24px; height: 10px; border-radius: 4px; background: #64748b; opacity: 0.35; }
  .legend-avgline { display: inline-block; width: 3px; height: 14px; background: #1e293b; border-radius: 2px; }
  .legend-dot { display: inline-block; width: 10px; height: 10px; background: #dc2626; border: 2px solid white; border-radius: 50%; outline: 1px solid #dc2626; }
  .zt-row { display: flex; align-items: center; gap: 0.75rem; margin-bottom: 0.6rem; }
  .zt-label { width: 50px; font-size: 0.82rem; font-weight: 600; color: #475569; flex-shrink: 0; }
  .zt-track { flex: 1; height: 28px; background: #f1f5f9; border-radius: 4px; position: relative; }
  .zt-tick { position: absolute; top: 3px; width: 2px; height: 22px; background: #dc2626; border-radius: 1px; transform: translateX(-50%); opacity: 0.7; }
  .zt-tick-extreme { background: #7c3aed; opacity: 0.9; }
  .zt-count { width: 45px; font-size: 0.82rem; font-weight: 600; color: #dc2626; text-align: right; flex-shrink: 0; }
  .zt-axis { display: flex; justify-content: space-between; font-size: 0.75rem; color: #94a3b8; margin-top: 0.25rem; padding: 0 0 0 60px; }
  .sigma-control { display: flex; align-items: center; gap: 1rem; margin-bottom: 1rem; flex-wrap: wrap; }
  .sigma-control label { font-size: 0.88rem; color: #475569; }
  .sigma-control input { width: 160px; accent-color: #7c3aed; }
  .sigma-hint { font-size: 0.75rem; color: #94a3b8; }
  .sigma-range { font-size: 0.75rem; color: #94a3b8; margin: -0.3rem 0 0.6rem 60px; }
  .histo-group { margin-bottom: 1.5rem; }
  .histo-title { font-size: 0.82rem; font-weight: 700; color: #475569; text-transform: uppercase; letter-spacing: 0.05em; margin-bottom: 0.4rem; }
  .histo-row { display: flex; align-items: center; gap: 0.5rem; margin-bottom: 0.2rem; }
  .histo-label { width: 130px; font-size: 0.78rem; color: #64748b; font-family: monospace; flex-shrink: 0; text-align: right; }
  .histo-track { flex: 1; height: 14px; background: #f1f5f9; border-radius: 4px; overflow: hidden; }
  .histo-bar { height: 100%; border-radius: 4px; transition: width 0.3s; }
  .histo-count { width: 45px; font-size: 0.78rem; font-weight: 600; text-align: right; flex-shrink: 0; }
  .range-chart { margin-top: 1.25rem; display: flex; flex-direction: column; gap: 0.75rem; }
  .range-row { display: flex; align-items: center; gap: 0.75rem; }
  .range-label { width: 110px; font-size: 0.82rem; color: #475569; font-weight: 600; flex-shrink: 0; }
  .range-track { flex: 1; height: 16px; background: #f1f5f9; border-radius: 8px; position: relative; }
  .range-fill { position: absolute; top: 0; height: 100%; border-radius: 8px; opacity: 0.7; }
  .range-avg { position: absolute; top: -3px; width: 3px; height: 22px; background: #1e293b; border-radius: 2px; transform: translateX(-50%); }
  .range-ends { font-size: 0.78rem; color: #64748b; white-space: nowrap; min-width: 100px; }
  .range-legend { font-size: 0.75rem; color: #94a3b8; display: flex; align-items: center; gap: 0.25rem; margin-top: 0.25rem; }
  .legend-bar { display: inline-block; width: 24px; height: 10px; border-radius: 4px; background: #94a3b8; opacity: 0.7; }
  .legend-tick { display: inline-block; width: 3px; height: 14px; background: #1e293b; border-radius: 2px; margin: 0 0.25rem; }
</style>
