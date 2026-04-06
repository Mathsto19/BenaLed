import * as THREE from "three";
import { OrbitControls } from "three/addons/controls/OrbitControls.js";

const app3d = document.getElementById("app3d");
const clearBtn = document.getElementById("clearBtn");
const presetBtn = document.getElementById("presetBtn");
const toggleColorsBtn = document.getElementById("toggleColorsBtn");
const paintToolbar = document.getElementById("paintToolbar");
const card = document.querySelector(".card");
const lockViewBtn = document.getElementById("lockViewBtn");

const presetModal = document.getElementById("presetModal");
const colorPicker = document.getElementById("colorPicker");
const currentColorPreview = document.getElementById("currentColorPreview");
const currentColorLabel = document.getElementById("currentColorLabel");
const mediaBtn = document.getElementById("mediaBtn");
const mediaInput = document.getElementById("mediaInput");
const statusBar = document.getElementById("statusBar");

const modeButtons = document.querySelectorAll("[data-mode]");
const swatchButtons = document.querySelectorAll(".inline-palette .color-swatch");
const presetButtons = document.querySelectorAll(".preset-item");

let selectedColor = "#00eeff";
let paintMode = "paint";
let rainbowPixelCounter = 0;
let isViewLocked = false;
let isDragPainting = false;
let lastPaintedKey = null;

const BASE_BLUE = "#2db8ff";
const BASE_GREEN = "#ffffff";
const OFF_COLOR = "#111827";

function setStatus(text) {
  if (statusBar) statusBar.textContent = text;
}

function hslToHex(h, s, l) {
  s /= 100;
  l /= 100;
  const c = (1 - Math.abs(2 * l - 1)) * s;
  const x = c * (1 - Math.abs((h / 60) % 2 - 1));
  const m = l - c / 2;
  let r = 0, g = 0, b = 0;
  if (h < 60) { r = c; g = x; }
  else if (h < 120) { r = x; g = c; }
  else if (h < 180) { g = c; b = x; }
  else if (h < 240) { g = x; b = c; }
  else if (h < 300) { r = x; b = c; }
  else { r = c; b = x; }
  const toHex = (v) => Math.round((v + m) * 255).toString(16).padStart(2, "0");
  return `#${toHex(r)}${toHex(g)}${toHex(b)}`;
}

function getCurrentPaintColor() {
  if (paintMode === "eraser") return OFF_COLOR;
  if (paintMode === "rainbow") {
    const hue = (Math.floor(rainbowPixelCounter / 3) * 20) % 360;
    rainbowPixelCounter += 1;
    return hslToHex(hue, 100, 60);
  }
  return selectedColor;
}

function updateToolbarUi() {
  modeButtons.forEach((button) => {
    button.classList.toggle("is-active", button.dataset.mode === paintMode);
  });

  swatchButtons.forEach((button) => {
    const isSelected =
      paintMode === "paint" &&
      button.dataset.color.toLowerCase() === selectedColor.toLowerCase();

    button.classList.toggle("is-selected", isSelected);
  });

  if (paintMode === "eraser") {
    currentColorLabel.textContent = "Borracha";
    currentColorPreview.style.background =
      "repeating-linear-gradient(135deg, #2a3442 0 8px, #0d1523 8px 16px)";
    return;
  }

  if (paintMode === "rainbow") {
    currentColorLabel.textContent = "Rainbow";
    currentColorPreview.style.background =
      "conic-gradient(#ff3b30, #ff9500, #ffd60a, #34c759, #00c7be, #0a84ff, #5e5ce6, #bf5af2, #ff2d55, #ff3b30)";
    return;
  }

  currentColorLabel.textContent = "Cor atual";
  currentColorPreview.style.background = selectedColor;
}

function setPaintMode(mode) {
  paintMode = mode;
  if (paintMode === "rainbow") rainbowPixelCounter = 0;
  updateToolbarUi();
}

function setSelectedColor(color) {
  selectedColor = color.toLowerCase();
  colorPicker.value = selectedColor;
  paintMode = "paint";
  updateToolbarUi();
}

