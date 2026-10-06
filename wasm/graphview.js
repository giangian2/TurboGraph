/*
 * WebGL renderer for graphs coming from turbograph.js.
 *
 * The C library only knows topology: positions, layout and interaction
 * live here. Vertices get their positions either from real coordinates
 * (setPositions, e.g. a DIMACS .co file, see coords.js) or from the
 * force-directed layout in layout.js, which only runs on graphs small
 * enough for it (opt.maxLayoutN).
 *
 * Edges and vertices are drawn with WebGL from flat buffers, so large road
 * networks (hundreds of thousands of edges) stay interactive. A 2D canvas
 * on top draws what only matters when zoomed in: selection rings, vertex
 * ids and arrowheads.
 *
 *   const view = new GraphView(container, graph);
 *   view.onNodeClick = (v, ev) => { ... };
 *   view.showTraversal(graph.bfs(v));
 *
 * Interaction: drag a vertex to move it (with the layout running it stays
 * pinned, double-click releases it), drag the background to pan, wheel to
 * zoom.
 */
import { LAYOUT_DEFAULTS, forceTick } from "./layout.js";

const DEFAULTS = {
  ...LAYOUT_DEFAULTS,
  nodeRadius: 5,        // world units: vertices grow when zooming in
  minNodePx: 5,         // smallest vertex diameter on screen...
  maxNodePx: 14,        // ...and the largest, however far one zooms in
  dotsBelowN: 20000,    // ...enforced only below this size, larger graphs show just edges when zoomed out
  maxLayoutN: 50000,    // no force layout above this: too slow, load coordinates instead
  overlayMaxN: 20000,   // labels and arrowheads only below this size
  labelZoom: 1.6,       // draw vertex ids when zoomed in past this scale
  warmupMs: 150,        // layout ticks run before the first paint of a new graph
  hideIsolated: false,
};

/* Categorical palette for components, readable on both themes. */
const PALETTE = [
  "#4e79a7", "#f28e2b", "#e15759", "#76b7b2", "#59a14f",
  "#edc948", "#b07aa1", "#ff9da7", "#9c755f", "#bab0ac",
].map((h) => bytes(rgb(h)));

function rgb(hex, a = 1) {
  const n = parseInt(hex.slice(1), 16);
  return [(n >> 16) / 255, ((n >> 8) & 255) / 255, (n & 255) / 255, a];
}

/* Per-vertex colors travel as 4 bytes (normalized on the GPU), not 4 floats. */
function bytes(c) {
  return c.map((k) => Math.round(k * 255));
}

function hsl(h, s, l) {
  const k = (n) => (n + h / 30) % 12;
  const f = (n) => l - s * Math.min(l, 1 - l) * Math.max(-1, Math.min(k(n) - 3, 9 - k(n), 1));
  return [f(0), f(8), f(4), 1];
}

function themeColors() {
  const dark = matchMedia("(prefers-color-scheme: dark)").matches;
  const c = dark
    ? { bg: "#16181d", edge: rgb("#c8cdd7", 0.22), node: "#9aa4b2", text: "#e6e8eb",
        tree: "#ff7a59", source: "#ffb020", unreached: "#3a3f48", select: "#5cc8ff" }
    : { bg: "#fbfbfc", edge: rgb("#3c4048", 0.22), node: "#5b6472", text: "#1d2128",
        tree: "#e2462b", source: "#e8920c", unreached: "#d5d8dd", select: "#0a84ff" };
  return { ...c, dark, bgRGB: rgb(c.bg), treeRGB: rgb(c.tree),
           nodeB: bytes(rgb(c.node)), sourceB: bytes(rgb(c.source)), unreachedB: bytes(rgb(c.unreached)) };
}

/* ---- WebGL plumbing ----------------------------------------------------- */

/* Positions are world coordinates; u_scale/u_shift map them to CSS pixels
 * (the camera), u_view converts CSS pixels to clip space. */
const VS_HEAD = `
  attribute vec2 a_pos;
  uniform vec2 u_scale, u_shift, u_view;
  vec4 project() {
    vec2 q = (a_pos * u_scale + u_shift) / u_view * 2.0 - 1.0;
    return vec4(q.x, -q.y, 0.0, 1.0);
  }`;

const LINE_VS = `${VS_HEAD}
  void main() { gl_Position = project(); }`;
const LINE_FS = `
  precision mediump float;
  uniform vec4 u_color;
  void main() { gl_FragColor = u_color; }`;

