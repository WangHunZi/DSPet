"""组织语音问答、最近的会话历史和回合取消。"""

from collections import OrderedDict, deque
from dataclasses import dataclass, field
from pathlib import Path
from threading import Event, Lock
import time

from .audio import resample_reply, validate_input, wrap_wav
from .errors import GatewayError

_PROMPT = Path(__file__).with_name("system_prompt.txt").read_text(encoding="utf-8").strip()
_HISTORY_TURNS = 6
_SESSION_CAPACITY = 32


@dataclass
class _Turn:
    turn_id: int
    deadline: float
    cancelled: Event = field(default_factory=Event)

    def remaining(self):
        if self.cancelled.is_set():
            raise GatewayError("turn_cancelled", 409, "这个回合已取消。")
        remaining = self.deadline - time.monotonic()
        if remaining <= 0:
            raise GatewayError("turn_timeout", 504, "本轮问答超时。")
        return remaining


@dataclass
class _Exchange:
    turn_id: int
    transcript: str
    reply: str
    interrupted: bool = False

    def messages(self):
        reply = self.reply
        if self.interrupted:
            reply += "\n（这段回复的播报已被打断，用户听到的范围未知。）"
        return [{"role": "user", "content": self.transcript}, {"role": "assistant", "content": reply}]


@dataclass
class _Session:
    last_turn_id: int = 0
    active: _Turn | None = None
    history: deque = field(default_factory=lambda: deque(maxlen=_HISTORY_TURNS))


@dataclass(frozen=True)
class TurnResult:
    transcript: str
    reply: str
    pcm: bytes
    asr_seconds: float
    chat_seconds: float
    tts_first_audio_seconds: float
    tts_seconds: float
    total_seconds: float


class Conversation:
    def __init__(self, provider, timeout_seconds=45):
        self._provider = provider
        self._timeout_seconds = timeout_seconds
        self._sessions = OrderedDict()
        self._lock = Lock()

    def _begin(self, session_id, turn_id, started):
        with self._lock:
            session = self._sessions.get(session_id)
            if session is None:
                if len(self._sessions) == _SESSION_CAPACITY:
                    idle_id = next((key for key, value in self._sessions.items() if value.active is None), None)
                    if idle_id is None:
                        raise GatewayError("gateway_busy", 503, "网关当前正在处理其他会话。")
                    del self._sessions[idle_id]
                session = self._sessions[session_id] = _Session()

            if turn_id <= session.last_turn_id:
                raise GatewayError("stale_turn", 409, "回合编号必须在会话内递增。")
            if session.active:
                session.active.cancelled.set()
            active = _Turn(turn_id, started + self._timeout_seconds)
            session.last_turn_id = turn_id
            session.active = active
            self._sessions.move_to_end(session_id)

            messages = [{"role": "system", "content": _PROMPT}]
            for exchange in session.history:
                messages.extend(exchange.messages())
            return session, active, messages

    def cancel(self, session_id, turn_id):
        with self._lock:
            session = self._sessions.get(session_id)
            if session is None:
                return False
            if session.active and session.active.turn_id == turn_id:
                session.active.cancelled.set()
                return True
            for exchange in session.history:
                if exchange.turn_id == turn_id:
                    exchange.interrupted = True
                    return True
            return session.last_turn_id == turn_id

    def run(self, session_id, turn_id, pcm):
        validate_input(pcm)
        started = time.monotonic()
        session, active, messages = self._begin(session_id, turn_id, started)
        try:
            transcript = self._provider.transcribe(wrap_wav(pcm), active.remaining)
            asr_finished = time.monotonic()
            if not transcript:
                raise GatewayError("empty_transcript", 422, "没听清这一句，请再说一次。")

            messages.append({"role": "user", "content": transcript})
            reply = self._provider.reply(messages, active.remaining)
            chat_finished = time.monotonic()
            synthesis = self._provider.synthesize(reply, active.remaining)
            tts_finished = time.monotonic()
            audio = resample_reply(synthesis.pcm, active.remaining())

            with self._lock:
                active.remaining()
                session.history.append(_Exchange(turn_id, transcript, reply))
                session.active = None

            return TurnResult(
                transcript, reply, audio, asr_finished - started, chat_finished - asr_finished,
                synthesis.first_audio_seconds, tts_finished - chat_finished, time.monotonic() - started,
            )
        finally:
            with self._lock:
                if session.active is active:
                    session.active = None
