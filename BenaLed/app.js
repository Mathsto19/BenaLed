const GRID_SIZE = 32;
const OFF_COLOR = "#000000";

const IMPORT_PREVIEW_SCALE = 12;
const IMPORT_PREVIEW_SIZE = GRID_SIZE * IMPORT_PREVIEW_SCALE;
const IMPORT_TRIM_ALPHA_THRESHOLD = 8;
const IMPORT_CELL_ALPHA_THRESHOLD = 0.12;

const canvas = document.getElementById("ledCanvas");
const ctx = canvas.getContext("2d");

const clearBtn = document.getElementById("clearBtn");
const presetBtn = document.getElementById("presetBtn");
const toggleColorsBtn = document.getElementById("toggleColorsBtn");
const paintToolbar = document.getElementById("paintToolbar");
const card = document.querySelector(".card");

const presetModal = document.getElementById("presetModal");
const colorPicker = document.getElementById("colorPicker");
const currentColorPreview = document.getElementById("currentColorPreview");
const currentColorLabel = document.getElementById("currentColorLabel");

const modeButtons = document.querySelectorAll("[data-mode]");
const swatchButtons = document.querySelectorAll(".inline-palette .color-swatch");
const presetButtons = document.querySelectorAll(".preset-item");

const mediaBtn = document.getElementById("mediaBtn");
const mediaInput = document.getElementById("mediaInput");
const fullscreenBtn = document.getElementById("fullscreenBtn");
const mediaPlaybackBtn = document.getElementById("mediaPlaybackBtn");

let activeVideoElement = null;
let activeVideoUrl = null;
let videoAnimationFrameId = null;
let isStoppingVideoPlayback = false;
let activeGifImage = null;
let activeGifUrl = null;
let gifAnimationFrameId = null;
let activeGifState = null;
let gifPlaybackTimer = null;

let selectedColor = "#00eeff";
let paintMode = "paint";
let isDrawing = false;
let lastPaintedCell = null;
let rainbowPixelCounter = 0;
let fullscreenAutoAttempted = false;
let shouldRestoreFullscreenAfterMediaPicker = false;
let fullscreenRestoreAfterMediaPickerTimer = null;
let fullscreenRestoreAfterMediaPickerAttempts = 0;
let isRestoringFullscreenAfterMediaPicker = false;

const FULLSCREEN_RESTORE_RETRY_DELAY_MS = 180;
const FULLSCREEN_RESTORE_MAX_ATTEMPTS = 28;

const EASTER_EGG_COLOR = "#060708"; 
const GIF_EASTER_EGG_COLOR = "#5c5d05"; 

const GIF_EASTER_EGG_PATHS = [
  "./spfc.gif",
  "spfc.gif",
  "./Complemento/spfc.gif",
  "/spfc.gif"
];

let frame = createEmptyFrame();

function createEmptyFrame() {
  return Array.from({ length: GRID_SIZE }, () =>
    Array.from({ length: GRID_SIZE }, () => OFF_COLOR)
  );
}

function cloneFrame(source) {
  return source.map((row) => [...row]);
}

function getFullscreenElement() {
  return document.fullscreenElement || document.webkitFullscreenElement || null;
}

function isStandaloneDisplayMode() {
  return Boolean(
    window.matchMedia?.("(display-mode: standalone)")?.matches ||
    window.navigator.standalone === true
  );
}

function isIPhoneDevice() {
  return /iPhone/i.test(window.navigator.userAgent || "");
}

function canUseFullscreen() {
  return Boolean(
    document.fullscreenEnabled ||
    document.webkitFullscreenEnabled ||
    document.documentElement.requestFullscreen ||
    document.documentElement.webkitRequestFullscreen
  );
}

function requestAppFullscreen() {
  const target = document.documentElement;

  if (target.requestFullscreen) {
    return target.requestFullscreen();
  }

  if (target.webkitRequestFullscreen) {
    target.webkitRequestFullscreen();
    return Promise.resolve();
  }

  return Promise.reject(new Error("Fullscreen API indisponivel"));
}

function exitAppFullscreen() {
  if (document.exitFullscreen) {
    return document.exitFullscreen();
  }

  if (document.webkitExitFullscreen) {
    document.webkitExitFullscreen();
    return Promise.resolve();
  }

  return Promise.reject(new Error("Nao foi possivel sair do fullscreen"));
}

function showFullscreenFallbackMessage() {
  const isStandalone = isStandaloneDisplayMode();
  const isIPhone = isIPhoneDevice();

  const message = isIPhone && !isStandalone
    ? "No iPhone, o Safari não libera fullscreen para páginas web."
    : isStandalone
      ? "Este navegador não liberou fullscreen para esta pagina."
      : "Este navegador do celular não liberou fullscreen para esta pagina.";

  alert(message);
}

function updateFullscreenButton() {
  if (!fullscreenBtn) return;

  const supported = canUseFullscreen();
  const isActive = Boolean(getFullscreenElement());
  const isIPhone = isIPhoneDevice();
  const isStandalone = isStandaloneDisplayMode();

  let label = supported
    ? (isActive ? "Sair da tela cheia" : "Entrar em tela cheia")
    : "Fullscreen indisponivel";

  if (!supported && isIPhone && !isStandalone) {
    label = "No iPhone, use Adicionar a Tela de Inicio";
  } else if (!supported && isStandalone) {
    label = "Modo app ativo";
  }

  fullscreenBtn.removeAttribute("disabled");
  fullscreenBtn.setAttribute("aria-label", label);
  fullscreenBtn.setAttribute("title", label);
  fullscreenBtn.setAttribute("aria-pressed", String(isActive));
  fullscreenBtn.setAttribute("aria-disabled", String(!supported));
  fullscreenBtn.classList.toggle("is-active", isActive);
  fullscreenBtn.classList.toggle("is-unsupported", !supported);
  document.documentElement.classList.toggle("is-fullscreen", isActive);
}

function getLoadedPlayableMediaType() {
  if (activeVideoElement) return "video";
  if (activeGifState) return "gif";
  return null;
}

function isLoadedPlayableMediaPaused() {
  if (activeVideoElement) {
    return activeVideoElement.paused || activeVideoElement.ended;
  }

  if (activeGifState) {
    return Boolean(activeGifState.isPaused);
  }

  return false;
}

function updateMediaPlaybackButton() {
  if (!mediaPlaybackBtn) return;

  const mediaType = getLoadedPlayableMediaType();
  const hasPlayableMedia = Boolean(mediaType);

  mediaPlaybackBtn.hidden = !hasPlayableMedia;
  mediaPlaybackBtn.setAttribute("aria-hidden", String(!hasPlayableMedia));

  if (!hasPlayableMedia) {
    mediaPlaybackBtn.classList.remove("is-paused");
    mediaPlaybackBtn.setAttribute("aria-pressed", "false");
    return;
  }

  const isPaused = isLoadedPlayableMediaPaused();
  const labelTarget = mediaType === "gif" ? "GIF" : "video";
  const label = isPaused ? `Iniciar ${labelTarget}` : `Parar ${labelTarget}`;

  mediaPlaybackBtn.setAttribute("aria-label", label);
  mediaPlaybackBtn.setAttribute("title", label);
  mediaPlaybackBtn.setAttribute("aria-pressed", String(!isPaused));
  mediaPlaybackBtn.classList.toggle("is-paused", isPaused);
}

function attemptFullscreenFromGesture() {
  if (!canUseFullscreen()) return;
  const shouldRestore = shouldRestoreFullscreenAfterMediaPicker && !getFullscreenElement();

  if (!shouldRestore && (fullscreenAutoAttempted || getFullscreenElement())) return;

  if (!shouldRestore) {
    fullscreenAutoAttempted = true;
  }

  requestAppFullscreen()
    .catch(() => {})
    .finally(() => {
      if (getFullscreenElement()) {
        shouldRestoreFullscreenAfterMediaPicker = false;
      }
      updateFullscreenButton();
    });
}

function clearFullscreenRestoreAfterMediaPickerTimer() {
  if (fullscreenRestoreAfterMediaPickerTimer !== null) {
    clearTimeout(fullscreenRestoreAfterMediaPickerTimer);
    fullscreenRestoreAfterMediaPickerTimer = null;
  }

  fullscreenRestoreAfterMediaPickerAttempts = 0;
}

async function restoreFullscreenAfterMediaPicker() {
  if (!shouldRestoreFullscreenAfterMediaPicker || !canUseFullscreen()) return false;
  if (isRestoringFullscreenAfterMediaPicker) return false;

  if (getFullscreenElement()) {
    shouldRestoreFullscreenAfterMediaPicker = false;
    clearFullscreenRestoreAfterMediaPickerTimer();
    updateFullscreenButton();
    return true;
  }

  isRestoringFullscreenAfterMediaPicker = true;
  try {
    await requestAppFullscreen();
  } catch (_) {
    return false;
  } finally {
    isRestoringFullscreenAfterMediaPicker = false;
    if (getFullscreenElement()) {
      shouldRestoreFullscreenAfterMediaPicker = false;
      clearFullscreenRestoreAfterMediaPickerTimer();
    }
    updateFullscreenButton();
  }

  return Boolean(getFullscreenElement());
}