function openModal(modal) {
  modal.classList.remove("hidden");
}

function closeModal(modal) {
  modal.classList.add("hidden");
}

/* ===== Three.js ===== */
const scene = new THREE.Scene();
scene.background = new THREE.Color(0x08111e);

function getViewportSize() {
  const rect = app3d.getBoundingClientRect();
  return {
    width: Math.max(1, Math.floor(rect.width)),
    height: Math.max(1, Math.floor(rect.height))
  };
}

const initialSize = getViewportSize();

const camera = new THREE.PerspectiveCamera(
  45,
  initialSize.width / initialSize.height,
  0.1,
  1000
);
camera.position.set(0, 9, 25);

const renderer = new THREE.WebGLRenderer({ antialias: true });
renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
renderer.setSize(initialSize.width, initialSize.height);
app3d.appendChild(renderer.domElement);
renderer.domElement.addEventListener("contextmenu", (event) => event.preventDefault());

renderer.domElement.addEventListener("wheel", (event) => {
  event.preventDefault();

  const offset = new THREE.Vector3();
  offset.copy(camera.position).sub(controls.target);

  const currentDistance = offset.length();
  const zoomFactor = event.deltaY > 0 ? 1.04 : 0.96;
  let nextDistance = currentDistance * zoomFactor;

  nextDistance = Math.max(controls.minDistance, Math.min(controls.maxDistance, nextDistance));

  offset.setLength(nextDistance);
  camera.position.copy(controls.target).add(offset);
}, { passive: false });

const controls = new OrbitControls(camera, renderer.domElement);
controls.enableDamping = true;
controls.dampingFactor = 0.08;
controls.enablePan = true;
controls.enableZoom = false;
controls.minDistance = 8;
controls.maxDistance = 70;
controls.zoomSpeed = 0.35;
controls.rotateSpeed = 0.72;
controls.panSpeed = 0.55;
controls.target.set(0, 5.0, 0);
controls.mouseButtons = {
  LEFT: THREE.MOUSE.ROTATE,
  MIDDLE: THREE.MOUSE.DOLLY,
  RIGHT: THREE.MOUSE.PAN
};
controls.touches = {
  ONE: THREE.TOUCH.ROTATE,
  TWO: THREE.TOUCH.DOLLY_PAN
};

scene.add(new THREE.AmbientLight(0xffffff, 1.4));

const keyLight = new THREE.DirectionalLight(0xffffff, 1.5);
keyLight.position.set(10, 18, 12);
scene.add(keyLight);

const rimLight = new THREE.DirectionalLight(0x66d9ff, 0.7);
rimLight.position.set(-8, 10, -12);
scene.add(rimLight);

const ground = new THREE.Mesh(
  new THREE.PlaneGeometry(30, 30),
  new THREE.MeshStandardMaterial({
    color: 0xaed0dc,
    transparent: true,
    opacity: 0.18,
    side: THREE.DoubleSide
  })
);
ground.rotation.x = -Math.PI / 2;
ground.position.y = -2.2;
scene.add(ground);

const leds = [];
const raycaster = new THREE.Raycaster();
const pointer = new THREE.Vector2();

function makeLed(x, y, z, family, stripIndex, ledIndex, defaultColor) {
  const geometry = new THREE.SphereGeometry(0.11, 12, 12);
  const material = new THREE.MeshStandardMaterial({
    color: new THREE.Color(defaultColor),
    emissive: new THREE.Color(defaultColor),
    emissiveIntensity: 0.35,
    metalness: 0.12,
    roughness: 0.35
  });

  const mesh = new THREE.Mesh(geometry, material);
  mesh.position.set(x, y, z);
  mesh.userData = {
    family,
    stripIndex,
    ledIndex,
    baseColor: defaultColor,
    currentColor: defaultColor,
    off: false
  };

  scene.add(mesh);
  leds.push(mesh);
}

function smooth01(t) {
  return t * t * (3 - 2 * t);
}

