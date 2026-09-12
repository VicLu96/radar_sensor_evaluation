/**
 * Recording frames to a file.
 *
 * TWO FORMATS, because they are good at different things and the difference is
 * about 3x in size:
 *
 *                    54x42, 1 plane      54x42, 3 planes
 *   binary .wstof    0.68 MB / minute    2.04 MB / minute
 *   csv              2.05 MB / minute    6.13 MB / minute
 *
 * CSV is the default. It opens in anything, it carries its own documentation in
 * a `#` comment block, and at a few minutes of recording the size difference
 * does not matter. Binary is there for long runs and for the duty-cycle work,
 * where recordings get long and exact.
 *
 * BOTH CARRY THE FULL CONFIGURATION, including changes made DURING the
 * recording. A file that says "2268 zones" but not at what exposure, in which
 * ranging context, or that someone switched mode ninety seconds in, is not a
 * measurement — it is a picture. Every configuration the node reported while
 * recording is written into the header with the frame index it took effect at.
 *
 * The format is specified in docs/plan/recording-format.md, with Python
 * decoders for both.
 */

import { RESOLUTIONS, PLANE, RANGE, modeFor, type Config, type Frame } from './protocol';

export const RECORD_FORMAT_VERSION = 1;

/** A configuration, and the frame index at which it started applying. */
export interface ConfigEpoch {
  fromFrameIndex: number;
  config: Config;
  modeName: string | null;
}

interface StoredFrame {
  seq: number;
  deviceFrame: number;
  tHostMs: number;
  captureMs: number;
  temperatureRaw: number;
  validCount: number;
  cols: number;
  rows: number;
  planes: number;
  /** Millimetres, with -1 where the device reported no valid measurement. */
  distance: Int16Array;
  amplitude: Uint16Array | null;
  ambient: Uint16Array | null;
}

/**
 * Frames are kept in memory until the recording stops.
 *
 * At 54x42 with three planes that is ~13.6 KB per frame, so 4000 frames is
 * ~55 MB — enough for about 25 minutes at 2.5 fps, and comfortably inside what
 * a browser tab holds. The cap exists so a recording left running overnight
 * fails visibly rather than by killing the tab.
 */
export const MAX_FRAMES = 4000;

export class Recorder {
  private frames: StoredFrame[] = [];
  private epochs: ConfigEpoch[] = [];
  private startedAt = 0;

  recording = false;
  hitCap = false;

  start(config: Config): void {
    this.frames = [];
    this.epochs = [];
    this.hitCap = false;
    this.startedAt = Date.now();
    this.recording = true;
    this.noteConfig(config);
  }

  stop(): void {
    this.recording = false;
  }

  /** Call whenever the node reports a configuration, during or at the start. */
  noteConfig(config: Config): void {
    if (!this.recording) return;
    const last = this.epochs[this.epochs.length - 1];
    if (last && JSON.stringify(last.config) === JSON.stringify(config)) return;
    this.epochs.push({
      fromFrameIndex: this.frames.length,
      config: { ...config },
      modeName: modeFor(config)?.name ?? null,
    });
  }

  add(f: Frame): void {
    if (!this.recording || this.frames.length >= MAX_FRAMES) {
      if (this.recording) {
        this.hitCap = true;
        this.recording = false;
      }
      return;
    }

    const n = f.info.cols * f.info.rows;
    const distance = new Int16Array(n);
    let validCount = 0;
    for (let i = 0; i < n; i++) {
      if (f.valid[i]) {
        /* Clamp rather than wrap: the device cannot range past 9.6 m anyway,
         * and a silent wrap into negatives would look like an invalid zone. */
        distance[i] = Math.min(f.distance[i], 32767);
        validCount++;
      } else {
        distance[i] = -1; /* no valid measurement — NOT zero distance */
      }
    }

    this.frames.push({
      seq: f.info.seq,
      deviceFrame: f.info.deviceFrame,
      tHostMs: f.receivedAt,
      captureMs: f.info.captureMs,
      temperatureRaw: f.info.temperatureRaw,
      validCount,
      cols: f.info.cols,
      rows: f.info.rows,
      planes: f.info.planes,
      distance,
      amplitude: f.amplitude ? new Uint16Array(f.amplitude) : null,
      ambient: f.ambient ? new Uint16Array(f.ambient) : null,
    });
  }

