/**
 * The wire format, mirrored from firmware_test/src/ble/app_ble.h.
 *
 * CHANGE ONE AND YOU MUST CHANGE THE OTHER. Nothing checks this at build time;
 * the only guard is PROTOCOL_VERSION, which the firmware refuses to accept a
 * mismatch on and this file refuses to decode.
 *
 * Everything is little-endian and explicitly so: `true` as the last argument to
 * every DataView getter. The default is big-endian, and a forgotten flag here
 * produces distances in the tens of metres rather than an error.
 */

export const PROTOCOL_VERSION = 1;

/* --- Frozen UUIDs. Mirror of firmware_test/src/ble/ble_uuid.h ------------- */

const base = (n: string) => `53f9${n}-1e2d-11ef-9262-0242ac120002`;

export const UUID = {
  frameService: base('0001'),
  frameData: base('0002'),
  frameInfo: base('0003'),

  configService: base('1001'),
  config: base('1002'),
  command: base('1003'),
  configResult: base('1004'),

  telemetryService: base('2001'),
  health: base('2002'),
  energy: base('2003'),
} as const;

/* --- Resolutions ---------------------------------------------------------- *
 *
 * The enum order IS the wire value, matching enum vl53l9cx_res.
 *
 * The two FAMILIES are not interchangeable. Wide formats merge zones and keep
 * the full field of view; square formats transmit a larger array and crop it
 * on-device with a y-offset, so they see something DIFFERENT. An energy-versus-
 * zones curve that mixes families is comparing two fields of view.
 */
export interface ResolutionInfo {
  value: number;
  label: string;
  cols: number;
  rows: number;
  zones: number;
  family: 'wide' | 'square';
}

export const RESOLUTIONS: ResolutionInfo[] = [
  { value: 0, label: '4x4', cols: 4, rows: 4, zones: 16, family: 'square' },
  { value: 1, label: '8x6', cols: 8, rows: 6, zones: 48, family: 'square' },
  { value: 2, label: '12x10', cols: 12, rows: 10, zones: 120, family: 'wide' },
  { value: 3, label: '18x14', cols: 18, rows: 14, zones: 252, family: 'wide' },
  { value: 4, label: '24x20', cols: 24, rows: 20, zones: 480, family: 'square' },
  { value: 5, label: '54x42', cols: 54, rows: 42, zones: 2268, family: 'wide' },
];

export const PLANE = {
  distance: 1 << 0,
  amplitude: 1 << 1,
  ambient: 1 << 2,
} as const;

export const MODE = { idle: 0, streaming: 1 } as const;

/**
 * Measurement range: which ranging CONTEXT the device uses.
 *
 * Not a filter on the output. The context changes the analogue front end's
 * distance scaling and its calibration offsets, so it decides how well close
 * targets resolve at all. The driver used FAR unconditionally until 2026-09-12,
 * which is why nothing measured close despite the part being specified from
 * 5 cm.
 *
 * ST's own profiles split the same way: their precision and autofocus profiles
 * use the near context, their two range profiles use far.
 *
 * RANGE IS NOT ONLY THIS. Exposure matters as much at close distance and in the
 * opposite direction: a near target returns a lot of light, and too much
 * exposure saturates the histogram so the zone is REJECTED rather than reported
 * near. Expect to lower exposure when selecting NEAR.
 */
export const RANGE = { far: 0, near: 1 } as const;

/**
 * MEASUREMENT MODES.
 *
 * A mode is a whole working point, not just a range: context, exposure,
 * switchover, resolution and frame period together. They are bundled because
 * they are not independent — a near target needs the SHORT context AND a low
 * exposure AND enough frame rate to follow a moving hand, and setting one
 * without the others produces a worse result than leaving the default alone.
 *
 * `band` is what the mode is GOOD AT, not what the sensor can physically do.
 * The part is specified 5 cm to 8.8 m and hard-limited to 9.6 m (UM3683 fixes
 * the ranging period at 64 ns); no mode extends that, they trade where inside
 * it the measurement is accurate.
 *
 * THE EXPOSURE FIGURES ARE ESTIMATES. Nothing in this repo has yet measured
 * valid-zone count against distance for either context. They are a place to
 * start; the amplitude split in the stats bar says which way to move.
 * docs/plan/measurement-range.md has the half hour of measurement that turns
 * them into settings.
 */
export interface MeasurementMode {
  id: string;
  name: string;
  band: string;
  bandMinMm: number;
  bandMaxMm: number;
  goodFor: string;
  watchOut: string;
  range: number;
  exposureMs: number;
  switchoverMm: number;
  resolution: number;
  framePeriodMs: number;
}

