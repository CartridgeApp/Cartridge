/**
 * Cartridge M1 -- <RetroArchSurface/> Fabric native component.
 *
 * Hosts the SurfaceView RetroArch's GL/Vulkan driver renders into (plan.md
 * section 4.2). No custom props/events for M1: the surface's lifecycle
 * (created/changed/destroyed) is wired straight from the host Activity's
 * Kotlin ViewManager into cartridge_api's native window plumbing, the same
 * way CartridgeSpikeActivity did in M0 -- there is nothing for JS to control
 * here yet. Later milestones may add props (e.g. aspect ratio) as needed.
 */
import type {ViewProps} from 'react-native';
import codegenNativeComponent from 'react-native/Libraries/Utilities/codegenNativeComponent';
import type {HostComponent} from 'react-native';

export interface NativeProps extends ViewProps {}

export default codegenNativeComponent<NativeProps>(
  'RetroArchSurface',
) as HostComponent<NativeProps>;