function scheduleFullscreenRestoreAfterMediaPicker(immediate = false) {
  if (!shouldRestoreFullscreenAfterMediaPicker || !canUseFullscreen()) return;
  if (getFullscreenElement()) {
    shouldRestoreFullscreenAfterMediaPicker = false;
    clearFullscreenRestoreAfterMediaPickerTimer();
    updateFullscreenButton();
    return;
  }

  if (fullscreenRestoreAfterMediaPickerTimer !== null) {
    if (!immediate) return;

    clearTimeout(fullscreenRestoreAfterMediaPickerTimer);
    fullscreenRestoreAfterMediaPickerTimer = null;
  }

  fullscreenRestoreAfterMediaPickerTimer = setTimeout(async () => {
    fullscreenRestoreAfterMediaPickerTimer = null;

    if (!shouldRestoreFullscreenAfterMediaPicker || getFullscreenElement()) {
      shouldRestoreFullscreenAfterMediaPicker = false;
      clearFullscreenRestoreAfterMediaPickerTimer();
      updateFullscreenButton();
      return;
    }

    const restored = await restoreFullscreenAfterMediaPicker();

    if (restored || !shouldRestoreFullscreenAfterMediaPicker) {
      return;
    }

    fullscreenRestoreAfterMediaPickerAttempts += 1;

    if (fullscreenRestoreAfterMediaPickerAttempts >= FULLSCREEN_RESTORE_MAX_ATTEMPTS) {
      return;
    }

    scheduleFullscreenRestoreAfterMediaPicker();
  }, immediate ? 0 : FULLSCREEN_RESTORE_RETRY_DELAY_MS);
}

async function toggleFullscreenFromButton() {
  if (!canUseFullscreen()) {
    showFullscreenFallbackMessage();
    return;
  }

  try {
    if (getFullscreenElement()) {
      await exitAppFullscreen();
    } else {
      await requestAppFullscreen();
    }
  } catch (error) {
    console.warn("Falha ao alternar fullscreen.", error);
    alert("O navegador bloqueou a tela cheia. Tente tocar no botao novamente ou abrir como app/PWA.");
  } finally {
    updateFullscreenButton();
  }
}


function frameToRgbMatrix(sourceFrame = frame) {
  return sourceFrame.map((row) =>
    row.map((color) => color.replace("#", "").toLowerCase())
  );
}

function frameToRgbMatrixString(sourceFrame = frame, variableName = "liveFrame") {
  const rows = frameToRgbMatrix(sourceFrame)
    .map((row) => `  [${row.map((color) => `"${color}"`).join(", ")}]`)
    .join(",\n");

  return `const ${variableName} = [\n${rows}\n];`;
}

window.getCurrentLedMatrix = () => frameToRgbMatrix(frame);
window.getCurrentLedMatrixString = () => frameToRgbMatrixString(frame);
window.copyLedMatrixToClipboard = async () => {
  const matrixString = frameToRgbMatrixString(frame);
  await navigator.clipboard.writeText(matrixString);
  return matrixString;
};
window.benaLedLiveMatrix = frameToRgbMatrix(frame);

function hslToHex(h, s, l) {
  s /= 100;
  l /= 100;

  const c = (1 - Math.abs(2 * l - 1)) * s;
  const x = c * (1 - Math.abs((h / 60) % 2 - 1));
  const m = l - c / 2;

  let r = 0;
  let g = 0;
  let b = 0;

  if (h < 60) {
    r = c; g = x; b = 0;
  } else if (h < 120) {
    r = x; g = c; b = 0;
  } else if (h < 180) {
    r = 0; g = c; b = x;
  } else if (h < 240) {
    r = 0; g = x; b = c;
  } else if (h < 300) {
    r = x; g = 0; b = c;
  } else {
    r = c; g = 0; b = x;
  }

  const toHex = (value) =>
    Math.round((value + m) * 255)
      .toString(16)
      .padStart(2, "0");

  return `#${toHex(r)}${toHex(g)}${toHex(b)}`;
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

  if (paintMode === "rainbow") {
    rainbowPixelCounter = 0;
  }

  updateToolbarUi();
}

async function triggerGifEasterEgg() {
  stopActiveMediaPlayback();

  let lastError = null;

  for (const path of GIF_EASTER_EGG_PATHS) {
    try {
      const response = await fetch(path, { cache: "no-store" });
      const contentType = response.headers.get("content-type") || "";

      if (!response.ok) {
        throw new Error(`HTTP ${response.status} em ${path}`);
      }

      if (!contentType.includes("gif")) {
        throw new Error(`Arquivo em ${path} não veio como GIF (${contentType || "sem content-type"})`);
      }

      const gifBlob = await response.blob();
      await loadGifFileToFrame(gifBlob);
      return;
    } catch (error) {
      lastError = error;
      console.warn("Falha ao tentar carregar easter egg GIF em:", path, error);
    }
  }

  console.error("Nenhum caminho do easter egg GIF funcionou.", lastError);
  alert("Não foi possível carregar o GIF do easter egg.");
}

function setSelectedColor(color) {
  selectedColor = color.toLowerCase();
  colorPicker.value = selectedColor;
  paintMode = "paint";
  updateToolbarUi();

  if (selectedColor === EASTER_EGG_COLOR) {
    applyPreset("easteregg");
    return;
  }

  if (selectedColor === GIF_EASTER_EGG_COLOR) {
    triggerGifEasterEgg();
  }
}

function getRainbowColor() {
  const hue = (Math.floor(rainbowPixelCounter / 3) * 20) % 360;
  rainbowPixelCounter += 1;
  return hslToHex(hue, 100, 60);
}

function getCurrentPaintColor() {
  if (paintMode === "eraser") return OFF_COLOR;
  if (paintMode === "rainbow") return getRainbowColor();
  return selectedColor;
}

function openModal(modal) {
  modal.classList.remove("hidden");
}

function closeModal(modal) {
  modal.classList.add("hidden");
}

function roundRectPath(context, x, y, width, height, radius) {
  const r = Math.min(radius, width / 2, height / 2);

  context.beginPath();
  context.moveTo(x + r, y);
  context.lineTo(x + width - r, y);
  context.quadraticCurveTo(x + width, y, x + width, y + r);
  context.lineTo(x + width, y + height - r);
  context.quadraticCurveTo(x + width, y + height, x + width - r, y + height);
  context.lineTo(x + r, y + height);
  context.quadraticCurveTo(x, y + height, x, y + height - r);
  context.lineTo(x, y + r);
  context.quadraticCurveTo(x, y, x + r, y);
  context.closePath();
}

function hexToRgba(hex, alpha) {
  const normalized = hex.replace("#", "");
  const value = normalized.length === 3
    ? normalized.split("").map((c) => c + c).join("")
    : normalized;

  const r = parseInt(value.slice(0, 2), 16);
  const g = parseInt(value.slice(2, 4), 16);
  const b = parseInt(value.slice(4, 6), 16);

  return `rgba(${r}, ${g}, ${b}, ${alpha})`;
}

function rgbToHex(r, g, b) {
  return `#${[r, g, b]
    .map((value) => Math.max(0, Math.min(255, value)).toString(16).padStart(2, "0"))
    .join("")}`;
}

function createWorkCanvas(width, height) {
  const workCanvas = document.createElement("canvas");
  workCanvas.width = width;
  workCanvas.height = height;
  return workCanvas;
}

function getCanvas2dContext(targetCanvas) {
  return targetCanvas.getContext("2d", { willReadFrequently: true });
}

function loadImageElementFromFile(file) {
  return new Promise((resolve, reject) => {
    const imageUrl = URL.createObjectURL(file);
    const img = new Image();

    img.onload = () => {
      URL.revokeObjectURL(imageUrl);
      resolve(img);
    };

    img.onerror = () => {
      URL.revokeObjectURL(imageUrl);
      reject(new Error("Não foi possível decodificar a imagem."));
    };

    img.decoding = "async";
    img.src = imageUrl;
  });
}

function findVisibleBounds(imageData, width, height, alphaThreshold = IMPORT_TRIM_ALPHA_THRESHOLD) {
  let minX = width;
  let minY = height;
  let maxX = -1;
  let maxY = -1;

  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      const index = (y * width + x) * 4;
      const alpha = imageData[index + 3];

      if (alpha > alphaThreshold) {
        if (x < minX) minX = x;
        if (y < minY) minY = y;
        if (x > maxX) maxX = x;
        if (y > maxY) maxY = y;
      }
    }
  }

  if (maxX === -1 || maxY === -1) {
    return {
      x: 0,
      y: 0,
      width,
      height,
    };
  }

  return {
    x: minX,
    y: minY,
    width: maxX - minX + 1,
    height: maxY - minY + 1,
  };
}