function lerp(a, b, t) {
  return a + (b - a) * t;
}

function radiusProfileBlue(t) {
  if (t < 0.16) {
    const k = smooth01(t / 0.16);
    return lerp(8.1, 6.2, k);
  }
  if (t < 0.84) {
    const k = (t - 0.16) / 0.68;
    return lerp(6.2, 6.0, k);
  }
  const k = smooth01((t - 0.84) / 0.16);
  return lerp(6.0, 5.55, k);
}

function createBlueStructure() {
  const stripCount = 72;
  const pointsPerStrip = 34;
  const height = 11.6;
  const yStart = -1.25;

  for (let i = 0; i < stripCount; i++) {
    const angle = (i / stripCount) * Math.PI * 2;

    for (let j = 0; j < pointsPerStrip; j++) {
      const t = j / (pointsPerStrip - 1);
      const r = radiusProfileBlue(t);

      let y = yStart + t * height;
      if (t > 0.86) {
        const k = (t - 0.86) / 0.14;
        y += Math.sin(k * Math.PI * 0.5) * 0.35;
      }

      const x = Math.cos(angle) * r;
      const z = Math.sin(angle) * r;
      makeLed(x, y, z, "blue", i, j, BASE_BLUE);
    }
  }

  const bottomRingCount = 72;
  const bottomRingPoints = 10;

  for (let i = 0; i < bottomRingCount; i++) {
    const angle = (i / bottomRingCount) * Math.PI * 2;

    for (let j = 0; j < bottomRingPoints; j++) {
      const t = j / (bottomRingPoints - 1);
      const r = lerp(6.35, 8.25, t);
      const y = -1.15 + t * (-1.05);

      const x = Math.cos(angle) * r;
      const z = Math.sin(angle) * r;
      makeLed(x, y, z, "blue", stripCount + i, j, BASE_BLUE);
    }
  }
}

function createGreenStructure() {
  let stripId = 0;
  const outerCount = 24;
  const outerPoints = 24;

  for (let i = 0; i < outerCount; i++) {
    const angle = (i / outerCount) * Math.PI * 2;

    for (let j = 0; j < outerPoints; j++) {
      const t = j / (outerPoints - 1);
      const startR = 5.65;
      const endR = 13.0 + 0.45 * Math.sin(i * 0.9);
      const r = lerp(startR, endR, smooth01(t));
      const tangentWave = Math.sin(t * Math.PI * 1.45 + i * 0.65) * 0.42;
      const radialWave = Math.sin(t * Math.PI * 2.0 + i * 0.3) * 0.12;

      const radialDirX = Math.cos(angle);
      const radialDirZ = Math.sin(angle);
      const tangentDirX = Math.cos(angle + Math.PI / 2);
      const tangentDirZ = Math.sin(angle + Math.PI / 2);

      const x = radialDirX * (r + radialWave) + tangentDirX * tangentWave;
      const z = radialDirZ * (r + radialWave) + tangentDirZ * tangentWave;
      const y = 9.15 + t * 3.35 + Math.sin(t * Math.PI * 1.7 + i * 0.35) * 0.22;
      makeLed(x, y, z, "secondaryBlue", stripId, j, BASE_GREEN);
    }
    stripId++;
  }

  const innerCount = 18;
  const innerPoints = 13;

  for (let i = 0; i < innerCount; i++) {
    const angle = (i / innerCount) * Math.PI * 2 + 0.08;

    for (let j = 0; j < innerPoints; j++) {
      const t = j / (innerPoints - 1);
      const startR = 3.7 + 0.12 * Math.sin(i * 1.8);
      const endR = 5.15 + 0.18 * Math.cos(i * 1.15);
      const r = lerp(startR, endR, smooth01(t));
      const side = Math.sin(t * Math.PI * 1.05 + i * 0.8) * 0.10;

      const radialDirX = Math.cos(angle);
      const radialDirZ = Math.sin(angle);
      const tangentDirX = Math.cos(angle + Math.PI / 2);
      const tangentDirZ = Math.sin(angle + Math.PI / 2);

      const x = radialDirX * r + tangentDirX * side;
      const z = radialDirZ * r + tangentDirZ * side;
      const y = 3.2 + t * 6.0 + Math.sin(t * Math.PI * 1.15 + i * 0.55) * 0.08;
      makeLed(x, y, z, "secondaryBlue", stripId, j, BASE_GREEN);
    }
    stripId++;
  }

  const crownCount = 26;
  const crownPoints = 8;

  for (let i = 0; i < crownCount; i++) {
    const angle = (i / crownCount) * Math.PI * 2 + ((i % 2) * 0.02);

    for (let j = 0; j < crownPoints; j++) {
      const t = j / (crownPoints - 1);
      const r = lerp(4.55, 5.45, t);
      const x = Math.cos(angle) * r;
      const z = Math.sin(angle) * r;
      const y = 8.65 + t * 1.05;
      makeLed(x, y, z, "secondaryBlue", stripId, j, BASE_GREEN);
    }
    stripId++;
  }
}

