"""DeepSeek 官方语音和对话接口适配。"""

from contextlib import contextmanager
from dataclasses import dataclass
from http.client import HTTPException
import json
import time
import urllib.error
import urllib.request
import uuid

from .audio import MAX_REPLY_BYTES, SAMPLE_BYTES, SYNTHESIS_RATE
from .errors import GatewayError

_API_ORIGIN = "https://api.deepseek.com"
_MAX_JSON_BYTES = 512 * 1024


class _RejectRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, response, code, message, headers, url):
        return None


@dataclass(frozen=True)
class SynthesizedSpeech:
    pcm: bytes
    first_audio_seconds: float


class DeepSeek:
    def __init__(self, api_key, opener=None):
        self._api_key = api_key
        self._opener = opener if opener is not None else urllib.request.build_opener(_RejectRedirect())

    @contextmanager
    def _request(self, path, body, content_type, remaining):
        request = urllib.request.Request(
            _API_ORIGIN + path, data=body, method="POST",
            headers={"Authorization": f"Bearer {self._api_key}", "Content-Type": content_type},
        )
        try:
            with self._opener.open(request, timeout=remaining()) as response:
                yield response
        except urllib.error.HTTPError as error:
            error.close()
            if error.code in (401, 403):
                raise GatewayError("upstream_auth", 502, "DeepSeek 拒绝了凭据。") from error
            if error.code == 429:
                raise GatewayError("upstream_rate_limited", 429, "DeepSeek 请求受限，请稍后再试。") from error
            if error.code in (408, 504):
                raise GatewayError("upstream_timeout", 504, "DeepSeek 请求超时。") from error
            raise GatewayError("upstream_http", 502, f"DeepSeek 返回 HTTP {error.code}。") from error
        except TimeoutError as error:
            raise GatewayError("upstream_timeout", 504, "DeepSeek 请求超时。") from error
        except urllib.error.URLError as error:
            if isinstance(error.reason, TimeoutError):
                raise GatewayError("upstream_timeout", 504, "DeepSeek 请求超时。") from error
            raise GatewayError("upstream_connection", 502, "无法连接 DeepSeek。") from error
        except (OSError, HTTPException) as error:
            raise GatewayError("upstream_connection", 502, "DeepSeek 响应连接中断。") from error

    @staticmethod
    def _read(response, limit, remaining):
        result = bytearray()
        while True:
            remaining()
            chunk = response.read1(min(8192, limit - len(result) + 1))
            if not chunk:
                remaining()
                return bytes(result)
            result.extend(chunk)
            if len(result) > limit:
                raise GatewayError("upstream_response_too_large", 502, "DeepSeek 响应超过网关容量。")

    @classmethod
    def _read_json(cls, response, remaining):
        try:
            return json.loads(cls._read(response, _MAX_JSON_BYTES, remaining))
        except (ValueError, UnicodeError) as error:
            raise GatewayError("invalid_upstream_response", 502, "DeepSeek 返回的 JSON 无效。") from error

    def transcribe(self, wav, remaining):
        boundary = uuid.uuid4().hex
        header = (
            f'--{boundary}\r\nContent-Disposition: form-data; name="file"; filename="speech.wav"\r\n'
            "Content-Type: audio/wav\r\n\r\n"
        ).encode("ascii")
        body = header + wav + f"\r\n--{boundary}--\r\n".encode("ascii")
        with self._request("/v1/tools/asr", body, f"multipart/form-data; boundary={boundary}", remaining) as response:
            data = self._read_json(response, remaining)
        if not isinstance(data, dict) or not isinstance(data.get("text"), str):
            raise GatewayError("invalid_upstream_response", 502, "DeepSeek 返回的转写格式异常。")
        return data["text"].strip()

    def reply(self, messages, remaining):
        body = json.dumps({
            "model": "deepseek-flash",
            "reasoning_effort": "low",
            "max_tokens": 1536,
            "messages": messages,
        }, ensure_ascii=False).encode()
        with self._request("/chat/completions", body, "application/json", remaining) as response:
            data = self._read_json(response, remaining)
        try:
            choice = data["choices"][0]
            content = choice["message"]["content"]
            finish_reason = choice["finish_reason"]
        except (KeyError, IndexError, TypeError) as error:
            raise GatewayError("invalid_upstream_response", 502, "DeepSeek 返回的回复格式异常。") from error
        if finish_reason != "stop" or not isinstance(content, str) or not content.strip():
            raise GatewayError("incomplete_reply", 502, "DeepSeek 未返回完整的可朗读回复。")
        return content.strip()

    def synthesize(self, text, remaining):
        body = json.dumps({"text": text, "voice_id": "stella", "format": "pcm"}, ensure_ascii=False).encode()
        started = time.monotonic()
        with self._request("/v1/tools/tts", body, "application/json", remaining) as response:
            encoding = response.headers
            if encoding.get_content_type() != "audio/pcm" or any(
                encoding.get_param(name, default) != default
                for name, default in (("rate", str(SYNTHESIS_RATE)), ("bits", "16"), ("channels", "1"))
            ):
                raise GatewayError("invalid_tts_audio", 502, "DeepSeek 返回的音频格式与 PCM16 单声道约定不符。")
            remaining()
            first = response.read1(1024)
            first_audio_seconds = time.monotonic() - started
            pcm = first + self._read(response, MAX_REPLY_BYTES - len(first), remaining) if first else b""
            if not pcm or len(pcm) % SAMPLE_BYTES or response.length not in (0, None):
                raise GatewayError("invalid_tts_audio", 502, "DeepSeek 返回的音频为空或传输不完整。")
        return SynthesizedSpeech(pcm, first_audio_seconds)