function buildImportPreviewCanvas(img) {
  const sourceWidth = img.videoWidth || img.naturalWidth || img.width;
  const sourceHeight = img.videoHeight || img.naturalHeight || img.height;

  const sourceCanvas = createWorkCanvas(sourceWidth, sourceHeight);
  const sourceCtx = getCanvas2dContext(sourceCanvas);

  sourceCtx.clearRect(0, 0, sourceWidth, sourceHeight);
  sourceCtx.drawImage(img, 0, 0, sourceWidth, sourceHeight);

  const sourceImageData = sourceCtx.getImageData(0, 0, sourceWidth, sourceHeight);
  const visibleBounds = findVisibleBounds(
    sourceImageData.data,
    sourceWidth,
    sourceHeight
  );

  const previewCanvas = createWorkCanvas(IMPORT_PREVIEW_SIZE, IMPORT_PREVIEW_SIZE);
  const previewCtx = getCanvas2dContext(previewCanvas);

  previewCtx.clearRect(0, 0, IMPORT_PREVIEW_SIZE, IMPORT_PREVIEW_SIZE);
  previewCtx.imageSmoothingEnabled = true;
  previewCtx.imageSmoothingQuality = "high";

  const scale = Math.min(
    IMPORT_PREVIEW_SIZE / visibleBounds.width,
    IMPORT_PREVIEW_SIZE / visibleBounds.height
  );

  const drawWidth = Math.max(1, Math.round(visibleBounds.width * scale));
  const drawHeight = Math.max(1, Math.round(visibleBounds.height * scale));

  const offsetX = Math.floor((IMPORT_PREVIEW_SIZE - drawWidth) / 2);
  const offsetY = Math.floor((IMPORT_PREVIEW_SIZE - drawHeight) / 2);

  previewCtx.drawImage(
    sourceCanvas,
    visibleBounds.x,
    visibleBounds.y,
    visibleBounds.width,
    visibleBounds.height,
    offsetX,
    offsetY,
    drawWidth,
    drawHeight
  );

  return previewCanvas;
}

function samplePreviewCanvasToFrame(previewCanvas) {
  const previewCtx = getCanvas2dContext(previewCanvas);
  const { width, height } = previewCanvas;
  const pixels = previewCtx.getImageData(0, 0, width, height).data;

  const blockWidth = width / GRID_SIZE;
  const blockHeight = height / GRID_SIZE;

  const newFrame = createEmptyFrame();

  for (let gridY = 0; gridY < GRID_SIZE; gridY++) {
    for (let gridX = 0; gridX < GRID_SIZE; gridX++) {
      const startX = Math.floor(gridX * blockWidth);
      const endX = Math.max(startX + 1, Math.floor((gridX + 1) * blockWidth));
      const startY = Math.floor(gridY * blockHeight);
      const endY = Math.max(startY + 1, Math.floor((gridY + 1) * blockHeight));

      let weightedR = 0;
      let weightedG = 0;
      let weightedB = 0;
      let alphaWeightSum = 0;
      let alphaCoverage = 0;

      const totalSamples = (endX - startX) * (endY - startY);

      for (let y = startY; y < endY; y++) {
        for (let x = startX; x < endX; x++) {
          const index = (y * width + x) * 4;
          const r = pixels[index];
          const g = pixels[index + 1];
          const b = pixels[index + 2];
          const a = pixels[index + 3] / 255;

          alphaCoverage += a;

          if (a <= 0) continue;

          weightedR += r * a;
          weightedG += g * a;
          weightedB += b * a;
          alphaWeightSum += a;
        }
      }

      const coverageRatio = totalSamples > 0 ? alphaCoverage / totalSamples : 0;

      if (alphaWeightSum <= 0 || coverageRatio < IMPORT_CELL_ALPHA_THRESHOLD) {
        newFrame[gridY][gridX] = OFF_COLOR;
        continue;
      }

      const finalR = Math.round(weightedR / alphaWeightSum);
      const finalG = Math.round(weightedG / alphaWeightSum);
      const finalB = Math.round(weightedB / alphaWeightSum);

      newFrame[gridY][gridX] = rgbToHex(finalR, finalG, finalB);
    }
  }

  return newFrame;
}

async function loadImageFileToFrame(file) {
  stopActiveVideoPlayback();
  stopActiveGifPlayback();

  try {
    const img = await loadImageElementFromFile(file);
    const previewCanvas = buildImportPreviewCanvas(img);
    const convertedFrame = samplePreviewCanvasToFrame(previewCanvas);

    frame = convertedFrame;
    render();
    scheduleConsoleExport();
  } catch (error) {
    console.error(error);
    alert("Não foi possível carregar a imagem selecionada.");
  }
}

function stopActiveMediaPlayback() {
  stopActiveVideoPlayback();
  stopActiveGifPlayback();
}

function stopActiveGifPlayback() {
  if (gifPlaybackTimer !== null) {
    clearTimeout(gifPlaybackTimer);
    gifPlaybackTimer = null;
  }

  if (gifAnimationFrameId !== null) {
    cancelAnimationFrame(gifAnimationFrameId);
    gifAnimationFrameId = null;
  }

  if (activeGifUrl) {
    URL.revokeObjectURL(activeGifUrl);
    activeGifUrl = null;
  }

  activeGifImage = null;
  activeGifState = null;
  updateMediaPlaybackButton();
}

async function loadImageFileToFrame(file) {
  stopActiveVideoPlayback();
  stopActiveGifPlayback();

  try {
    const img = await loadImageElementFromFile(file);
    const previewCanvas = buildImportPreviewCanvas(img);
    const convertedFrame = samplePreviewCanvasToFrame(previewCanvas);

    frame = convertedFrame;
    render();
    scheduleConsoleExport();
  } catch (error) {
    console.error(error);
    alert("Não foi possível carregar a imagem selecionada.");
  }
}

async function decodeGifFrames(file) {
  if (typeof window.GIF !== "function") {
    throw new Error("A biblioteca gifuct-js não foi carregada corretamente.");
  }

  const buffer = await file.arrayBuffer();
  const gif = new window.GIF(buffer);
  const rawFrames = gif.decompressFrames(true);

  const gifWidth = gif.raw.lsd.width;
  const gifHeight = gif.raw.lsd.height;

  const workCanvas = createWorkCanvas(gifWidth, gifHeight);
  const workCtx = getCanvas2dContext(workCanvas);
  workCtx.clearRect(0, 0, gifWidth, gifHeight);

  const decodedFrames = [];

  for (const rawFrame of rawFrames) {
    let previousSnapshot = null;

    if (rawFrame.disposalType === 3) {
      previousSnapshot = workCtx.getImageData(0, 0, gifWidth, gifHeight);
    }

    const patchImageData = workCtx.createImageData(
      rawFrame.dims.width,
      rawFrame.dims.height
    );
    patchImageData.data.set(rawFrame.patch);

    workCtx.putImageData(
      patchImageData,
      rawFrame.dims.left,
      rawFrame.dims.top
    );

    const frameCanvas = createWorkCanvas(gifWidth, gifHeight);
    const frameCtx = getCanvas2dContext(frameCanvas);
    frameCtx.clearRect(0, 0, gifWidth, gifHeight);
    frameCtx.drawImage(workCanvas, 0, 0);

    decodedFrames.push({
      canvas: frameCanvas,
      delayMs: Math.max(20, rawFrame.delay || 100),
    });

    if (rawFrame.disposalType === 2) {
      workCtx.clearRect(
        rawFrame.dims.left,
        rawFrame.dims.top,
        rawFrame.dims.width,
        rawFrame.dims.height
      );
    } else if (rawFrame.disposalType === 3 && previousSnapshot) {
      workCtx.putImageData(previousSnapshot, 0, 0);
    }
  }

  return decodedFrames;
}

function stopActiveVideoPlayback() {
  isStoppingVideoPlayback = true;

  if (videoAnimationFrameId !== null) {
    cancelAnimationFrame(videoAnimationFrameId);
    videoAnimationFrameId = null;
  }

  if (activeVideoElement) {
    activeVideoElement.pause();

    activeVideoElement.onloadedmetadata = null;
    activeVideoElement.onerror = null;

    activeVideoElement.removeAttribute("src");
    activeVideoElement.load();

    activeVideoElement = null;
  }

  if (activeVideoUrl) {
    URL.revokeObjectURL(activeVideoUrl);
    activeVideoUrl = null;
  }

  isStoppingVideoPlayback = false;
  updateMediaPlaybackButton();
}



function renderGifCanvasFrame(frameCanvas) {
  const previewCanvas = buildImportPreviewCanvas(frameCanvas);
  const convertedFrame = samplePreviewCanvasToFrame(previewCanvas);

  frame = convertedFrame;
  render();
  scheduleConsoleExport();
}

function playNextGifFrame() {
  if (!activeGifState || activeGifState.frames.length === 0 || activeGifState.isPaused) return;

  const currentFrame = activeGifState.frames[activeGifState.frameIndex];

  renderGifCanvasFrame(currentFrame.canvas);
  activeGifState.lastDelayMs = currentFrame.delayMs;

  activeGifState.frameIndex =
    (activeGifState.frameIndex + 1) % activeGifState.frames.length;

  gifPlaybackTimer = setTimeout(() => {
    gifPlaybackTimer = null;
    playNextGifFrame();
  }, currentFrame.delayMs);
}

function pauseActiveGifPlayback() {
  if (!activeGifState) return;

  activeGifState.isPaused = true;

  if (gifPlaybackTimer !== null) {
    clearTimeout(gifPlaybackTimer);
    gifPlaybackTimer = null;
  }

  updateMediaPlaybackButton();
}

function resumeActiveGifPlayback() {
  if (!activeGifState) return;

  activeGifState.isPaused = false;
  updateMediaPlaybackButton();

  const delayMs = activeGifState.lastDelayMs ?? 0;

  if (delayMs > 0) {
    gifPlaybackTimer = setTimeout(() => {
      gifPlaybackTimer = null;
      playNextGifFrame();
    }, delayMs);
    return;
  }

  playNextGifFrame();
}