export const MEASUREMENT_MODES: MeasurementMode[] = [
  {
    id: 'close',
    name: 'Close object',
    band: '5 cm – 50 cm',
    bandMinMm: 50,
    bandMaxMm: 500,
    goodFor:
      'A hand, a cup, a face right in front of the sensor. Near context with the shortest exposure, because a target this close returns a great deal of light.',
    watchOut:
      'If a very close object reads as EMPTY rather than near, exposure is still too high — it is saturating and the zone gets rejected. Drop it to 1 ms.',
    range: 1,
    exposureMs: 1,
    switchoverMm: 200,
    resolution: 4, // 24x20 — ~4x the frame rate of full resolution
    framePeriodMs: 0,
  },
  {
    id: 'desk',
    name: 'Desk / gesture',
    band: '10 cm – 1.5 m',
    bandMinMm: 100,
    bandMaxMm: 1500,
    goodFor:
      'Arm’s reach. Hand tracking, presence at a desk, objects on a table. This is the context ST use for their precision profiles.',
    watchOut:
      'Still the near context, so anything past ~2 m will drop out. Switch to Room if the far half of the scene goes dark.',
    range: 1,
    exposureMs: 2,
    switchoverMm: 400,
    resolution: 4,
    framePeriodMs: 0,
  },
  {
    id: 'room',
    name: 'Room detection',
    band: '0.5 m – 4 m',
    bandMinMm: 500,
    bandMaxMm: 4000,
    goodFor:
      'People in a room, the corner-mount case this project is built for. Full resolution, far context, moderate exposure.',
    watchOut:
      'Full resolution is ~334 ms per frame at 400 kHz, so about 2.5 fps. Fine for people, stuttery for a waving hand.',
    range: 0,
    exposureMs: 4,
    switchoverMm: 650,
    resolution: 5, // 54x42
    framePeriodMs: 0,
  },
  {
    id: 'far',
    name: 'Long range',
    band: '2 m – 9.6 m',
    bandMinMm: 2000,
    bandMaxMm: 9600,
    goodFor:
      'A corridor, the far wall, the full depth of a large room. Long exposure for a weak return.',
    watchOut:
      '9.6 m is a HARD ceiling, not a guideline — UM3683 fixes the ranging period at 64 ns and nothing sees past it. Close objects will saturate badly at this exposure.',
    range: 0,
    exposureMs: 16,
    switchoverMm: 1500,
    resolution: 5,
    framePeriodMs: 0,
  },
];

/** The mode a configuration corresponds to, or null if it has been hand-edited. */
export function modeFor(c: {
  rangeMode: number;
  exposureMs: number;
  switchoverMm: number;
  resolution: number;
}): MeasurementMode | null {
  return (
    MEASUREMENT_MODES.find(
      (m) =>
        m.range === c.rangeMode &&
        m.exposureMs === c.exposureMs &&
        m.switchoverMm === c.switchoverMm &&
        m.resolution === c.resolution,
    ) ?? null
  );
}

export const OPCODE = {
  none: 0,
  start: 1,
  stop: 2,
  singleShot: 3,
  rebootSensor: 4,
  calibrate: 5,
  clearCalibration: 6,
} as const;

/* --- Config, 16 bytes ----------------------------------------------------- */

export interface Config {
  protocolVersion: number;
  resolution: number;
  planes: number;
  instanceId: number;
  exposureMs: number;
  framePeriodMs: number;
  advIntervalMs: number;
  mode: number;
  flags: number;
  /** enum vl53l9cx_range_mode: 0 far (LONG context), 1 near (SHORT). */
  rangeMode: number;
  /** STREAM_SWITCHOVER_DIST in mm; 0 leaves the device's value alone. */
  switchoverMm: number;
}

export function decodeConfig(dv: DataView): Config {
  return {
    protocolVersion: dv.getUint8(0),
    resolution: dv.getUint8(1),
    planes: dv.getUint8(2),
    instanceId: dv.getUint8(3),
    exposureMs: dv.getUint16(4, true),
    framePeriodMs: dv.getUint16(6, true),
    advIntervalMs: dv.getUint16(8, true),
    mode: dv.getUint8(10),
    flags: dv.getUint8(11),
    rangeMode: dv.getUint8(12),
    switchoverMm: dv.getUint16(14, true),
  };
}

export function encodeConfig(c: Config): ArrayBuffer {
  const buf = new ArrayBuffer(16);
  const dv = new DataView(buf);
  dv.setUint8(0, PROTOCOL_VERSION);
  dv.setUint8(1, c.resolution);
  dv.setUint8(2, c.planes);
  dv.setUint8(3, c.instanceId);
  dv.setUint16(4, c.exposureMs, true);
  dv.setUint16(6, c.framePeriodMs, true);
  dv.setUint16(8, c.advIntervalMs, true);
  dv.setUint8(10, c.mode);
  dv.setUint8(11, c.flags);
  dv.setUint8(12, c.rangeMode);
  /* byte 13 reserved */
  dv.setUint16(14, c.switchoverMm, true);
  return buf;
}

