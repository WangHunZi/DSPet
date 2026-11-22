"""用板载已有 PCM 验证直连 ASR、流式播放和续听。"""

import argparse
from getpass import getpass
import json
import sys
import time

from verify_direct_voice import check_private_input, send_private_input
from verify_local_voice import Board


def select_offline(board):
    board.command("dspet voice status")
    mode = board.expect(r"DSPET_VOICE_STATUS mode=(on|off) state=\w+")
    if mode.group(1) == "off":
        return
    board.command("dspet voice disable")
    disabled = board.expect(r"DSPET_VOICE event=disabled state=idle|action=disable result=ESP_ERR_INVALID_STATE", 25)
    if "ESP_ERR" in disabled.group():
        raise RuntimeError("板载语音仍在启动或释放。")


def enable_direct(board, ssid, password, key):
    select_offline(board)
    for _ in range(2):
        command = f"dspet voice direct {json.dumps(ssid, ensure_ascii=False)} {int(time.time())}"
        check_private_input(board, command, r"DSPET_VOICE_COMMAND action=direct result=ESP_ERR_INVALID_ARG")
        send_private_input(board, command, password, key, r"DSPET_VOICE_COMMAND action=direct result=ESP_OK")
        result = board.expect(r"DSPET_DIRECT_VOICE ready=1|DSPET_VOICE event=disabled state=idle", 25)
        if "ready=1" in result.group():
            return
    raise RuntimeError("板端两次均未准备好直连会话。")


def verify_fixture(board):
    board.command("dspet voice fixture")
    start = board.expect(r"DSPET_VOICE event=fixture_start state=listening turn=(\d+)")
    turn = start.group(1)
    asr = board.expect(fr"DSPET_DIRECT_ASR turn={turn} elapsed_us=\d+ text=\S+|DSPET_VOICE event=direct_error", 80)
    if "direct_error" in asr.group():
        raise RuntimeError("板载会话在取得 ASR 结果前失败。")
    started = False
    buffered = None
    gaps = None
    patterns = [
        fr"DSPET_DIRECT_STREAM turn={turn} before_end=([01])",
        fr"DSPET_VOICE event=playback_started state=speaking turn={turn}\b",
        fr"DSPET_DIRECT_END turn={turn} result=\w+ cancelled=\d+ samples=\d+ gaps=\d+",
    ]
    for _ in patterns:
        event = board.expect("|".join(patterns), 70).group()
        if event.startswith("DSPET_DIRECT_STREAM"):
            buffered = event.endswith("before_end=0")
        elif event.startswith("DSPET_VOICE"):
            started = True
        else:
            if "result=ESP_OK cancelled=0" not in event:
                raise RuntimeError("自动用例播放未完整结束。")
            gaps = int(event.rsplit("gaps=", 1)[1])
    if not started or buffered is None or gaps is None or (buffered and gaps != 0):
        raise RuntimeError("串口未确认播放策略、收尾和缓冲结果。")
    print(f"DSPET_HOST_METRIC direct_fixture buffered={int(buffered)} gaps={gaps}", flush=True)
    board.expect(r"DSPET_VOICE event=playback_finished state=listening turn=\d+", 40)
    board.expect(r"DSPET_VOICE event=listen_timeout state=idle turn=\d+", 12)
    print("DSPET_HOST_TEST direct_fixture=pass", flush=True)