async function loadGifFileToFrame(file) {
  stopActiveVideoPlayback();
  stopActiveGifPlayback();

  try {
    const decodedFrames = await decodeGifFrames(file);

    if (!decodedFrames.length) {
      throw new Error("GIF sem frames válidos.");
    }

    activeGifState = {
      frames: decodedFrames,
      frameIndex: 0,
      isPaused: false,
      lastDelayMs: null,
    };

    updateMediaPlaybackButton();
    playNextGifFrame();
  } catch (error) {
    console.error(error);
    stopActiveGifPlayback();
    alert(error.message || "Não foi possível carregar o GIF selecionado.");
  }
}

function renderVideoFrameToFrame(video) {
  const previewCanvas = buildImportPreviewCanvas(video);
  const convertedFrame = samplePreviewCanvasToFrame(previewCanvas);

  frame = convertedFrame;
  render();
  scheduleConsoleExport();
}

function animateVideoToLedFrame() {
  videoAnimationFrameId = null;

  if (!activeVideoElement) return;

  if (
    activeVideoElement.readyState >= 2 &&
    !activeVideoElement.paused &&
    !activeVideoElement.ended
  ) {
    renderVideoFrameToFrame(activeVideoElement);
  }

  if (activeVideoElement.paused || activeVideoElement.ended) {
    updateMediaPlaybackButton();
    return;
  }

  videoAnimationFrameId = requestAnimationFrame(animateVideoToLedFrame);
}

function startVideoAnimationLoop() {
  if (videoAnimationFrameId !== null) return;
  videoAnimationFrameId = requestAnimationFrame(animateVideoToLedFrame);
}

function pauseActiveVideoPlayback() {
  if (!activeVideoElement) return;

  if (videoAnimationFrameId !== null) {
    cancelAnimationFrame(videoAnimationFrameId);
    videoAnimationFrameId = null;
  }

  activeVideoElement.pause();

  if (activeVideoElement.readyState >= 2) {
    renderVideoFrameToFrame(activeVideoElement);
  }

  updateMediaPlaybackButton();
}

async function resumeActiveVideoPlayback() {
  if (!activeVideoElement) return;

  try {
    await activeVideoElement.play();
    startVideoAnimationLoop();
  } catch (error) {
    console.error(error);
    alert("Nao foi possivel reiniciar o video.");
  } finally {
    updateMediaPlaybackButton();
  }
}

function toggleActiveMediaPlayback() {
  if (activeVideoElement) {
    if (activeVideoElement.paused || activeVideoElement.ended) {
      resumeActiveVideoPlayback();
    } else {
      pauseActiveVideoPlayback();
    }
    return;
  }

  if (activeGifState) {
    if (activeGifState.isPaused) {
      resumeActiveGifPlayback();
    } else {
      pauseActiveGifPlayback();
    }
  }
}

function loadVideoFileToFrame(file) {
  stopActiveVideoPlayback();
  stopActiveGifPlayback();

  const videoUrl = URL.createObjectURL(file);
  const video = document.createElement("video");

  activeVideoElement = video;
  activeVideoUrl = videoUrl;

  video.src = videoUrl;
  video.loop = true;
  video.muted = true;
  video.playsInline = true;
  video.autoplay = true;
  video.preload = "auto";

  video.onloadedmetadata = async () => {
    try {
      await video.play();
      updateMediaPlaybackButton();
      startVideoAnimationLoop();
    } catch (error) {
      console.error(error);
      updateMediaPlaybackButton();
      if (!isStoppingVideoPlayback) {
        alert("Não foi possível iniciar o vídeo automaticamente.");
      }
    }
  };

  video.onerror = () => {
    if (isStoppingVideoPlayback) return;

    stopActiveVideoPlayback();
    alert("Não foi possível carregar o vídeo selecionado.");
  };
}

function isOffCell(color) {
  return color.toLowerCase() === OFF_COLOR;
}

function drawLedCell(x, y, color, cellSize) {
  const gap = Math.max(1.6, cellSize * 0.11);
  const ledSize = cellSize - gap;
  const drawX = x * cellSize + gap / 2;
  const drawY = y * cellSize + gap / 2;
  const radius = Math.max(2, ledSize * 0.24);

  if (isOffCell(color)) {
    ctx.fillStyle = "#04070c";
    roundRectPath(ctx, drawX, drawY, ledSize, ledSize, radius);
    ctx.fill();

    ctx.fillStyle = "#0a1018";
    roundRectPath(ctx, drawX + 1.4, drawY + 1.4, ledSize - 2.8, ledSize - 2.8, Math.max(2, radius - 1));
    ctx.fill();

    ctx.fillStyle = "rgba(255,255,255,0.035)";
    roundRectPath(ctx, drawX + 2.2, drawY + 2.2, ledSize - 4.4, Math.max(2, ledSize * 0.24), Math.max(2, radius - 2));
    ctx.fill();

    return;
  }

  ctx.save();
  ctx.shadowColor = hexToRgba(color, 0.75);
  ctx.shadowBlur = cellSize * 0.5;
  ctx.shadowOffsetX = 0;
  ctx.shadowOffsetY = 0;
  ctx.fillStyle = color;
  roundRectPath(ctx, drawX, drawY, ledSize, ledSize, radius);
  ctx.fill();
  ctx.restore();

  ctx.fillStyle = "rgba(255,255,255,0.18)";
  roundRectPath(
    ctx,
    drawX + 2.2,
    drawY + 2.2,
    ledSize - 4.4,
    Math.max(3, ledSize * 0.26),
    Math.max(2, radius - 2)
  );
  ctx.fill();

  ctx.strokeStyle = hexToRgba("#ffffff", 0.08);
  ctx.lineWidth = 1;
  roundRectPath(ctx, drawX + 0.5, drawY + 0.5, ledSize - 1, ledSize - 1, radius);
  ctx.stroke();
}

function render() {
  const cellSize = canvas.width / GRID_SIZE;

  ctx.clearRect(0, 0, canvas.width, canvas.height);

  ctx.fillStyle = "#05070b";
  ctx.fillRect(0, 0, canvas.width, canvas.height);

  for (let y = 0; y < GRID_SIZE; y++) {
    for (let x = 0; x < GRID_SIZE; x++) {
      drawLedCell(x, y, frame[y][x], cellSize);
    }
  }
}

function getCellFromEvent(event) {
  const rect = canvas.getBoundingClientRect();
  const scaleX = canvas.width / rect.width;
  const scaleY = canvas.height / rect.height;

  const canvasX = (event.clientX - rect.left) * scaleX;
  const canvasY = (event.clientY - rect.top) * scaleY;

  const cellSize = canvas.width / GRID_SIZE;
  const x = Math.floor(canvasX / cellSize);
  const y = Math.floor(canvasY / cellSize);

  if (x < 0 || x >= GRID_SIZE || y < 0 || y >= GRID_SIZE) {
    return null;
  }

  return { x, y };
}

function paintCell(x, y, color) {
  frame[y][x] = color;
  scheduleConsoleExport();
}

function paintUsingActiveMode(x, y) {
  paintCell(x, y, getCurrentPaintColor());
}

function drawInterpolatedLine(x0, y0, x1, y1) {
  const dx = x1 - x0;
  const dy = y1 - y0;
  const steps = Math.max(Math.abs(dx), Math.abs(dy));

  if (steps === 0) {
    paintUsingActiveMode(x0, y0);
    return;
  }

  for (let i = 0; i <= steps; i++) {
    const x = Math.round(x0 + (dx * i) / steps);
    const y = Math.round(y0 + (dy * i) / steps);
    paintUsingActiveMode(x, y);
  }
}

function drawFromEvent(event) {
  const cell = getCellFromEvent(event);
  if (!cell) return;

  if (lastPaintedCell) {
    drawInterpolatedLine(
      lastPaintedCell.x,
      lastPaintedCell.y,
      cell.x,
      cell.y
    );
  } else {
    paintUsingActiveMode(cell.x, cell.y);
  }

  lastPaintedCell = cell;
  scheduleRender();
}

function processPointerEvent(event) {
  const events = typeof event.getCoalescedEvents === "function"
    ? event.getCoalescedEvents()
    : [event];

  for (const pointerEvent of events) {
    drawFromEvent(pointerEvent);
  }
}

function clearFrame() {
  stopActiveVideoPlayback();
  stopActiveGifPlayback();
  frame = createEmptyFrame();
  render();
  scheduleConsoleExport();
}

function setPixelSafe(targetFrame, x, y, color) {
  if (x < 0 || x >= GRID_SIZE || y < 0 || y >= GRID_SIZE) return;
  targetFrame[y][x] = color;
}

function getPresetFrameByName(name) {
  switch (name) {
    case "alien":
      return createAlienPreset();
    case "parque":
      return createParquePreset();
    case "quack":
      return createQuackPreset();
    case "mushroom":
      return createMushroomPreset();
    case "utfpr":
      return createUtfprPreset();
    case "noctua":
      return createNoctuaPreset();
    case "robotnik":
      return createRobotnikPreset();
    case "fogo":
      return createFogoPreset();
    case "easteregg":
      return createEasterEggPreset();
    default:
      return createEmptyFrame();
  }
}

