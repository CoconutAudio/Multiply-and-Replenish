# Multiply and Replenish

![Screenshot](screenshot.png)

A vocal pitch editor: open a take, see the melody as notes on a piano roll, move them, and hear the
take sung back at the pitches you put them on. Each tab is another edit of the same take, and they
all play together, which is how a harmony is made. A VST3 plug-in and a standalone app, built from
the same processor: the standalone opens audio files and saves projects, and the plug-in takes its
audio from the host through ARA.

## Build from Source

1. **Clone the repository:**
```bash
git clone https://github.com/vivekvjyn/MultiplyAndReplenish.git
cd MultiplyAndReplenish
git submodule update --init --recursive --depth 1 libs/JUCE libs/googletest libs/ARA_SDK
```
2. **Build the plugin:**
```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## License
This project is licensed under the GNU General Public License. See the [LICENSE](LICENSE) for details.
