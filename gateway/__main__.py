"""通过 Mac 终端验证 DSPet 语音问答。"""

import argparse
import getpass
import logging
import os
from pathlib import Path
import sys
import uuid

from .audio import read_wav, wrap_wav
from .conversation import Conversation
from .deepseek import DeepSeek
from .errors import GatewayError
from .server import create_server


def _conversation(ask_key):
    api_key = os.environ.get("DEEPSEEK_API_KEY", "").strip()
    if ask_key:
        if not sys.stdin.isatty():
            raise GatewayError("missing_terminal", 400, "隐藏输入需要连接终端。")
        api_key = getpass.getpass("DeepSeek API Key: ").strip()
    if not api_key:
        raise GatewayError("missing_api_key", 400, "请设置 DEEPSEEK_API_KEY 或传入 --ask-key。")
    return Conversation(DeepSeek(api_key))


def _run_wav(args, conversation):
    args.output_dir.mkdir(parents=True, exist_ok=True)
    session_id = uuid.uuid4().hex
    for turn_id, path in enumerate(args.audio, start=1):
        result = conversation.run(session_id, turn_id, read_wav(path))
        output = args.output_dir / f"reply-{turn_id}.wav"
        output.write_bytes(wrap_wav(result.pcm))
        print(f"第 {turn_id} 轮转写 {result.transcript}", flush=True)
        print(f"回复 {result.reply}", flush=True)
        first = result.asr_seconds + result.chat_seconds + result.tts_first_audio_seconds
        print(
            f"ASR {result.asr_seconds:.2f}s · 对话 {result.chat_seconds:.2f}s · "
            f"TTS {result.tts_seconds:.2f}s · 上游首批音频 {first:.2f}s · "
            f"音频可用 {result.total_seconds:.2f}s\n{output}",
            flush=True,
        )


def _serve(args, conversation):
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(message)s")
    device_token = os.environ.get("DSPET_DEVICE_TOKEN", "")
    with create_server((args.bind, args.port), conversation, device_token) as server:
        host, port = server.server_address
        print(f"DSPet 网关监听 http://{host}:{port}", flush=True)
        server.serve_forever()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ask-key", action="store_true", help="在终端隐藏输入 DeepSeek Key")
    commands = parser.add_subparsers(dest="command", required=True)
    wav = commands.add_parser("wav", help="把一组 WAV 按顺序放在同一会话里")
    wav.add_argument("audio", type=Path, nargs="+", help="16 kHz 单声道 PCM16 WAV")
    wav.add_argument("--output-dir", type=Path, default=Path(".cache/voice-replies"))
    wav.set_defaults(run=_run_wav)
    serve = commands.add_parser("serve", help="启动 DSPet 局域网语音网关")
    serve.add_argument("--bind", default="127.0.0.1")
    serve.add_argument("--port", type=int, default=8765)
    serve.set_defaults(run=_serve)
    args = parser.parse_args(argv)
    try:
        args.run(args, _conversation(args.ask_key))
    except (GatewayError, OSError) as error:
        print(f"网关错误 {error}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 130
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
