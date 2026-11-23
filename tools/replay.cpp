// Runs a recorded CSV (from tools/monitor.py --raw) through the exact same
// feature extractor + detector as the firmware, on the Mac.
//
//   make -C tools replay && tools/replay recordings/raw/shake1.csv
//   tools/replay rec.csv --quiet          # events only
//   tools/replay rec.csv --gyr 80 --rhythm 0.25   # try other thresholds
//
// CSV lines: ms,ax,ay,az,gx,gy,gz  (g and deg/s). Lines starting with # are skipped.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "detector.h"
#include "features.h"

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: replay file.csv [--quiet] [--acc g] [--gyr dps] [--rhythm r] [--lo hz]\n");
    return 1;
  }
  DetectorConfig cfg;
  bool quiet = false;
  for (int i = 2; i < argc; i++) {
    if (!strcmp(argv[i], "--quiet")) quiet = true;
    else if (!strcmp(argv[i], "--acc") && i + 1 < argc) cfg.acc_rms_min = atof(argv[++i]);
    else if (!strcmp(argv[i], "--gyr") && i + 1 < argc) cfg.gyr_rms_min = atof(argv[++i]);
    else if (!strcmp(argv[i], "--rhythm") && i + 1 < argc) cfg.rhythm_min = atof(argv[++i]);
    else if (!strcmp(argv[i], "--lo") && i + 1 < argc) cfg.band_lo_hz = atof(argv[++i]);
  }
  FILE* f = fopen(argv[1], "r");
  if (!f) {
    perror(argv[1]);
    return 1;
  }
  FeatureExtractor fx;
  Detector det(cfg);
  char line[256];
  long n = 0;
  int events = 0;
  while (fgets(line, sizeof(line), f)) {
    if (line[0] == '#' || line[0] < '0' || line[0] > '9') continue;
    unsigned long ms;
    Sample s;
    if (sscanf(line, "%lu,%f,%f,%f,%f,%f,%f", &ms, &s.ax, &s.ay, &s.az, &s.gx, &s.gy, &s.gz) != 7) continue;
    n++;
    if (!fx.push(s)) continue;
    // Device timestamps arrive in 1 s batches; use sample count for time.
    const float t = float(n) / FeatureExtractor::kFs;
    const uint32_t now_ms = uint32_t(t * 1000);
    const Features& ft = fx.features();
    DetEvent e = det.update(ft, now_ms);
    if (!quiet)
      printf("%6.1fs a%.2f(%.2f) g%4.0f(%4.0f) fa%4.1f fg%4.1f r%.2f/%.2f %s c%u %s\n", t, ft.acc_rms,
             ft.acc_rms_1s, ft.gyr_rms, ft.gyr_rms_1s, ft.acc_dom_hz, ft.gyr_dom_hz, ft.acc_rhythm,
             ft.gyr_rhythm, det.isCandidate(ft) ? "CAND" : "    ", det.candidateCount(),
             stateName(det.state()));
    if (e.type != EventType::None) {
      events++;
      printf("%6.1fs >>> %s conf %u%% %us %.1fHz\n", t, eventName(e.type), e.confidence, e.duration_s,
             e.dom_hz);
    }
  }
  fclose(f);
  printf("%ld samples (%.0f s), %d events\n", n, float(n) / FeatureExtractor::kFs, events);
  return 0;
}
