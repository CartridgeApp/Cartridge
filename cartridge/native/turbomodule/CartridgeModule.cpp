/*  Cartridge M1 -- C++ TurboModule (implementation). See CartridgeModule.h. */
#include "CartridgeModule.h"

#include <string>
#include <utility>
#include <vector>

#include <ReactCommon/TurboModuleUtils.h>

#include "../cartridge_api.h"
#include "../cartridge_events.h"

namespace facebook::react {

namespace {

/* Bridges a cartridge_result_cb (fired on the emulation thread, see
 * cartridge_api.h) back to a JS promise on the JS thread. Owned by the
 * pending C call; freed once the callback fires exactly once. */
struct PendingResult {
  std::shared_ptr<Promise> promise;
  std::shared_ptr<CallInvoker> jsInvoker;
};

void resolveOrRejectPromise(bool success, const char *detail, void *userData) {
  std::unique_ptr<PendingResult> pending(static_cast<PendingResult *>(userData));
  std::string detailStr = detail ? detail : "";

  pending->jsInvoker->invokeAsync(
      [promise = pending->promise, success, detailStr](jsi::Runtime &rt) {
        if (success)
          promise->resolve(jsi::String::createFromUtf8(rt, detailStr));
        else
          promise->reject(detailStr);
      });
}

jsi::Value __hostFunction_CartridgeModule_loadContent(
    jsi::Runtime &rt, TurboModule &turboModule, const jsi::Value *args, size_t count) {
  return static_cast<CartridgeModule &>(turboModule).loadContent(rt, args[0], args[1]);
}

jsi::Value __hostFunction_CartridgeModule_setPaused(
    jsi::Runtime &rt, TurboModule &turboModule, const jsi::Value *args, size_t count) {
  return static_cast<CartridgeModule &>(turboModule).setPaused(rt, args[0]);
}

jsi::Value __hostFunction_CartridgeModule_takeScreenshot(
    jsi::Runtime &rt, TurboModule &turboModule, const jsi::Value * /*args*/, size_t /*count*/) {
  return static_cast<CartridgeModule &>(turboModule).takeScreenshot(rt);
}

/* cartridge_events_set_sink() only ever holds one JS-side listener (plan.md
 * section 4.3); CartridgeModule is likewise a singleton per JS runtime, so a
 * raw back-pointer captured at construction time is sufficient here. */

} // namespace

CartridgeModule::CartridgeModule(std::shared_ptr<CallInvoker> jsInvoker)
    : TurboModule("CartridgeModule", std::move(jsInvoker)) {
  methodMap_["loadContent"] = MethodMetadata{2, __hostFunction_CartridgeModule_loadContent};
  methodMap_["setPaused"] = MethodMetadata{1, __hostFunction_CartridgeModule_setPaused};
  methodMap_["takeScreenshot"] = MethodMetadata{0, __hostFunction_CartridgeModule_takeScreenshot};

  cartridge_events_set_sink(&CartridgeModule::onNativeEvent, this);
}

CartridgeModule::~CartridgeModule() {
  cartridge_events_set_sink(nullptr, nullptr);
}

void CartridgeModule::onNativeEvent(const char *name, const char *detail, void *userData) {
  auto *self = static_cast<CartridgeModule *>(userData);
  std::string eventName = name ? name : "";
  std::string detailStr = detail ? detail : "";

  self->emitDeviceEvent(
      "CartridgeEvent", [eventName, detailStr](jsi::Runtime &rt, std::vector<jsi::Value> &args) {
        jsi::Object payload(rt);
        payload.setProperty(rt, "name", jsi::String::createFromUtf8(rt, eventName));
        payload.setProperty(rt, "detail", jsi::String::createFromUtf8(rt, detailStr));
        args.emplace_back(std::move(payload));
      });
}

jsi::Value CartridgeModule::loadContent(
    jsi::Runtime &rt, const jsi::Value &corePathVal, const jsi::Value &contentPathVal) {
  std::string corePath = corePathVal.asString(rt).utf8(rt);
  std::string contentPath = contentPathVal.asString(rt).utf8(rt);
  std::shared_ptr<CallInvoker> jsInvoker = jsInvoker_;

  return createPromiseAsJSIValue(
      rt, [corePath, contentPath, jsInvoker](jsi::Runtime & /*rt2*/, std::shared_ptr<Promise> promise) {
        auto *pending = new PendingResult{std::move(promise), jsInvoker};
        cartridge_api_load_content(
            corePath.c_str(), contentPath.c_str(), &resolveOrRejectPromise, pending);
      });
}

jsi::Value CartridgeModule::setPaused(jsi::Runtime &rt, const jsi::Value &pausedVal) {
  bool paused = pausedVal.getBool();
  std::shared_ptr<CallInvoker> jsInvoker = jsInvoker_;

  return createPromiseAsJSIValue(
      rt, [paused, jsInvoker](jsi::Runtime & /*rt2*/, std::shared_ptr<Promise> promise) {
        auto *pending = new PendingResult{std::move(promise), jsInvoker};
        cartridge_api_set_paused(paused, &resolveOrRejectPromise, pending);
      });
}

jsi::Value CartridgeModule::takeScreenshot(jsi::Runtime &rt) {
  std::shared_ptr<CallInvoker> jsInvoker = jsInvoker_;

  return createPromiseAsJSIValue(
      rt, [jsInvoker](jsi::Runtime & /*rt2*/, std::shared_ptr<Promise> promise) {
        auto *pending = new PendingResult{std::move(promise), jsInvoker};
        cartridge_api_take_screenshot(&resolveOrRejectPromise, pending);
      });
}

std::shared_ptr<TurboModule> CartridgeModuleProvider(
    const std::shared_ptr<CallInvoker> &jsInvoker) {
  return std::make_shared<CartridgeModule>(jsInvoker);
}

} // namespace facebook::react
