import unittest

from orb_gateway.model import InvalidTransition, OrbDevice, OrbState


class OrbDeviceTests(unittest.TestCase):
    def test_voice_happy_path(self):
        orb = OrbDevice("desk")
        self.assertEqual(orb.snapshot().state, OrbState.IDLE)
        self.assertEqual(orb.apply("wake").state, OrbState.LISTENING)
        self.assertEqual(orb.apply("speech_end").state, OrbState.THINKING)
        answer = orb.apply("answer", {"title": "日程", "message": "15:00 开会"})
        self.assertEqual(answer.state, OrbState.ANSWER)
        self.assertEqual(answer.message, "15:00 开会")
        self.assertEqual(orb.apply("dismiss").state, OrbState.IDLE)

    def test_approval_happy_path(self):
        orb = OrbDevice("desk")
        pending = orb.apply(
            "request_approval",
            {"title": "部署？", "message": "production", "request_id": "job-1"},
        )
        self.assertEqual(pending.state, OrbState.APPROVAL)
        self.assertEqual(pending.request_id, "job-1")
        accepted = orb.apply("approve", {"request_id": "job-1"})
        self.assertEqual(accepted.state, OrbState.ANSWER)
        self.assertEqual(accepted.title, "已确认")

    def test_invalid_transition_does_not_mutate(self):
        orb = OrbDevice("desk")
        before = orb.snapshot()
        with self.assertRaises(InvalidTransition):
            orb.apply("answer")
        after = orb.snapshot()
        self.assertEqual(after.revision, before.revision)
        self.assertEqual(after.state, before.state)

    def test_text_is_normalized_and_bounded(self):
        orb = OrbDevice("desk")
        snapshot = orb.apply("attention", {"message": "  hello   world  " + "x" * 300})
        self.assertTrue(snapshot.message.startswith("hello world"))
        self.assertLessEqual(len(snapshot.message), 240)


if __name__ == "__main__":
    unittest.main()
