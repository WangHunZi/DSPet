from collections import deque
from concurrent.futures import ThreadPoolExecutor
from contextlib import contextmanager
from email.message import Message
import http.client
from io import BytesIO
import json
from threading import Event, Thread
import unittest
from urllib.error import HTTPError

from gateway.audio import MAX_INPUT_BYTES, wrap_wav
from gateway.conversation import Conversation
from gateway.deepseek import DeepSeek
from gateway.errors import GatewayError
from gateway.server import create_server

INPUT_PCM = b"\x01\x00" * 160
TTS_PCM = b"\x01\x00" * 12_000
HEADERS = {
    "Authorization": "Bearer dspet-test",
    "Content-Type": "application/octet-stream",
    "X-Session-Id": "voice-test",
    "X-Turn-Id": "1",
}


class UpstreamResponse(BytesIO):
    def __init__(self, value, mime="application/json"):
        super().__init__(value if isinstance(value, bytes) else json.dumps(value).encode())
        self.headers = Message()
        self.headers["Content-Type"] = mime

    @property
    def length(self):
        return self.getbuffer().nbytes - self.tell()


class ScriptedOpener:
    def __init__(self, *responses):
        self.responses = deque(responses)
        self.requests = []

    def open(self, request, timeout):
        self.requests.append(request)
        response = self.responses.popleft()
        if isinstance(response, Exception):
            raise response
        return response


class DeepSeekTests(unittest.TestCase):
    def test_asr_final_answer_only_and_pcm(self):
        opener = ScriptedOpener(
            UpstreamResponse({"text": "好久不见，金鱼娘。"}),
            UpstreamResponse({"choices": [{
                "message": {"content": "我在呢。", "reasoning_content": "内部思考"},
                "finish_reason": "stop",
            }]}),
            UpstreamResponse(TTS_PCM, "audio/pcm; rate=24000; bits=16; channels=1"),
        )
        client = DeepSeek("fake-token", opener)
        remaining = lambda: 1
        wav = wrap_wav(INPUT_PCM)
        transcript = client.transcribe(wav, remaining)
        self.assertEqual(transcript, "好久不见，金鱼娘。")
        answer = client.reply([{"role": "user", "content": transcript}], remaining)
        self.assertEqual(answer, "我在呢。")
        self.assertEqual(b"".join(client.synthesize(answer, remaining)), TTS_PCM)
        asr, chat, tts = opener.requests
        self.assertTrue(asr.full_url.endswith("/v1/tools/asr"))
        self.assertIn(wav, asr.data)
        self.assertIn(b'name="file"', asr.data)
        self.assertEqual(json.loads(chat.data)["messages"][-1]["content"], transcript)
        self.assertEqual(json.loads(tts.data)["text"], answer)

    def test_maps_upstream_errors(self):
        for status, code in [(401, "upstream_auth"), (429, "upstream_rate_limited"), (504, "upstream_timeout")]:
            with self.subTest(status=status):
                error = HTTPError("https://api.deepseek.com", status, "error", {}, BytesIO())
                client = DeepSeek("fake-token", ScriptedOpener(error))
                with self.assertRaises(GatewayError) as caught:
                    client.transcribe(b"wav", lambda: 1)
                self.assertEqual(caught.exception.code, code)

    def test_rejects_broken_playback(self):
        opener = ScriptedOpener(UpstreamResponse(b"\x00", "audio/pcm"))
        client = DeepSeek("fake-token", opener)
        with self.assertRaises(GatewayError) as caught:
            list(client.synthesize("你好。", lambda: 1))
        self.assertEqual(caught.exception.code, "invalid_tts_audio")


class FakeProvider:
    def __init__(self, *transcripts):
        self.transcripts = deque(transcripts)
        self.messages = []

    def transcribe(self, wav, remaining):
        remaining()
        return self.transcripts.popleft()

    def reply(self, messages, remaining):
        remaining()
        self.messages.append(messages)
        return f"答复 {messages[-1]['content']}"

    def synthesize(self, text, remaining):
        remaining()
        yield TTS_PCM


def run_turn(conversation, session_id, turn_id):
    chunks = []
    result = conversation.run(session_id, turn_id, INPUT_PCM, chunks.append)
    return result, b"".join(chunks)


@contextmanager
def serving(provider):
    conversation = Conversation(provider)
    with create_server(("127.0.0.1", 0), conversation, "dspet-test") as server:
        worker = Thread(target=server.serve_forever, kwargs={"poll_interval": 0.02}, daemon=True)
        worker.start()
        try:
            yield server.server_address[1]
        finally:
            server.shutdown()
            worker.join()


