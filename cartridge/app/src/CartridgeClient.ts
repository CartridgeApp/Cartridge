/**
 * Cartridge M1 -- unified client over the real TurboModule and a TS mock.
 *
 * plan.md section 5 (dev workflow): UI work happens two ways -- fast refresh
 * against the real dev client on a handheld, or `expo start --web`/Expo Go
 * against fake data with no native module present at all. This file is the
 * seam: everything in the app imports from here, never from
 * specs/NativeCartridgeModule directly, so swapping in the M2-generated
 * mock (from the settings catalog) later is a one-file change.
 */
import {DeviceEventEmitter} from 'react-native';
import NativeCartridgeModule from './specs/NativeCartridgeModule';

export interface CartridgeEvent {
  name: string;
  detail: string;
}

export interface CartridgeClient {
  loadContent(corePath: string, contentPath: string): Promise<string>;
  setPaused(paused: boolean): Promise<string>;
  takeScreenshot(): Promise<string>;
  addEventListener(listener: (event: CartridgeEvent) => void): () => void;
  readonly isMock: boolean;
}

const CARTRIDGE_EVENT_NAME = 'CartridgeEvent';

function createNativeClient(): CartridgeClient {
  return {
    isMock: false,
    loadContent: (corePath, contentPath) =>
      NativeCartridgeModule!.loadContent(corePath, contentPath),
    setPaused: paused => NativeCartridgeModule!.setPaused(paused),
    takeScreenshot: () => NativeCartridgeModule!.takeScreenshot(),
    addEventListener(listener) {
      const sub = DeviceEventEmitter.addListener(CARTRIDGE_EVENT_NAME, listener);
      return () => sub.remove();
    },
  };
}

/** Mirrors the real module's behavior closely enough to build/demo UI
 * against: async, occasionally emits a "message" event, no real RetroArch
 * underneath it. */
function createMockClient(): CartridgeClient {
  let paused = false;

  const emitMessage = (msg: string) =>
    DeviceEventEmitter.emit(CARTRIDGE_EVENT_NAME, {name: 'message', detail: msg} as CartridgeEvent);

  return {
    isMock: true,
    async loadContent(corePath, contentPath) {
      await new Promise(resolve => setTimeout(resolve, 200));
      emitMessage(`[mock] loaded ${contentPath} with ${corePath}`);
      return contentPath;
    },
    async setPaused(next) {
      await new Promise(resolve => setTimeout(resolve, 50));
      paused = next;
      emitMessage(paused ? '[mock] Paused' : '[mock] Unpaused');
      return paused ? '1' : '0';
    },
    async takeScreenshot() {
      await new Promise(resolve => setTimeout(resolve, 50));
      emitMessage('[mock] Screenshot saved');
      return '/mock/screenshots';
    },
    addEventListener(listener) {
      const sub = DeviceEventEmitter.addListener(CARTRIDGE_EVENT_NAME, listener);
      return () => sub.remove();
    },
  };
}

const Cartridge: CartridgeClient = NativeCartridgeModule
  ? createNativeClient()
  : createMockClient();

export default Cartridge;