def verify_fixture_barge_in(board):
    board.command("dspet voice fixture")
    first = board.expect(r"DSPET_VOICE event=fixture_start state=listening turn=(\d+)").group(1)
    asr = board.expect(fr"DSPET_DIRECT_ASR turn={first} elapsed_us=\d+ text=\S+|DSPET_VOICE event=direct_error", 80)
    if "direct_error" in asr.group():
        raise RuntimeError("受控插话前的 ASR 请求失败。")
    playback = fr"DSPET_VOICE event=playback_started state=speaking turn={first}\b"
    ended = fr"DSPET_DIRECT_END turn={first} result=(?P<result>\w+) cancelled=(?P<cancelled>[01])"
    event = board.expect(f"{playback}|{ended}", 75)
    network = event if event.group("result") else None
    if network:
        board.expect(playback, 8)

    board.command("dspet voice fixture")
    interrupted = r"DSPET_VOICE event=barge_in state=listening turn=(?P<next_turn>\d+)"
    event = board.expect(f"{interrupted}|{ended}", 6)
    if event.group("result"):
        network = event
        event = board.expect(interrupted, 3)
    second = event.group("next_turn")
    assert first != second

    drain = fr"DSPET_VOICE_DRAIN turn={first}\b"
    if network is None:
        event = board.expect(f"{drain}|{ended}", 6)
        if event.group("result"):
            network = event
            board.expect(drain, 3)
        else:
            network = board.expect(ended, 6)
    else:
        board.expect(drain, 3)

    cancelled = network.group("cancelled") == "1"
    if not cancelled and network.group("result") != "ESP_OK":
        raise RuntimeError("旧回合的网络响应未正常结束。")
    print(f"DSPET_HOST_METRIC barge_during_download={int(cancelled)}", flush=True)
    asr = board.expect(fr"DSPET_DIRECT_ASR turn={second} elapsed_us=\d+ text=\S+|DSPET_VOICE event=direct_error", 80)
    if "direct_error" in asr.group():
        raise RuntimeError("旧回合已排空和关闭，下一轮 ASR 请求失败。")
    board.expect(fr"DSPET_VOICE event=playback_started state=speaking turn={second}\b", 50)
    board.expect(r"DSPET_VOICE event=playback_finished state=listening turn=\d+", 50)
    board.expect(r"DSPET_VOICE event=listen_timeout state=idle turn=\d+", 13)
    print("DSPET_HOST_TEST direct_fixture_barge_in=pass", flush=True)


def verify_always_listening(board):
    board.command("dspet voice always on")
    turn = board.expect(r"DSPET_VOICE event=always_start state=listening turn=(\d+)").group(1)
    try:
        # 跨过普通聆听的 8 秒超时，免唤醒应继续等本句。
        time.sleep(9)
        board.command("dspet voice status")
        board.expect(r"DSPET_VOICE_STATUS mode=on state=listening always=1")

        # 现有板载 PCM 是桌宠自述的头两秒，应有转写但无需回应。
        board.command("dspet voice fixture")
        board.expect(fr"DSPET_VOICE event=fixture_end state=waiting turn={turn}\b")
        board.expect(fr"DSPET_DIRECT_ASR turn={turn} elapsed_us=\d+ text=\S+", 75)
        board.expect(r"DSPET_DIRECT_HTTP stage=chat status=200", 75)
        end = board.expect(fr"DSPET_DIRECT_END turn={turn} result=\w+ cancelled=\d+ samples=\d+ gaps=\d+", 40)
        if "result=ESP_ERR_NOT_FOUND cancelled=0 samples=0" not in end.group():
            raise RuntimeError(f"免唤醒未静默忽略板载自述，实际结果为 {end.group()}")
        board.expect(r"DSPET_VOICE event=direct_ignored state=listening turn=\d+")
    finally:
        board.command("dspet voice always off")
        board.expect(r"DSPET_VOICE event=always_stop state=idle turn=\d+")
        board.command("dspet voice status")
        board.expect(r"DSPET_VOICE_STATUS mode=on state=idle always=0")
    print("DSPET_HOST_TEST direct_always_listening=pass", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--ssid", required=True)
    parser.add_argument("--keep-enabled", action="store_true", help="结束后继续保持直连，供人工测试")
    parser.add_argument("--setup-only", action="store_true", help="只完成配网和启动，留给人工测试")
    parser.add_argument("--exercise-cancel", action="store_true", help="再验证一轮受控插话和下一次 ASR")
    parser.add_argument("--exercise-always", action="store_true", help="保持安静，验证免唤醒不超时以及忽略板载自述")
    args = parser.parse_args()
    if not sys.stdin.isatty():
        parser.error("需要在终端隐藏输入凭据。")
    password = getpass("Wi-Fi password: ")
    key = getpass("DeepSeek API Key: ").strip()
    with Board(args.port) as board:
        enable_direct(board, args.ssid, password, key)
        try:
            if not args.setup_only:
                verify_fixture(board)
                if args.exercise_cancel:
                    verify_fixture_barge_in(board)
                if args.exercise_always:
                    verify_always_listening(board)
        finally:
            if not args.keep_enabled:
                select_offline(board)
                board.command("dspet voice enable")
                board.expect(r"DSPET_VOICE event=ready state=idle")


if __name__ == "__main__":
    main()