@contextmanager
def request(port, method, path, body=None, headers=None):
    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
    try:
        connection.request(method, path, body=body, headers=HEADERS if headers is None else headers)
        with connection.getresponse() as response:
            yield response
    finally:
        connection.close()


class ConversationTests(unittest.TestCase):
    def test_history_and_interrupted_reply(self):
        provider = FakeProvider("为什么海水是咸的", "给我讲个故事", "然后呢", "你好")
        conversation = Conversation(provider)
        result, pcm = run_turn(conversation, "story", 1)
        self.assertEqual(result.transcript, "为什么海水是咸的")
        self.assertEqual(len(pcm), 16_000)
        run_turn(conversation, "story", 2)
        self.assertTrue(conversation.cancel("story", 2))
        run_turn(conversation, "story", 3)
        run_turn(conversation, "other", 1)
        self.assertEqual(provider.messages[2][0]["role"], "system")
        self.assertEqual(provider.messages[2][-3]["content"], "给我讲个故事")
        self.assertIn("播报已被打断", provider.messages[2][-2]["content"])
        self.assertEqual(provider.messages[2][-1]["content"], "然后呢")
        self.assertEqual(len(provider.messages[3]), 2)

    def test_superseded_response_does_not_enter_history(self):
        waiting, release = Event(), Event()

        class BlockedReply(FakeProvider):
            def reply(self, messages, remaining):
                if messages[-1]["content"] == "旧回合":
                    waiting.set()
                    if not release.wait(5):
                        raise RuntimeError("测试未释放旧回合")
                return super().reply(messages, remaining)

        provider = BlockedReply("旧回合", "新回合", "下一句")
        conversation = Conversation(provider)
        with ThreadPoolExecutor() as executor:
            old = executor.submit(run_turn, conversation, "session", 1)
            try:
                self.assertTrue(waiting.wait(3))
                run_turn(conversation, "session", 2)
            finally:
                release.set()
            with self.assertRaises(GatewayError) as caught:
                old.result(timeout=3)
            self.assertEqual(caught.exception.code, "turn_cancelled")

        run_turn(conversation, "session", 3)
        self.assertEqual(provider.messages[-1][1]["content"], "新回合")
        with self.assertRaises(GatewayError) as caught:
            run_turn(conversation, "session", 2)
        self.assertEqual(caught.exception.code, "stale_turn")


class ServerTests(unittest.TestCase):
    def test_rejects_invalid_requests_and_recovers_from_empty_transcript(self):
        with serving(FakeProvider("", "第二次")) as port:
            with request(port, "POST", "/v1/turns", INPUT_PCM, headers={}) as response:
                self.assertEqual(response.status, 401)
            with request(port, "POST", "/v1/turns", b"") as response:
                self.assertEqual(response.status, 400)
            oversized = {**HEADERS, "Content-Length": str(MAX_INPUT_BYTES + 2)}
            with request(port, "POST", "/v1/turns", headers=oversized) as response:
                self.assertEqual(response.status, 413)
            with request(port, "POST", "/v1/turns", INPUT_PCM) as response:
                self.assertEqual(response.status, 422)
                self.assertEqual(json.load(response)["error"]["code"], "empty_transcript")

            headers = {**HEADERS, "X-Turn-Id": "2"}
            with request(port, "POST", "/v1/turns", INPUT_PCM, headers=headers) as response:
                self.assertEqual(response.status, 200)
                self.assertEqual(response.getheader("X-Turn-Id"), "2")
                self.assertEqual(response.getheader("X-PCM-Rate"), "16000")
                self.assertEqual(len(response.read()), 16_000)

    def test_first_chunk_arrives_before_finish_and_can_be_cancelled(self):
        release = Event()

        class PausedTTS(FakeProvider):
            def synthesize(self, text, remaining):
                yield TTS_PCM
                if not release.wait(6):
                    raise RuntimeError("等待测试收到首块超时")
                remaining()
                yield TTS_PCM

        with serving(PausedTTS("长回复")) as port:
            try:
                with request(port, "POST", "/v1/turns", INPUT_PCM) as stream:
                    self.assertEqual(stream.status, 200)
                    self.assertEqual(stream.getheader("Transfer-Encoding"), "chunked")
                    self.assertEqual(len(stream.read(1024)), 1024)
                    with request(port, "DELETE", "/v1/turns/1") as response:
                        self.assertEqual(response.status, 204)
                    release.set()
                    with self.assertRaises(http.client.IncompleteRead):
                        stream.read()
            finally:
                release.set()