createBlueStructure();
createGreenStructure();

function updateLedVisual(mesh, colorHex, isOff) {
  const hex = isOff ? OFF_COLOR : colorHex;
  mesh.material.color.set(hex);
  mesh.material.emissive.set(hex);
  mesh.material.emissiveIntensity = isOff ? 0.05 : 0.42;
  mesh.userData.currentColor = hex;
  mesh.userData.off = isOff;
}

function resetTreeColors() {
  leds.forEach((mesh) => updateLedVisual(mesh, mesh.userData.baseColor, false));
}

function applyPresetColorScheme(name) {
  const schemes = {
    noctua: "#ffffff",
    robotnik: "#ff0000",
    utfpr: "#ffcc29",
    parque: "#2bd3e9",
    quack: "#f6ec15",
    alien: "#34c759",
    mushroom: "#ff2d55",
    fogo: "#f29b37"
  };

  const color = schemes[name] || "#00eeff";
  leds.forEach((mesh, index) => {
    if (mesh.userData.family === "secondaryBlue") {
      updateLedVisual(mesh, color, false);
    } else if (index % 3 === 0) {
      updateLedVisual(mesh, "#ffffff", false);
    }
  });
}

function getHitFromEvent(event) {
  const rect = renderer.domElement.getBoundingClientRect();
  pointer.x = ((event.clientX - rect.left) / rect.width) * 2 - 1;
  pointer.y = -((event.clientY - rect.top) / rect.height) * 2 + 1;
  raycaster.setFromCamera(pointer, camera);
  const hits = raycaster.intersectObjects(leds);
  return hits.length ? hits[0].object : null;
}

function paintLedFromEvent(event) {
  const led = getHitFromEvent(event);
  if (!led) return;

  const key = `${led.userData.family}-${led.userData.stripIndex}-${led.userData.ledIndex}`;
  if (key === lastPaintedKey && isDragPainting) return;

  lastPaintedKey = key;
  const color = getCurrentPaintColor();

  if (paintMode === "eraser") {
    updateLedVisual(led, OFF_COLOR, true);
    setStatus(`LED apagado | família ${led.userData.family} | linha ${led.userData.stripIndex} | índice ${led.userData.ledIndex}`);
  } else {
    updateLedVisual(led, color, false);
    setStatus(`LED aceso | família ${led.userData.family} | linha ${led.userData.stripIndex} | índice ${led.userData.ledIndex}`);
  }
}

function updateLockState() {
  controls.enabled = !isViewLocked;
  app3d.classList.toggle("is-locked", isViewLocked);
  lockViewBtn.classList.toggle("is-locked", isViewLocked);
  lockViewBtn.setAttribute("aria-pressed", String(isViewLocked));
  lockViewBtn.textContent = isViewLocked ? "🔒" : "🔓";
  setStatus(isViewLocked
    ? "Movimento travado. Agora você pode pintar arrastando."
    : "Clique em um LED para pintar. Arraste para girar. Scroll para zoom.");
}

