"""记录真人开口、直播播报停止和下一轮 ASR 的板载证据。"""

import argparse
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "hardware"))
from verify_local_voice import Board


def expect_asr(board, turn):
    result = board.expect(fr"DSPET_DIRECT_ASR turn={turn} elapsed_us=\d+ text=\S+|DSPET_VOICE event=direct_error", 80)
    if "direct_error" in result.group():
        raise RuntimeError("板载网络回合在 ASR 阶段失败。")


def verify(board):
    board.status()
    print("串口已就绪。说「你好小智」，然后请她讲一个小故事。", flush=True)
    first = board.expect(r"DSPET_VOICE event=wake_word state=listening turn=(\d+)", 150).group(1)
    board.expect(fr"DSPET_VOICE_CAPTURE turn={first} samples=\d+", 16)
    expect_asr(board, first)
    board.expect(fr"DSPET_VOICE event=playback_started state=speaking turn={first}\b", 50)
    print("听她说两三秒，再插话说「等一下，一加一等于几？」", flush=True)
    second = board.expect(r"DSPET_VOICE event=barge_in state=listening turn=(\d+)", 30).group(1)
    board.expect(fr"DSPET_VOICE_DRAIN turn={first}\b", 2)
    expect_asr(board, second)
    board.expect(fr"DSPET_VOICE event=playback_started state=speaking turn={second}\b", 45)
    print("听她完整回答后保持安静，等回到待机。", flush=True)
    board.expect(r"DSPET_VOICE event=playback_finished state=listening turn=\d+", 45)
    board.expect(r"DSPET_VOICE event=listen_timeout state=idle turn=\d+", 13)
    print("DSPET_HOST_TEST direct_barge_in_serial=pass", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    args = parser.parse_args()
    with Board(args.port) as board:
        verify(board)
