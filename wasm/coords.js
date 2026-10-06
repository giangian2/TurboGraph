/*
 * Vertex coordinates for GraphView.setPositions.
 *
 * Reads the DIMACS coordinate format (9th DIMACS Implementation Challenge,
 * the .co files shipped with the USA road networks):
 *
 *   c comment
 *   p aux sp co 321270
 *   v 1 -121745853 37608914      <- id, longitude, latitude in millionths of a degree
 *
 * Longitude/latitude are projected equirectangularly around the mean
 * latitude, which keeps distances right at city/region scale.
 *
 * Lines "id x y" (no leading v) are also accepted, as plain planar
 * coordinates with y pointing up.
 *
 * Input is consumed as a stream, line by line: the Western USA file is
 * ~200MB of text, more than is wise to hold as one JS string.
 */

/**
 * @param {ReadableStream<Uint8Array>} stream file contents (see byteStream)
 * @param {number} n vertex count of the graph (ids >= n are ignored)
 * @returns {Promise<{ xs: Float64Array, ys: Float64Array, count: number }>} NaN where an id has no coordinates
 */
export async function parseCoords(stream, n) {
  const xs = new Float64Array(n).fill(NaN), ys = new Float64Array(n).fill(NaN);
  let count = 0, dimacs = false, rest = "";

  /* Hand-rolled scanner: splitting 6M lines with a regex costs seconds. */
  const line = (l) => {
    let i = 0;
    const len = l.length;
    const skip = () => { while (i < len && (l.charCodeAt(i) === 32 || l.charCodeAt(i) === 9)) i++; };
    const num = () => {
      skip();
      const s = i;
      while (i < len && l.charCodeAt(i) > 32) i++;
      return i > s ? Number(l.slice(s, i)) : NaN;
    };
    skip();
    const c = l.charCodeAt(i);
    if (c === 118 /* v */) { dimacs = true; i++; }
    else if (!(c >= 48 && c <= 57)) return; // comments, "p ..." header, blank lines
    const id = num(), x = num(), y = num();
    if (!(id >= 0 && id < n) || !isFinite(x) || !isFinite(y)) return;
    xs[id] = x;
    ys[id] = y;
    count++;
  };

  const decoder = new TextDecoder();
  for (const reader = stream.getReader(); ;) {
    const { done, value } = await reader.read();
    const chunk = rest + (done ? decoder.decode() : decoder.decode(value, { stream: true }));
    const lines = chunk.split("\n");
    rest = done ? "" : lines.pop(); // a line cut by the chunk boundary
    for (const l of lines) line(l);
    if (done) break;
  }
  if (!count) throw new Error("no coordinates found");

  if (dimacs) {
    let lat0 = 0;
    for (let v = 0; v < n; v++) if (isFinite(ys[v])) lat0 += ys[v] / 1e6;
    const k = Math.cos(((lat0 / count) * Math.PI) / 180);
    for (let v = 0; v < n; v++) {
      xs[v] = (xs[v] / 1e6) * k;
      ys[v] = -ys[v] / 1e6; // north up on screen
    }
  } else {
    for (let v = 0; v < n; v++) ys[v] = -ys[v];
  }
  return { xs, ys, count };
}

/** Byte stream of a fetched Response or a File, gunzipped when the name ends in .gz. */
export function byteStream(source, name) {
  const body = source.body ?? source.stream();
  return /\.gz$/i.test(name) ? body.pipeThrough(new DecompressionStream("gzip")) : body;
}