lockViewBtn.addEventListener("click", () => {
  isViewLocked = !isViewLocked;
  updateLockState();
});

renderer.domElement.addEventListener("pointerdown", (event) => {
  if (isViewLocked) {
    if (event.button !== 0) return;
    isDragPainting = true;
    lastPaintedKey = null;
    renderer.domElement.setPointerCapture?.(event.pointerId);
    paintLedFromEvent(event);
    event.preventDefault();
    return;
  }

  if (event.button === 0) {
    paintLedFromEvent(event);
  }
});

renderer.domElement.addEventListener("pointermove", (event) => {
  if (!isViewLocked || !isDragPainting) return;
  paintLedFromEvent(event);
  event.preventDefault();
});

function stopDragPainting(event) {
  if (!isDragPainting) return;
  isDragPainting = false;
  lastPaintedKey = null;
  try {
    renderer.domElement.releasePointerCapture?.(event.pointerId);
  } catch (_) {}
}

renderer.domElement.addEventListener("pointerup", stopDragPainting);
renderer.domElement.addEventListener("pointercancel", stopDragPainting);
renderer.domElement.addEventListener("lostpointercapture", () => {
  isDragPainting = false;
  lastPaintedKey = null;
});

function resizeRenderer() {
  const { width, height } = getViewportSize();
  camera.aspect = width / height;
  camera.updateProjectionMatrix();
  renderer.setSize(width, height);
}

window.addEventListener("resize", resizeRenderer);
window.addEventListener("load", resizeRenderer);
setTimeout(resizeRenderer, 50);

function animate() {
  requestAnimationFrame(animate);
  controls.update();
  renderer.render(scene, camera);
}

/* ===== UI wiring from original interface ===== */
clearBtn.addEventListener("click", () => {
  resetTreeColors();
  setStatus("Árvore 3D limpa.");
});

presetBtn.addEventListener("click", () => openModal(presetModal));

mediaBtn.addEventListener("click", () => {
  mediaInput.click();
});

mediaInput.addEventListener("change", () => {
  setStatus("Foto/Vídeo ainda não foi integrado na árvore 3D.");
  mediaInput.value = "";
});

toggleColorsBtn.addEventListener("click", () => {
  const isHiddenNow = paintToolbar.classList.toggle("is-hidden");

  if (isHiddenNow) {
    toggleColorsBtn.textContent = "Cores";
    card.classList.remove("colors-open");
  } else {
    toggleColorsBtn.textContent = "Fechar cores";
    card.classList.add("colors-open");
  }
});

document.querySelectorAll("[data-close]").forEach((button) => {
  button.addEventListener("click", () => {
    const modalId = button.getAttribute("data-close");
    closeModal(document.getElementById(modalId));
  });
});

window.addEventListener("click", (event) => {
  if (event.target === presetModal) closeModal(presetModal);
});

window.addEventListener("keydown", (event) => {
  if (event.key === "Escape") closeModal(presetModal);
});

modeButtons.forEach((button) => {
  button.addEventListener("click", () => {
    setPaintMode(button.dataset.mode);
    setStatus(`Modo ${button.textContent} ativo.`);
  });
});

swatchButtons.forEach((button) => {
  button.addEventListener("click", () => {
    setSelectedColor(button.dataset.color);
    setStatus(`Cor atual: ${button.dataset.color.toLowerCase()}`);
  });
});

colorPicker.addEventListener("input", (event) => {
  setSelectedColor(event.target.value);
  setStatus(`Cor atual: ${event.target.value.toLowerCase()}`);
});

presetButtons.forEach((button) => {
  button.addEventListener("click", () => {
    applyPresetColorScheme(button.dataset.preset);
    closeModal(presetModal);
    setStatus(`Preset ${button.textContent} aplicado nas cores da árvore.`);
  });
});

setSelectedColor(selectedColor);
updateLockState();
resizeRenderer();
animate();
window.benaLed3DState = leds;