  get frameCount(): number {
    return this.frames.length;
  }

  get durationS(): number {
    if (this.frames.length === 0) return 0;
    return (this.frames[this.frames.length - 1].tHostMs - this.startedAt) / 1000;
  }

  /** Rough bytes if saved now, for the UI. */
  estimatedBytes(csv: boolean): number {
    const f = this.frames[0];
    if (!f) return 0;
    const n = f.cols * f.rows;
    const planes = (f.planes & PLANE.distance ? 1 : 0) +
      (f.planes & PLANE.amplitude ? 1 : 0) +
      (f.planes & PLANE.ambient ? 1 : 0);
    const per = csv ? n * planes * 6 + 60 : n * planes * 2 + 24;
    return per * this.frames.length;
  }

  private header(): Record<string, unknown> {
    const f = this.frames[0];
    const res = f ? RESOLUTIONS.find((r) => r.cols === f.cols && r.rows === f.rows) : undefined;
    return {
      format: 'water-sense-tof-recording',
      formatVersion: RECORD_FORMAT_VERSION,
      recordedAt: new Date(this.startedAt).toISOString(),
      /* The DEVICE HAS NO CLOCK, by decision. Every timestamp in this file is
       * the receiving host's, taken when the frame finished reassembling. */
      timeBase: 'host clock at frame completion, ms since Unix epoch',
      frameCount: this.frames.length,
      truncated: this.hitCap,
      cols: f?.cols ?? 0,
      rows: f?.rows ?? 0,
      zones: f ? f.cols * f.rows : 0,
      resolutionLabel: res?.label ?? null,
      resolutionFamily: res?.family ?? null,
      planes: {
        distance: f ? (f.planes & PLANE.distance) !== 0 : false,
        amplitude: f ? (f.planes & PLANE.amplitude) !== 0 : false,
        ambient: f ? (f.planes & PLANE.ambient) !== 0 : false,
      },
      invalidDistance: -1,
      distanceUnit: 'mm',
      zoneOrder: 'row-major, index = row * cols + col, origin top-left as the sensor sees it',
      zoneOrderVerified: false,
      configs: this.epochs.map((e) => ({
        fromFrameIndex: e.fromFrameIndex,
        mode: e.modeName,
        rangeContext: e.config.rangeMode === RANGE.near ? 'near' : 'far',
        exposureMs: e.config.exposureMs,
        switchoverMm: e.config.switchoverMm,
        framePeriodMs: e.config.framePeriodMs,
        resolutionIndex: e.config.resolution,
        planesBitmask: e.config.planes,
        instanceId: e.config.instanceId,
        protocolVersion: e.config.protocolVersion,
      })),
    };
  }

  /* --- CSV ---------------------------------------------------------------- */

