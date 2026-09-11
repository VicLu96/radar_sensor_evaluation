/**
 * Web Bluetooth client for the water-sense node.
 *
 * CHROME OR EDGE ONLY. Web Bluetooth is not implemented in Firefox or Safari
 * and there is no polyfill. The page says so rather than failing mysteriously.
 *
 * Three things bite everyone once, so they are handled here explicitly:
 *
 *  - A USER GESTURE is mandatory for requestDevice(). It must be called from a
 *    real click handler, never from useEffect.
 *  - EVERY service must be listed in optionalServices. A service that is not
 *    listed throws on getPrimaryService() AFTER a successful connect, which
 *    reads like a firmware fault and is not one.
 *  - SECURE CONTEXT required. localhost counts, so `npm run dev` is fine; a
 *    plain-HTTP LAN address is not.
 */

import {
  UUID,
  PROTOCOL_VERSION,
  decodeConfig,
  encodeConfig,
  encodeCommand,
  decodeCfgResult,
  decodeHealth,
  decodeEnergy,
  decodeFrameInfo,
  FrameAssembler,
  type Config,
  type Health,
  type Energy,
  type Frame,
  type CfgResult,
} from './protocol';

export interface NodeCallbacks {
  onFrame?: (f: Frame) => void;
  onHealth?: (h: Health) => void;
  onEnergy?: (e: Energy) => void;
  onConfig?: (c: Config) => void;
  onResult?: (r: CfgResult) => void;
  onDisconnect?: () => void;
  onLog?: (line: string) => void;
}

export function bluetoothAvailable(): boolean {
  return typeof navigator !== 'undefined' && 'bluetooth' in navigator;
}

export class Node {
  device: BluetoothDevice | null = null;
  private server: BluetoothRemoteGATTServer | null = null;
  private configChar: BluetoothRemoteGATTCharacteristic | null = null;
  private commandChar: BluetoothRemoteGATTCharacteristic | null = null;

  readonly assembler = new FrameAssembler();
  private cb: NodeCallbacks = {};

  /** Set when the frame service is absent — i.e. a deployed-profile build. */
  hasFrameService = false;

  constructor(cb: NodeCallbacks) {
    this.cb = cb;
    this.assembler.onFrame = (f) => this.cb.onFrame?.(f);
  }

  private log(line: string) {
    this.cb.onLog?.(line);
  }

  /**
   * Must be called from a click handler.
   *
   * @param showAll skip the name filter and list every device in range.
   *
   * The filtered path is the normal one. The unfiltered path exists because a
   * chooser that comes up EMPTY is ambiguous — it means either the node is not
   * advertising, or Chrome is not surfacing its name — and those need opposite
   * responses. Listing everything separates them in one click: if the node
   * appears here but not under the filter, the radio is fine and the name is
   * the problem; if it appears in neither, the radio is.
   *
   * Windows in particular can surface a device under a cached name from an
   * earlier pairing, which no namePrefix will match.
   */
  async connect(showAll = false): Promise<void> {
    if (!bluetoothAvailable()) {
      throw new Error(
        'Web Bluetooth is not available. Use Chrome or Edge over localhost or HTTPS.',
      );
    }

    this.device = await navigator.bluetooth.requestDevice({
      ...(showAll
        ? { acceptAllDevices: true }
        : { filters: [{ namePrefix: 'water-sense' }] }),
      /* Without these, getPrimaryService throws after a successful connect. */
      optionalServices: [UUID.configService, UUID.telemetryService, UUID.frameService],
    });

    this.device.addEventListener('gattserverdisconnected', () => {
      this.log('disconnected');
      this.server = null;
      this.cb.onDisconnect?.();
    });

    this.log(`connecting to ${this.device.name ?? 'node'}`);
    this.server = (await this.device.gatt!.connect()) as BluetoothRemoteGATTServer;

    await this.setupConfig();
    await this.setupTelemetry();
    await this.setupFrames();
    this.log('ready');
  }

