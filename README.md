# EH400-MIDI
A MIDI retrofit for the Electro Harmonix EH400 Mini Synthesizer

Uses an Arduino Pro Mini and multiplexers to simulate pressing the "keyboard". The software starts in "midi learn" mode, and will respond to the first channel it receives a message on.

All four octaves are supported over MIDI by using an analog switch that automatically changes octaves. Velocity over MIDI is supported by generating a control voltage via PWM. A fake portamento is implemented using fast glissando (set via CC #5 - Portamento Time).

#### SCHEMATIC ####
![Schematic](/img/EH400-MIDI%20schematic.png)

#### WIRING POINTS ####
![Wiring](/img/EH400-MIDI%20wiring.png)
