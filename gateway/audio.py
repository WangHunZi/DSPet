"""网关与板端约定的单声道 PCM16 格式。"""

from io import BytesIO
import subprocess
import wave

from .errors import GatewayError

DEVICE_RATE = 16_000
SYNTHESIS_RATE = 24_000
SAMPLE_BYTES = 2
MAX_INPUT_BYTES = 15 * DEVICE_RATE * SAMPLE_BYTES
MAX_REPLY_BYTES = 60 * SYNTHESIS_RATE * SAMPLE_BYTES


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


def resample_reply(pcm, timeout):
    try:
        result = subprocess.run(
            ["ffmpeg", "-nostdin", "-v", "error", "-f", "s16le", "-ac", "1",
             "-ar", str(SYNTHESIS_RATE), "-i", "pipe:0", "-ar", str(DEVICE_RATE),
             "-f", "s16le", "pipe:1"],
            input=pcm, capture_output=True, check=True, timeout=timeout,
        )
    except FileNotFoundError as error:
        raise GatewayError("ffmpeg_missing", 503, "Mac 网关需要 ffmpeg 转换采样率。") from error
    except subprocess.TimeoutExpired as error:
        raise GatewayError("turn_timeout", 504, "语音转换超时。") from error
    except subprocess.CalledProcessError as error:
        raise GatewayError("invalid_tts_audio", 502, "无法转换合成音频。") from error
    if not result.stdout or len(result.stdout) % SAMPLE_BYTES:
        raise GatewayError("invalid_tts_audio", 502, "语音转换结果为空或不完整。")
    return result.stdout
