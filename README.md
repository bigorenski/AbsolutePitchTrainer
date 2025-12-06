# Absolute Pitch Trainer (WIP)

**Reproduction of the training protocol from:**  
*“Learning fast and accurate absolute pitch judgment in adulthood” (2025)*  
https://link.springer.com/article/10.3758/s13423-024-02620-2

This project implements the core training paradigm described in the paper:  
a progressive pitch-naming program that begins with a small subset of notes and gradually expands to all 12 chromatic tones. The training includes timed responses, accuracy/RT tracking, corrective feedback, and structured level progression.

**Status:** Early experiments suggest the training behaves similarly as the one described in the paper, but the implementation is still under active development.

---

## Features

- **Progressive pitch-naming levels** (start with 1 note → expand to all 12)  
- **Timed responses** with accuracy and reaction-time logging  
- **Feedback** during early learning stages  
- **Simple pre-test and post-test modes**  
- **Real piano samples** for more natural tone recognition  
- **Automatic training log and progress file generation**

---

## Audio

The project uses high-quality acoustic piano samples (then converted to mp3) from the  
**University of Iowa Electronic Music Studios (MIS Piano)**:  
https://theremin.music.uiowa.edu/MISpiano.html

All required samples are already included in the repository under: ./pianoSounds

## License

This project is licensed under the GNU General Public License v3.0 (GPL-3.0).

