import unittest
from unittest.mock import MagicMock, patch

from orb_gateway.assistant import LocalAssistant, SnoopyAssistant, assistant_from_environment


class LocalAssistantTests(unittest.TestCase):
    def setUp(self):
        self.assistant = LocalAssistant(lambda: 2)

    def test_status_tool(self):
        response = self.assistant.respond("系统状态", "demo")
        self.assertEqual(response.tool, "orb_status")
        self.assertIn("2 个", response.message)

    def test_capabilities_tool(self):
        response = self.assistant.respond("你会做什么？", "demo")
        self.assertEqual(response.tool, "capabilities")

    def test_unknown_query_is_transparent_echo(self):
        response = self.assistant.respond("帮我分析邮件", "demo")
        self.assertEqual(response.tool, "echo")
        self.assertIn("真实 LLM 尚未接入", response.message)

    def test_empty_query_is_rejected(self):
        with self.assertRaises(ValueError):
            self.assistant.respond("   ", "demo")


class SnoopyAssistantTests(unittest.TestCase):
    def test_response_is_mapped_to_orb_answer(self):
        upstream = MagicMock()
        upstream.__enter__.return_value = upstream
        upstream.__exit__.return_value = False
        upstream.read.return_value = b'{"response":"\xe4\xbd\xa0\xe5\xa5\xbd","candidates":[]}'

        with patch("orb_gateway.assistant.urlopen", return_value=upstream) as mocked:
            result = SnoopyAssistant("http://127.0.0.1:4317", "secret").respond("测试", "demo")

        self.assertEqual(result.message, "你好")
        self.assertEqual(result.tool, "snoopy_chat")
        request = mocked.call_args.args[0]
        self.assertEqual(request.get_header("Authorization"), "Bearer secret")

    def test_environment_defaults_to_local_assistant(self):
        with patch.dict("os.environ", {}, clear=True):
            self.assertIsInstance(assistant_from_environment(lambda: 0), LocalAssistant)

    def test_environment_requires_url_and_token_together(self):
        with patch.dict("os.environ", {"SNOOPY_SERVER_URL": "http://127.0.0.1:4317"}, clear=True):
            with self.assertRaises(ValueError):
                assistant_from_environment(lambda: 0)


if __name__ == "__main__":
    unittest.main()
