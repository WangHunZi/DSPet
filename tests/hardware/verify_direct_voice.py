"""经 USB 串口验证板端 Wi-Fi 扫描与直连语音。"""

import argparse
from contextlib import redirect_stdout
from getpass import getpass
from io import StringIO
import json
import sys
import time

from verify_local_voice import Board


def run_scan(board, prefix):
    board.command(f"dspet test hardware network scan {json.dumps(prefix, ensure_ascii=False)}")
    started = board.expect(r"DSPET_TEST id=(\d+) case=network_scan state=running")
    return wait_result(board, started.group(1), "network_scan", 30)


def wait_result(board, run_id, name, seconds):
    result = board.expect(fr"DSPET_TEST id={run_id} case={name} state=(pass|fail) error=[^\r\n]+", seconds)
    return result.group(1) == "pass"


def voice_mode(board):
    board.command("dspet voice status")
    status = board.expect(r"DSPET_VOICE_STATUS mode=(on|off) state=\w+")
    return status.group(1)


def check_private_input(board, command, rejected=r"DSPET_ERROR invalid_network_input"):
    dummy = "dspet-private-input-check!"
    output = StringIO()
    with redirect_stdout(output):
        board.command(command)
        board.expect(r"DSPET_INPUT field=wifi_password")
        board.command(dummy)
        board.expect(r"DSPET_INPUT field=deepseek_key")
        board.connection.write(b"\x03")
        board.connection.flush()
        board.expect(rejected)
    if dummy in output.getvalue() or dummy in board.buffer:
        raise RuntimeError("板载终端回显了测试字符，已停止凭据传输。")


def send_private_input(board, command, password, key, accepted):
    output = StringIO()
    try:
        with redirect_stdout(output):
            board.command(command)
            board.expect(r"DSPET_INPUT field=wifi_password")
            board.command(password)
            board.expect(r"DSPET_INPUT field=deepseek_key")
            board.command(key)
            started = board.expect(accepted)
    except TimeoutError:
        raise RuntimeError("板载凭据输入未完成。") from None
    print(started.group(), flush=True)
    return started


def run_direct(board, ssid, password, key, short_reply):
    command = f"dspet test hardware network direct {json.dumps(ssid, ensure_ascii=False)} {int(time.time())}"
    if short_reply:
        command += " short"
    restore = voice_mode(board) == "on"
    if restore:
        board.command("dspet voice disable")
        board.expect(r"DSPET_VOICE event=disabled state=idle")
    try:
        check_private_input(board, command)
        started = send_private_input(board, command, password, key,
                                     r"DSPET_TEST id=(\d+) case=network_direct state=running")
        run_id = started.group(1)
        return wait_result(board, run_id, "network_direct", 110)
    finally:
        if restore:
            board.command("dspet voice enable")
            board.expect(r"DSPET_VOICE event=ready state=idle", 12)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    actions = parser.add_subparsers(dest="mode", required=True)
    scan = actions.add_parser("scan", help="扫描匹配前缀的 2.4 GHz 网络")
    scan.add_argument("prefix")
    direct = actions.add_parser("direct", help="隐藏输入凭据并触发固定问题的直连播放")
    direct.add_argument("ssid")
    direct.add_argument("--short", action="store_true", help="用一字回复排查音频长连接")
    args = parser.parse_args()
    if args.mode == "scan":
        with Board(args.port) as board:
            return 0 if run_scan(board, args.prefix) else 1

    if not sys.stdin.isatty():
        parser.error("直连测试需要终端隐藏输入。")
    password = getpass("Wi-Fi password: ")
    key = getpass("DeepSeek API Key: ").strip()
    if not 8 <= len(password.encode()) <= 64 or not 1 <= len(key.encode()) <= 128:
        parser.error("凭据长度无效。")
    if any(ord(char) < 32 or ord(char) == 127 for char in password + key):
        parser.error("凭据包含控制字符。")
    with Board(args.port) as board:
        return 0 if run_direct(board, args.ssid, password, key, args.short) else 1


if __name__ == "__main__":
    raise SystemExit(main())