export function encodeCommand(opcode: number, args: number[] = []): ArrayBuffer {
  const buf = new ArrayBuffer(4);
  const dv = new DataView(buf);
  dv.setUint8(0, opcode);
  for (let i = 0; i < 3; i++) dv.setUint8(1 + i, args[i] ?? 0);
  return buf;
}

/* --- Config Result, 4 bytes ----------------------------------------------- */

export interface CfgResult {
  opcode: number;
  detail: number;
  status: number;
}

export function decodeCfgResult(dv: DataView): CfgResult {
  return {
    opcode: dv.getUint8(0),
    detail: dv.getUint8(1),
    status: dv.getInt16(2, true),
  };
}

/* --- Telemetry ------------------------------------------------------------ */

export interface Health {
  sensorReady: boolean;
  streaming: boolean;
  errorStatus: number;
  errorCode: number;
  lastErrno: number;
  captureOk: number;
  captureFailed: number;
}

export function decodeHealth(dv: DataView): Health {
  const flags = dv.getUint8(0);
  return {
    sensorReady: (flags & 1) !== 0,
    streaming: (flags & 2) !== 0,
    errorStatus: dv.getUint8(1),
    errorCode: dv.getUint16(2, true),
    lastErrno: dv.getInt8(4),
    captureOk: dv.getUint32(8, true),
    captureFailed: dv.getUint32(12, true),
  };
}

export interface Energy {
  lastCaptureMs: number;
  blobUploadMs: number;
  exposureMs: number;
  resolution: number;
  framesSent: number;
  uptimeS: number;
}

export function decodeEnergy(dv: DataView): Energy {
  return {
    lastCaptureMs: dv.getUint16(0, true),
    blobUploadMs: dv.getUint16(2, true),
    exposureMs: dv.getUint16(4, true),
    resolution: dv.getUint8(6),
    framesSent: dv.getUint32(8, true),
    uptimeS: dv.getUint32(12, true),
  };
}

/**
 * The device's own verdict on itself.
 *
 * ERROR_STATUS (UM3683 Table 16) is a register of ERROR bits: a SET bit means
 * that error OCCURRED. This was inverted in the firmware twice, in two separate
 * files, and each time it announced "PLL NOT LOCKED" precisely when the clock
 * was fine — sending a day of debugging at the wrong subsystem. Stated
 * positively here so the same mistake cannot be made a third time.
 */
export const ERROR_STATUS_BITS = [
  'VHV overvoltage',
  'VHV undervoltage',
  'SPAD supply overload',
  'HV boost current limit',
  'SOF outside blanking',
  'PLL lock failed',
  'Reference array check',
  'Internal firmware error (see ERROR_CODE)',
];

/** UM3683 Table 17, the codes that actually occur on this path. */
export function errorCodeText(code: number): string {
  switch (code) {
    case 0x0000: return 'no error';
    case 0x0002: return 'streaming';
    case 0x0003: return 'DSS timeout';
    case 0x0004: return 'system fault';
    case 0x0008: return 'LDD timeout — the laser driver stopped responding';
    case 0x000a: return 'LDD SPI — the link to the laser driver failed';
    case 0x000d: return 'LDD safety — the laser driver tripped its interlock';
    case 0x0f00: return 'CABDT LDD FAULT — check the VCSEL supply (VBAT_LDD)';
    case 0x0f05: return 'CABDT VHV timeout';
    case 0x0f06: return 'CABDT DSS timeout';
    case 0x1004: return 'PHYPLL ext clock — AP_CLK is wrong or absent';
    case 0x1009: return 'invalid external clock frequency';
    default: return `unknown (UM3683 Table 17)`;
  }
}

/* --- Frames --------------------------------------------------------------- */

export interface FrameInfo {
  instanceId: number;
  protocolVersion: number;
  seq: number;
  deviceFrame: number;
  cols: number;
  rows: number;
  planes: number;
  flags: number;
  totalFragments: number;
  payloadBytes: number;
  temperatureRaw: number;
  captureMs: number;
}

export function decodeFrameInfo(dv: DataView): FrameInfo {
  return {
    instanceId: dv.getUint8(0),
    protocolVersion: dv.getUint8(1),
    seq: dv.getUint16(2, true),
    deviceFrame: dv.getUint16(4, true),
    cols: dv.getUint8(6),
    rows: dv.getUint8(7),
    planes: dv.getUint8(8),
    flags: dv.getUint8(9),
    totalFragments: dv.getUint16(10, true),
    payloadBytes: dv.getUint16(12, true),
    temperatureRaw: dv.getUint16(14, true),
    captureMs: dv.getUint16(16, true),
  };
}

