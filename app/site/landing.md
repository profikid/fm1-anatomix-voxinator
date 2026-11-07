# ANATOMIX

ANATOMIX VOXINATOR turns the M-VAVE FM-1 into a vocal processor: a USB microphone goes in, and a
corrected, harmonised, formant-shifted and effected voice comes out of the pedal. The screen is a
cross-section of a human head and throat — the lungs, the trachea, the larynx, the pharynx, the tongue,
the nasal cavity — and it moves with your voice and with every knob you turn. The folds vibrate at the
pitch you sing. The tract shortens when you shift the formants. The harmonies arrive as ghosted
overlays of the same anatomy.

![ANATOMIX VOXINATOR's home screen: the anatomical cutaway with the lungs, the larynx and the airflow, the detected note in the header](manual/img/screen-home.png "The anatomical cutaway")

Two engines. **MULTIVOICE** keeps the original and adds up to three voices of its own — a third above, a
fifth above, an octave below, detuned, formant-shifted, placed left and right. **GLIDE** is the other
half: the detected pitch is pulled to the nearest note of the key you chose, with the snap, the slide and
the human wobble under your hands.

Sixteen factory presets (ROBOT BASTARD, TINY RAT, DEMON, ALIEN CHOIR, ROTTERDAM 1995 and a dozen more),
sixteen of your own, four effects you can reorder, and a microphone that has to be on a **powered hub**:
the FM-1's USB port does not hand out power.

Version {{VERSION}}. Installing is at your own risk; the installer can put M-VAVE's own firmware back.

- [Try it in the browser](emu/): the same code the FM-1 runs, with sound. No FM-1 needed.
- [Install](install/): from Chrome or Edge, with the FM-1 connected by USB.
- [Manual](manual/): the microphone, the two engines, the effects, the presets.
- [Download {{PKG}}](firmware/{{PKG}}): for the command-line installer, `python3 base/tools/fm1_install.py {{PKG}}`.

Built on fm1-base and felucca-bsp, from Felucca by Leo Kuroshita, Hügelton Instruments. MPL-2.0.
M-VAVE and FM-1 are trademarks of their owners. ANATOMIX VOXINATOR is not affiliated with M-VAVE.
