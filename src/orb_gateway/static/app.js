const deviceId = "demo";
const baseUrl = `/api/v1/devices/${deviceId}`;
const display = document.querySelector("#display");
const title = document.querySelector("#title");
const message = document.querySelector("#message");
const stateName = document.querySelector("#stateName");
const revision = document.querySelector("#revision");
const feedback = document.querySelector("#feedback");
const connectionDot = document.querySelector("#connectionDot");
const askForm = document.querySelector("#askForm");
const queryInput = document.querySelector("#queryInput");
const micButton = document.querySelector("#micButton");
let currentSnapshot = { state: "idle", revision: 0 };
let recognition = null;
let voiceResult = "";

function render(snapshot) {
  currentSnapshot = snapshot;
  display.dataset.state = snapshot.state;
  title.textContent = snapshot.title;
  message.textContent = snapshot.message;
  stateName.textContent = snapshot.state.toUpperCase();
  revision.textContent = snapshot.revision;
  connectionDot.classList.add("online");
}

async function request(path, options = {}) {
  const response = await fetch(`${baseUrl}/${path}`, { cache: "no-store", ...options });
  const body = await response.json();
  if (!response.ok) throw new Error(body.message || `Gateway 返回 ${response.status}`);
  return body;
}

async function loadState() {
  try {
    render(await request("state"));
  } catch (error) {
    connectionDot.classList.remove("online");
    showFeedback(`无法连接 Gateway：${error.message}`, true);
  }
}

async function sendActionPayload(payload) {
  const body = await request("actions", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(payload),
  });
  render(body);
  return body;
}

async function sendAction(button) {
  const payload = { action: button.dataset.action };
  if (button.dataset.title) payload.title = button.dataset.title;
  if (button.dataset.message) payload.message = button.dataset.message;
  try {
    const body = await sendActionPayload(payload);
    showFeedback(`${payload.action} → ${body.state}`);
  } catch (error) {
    showFeedback(error.message, true);
  }
}

async function prepareThinking() {
  if (currentSnapshot.state !== "listening") {
    if (!["idle", "answer", "attention"].includes(currentSnapshot.state)) {
      await sendActionPayload({ action: "reset" });
    }
    await sendActionPayload({ action: "wake" });
  }
  await sendActionPayload({ action: "speech_end" });
}

async function submitQuery(text) {
  const cleaned = text.trim();
  if (!cleaned) return;
  queryInput.value = cleaned;
  askForm.classList.add("busy");
  try {
    await prepareThinking();
    showFeedback("正在通过本地工具处理…");
    const body = await request("query", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ text: cleaned, source: "web" }),
    });
    render(body);
    showFeedback(`工具：${body.tool}`);
  } catch (error) {
    showFeedback(error.message, true);
  } finally {
    askForm.classList.remove("busy");
  }
}

function showFeedback(text, isError = false) {
  feedback.textContent = text;
  feedback.classList.toggle("error", isError);
}

function configureSpeechRecognition() {
  const SpeechRecognition = window.SpeechRecognition || window.webkitSpeechRecognition;
  if (!SpeechRecognition) {
    micButton.title = "当前浏览器不支持语音识别，请使用文字输入";
    return;
  }
  recognition = new SpeechRecognition();
  recognition.lang = "zh-CN";
  recognition.continuous = false;
  recognition.interimResults = true;
  recognition.onstart = async () => {
    voiceResult = "";
    micButton.classList.add("listening");
    showFeedback("正在聆听…");
    try {
      if (currentSnapshot.state !== "listening") {
        if (!["idle", "answer", "attention"].includes(currentSnapshot.state)) {
          await sendActionPayload({ action: "reset" });
        }
        await sendActionPayload({ action: "wake" });
      }
    } catch (error) {
      showFeedback(error.message, true);
    }
  };
  recognition.onresult = (event) => {
    let interim = "";
    for (let index = event.resultIndex; index < event.results.length; index += 1) {
      const transcript = event.results[index][0].transcript;
      if (event.results[index].isFinal) voiceResult += transcript;
      else interim += transcript;
    }
    queryInput.value = voiceResult || interim;
  };
  recognition.onerror = (event) => showFeedback(`语音识别失败：${event.error}`, true);
  recognition.onend = () => {
    micButton.classList.remove("listening");
    if (voiceResult.trim()) submitQuery(voiceResult);
  };
}

askForm.addEventListener("submit", (event) => {
  event.preventDefault();
  submitQuery(queryInput.value);
});

micButton.addEventListener("click", () => {
  if (!recognition) {
    showFeedback("当前浏览器不支持语音识别，请使用文字输入。", true);
    queryInput.focus();
    return;
  }
  if (micButton.classList.contains("listening")) recognition.stop();
  else recognition.start();
});

document.querySelectorAll("[data-action]").forEach((button) => {
  button.addEventListener("click", () => sendAction(button));
});

document.querySelectorAll("[data-query]").forEach((button) => {
  button.addEventListener("click", () => submitQuery(button.dataset.query));
});

configureSpeechRecognition();
loadState();
setInterval(loadState, 800);
