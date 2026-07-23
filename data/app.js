const PWM_MAX = 1023;

// Elementos principales de la pagina. Se buscan una vez al cargar el script y se
// reutilizan despues para no repetir document.getElementById por todo el codigo.
const configForm = document.getElementById('configForm');
const resetForm = document.getElementById('resetForm');
const refreshState = document.getElementById('refreshState');

// Mientras el usuario edita el formulario, se pausan las actualizaciones
// automaticas para que el navegador no sobrescriba lo que esta escribiendo.
let editing = false;

// Actualiza el texto visible de un elemento si existe.
function txt(id, value) {
  const element = document.getElementById(id);
  if (element) {
    element.textContent = value;
  }
}

// Actualiza el valor de un campo de formulario si existe.
function value(id, nextValue) {
  const element = document.getElementById(id);
  if (element) {
    element.value = nextValue;
  }
}

// Cambia el modo de edicion y muestra al usuario si la actualizacion en vivo
// esta activa o pausada.
function setEditing(nextEditing) {
  editing = nextEditing;
  refreshState.textContent = nextEditing
    ? 'Actualizacion pausada mientras editas'
    : 'Actualizacion automatica activa';
}

// Rellena el formulario con la configuracion guardada en el ESP32.
function fillConfig(config) {
  value('setpoint', Number(config.setpoint).toFixed(1));
  value('kp', Number(config.kp).toFixed(2));
  value('ki', Number(config.ki).toFixed(2));
  value('kd', Number(config.kd).toFixed(2));
  value('outMin', Number(config.outMin).toFixed(0));
  value('outMax', Number(config.outMax).toFixed(0));
  document.getElementById('invert').checked = Boolean(config.invert);
}

// Lee la configuracion actual desde el firmware. cache:no-store evita que el
// navegador reutilice una respuesta antigua.
async function loadConfig() {
  const response = await fetch('/config.json', { cache: 'no-store' });
  fillConfig(await response.json());
}

// Pide al ESP32 los valores vivos del PID y actualiza las tarjetas superiores.
async function updateStatus() {
  if (editing) {
    return;
  }

  try {
    const response = await fetch('/status', { cache: 'no-store' });
    const data = await response.json();

    // Los valores numericos se formatean aqui para mantener el HTML estatico.
    txt('input', Number(data.input).toFixed(1));
    txt('setpointValue', Number(data.setpoint).toFixed(1));
    txt('error', Number(data.error).toFixed(1));
    txt('output', (Number(data.output) * 100 / PWM_MAX).toFixed(1));
    txt('raw', data.raw);
    txt('rawPercent', Number(data.rawPercent).toFixed(1));

    const state = document.getElementById('state');
    state.textContent = data.inSet ? 'SET' : 'NO SET';
    state.classList.toggle('ok', Boolean(data.inSet));
  } catch (error) {
    // Si se pierde momentaneamente la conexion WiFi, se ignora este ciclo y el
    // siguiente setInterval volvera a intentarlo.
  }
}

// Envia los parametros del formulario al ESP32 sin recargar la pagina.
async function submitForm(event) {
  event.preventDefault();
  const body = new URLSearchParams(new FormData(configForm));
  await fetch('/config', { method: 'POST', body });
  setEditing(false);
  await loadConfig();
  await updateStatus();
}

// Reinicia solo el estado interno del PID, no el ESP32.
async function resetPid(event) {
  event.preventDefault();
  await fetch('/reset', { method: 'POST' });
  await updateStatus();
}

// focusin/focusout detectan cuando el usuario entra o sale de cualquier campo
// del formulario. El pequeno retardo permite que document.activeElement se
// actualice antes de decidir si seguimos dentro del formulario.
configForm.addEventListener('focusin', () => setEditing(true));
configForm.addEventListener('focusout', () => {
  setTimeout(() => setEditing(configForm.contains(document.activeElement)), 80);
});
configForm.addEventListener('submit', submitForm);
resetForm.addEventListener('submit', resetPid);

// Arranque de la interfaz: carga configuracion, pide el primer estado y despues
// refresca los datos vivos una vez por segundo.
loadConfig().catch(() => {});
updateStatus();
setInterval(updateStatus, 1000);
