#include "settings.h"

#include <Preferences.h>

#include "config.h"

namespace settings {

S load() {
  Preferences p;
  p.begin("lora", true);
  S s;
  s.nodeId = p.getUChar("node", cfg::kDefaultNodeId);
  s.latencyMs = p.getInt("latency", 0);
  s.windowOn = p.getBool("win_on", false);
  s.winStartMin = p.getShort("win_a", 0);
  s.winEndMin = p.getShort("win_b", 0);
  s.fileCounter = p.getUInt("file_n", 1);
  if (p.getString("session", s.session, sizeof s.session) == 0) s.session[0] = '\0';
  if (p.getString("stop", s.stop, sizeof s.stop) == 0) s.stop[0] = '\0';
  s.heightCm = p.getUShort("h_cm", 130);
  s.mode = p.getUChar("mode", 0);
  p.end();
  return s;
}

void save(const S& s) {
  Preferences p;
  p.begin("lora", false);
  p.putUChar("node", s.nodeId);
  p.putInt("latency", s.latencyMs);
  p.putBool("win_on", s.windowOn);
  p.putShort("win_a", s.winStartMin);
  p.putShort("win_b", s.winEndMin);
  p.putUInt("file_n", s.fileCounter);
  p.putString("session", s.session);
  p.putString("stop", s.stop);
  p.putUShort("h_cm", s.heightCm);
  p.putUChar("mode", s.mode);
  p.end();
}

}  // namespace settings
