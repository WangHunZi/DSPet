"""网关与板端约定的单声道 PCM16 格式。"""

from contextlib import closing
from io import BytesIO
import os
import selectors
import subprocess
from threading import Thread
import wave

from .errors import GatewayError

DEVICE_RATE = 16_000
SYNTHESIS_RATE = 24_000
SAMPLE_BYTES = 2
MAX_INPUT_BYTES = 15 * DEVICE_RATE * SAMPLE_BYTES
MAX_REPLY_BYTES = 120 * SYNTHESIS_RATE * SAMPLE_BYTES


def validate_input(pcm):
    if not pcm or len(pcm) % SAMPLE_BYTES:
        raise GatewayError("invalid_audio", 400, "录音须包含完整的 PCM16 样本。")
    if len(pcm) > MAX_INPUT_BYTES:
        raise GatewayError("audio_too_large", 413, "单次录音最长为 15 秒。")


def wrap_wav(pcm):
    result = BytesIO()
    with wave.open(result, "wb") as wav:
        wav.setparams((1, SAMPLE_BYTES, DEVICE_RATE, 0, "NONE", "not compressed"))
        wav.writeframes(pcm)
    return result.getvalue()


def read_wav(path):
    try:
        with wave.open(str(path), "rb") as wav:
            if (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) != (1, SAMPLE_BYTES, DEVICE_RATE):
                raise GatewayError("invalid_audio_format", 400, "WAV 须为 16 kHz、单声道、16 位 PCM。")
            frames = wav.getnframes()
            if frames > MAX_INPUT_BYTES // SAMPLE_BYTES:
                raise GatewayError("audio_too_large", 413, "单次录音最长为 15 秒。")
            pcm = wav.readframes(frames)
            if len(pcm) != frames * SAMPLE_BYTES:
                raise GatewayError("invalid_audio", 400, "WAV 的音频数据不完整。")
    except (OSError, EOFError, wave.Error) as error:
        raise GatewayError("invalid_audio", 400, "无法读取 PCM WAV。") from error
    validate_input(pcm)
    return pcm


def resample_reply(blocks, remaining):
    """保持同一转换进程处理整段音频，逐块交付完整的设备侧样本。"""
    command = [
        "ffmpeg", "-nostdin", "-v", "error",
        "-probesize", "32", "-analyzeduration", "0",
        "-f", "s16le", "-ac", "1", "-ar", str(SYNTHESIS_RATE), "-i", "pipe:0",
        "-ar", str(DEVICE_RATE), "-f", "s16le", "-flush_packets", "1", "pipe:1",
    ]
    env = {name: value for name, value in os.environ.items()
           if name not in ("DEEPSEEK_API_KEY", "DSPET_DEVICE_TOKEN")}
    try:
        process = subprocess.Popen(
            command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, env=env,
        )
    except FileNotFoundError as error:
        raise GatewayError("ffmpeg_missing", 503, "Mac 网关需要 ffmpeg 转换采样率。") from error

    failures = []

    def feed():
        try:
            with closing(blocks):
                for block in blocks:
                    process.stdin.write(block)
                    process.stdin.flush()
        except Exception as error:
            failures.append(error)
        finally:
            try:
                process.stdin.close()
            except OSError as error:
                failures.append(error)

    writer = Thread(target=feed, daemon=True, name="dspet-tts-input")
    with process:
        try:
            writer.start()
            pending = b""
            with selectors.DefaultSelector() as selector:
                selector.register(process.stdout, selectors.EVENT_READ)
                while True:
                    if not selector.select(min(0.1, remaining())):
                        continue
                    block = os.read(process.stdout.fileno(), 4096)
                    if not block:
                        break
                    block = pending + block
                    whole = len(block) - len(block) % SAMPLE_BYTES
                    pending = block[whole:]
                    if whole:
                        yield block[:whole]
            if process.wait(timeout=remaining()) != 0:
                if failures and isinstance(failures[0], GatewayError):
                    raise failures[0]
                raise GatewayError("invalid_tts_audio", 502, "无法转换合成音频。")
            writer.join(timeout=remaining())
            remaining()
            if failures:
                error = failures[0]
                if isinstance(error, GatewayError):
                    raise error
                raise GatewayError("invalid_tts_audio", 502, "无法接收合成音频。") from error
            if pending:
                raise GatewayError("invalid_tts_audio", 502, "语音转换结果不完整。")
        except subprocess.TimeoutExpired as error:
            raise GatewayError("turn_timeout", 504, "语音转换超时。") from error
        finally:
            if process.poll() is None:
                process.kill()
