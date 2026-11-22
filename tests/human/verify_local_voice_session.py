"""协同验收一次唤醒、连续三句、插话和超时。打印板载证据，由人确认实际听感。"""

import argparse
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "hardware"))
from verify_local_voice import Board


def verify_session(board):
    board.status()
    print("请说「你好小智」。看到角色动作切换后说第一句，等她完整答完。", flush=True)
    first = board.expect(r"DSPET_VOICE event=wake_word state=listening turn=(\d+)", 90).group(1)
    board.expect(fr"DSPET_VOICE_CAPTURE turn={first} samples=\d+ preroll=\d+ capped=0", 14)
    board.expect(fr"DSPET_VOICE event=playback_started state=speaking turn={first}\b", 16)
    print("她说完后直接说第二句。在第二次播报中途，用正常音量插话说「等一下，我问的是明天」。", flush=True)
    second = board.expect(r"DSPET_VOICE event=playback_finished state=listening turn=(\d+)", 30).group(1)
    board.expect(fr"DSPET_VOICE_CAPTURE turn={second} samples=\d+ preroll=\d+ capped=0", 14)
    board.expect(fr"DSPET_VOICE event=playback_started state=speaking turn={second}\b", 16)
    third = board.expect(r"DSPET_VOICE event=barge_in state=recording turn=(\d+)", 14).group(1)
    board.expect(fr"DSPET_VOICE_DRAIN turn={second}\b", 2)
    board.expect(fr"DSPET_VOICE_CAPTURE turn={third} samples=\d+ preroll=\d+ capped=0", 10)
    board.expect(fr"DSPET_VOICE event=playback_started state=speaking turn={third}\b", 12)
    print("听完本次播报后保持安静，等桌宠回到待机。", flush=True)
    after = board.expect(r"DSPET_VOICE event=playback_finished state=listening turn=(\d+)", 28).group(1)
    board.expect(fr"DSPET_VOICE event=listen_timeout state=idle turn={after}\b", 12)
    assert len({first, second, third, after}) == 4
    print("DSPET_HOST_TEST voice_session_serial=pass，实际听感请人工确认。")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    args = parser.parse_args()
    with Board(args.port) as board:
        verify_session(board)