function getDominantColorFromFrame(sourceFrame) {
  const colorCount = new Map();

  for (const row of sourceFrame) {
    for (const color of row) {
      const normalized = color.toLowerCase();

      if (normalized === OFF_COLOR) continue;

      colorCount.set(normalized, (colorCount.get(normalized) || 0) + 1);
    }
  }

  if (colorCount.size === 0) {
    return "#00e5ff";
  }

  let dominantColor = "#00e5ff";
  let maxCount = -1;

  for (const [color, count] of colorCount.entries()) {
    if (count > maxCount) {
      dominantColor = color;
      maxCount = count;
    }
  }

  return dominantColor;
}

function applyPresetHoverColors() {
  presetButtons.forEach((button) => {
    const presetName = button.dataset.preset;
    const presetFrame = getPresetFrameByName(presetName);
    const dominantColor = getDominantColorFromFrame(presetFrame);

    button.style.setProperty("--preset-glow", dominantColor);
    button.style.setProperty("--preset-glow-soft", hexToRgba(dominantColor, 0.18));
    button.style.setProperty("--preset-glow-border", hexToRgba(dominantColor, 0.38));
    button.style.setProperty("--preset-glow-shadow", hexToRgba(dominantColor, 0.22));
  });
}

function applyPreset(name) {
  stopActiveMediaPlayback();

  const newFrame = getPresetFrameByName(name);

  frame = cloneFrame(newFrame);
  render();
  scheduleConsoleExport();
  closeModal(presetModal);
}

function drawSpriteCentered(targetFrame, spriteMap, colors) {
  const spriteHeight = spriteMap.length;
  const spriteWidth = spriteMap[0].length;
  const scale = Math.floor(GRID_SIZE / Math.max(spriteWidth, spriteHeight));
  const scaledWidth = spriteWidth * scale;
  const scaledHeight = spriteHeight * scale;

  const offsetX = Math.floor((GRID_SIZE - scaledWidth) / 2);
  const offsetY = Math.floor((GRID_SIZE - scaledHeight) / 2);

  for (let y = 0; y < spriteHeight; y++) {
    for (let x = 0; x < spriteWidth; x++) {
      const pixelValue = spriteMap[y][x];
      if (pixelValue > 0 && colors[pixelValue - 1]) {
        for (let sy = 0; sy < scale; sy++) {
          for (let sx = 0; sx < scale; sx++) {
            setPixelSafe(targetFrame, offsetX + x * scale + sx, offsetY + y * scale + sy, colors[pixelValue - 1]);
          }
        }
      }
    }
  }
}

function createAlienPreset() {
  const targetFrame = createEmptyFrame();
  const alien = [
    [0,0,1,0,0,0,0,0,1,0,0],
    [0,0,0,1,0,0,0,1,0,0,0],
    [0,0,1,1,1,1,1,1,1,0,0],
    [0,1,1,0,1,1,1,0,1,1,0],
    [1,1,1,1,1,1,1,1,1,1,1],
    [1,0,1,1,1,1,1,1,1,0,1],
    [1,0,1,0,0,0,0,0,1,0,1],
    [0,0,0,1,1,0,1,1,0,0,0]
  ];
  drawSpriteCentered(targetFrame, alien, ["#34c759"]);
  return targetFrame;
}

function createParquePreset() {
  const targetFrame = createEmptyFrame();
  const parque = [
    [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,2,2,2,2,2,2,2,2,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,2,2,2,2,2,2,2,2,2,2,2,2,2,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,2,2,2,2,2,0,2,0,0,0,2,2,2,2,2,2,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,2,2,2,0,0,0,2,2,0,0,2,0,2,2,2,2,2,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,2,2,2,2,0,0,0,0,2,0,2,0,0,0,2,2,2,2,2,0,0,0],
    [0,0,0,0,0,0,0,2,2,2,2,2,2,2,0,0,0,0,2,2,2,0,0,0,0,2,2,2,2,2,0,0],
    [0,0,0,0,0,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,1,1,1,1,0,0],
    [0,0,2,2,2,2,2,2,2,2,2,0,0,0,0,0,0,0,2,2,2,0,0,0,2,1,1,1,1,1,0,0],
    [0,2,2,2,2,2,2,2,2,0,0,0,0,0,0,0,0,2,2,0,2,0,0,0,0,0,0,1,1,1,0,0],
    [2,2,2,2,2,2,2,0,0,0,0,0,0,0,0,0,0,2,0,0,2,0,0,0,0,0,0,1,1,1,1,0],
    [0,2,2,2,2,0,0,0,2,2,2,2,0,0,0,0,2,0,0,0,2,2,0,0,0,0,0,1,1,1,1,0],
    [0,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,0,0,0,0,2,0,0,0,0,0,1,1,1,1,0],
    [0,0,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,0,0,0,0,2,2,0,0,0,0,0,1,1,1,0],
    [0,0,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,0,0,0,2,2,2,2,0,0,0,2,1,2,0],
    [0,0,0,2,2,0,0,0,0,0,0,0,0,0,2,2,2,2,2,2,2,2,0,2,2,2,2,2,3,3,3,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,2,2,2,2,0,0,0,0,0,0,2,3,3,3,3,3],
    [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,1,2,0,0,0,0,0,0,0,0,0,3,3,3,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,1,2,0,0,0,0,0,0,0,0,0,3,3,3,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,1,2,0,0,0,0,0,0,0,0,3,3,3,3,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,1,2,0,0,0,0,0,0,0,0,3,3,3,3,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,1,2,0,0,0,0,0,0,0,0,3,3,3,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,1,2,0,0,0,0,0,0,0,0,3,3,3,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,2,2,0,0,0,0,0,0,0,0,3,3,3,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0]
  ];

  drawSpriteCentered(targetFrame, parque, [
    "#2bd3e9", // 1 brilho ciano
    "#178ea3", // 2 corpo principal
    "#127586"  // 3 sombra azul
  ]);

  return targetFrame;
}

function createQuackPreset() {
  const targetFrame = createEmptyFrame();

  // 0 = fundo/apagado
  // 1 = contorno preto
  // 2 = amarelo principal
  // 3 = amarelo/sombra
  // 4 = laranja do bico
  // 5 = branco do olho
  const quack = [
    [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,3,3,3,3,3,1,1,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3,2,2,2,2,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3,3,3,2,2,2,2,2,2,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,1,1,1,3,3,3,3,2,2,2,2,2,2,2,2,1,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,1,3,3,3,3,3,2,2,2,2,2,2,2,2,2,1,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,1,3,3,3,2,2,2,2,2,2,2,2,2,2,2,1,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,1,3,3,3,2,2,2,2,2,2,2,2,2,2,2,1,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,1,3,3,3,2,2,2,2,2,2,1,5,2,2,2,1,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,1,3,3,3,2,2,2,2,2,2,1,1,2,2,1,4,1,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,1,3,3,3,2,2,2,2,2,2,2,2,2,2,1,4,4,1,1,1,0],
    [0,0,0,0,0,0,0,0,0,0,0,1,3,3,3,2,2,2,2,2,2,2,2,2,2,1,4,4,4,4,1,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,1,0,3,3,2,2,2,2,2,2,2,2,1,4,4,4,4,4,1,0],
    [0,0,1,0,0,0,0,0,0,0,0,0,1,0,3,3,3,2,2,2,2,2,2,2,1,4,4,4,4,1,0,0],
    [0,0,1,1,0,0,0,0,0,0,0,0,0,0,1,3,3,3,2,2,2,2,2,2,1,1,1,1,1,0,0,0],
    [0,0,1,2,1,0,0,0,0,0,1,0,0,0,3,1,3,3,3,3,3,3,3,2,1,0,0,0,0,0,0,0],
    [0,1,1,2,2,1,0,0,1,1,3,3,3,3,3,3,3,2,2,2,2,2,2,3,3,1,0,0,0,0,0,0],
    [0,1,2,1,2,1,0,0,0,0,3,3,3,3,3,3,3,2,2,2,2,2,2,3,3,0,0,0,0,0,0,0],
    [0,1,2,2,2,2,1,1,3,3,3,2,2,2,2,2,2,2,2,2,2,2,2,2,3,3,1,0,0,0,0,0],
    [0,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,3,3,1,0,0,0,0],
    [0,1,2,2,2,2,2,2,2,2,2,2,1,1,1,1,2,2,2,2,2,2,1,2,2,3,3,1,0,0,0,0],
    [0,1,2,2,2,2,2,2,1,1,1,1,1,0,2,2,2,2,2,2,2,2,1,2,2,3,3,3,1,0,0,0],
    [0,0,1,2,2,2,2,2,2,1,2,2,2,2,2,2,2,2,2,2,2,2,1,2,2,3,3,3,1,0,0,0],
    [0,0,1,2,2,2,2,2,2,1,1,1,1,1,1,2,2,2,2,2,2,2,1,2,2,3,3,3,1,0,0,0],
    [0,0,0,1,2,2,2,2,2,2,1,2,2,2,2,2,2,2,2,2,2,2,1,2,3,3,3,3,1,0,0,0],
    [0,0,0,1,2,2,2,2,2,2,2,1,2,2,2,2,2,2,2,2,2,1,2,2,3,3,3,1,0,0,0,0],
    [0,0,0,0,1,3,2,2,2,2,2,2,1,0,2,2,2,2,2,1,1,2,2,3,3,3,3,1,0,0,0,0],
    [0,0,0,0,0,1,3,3,2,2,2,2,2,0,1,1,1,1,1,2,2,2,3,3,3,3,1,0,0,0,0,0],
    [0,0,0,0,0,0,1,3,3,3,2,2,2,2,2,2,2,2,2,2,3,3,3,3,1,1,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,1,1,3,3,3,3,3,3,3,3,3,3,3,3,3,1,1,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0]
  ];

  drawSpriteCentered(targetFrame, quack, [
    "#181818", // 1 contorno preto
    "#f6ec15", // 2 amarelo principal
    "#f4ca19", // 3 sombra amarela
    "#fa5a22", // 4 laranja do bico
    "#ffffff"  // 5 branco do olho
  ]);

  return targetFrame;
}

