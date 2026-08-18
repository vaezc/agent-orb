import json
import threading
import unittest
from unittest.mock import Mock
from urllib.error import HTTPError
from urllib.request import Request, urlopen

from orb_gateway.app import OrbGatewayServer
from orb_gateway.assistant import AssistantUnavailable


class GatewayHttpTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = OrbGatewayServer(("127.0.0.1", 0))
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.base = f"http://127.0.0.1:{cls.server.server_port}"

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join(timeout=2)

    def get_json(self, path):
        with urlopen(self.base + path, timeout=2) as response:
            return response.status, json.load(response)

    def post_json(self, path, payload):
        request = Request(
            self.base + path,
            data=json.dumps(payload).encode(),
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        with urlopen(request, timeout=2) as response:
            return response.status, json.load(response)

    def test_health_and_static_ui(self):
        status, body = self.get_json("/api/v1/health")
        self.assertEqual(status, 200)
        self.assertTrue(body["ok"])
        with urlopen(self.base + "/", timeout=2) as response:
            self.assertIn(b"Agent Orb", response.read())

    def test_action_updates_state(self):
        status, body = self.post_json(
            "/api/v1/devices/http-test/actions",
            {"action": "attention", "message": "hello"},
        )
        self.assertEqual(status, 200)
        self.assertEqual(body["state"], "attention")
        _, current = self.get_json("/api/v1/devices/http-test/state")
        self.assertEqual(current["message"], "hello")


    def test_tools_and_query_flow(self):
        _, tools = self.get_json("/api/v1/tools")
        self.assertGreaterEqual(len(tools["tools"]), 4)
        status, body = self.post_json(
            "/api/v1/devices/query-test/query",
            {"text": "系统状态", "source": "test"},
        )
        self.assertEqual(status, 200)
        self.assertEqual(body["state"], "answer")
        self.assertEqual(body["tool"], "orb_status")
        self.assertEqual(body["input"], "系统状态")

    def test_invalid_transition_returns_conflict(self):
        try:
            self.post_json("/api/v1/devices/conflict/actions", {"action": "answer"})
        except HTTPError as error:
            try:
                self.assertEqual(error.code, 409)
                body = json.load(error)
                self.assertIn("cannot apply", body["message"])
            finally:
                error.close()
        else:
            self.fail("expected HTTP 409")

    def test_assistant_failure_moves_device_to_error(self):
        assistant = Mock()
        assistant.respond.side_effect = AssistantUnavailable("offline")
        assistant.list_tools.return_value = []
        server = OrbGatewayServer(("127.0.0.1", 0), assistant=assistant)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            request = Request(
                f"http://127.0.0.1:{server.server_port}/api/v1/devices/failure/query",
                data=json.dumps({"text": "hello"}).encode(),
                headers={"Content-Type": "application/json"},
                method="POST",
            )
            with self.assertRaises(HTTPError) as raised:
                urlopen(request, timeout=2)
            self.assertEqual(raised.exception.code, 502)
            raised.exception.close()
            self.assertEqual(server.registry.get("failure").snapshot().state.value, "error")
        finally:
            server.shutdown()
            server.server_close()
            thread.join(timeout=2)


if __name__ == "__main__":
    unittest.main()
