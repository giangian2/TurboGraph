/*
 * JavaScript wrapper around the Emscripten build of the graph library
 * (bin/wasm/graphs.mjs, see `make wasm` and wasm/wasm_api.c).
 *
 *   import createGraphsModule from "../bin/wasm/graphs.mjs";
 *   import { TurboGraph, Repr } from "./turbograph.js";
 *
 *   const tg = await TurboGraph.load(createGraphsModule);
 *   const g  = tg.fromDot(text, { repr: Repr.LIST });
 *   const t  = g.bfs(0);           // { order, parent, dist }
 *   g.free();
 *
 * Graph objects own wasm heap memory: call free() when done (a
 * FinalizationRegistry is a safety net, not a guarantee).
 */

/* Must match GraphRepr in include/Graph.h. MATRIX is listed for
 * completeness but graph_create currently refuses it. */
export const Repr = Object.freeze({ MATRIX: 0, LIST: 1, STAR: 2 });

/* Must match ERROR_CODES in include/Graph.h. */
const ERRORS = {
  [-1]: "invalid argument / vertex out of range",
  [-2]: "graph is immutable (star representation)",
  [-3]: "out of memory",
};

export class GraphError extends Error {
  constructor(code, op) {
    super(`${op}: ${ERRORS[code] ?? `error ${code}`}`);
    this.code = code;
  }
}

function check(rc, op) {
  if (rc < 0) throw new GraphError(rc, op);
  return rc;
}

export class TurboGraph {
  /** @param {(opts?: object) => Promise<object>} factory default export of graphs.mjs */
  static async load(factory, moduleOptions = {}) {
    return new TurboGraph(await factory(moduleOptions));
  }

  constructor(mod) {
    this.mod = mod;
    this._registry = new FinalizationRegistry((ptr) => mod._wg_free(ptr));
    this._tmp = 0;
  }

  /** Empty mutable graph (list representation). */
  create(n, directed = false) {
    return this._wrap(this.mod._wg_create(n, directed ? 1 : 0, Repr.LIST), "create");
  }

  /** @param {Array<[number, number, number?]>} edges [u, v, w=1] triples */
  fromEdges(n, directed, edges, { repr = Repr.LIST } = {}) {
    const m = edges.length;
    const M = this.mod;
    const uv = M._malloc(Math.max(1, m) * 8);
    const w = M._malloc(Math.max(1, m) * 8);
    try {
      const U = M.HEAP32.subarray(uv >> 2, (uv >> 2) + 2 * m);
      const W = M.HEAPF64.subarray(w >> 3, (w >> 3) + m);
      for (let k = 0; k < m; k++) {
        U[2 * k] = edges[k][0];
        U[2 * k + 1] = edges[k][1];
        W[k] = edges[k][2] ?? 1;
      }
      return this._wrap(M._wg_from_edges(n, directed ? 1 : 0, uv, w, m, repr), "fromEdges");
    } finally {
      M._free(uv);
      M._free(w);
    }
  }

  /** Parses DOT text (string or bytes) with graph_import_dot, through the virtual FS. */
  fromDot(text, { repr = Repr.LIST } = {}) {
    const path = `/tmp/import-${this._tmp++}.dot`;
    this.mod.FS.writeFile(path, text);
    return this._importFile(path, repr);
  }

  /**
   * Same as fromDot, from a ReadableStream of bytes (fetch(...).body,
   * File.stream()). The file is copied into the virtual FS chunk by chunk,
   * so inputs larger than the maximum JS string length still work.
   */
  async fromDotStream(stream, { repr = Repr.LIST } = {}) {
    const FS = this.mod.FS;
    const path = `/tmp/import-${this._tmp++}.dot`;
    const out = FS.open(path, "w");
    try {
      for (const reader = stream.getReader(); ;) {
        const { done, value } = await reader.read();
        if (done) break;
        FS.write(out, value, 0, value.length);
      }
    } finally {
      FS.close(out);
    }
    return this._importFile(path, repr);
  }

  _importFile(path, repr) {
    const M = this.mod;
    const cpath = M.stringToNewUTF8(path);
    try {
      return this._wrap(M._wg_import_dot(cpath, repr), "fromDot");
    } finally {
      M._free(cpath);
      M.FS.unlink(path);
    }
  }

  _wrap(ptr, op) {
    if (!ptr) throw new GraphError(-1, op);
    const g = new Graph(this, ptr);
    this._registry.register(g, ptr, g);
    return g;
  }

  /* Allocates `count` ints on the wasm heap, runs fn(...ptrs), copies the
   * arrays out and frees them. Views are taken after fn: the heap may grow. */
  _withInts(count, nArrays, fn) {
    const M = this.mod;
    const ptrs = Array.from({ length: nArrays }, () => M._malloc(Math.max(1, count) * 4));
    try {
      const rc = fn(...ptrs);
      const out = ptrs.map((p) => M.HEAP32.slice(p >> 2, (p >> 2) + count));
      return { rc, out };
    } finally {
      ptrs.forEach((p) => M._free(p));
    }
  }
}

export class Graph {
  constructor(tg, ptr) {
    this.tg = tg;
    this.ptr = ptr;
  }

  get _m() {
    if (!this.ptr) throw new Error("graph already freed");
    return this.tg.mod;
  }