function createMushroomPreset() {
  const targetFrame = createEmptyFrame();
  const mushroom = [
    [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,3,3,3,3,3,3,3,3,3,3,3,3,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,3,3,3,3,3,3,3,3,3,3,3,3,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,3,3,1,1,1,1,1,1,1,1,1,1,1,1,3,3,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,3,3,1,1,1,1,1,1,1,1,1,1,1,1,3,3,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,3,3,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,3,3,0,0,0,0,0,0],
    [0,0,0,0,0,0,3,3,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,3,3,0,0,0,0,0,0],
    [0,0,0,0,3,3,1,1,1,1,2,2,2,2,1,1,1,1,2,2,2,2,1,1,1,1,3,3,0,0,0,0],
    [0,0,0,0,3,3,1,1,1,1,2,2,2,2,1,1,1,1,2,2,2,2,1,1,1,1,3,3,0,0,0,0],
    [0,0,3,3,1,1,1,1,2,2,2,2,2,2,2,2,1,1,2,2,2,2,2,2,2,2,1,1,3,3,0,0],
    [0,0,3,3,1,1,1,1,2,2,2,2,2,2,2,2,1,1,2,2,2,2,2,2,2,2,1,1,3,3,0,0],
    [0,0,3,3,1,1,1,1,2,2,2,2,2,2,2,2,1,1,2,2,2,2,2,2,2,2,1,1,3,3,0,0],
    [0,0,3,3,1,1,1,1,2,2,2,2,2,2,2,2,1,1,2,2,2,2,2,2,2,2,1,1,3,3,0,0],
    [0,3,3,2,2,2,1,1,1,1,2,2,2,2,1,1,1,1,1,1,2,2,2,2,1,1,1,1,2,3,3,0],
    [0,3,3,2,2,2,1,1,1,1,2,2,2,2,1,1,1,1,1,1,2,2,2,2,1,1,1,1,2,3,3,0],
    [0,3,3,2,2,2,2,2,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,2,2,2,3,3,0],
    [0,3,3,2,2,2,2,2,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,2,2,2,3,3,0],
    [0,3,3,2,2,2,2,2,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,2,2,2,3,3,0],
    [0,3,3,2,2,2,2,2,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,2,2,2,3,3,0],
    [0,0,3,3,2,2,2,2,1,1,1,1,1,1,3,3,3,3,1,1,1,1,1,1,2,2,2,2,3,3,0,0],
    [0,0,3,3,2,2,2,2,1,1,1,1,1,1,3,3,3,3,1,1,1,1,1,1,2,2,2,2,3,3,0,0],
    [0,0,0,0,3,3,3,3,3,3,3,3,3,3,4,4,4,4,3,3,3,3,3,3,3,3,3,3,0,0,0,0],
    [0,0,0,0,3,3,3,3,3,3,3,3,3,3,4,4,4,4,3,3,3,3,3,3,3,3,3,3,0,0,0,0],
    [0,0,0,0,0,0,3,3,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,3,3,0,0,0,0,0,0],
    [0,0,0,0,0,0,3,3,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,3,3,0,0,0,0,0,0],
    [0,0,0,0,0,0,3,3,4,4,4,4,3,3,4,4,4,4,3,3,4,4,4,4,3,3,0,0,0,0,0,0],
    [0,0,0,0,0,0,3,3,4,4,4,4,3,3,4,4,4,4,3,3,4,4,4,4,3,3,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,3,3,4,4,4,4,4,4,4,4,4,4,4,4,3,3,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,3,3,4,4,4,4,4,4,4,4,4,4,4,4,3,3,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,3,3,3,3,3,3,3,3,3,3,3,3,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,3,3,3,3,3,3,3,3,3,3,3,3,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0]
  ];
  drawSpriteCentered(targetFrame, mushroom, ["#ff2d55", "#ffffff", "#1d1d1d", "#ffcc99"]);
  return targetFrame;
}

function createFogoPreset() {
  const targetFrame = createEmptyFrame();
  const fogo = [
    [0,0,0,0,0,0,0,0,0,0,0,0,0,0,7,1,1,1,7,0,0,0,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,0,7,1,8,2,1,7,0,0,0,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,7,1,9,2,2,2,1,7,0,0,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,1,9,2,2,2,2,2,9,0,0,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,7,9,2,2,2,3,2,2,9,7,0,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,7,1,9,2,2,3,3,3,3,3,2,9,7,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,7,1,9,2,3,3,3,3,3,3,3,3,8,6,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,1,9,2,3,3,3,4,4,4,4,3,3,2,9,7,9,7,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,7,9,2,3,3,3,4,4,4,4,4,4,3,3,2,1,2,9,7,0,0,0,0,0,0],
    [0,0,0,0,7,1,7,1,8,2,3,3,3,4,4,4,4,4,4,3,3,3,9,2,8,6,0,0,0,0,0,0],
    [0,0,0,0,1,9,1,1,8,3,3,3,4,4,4,4,4,4,4,4,3,3,3,2,2,9,7,0,0,0,0,0],
    [0,0,0,7,9,2,9,1,9,3,3,4,4,4,4,5,5,4,4,4,4,3,3,3,2,8,7,7,0,0,0,0],
    [0,0,0,1,8,3,2,8,2,3,4,4,4,4,5,5,5,5,4,4,4,4,3,3,3,2,1,7,0,0,0,0],
    [0,0,0,7,9,2,3,2,3,3,4,4,4,5,5,5,5,5,5,4,4,4,4,3,3,2,1,7,0,0,0,0],
    [0,0,0,0,7,8,8,3,3,4,4,4,5,5,5,5,5,5,5,5,4,4,4,4,3,2,1,7,0,0,0,0],
    [0,0,0,0,7,7,1,2,3,4,4,4,5,5,5,5,5,5,5,5,4,4,4,4,3,2,1,7,0,0,0,0],
    [0,0,0,0,7,8,8,2,4,4,4,4,5,5,5,5,5,5,5,5,4,4,4,4,4,2,2,1,0,0,0,0],
    [0,0,0,0,9,2,2,3,4,4,5,6,5,5,5,5,5,5,5,5,6,5,4,4,4,3,2,1,0,0,0,0],
    [0,0,0,0,9,2,2,3,4,6,7,7,7,5,5,5,5,5,5,7,7,7,6,4,4,3,8,7,0,0,0,0],
    [0,0,0,0,9,2,3,3,6,7,7,1,6,5,5,5,5,5,5,7,1,7,7,6,4,2,2,9,0,0,0,0],
    [0,0,0,0,9,2,3,3,6,7,7,1,6,5,5,5,5,5,5,7,1,7,7,7,4,3,2,1,0,0,0,0],
    [0,0,0,0,6,9,3,3,5,7,7,7,7,5,5,5,5,5,5,7,7,7,7,5,4,2,2,1,0,0,0,0],
    [0,7,7,7,7,1,2,3,4,5,5,6,4,2,2,2,2,2,3,5,6,5,5,4,3,2,8,1,7,7,7,0],
    [7,7,1,1,1,1,2,3,3,4,4,5,3,2,2,2,2,2,2,5,5,4,4,3,3,8,1,1,1,1,7,7],
    [7,1,8,8,8,8,1,2,2,4,4,4,5,2,2,2,2,2,5,5,4,4,3,2,8,8,8,8,1,8,1,7],
    [9,8,8,8,8,9,8,1,1,9,2,4,5,5,5,5,5,5,5,4,3,9,9,1,8,8,9,8,8,9,10,1],
    [9,9,9,8,8,9,9,9,8,1,1,8,8,8,9,8,8,8,9,8,8,1,1,8,9,9,9,8,8,9,10,1],
    [9,9,9,9,9,9,9,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,9,9,9,8,8,9,9,10,1],
    [9,8,8,8,8,8,8,9,9,9,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,9,8,1,8,10,1],
    [9,9,9,9,8,9,8,8,8,8,9,9,8,8,9,8,8,8,9,8,8,8,9,8,8,8,9,8,1,9,10,1],
    [9,8,8,8,9,9,9,9,9,9,9,8,8,8,8,8,8,8,8,9,9,9,9,9,8,8,8,8,1,8,9,1],
    [7,8,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,7]
  ];

  drawSpriteCentered(targetFrame, fogo, [
    "#362e2b", // 1 contorno escuro
    "#be433a", // 2 vermelho escuro
    "#e76939", // 3 vermelho/laranja
    "#f29b37", // 4 laranja
    "#f1cb75", // 5 amarelo
    "#e7d7b3", // 6 amarelo claro
    "#fefefd", // 7 branco
    "#5f3d29", // 8 madeira escura
    "#72513d", // 9 madeira média
    "#91867d"  // 10 detalhe claro da madeira
  ]);

  return targetFrame;
}

