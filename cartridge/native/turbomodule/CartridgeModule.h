/*  Cartridge M1 -- C++ TurboModule (JSI, New Architecture).
 *
 *  Hand-written for M1 (plan.md milestone M1: "3 hand-written calls...
 *  + 1 event"). M2 replaces the manual methodMap_ registrations here with a
 *  spec generated from cartridge.idl.ts -- the shape stays the same, only
 *  the boilerplate becomes generated.
 *
 *  This is the *only* C++ file that talks to cartridge_api.h/cartridge_events.h
 *  (plain C, compiled into libretroarch-activity.so); everything on this side
 *  is JSI/CallInvoker/Promise plumbing shared between Android and iOS
 *  (plan.md section 4.3).
 */
#pragma once

#include <memory>

#include <ReactCommon/TurboModule.h>

namespace facebook::react {

class CartridgeModule : public TurboModule {
 public:
  explicit CartridgeModule(std::shared_ptr<CallInvoker> jsInvoker);
  ~CartridgeModule() override;

  jsi::Value loadContent(
      jsi::Runtime &rt, const jsi::Value &corePath, const jsi::Value &contentPath);
  jsi::Value setPaused(jsi::Runtime &rt, const jsi::Value &paused);
  jsi::Value takeScreenshot(jsi::Runtime &rt);

 private:
  static void onNativeEvent(const char *name, const char *detail, void *userData);
};

std::shared_ptr<TurboModule> CartridgeModuleProvider(
    const std::shared_ptr<CallInvoker> &jsInvoker);

} // namespace facebook::react