  toCsv(): Blob {
    const h = this.header();
    const f = this.frames[0];
    const n = f ? f.cols * f.rows : 0;
    const hasAmp = !!f?.amplitude;
    const hasAmb = !!f?.ambient;

    const lines: string[] = [];
    lines.push('# water-sense ToF recording');
    lines.push('# Decode in pandas with:  pd.read_csv(path, comment="#")');
    lines.push('# Full specification: docs/plan/recording-format.md');
    lines.push('#');
    for (const line of JSON.stringify(h, null, 1).split('\n')) {
      lines.push('# ' + line);
    }
    lines.push('#');
    lines.push('# d<i> = distance in mm for zone i, -1 means NO VALID MEASUREMENT');
    lines.push('#        (which is not the same as zero distance)');
    if (hasAmp) lines.push('# a<i> = return amplitude for zone i, device units');
    if (hasAmb) lines.push('# b<i> = ambient level for zone i, device units');
    lines.push('#');

    const cols = ['seq', 'device_frame', 't_host_ms', 'capture_ms', 'temperature_raw', 'valid_count'];
    for (let i = 0; i < n; i++) cols.push(`d${i}`);
    if (hasAmp) for (let i = 0; i < n; i++) cols.push(`a${i}`);
    if (hasAmb) for (let i = 0; i < n; i++) cols.push(`b${i}`);
    lines.push(cols.join(','));

    for (const fr of this.frames) {
      const row: (string | number)[] = [
        fr.seq, fr.deviceFrame, fr.tHostMs, fr.captureMs, fr.temperatureRaw, fr.validCount,
      ];
      for (let i = 0; i < n; i++) row.push(fr.distance[i]);
      if (hasAmp && fr.amplitude) for (let i = 0; i < n; i++) row.push(fr.amplitude[i]);
      if (hasAmb && fr.ambient) for (let i = 0; i < n; i++) row.push(fr.ambient[i]);
      lines.push(row.join(','));
    }

    return new Blob([lines.join('\n') + '\n'], { type: 'text/csv;charset=utf-8' });
  }

  /* --- binary ------------------------------------------------------------- */

  toBinary(): Blob {
    const h = this.header();
    const json = new TextEncoder().encode(JSON.stringify(h));
    const f = this.frames[0];
    const n = f ? f.cols * f.rows : 0;
    const hasAmp = !!f?.amplitude;
    const hasAmb = !!f?.ambient;

    const FRAME_HDR = 24;
    const frameBytes = FRAME_HDR + n * 2 * (1 + (hasAmp ? 1 : 0) + (hasAmb ? 1 : 0));

    const prefix = new ArrayBuffer(12);
    const pv = new DataView(prefix);
    pv.setUint8(0, 0x57); /* W */
    pv.setUint8(1, 0x53); /* S */
    pv.setUint8(2, 0x54); /* T */
    pv.setUint8(3, 0x46); /* F */
    pv.setUint32(4, RECORD_FORMAT_VERSION, true);
    pv.setUint32(8, json.byteLength, true);

    const body = new ArrayBuffer(frameBytes * this.frames.length);
    const bv = new DataView(body);
    let o = 0;
    for (const fr of this.frames) {
      bv.setUint32(o, fr.seq, true);
      bv.setUint32(o + 4, fr.deviceFrame, true);
      bv.setFloat64(o + 8, fr.tHostMs, true);
      bv.setUint16(o + 16, fr.captureMs, true);
      bv.setUint16(o + 18, fr.temperatureRaw, true);
      bv.setUint16(o + 20, fr.validCount, true);
      bv.setUint16(o + 22, 0, true); /* pad, keeps the arrays 4-byte aligned */
      let p = o + FRAME_HDR;
      for (let i = 0; i < n; i++, p += 2) bv.setInt16(p, fr.distance[i], true);
      if (hasAmp && fr.amplitude) for (let i = 0; i < n; i++, p += 2) bv.setUint16(p, fr.amplitude[i], true);
      if (hasAmb && fr.ambient) for (let i = 0; i < n; i++, p += 2) bv.setUint16(p, fr.ambient[i], true);
      o += frameBytes;
    }

    return new Blob([prefix, json, body], { type: 'application/octet-stream' });
  }

  filenameStem(): string {
    const d = new Date(this.startedAt);
    const pad = (x: number) => String(x).padStart(2, '0');
    const mode = this.epochs[0]?.modeName?.replace(/[^a-z0-9]+/gi, '-').toLowerCase() ?? 'custom';
    return `tof-${d.getFullYear()}${pad(d.getMonth() + 1)}${pad(d.getDate())}` +
      `-${pad(d.getHours())}${pad(d.getMinutes())}${pad(d.getSeconds())}-${mode}`;
  }
}

/** Hand a Blob to the browser as a download. */
export function download(blob: Blob, filename: string): void {
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = filename;
  document.body.appendChild(a);
  a.click();
  a.remove();
  /* Revoking immediately can cancel the download in some browsers. */
  setTimeout(() => URL.revokeObjectURL(url), 10_000);
}