function createUtfprPreset() {
  const targetFrame = createEmptyFrame("#FFFFFF");

  const utfpr = [
  [2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2],
  [2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2],
  [2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2],
  [2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2],
  [2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2],
  [2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2],
  [1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1],
  [1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1],
  [1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1],
  [1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1],
  [1,1,2,2,1,1,2,2,2,2,2,2,2,2,2,2,2,2,1,1,1,1,1,1,1,1,1,1,1,1,1,1],
  [1,1,2,2,1,1,2,2,2,2,2,2,2,2,2,2,2,2,1,1,1,1,1,1,1,1,1,1,1,1,1,1],
  [1,1,2,2,1,1,2,2,1,2,2,3,3,3,3,3,3,3,1,1,1,1,1,1,1,1,1,1,1,1,1,1],
  [1,1,2,2,1,1,2,2,1,2,2,3,3,3,3,3,3,3,1,1,1,1,1,1,1,1,1,1,1,1,1,1],
  [1,1,2,2,1,1,2,2,1,2,2,3,3,2,2,2,2,2,1,2,2,2,2,1,1,2,2,2,2,1,1,1],
  [1,1,2,2,1,1,2,2,1,2,2,3,3,2,2,2,2,2,1,2,2,2,2,2,1,2,2,2,2,2,1,1],
  [1,1,2,2,1,1,2,2,1,2,2,3,3,2,2,1,1,1,1,2,2,1,2,2,1,2,2,1,2,2,1,1],
  [1,1,2,2,1,1,2,2,1,2,2,3,3,2,2,1,1,1,1,2,2,2,2,2,1,2,2,2,2,2,1,1],
  [1,1,2,2,1,1,2,2,1,2,2,3,3,2,2,1,1,1,1,2,2,2,2,1,1,2,2,2,2,1,1,1],
  [1,1,1,2,2,2,2,1,1,2,2,3,3,2,2,1,1,1,1,2,2,1,1,1,1,2,2,1,2,2,1,1],
  [1,1,1,2,2,2,2,1,1,2,2,3,3,2,2,1,1,1,1,2,2,1,1,1,1,2,2,1,2,2,1,1],
  [1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1],
  [1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1],
  [1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1],
  [1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1],
  [2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2],
  [2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2],
  [2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2],
  [2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2],
  [2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2],
  [2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2],
  [2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2]
];

  drawSpriteCentered(targetFrame, utfpr, ["#FFFFFF", "#000000", "#FFCC29"]);
  
  return targetFrame;
}

function createNoctuaPreset() {
  const targetFrame = createEmptyFrame();

  // 0 = transparente
  // 1 = branco (#FFFFFF)
  const owl = [
    [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,1,1,0,0,0,0,1,1,0,0,0,1,0,0,0,0,1,1,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,1,1,1,1,0,1,1,0,0,1,1,0,1,1,1,1,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,1,1,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,1,1,0,0,1,1,0,0,1,1,0,0,1,1,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,1,0,0,0,0,1,1,1,1,0,0,0,0,1,1,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,1,1,0,1,1,1,0,1,1,0,0,1,1,0,1,1,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,1,1,0,1,1,1,0,0,0,0,1,1,1,0,1,1,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,1,1,0,0,1,0,0,1,1,0,0,1,1,0,1,1,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,1,1,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,1,0,1,1,1,1,0,1,1,0,1,1,1,1,0,1,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,1,0,1,0,0,0,0,1,0,0,1,0,0,0,0,1,0,1,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,1,1,0,1,1,1,1,0,1,0,0,1,0,1,1,1,1,0,1,1,0,0,0,0,0,0],
    [0,0,0,0,0,0,1,1,0,0,0,0,1,0,1,0,0,1,0,1,0,0,0,0,0,1,0,0,0,0,0,0],
    [0,0,0,0,0,0,1,0,1,0,1,1,1,0,1,0,0,1,0,1,1,1,0,1,0,1,0,0,0,0,0,0],
    [0,0,0,0,0,0,1,0,0,0,0,1,1,0,1,0,0,1,0,0,1,0,0,0,0,1,0,0,0,0,0,0],
    [0,0,0,0,0,0,1,0,1,1,1,1,0,0,1,1,1,1,0,0,1,1,1,1,0,1,0,0,0,0,0,0],
    [0,0,0,0,0,0,1,1,0,0,0,0,0,1,1,1,1,1,1,0,0,1,1,1,0,1,0,0,0,0,0,0],
    [0,0,0,0,0,0,1,1,0,0,1,1,0,0,1,1,1,1,1,1,0,0,0,0,1,1,0,0,0,0,0,0],
    [0,0,0,0,0,0,1,1,0,1,0,0,0,0,1,1,1,1,0,0,0,0,1,0,1,1,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,1,0,0,1,1,0,0,1,1,1,1,0,0,1,1,0,0,1,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,1,1,0,1,1,0,0,1,1,1,1,0,0,1,1,0,1,1,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,1,1,0,0,0,0,1,1,1,1,0,0,0,0,0,1,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,1,0,0,1,1,1,1,1,1,1,1,0,0,1,1,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,1,1,0,1,1,1,0,0,0,1,1,0,1,1,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,1,1,1,0,0,0,0,0,0,1,1,1,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,1,1,1,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0]
  ];

  drawSpriteCentered(targetFrame, owl, ["#FFFFFF"]);

  return targetFrame;
}


function createRobotnikPreset() {
  const targetFrame = createEmptyFrame();

  const robotnik = [
    [0,0,0,0,0,0,0,0,1,1,1,0,0,0,0,0,0,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,1,6,6,6,6,1,0,0,0,0,1,6,6,6,6,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,1,6,6,6,1,1,6,0,0,0,1,6,6,6,1,6,6,1,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,1,6,6,1,8,1,6,1,1,1,1,6,6,1,8,1,6,1,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,1,6,6,6,1,6,6,6,6,6,1,6,6,1,1,8,1,1,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,1,6,6,6,6,6,1,1,1,6,1,6,6,6,1,1,6,1,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,1,6,6,6,0,3,2,2,1,6,1,6,6,6,6,6,6,1,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,1,1,1,2,2,2,2,2,1,6,6,6,6,6,1,1,6,1,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,3,2,2,2,2,2,2,2,2,1,1,1,1,1,2,3,1,6,1,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,1,2,2,2,2,2,2,2,1,1,2,2,2,2,2,3,1,6,1,0,0,0,0,0],
    [0,0,0,0,0,0,0,1,2,1,2,2,2,2,2,1,1,1,1,2,2,2,2,2,1,1,0,0,0,0,0,0],
    [0,0,0,0,0,0,1,3,1,1,1,2,2,2,1,1,1,1,1,2,2,2,2,2,1,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,1,3,1,1,8,1,2,1,1,8,1,1,1,1,2,2,2,1,1,0,0,0,0,0,0,0],
    [0,0,1,0,1,0,1,3,2,1,1,1,4,1,1,1,1,2,1,1,2,2,2,2,3,1,0,0,0,0,0,0],
    [0,0,1,1,1,1,1,1,1,4,4,4,4,1,1,1,2,1,4,4,1,2,1,2,2,3,1,0,0,0,0,0],
    [0,0,0,1,1,1,4,4,4,4,1,1,4,4,0,4,4,4,4,1,1,1,1,1,2,3,1,0,0,0,0,0],
    [0,1,1,1,4,0,1,1,4,4,4,4,4,4,4,4,1,1,1,1,1,1,1,2,2,0,0,0,0,0,0,0],
    [1,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,1,4,1,1,2,2,1,0,0,0,0,0,0,0],
    [1,4,4,4,1,1,1,1,4,4,4,4,4,4,1,1,1,1,4,4,1,2,2,1,0,0,0,0,0,0,0,0],
    [0,1,1,1,4,4,4,1,1,1,4,4,1,1,4,4,4,4,1,1,2,2,3,0,0,0,0,0,0,0,0,0],
    [0,0,0,1,4,4,4,4,4,4,1,1,4,4,1,1,1,1,3,2,2,3,1,1,1,0,0,0,0,0,0,0],
    [0,0,0,0,1,1,1,1,1,1,1,1,1,1,3,3,2,2,2,2,2,3,1,6,1,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,1,5,1,1,3,2,2,2,1,1,1,1,1,5,1,1,1,1,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,1,5,5,1,1,1,1,1,5,5,5,5,5,1,1,5,5,5,1,0,0,0],
    [0,0,0,0,0,0,0,0,1,1,5,1,1,5,5,5,5,5,5,6,1,1,1,5,5,5,5,5,5,1,0,0],
    [0,0,0,0,0,0,0,1,6,6,6,6,6,1,1,1,1,1,1,1,6,5,5,5,6,5,5,5,5,5,1,0],
    [0,0,0,0,0,0,1,6,5,5,5,5,5,1,1,1,6,5,5,5,5,5,5,6,5,5,5,5,5,5,1,0],
    [0,0,0,0,0,0,5,5,5,5,5,5,5,1,6,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,6,0],
    [0,0,0,0,0,1,5,5,5,5,5,5,5,6,5,5,5,5,5,5,5,5,1,5,5,5,5,5,5,5,5,1],
    [0,0,0,0,0,1,5,5,5,5,5,5,1,5,5,5,5,5,5,5,5,5,1,5,5,5,5,5,5,5,5,1],
    [0,0,0,0,0,1,5,5,5,5,5,5,1,5,5,5,5,5,5,5,5,5,1,5,5,5,5,5,5,5,5,1],
    [0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1]
  ];

  drawSpriteCentered(targetFrame, robotnik, [
    "#181818", // 0
    "#ffffff", // 1
    "#fcfcfc", // 2
    "#fffb00", // 3
    "#ff0000", // 4
    "#b60006", // 5
    "#96230e", // 6
    "#00cbfd", // 7
    "#24e7ff"  // 8
  ]);

  return targetFrame;
}