export interface Frame {
  info: FrameInfo;
  /** Millimetres, bit 15 already stripped. */
  distance: Uint16Array;
  /** True where the device says the measurement is real. */
  valid: Uint8Array;
  amplitude: Uint16Array | null;
  ambient: Uint16Array | null;
  /** Host clock at completion — the device has no clock, by decision. */
  receivedAt: number;
}

/**
 * Reassembles fragments into frames, and counts what it loses.
 *
 * There is no retransmission in this protocol: at ~2.5 fps a lost frame is
 * cheaper than a stall. That makes the DROP RATE the honest measure of whether
 * the link is keeping up, so it is counted here and displayed, rather than
 * quietly papered over.
 */
export class FrameAssembler {
  private info: FrameInfo | null = null;
  private buf: Uint8Array = new Uint8Array(0);
  private fragSize = 0;
  private nextIndex = 0;
  private filled = 0;

  framesCompleted = 0;
  framesDropped = 0;
  fragmentsDropped = 0;
  bytesReceived = 0;

  onFrame: ((f: Frame) => void) | null = null;

  /** A Frame Info notification: the start of a new frame. */
  begin(info: FrameInfo): void {
    if (this.info && this.filled < this.info.payloadBytes) {
      /* The previous frame never completed. */
      this.framesDropped++;
    }
    this.info = info;
    this.buf = new Uint8Array(info.payloadBytes);
    this.fragSize = 0;
    this.nextIndex = 0;
    this.filled = 0;
  }

  /** A Frame Data notification. */
  fragment(dv: DataView): void {
    const seq = dv.getUint16(0, true);
    const index = dv.getUint16(2, true);
    const payload = new Uint8Array(dv.buffer, dv.byteOffset + 4, dv.byteLength - 4);

    this.bytesReceived += dv.byteLength;

    if (!this.info || seq !== this.info.seq) {
      /* A fragment for a frame we have no header for. Nothing useful can be
       * done with it — the header is what says how big the frame is. */
      this.fragmentsDropped++;
      return;
    }

    if (this.fragSize === 0) this.fragSize = payload.length;

    if (index !== this.nextIndex) {
      /* A gap. Count it and carry on: placing the fragment at its own index
       * keeps the rest of the frame correct rather than smearing it. */
      this.fragmentsDropped += Math.max(0, index - this.nextIndex);
    }

    const offset = index * this.fragSize;
    if (offset + payload.length <= this.buf.length) {
      this.buf.set(payload, offset);
      this.filled += payload.length;
    }
    this.nextIndex = index + 1;

    if (this.nextIndex >= this.info.totalFragments) {
      this.complete();
    }
  }

  private complete(): void {
    const info = this.info;
    if (!info) return;

    const zones = info.cols * info.rows;
    const dv = new DataView(this.buf.buffer, this.buf.byteOffset, this.buf.byteLength);

    const distance = new Uint16Array(zones);
    const valid = new Uint8Array(zones);
    let amplitude: Uint16Array | null = null;
    let ambient: Uint16Array | null = null;

    /* Planes arrive in ascending bit order, only the enabled ones present. */
    let plane = 0;
    const readPlane = (): Uint16Array => {
      const out = new Uint16Array(zones);
      const base = plane * zones * 2;
      for (let i = 0; i < zones; i++) {
        const o = base + i * 2;
        out[i] = o + 1 < dv.byteLength ? dv.getUint16(o, true) : 0;
      }
      plane++;
      return out;
    };

    if (info.planes & PLANE.distance) {
      const raw = readPlane();
      for (let i = 0; i < zones; i++) {
        /* Millimetres in bits 14:0, validity in bit 15. Forgetting the mask
         * yields readings around 32 m and a person with a hole in the middle. */
        distance[i] = raw[i] & 0x7fff;
        valid[i] = raw[i] & 0x8000 ? 1 : 0;
      }
    }
    if (info.planes & PLANE.amplitude) amplitude = readPlane();
    if (info.planes & PLANE.ambient) ambient = readPlane();

    this.framesCompleted++;
    this.info = null;

    this.onFrame?.({ info, distance, valid, amplitude, ambient, receivedAt: Date.now() });
  }

  reset(): void {
    this.info = null;
    this.filled = 0;
    this.framesCompleted = 0;
    this.framesDropped = 0;
    this.fragmentsDropped = 0;
    this.bytesReceived = 0;
  }
}