  disconnect(): void {
    this.device?.gatt?.disconnect();
  }

  get connected(): boolean {
    return this.device?.gatt?.connected ?? false;
  }

  private async setupConfig() {
    const svc = await this.server!.getPrimaryService(UUID.configService);
    this.configChar = await svc.getCharacteristic(UUID.config);
    this.commandChar = await svc.getCharacteristic(UUID.command);

    const result = await svc.getCharacteristic(UUID.configResult);
    await result.startNotifications();
    result.addEventListener('characteristicvaluechanged', (ev) => {
      const dv = (ev.target as BluetoothRemoteGATTCharacteristic).value!;
      this.cb.onResult?.(decodeCfgResult(dv));
    });

    await this.readConfig();
  }

  async readConfig(): Promise<Config> {
    const dv = await this.configChar!.readValue();
    const cfg = decodeConfig(dv);
    if (cfg.protocolVersion !== PROTOCOL_VERSION) {
      /* Refusing is better than rendering garbage: every struct in this
       * protocol is a fixed byte layout, and a version bump means at least one
       * of them moved. */
      throw new Error(
        `Node speaks protocol ${cfg.protocolVersion}, this page speaks ${PROTOCOL_VERSION}. Reflash or reload.`,
      );
    }
    this.cb.onConfig?.(cfg);
    return cfg;
  }

  async writeConfig(cfg: Config): Promise<void> {
    await this.configChar!.writeValueWithResponse(encodeConfig(cfg));
    await this.readConfig();
  }

  async command(opcode: number, args: number[] = []): Promise<void> {
    await this.commandChar!.writeValueWithResponse(encodeCommand(opcode, args));
  }

  private async setupTelemetry() {
    const svc = await this.server!.getPrimaryService(UUID.telemetryService);

    const health = await svc.getCharacteristic(UUID.health);
    this.cb.onHealth?.(decodeHealth(await health.readValue()));
    await health.startNotifications();
    health.addEventListener('characteristicvaluechanged', (ev) => {
      const dv = (ev.target as BluetoothRemoteGATTCharacteristic).value!;
      this.cb.onHealth?.(decodeHealth(dv));
    });

    const energy = await svc.getCharacteristic(UUID.energy);
    this.cb.onEnergy?.(decodeEnergy(await energy.readValue()));
    await energy.startNotifications();
    energy.addEventListener('characteristicvaluechanged', (ev) => {
      const dv = (ev.target as BluetoothRemoteGATTCharacteristic).value!;
      this.cb.onEnergy?.(decodeEnergy(dv));
    });
  }

  private async setupFrames() {
    let svc: BluetoothRemoteGATTService;
    try {
      svc = await this.server!.getPrimaryService(UUID.frameService);
    } catch {
      /*
       * Not an error. A build with CONFIG_APP_BLE_FRAME_SERVICE=n has no frame
       * service at all — that is the deployed profile, and its absence is the
       * privacy claim being true rather than merely stated.
       */
      this.hasFrameService = false;
      this.log(
        'no frame service — this is a DEPLOYED build. Frames cannot leave it; only counts can.',
      );
      return;
    }
    this.hasFrameService = true;

    /* Info first: it carries the frame geometry the fragments are decoded
     * against, so subscribing to data first would drop the first frame. */
    const info = await svc.getCharacteristic(UUID.frameInfo);
    await info.startNotifications();
    info.addEventListener('characteristicvaluechanged', (ev) => {
      const dv = (ev.target as BluetoothRemoteGATTCharacteristic).value!;
      this.assembler.begin(decodeFrameInfo(dv));
    });

    const data = await svc.getCharacteristic(UUID.frameData);
    await data.startNotifications();
    data.addEventListener('characteristicvaluechanged', (ev) => {
      const dv = (ev.target as BluetoothRemoteGATTCharacteristic).value!;
      this.assembler.fragment(dv);
    });
  }
}