function createEasterEggPreset() {
  const targetFrame = createEmptyFrame();

  // 0 = transparente / fundo preto do quadro
  // 1 = branco
  // 2 = vermelho vivo
  const spfc = [
    [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],
    [0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0],
    [0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0],
    [0,1,0,0,0,1,1,1,1,0,0,1,1,1,1,0,0,1,1,1,1,0,0,1,1,1,1,0,0,0,1,0],
    [0,1,0,0,0,1,0,0,0,0,0,1,0,0,1,0,0,1,0,0,0,0,0,1,0,0,0,0,0,0,1,0],
    [0,1,0,0,0,1,1,1,1,0,0,1,1,1,1,0,0,1,1,1,0,0,0,1,0,0,0,0,0,0,1,0],
    [0,1,0,0,0,0,0,0,1,0,0,1,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,0,1,0],
    [0,1,0,0,0,1,1,1,1,0,0,1,0,0,0,0,0,1,0,0,0,0,0,1,1,1,1,0,0,0,1,0],
    [0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0],
    [0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0],
    [0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0],
    [0,1,1,2,2,2,2,2,2,2,2,2,2,2,2,1,1,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0],
    [0,1,1,1,2,2,2,2,2,2,2,2,2,2,2,1,1,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0],
    [0,0,1,1,1,2,2,2,2,2,2,2,2,2,2,1,1,0,0,0,0,0,0,0,0,0,0,1,1,1,0,0],
    [0,0,0,1,1,1,2,2,2,2,2,2,2,2,2,1,1,0,0,0,0,0,0,0,0,0,1,1,1,0,0,0],
    [0,0,0,0,1,1,1,2,2,2,2,2,2,2,2,1,1,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0],
    [0,0,0,0,0,1,1,1,2,2,2,2,2,2,2,1,1,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0],
    [0,0,0,0,0,1,1,1,1,2,2,2,2,2,2,1,1,0,0,0,0,0,0,0,1,1,1,0,0,0,0,0],
    [0,0,0,0,0,0,1,1,1,1,2,2,2,2,2,1,1,0,0,0,0,0,0,1,1,1,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,1,1,1,2,2,2,2,2,1,1,0,0,0,0,0,1,1,1,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,1,1,1,2,2,2,2,1,1,0,0,0,0,1,1,1,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,1,1,1,2,2,2,1,1,0,0,0,1,1,1,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,1,1,1,2,2,1,1,0,0,1,1,1,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,1,1,1,2,2,1,1,0,0,1,1,1,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,1,1,1,2,1,1,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],
    [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0]
  ];

  drawSpriteCentered(targetFrame, spfc, [
    "#ffffff", // 1 branco
    "#ff0000", // 2 vermelho escuro
  ]);

  return targetFrame;
}

function stopDrawing(event) {
  if (event && typeof canvas.hasPointerCapture === "function") {
    if (canvas.hasPointerCapture(event.pointerId)) {
      try {
        canvas.releasePointerCapture(event.pointerId);
      } catch (_) {}
    }
  }

  isDrawing = false;
  lastPaintedCell = null;
}

canvas.addEventListener("pointerdown", (event) => {
  event.preventDefault();
  attemptFullscreenFromGesture();

  stopActiveMediaPlayback();

  isDrawing = true;
  lastPaintedCell = null;

  if (typeof canvas.setPointerCapture === "function") {
    try {
      canvas.setPointerCapture(event.pointerId);
    } catch (_) {}
  }

  drawFromEvent(event);
  render();
});

canvas.addEventListener("pointermove", (event) => {
  if (!isDrawing) return;
  event.preventDefault();
  processPointerEvent(event);
});

canvas.addEventListener("pointerup", (event) => {
  event.preventDefault();
  stopDrawing(event);
});

canvas.addEventListener("pointercancel", (event) => {
  stopDrawing(event);
});

canvas.addEventListener("click", (event) => {
  event.preventDefault();
  stopActiveMediaPlayback();
  drawFromEvent(event);
  render();
});

canvas.addEventListener("lostpointercapture", () => {
  isDrawing = false;
  lastPaintedCell = null;
});

clearBtn.addEventListener("click", clearFrame);
presetBtn.addEventListener("click", () => openModal(presetModal));

mediaBtn.addEventListener("click", () => {
  shouldRestoreFullscreenAfterMediaPicker = Boolean(getFullscreenElement());
  clearFullscreenRestoreAfterMediaPickerTimer();
  mediaInput.click();
});

if (fullscreenBtn) {
  fullscreenBtn.addEventListener("click", (event) => {
    event.preventDefault();
    toggleFullscreenFromButton();
  });
}

if (mediaPlaybackBtn) {
  mediaPlaybackBtn.addEventListener("click", (event) => {
    event.preventDefault();
    toggleActiveMediaPlayback();
  });
}

mediaInput.addEventListener("change", async (event) => {
  await restoreFullscreenAfterMediaPicker();

  const file = event.target.files?.[0];

  if (!file) {
    mediaInput.value = "";
    scheduleFullscreenRestoreAfterMediaPicker();
    return;
  }

  const isGif =
    file.type === "image/gif" ||
    /\.gif$/i.test(file.name);

  if (isGif) {
    await loadGifFileToFrame(file);
  } else if (file.type.startsWith("image/")) {
    await loadImageFileToFrame(file);
  } else if (file.type.startsWith("video/")) {
    loadVideoFileToFrame(file);
  } else {
    alert("Arquivo inválido.");
  }

  mediaInput.value = "";
  scheduleFullscreenRestoreAfterMediaPicker();
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
  if (event.key === "Escape") {
    closeModal(presetModal);
  }
});

window.addEventListener("focus", () => {
  scheduleFullscreenRestoreAfterMediaPicker();
});

document.addEventListener("visibilitychange", () => {
  if (document.visibilityState === "visible") {
    scheduleFullscreenRestoreAfterMediaPicker();
  }
});

document.addEventListener("fullscreenchange", updateFullscreenButton);
document.addEventListener("webkitfullscreenchange", updateFullscreenButton);

modeButtons.forEach((button) => {
  button.addEventListener("click", () => {
    setPaintMode(button.dataset.mode);
  });
});

swatchButtons.forEach((button) => {
  button.addEventListener("click", () => {
    setSelectedColor(button.dataset.color);
  });
});

colorPicker.addEventListener("input", (event) => {
  setSelectedColor(event.target.value);
});

presetButtons.forEach((button) => {
  button.addEventListener("click", () => {
    const presetName = button.dataset.preset;
    applyPreset(presetName);
  });
});

let renderScheduled = false;

function scheduleRender() {
  if (renderScheduled) return;

  renderScheduled = true;

  requestAnimationFrame(() => {
    renderScheduled = false;
    render();
  });
}


const CONSOLE_EXPORT_INTERVAL_MS = 100;
let consoleExportScheduled = false;
let consoleExportTimer = null;
let lastConsoleExportAt = 0;

function writeMatrixToConsoleNow() {
  window.benaLedLiveMatrix = frameToRgbMatrix(frame);
  console.clear();
  console.log(frameToRgbMatrixString(frame));
  lastConsoleExportAt = performance.now();
  consoleExportScheduled = false;
  consoleExportTimer = null;
}

function scheduleConsoleExport() {
  window.benaLedLiveMatrix = frameToRgbMatrix(frame);

  if (consoleExportScheduled) return;

  const now = performance.now();
  const wait = Math.max(0, CONSOLE_EXPORT_INTERVAL_MS - (now - lastConsoleExportAt));
  consoleExportScheduled = true;

  if (wait === 0) {
    requestAnimationFrame(writeMatrixToConsoleNow);
    return;
  }

  consoleExportTimer = setTimeout(() => {
    requestAnimationFrame(writeMatrixToConsoleNow);
  }, wait);
}

window.forceConsoleMatrixUpdate = () => {
  if (consoleExportTimer) {
    clearTimeout(consoleExportTimer);
    consoleExportTimer = null;
  }
  writeMatrixToConsoleNow();
};

setSelectedColor(selectedColor);
applyPresetHoverColors();
render();
scheduleConsoleExport();
updateFullscreenButton();
updateMediaPlaybackButton();
