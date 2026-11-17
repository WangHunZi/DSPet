#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
export IDF_PATH="${IDF_PATH:-$HOME/esp-idf}"
# The existing SDK was installed with Python 3.12; don't infer its venv
# from whichever Python happens to be first on PATH today.
if [[ -z "${IDF_PYTHON_ENV_PATH:-}" && -d "$HOME/.espressif/python_env/idf6.0_py3.12_env" ]]; then
    export IDF_PYTHON_ENV_PATH="$HOME/.espressif/python_env/idf6.0_py3.12_env"
fi
source "$IDF_PATH/export.sh"
if [[ "${1:-}" == "flash-usb" ]]; then
    port="${2:?Usage: bash scripts/idf.sh flash-usb /dev/cu.usbmodem...}"
    idf.py build
    cd build
    # Native USB-JTAG reset is more reliable than UART-style DTR/RTS here.
    exec "$IDF_PYTHON_ENV_PATH/bin/python" -m esptool --chip esp32s3 \
        --port "$port" --before usb-reset --after hard-reset \
        write-flash @flash_args
fi
exec idf.py "$@"
