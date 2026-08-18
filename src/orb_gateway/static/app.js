const deviceId = "demo";
const baseUrl = `/api/v1/devices/${deviceId}`;
const display = document.querySelector("#display");
const title = document.querySelector("#title");
const message = document.querySelector("#message");
const stateName = document.querySelector("#stateName");
const revision = document.querySelector("#revision");
const feedback = document.querySelector("#feedback");
const connectionDot = document.querySelector("#connectionDot");

function render(snapshot) {
  display.dataset.state = snapshot.state;
  title.textContent = snapshot.title;
  message.textContent = snapshot.message;
  stateName.textContent = snapshot.state.toUpperCase();
  revision.textContent = snapshot.revision;
  connectionDot.classList.add("online");
}

async function loadState() {
  try {
    const response = await fetch(`${baseUrl}/state`, { cache: "no-store" });
    if (!response.ok) throw new Error(`Gateway 返回 ${response.status}`);
    render(await response.json());
  } catch (error) {
    connectionDot.classList.remove("online");
    feedback.textContent = `无法连接 Gateway：${error.message}`;
    feedback.classList.add("error");
  }
}

async function sendAction(button) {
  const payload = { action: button.dataset.action };
  if (button.dataset.title) payload.title = button.dataset.title;
  if (button.dataset.message) payload.message = button.dataset.message;
  feedback.classList.remove("error");
  try {
    const response = await fetch(`${baseUrl}/actions`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload),
    });
    const body = await response.json();
    if (!response.ok) throw new Error(body.message || `Gateway 返回 ${response.status}`);
    render(body);
    feedback.textContent = `${payload.action} → ${body.state}`;
  } catch (error) {
    feedback.textContent = error.message;
    feedback.classList.add("error");
  }
}

document.querySelectorAll("[data-action]").forEach((button) => {
  button.addEventListener("click", () => sendAction(button));
});

loadState();
setInterval(loadState, 800);
