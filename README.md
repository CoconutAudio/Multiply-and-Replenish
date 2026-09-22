# Multiply and Replenish

![Screenshot](screenshot.png)

A neural vocal pitch editor that lets you create multiple instances of a vocal take and manipulate each differently to create realistic harmonies. Multiply and Replenish supports ARA.

## Build from Source

1. **Clone the repository:**
```bash
git clone https://github.com/vivekvjyn/MultiplyAndReplenish.git
cd MultiplyAndReplenish
git submodule update --recursive
```
2. **Build the plugin:**
```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## License
This project is licensed under the GNU General Public License. See the [LICENSE](LICENSE) for details.
