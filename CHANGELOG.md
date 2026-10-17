## 0.1.0

- Initial release.
- `GsctDecoder.probe` / `probeAsync`: parse the stream header and attribute
  metadata (gaussian count, attribute count, version, SH degree) without
  decoding; returns null for non-gsct files.
- `GsctDecoder.decodeToPly`: decode a gsct stream into a standard 3DGS PLY
  (`binary_little_endian`, full SH); runs in a separate isolate and throws
  `GsctException` on failure.
- Android / iOS compile the native code from `src/` at build time; OpenHarmony
  ships prebuilt `libgsct_decoder.so` under `ohos/libs/<abi>/` (see README for
  how to rebuild).
