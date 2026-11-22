"""XIAO 可调用的本地 HTTP 接口。"""

from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import hmac
import json
import logging
import re

from .audio import DEVICE_RATE, MAX_INPUT_BYTES
from .errors import GatewayError

_LOGGER = logging.getLogger(__name__)
_SESSION_ID = re.compile(r"[A-Za-z0-9._-]{1,64}")
_TURN_ID = re.compile(r"[1-9][0-9]{0,9}")


def _parse_turn_id(value):
    if value is None or not _TURN_ID.fullmatch(value) or int(value) > 0xFFFFFFFF:
        raise GatewayError("invalid_turn_id", 400, "回合编号须为正的 32 位无符号整数。")
    return int(value)


def create_server(address, conversation, device_token):
    if not device_token or not device_token.isascii() or any(not 33 <= ord(char) <= 126 for char in device_token):
        raise GatewayError("invalid_device_token", 400, "请设置 DSPET_DEVICE_TOKEN 为非空 ASCII 设备凭据。")
    expected_auth = f"Bearer {device_token}".encode("ascii")

    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def setup(self):
            self.request.settimeout(15)
            super().setup()

        def log_message(self, *_args):
            pass

        def _send(self, status, mime, body):
            self.close_connection = True
            self.send_response(status)
            if mime:
                self.send_header("Content-Type", mime)
            if status != 204:
                self.send_header("Content-Length", str(len(body)))
            self.send_header("Connection", "close")
            self.end_headers()
            if body:
                self.wfile.write(body)

        def _send_json(self, status, data):
            self._send(status, "application/json; charset=utf-8", json.dumps(data, ensure_ascii=False).encode())

        def _session_id(self):
            value = self.headers.get("X-Session-Id", "")
            if not _SESSION_ID.fullmatch(value):
                raise GatewayError("invalid_session_id", 400, "缺少有效的 X-Session-Id。")
            return value

        def _input_pcm(self):
            if self.headers.get("Transfer-Encoding"):
                raise GatewayError("invalid_audio", 400, "录音请求须传入 Content-Length。")
            values = self.headers.get_all("Content-Length") or []
            if len(values) != 1 or not values[0].isascii() or not values[0].isdecimal():
                raise GatewayError("invalid_audio", 400, "缺少有效的 Content-Length。")
            if len(values[0]) > len(str(MAX_INPUT_BYTES)) or int(values[0]) > MAX_INPUT_BYTES:
                raise GatewayError("audio_too_large", 413, "单次录音最长为 15 秒。")
            if self.headers.get_content_type() != "application/octet-stream":
                raise GatewayError("invalid_audio_format", 415, "请求内容须为 16 kHz 单声道 PCM16LE。")
            size = int(values[0])
            pcm = self.rfile.read(size)
            if len(pcm) != size:
                raise GatewayError("invalid_audio", 400, "上传的录音不完整。")
            return pcm

        def _run_turn(self):
            session_id = self._session_id()
            turn_id = _parse_turn_id(self.headers.get("X-Turn-Id"))
            pcm = self._input_pcm()
            streaming = False

            def send_audio(block):
                nonlocal streaming
                if not streaming:
                    self.close_connection = True
                    self.send_response(200)
                    self.send_header("Content-Type", "application/octet-stream")
                    self.send_header("Transfer-Encoding", "chunked")
                    self.send_header("Connection", "close")
                    self.send_header("X-Turn-Id", str(turn_id))
                    self.send_header("X-PCM-Rate", str(DEVICE_RATE))
                    self.end_headers()
                    streaming = True
                self.wfile.write(f"{len(block):x}\r\n".encode() + block + b"\r\n")

            try:
                result = conversation.run(session_id, turn_id, pcm, send_audio)
            except GatewayError as error:
                if not streaming:
                    raise
                # 半途失败时省略结束块，客户端可区分完整 PCM 和截断的回复。
                _LOGGER.info("turn=%d stream_error=%s", turn_id, error.code)
                return
            self.wfile.write(b"0\r\n\r\n")
            _LOGGER.info(
                "turn=%d asr_ms=%.0f chat_ms=%.0f tts_first_ms=%.0f tts_ms=%.0f first_pcm_ms=%.0f total_ms=%.0f bytes=%d",
                turn_id, 1000 * result.asr_seconds, 1000 * result.chat_seconds,
                1000 * result.tts_first_audio_seconds, 1000 * result.tts_seconds,
                1000 * result.first_output_seconds, 1000 * result.total_seconds, result.audio_bytes,
            )

        def _cancel_turn(self):
            session_id = self._session_id()
            turn_id = _parse_turn_id(self.path.removeprefix("/v1/turns/"))
            if not conversation.cancel(session_id, turn_id):
                raise GatewayError("unknown_turn", 404, "会话里找不到这个回合。")
            self._send(204, None, b"")

        def _handle(self, action):
            try:
                try:
                    auth = self.headers.get("Authorization", "").encode("utf-8")
                    if not hmac.compare_digest(auth, expected_auth):
                        raise GatewayError("device_auth", 401, "设备凭据无效。")
                    action()
                except GatewayError as error:
                    _LOGGER.info("method=%s status=%d code=%s", self.command, error.status, error.code)
                    self._send_json(error.status, {"error": {"code": error.code, "message": str(error)}})
            except (BrokenPipeError, ConnectionResetError, TimeoutError):
                self.close_connection = True

        def _not_found(self):
            raise GatewayError("not_found", 404, "未知的网关接口。")

        def do_GET(self):
            if self.path == "/health":
                self._handle(lambda: self._send_json(200, {"status": "ok"}))
            else:
                self._handle(self._not_found)

        def do_POST(self):
            self._handle(self._run_turn if self.path == "/v1/turns" else self._not_found)

        def do_DELETE(self):
            self._handle(self._cancel_turn if self.path.startswith("/v1/turns/") else self._not_found)

    return ThreadingHTTPServer(address, Handler)
