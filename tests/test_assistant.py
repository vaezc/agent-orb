import unittest

from orb_gateway.assistant import LocalAssistant


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


if __name__ == "__main__":
    unittest.main()
