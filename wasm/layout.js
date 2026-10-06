/*
 * Force-directed layout, modeled on d3-force: springs on edges weighted by
 * degree, long-range repulsion through a Barnes-Hut quadtree (a tick is
 * O(m + n log n)), weak gravity towards the origin so disconnected
 * components stay on screen, velocity decay and a cap on each step.
 *
 * Only used when the graph comes without coordinates (see GraphView).
 */

export const LAYOUT_DEFAULTS = {
  linkDistance: 30,
  repulsion: 30,        // node-node push, as d3.forceManyBody().strength(-30)
  theta: 0.9,           // Barnes-Hut accuracy: higher is faster and coarser
  gravity: 0.03,        // pull towards the origin, as d3.forceX/forceY
  velocityDecay: 0.4,
  maxStep: 100,         // cap on a vertex's move per tick, keeps dense graphs stable
  alphaDecay: 1 - Math.pow(0.001, 1 / 300), // cools from 1 to 0.001 in 300 ticks
};


/*
 * Barnes-Hut quadtree over the visible vertices. Each cell stores the
 * number of vertices below it and their centroid; a leaf stores one vertex
 * (coincident vertices share a leaf through `more`).
 */
function buildQuadtree(x, y, vis) {
  let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity;
  for (const v of vis) {
    if (x[v] < x0) x0 = x[v]; if (x[v] > x1) x1 = x[v];
    if (y[v] < y0) y0 = y[v]; if (y[v] > y1) y1 = y[v];
  }
  const size = Math.max(x1 - x0, y1 - y0, 1);
  const root = { x0, y0, size, count: 0, cx: 0, cy: 0, kids: null, v: -1, more: null };

  const child = (q, v) => {
    const h = q.size / 2;
    const i = (x[v] >= q.x0 + h ? 1 : 0) | (y[v] >= q.y0 + h ? 2 : 0);
    return (q.kids[i] ??= { x0: q.x0 + (i & 1 ? h : 0), y0: q.y0 + (i & 2 ? h : 0), size: h,
                            count: 0, cx: 0, cy: 0, kids: null, v: -1, more: null });
  };
  const insert = (v) => {
    let q = root, depth = 0;
    for (;;) {
      if (q.kids) { q = child(q, v); depth++; continue; }
      if (q.v < 0) { q.v = v; return; }
      /* Occupied leaf: split it, unless the two vertices (nearly) coincide. */
      if (depth > 40 || (x[q.v] === x[v] && y[q.v] === y[v])) { (q.more ??= []).push(v); return; }
      const old = q.v;
      q.v = -1;
      q.kids = [null, null, null, null];
      child(q, old).v = old;
    }
  };
  for (const v of vis) insert(v);

  /* Bottom-up: vertex count and centroid of every cell. */
  const finish = (q) => {
    if (q.kids) {
      for (const k of q.kids) {
        if (!k) continue;
        finish(k);
        q.count += k.count; q.cx += k.cx * k.count; q.cy += k.cy * k.count;
      }
    } else if (q.v >= 0) {
      q.count = 1; q.cx = x[q.v]; q.cy = y[q.v];
      if (q.more) for (const u of q.more) { q.count++; q.cx += x[u]; q.cy += y[u]; }
    }
    if (q.count) { q.cx /= q.count; q.cy /= q.count; }
  };
  finish(root);
  return root;
}

/*
 * One simulation step. s holds the state: positions x/y, velocities vx/vy
 * (Float64Array), pinned (Uint8Array), the edge list eu/ev, degree, the
 * current alpha and hidden(v); o the options. Updates s in place.
 */
export function forceTick(s, o) {
  const { x, y, vx, vy, pinned, eu, ev, degree } = s;
  const n = x.length, a = s.alpha;

  /* Springs along edges, as d3.forceLink: the strength is divided by the
   * smaller degree and the correction is split by degree, so hubs with
   * hundreds of edges do not get yanked around by all of them at once. */
  for (let i = 0; i < eu.length; i++) {
    const u = eu[i], v = ev[i];
    if (u === v) continue;
    let dx = x[v] + vx[v] - x[u] - vx[u], dy = y[v] + vy[v] - y[u] - vy[u];
    const d = Math.sqrt(dx * dx + dy * dy) || 1e-6;
    const k = ((d - o.linkDistance) / d) * a / Math.min(degree[u], degree[v]);
    dx *= k; dy *= k;
    const b = degree[u] / (degree[u] + degree[v]);
    vx[v] -= dx * b; vy[v] -= dy * b;
    vx[u] += dx * (1 - b); vy[u] += dy * (1 - b);
  }

  /* Repulsion: far-away cells act as a single body at their centroid. */
  const vis = [];
  for (let v = 0; v < n; v++) if (!s.hidden(v)) vis.push(v);
  const root = buildQuadtree(x, y, vis);
  const theta2 = o.theta * o.theta, strength = o.repulsion * a;
  const push = (v, px, py, w) => {
    let dx = x[v] - px, dy = y[v] - py, d2 = dx * dx + dy * dy;
    if (d2 === 0) { dx = Math.random() - 0.5; dy = Math.random() - 0.5; d2 = dx * dx + dy * dy; }
    if (d2 < 1) d2 = Math.sqrt(d2); // d3's distanceMin: tames near-collisions
    const f = (strength * w) / d2;
    vx[v] += dx * f; vy[v] += dy * f;
  };
  const stack = [];
  for (const v of vis) {
    stack.push(root);
    while (stack.length) {
      const q = stack.pop();
      if (!q.count) continue;
      const dx = q.cx - x[v], dy = q.cy - y[v];
      if (q.kids && (q.size * q.size) / (dx * dx + dy * dy) < theta2) {
        push(v, q.cx, q.cy, q.count);
        continue;
      }
      if (q.kids) { for (const k of q.kids) if (k) stack.push(k); continue; }
      if (q.v !== v) push(v, x[q.v], y[q.v], 1);
      if (q.more) for (const u of q.more) if (u !== v) push(v, x[u], y[u], 1);
    }
  }

  const keep = 1 - o.velocityDecay, g = o.gravity * a, cap = o.maxStep;
  for (let v = 0; v < n; v++) {
    if (pinned[v]) { vx[v] = vy[v] = 0; continue; }
    vx[v] = (vx[v] - x[v] * g) * keep;
    vy[v] = (vy[v] - y[v] * g) * keep;
    const sp = Math.sqrt(vx[v] * vx[v] + vy[v] * vy[v]);
    if (sp > cap) { vx[v] *= cap / sp; vy[v] *= cap / sp; }
    x[v] += vx[v];
    y[v] += vy[v];
  }
  s.alpha += (0 - s.alpha) * o.alphaDecay;
}
