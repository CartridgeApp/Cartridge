/**
 * Cartridge M1 -- imperative FFI surface (TurboModule spec).
 *
 * Hand-written for M1 (plan.md milestone M1: "3 hand-written calls
 * (loadContent, pause, screenshot) + 1 event"). M2 generates this file (and
 * the C++ side) from cartridge.idl.ts instead -- the shape is meant to carry
 * over unchanged.
 *
 * The native side is cartridge/native/turbomodule/CartridgeModule.cpp, which
 * marshals these calls onto RetroArch's emulation thread via
 * cartridge/native/cartridge_api.{h,c} (never touches RetroArch internals
 * directly on the JS thread -- see plan.md section 7 risks).
 */
import {TurboModule, TurboModuleRegistry} from 'react-native';

export interface Spec extends TurboModule {
  /** Loads `contentPath` with the core at `corePath`. Resolves with
   * `contentPath` on success, rejects with a message on failure. */
  loadContent(corePath: string, contentPath: string): Promise<string>;

  /** Pauses (true) or unpauses (false) the running content. Resolves with
   * "1"/"0" reflecting the requested state. */
  setPaused(paused: boolean): Promise<string>;

  /** Takes a screenshot into the configured screenshot directory. Resolves
   * with that directory path on success. */
  takeScreenshot(): Promise<string>;
}

/* Optional, not enforcing: Cartridge also runs against a TS mock in Expo Go
 * and `expo start --web` (plan.md section 5), where no native module exists. */
export default TurboModuleRegistry.get<Spec>('CartridgeModule');
