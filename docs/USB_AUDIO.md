# USB audio: the microphone, the host port and the power

**The PRD's Phase 1.** ANATOMIX VOXINATOR takes its input from an external USB microphone plugged into the
FM-1's USB port. Before any of the vocal engine can be written, the PRD asks six questions. This file
answers them with the evidence, and says plainly what still needs a physical FM-1.

Short version: **the AC791N's USB controller can act as a host and the SIE already speaks isochronous
endpoints, so a USB Audio Class microphone is feasible. The FM-1's port drives no VBUS, so the microphone
must be powered from outside — a powered hub is required. The driver work is done and tested against the
SIE model; the silicon is not yet verified.**

---

## 1. Does the AC791N USB controller support USB host functionality?

**Yes.** The controller has a host-mode SIE with its own register file, and fm1-base already drives it.

| Evidence | Where |
|---|---|
| The host mode's register set (DEVCTL, TXTYPE, TXINTERVAL, RXTYPE, RXINTERVAL) | `base/bsp/chip/usb_host.c:29-30` |
| The host PHY set-up (D+/D- as inputs with their pull-downs, no pull-ups, the PHY on, the SIE out of reset) | `base/bsp/chip/fm1_usb.h`, `fm1_usb_host_attach()` |
| The register sequences follow the AC79 SDK's own USB-host code (`usb_h_sie_init`, `usb_h_chirp_and_reset`, `usb0_h_ep_read`, `usb_ctlXfer`) | `base/bsp/chip/usb_host.c` (the header comment) |
| A working host port: USB-MIDI controllers and synths, directly or behind one hub | `base/bsp/docs/USB_HOST.md` |

The host port boots as a *device* (so the update paths always work) and the application asks for host mode
from the main loop.

## 2. Does the FM-1's USB connector expose the required host-mode hardware?

**Yes, the same connector does both.** USB 2.0's OTG-style A/B roles share the connector; the FM-1's USB
port drives the PHY as an A-device when it is the host. The board needs no change for the data path.

The one thing the board does **not** have is a VBUS driver — see §4.

## 3. Can USB Audio Class microphones be enumerated?

**In software, yes — the code exists and is tested against the SIE model. On silicon, not yet.**

The port was MIDI-only: `uh_parse_midi()` (in `base/bsp/chip/usb_host_desc.h`) accepted only endpoint
types 2 (bulk) and 3 (interrupt), and the RX service loop used the request/release handshake that
isochronous endpoints do not use.

The isochronous path was added on top of the existing host driver:

* the configuration descriptor is walked for an AudioControl interface and an AudioStreaming interface;
* the AudioStreaming format descriptors give the sample rate, the channel count and the bits per sample;
* the isochronous IN endpoint is armed with the SIE's ISO-mode bit — `S_RXCSR2` bit 6 — the same bit the
  *device-side* USB-audio OUT path already uses (`base/bsp/chip/usb.c`, `uo_ep4_reset()`). The silicon and
  the register file already supported isochronous endpoints; only the host driver never enabled them;
* the endpoint is serviced once per USB frame, and the PCM lands in a lock-free single-producer /
  single-consumer ring.

**The platform's API** (in `base/include/plat.h`, implemented in `base/device/`):

```c
enum { PLAT_MIC_NONE = 0, PLAT_MIC_CONNECTED = 1, PLAT_MIC_STREAMING = 2, PLAT_MIC_ERROR = 3 };
int plat_mic_state(void);
uint32_t plat_mic_rate(void);       /* the device's sample rate (44100 or 48000); 0 = no device */
int plat_mic_channels(void);        /* 1 or 2 */
int plat_mic_bits(void);             /* 16 or 24 */
int plat_mic_read(int32_t *dst, uint32_t n);   /* up to n samples, interleaved as the device sends them;
                                               * the count read is a whole number of frames */
```

`plat_mic_read` is called from the app's audio interrupt: it is a bounded copy from the ring, never a wait,
never an allocation and never a flash read.

**The capture's unit is a frame, not a sample.** A USB Audio Class endpoint sends interleaved frames, and a
half frame would swap the channels of the next read, so the platform rounds the count down to a whole
number of frames (the device's `umic_read` and the host's `plat_mic_read` both do it). The app can then
split the channels without keeping a leftover.

### The app's side: a jitter buffer and a rate conversion

The device's rate is not the engine's rate (48 kHz against 44.1 kHz), the device delivers its frames in
1 ms packets, and the engine consumes 256 frames every 5.8 ms. So the app sits between them with a
**jitter buffer**: 1024 frames, one channel per row, in the platform's RAM pool.

* the app drains the capture at every block and pushes the frames into the buffer;
* the app reads the buffer at the **device's rate**, so a 48 kHz microphone is read 48/44.1 = 1.0884
  frames per engine frame; the frames in between are interpolated linearly;
* the buffer's level is kept at its target by a small correction to the read rate: the device's crystal and
  the engine's clock are never exactly the same, and without the correction the buffer would fill up or run
  dry over a few minutes. The correction is at most 0.3 % — a tenth of a cent — and it is steered on the
  *averaged* level, so the device's 1 ms packets do not put a wobble on the pitch;
* if the device falls behind, the app **holds its place** and repeats the last value rather than dropping
  to zero: a hold is quiet, a zero is a click.

