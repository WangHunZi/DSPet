"""通过板载 Shell 验证聆听超时和设备释放。实机需要保持安静。"""

import argparse
import re
import time

import serial


class Board:
    def __init__(self, port):
        self.connection = serial.Serial(port=None, baudrate=115200, timeout=0.2)
        self.connection.dtr = True
        self.connection.rts = False
        self.connection.port = port
        self.buffer = ""

    def __enter__(self):
        self.connection.open()
        self.command("")
        self.expect(r"xiao>", 10)
        return self

    def __exit__(self, *exc):
        self.connection.close()

    def command(self, text):
        self.connection.write((text + "\r").encode())
        self.connection.flush()

    def expect(self, pattern, timeout=5):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            match = re.search(pattern, self.buffer)
            if match:
                self.buffer = self.buffer[match.end():]
                return match
            chunk = self.connection.read(512).decode(errors="replace")
            self.buffer += chunk
            print(chunk, end="", flush=True)
        raise TimeoutError(f"串口未收到 {pattern!r}，最后输出 {self.buffer[-500:]!r}")

    def status(self, mode="on", state="idle"):
        self.command("dspet voice status")
        self.expect(fr"DSPET_VOICE_STATUS mode={mode} state={state}")


def verify_timeout_and_release(board):
    board.status()
    board.command("dspet voice start")
    started = board.expect(r"DSPET_VOICE event=manual_start state=listening turn=(\d+)")
    turn = started.group(1)
    board.expect(fr"DSPET_VOICE event=listen_timeout state=idle turn={turn}\b", 12)
    board.status()

    board.command("dspet test hardware audio barge-in-quiet")
    board.expect(r"DSPET_ERROR voice_owns_audio")
    board.command("dspet voice disable")
    board.expect(r"DSPET_VOICE event=disabled state=idle")
    board.status(mode="off")
    try:
        board.command("dspet test hardware audio duplex")
        run = board.expect(r"DSPET_TEST id=(\d+) case=audio_duplex state=running").group(1)
        board.expect(fr"DSPET_TEST id={run} case=audio_duplex state=pass error=ESP_OK", 30)
    finally:
        board.command("dspet voice enable")
        board.expect(r"DSPET_VOICE event=ready state=idle", 10)
        board.status()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    args = parser.parse_args()
    with Board(args.port) as board:
        verify_timeout_and_release(board)
    print("DSPET_HOST_TEST voice_timeout_and_release=pass")