const POINT_VS = `${VS_HEAD}
  attribute vec4 a_color;
  uniform float u_size;
  varying vec4 v_color;
  varying float v_size;
  void main() { gl_Position = project(); gl_PointSize = u_size; v_color = a_color; v_size = u_size; }`;
/* Round dots with a thin ring in the background color. */
const POINT_FS = `
  precision mediump float;
  uniform vec4 u_ring;
  varying vec4 v_color;
  varying float v_size;
  void main() {
    if (v_color.a == 0.0) discard;
    float r = length(gl_PointCoord - 0.5) * 2.0;
    if (r > 1.0) discard;
    gl_FragColor = (v_size > 6.0 && r > 1.0 - 2.4 / v_size) ? u_ring : v_color;
  }`;

function program(gl, vs, fs) {
  const p = gl.createProgram();
  for (const [type, src] of [[gl.VERTEX_SHADER, vs], [gl.FRAGMENT_SHADER, fs]]) {
    const sh = gl.createShader(type);
    gl.shaderSource(sh, src);
    gl.compileShader(sh);
    if (!gl.getShaderParameter(sh, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(sh));
    gl.attachShader(p, sh);
  }
  gl.linkProgram(p);
  if (!gl.getProgramParameter(p, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(p));
  const loc = {};
  for (const name of ["a_pos", "a_color"]) loc[name] = gl.getAttribLocation(p, name);
  for (const name of ["u_scale", "u_shift", "u_view", "u_color", "u_size", "u_ring"])
    loc[name] = gl.getUniformLocation(p, name);
  return { p, loc };
}

/* A GPU buffer together with its CPU-side copy. */
class Buf {
  constructor(gl) { this.gl = gl; this.id = gl.createBuffer(); this.data = new Float32Array(0); }
  upload(data = this.data) {
    this.data = data;
    this.gl.bindBuffer(this.gl.ARRAY_BUFFER, this.id);
    this.gl.bufferData(this.gl.ARRAY_BUFFER, data, this.gl.DYNAMIC_DRAW);
  }
  /* Re-sends floats [from, from + count) after an in-place edit of data. */
  patch(from, count) {
    this.gl.bindBuffer(this.gl.ARRAY_BUFFER, this.id);
    this.gl.bufferSubData(this.gl.ARRAY_BUFFER, from * 4, this.data.subarray(from, from + count));
  }
}

/* ---- the view ------------------------------------------------------------ */

export class GraphView {
  constructor(container, graph, options = {}) {
    this.opt = { ...DEFAULTS, ...options };
    this.colors = themeColors();
    this.scale = 1;
    this.tx = 0;
    this.ty = 0;
    this.alpha = 0;
    this.selected = new Set();
    this.onNodeClick = null;  // (v, PointerEvent) => void
    this.onEdgeClick = null;  // (u, v, PointerEvent) => void
    this.onBackgroundClick = null;
    this.highlight = null;
    this._anim = null;
    this._drag = null;
    this._dirty = { positions: true, colors: true, highlight: true, draw: true, grid: true };

    container.style.position ||= "relative";
    const mk = () => {
      const c = document.createElement("canvas");
      c.style.cssText = "position:absolute;inset:0;width:100%;height:100%;display:block";
      container.appendChild(c);
      return c;
    };
    this.glCanvas = mk();
    this.canvas = mk(); // overlay: receives the pointer events
    this.ctx = this.canvas.getContext("2d");
    const gl = this.glCanvas.getContext("webgl", { antialias: true, premultipliedAlpha: false });
    if (!gl) throw new Error("WebGL not available");
    this.gl = gl;
    this.lineProg = program(gl, LINE_VS, LINE_FS);
    this.pointProg = program(gl, POINT_VS, POINT_FS);
    this.maxPointPx = gl.getParameter(gl.ALIASED_POINT_SIZE_RANGE)[1];
    this.buf = {
      edges: new Buf(gl), nodes: new Buf(gl), nodeColors: new Buf(gl),
      tree: new Buf(gl), reached: new Buf(gl), reachedColors: new Buf(gl),
    };

    matchMedia("(prefers-color-scheme: dark)").addEventListener("change", () => {
      this.colors = themeColors();
      this._dirty.colors = this._dirty.highlight = this._dirty.draw = true;
    });
    new ResizeObserver(() => this._resize()).observe(container);
    this._resize();
    this._bindPointer();
    this.setGraph(graph, { reset: true });
    this._frame = this._frame.bind(this);
    requestAnimationFrame(this._frame);
  }

  /* ---- data ----------------------------------------------------------- */

  /**
   * (Re)reads topology from the wasm graph. Existing vertices keep their
   * positions, new ones are placed next to a neighbor. With reset (a new
   * graph rather than an edit) positions and coordinates are discarded.
   */
  setGraph(graph, { reset = false } = {}) {
    this.graph = graph;
    this.directed = graph.directed;
    const n = graph.n;
    if (reset) {
      this.x = null;
      this.hasCoords = false;
      this.selected.clear();
    }
    const old = this.x ? this.x.length : 0;
    const grow = (a, Ctor) => {
      const b = new Ctor(n);
      if (a) b.set(a.subarray(0, Math.min(old, n)));
      return b;
    };
    this.x = grow(this.x, Float64Array);
    this.y = grow(this.y, Float64Array);
    /* Velocities only matter to the force layout: skip them (2 x 8 bytes
     * per vertex) on graphs too large for it. */
    const nv = n <= this.opt.maxLayoutN ? n : 0;
    this.vx = nv ? grow(this.vx, Float64Array) : new Float64Array(0);
    this.vy = nv ? grow(this.vy, Float64Array) : new Float64Array(0);
    this.pinned = grow(this.pinned, Uint8Array);
    this._readEdges();

    const R = Math.sqrt(n) * this.opt.linkDistance * 0.6;
    for (let v = old; v < n; v++) {
      const a = v * 2.399963; // golden angle: spreads new vertices evenly
      const r = R * Math.sqrt((v + 0.5) / n);
      this.x[v] = r * Math.cos(a);
      this.y[v] = r * Math.sin(a);
    }
    if (old > 0) {
      /* Place each new vertex near an existing neighbor, if it has one. */
      for (let i = 0; i < this.eu.length; i++) {
        const u = this.eu[i], v = this.ev[i];
        if (v >= old && u < old) { this.x[v] = this.x[u] + 10; this.y[v] = this.y[u] + 10; }
        if (u >= old && v < old) { this.x[u] = this.x[v] + 10; this.y[u] = this.y[v] + 10; }
      }
    }
    /* hideIsolated only applies to the graph as first loaded (e.g. vertex 0
     * of a file numbered from 1): vertices added later stay visible. */
    if (old === 0) this._loadedN = n;
    for (const s of this.selected) if (s >= n) this.selected.delete(s);
    this.highlight = null;
    this._anim = null;

    this.layoutOn = !this.hasCoords && n <= this.opt.maxLayoutN;
    this.layoutTooBig = !this.hasCoords && n > this.opt.maxLayoutN;
    if (this.layoutOn) {
      this.reheat(old === 0 ? 1 : 0.3);
      if (old === 0) this.warmup();
    }
    this._dirty = { positions: true, colors: true, highlight: true, draw: true, grid: true };
    if (old === 0) this.fit();
  }

  /*
   * Edge list to draw. In a directed graph the two arcs u->v and v->u are
   * drawn as one segment (road networks list every street both ways);
   * bidir marks those, for the arrowheads. The C side does the pairing
   * (wg_draw_edges). Also builds the incidence lists
   * (CSR) used when dragging a vertex and when picking edges.
   */
  _readEdges() {
    const g = this.graph, n = g.n, e = g.drawEdges();
    this.eu = e.u;
    this.ev = e.v;
    this.bidir = g.directed ? e.bidir : null;

    const m = this.eu.length;
    this.degree = new Int32Array(n);
    for (let i = 0; i < m; i++) { this.degree[this.eu[i]]++; this.degree[this.ev[i]]++; }
    this.incStart = new Int32Array(n + 1);
    for (let v = 0; v < n; v++) this.incStart[v + 1] = this.incStart[v] + this.degree[v];
    this.incEdge = new Int32Array(2 * m);
    const fill = this.incStart.slice(0, n);
    for (let i = 0; i < m; i++) {
      this.incEdge[fill[this.eu[i]]++] = i;
      this.incEdge[fill[this.ev[i]]++] = i;
    }
  }

  /**
   * Places vertices at fixed coordinates (y grows downwards) and turns the
   * force layout off. xs/ys have one entry per vertex; NaN marks a vertex
   * without coordinates, which goes to the centroid. Coordinates are
   * rescaled so the median edge is opt.linkDistance long: the same zoom
   * levels then work for every graph.
   */
  setPositions(xs, ys) {
    const n = this.x.length;
    let cx = 0, cy = 0, k = 0;
    for (let v = 0; v < n; v++) if (isFinite(xs[v]) && isFinite(ys[v])) { cx += xs[v]; cy += ys[v]; k++; }
    if (!k) throw new Error("no vertex has coordinates");
    cx /= k; cy /= k;

    const step = Math.max(1, Math.floor(this.eu.length / 20000)), lens = [];
    for (let i = 0; i < this.eu.length; i += step) {
      const u = this.eu[i], v = this.ev[i];
      const d = Math.hypot(xs[u] - xs[v], ys[u] - ys[v]);
      if (d > 0 && isFinite(d)) lens.push(d);
    }
    lens.sort((a, b) => a - b);
    const f = lens.length ? this.opt.linkDistance / lens[lens.length >> 1] : 1;

    for (let v = 0; v < n; v++) {
      const ok = isFinite(xs[v]) && isFinite(ys[v]);
      this.x[v] = ok ? (xs[v] - cx) * f : 0;
      this.y[v] = ok ? (ys[v] - cy) * f : 0;
    }
    this.vx.fill(0);
    this.vy.fill(0);
    this.pinned.fill(0);
    this.hasCoords = true;
    this.layoutOn = false;
    this.layoutTooBig = false;
    this.alpha = 0;
    this._dirty.positions = this._dirty.highlight = this._dirty.grid = this._dirty.draw = true;
    this.fit();
  }

  /* ---- highlighting --------------------------------------------------- */

  /**
   * Colors a traversal result ({ order, parent, dist }): reached vertices
   * get a color by distance, tree edges (parent[v] -> v) are drawn in the
   * accent color. With animate they appear in visit order over ~seconds.
   */
  showTraversal(t, { animate = true, seconds = 3 } = {}) {
    let maxd = 1;
    for (const v of t.order) if (t.dist[v] > maxd) maxd = t.dist[v];
    this.highlight = { kind: "traversal", t, maxd, shown: animate ? 0 : t.order.length };
    this._anim = animate ? { start: performance.now(), perSecond: Math.max(60, t.order.length / seconds) } : null;
    this._dirty.colors = this._dirty.highlight = this._dirty.draw = true;
  }

  showComponents(c) {
    const size = new Int32Array(c.count);
    for (const k of c.comp) size[k]++;
    /* Palette goes to the largest components first, singletons stay neutral. */
    const rank = Array.from(size.keys()).sort((a, b) => size[b] - size[a]);
    const color = new Array(c.count);
    rank.forEach((k, i) => { color[k] = size[k] > 1 ? PALETTE[i % PALETTE.length] : null; });
    this.highlight = { kind: "components", comp: c.comp, color };
    this._anim = null;
    this._dirty.colors = this._dirty.highlight = this._dirty.draw = true;
  }

  clearHighlight() {
    this.highlight = null;
    this._anim = null;
    this._dirty.colors = this._dirty.highlight = this._dirty.draw = true;
  }

  /* ---- camera --------------------------------------------------------- */

  fit(padding = 40) {
    const n = this.x.length;
    let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity;
    for (let v = 0; v < n; v++) {
      if (this._hidden(v)) continue;
      x0 = Math.min(x0, this.x[v]); x1 = Math.max(x1, this.x[v]);
      y0 = Math.min(y0, this.y[v]); y1 = Math.max(y1, this.y[v]);
    }
    if (!isFinite(x0)) return;
    const w = this.cssW, h = this.cssH;
    this.scale = Math.min((w - 2 * padding) / (x1 - x0 || 1), (h - 2 * padding) / (y1 - y0 || 1), 4);
    this.tx = w / 2 - this.scale * (x0 + x1) / 2;
    this.ty = h / 2 - this.scale * (y0 + y1) / 2;
    this._dirty.draw = true;
  }

  reheat(alpha = 0.5) {
    if (this.layoutOn) this.alpha = Math.max(this.alpha, alpha);
  }

  /** Runs layout ticks synchronously for up to `ms` milliseconds. */
  warmup(ms = this.opt.warmupMs) {
    const end = performance.now() + ms;
    while (this.layoutOn && this.alpha > 0.005 && performance.now() < end) this._tick();
  }

  toWorld(sx, sy) {
    return [(sx - this.tx) / this.scale, (sy - this.ty) / this.scale];
  }

  /* ---- picking -------------------------------------------------------- */

  /*
   * Spatial hash over vertex positions, rebuilt lazily when they change.
   * Cells are hashed into a power-of-two table and stored as CSR (bucket
   * offsets + vertex ids): two flat arrays whatever the graph size. Cells
   * sharing a bucket only add candidates, the distance test filters them.
   */
  _grid() {
    if (!this._dirty.grid) return;
    const { x, y } = this, n = x.length;
    const cell = (this._cell = this.opt.linkDistance * 2);
    let T = 1024;
    while (T < n) T *= 2;
    const mask = (this._mask = T - 1);
    const start = (this._bucketStart = new Int32Array(T + 1));
    const key = new Int32Array(n);
    for (let v = 0; v < n; v++) {
      key[v] = this._hidden(v) ? -1 : bucket(Math.floor(x[v] / cell), Math.floor(y[v] / cell), mask);
      if (key[v] >= 0) start[key[v] + 1]++;
    }
    for (let b = 0; b < T; b++) start[b + 1] += start[b];
    const fill = start.slice(0, T), verts = (this._bucketVerts = new Int32Array(start[T]));
    for (let v = 0; v < n; v++) if (key[v] >= 0) verts[fill[key[v]]++] = v;
    this._dirty.grid = false;
  }

  /* Calls fn(v, d2) for every vertex within r (world units) of (wx, wy). */
  _near(wx, wy, r, fn) {
    this._grid();
    const cell = this._cell, start = this._bucketStart, verts = this._bucketVerts;
    const span = Math.ceil(r / cell);
    if (span > 60) return; // zoomed far out: too coarse to pick anything
    const cx = Math.floor(wx / cell), cy = Math.floor(wy / cell);
    for (let gx = cx - span; gx <= cx + span; gx++)
      for (let gy = cy - span; gy <= cy + span; gy++) {
        const b = bucket(gx, gy, this._mask);
        for (let k = start[b]; k < start[b + 1]; k++) {
          const v = verts[k], dx = this.x[v] - wx, dy = this.y[v] - wy, d2 = dx * dx + dy * dy;
          if (d2 <= r * r) fn(v, d2);
        }
      }
  }

  /** Closest vertex under a screen point, or -1. */
  nodeAt(sx, sy) {
    const [wx, wy] = this.toWorld(sx, sy);
    let best = -1, bestD = Infinity;
    this._near(wx, wy, (this._nodePx() / 2 + 4) / this.scale, (v, d) => {
      if (d < bestD) { best = v; bestD = d; }
    });
    return best;
  }

  /** Index (into eu/ev) of the edge under a screen point, or -1. Only
   * edges with an endpoint reasonably close by are considered. */
  edgeAt(sx, sy) {
    const [wx, wy] = this.toWorld(sx, sy);
    const tol = 5 / this.scale;
    let best = -1, bestD = tol * tol;
    this._near(wx, wy, tol + 4 * this.opt.linkDistance, (v) => {
      for (let k = this.incStart[v]; k < this.incStart[v + 1]; k++) {
        const i = this.incEdge[k];
        const ax = this.x[this.eu[i]], ay = this.y[this.eu[i]];
        const dx = this.x[this.ev[i]] - ax, dy = this.y[this.ev[i]] - ay, len = dx * dx + dy * dy || 1;
        const s = Math.max(0, Math.min(1, ((wx - ax) * dx + (wy - ay) * dy) / len));
        const px = ax + s * dx - wx, py = ay + s * dy - wy, d = px * px + py * py;
        if (d <= bestD) { best = i; bestD = d; }
      }
    });
    return best;
  }

  _hidden(v) {
    return this.opt.hideIsolated && this.degree[v] === 0 && v < this._loadedN;
  }

  /* Vertex diameter on screen; 0 means "do not draw plain vertices". */
  _nodePx() {
    const o = this.opt, cap = Math.min(o.maxNodePx, this.maxPointPx);
    const px = o.nodeRadius * 2 * this.scale;
    if (this.x.length < o.dotsBelowN) return Math.min(Math.max(px, o.minNodePx), cap);
    return px < 2 ? 0 : Math.min(px, cap);
  }

  /* ---- layout --------------------------------------------------------- */

  _tick() {
    forceTick(this, this.opt);
    this._dirty.positions = true;
  }

  hidden(v) { // the hook forceTick expects
    return this._hidden(v);
  }

  /* ---- buffers -------------------------------------------------------- */

  _fillPositions() {
    const { x, y, eu, ev } = this, n = x.length, m = eu.length;
    const nodes = this.buf.nodes.data.length === 2 * n ? this.buf.nodes.data : new Float32Array(2 * n);
    for (let v = 0; v < n; v++) { nodes[2 * v] = x[v]; nodes[2 * v + 1] = y[v]; }
    const edges = this.buf.edges.data.length === 4 * m ? this.buf.edges.data : new Float32Array(4 * m);
    for (let i = 0; i < m; i++) {
      const u = eu[i], v = ev[i];
      edges[4 * i] = x[u]; edges[4 * i + 1] = y[u];
      edges[4 * i + 2] = x[v]; edges[4 * i + 3] = y[v];
    }
    this.buf.nodes.upload(nodes);
    this.buf.edges.upload(edges);
  }

  /* After moving one vertex without the layout: patch it and its edges. */
  _patchVertex(v) {
    const { x, y } = this;
    const nodes = this.buf.nodes.data, edges = this.buf.edges.data;
    nodes[2 * v] = x[v]; nodes[2 * v + 1] = y[v];
    this.buf.nodes.patch(2 * v, 2);
    for (let k = this.incStart[v]; k < this.incStart[v + 1]; k++) {
      const i = this.incEdge[k], off = this.eu[i] === v ? 0 : 2;
      edges[4 * i + off] = x[v]; edges[4 * i + off + 1] = y[v];
      this.buf.edges.patch(4 * i + off, 2);
    }
    this._dirty.grid = this._dirty.draw = true;
    if (this.highlight?.kind === "traversal") this._dirty.highlight = true;
  }

  _fillColors() {
    const n = this.x.length, h = this.highlight, c = this.colors;
    const col = new Uint8Array(4 * n), none = [0, 0, 0, 0];
    for (let v = 0; v < n; v++) {
      let rgba = c.nodeB;
      if (this._hidden(v)) rgba = none;
      else if (h?.kind === "components") rgba = h.color[h.comp[v]] ?? c.unreachedB;
      else if (h?.kind === "traversal") rgba = c.unreachedB;
      col.set(rgba, 4 * v);
    }
    this.buf.nodeColors.upload(col);
  }

  /* Traversal buffers are in visit order, so drawing the first `shown`
   * entries is all the animation needs. */
  _fillTraversal() {
    const h = this.highlight;
    if (h?.kind !== "traversal") return;
    const { order, parent, dist } = h.t, k = order.length, { x, y } = this;
    const tree = new Float32Array(4 * k), pos = new Float32Array(2 * k), col = new Uint8Array(4 * k);
    /* Hue runs from warm (near the source) to cool (far): one color per distance. */
    const ramp = [];
    for (let d = 0; d <= h.maxd; d++)
      ramp.push(d === 0 ? this.colors.sourceB : bytes(hsl(20 + 200 * (d / h.maxd), 0.7, this.colors.dark ? 0.62 : 0.48)));
    for (let i = 0; i < k; i++) {
      const v = order[i], p = parent[v] < 0 ? v : parent[v];
      tree[4 * i] = x[p]; tree[4 * i + 1] = y[p]; tree[4 * i + 2] = x[v]; tree[4 * i + 3] = y[v];
      pos[2 * i] = x[v]; pos[2 * i + 1] = y[v];
      col.set(ramp[dist[v]], 4 * i);
    }
    this.buf.tree.upload(tree);
    this.buf.reached.upload(pos);
    this.buf.reachedColors.upload(col);
  }

  /* ---- rendering ------------------------------------------------------ */

  _resize() {
    const dpr = window.devicePixelRatio || 1;
    const r = this.canvas.getBoundingClientRect();
    this.cssW = r.width;
    this.cssH = r.height;
    for (const c of [this.canvas, this.glCanvas]) {
      c.width = Math.round(r.width * dpr);
      c.height = Math.round(r.height * dpr);
    }
    this.dpr = dpr;
    if (this._dirty) this._dirty.draw = true;
  }

  _frame(now) {
    if (this.layoutOn && (this.alpha > 0.005 || this._drag?.node >= 0)) this._tick();
    const h = this.highlight;
    if (this._anim && h?.kind === "traversal") {
      h.shown = Math.min(h.t.order.length, Math.floor(((now - this._anim.start) / 1000) * this._anim.perSecond));
      if (h.shown >= h.t.order.length) this._anim = null;
      this._dirty.draw = true;
    }
    const d = this._dirty;
    if (d.positions) { this._fillPositions(); d.grid = d.highlight = d.draw = true; }
    if (d.colors) { this._fillColors(); d.draw = true; }
    if (d.highlight) { this._fillTraversal(); d.draw = true; }
    d.positions = d.colors = d.highlight = false;
    if (d.draw) { this._draw(); d.draw = false; }
    requestAnimationFrame(this._frame);
  }

  _draw() {
    const gl = this.gl, c = this.colors, h = this.highlight;
    gl.viewport(0, 0, this.glCanvas.width, this.glCanvas.height);
    gl.clearColor(...c.bgRGB);
    gl.clear(gl.COLOR_BUFFER_BIT);
    gl.enable(gl.BLEND);
    gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);

    const use = ({ p, loc }) => {
      gl.useProgram(p);
      gl.uniform2f(loc.u_scale, this.scale, this.scale);
      gl.uniform2f(loc.u_shift, this.tx, this.ty);
      gl.uniform2f(loc.u_view, this.cssW, this.cssH);
      return loc;
    };
    const attrib = (loc, buf, size) => {
      gl.bindBuffer(gl.ARRAY_BUFFER, buf.id);
      gl.enableVertexAttribArray(loc);
      if (buf.data instanceof Uint8Array) gl.vertexAttribPointer(loc, size, gl.UNSIGNED_BYTE, true, 0, 0);
      else gl.vertexAttribPointer(loc, size, gl.FLOAT, false, 0, 0);
    };

    /* Edges, then the traversal tree on top. */
    let loc = use(this.lineProg);
    attrib(loc.a_pos, this.buf.edges, 2);
    gl.uniform4fv(loc.u_color, c.edge);
    gl.drawArrays(gl.LINES, 0, 2 * this.eu.length);
    if (h?.kind === "traversal" && h.shown) {
      attrib(loc.a_pos, this.buf.tree, 2);
      gl.uniform4fv(loc.u_color, c.treeRGB);
      gl.drawArrays(gl.LINES, 0, 2 * h.shown);
    }
    gl.disableVertexAttribArray(loc.a_pos);

    /* Vertices, then the reached ones of the traversal on top. */
    loc = use(this.pointProg);
    gl.uniform4fv(loc.u_ring, c.bgRGB);
    const size = this._nodePx() * this.dpr;
    if (size > 0) {
      gl.uniform1f(loc.u_size, size);
      attrib(loc.a_pos, this.buf.nodes, 2);
      attrib(loc.a_color, this.buf.nodeColors, 4);
      gl.drawArrays(gl.POINTS, 0, this.x.length);
    }
    if (h?.kind === "traversal" && h.shown) {
      gl.uniform1f(loc.u_size, Math.max(size, 3 * this.dpr));
      attrib(loc.a_pos, this.buf.reached, 2);
      attrib(loc.a_color, this.buf.reachedColors, 4);
      gl.drawArrays(gl.POINTS, 0, h.shown);
    }
    gl.disableVertexAttribArray(loc.a_pos);
    gl.disableVertexAttribArray(loc.a_color);

    this._drawOverlay(size / this.dpr);
  }

  /* Selection rings always; ids and arrowheads only on smaller graphs. */
  _drawOverlay(nodePx) {
    const { ctx, colors: c, opt: o, x, y } = this;
    ctx.setTransform(this.dpr, 0, 0, this.dpr, 0, 0);
    ctx.clearRect(0, 0, this.cssW, this.cssH);
    const sx = (v) => x[v] * this.scale + this.tx, sy = (v) => y[v] * this.scale + this.ty;
    const r = Math.max(nodePx, 4) / 2;

    if (this.directed && this.x.length <= o.overlayMaxN && this.scale > 0.6) {
      ctx.fillStyle = `rgba(${c.edge.slice(0, 3).map((k) => k * 255).join(",")},0.6)`;
      ctx.beginPath();
      for (let i = 0; i < this.eu.length; i++) {
        this._arrow(sx(this.eu[i]), sy(this.eu[i]), sx(this.ev[i]), sy(this.ev[i]), r);
        if (this.bidir?.[i]) this._arrow(sx(this.ev[i]), sy(this.ev[i]), sx(this.eu[i]), sy(this.eu[i]), r);
      }
      ctx.fill();
    }

    if (this.x.length <= o.overlayMaxN && this.scale >= o.labelZoom) {
      ctx.fillStyle = c.text;
      ctx.font = "11px system-ui, sans-serif";
      ctx.textBaseline = "middle";
      for (let v = 0; v < x.length; v++) {
        if (this._hidden(v)) continue;
        const px = sx(v), py = sy(v);
        if (px < -20 || py < -20 || px > this.cssW + 20 || py > this.cssH + 20) continue;
        ctx.fillText(String(v), px + r + 2, py);
      }
    }

    ctx.lineWidth = 2.5;
    ctx.strokeStyle = c.select;
    for (const v of this.selected) {
      ctx.beginPath();
      ctx.arc(sx(v), sy(v), r + 4, 0, 2 * Math.PI);
      ctx.stroke();
    }
  }

  _arrow(ax, ay, bx, by, r) {
    const dx = bx - ax, dy = by - ay, d = Math.hypot(dx, dy);
    if (d < 2 * r + 8) return;
    const ux = dx / d, uy = dy / d, tx = bx - ux * r, ty = by - uy * r;
    this.ctx.moveTo(tx, ty);
    this.ctx.lineTo(tx - ux * 7 - uy * 3, ty - uy * 7 + ux * 3);
    this.ctx.lineTo(tx - ux * 7 + uy * 3, ty - uy * 7 - ux * 3);
    this.ctx.closePath();
  }

  /* ---- interaction ---------------------------------------------------- */

  _bindPointer() {
    const cv = this.canvas;
    cv.style.touchAction = "none";
    const pos = (ev) => {
      const r = cv.getBoundingClientRect();
      return [ev.clientX - r.left, ev.clientY - r.top];
    };

    cv.addEventListener("pointerdown", (ev) => {
      const [sx, sy] = pos(ev);
      const node = this.nodeAt(sx, sy);
      this._drag = { node, sx, sy, tx: this.tx, ty: this.ty, moved: false };
      cv.setPointerCapture(ev.pointerId);
      if (node >= 0 && this.layoutOn) {
        this.pinned[node] = 1;
        this.reheat(0.3);
      }
    });

    cv.addEventListener("pointermove", (ev) => {
      const [sx, sy] = pos(ev);
      const d = this._drag;
      if (!d) {
        if (ev.buttons === 0) cv.style.cursor = this.nodeAt(sx, sy) >= 0 ? "grab" : "default";
        return;
      }
      if (Math.abs(sx - d.sx) + Math.abs(sy - d.sy) > 3) d.moved = true;
      if (d.node >= 0) {
        const [wx, wy] = this.toWorld(sx, sy);
        this.x[d.node] = wx;
        this.y[d.node] = wy;
        if (this.layoutOn) this.reheat(0.3);
        else this._patchVertex(d.node);
        cv.style.cursor = "grabbing";
      } else {
        this.tx = d.tx + sx - d.sx;
        this.ty = d.ty + sy - d.sy;
      }
      this._dirty.draw = true;
    });

    const end = (ev) => {
      const d = this._drag;
      this._drag = null;
      if (!d || d.moved) return;
      const [sx, sy] = pos(ev);
      if (d.node >= 0) {
        this.onNodeClick?.(d.node, ev);
        return;
      }
      const e = this.edgeAt(sx, sy);
      if (e >= 0) this.onEdgeClick?.(this.eu[e], this.ev[e], ev);
      else this.onBackgroundClick?.(ev);
    };
    cv.addEventListener("pointerup", end);
    cv.addEventListener("pointercancel", () => { this._drag = null; });

    cv.addEventListener("dblclick", (ev) => {
      const v = this.nodeAt(...pos(ev));
      if (v >= 0 && this.layoutOn) { this.pinned[v] = 0; this.reheat(0.3); }
    });

    cv.addEventListener("wheel", (ev) => {
      ev.preventDefault();
      const [sx, sy] = pos(ev);
      const k = Math.exp(-ev.deltaY * 0.0015);
      const s = Math.min(50, Math.max(0.0005, this.scale * k));
      this.tx = sx - ((sx - this.tx) * s) / this.scale;
      this.ty = sy - ((sy - this.ty) * s) / this.scale;
      this.scale = s;
      this._dirty.draw = true;
    }, { passive: false });
  }
}

/* Bucket of grid cell (cx, cy) in a table of mask + 1 entries. */
function bucket(cx, cy, mask) {
  return (Math.imul(cx, 73856093) ^ Math.imul(cy, 19349663)) & mask;
}