  get n() { return this._m._wg_n(this.ptr); }
  get m() { return this._m._wg_m(this.ptr); }
  get directed() { return this._m._wg_directed(this.ptr) === 1; }
  get repr() { return this._m._wg_repr(this.ptr); }
  get mutable() { return this.repr !== Repr.STAR; }

  addEdge(u, v, w = 1) { check(this._m._wg_add_edge(this.ptr, u, v, w), "addEdge"); }
  removeEdge(u, v) { check(this._m._wg_remove_edge(this.ptr, u, v), "removeEdge"); }
  hasEdge(u, v) { return this._m._wg_has_edge(this.ptr, u, v) === 1; }
  weight(u, v) { return this._m._wg_get_weight(this.ptr, u, v); }

  /** @returns {{ u: Int32Array, v: Int32Array, w: Float64Array }} one entry per logical edge */
  edges() {
    const M = this._m;
    const m = this.m;
    const uv = M._malloc(Math.max(1, m) * 8);
    const w = M._malloc(Math.max(1, m) * 8);
    try {
      const k = check(M._wg_edges(this.ptr, uv, w, m), "edges");
      const U = M.HEAP32.subarray(uv >> 2, (uv >> 2) + 2 * k);
      const out = { u: new Int32Array(k), v: new Int32Array(k), w: M.HEAPF64.slice(w >> 3, (w >> 3) + k) };
      for (let i = 0; i < k; i++) {
        out.u[i] = U[2 * i];
        out.v[i] = U[2 * i + 1];
      }
      return out;
    } finally {
      M._free(uv);
      M._free(w);
    }
  }

  /**
   * Edges for drawing: in a directed graph u->v and v->u collapse into one
   * entry flagged in bidir (see wg_draw_edges).
   * @returns {{ u: Int32Array, v: Int32Array, bidir: Uint8Array }}
   */
  drawEdges() {
    const M = this._m;
    const m = this.m;
    const uv = M._malloc(Math.max(1, m) * 8);
    const bi = M._malloc(Math.max(1, m));
    try {
      const k = check(M._wg_draw_edges(this.ptr, uv, bi, m), "drawEdges");
      const U = M.HEAP32.subarray(uv >> 2, (uv >> 2) + 2 * k);
      const out = { u: new Int32Array(k), v: new Int32Array(k), bidir: M.HEAPU8.slice(bi, bi + k) };
      for (let i = 0; i < k; i++) {
        out.u[i] = U[2 * i];
        out.v[i] = U[2 * i + 1];
      }
      return out;
    } finally {
      M._free(uv);
      M._free(bi);
    }
  }

  /** @returns {{ order: Int32Array, parent: Int32Array, dist: Int32Array }} */
  bfs(source) { return this._traverse("_wg_bfs", source); }
  dfs(source) { return this._traverse("_wg_dfs", source); }

  _traverse(fn, source) {
    const n = this.n;
    const { rc, out } = this.tg._withInts(n, 3, (o, p, d) => this._m[fn](this.ptr, source, o, p, d));
    check(rc, fn.slice(4));
    return { order: out[0].subarray(0, rc), parent: out[1], dist: out[2] };
  }

  /** Connected (strongly, if directed) components: comp[v] = component index. */
  components() {
    const { rc, out } = this.tg._withInts(this.n, 1, (c) => this._m._wg_components(this.ptr, c));
    check(rc, "components");
    return { count: rc, comp: out[0] };
  }

  /**
   * Kruskal minimum spanning forest (undirected graphs only), edges in the
   * order Kruskal picks them: by increasing weight.
   * @returns {{ u: Int32Array, v: Int32Array, w: Float64Array, weight: number }}
   */
  mstKruskal() {
    const M = this._m;
    const cap = Math.max(1, this.n - 1);
    const uv = M._malloc(cap * 8);
    const w = M._malloc(cap * 8);
    try {
      const k = check(M._wg_mst_kruskal(this.ptr, uv, w, cap), "mstKruskal");
      const U = M.HEAP32.subarray(uv >> 2, (uv >> 2) + 2 * k);
      const out = { u: new Int32Array(k), v: new Int32Array(k), w: M.HEAPF64.slice(w >> 3, (w >> 3) + k), weight: 0 };
      for (let i = 0; i < k; i++) {
        out.u[i] = U[2 * i];
        out.v[i] = U[2 * i + 1];
        out.weight += out.w[i];
      }
      return out;
    } finally {
      M._free(uv);
      M._free(w);
    }
  }

  /**
   * The library has a fixed vertex count, so adding vertices rebuilds the
   * graph. Returns the new Graph and frees this one.
   */
  withVertices(extra = 1) {
    const e = this.edges();
    const list = Array.from(e.u, (u, i) => [u, e.v[i], e.w[i]]);
    const g = this.tg.fromEdges(this.n + extra, this.directed, list, { repr: this.repr });
    this.free();
    return g;
  }

  /** Same graph in another representation (e.g. STAR for fast read-only traversals). */
  convert(repr) {
    const e = this.edges();
    const list = Array.from(e.u, (u, i) => [u, e.v[i], e.w[i]]);
    return this.tg.fromEdges(this.n, this.directed, list, { repr });
  }

  free() {
    if (!this.ptr) return;
    this.tg._registry.unregister(this);
    this.tg.mod._wg_free(this.ptr);
    this.ptr = 0;
  }
}
