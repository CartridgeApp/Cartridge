import { StatusBar } from "expo-status-bar";
import { useEffect, useState } from "react";
import { Pressable, StyleSheet, Text, View } from "react-native";

import Cartridge, { CartridgeEvent } from "./src/CartridgeClient";
import RetroArchSurface from "./src/specs/RetroArchSurfaceNativeComponent";

/**
 * Cartridge M1 exit-criteria demo: <RetroArchSurface/> composited under a
 * transparent React overlay with a Pause button that actually pauses the
 * running content via the TurboModule (or the mock, in Expo Go/web -- see
 * src/CartridgeClient.ts). Screenshot is wired the same way to exercise all
 * 3 hand-written calls; a running log shows the 1 event (native "message"
 * notifications, e.g. "Paused"/"Screenshot saved to ...").
 */
export default function App() {
  const [paused, setPaused] = useState(false);
  const [busy, setBusy] = useState(false);
  const [log, setLog] = useState<string[]>([]);

  useEffect(() => {
    return Cartridge.addEventListener((event: CartridgeEvent) => {
      setLog((prev) => [`${event.name}: ${event.detail}`, ...prev].slice(0, 8));
    });
  }, []);

  const togglePause = async () => {
    setBusy(true);
    try {
      const result = await Cartridge.setPaused(!paused);
      setPaused(result === "1");
    } finally {
      setBusy(false);
    }
  };

  const screenshot = async () => {
    setBusy(true);
    try {
      await Cartridge.takeScreenshot();
    } finally {
      setBusy(false);
    }
  };

  return (
    <View style={styles.container}>
      <RetroArchSurface style={StyleSheet.absoluteFill} />

      <View style={styles.overlay} pointerEvents="box-none">
        {Cartridge.isMock && (
          <Text style={styles.mockBadge}>mock FFI (no native module)</Text>
        )}

        <View style={styles.buttonRow}>
          <Pressable
            style={styles.button}
            onPress={togglePause}
            disabled={busy}
          >
            <Text style={styles.buttonText}>{paused ? "Resume" : "Pause"}</Text>
          </Pressable>
          <Pressable style={styles.button} onPress={screenshot} disabled={busy}>
            <Text style={styles.buttonText}>Screenshot</Text>
          </Pressable>
        </View>

        <View style={styles.log}>
          {log.map((line, i) => (
            <Text key={i} style={styles.logLine}>
              {line}
            </Text>
          ))}
        </View>
      </View>

      <StatusBar style="auto" />
    </View>
  );
}

const styles = StyleSheet.create({
  container: {
    flex: 1,
    backgroundColor: "#000",
  },
  overlay: {
    flex: 1,
    justifyContent: "flex-end",
    padding: 24,
  },
  mockBadge: {
    color: "#ffcc00",
    marginBottom: 8,
  },
  buttonRow: {
    flexDirection: "row",
    gap: 12,
  },
  button: {
    backgroundColor: "rgba(255,255,255,0.85)",
    paddingVertical: 12,
    paddingHorizontal: 20,
    borderRadius: 8,
  },
  buttonText: {
    fontWeight: "600",
  },
  log: {
    marginTop: 16,
  },
  logLine: {
    color: "#0f0",
    fontFamily: "monospace",
    fontSize: 12,
  },
});