The result is measured in the host simulator (`app/tests/audio.txt`): a 220 Hz sine fed in as mono at
44.1 kHz, as stereo at 48 kHz and as mono at 48 kHz comes out as 220.0 Hz in all three cases — the
converter's pitch error is under a tenth of a cent.

## 4. Can microphone power be supplied?

**No. This is the hard constraint of Phase 1, and it is a hardware fact, not a software one.**

`base/bsp/chip/fm1_usb.h` says it outright, in the host-mode set-up:

> *"Nothing here drives VBUS: the port needs power from outside."*

There is no USB power switch in the FM-1's board files, and the "VBUS" bit in `USB_CON0` is the controller's
*VBUS-valid* status input, not a power driver.

**Consequence:** a bus-powered USB microphone plugged straight into the FM-1 **will not work** — it gets no
5 V. The instrument needs one of:

1. **A powered USB hub** between the microphone and the FM-1 (the host driver supports one hub, up to
   seven ports, and it is the arrangement the existing MIDI host already documents). *This is the
   recommended route: it is one cheap accessory and it needs no hardware change.*
2. A microphone with its own power and a self-powered USB interface — rare, and many of them still want VBUS
   for the PHY.
3. A hardware revision of the FM-1 with a VBUS switch and the current limit for it. Out of scope for the
   firmware, and the board is not ours to change.

The base's own MIDI host documentation already tells its users the same thing ("The PHY setup does not
supply 5 V VBUS. Use a powered hub or powered adapter"), so this is the platform's established position.

## 5. Can isochronous USB audio transfers be received reliably?

**The mechanism is implemented and exercised in the SIE model. The silicon is untested.**

What the model proves (the host test drives the production USB source against a modelled SIE under
ASan/UBSan):

* the descriptor walk finds the UAC microphone's streaming interface and its isochronous IN endpoint;
* the endpoint is armed with the ISO bit and the per-frame service reads the packets in order;
* the PCM reaches `plat_mic_read()` in order, and the ring survives a short packet, a full packet and an
  overrun;
* a disconnect flushes the ring and the state returns to `PLAT_MIC_NONE`; a reconnect starts cleanly.

What the model cannot prove, and what a person with an FM-1 must test:

* that the real silicon delivers the packets at the rate the descriptor claims;
* the jitter of the USB clock against the FM-1's 44.1 kHz audio clock, and the resampler's quality when the
  microphone runs at 48 kHz;
* that the transfer really is isochronous on the wire with the real microphone (bus errors, retries);
* the interrupt deadline under sustained traffic: the host service runs in the TIMER5 interrupt, and the
  audio render runs in the ALNK0 interrupt. The two must not starve each other.

## 6. Can the firmware process incoming PCM without disrupting the existing audio engine?

**Yes, by construction.**

* The capture is a ring buffer written by the USB host's timer interrupt and read by the app's audio
  interrupt. The two sides never block each other: the reader takes whatever is there, and an empty ring
  means silence (with the overrun counted).
* The app's conversion stage (the device's rate and depth to the engine's 44.1 kHz float) is the app's own
  code; the platform delivers the samples as the device sent them. It is a jitter buffer with a rate
  conversion in it (see above): the buffer is in the platform's RAM pool, it never allocates, and its
  latency is about 3 ms of buffer plus the engine's block.
* The render's budget is unchanged: the engine's block time is unchanged by the capture, and the platform's
  `plat_cpu_pct()` / `plat_xruns()` show it.

---

## What a person with an FM-1 must test

The list is short and specific. Nothing here needs a firmware change — it needs an FM-1, a powered hub and
a USB microphone.

1. **The power.** Plug a powered hub into the FM-1 and a microphone into the hub. Does the microphone light
   up? (If not: the hub is not powering its ports, or the cable is charge-only.)
2. **The enumeration.** Does the host driver find the microphone? The platform reports it in the app's
   INPUT screen: the device's state, its rate, its channels and its bits.
3. **The stream.** Sing into the microphone and watch the input's level move. The anatomy's lungs must
   breathe with the signal.
4. **The rate.** A 48 kHz microphone must be resampled to 44.1 kHz without an audible pitch error — check a
   sustained note against a reference.
5. **The endurance.** Leave it running for ten minutes. Count the overruns (the platform exposes them);
   the render must not drop a block (no clicks, no `plat_xruns()`).
6. **The unplug.** Pull the microphone mid-word. The state must go to "no microphone", the input to silence,
   and the anatomy must fall back to its idle breathing. Plug it back in: the state must return to
   streaming within a second.
7. **The hub.** Repeat 1-6 behind a hub. The host driver supports one hub; a nested hub is not supported.
8. **The interference.** With the microphone running, record the FM-1's own output to the computer over USB.
   The FM-1 is a USB *device* to the computer and a USB *host* to the microphone at the same time — the two
   roles share the port's PHY, and this is the one thing the model cannot check at all.

---

## The honest summary

| PRD question | Answer |
|---|---|
| 1. USB host functionality | Yes |
| 2. Host-mode hardware on the connector | Yes (same connector) |
| 3. UAC microphones enumerated | The code exists and is model-tested; the silicon is unverified |
| 4. Microphone power | **No — a powered hub is required** |
| 5. Isochronous transfers | Implemented; model-tested only |
| 6. Without disrupting the audio engine | Yes, by construction (a ring, never a wait) |

Phase 1 is therefore **conditionally passed**: the firmware work is done and testable on this machine; the
hardware acceptance needs an FM-1, a powered hub and a microphone.
