# RX3 browser waveform response findings

An isolated execution pass on stock RX3 1.20 `rbp` (SHA256 `3f0a1a9c4d107fcb856eb59bd77b39139b239b111f431d40d11b5c7e66dffddb`) exercised56 waveform-response/row-registration cases and32 high-word identity-bit comparisons. These are behavioral observations for future BiteDJ work, not copied implementation or a live-device performance result.

The message handler at0x00134e74 checks a64-entry cancellation table and the pending waveform identity. The consumer at0x00134f98 then attempts registration at0x0010af78 against one of13 current visible records. All13 destinations were tested: a row must still have the matching identity and state2. Changed identities or nonwaiting states prevent registration. An admitted response is consumed and cleared even if no visible row still matches.

A populated response changes the matched waveform state to3 and sets row/list dirty flags. An all-zero payload changes it to1 without those dirty flags. These numeric states are observed behavior, not recovered enum declarations. Only the first matching row is updated when two visible rows share the tested identity.

The browser comparator at0x0010f878 checks high-word byte1 plus the low word; the common comparator at0x00186754 checks all24 meaningful high-word bits plus the low word. A different request number alone did not reject a matching, uncanceled synthetic response. Upstream constraints and the omitted byte meanings are not established, so these observations do not prove a live collision bug.

For BiteDJ's future browser waveforms, preserve a complete source/library and track identity together with analysis/render parameters and a request generation. Check them both when receiving the result and when publishing to the current row. Retire empty, failed and stale results explicitly. Do not copy the RX3's reduced comparison without knowing its assumptions.

The actual ARM instructions performed the message, cancel lookup/removal, identity comparisons, state gates and row writes. Semaphore operations, row-pointer getters, serial diagnostics and next-request discovery were controlled providers. Payload words were copied tokens, not rendered or dereferenced waveform data. No live threads, network, UI drawing or audio playback ran. This note does not implement a new BiteDJ waveform column or resolve the separate physical-panel lag investigation.
