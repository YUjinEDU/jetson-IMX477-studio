// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 YUjinEDU

(function () {
  'use strict';

  // --- DOM Elements ---
  const connPill = document.getElementById('conn-pill');
  const streamImg = document.getElementById('stream-img');
  const viewportWrap = document.getElementById('viewport-wrap');
  const viewportCard = document.getElementById('viewport-card');
  const hudRes = document.getElementById('hud-res');
  const hudSensor = document.getElementById('hud-sensor');
  const hudFps = document.getElementById('hud-fps');
  const hudAi = document.getElementById('hud-ai');
  const hudZoom = document.getElementById('hud-zoom');
  const badgeAi = document.getElementById('badge-ai');
  const badgeRec = document.getElementById('badge-rec');
  const metricViewFps = document.getElementById('metric-view-fps');
  const fpsGraphTitle = document.getElementById('fps-graph-title');
  const btnPresetIndoor = document.getElementById('btn-preset-indoor');
  const btnPresetPitch = document.getElementById('btn-preset-pitch');
  const toast = document.getElementById('toast');
  const shutterFlash = document.getElementById('shutter-flash');
  const minimapBox = document.getElementById('minimap-box');
  const minimapCropRect = document.getElementById('minimap-crop-rect');

  // KPI Elements
  const metricSensorFps = document.getElementById('metric-sensor-fps');
  const metricGaps = document.getElementById('metric-gaps');
  const metricTotalFrames = document.getElementById('metric-total-frames');
  const metricUptime = document.getElementById('metric-uptime');

  // Controls Elements
  const rangeExp = document.getElementById('range-exp');
  const valExpUs = document.getElementById('val-exp-us');
  const valExpMs = document.getElementById('val-exp-ms');
  const rangeGain = document.getElementById('range-gain');
  const valGain = document.getElementById('val-gain');
  const rangeEv = document.getElementById('range-ev');
  const valEv = document.getElementById('val-ev');
  const selectAntibanding = document.getElementById('select-antibanding');
  const selectAwb = document.getElementById('select-awb');
  const chkAwbLock = document.getElementById('chk-awb-lock');
  const valActualCct = document.getElementById('val-actual-cct');
  const rangeIspGain = document.getElementById('range-isp-gain');
  const valIspGain = document.getElementById('val-isp-gain');
  const selectDenoise = document.getElementById('select-denoise');
  const selectEdge = document.getElementById('select-edge');

  // AE Elements
  const manualExpGroup = document.getElementById('manual-exp-group');
  const manualGainGroup = document.getElementById('manual-gain-group');
  const aeOptions = document.getElementById('ae-options');
  const rangeAeMaxExp = document.getElementById('range-ae-max-exp');
  const valAeMaxExp = document.getElementById('val-ae-max-exp');
  const rangeAeMaxGain = document.getElementById('range-ae-max-gain');
  const valAeMaxGain = document.getElementById('val-ae-max-gain');
  const chkAeLock = document.getElementById('chk-ae-lock');

  // Instruments
  const focusScoreVal = document.getElementById('focus-score-val');
  const focusMeterFill = document.getElementById('focus-meter-fill');
  const focusMeterPeak = document.getElementById('focus-meter-peak');
  const histCanvas = document.getElementById('histogram-canvas');
  const histCtx = histCanvas ? histCanvas.getContext('2d') : null;
  const sparklineCanvas = document.getElementById('fps-sparkline-canvas');
  const sparklineCtx = sparklineCanvas ? sparklineCanvas.getContext('2d') : null;
  const sparklineVal = document.getElementById('sparkline-val');

  // Blur Calc
  const inputBallSpeed = document.getElementById('input-ball-speed');
  const inputFovWidth = document.getElementById('input-fov-width');
  const calcSpeedMs = document.getElementById('calc-speed-ms');
  const calcBlurCm = document.getElementById('calc-blur-cm');
  const calcBlurCmBadge = document.getElementById('calc-blur-cm-badge');
  const calcBlurPx = document.getElementById('calc-blur-px');
  const calcBlurRec = document.getElementById('calc-blur-rec');
  const blurStreakLine = document.getElementById('blur-streak-line');

  // Toolbar & Assists
  const zoomBtns = document.querySelectorAll('.btn-tool[data-zoom]');
  const chkPeaking = document.getElementById('chk-peaking');
  const selectPeakingColor = document.getElementById('select-peaking-color');
  const chkZebra = document.getElementById('chk-zebra');
  const selectGrid = document.getElementById('select-grid');
  const selectViewFps = document.getElementById('select-view-fps');
  const btnFullscreen = document.getElementById('btn-fullscreen');

  const chkFalseColor = document.getElementById('chk-false-color');
  const metricCpuTemp = document.getElementById('metric-cpu-temp');
  const metricRamUsed = document.getElementById('metric-ram-used');

  // Instant Replay Elements
  const replayModal = document.getElementById('replay-modal');
  const replayFrame = document.getElementById('replay-frame');
  const replayModalTitle = document.getElementById('replay-modal-title');
  const btnCloseReplay = document.getElementById('btn-close-replay');
  const btnReplayPlay = document.getElementById('btn-replay-play');
  const replayScrubber = document.getElementById('replay-scrubber');
  const replayFrameText = document.getElementById('replay-frame-text');
  const replayTimeText = document.getElementById('replay-time-text');
  const btnStepPrev = document.getElementById('btn-step-prev');
  const btnStepNext = document.getElementById('btn-step-next');
  const replayCsvBtn = document.getElementById('replay-csv-btn');

  let currentReplayClip = null;
  let currentReplayFrame = 0;
  let replayPlaying = false;
  let replayTimer = null;
  let replaySpeed = 0.25;

  // Presets & Media
  const presetSelect = document.getElementById('preset-select');
  const btnLoadPreset = document.getElementById('btn-load-preset');
  const btnSavePreset = document.getElementById('btn-save-preset');
  const btnSnapshot = document.getElementById('btn-snapshot');
  const galleryContainer = document.getElementById('gallery-container');
  const btnRefreshGallery = document.getElementById('btn-refresh-gallery');

  // Video Recording Elements
  const btnRecord = document.getElementById('btn-record');
  const recLabel = document.getElementById('rec-label');
  const recTimer = document.getElementById('rec-timer');
  const osdRecBadge = document.getElementById('osd-rec-badge');
  let isRecording = false;
  let recordTimerInterval = null;
  let recordSeconds = 0;

  // Pitch Auto-Clip Elements
  const chkPitchAutoClip = document.getElementById('chk-pitch-autoclip');
  const valPitchMotion = document.getElementById('val-pitch-motion');
  const pitchMotionFill = document.getElementById('pitch-motion-fill');
  const selectPitchPreroll = document.getElementById('select-pitch-preroll');
  const selectPitchPostroll = document.getElementById('select-pitch-postroll');
  const valPitchClipsCount = document.getElementById('val-pitch-clips-count');
  const pitchClipStatusBadge = document.getElementById('pitch-clip-status-badge');
  const valPitchBuffered = document.getElementById('val-pitch-buffered');
  const pitchBufferReadiness = document.getElementById('pitch-buffer-readiness');
  const pitchBufferFill = document.getElementById('pitch-buffer-fill');
  const pitchNativeSpec = document.getElementById('pitch-native-spec');
  const pitchErrorAlert = document.getElementById('pitch-error-alert');
  // Calibration Elements
  const btnCalibCapture = document.getElementById('btn-calib-capture');
  const btnCalibCompute = document.getElementById('btn-calib-compute');
  const btnCalibClear = document.getElementById('btn-calib-clear');
  const calibFrameCount = document.getElementById('calib-frame-count');
  const calibStatusText = document.getElementById('calib-status-text');
  const calibRmsText = document.getElementById('calib-rms-text');
  const calibMatrixGrid = document.getElementById('calib-matrix-grid');

  // Sensor Mode Buttons
  const sensorModeBtns = document.querySelectorAll('.seg-btn[data-sensor-mode]');

  // Modal
  const shortcutsModal = document.getElementById('shortcuts-modal');
  const btnShortcuts = document.getElementById('btn-shortcuts');
  const btnCloseModal = document.getElementById('btn-close-modal');

  // State
  let currentExposureUs = 4000;
  let postControlsTimer = null;
  let postDisplayTimer = null;
  let peakFocus = 10.0;
  let activeZoom = 'fit';
  let activeCenterX = 0.5;
  let activeCenterY = 0.5;
  let isSnapping = false;
  const fpsHistory = [];
  let activeWidth = 1332;
  let activeHeight = 990;
  let pendingControls = {};
  let pendingDisplay = {};
  let activeSensorMode = 2;
  let currentTargetFps = 120;
  let isApplyingPreset = false;
  // --- Stream Auto-Reconnect & Fallback Loop ---
  let isFallbackLoopRunning = false;
  let fallbackGeneration = 0;
  let fallbackAbortController = null;
  let fallbackTimerId = null;
  let fallbackCheckTimerId = null;
  let activeFallbackBlobUrl = null;
  const fallbackBlobUrls = new Set();

  function revokeFallbackBlobUrlSafely(url) {
    if (url && fallbackBlobUrls.has(url)) {
      URL.revokeObjectURL(url);
      fallbackBlobUrls.delete(url);
    }
  }

  function cleanupAllFallbackBlobUrls() {
    for (const url of fallbackBlobUrls) {
      URL.revokeObjectURL(url);
    }
    fallbackBlobUrls.clear();
    activeFallbackBlobUrl = null;
  }

  function getSelectedDisplayFps() {
    if (!selectViewFps) return 30;
    const v = parseInt(selectViewFps.value, 10);
    return (Number.isFinite(v) && v > 0) ? v : 30;
  }

  function stopFrameLoopFallback() {
    fallbackGeneration++;
    isFallbackLoopRunning = false;
    if (fallbackAbortController) {
      fallbackAbortController.abort();
      fallbackAbortController = null;
    }
    if (fallbackTimerId) {
      clearTimeout(fallbackTimerId);
      fallbackTimerId = null;
    }
    if (activeFallbackBlobUrl) {
      const u = activeFallbackBlobUrl;
      activeFallbackBlobUrl = null;
      revokeFallbackBlobUrlSafely(u);
    }
    cleanupAllFallbackBlobUrls();
  }

  function startFrameLoopFallback() {
    if (isFallbackLoopRunning) return;
    // Keep native stream if painted
    if (streamImg && streamImg.src.includes('/stream.mjpg') && streamImg.naturalWidth > 0) {
      return;
    }
    isFallbackLoopRunning = true;
    const generation = fallbackGeneration;

    async function step() {
      if (!isFallbackLoopRunning || generation !== fallbackGeneration) return;
      const startTime = performance.now();
      const controller = new AbortController();
      fallbackAbortController = controller;
      let blobUrl = null;

      try {
        const res = await fetch('/api/frame.jpg?t=' + Date.now(), {
          signal: controller.signal
        });
        if (!res.ok) throw new Error(`HTTP ${res.status}`);
        const blob = await res.blob();
        if (!isFallbackLoopRunning || generation !== fallbackGeneration) return;

        blobUrl = URL.createObjectURL(blob);
        fallbackBlobUrls.add(blobUrl);

        // Wait for decoded image before presentation
        const img = new Image();
        img.src = blobUrl;
        if (img.decode) {
          await img.decode();
        } else {
          await new Promise((resolve, reject) => {
            img.onload = resolve;
            img.onerror = () => reject(new Error('Image decode error'));
          });
        }

        if (!isFallbackLoopRunning || generation !== fallbackGeneration) {
          revokeFallbackBlobUrlSafely(blobUrl);
          return;
        }

        const prevUrl = activeFallbackBlobUrl;
        activeFallbackBlobUrl = blobUrl;
        if (streamImg) {
          streamImg.src = blobUrl;
        }
        if (prevUrl && prevUrl !== blobUrl) {
          setTimeout(() => revokeFallbackBlobUrlSafely(prevUrl), 200);
        }
      } catch (err) {
        if (blobUrl) {
          revokeFallbackBlobUrlSafely(blobUrl);
        }
      } finally {
        if (fallbackAbortController === controller) fallbackAbortController = null;
      }

      if (!isFallbackLoopRunning || generation !== fallbackGeneration) return;

      const elapsed = performance.now() - startTime;
      const targetFps = getSelectedDisplayFps();
      const targetInterval = 1000.0 / targetFps;
      const delay = Math.max(0, targetInterval - elapsed);

      fallbackTimerId = setTimeout(step, delay);
    }

    step();
  }

  function scheduleFallbackCheck() {
    if (fallbackCheckTimerId) {
      clearTimeout(fallbackCheckTimerId);
    }
    fallbackCheckTimerId = setTimeout(() => {
      fallbackCheckTimerId = null;
      // Do not use !streamImg.complete: multipart stream complete remains false even when healthy.
      if (streamImg && streamImg.naturalWidth === 0) {
        console.warn('Native MJPEG stream not painting, switching to frame loop fallback');
        startFrameLoopFallback();
      }
    }, 1200);
  }

  function reloadStream() {
    if (!streamImg) return;
    stopFrameLoopFallback();
    if (fallbackCheckTimerId) {
      clearTimeout(fallbackCheckTimerId);
      fallbackCheckTimerId = null;
    }
    streamImg.src = '/stream.mjpg?t=' + Date.now();
    scheduleFallbackCheck();
  }

  if (streamImg) {
    streamImg.onload = () => {
      if (streamImg.src.includes('/stream.mjpg') && streamImg.naturalWidth > 0) {
        if (fallbackCheckTimerId) {
          clearTimeout(fallbackCheckTimerId);
          fallbackCheckTimerId = null;
        }
      }
    };
    streamImg.onerror = () => {
      if (streamImg.src.includes('/stream.mjpg')) {
        if (fallbackCheckTimerId) {
          clearTimeout(fallbackCheckTimerId);
          fallbackCheckTimerId = null;
        }
        console.warn('Native MJPEG stream onerror, switching to frame loop fallback');
        startFrameLoopFallback();
      }
    };
  }

  scheduleFallbackCheck();



  function showToast(msg) {
    if (!toast) return;
    toast.textContent = msg;
    toast.classList.add('show');
    setTimeout(() => toast.classList.remove('show'), 3200);
  }

  // --- API Handlers ---
  function postControls(partial) {
    clearTimeout(postControlsTimer);
    Object.assign(pendingControls, partial);
    postControlsTimer = setTimeout(() => {
      const payload = { ...pendingControls };
      pendingControls = {};
      fetch('/api/controls', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload)
      }).catch(err => console.error('Failed to post controls:', err));
    }, 40);
  }

  function postDisplay(partial) {
    clearTimeout(postDisplayTimer);
    Object.assign(pendingDisplay, partial);
    postDisplayTimer = setTimeout(() => {
      const payload = { ...pendingDisplay };
      pendingDisplay = {};
      fetch('/api/display', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload)
      }).catch(err => console.error('Failed to post display:', err));
    }, 40);
  }

  // --- Tabs Navigation ---
  document.querySelectorAll('.itab-btn').forEach(btn => {
    btn.addEventListener('click', () => {
      document.querySelectorAll('.itab-btn').forEach(b => b.classList.remove('active'));
      document.querySelectorAll('.itab-pane').forEach(p => p.classList.remove('active'));
      btn.classList.add('active');
      const target = document.getElementById(btn.dataset.tab);
      if (target) target.classList.add('active');
      if (btn.dataset.tab === 'tab-media') {
        loadMediaGallery();
      } else if (btn.dataset.tab === 'tab-calib') {
        refreshCalibrationState();
      }
      history.replaceState(null, '', '#' + btn.dataset.tab);
    });
  });

  // --- Dynamic Target FPS & Telemetry HUD ---
  function updateTargetFpsUI(targetFps) {
    if (typeof targetFps === 'number' && targetFps > 0) {
      currentTargetFps = targetFps;
    }
    if (fpsGraphTitle) {
      fpsGraphTitle.textContent = `⚡ ${Math.round(currentTargetFps)} FPS 성능 안정성 (60초 히스토리)`;
    }
  }

  // --- Truthful Native Recording Telemetry ---
  function updateRecordingUI(rec) {
    if (!rec) return;
    const isRec = !!rec.recording;
    const wasRecording = isRecording;
    isRecording = isRec;

    if (btnRecord) {
      btnRecord.classList.toggle('is-recording', isRec);
    }
    if (recLabel) {
      recLabel.textContent = isRec ? '센서 원본 정지' : '센서 원본 REC';
    }

    if (isRec && !wasRecording) {
      recordSeconds = 0;
      updateRecordTimerDisplay();
      if (recTimer) recTimer.style.display = 'inline';
      if (!recordTimerInterval) {
        recordTimerInterval = setInterval(() => {
          recordSeconds++;
          updateRecordTimerDisplay();
        }, 1000);
      }
    } else if (!isRec && wasRecording) {
      if (recTimer) recTimer.style.display = 'none';
      if (recordTimerInterval) {
        clearInterval(recordTimerInterval);
        recordTimerInterval = null;
      }
    }

    // Header REC status badge
    if (badgeRec) {
      if (rec.error) {
        badgeRec.className = 'pill pill-danger';
        badgeRec.textContent = `REC 에러: ${rec.error}`;
        badgeRec.title = `센서 원본 녹화 에러: ${rec.error}`;
      } else if (isRec) {
        badgeRec.className = 'pill pill-rec-active';
        const fpsVal = rec.actual_fps > 0 ? rec.actual_fps.toFixed(1) : (rec.fps || currentTargetFps);
        const drops = rec.dropped_frames || 0;
        badgeRec.textContent = `● 센서 원본 REC (${rec.width || activeWidth}×${rec.height || activeHeight} @ ${fpsVal}fps)`;
        badgeRec.title = `센서 원본 무손실 녹화 중: ${rec.frames || 0} 프레임, 드롭 ${drops}개 (${rec.path || ''})`;
      } else {
        badgeRec.className = 'pill pill-muted';
        badgeRec.textContent = '센서 원본 REC 대기';
        badgeRec.title = '센서 원본 고속 레코더 대기 중';
      }
    }

    // Viewport OSD rec badge
    if (osdRecBadge) {
      if (isRec) {
        osdRecBadge.style.display = 'block';
        const fpsVal = rec.actual_fps > 0 ? rec.actual_fps.toFixed(1) : (rec.fps || currentTargetFps);
        const drops = rec.dropped_frames || 0;
        osdRecBadge.textContent = `● 센서 원본 REC | ${rec.width || activeWidth}×${rec.height || activeHeight} @ ${fpsVal} FPS | 드롭 ${drops}`;
        osdRecBadge.style.background = drops > 0 ? 'rgba(239, 68, 68, 0.95)' : 'rgba(220, 38, 38, 0.9)';
      } else {
        osdRecBadge.style.display = 'none';
      }
    }
  }

  // --- AI Stream & Heartbeat Telemetry ---
  function updateAiUI(ai) {
    if (!badgeAi && !hudAi) return;
    const isMode2 = (activeSensorMode === 2);
    const enabled = isMode2 && ai && ai.enabled;
    const readerActive = enabled && ai.reader_active;

    if (!enabled) {
      if (badgeAi) {
        badgeAi.className = 'pill pill-muted';
        badgeAi.textContent = 'AI: OFF';
        badgeAi.title = 'AI 스트림 꺼짐 (모드2 1332×990 @ 120fps 전용)';
      }
      if (hudAi) hudAi.textContent = 'AI: OFF';
    } else if (!readerActive) {
      if (badgeAi) {
        badgeAi.className = 'pill pill-ready';
        badgeAi.textContent = 'AI: READY (SHM)';
        badgeAi.title = `AI 공유메모리(${ai.shm_name || '/dev/shm'}) 준비 완료, 외부 판독기 연결 대기`;
      }
      if (hudAi) hudAi.textContent = 'AI: READY';
    } else {
      if (badgeAi) {
        badgeAi.className = 'pill pill-active';
        badgeAi.textContent = 'AI: ACTIVE (수신)';
        badgeAi.title = `외부 판독기 하트비트 수신 중 (주의: 판독기 프로세스 연결 하트비트이며 모델 추론 성공 여부가 아닙니다)`;
      }
      if (hudAi) hudAi.textContent = 'AI: ACTIVE';
    }
  }

  // --- One-Click Preset Apply Handler ---
  async function applyPreset(name, isPurpose = false) {
    if (!name || isApplyingPreset || isSwitchingSensorMode) return;
    isApplyingPreset = true;

    // Cancel pending debounced old control/display posts
    if (postControlsTimer) {
      clearTimeout(postControlsTimer);
      postControlsTimer = null;
    }
    pendingControls = {};
    if (postDisplayTimer) {
      clearTimeout(postDisplayTimer);
      postDisplayTimer = null;
    }
    pendingDisplay = {};

    // Disable conflicting mode/purpose buttons
    sensorModeBtns.forEach(b => { b.disabled = true; b.style.pointerEvents = 'none'; });
    if (btnPresetIndoor) btnPresetIndoor.disabled = true;
    if (btnPresetPitch) btnPresetPitch.disabled = true;
    if (btnLoadPreset) btnLoadPreset.disabled = true;

    showToast(`프리셋 적용 중: ${name}...`);

    try {
      const res = await fetch(`/api/presets/${encodeURIComponent(name)}/apply`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' }
      });
      const data = await res.json().catch(() => ({}));
      if (!res.ok || data.status === 'error') {
        throw new Error(data.error || `HTTP ${res.status}`);
      }

      applyStateToUI(data);
      if (data.recording_status) updateRecordingUI(data.recording_status);
      if (data.ai_status) updateAiUI(data.ai_status);

      const activeMode = data.mode !== undefined ? data.mode : data.capabilities?.active_sensor_mode;
      if (activeMode !== undefined) {
        activeSensorMode = activeMode;
        sensorModeBtns.forEach(b => b.classList.toggle('active', parseInt(b.dataset.sensorMode, 10) === activeMode));
      }
      if (data.target_fps) {
        updateTargetFpsUI(data.target_fps);
      }

      if (btnPresetIndoor) btnPresetIndoor.classList.toggle('active', name === 'indoor-preview');
      if (btnPresetPitch) btnPresetPitch.classList.toggle('active', name === 'pitch-ai-120fps');

      if (name === 'pitch-ai-120fps') {
        showToast('투구·AI 120fps 적용됨 (1ms 셔터는 충분한 조명이 필요하며 AI 판독기는 별도 연결됩니다)');
      } else if (name === 'indoor-preview') {
        showToast('실내 확인 적용됨 (1080p 60fps / 8ms 노출)');
      } else {
        showToast(`프리셋 적용 완료: ${name}`);
      }

      reloadStream();
    } catch (err) {
      console.error('Failed to apply preset:', err);
      showToast(`프리셋 적용 실패: ${err.message || err}`);
    } finally {
      isApplyingPreset = false;
      sensorModeBtns.forEach(b => { b.disabled = false; b.style.pointerEvents = ''; });
      if (btnPresetIndoor) btnPresetIndoor.disabled = false;
      if (btnPresetPitch) btnPresetPitch.disabled = false;
      if (btnLoadPreset) btnLoadPreset.disabled = false;
    }
  }

  if (btnPresetIndoor) {
    btnPresetIndoor.addEventListener('click', () => applyPreset('indoor-preview', true));
  }
  if (btnPresetPitch) {
    btnPresetPitch.addEventListener('click', () => applyPreset('pitch-ai-120fps', true));
  }

  // --- Sensor Mode Switching (4K 30P / 1080 60P / 990 120P) ---
  let isSwitchingSensorMode = false;
  sensorModeBtns.forEach(btn => {
    btn.addEventListener('click', () => {
      if (isSwitchingSensorMode || isApplyingPreset) return;
      const mode = parseInt(btn.dataset.sensorMode, 10);
      isSwitchingSensorMode = true;

      // Cancel pending debounced posts
      if (postControlsTimer) {
        clearTimeout(postControlsTimer);
        postControlsTimer = null;
      }
      pendingControls = {};
      if (postDisplayTimer) {
        clearTimeout(postDisplayTimer);
        postDisplayTimer = null;
      }
      pendingDisplay = {};

      sensorModeBtns.forEach(b => b.style.pointerEvents = 'none');
      if (btnPresetIndoor) btnPresetIndoor.disabled = true;
      if (btnPresetPitch) btnPresetPitch.disabled = true;

      showToast(`Switching to sensor mode ${mode}...`);
      fetch('/api/sensor_mode', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ mode: mode })
      })
      .then(async r => {
        const d = await r.json().catch(() => ({}));
        if (!r.ok || d.status !== 'ok') {
          throw new Error(d.error || `HTTP ${r.status}`);
        }
        return d;
      })
      .then(d => {
        activeSensorMode = mode;
        sensorModeBtns.forEach(b => b.classList.toggle('active', parseInt(b.dataset.sensorMode, 10) === mode));
        if (btnPresetIndoor) btnPresetIndoor.classList.toggle('active', mode === 1);
        if (btnPresetPitch) btnPresetPitch.classList.toggle('active', mode === 2);

        hudRes.textContent = `${d.width}×${d.height}`;
        hudSensor.textContent = `${d.target_fps.toFixed(0)} FPS`;
        updateTargetFpsUI(d.target_fps);
        showToast(`Active: ${d.width}×${d.height} @ ${d.target_fps} FPS`);
        // Re-fetch state for new exposure limits
        fetch('/api/state').then(r => r.json()).then(applyStateToUI).catch(console.error);
        reloadStream();
      })
      .catch(e => showToast(`Mode switch failed: ${e.message || e}`))
      .finally(() => {
        isSwitchingSensorMode = false;
        sensorModeBtns.forEach(b => b.style.pointerEvents = '');
        if (btnPresetIndoor) btnPresetIndoor.disabled = false;
        if (btnPresetPitch) btnPresetPitch.disabled = false;
      });
    });
  });

  // --- Video Recording Toggle ---
  function updateRecordTimerDisplay() {
    const mins = String(Math.floor(recordSeconds / 60)).padStart(2, '0');
    const secs = String(recordSeconds % 60).padStart(2, '0');
    if (recTimer) recTimer.textContent = `${mins}:${secs}`;
  }

  function toggleRecording() {
    if (!isRecording) {
      fetch('/api/record/start', { method: 'POST' })
        .then(r => r.json())
        .then(d => {
          if (d.status === 'ok') {
            updateRecordingUI(d.recording_status || d);
            showToast(`● 센서 원본 녹화 시작: ${d.path || ''}`);
          } else {
            showToast(`녹화 시작 실패: ${d.error || '알 수 없는 오류'}`);
          }
        })
        .catch(e => showToast(`녹화 오류: ${e.message || e}`));
    } else {
      fetch('/api/record/stop', { method: 'POST' })
        .then(r => r.json())
        .then(d => {
          updateRecordingUI(d.recording_status || { recording: false, frames: d.frames });
          const dropMsg = (d.dropped_frames || (d.recording_status && d.recording_status.dropped_frames)) ? ` (드롭: ${d.dropped_frames || d.recording_status.dropped_frames})` : '';
          showToast(`■ 센서 원본 저장 완료: ${d.path || ''} (${d.frames || 0} frames${dropMsg})`);
          loadMediaGallery();
        })
        .catch(e => showToast(`녹화 오류: ${e.message || e}`));
    }
  }
  if (btnRecord) btnRecord.addEventListener('click', toggleRecording);

  // --- Camera Calibration Handlers ---
  function refreshCalibrationState() {
    fetch('/api/calibration')
      .then(r => r.json())
      .then(d => {
        if (calibFrameCount) calibFrameCount.textContent = d.frame_count || 0;
        if (d.status === 'ok') {
          if (calibStatusText) calibStatusText.textContent = 'Calibrated';
          if (calibRmsText) calibRmsText.textContent = `${d.rms_reprojection_error.toFixed(3)} px`;
          if (calibMatrixGrid && d.camera_matrix) {
            calibMatrixGrid.innerHTML = `
              <span>fx: ${d.camera_matrix.fx.toFixed(1)}</span>
              <span>cx: ${d.camera_matrix.cx.toFixed(1)}</span>
              <span>fy: ${d.camera_matrix.fy.toFixed(1)}</span>
              <span>cy: ${d.camera_matrix.cy.toFixed(1)}</span>
              <span>k1: ${d.distortion.k1.toFixed(3)}</span>
              <span>k2: ${d.distortion.k2.toFixed(3)}</span>
            `;
          }
        } else {
          if (calibStatusText) calibStatusText.textContent = 'Uncalibrated';
          if (calibRmsText) calibRmsText.textContent = '-- px';
        }
      })
      .catch(console.error);
  }

  if (btnCalibCapture) {
    btnCalibCapture.addEventListener('click', () => {
      fetch('/api/calibration/capture', { method: 'POST' })
        .then(r => r.json())
        .then(d => {
          if (calibFrameCount) calibFrameCount.textContent = d.frame_count;
          showToast(d.chessboard_found ? `✓ Chessboard detected (Frame ${d.frame_count})` : '✗ No 9×6 chessboard found');
        });
    });
  }
  if (btnCalibCompute) {
    btnCalibCompute.addEventListener('click', () => {
      showToast('Computing camera matrix...');
      fetch('/api/calibration/compute', { method: 'POST' })
        .then(r => r.json())
        .then(d => {
          if (d.status === 'ok') {
            showToast(`Calibration success! RMS: ${d.rms_reprojection_error.toFixed(3)} px`);
            refreshCalibrationState();
          } else {
            showToast(`Calibration error: ${d.error || 'Need ≥3 frames'}`);
          }
        });
    });
  }
  if (btnCalibClear) {
    btnCalibClear.addEventListener('click', () => {
      fetch('/api/calibration/clear', { method: 'POST' })
        .then(() => {
          if (calibFrameCount) calibFrameCount.textContent = '0';
          showToast('Calibration frames reset');
        });
    });
  }

  // --- Motion Blur Calculator ---
  function updateBlurCalculator() {
    if (!inputBallSpeed || !inputFovWidth) return;
    const kmh = parseFloat(inputBallSpeed.value) || 145;
    const fovM = parseFloat(inputFovWidth.value) || 3.0;
    const speedMs = kmh / 3.6;
    if (calcSpeedMs) calcSpeedMs.textContent = speedMs.toFixed(1) + ' m/s';

    const expSec = currentExposureUs / 1e6;
    const blurMeters = speedMs * expSec;
    const blurCm = blurMeters * 100;
    const blurPx = (blurMeters / fovM) * activeWidth;

    if (calcBlurCm) calcBlurCm.textContent = blurCm.toFixed(1) + ' cm';
    if (calcBlurCmBadge) calcBlurCmBadge.textContent = blurCm.toFixed(1) + ' cm';
    if (calcBlurPx) calcBlurPx.textContent = Math.round(blurPx) + ' px';

    if (calcBlurRec) {
      if (blurCm <= 4.0) {
        calcBlurRec.textContent = 'Low blur (<4cm) ✓';
      } else {
        calcBlurRec.textContent = `High blur! Target ≤ ${(4.0 / (speedMs * 100) * 1000).toFixed(1)}ms`;
      }
    }

    if (blurStreakLine) {
      const maxStreakPct = 85;
      const pct = Math.min(maxStreakPct, Math.max(5, (blurCm / 30.0) * maxStreakPct));
      blurStreakLine.style.width = pct.toFixed(1) + '%';
    }
  }

  if (inputBallSpeed) inputBallSpeed.addEventListener('input', updateBlurCalculator);
  if (inputFovWidth) inputFovWidth.addEventListener('input', updateBlurCalculator);

  document.querySelectorAll('.btn-chip[data-speed]').forEach(btn => {
    btn.addEventListener('click', () => {
      document.querySelectorAll('.btn-chip[data-speed]').forEach(b => b.classList.remove('active'));
      btn.classList.add('active');
      inputBallSpeed.value = btn.dataset.speed;
      updateBlurCalculator();
    });
  });

  const btnApplyRecShutter = document.getElementById('btn-apply-rec-shutter');
  if (btnApplyRecShutter) {
    btnApplyRecShutter.addEventListener('click', () => {
      const speedKmh = parseFloat(inputBallSpeed.value) || 145;
      const targetExp = speedKmh >= 150 ? 500 : 1000;
      const targetGain = targetExp === 500 ? 8.0 : 4.0;
      postControls({ auto_exposure: false, exposure_us: targetExp, gain: targetGain });
      rangeExp.value = targetExp;
      valExpUs.textContent = targetExp;
      valExpMs.textContent = `${(targetExp / 1000).toFixed(2)} ms`;
      rangeGain.value = targetGain;
      valGain.textContent = `${targetGain.toFixed(2)}`;
      currentExposureUs = targetExp;
      updateBlurCalculator();
      showToast(`Applied ${targetExp / 1000}ms shutter @ ${targetGain}× gain`);
    });
  }

  // --- Pitch Auto-Clip Handlers & Readiness ---
  function updatePitchStatusUI(d) {
    if (!d) return;
    const motion = d.current_motion_level !== undefined ? d.current_motion_level : (d.motion_level || 0);
    if (valPitchMotion) valPitchMotion.textContent = `${motion} %`;
    if (pitchMotionFill) pitchMotionFill.style.width = `${Math.min(100, motion)}%`;
    if (valPitchClipsCount) valPitchClipsCount.textContent = d.total_clips !== undefined ? d.total_clips : 0;
    if (pitchClipStatusBadge) {
      if (d.is_clipping) {
        pitchClipStatusBadge.textContent = '● 클립 녹화 중 (CLIPPING)';
        pitchClipStatusBadge.className = 'pill pill-danger';
      } else {
        pitchClipStatusBadge.textContent = '대기 중 (IDLE)';
        pitchClipStatusBadge.className = 'pill pill-muted';
      }
    }

    // Native pre-roll buffer readiness
    const reqPreRoll = (selectPitchPreroll && parseFloat(selectPitchPreroll.value)) || 1.5;
    const bufSec = typeof d.buffered_sec === 'number' ? d.buffered_sec : 0;
    if (valPitchBuffered) {
      valPitchBuffered.textContent = `${bufSec.toFixed(1)}s / ${reqPreRoll.toFixed(1)}s`;
    }
    if (pitchBufferFill) {
      const pct = Math.min(100, Math.round((bufSec / reqPreRoll) * 100));
      pitchBufferFill.style.width = `${pct}%`;
    }
    if (pitchBufferReadiness) {
      if (bufSec >= reqPreRoll * 0.9) {
        pitchBufferReadiness.textContent = '버퍼 준비 완료 (READY)';
        pitchBufferReadiness.className = 'pill pill-success';
      } else {
        pitchBufferReadiness.textContent = '버퍼 충전 중...';
        pitchBufferReadiness.className = 'pill pill-ready';
      }
    }
    if (pitchNativeSpec) {
      const w = d.width || activeWidth;
      const h = d.height || activeHeight;
      const fps = d.fps ? d.fps.toFixed(0) : currentTargetFps.toFixed(0);
      pitchNativeSpec.textContent = `${w}×${h} @ ${fps} FPS`;
    }
    if (pitchErrorAlert) {
      if (d.error) {
        pitchErrorAlert.style.display = 'block';
        pitchErrorAlert.textContent = `피치 에러: ${d.error}`;
      } else {
        pitchErrorAlert.style.display = 'none';
      }
    }
  }

  function updatePitchConfig() {
    if (!chkPitchAutoClip) return;
    const cfg = {
      enabled: chkPitchAutoClip.checked,
      min_motion_pixels: 800,
      pre_roll_sec: parseFloat(selectPitchPreroll.value) || 1.5,
      post_roll_sec: parseFloat(selectPitchPostroll.value) || 1.5,
      cooldown_sec: 3.0
    };
    fetch('/api/pitch/config', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(cfg)
    }).catch(console.error);
    showToast(`Pitch Auto-Clip: ${cfg.enabled ? 'ON' : 'OFF'}`);
  }

  if (chkPitchAutoClip) chkPitchAutoClip.addEventListener('change', updatePitchConfig);
  if (selectPitchPreroll) selectPitchPreroll.addEventListener('change', updatePitchConfig);
  if (selectPitchPostroll) selectPitchPostroll.addEventListener('change', updatePitchConfig);

  setInterval(() => {
    fetch('/api/pitch/status')
      .then(r => r.json())
      .then(updatePitchStatusUI)
      .catch(() => {});
  }, 350);

  // --- Telemetry Drawing (Sparkline & Histogram) ---
  function drawSparkline(val) {
    if (!sparklineCtx || !sparklineCanvas) return;
    if (val > 0) {
      fpsHistory.push(val);
      if (fpsHistory.length > 60) fpsHistory.shift();
    }
    if (fpsHistory.length < 2) return;

    const w = sparklineCanvas.width;
    const h = sparklineCanvas.height;
    sparklineCtx.fillStyle = '#edf1f5';
    sparklineCtx.fillRect(0, 0, w, h);

    // Target FPS horizontal guideline matching currentTargetFps
    const maxScale = Math.max(currentTargetFps * 1.2, 35.0);
    const targetY = h - (currentTargetFps / maxScale) * (h - 6) - 3;
    sparklineCtx.strokeStyle = 'rgba(16, 185, 129, 0.5)';
    sparklineCtx.lineWidth = 1;
    sparklineCtx.setLineDash([3, 3]);
    sparklineCtx.beginPath();
    sparklineCtx.moveTo(0, targetY);
    sparklineCtx.lineTo(w, targetY);
    sparklineCtx.stroke();
    sparklineCtx.setLineDash([]);

    // Line
    sparklineCtx.beginPath();
    const step = w / 60;
    const offset = 60 - fpsHistory.length;
    for (let i = 0; i < fpsHistory.length; i++) {
      const x = (offset + i) * step;
      const y = h - Math.min(1.0, Math.max(0.0, fpsHistory[i] / maxScale)) * (h - 6) - 3;
      if (i === 0) sparklineCtx.moveTo(x, y);
      else sparklineCtx.lineTo(x, y);
    }
    sparklineCtx.strokeStyle = val < (currentTargetFps * 0.9) ? '#ef4444' : '#0284c7';
    sparklineCtx.lineWidth = 1.5;
    sparklineCtx.stroke();

    if (sparklineVal) sparklineVal.textContent = (val > 0 ? val.toFixed(1) : '--') + ' fps';
  }

  function drawHistogram(bins) {
    if (!histCtx || !bins || bins.length !== 64) return;
    const w = histCanvas.width;
    const h = histCanvas.height;
    histCtx.fillStyle = '#edf1f5';
    histCtx.fillRect(0, 0, w, h);

    const maxVal = Math.max(...bins, 1);
    const barW = w / 64;

    for (let i = 0; i < 64; i++) {
      const barH = (bins[i] / maxVal) * (h - 2);
      const x = i * barW;
      const y = h - barH;
      histCtx.fillStyle = '#0284c7';
      histCtx.fillRect(x, y, barW - 0.5, barH);
    }
  }

  // --- Minimap Navigator Update ---
  function updateMinimap() {
    if (!minimapBox || !minimapCropRect) return;
    if (activeZoom === 'fit') {
      minimapBox.style.display = 'none';
      if (hudZoom) hudZoom.textContent = 'FIT';
      return;
    }

    minimapBox.style.display = 'block';
    if (hudZoom) hudZoom.textContent = `${activeZoom.toUpperCase()} (${Math.round(activeCenterX*100)}%, ${Math.round(activeCenterY*100)}%)`;

    const cropFactor = (activeZoom === '1x') ? Math.min(1.0, 960 / activeWidth) : (activeZoom === '4x') ? 0.25 : 0.5;
    const cropW = cropFactor;
    const cropH = cropFactor;

    const left = Math.max(0, Math.min(1 - cropW, activeCenterX - cropW / 2));
    const top = Math.max(0, Math.min(1 - cropH, activeCenterY - cropH / 2));

    minimapCropRect.style.left = (left * 100).toFixed(1) + '%';
    minimapCropRect.style.top = (top * 100).toFixed(1) + '%';
    minimapCropRect.style.width = (cropW * 100).toFixed(1) + '%';
    minimapCropRect.style.height = (cropH * 100).toFixed(1) + '%';
  }

  // --- Synchronize State to UI ---
  function applyStateToUI(state) {
    if (!state) return;
    const ctrl = state.controls;
    const ds = state.display_settings || state.display;
    const caps = state.capabilities;

    if (caps) {
      if (caps.width && caps.height && hudRes) hudRes.textContent = `${caps.width}×${caps.height}`;
      if (caps.width) activeWidth = caps.width;
      if (caps.height) activeHeight = caps.height;
      if (caps.target_fps) {
        updateTargetFpsUI(caps.target_fps);
        if (hudSensor) hudSensor.textContent = `${caps.target_fps.toFixed(0)} FPS`;
      }
      if (caps.active_sensor_mode !== undefined) {
        activeSensorMode = caps.active_sensor_mode;
        sensorModeBtns.forEach(b => b.classList.toggle('active', parseInt(b.dataset.sensorMode, 10) === caps.active_sensor_mode));
      }
      if (caps.max_exposure_us && rangeExp) {
        rangeExp.max = caps.max_exposure_us;
      }
    }

    if (state.mode !== undefined) {
      activeSensorMode = state.mode;
      sensorModeBtns.forEach(b => b.classList.toggle('active', parseInt(b.dataset.sensorMode, 10) === state.mode));
    }
    if (state.target_fps !== undefined) {
      updateTargetFpsUI(state.target_fps);
    }
    if (state.recording_status) {
      updateRecordingUI(state.recording_status);
    }
    if (state.ai_status) {
      updateAiUI(state.ai_status);
    }
    if (ctrl) {
      if (ctrl.auto_exposure !== undefined) {
        const radio = document.querySelector(`input[name="ae-mode"][value="${ctrl.auto_exposure ? 'auto' : 'manual'}"]`);
        if (radio) {
          radio.checked = true;
          document.querySelectorAll('.seg-group input[name="ae-mode"]').forEach(inp => {
            inp.closest('.seg-btn').classList.toggle('active', inp.checked);
          });
        }
        if (manualExpGroup) manualExpGroup.style.opacity = ctrl.auto_exposure ? '0.4' : '1.0';
        if (manualGainGroup) manualGainGroup.style.opacity = ctrl.auto_exposure ? '0.4' : '1.0';
        if (aeOptions) aeOptions.style.display = ctrl.auto_exposure ? 'block' : 'none';
      }
      if (ctrl.exposure_us !== undefined && rangeExp) {
        rangeExp.value = ctrl.exposure_us;
        if (valExpUs) valExpUs.textContent = ctrl.exposure_us;
        if (valExpMs) valExpMs.textContent = (ctrl.exposure_us / 1000).toFixed(2) + ' ms';
        currentExposureUs = ctrl.exposure_us;
        updateBlurCalculator();
        document.querySelectorAll('.btn-chip[data-exp]').forEach(b => {
          b.classList.toggle('active', parseInt(b.dataset.exp, 10) === ctrl.exposure_us);
        });
      }
      if (ctrl.gain !== undefined && rangeGain) {
        rangeGain.value = ctrl.gain;
        if (valGain) valGain.textContent = ctrl.gain.toFixed(2);
        document.querySelectorAll('.btn-chip[data-gain]').forEach(b => {
          b.classList.toggle('active', Math.abs(parseFloat(b.dataset.gain) - ctrl.gain) < 0.1);
        });
      }
      if (ctrl.ae_max_exposure_us !== undefined && rangeAeMaxExp) {
        rangeAeMaxExp.value = ctrl.ae_max_exposure_us;
        if (valAeMaxExp) valAeMaxExp.textContent = ctrl.ae_max_exposure_us + ' \u03bcs';
      }
      if (ctrl.ae_max_gain !== undefined && rangeAeMaxGain) {
        rangeAeMaxGain.value = ctrl.ae_max_gain;
        if (valAeMaxGain) valAeMaxGain.textContent = ctrl.ae_max_gain.toFixed(1) + '\u00d7';
      }
      if (ctrl.ae_lock !== undefined && chkAeLock) chkAeLock.checked = ctrl.ae_lock;
      if (ctrl.ev_compensation !== undefined && rangeEv) {
        rangeEv.value = ctrl.ev_compensation;
        if (valEv) valEv.textContent = (ctrl.ev_compensation >= 0 ? '+' : '') + ctrl.ev_compensation.toFixed(2) + ' EV';
      }
      if (ctrl.antibanding && selectAntibanding) selectAntibanding.value = ctrl.antibanding;
      if (ctrl.awb_mode && selectAwb) selectAwb.value = ctrl.awb_mode;
      if (ctrl.awb_lock !== undefined && chkAwbLock) chkAwbLock.checked = ctrl.awb_lock;
      if (ctrl.isp_digital_gain !== undefined && rangeIspGain) {
        rangeIspGain.value = ctrl.isp_digital_gain;
        if (valIspGain) valIspGain.textContent = ctrl.isp_digital_gain.toFixed(1) + '\u00d7';
      }
      if (ctrl.denoise_mode && selectDenoise) selectDenoise.value = ctrl.denoise_mode;
      if (ctrl.edge_mode && selectEdge) selectEdge.value = ctrl.edge_mode;
    }

    if (ds) {
      if (ds.zoom_mode) {
        activeZoom = ds.zoom_mode;
        zoomBtns.forEach(b => b.classList.toggle('active', b.dataset.zoom === activeZoom));
        updateMinimap();
      }
      if (ds.focus_peaking !== undefined && chkPeaking) chkPeaking.checked = ds.focus_peaking;
      if (ds.peaking_color && selectPeakingColor) selectPeakingColor.value = ds.peaking_color;
      if (ds.zebra_stripes !== undefined && chkZebra) chkZebra.checked = ds.zebra_stripes;
      if (ds.grid_mode && selectGrid) selectGrid.value = ds.grid_mode;
      if (ds.fps_limit && selectViewFps) selectViewFps.value = String(ds.fps_limit);
    }
  }

  // --- Real-time SSE Stream Consumer ---
  function setupSSE() {
    const es = new EventSource('/events');
    es.onopen = () => {
      if (connPill) connPill.classList.add('connected');
    };
    es.onerror = () => {
      if (connPill) connPill.classList.remove('connected');
    };
    es.onmessage = (event) => {
      try {
        const d = JSON.parse(event.data);
        const cm = d.camera_metrics;
        const dm = d.display_metrics;

        if (cm) {
          if (metricSensorFps) metricSensorFps.textContent = cm.sensor_fps > 0 ? cm.sensor_fps.toFixed(1) : '--';
          if (hudSensor) {
            const fpsVal = cm.sensor_fps > 0 ? cm.sensor_fps.toFixed(1) : (currentTargetFps || 120).toFixed(0);
            hudSensor.textContent = `${fpsVal} FPS`;
          }
          if (metricGaps) metricGaps.textContent = cm.sequence_gaps || 0;
          if (metricTotalFrames) metricTotalFrames.textContent = (cm.total_frames || 0).toLocaleString();
          if (metricUptime && cm.uptime_ms) {
            const sec = Math.floor(cm.uptime_ms / 1000);
            const m = Math.floor(sec / 60);
            const s = sec % 60;
            metricUptime.textContent = `${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}`;
          }
          if (valActualCct && cm.actual_cct) valActualCct.textContent = cm.actual_cct + ' K';
          drawSparkline(cm.sensor_fps || 0);
        }
        if (d.system_metrics) {
          if (metricCpuTemp) metricCpuTemp.textContent = `${d.system_metrics.cpu_temp_c.toFixed(1)}°C`;
          if (metricRamUsed) metricRamUsed.textContent = `${(d.system_metrics.ram_used_mb / 1024).toFixed(1)} GB`;
        }

        if (dm) {
          if (hudFps) hudFps.textContent = (dm.display_fps || 0).toFixed(0) + ' FPS VIEW';
          if (metricViewFps) metricViewFps.textContent = (dm.display_fps || 0).toFixed(0);
          if (dm.focus_score !== undefined) {
            const score = Math.round(dm.focus_score);
            if (focusScoreVal) focusScoreVal.textContent = score;
            peakFocus = Math.max(peakFocus, score, 50);
            const pct = Math.min(100, Math.max(0, (score / peakFocus) * 100));
            if (focusMeterFill) focusMeterFill.style.width = pct + '%';
            if (focusMeterPeak) focusMeterPeak.style.left = pct + '%';
          }
          if (dm.histogram_64) {
            drawHistogram(dm.histogram_64);
          }
        }

        if (d.recording_status) {
          updateRecordingUI(d.recording_status);
        }
        if (d.ai_status) {
          updateAiUI(d.ai_status);
        }
      } catch (err) {
        console.error('SSE parse error:', err);
      }
    };
  }

  // --- Snapshot Trigger ---
  function triggerSnapshot() {
    if (isSnapping) return;
    isSnapping = true;
    if (btnSnapshot) {
      btnSnapshot.disabled = true;
      btnSnapshot.textContent = '...';
    }
    if (shutterFlash) {
      shutterFlash.classList.remove('active');
      void shutterFlash.offsetWidth;
      shutterFlash.classList.add('active');
      setTimeout(() => shutterFlash.classList.remove('active'), 200);
    }

    fetch('/api/snapshot', { method: 'POST' })
      .then(r => r.json())
      .then(d => {
        if (d.status === 'ok') {
          showToast(`Snapshot saved: ${d.path}`);
          loadMediaGallery();
        } else {
          showToast(`Snapshot failed: ${d.error || 'Unknown error'}`);
        }
      })
      .catch(e => showToast('Error: ' + e))
      .finally(() => {
        isSnapping = false;
        if (btnSnapshot) {
          btnSnapshot.disabled = false;
          btnSnapshot.textContent = 'SNAP';
        }
      });
  }
  if (btnSnapshot) btnSnapshot.addEventListener('click', triggerSnapshot);

  // --- Combined Media Gallery (Snapshots & Video Recordings) ---
  function loadMediaGallery() {
    if (!galleryContainer) return;
    Promise.all([
      fetch('/api/snapshots').then(r => r.json()).catch(() => []),
      fetch('/api/recordings').then(r => r.json()).catch(() => [])
    ]).then(([snaps, recs]) => {
      const items = [];
      (snaps || []).forEach(s => items.push({ ...s, type: 'photo' }));
      (recs || []).forEach(r => items.push({ ...r, type: 'video' }));

      if (items.length === 0) {
        galleryContainer.innerHTML = '<p class="gallery-empty">No media captured yet.</p>';
        return;
      }

      galleryContainer.innerHTML = '';
      items.sort((a, b) => (b.filename || '').localeCompare(a.filename || '')).slice(0, 16).forEach(item => {
        const card = document.createElement('div');
        card.className = 'gallery-card';
        const isVid = item.type === 'video';
        const metaCsvHtml = item.metadata_url
          ? `<a href="${item.metadata_url}" download class="gallery-csv-link" title="센서 타임스탬프 CSV 다운로드">CSV 메타데이터</a>`
          : '';
        card.innerHTML = `
          <a href="${item.url}" class="${isVid ? 'replay-link' : ''}" data-url="${item.url}" data-name="${item.filename}" data-meta="${item.metadata_url || ''}" ${isVid ? '' : 'target="_blank"'}>
            ${isVid ? `<div style="height:90px;background:#0f172a;color:#fff;display:flex;align-items:center;justify-content:center;font-weight:700;font-size:11px;cursor:pointer;">▶ SLOW-MO REPLAY</div>` : `<img src="${item.url}" alt="${item.filename}" loading="lazy">`}
          </a>
          <div class="gallery-meta">
            <span class="${isVid ? 'replay-link' : ''}" data-url="${item.url}" data-name="${item.filename}" data-meta="${item.metadata_url || ''}" style="${isVid ? 'cursor:pointer;color:var(--accent);font-weight:700;' : ''}">${item.filename.length > 20 ? item.filename.substring(0, 18) + '..' : item.filename}</span>
            <div style="display:flex;gap:6px;align-items:center;">
              ${metaCsvHtml}
              <a href="${item.url}" download style="color:var(--accent);text-decoration:none;font-weight:700;">Save</a>
            </div>
          </div>
        `;
        galleryContainer.appendChild(card);
      });

      document.querySelectorAll('.replay-link').forEach(link => {
        link.addEventListener('click', (e) => {
          e.preventDefault();
          openReplayModal(link.dataset.url, link.dataset.name, link.dataset.meta);
        });
      });
    }).catch(console.error);
  }
  if (btnRefreshGallery) btnRefreshGallery.addEventListener('click', loadMediaGallery);

  // --- Fullscreen Toggle ---
  if (btnFullscreen) {
    btnFullscreen.addEventListener('click', () => {
      viewportCard.classList.toggle('is-fullscreen');
      const isFs = viewportCard.classList.contains('is-fullscreen');
      btnFullscreen.innerHTML = isFs ? '✕' : '⛶';
    });
  }

  // --- Keyboard Shortcuts Handler ---
  window.addEventListener('keydown', (e) => {
    if (e.repeat) return;
    if (['INPUT', 'SELECT', 'TEXTAREA'].includes(e.target.tagName)) return;
    if (e.code === 'Space') {
      e.preventDefault();
      triggerSnapshot();
    } else if (e.key === 'r' || e.key === 'R') {
      toggleRecording();
    } else if (e.key === 'f' || e.key === 'F') {
      if (btnFullscreen) btnFullscreen.click();
    } else if (e.key === 'p' || e.key === 'P') {
      if (chkPeaking) {
        chkPeaking.checked = !chkPeaking.checked;
        postDisplay({ focus_peaking: chkPeaking.checked });
        showToast(`Peaking: ${chkPeaking.checked ? 'ON' : 'OFF'}`);
      }
    } else if (e.key === 'z' || e.key === 'Z') {
      const modes = ['fit', '1x', '2x', '4x'];
      const next = modes[(modes.indexOf(activeZoom) + 1) % modes.length];
      const targetBtn = document.querySelector(`.btn-tool[data-zoom="${next}"]`);
      if (targetBtn) targetBtn.click();
    } else if (e.key === '1') {
      const b = document.querySelector('.btn-chip[data-exp="500"]'); if (b) b.click();
    } else if (e.key === '2') {
      const b = document.querySelector('.btn-chip[data-exp="1000"]'); if (b) b.click();
    } else if (e.key === '3') {
      const b = document.querySelector('.btn-chip[data-exp="2000"]'); if (b) b.click();
    } else if (e.key === '4') {
      const b = document.querySelector('.btn-chip[data-exp="4000"]'); if (b) b.click();
    } else if (e.key === '?' || (e.key === '/' && e.shiftKey)) {
      if (shortcutsModal) shortcutsModal.style.display = shortcutsModal.style.display === 'none' ? 'flex' : 'none';
    } else if (e.key === 'Escape') {
      if (shortcutsModal) shortcutsModal.style.display = 'none';
      if (replayModal && replayModal.style.display !== 'none') {
        closeReplayModal();
      }
      if (viewportCard.classList.contains('is-fullscreen')) {
        btnFullscreen.click();
      }
    }
  });

  if (btnShortcuts) btnShortcuts.addEventListener('click', () => { if (shortcutsModal) shortcutsModal.style.display = 'flex'; });
  if (btnCloseModal) btnCloseModal.addEventListener('click', () => { if (shortcutsModal) shortcutsModal.style.display = 'none'; });
  if (shortcutsModal) {
    shortcutsModal.addEventListener('click', (e) => {
      if (e.target === shortcutsModal) shortcutsModal.style.display = 'none';
    });
  }

  // --- Instant Slow-Motion Replay Handlers ---
  let replaySessionCounter = 0;
  let frameRequestId = 0;
  let replayInfoAbort = null;
  let activeReplayBlobUrl = null;
  let replayPlaybackEpoch = 0;
  const replayCache = new Map();
  const REPLAY_CACHE_MAX = 16;
  const inFlightRequests = new Map(); // frameIdx -> { promise, abortController }
  let prefetchWorkerRunning = false;

  function isUrlInReplayCache(url) {
    for (const entry of replayCache.values()) {
      if (entry && entry.blobUrl === url) return true;
    }
    return false;
  }

  function putInReplayCache(idx, entry) {
    if (replayCache.has(idx)) return;
    if (replayCache.size >= REPLAY_CACHE_MAX) {
      const oldestKey = replayCache.keys().next().value;
      const oldEntry = replayCache.get(oldestKey);
      replayCache.delete(oldestKey);
      if (oldEntry && oldEntry.blobUrl !== activeReplayBlobUrl) {
        URL.revokeObjectURL(oldEntry.blobUrl);
      }
    }
    replayCache.set(idx, entry);
  }

  function clearReplayCache() {
    for (const entry of replayCache.values()) {
      if (entry && entry.blobUrl !== activeReplayBlobUrl) {
        URL.revokeObjectURL(entry.blobUrl);
      }
    }
    replayCache.clear();
  }

  function cancelAllInFlight() {
    frameRequestId++;
    for (const item of inFlightRequests.values()) {
      item.abortController.abort();
    }
    inFlightRequests.clear();
  }

  function updateReplayLabels(frameIdx, totalFrames, fps, durationSec) {
    const validFps = (typeof fps === 'number' && fps > 0) ? fps : 30;
    const deltaMs = (1000.0 / validFps).toFixed(1);
    if (replayFrameText) {
      replayFrameText.textContent = `Frame ${frameIdx + 1} / ${totalFrames} (Δ ${deltaMs}ms)`;
    }
    if (replayTimeText) {
      const curSec = (frameIdx / validFps).toFixed(3);
      const durSec = (typeof durationSec === 'number' && durationSec > 0) ? durationSec.toFixed(3) : (totalFrames / validFps).toFixed(3);
      replayTimeText.textContent = `${curSec}s / ${durSec}s (${validFps.toFixed(0)} FPS)`;
    }
  }

  async function fetchAndDecodeFrame(clipPath, frameIdx, signal) {
    if (signal && signal.aborted) {
      const err = new Error('Aborted');
      err.name = 'AbortError';
      throw err;
    }
    const url = `/api/clip/frame?path=${encodeURIComponent(clipPath)}&frame=${frameIdx}`;
    const res = await fetch(url, { signal });
    if (!res.ok) {
      throw new Error(`HTTP ${res.status}`);
    }
    const blob = await res.blob();
    if (signal && signal.aborted) {
      const err = new Error('Aborted');
      err.name = 'AbortError';
      throw err;
    }
    const blobUrl = URL.createObjectURL(blob);
    const img = new Image();
    img.src = blobUrl;
    try {
      if (img.decode) {
        await img.decode();
      } else {
        await new Promise((resolve, reject) => {
          img.onload = resolve;
          img.onerror = () => reject(new Error('Image decode failed'));
        });
      }
      if (signal && signal.aborted) {
        URL.revokeObjectURL(blobUrl);
        const err = new Error('Aborted');
        err.name = 'AbortError';
        throw err;
      }
    } catch (decodeErr) {
      URL.revokeObjectURL(blobUrl);
      throw decodeErr;
    }
    return { blobUrl, img };
  }

  function getOrFetchFrame(clipPath, frameIdx) {
    if (replayCache.has(frameIdx)) {
      return Promise.resolve(replayCache.get(frameIdx));
    }
    if (inFlightRequests.has(frameIdx)) {
      return inFlightRequests.get(frameIdx).promise;
    }

    const abortController = new AbortController();
    const promise = (async () => {
      try {
        const entry = await fetchAndDecodeFrame(clipPath, frameIdx, abortController.signal);
        putInReplayCache(frameIdx, entry);
        return entry;
      } finally {
        if (inFlightRequests.get(frameIdx)?.abortController === abortController) {
          inFlightRequests.delete(frameIdx);
        }
      }
    })();

    inFlightRequests.set(frameIdx, { promise, abortController });
    return promise;
  }

  function presentDecodedEntry(frameIdx, entry) {
    if (!currentReplayClip || !replayFrame) return;
    const oldUrl = activeReplayBlobUrl;
    activeReplayBlobUrl = entry.blobUrl;
    replayFrame.src = entry.blobUrl;
    currentReplayFrame = frameIdx;
    if (replayScrubber) {
      replayScrubber.value = frameIdx;
    }
    updateReplayLabels(frameIdx, currentReplayClip.total_frames, currentReplayClip.fps, currentReplayClip.duration_sec);
    if (oldUrl && oldUrl !== entry.blobUrl && !isUrlInReplayCache(oldUrl)) {
      URL.revokeObjectURL(oldUrl);
    }
  }

  async function presentFrame(targetFrame, requirePlaying) {
    const reqId = ++frameRequestId;
    const session = replaySessionCounter;
    if (!currentReplayClip) return false;
    const clip = currentReplayClip;
    const target = Math.max(0, Math.min(clip.total_frames - 1, targetFrame));

    try {
      const entry = await getOrFetchFrame(clip.path, target);
      if (reqId !== frameRequestId || session !== replaySessionCounter) {
        return false;
      }
      if (requirePlaying && !replayPlaying) {
        return false;
      }
      presentDecodedEntry(target, entry);
      pumpPrefetch();
      return true;
    } catch (err) {
      if (err.name === 'AbortError') return false;
      if (reqId !== frameRequestId || session !== replaySessionCounter) return false;
      console.error('Failed to load frame:', err);
      showToast(`Failed to load frame ${target + 1}: ${err.message || err}`);
      return false;
    }
  }

  function requestFramePresentation(targetFrame) {
    cancelAllInFlight();
    presentFrame(targetFrame, false);
  }

  function loadReplayFrame(idx) {
    requestFramePresentation(idx);
  }

  async function pumpPrefetch(step = 1) {
    if (prefetchWorkerRunning || !currentReplayClip || !replayPlaying) return;
    prefetchWorkerRunning = true;
    try {
      const clip = currentReplayClip;
      const session = replaySessionCounter;
      const PREFETCH_AHEAD = Math.max(4, step * 4);

      while (replayPlaying && session === replaySessionCounter && currentReplayClip === clip) {
        let targetIdx = -1;
        for (let i = 1; i <= PREFETCH_AHEAD; i++) {
          const idx = (currentReplayFrame + (i * step)) % clip.total_frames;
          if (!replayCache.has(idx) && !inFlightRequests.has(idx)) {
            targetIdx = idx;
            break;
          }
        }

        if (targetIdx === -1) {
          break;
        }

        try {
          await getOrFetchFrame(clip.path, targetIdx);
        } catch (e) {
          break;
        }
      }
    } finally {
      prefetchWorkerRunning = false;
    }
  }

  function scheduleNextPlaybackTick(delay) {
    if (!replayPlaying) return;
    clearTimeout(replayTimer);
    replayTimer = setTimeout(playbackTick, Math.max(0, delay));
  }

  async function playbackTick() {
    if (!replayPlaying || !currentReplayClip) return;
    const clip = currentReplayClip;
    const session = replaySessionCounter;
    const epoch = replayPlaybackEpoch;

    const clipFps = (clip.fps && clip.fps > 0) ? clip.fps : 30;
    const effectiveFps = Math.max(1, clipFps * replaySpeed);

    // Browser display refresh limit (e.g. 60 FPS) to maintain real time at 1.0x 120fps
    const maxDisplayFps = 60;
    let frameStep = 1;
    let targetIntervalMs;

    if (effectiveFps <= maxDisplayFps) {
      frameStep = 1;
      targetIntervalMs = 1000.0 / effectiveFps;
    } else {
      frameStep = Math.max(1, Math.round(effectiveFps / maxDisplayFps));
      targetIntervalMs = 1000.0 / (effectiveFps / frameStep);
    }

    let next = (currentReplayFrame + frameStep) % clip.total_frames;

    const startTime = performance.now();
    const ok = await presentFrame(next, true);
    if (!replayPlaying || session !== replaySessionCounter || epoch !== replayPlaybackEpoch) return;
    if (!ok) { pauseReplayPlayback(); return; }

    const elapsed = performance.now() - startTime;
    const remainingDelay = Math.max(0, targetIntervalMs - elapsed);

    pumpPrefetch(frameStep);
    scheduleNextPlaybackTick(remainingDelay);
  }

  function startReplayPlayback() {
    if (replayPlaying || !currentReplayClip) return;
    replayPlaying = true;
    if (btnReplayPlay) {
      btnReplayPlay.textContent = '⏸ 일시정지';
    }
    pumpPrefetch();
    scheduleNextPlaybackTick(0);
  }

  function pauseReplayPlayback() {
    replayPlaybackEpoch++;
    replayPlaying = false;
    clearTimeout(replayTimer);
    replayTimer = null;
    if (btnReplayPlay) {
      btnReplayPlay.textContent = '▶ 재생';
    }
  }

  function closeReplayModal() {
    replaySessionCounter++;
    pauseReplayPlayback();
    cancelAllInFlight();
    if (replayInfoAbort) {
      replayInfoAbort.abort();
      replayInfoAbort = null;
    }
    clearReplayCache();
    if (activeReplayBlobUrl) {
      URL.revokeObjectURL(activeReplayBlobUrl);
      activeReplayBlobUrl = null;
    }
    if (replayFrame) {
      replayFrame.removeAttribute('src');
    }
    if (replayCsvBtn) {
      replayCsvBtn.style.display = 'none';
      replayCsvBtn.removeAttribute('href');
    }
    currentReplayClip = null;
    if (replayModal) {
      replayModal.style.display = 'none';
    }
  }

  function openReplayModal(path, title, metaUrl) {
    if (!replayModal) return;
    const session = ++replaySessionCounter;
    pauseReplayPlayback();
    cancelAllInFlight();
    if (replayInfoAbort) {
      replayInfoAbort.abort();
      replayInfoAbort = null;
    }
    clearReplayCache();
    if (activeReplayBlobUrl) {
      URL.revokeObjectURL(activeReplayBlobUrl);
      activeReplayBlobUrl = null;
    }
    if (replayFrame) {
      replayFrame.removeAttribute('src');
    }
    currentReplayClip = null;

    if (replayModalTitle) replayModalTitle.textContent = `⚾ ${title || 'Instant Replay'}`;
    if (replayScrubber) {
      replayScrubber.disabled = true;
      replayScrubber.value = 0;
    }
    if (replayFrameText) replayFrameText.textContent = 'Loading...';
    if (replayTimeText) replayTimeText.textContent = '0.000s / 0.000s';
    if (replayCsvBtn) {
      if (metaUrl) {
        replayCsvBtn.href = metaUrl;
        replayCsvBtn.style.display = 'inline-flex';
        replayCsvBtn.title = `CSV 메타데이터 다운로드: ${metaUrl}`;
      } else {
        replayCsvBtn.style.display = 'none';
        replayCsvBtn.removeAttribute('href');
      }
    }
    replayModal.style.display = 'flex';

    replayInfoAbort = new AbortController();
    fetch(`/api/clip/info?path=${encodeURIComponent(path)}`, { signal: replayInfoAbort.signal })
      .then(async r => {
        if (!r.ok) {
          throw new Error(`HTTP ${r.status}`);
        }
        return r.json();
      })
      .then(info => {
        if (session !== replaySessionCounter) return;
        if (!info || !Number.isInteger(info.total_frames) || info.total_frames <= 0 ||
            !Number.isFinite(info.fps) || info.fps <= 0) {
          showToast('Cannot open video clip');
          closeReplayModal();
          return;
        }
        currentReplayClip = info;
        currentReplayFrame = 0;

        const csvUrl = info.metadata_url || metaUrl;
        if (replayCsvBtn) {
          if (csvUrl) {
            replayCsvBtn.href = csvUrl;
            replayCsvBtn.style.display = 'inline-flex';
            replayCsvBtn.title = `CSV 메타데이터 다운로드: ${csvUrl}`;
          } else {
            replayCsvBtn.style.display = 'none';
            replayCsvBtn.removeAttribute('href');
          }
        }

        if (replayScrubber) {
          replayScrubber.disabled = false;
          replayScrubber.min = 0;
          replayScrubber.max = info.total_frames - 1;
          replayScrubber.step = 1;
          replayScrubber.value = 0;
        }
        presentFrame(0, false).then(ok => {
          if (session === replaySessionCounter && ok) {
            startReplayPlayback();
          }
        }).catch(err => {
          if (session !== replaySessionCounter) return;
          console.error('Failed to present initial frame:', err);
        });
      })
      .catch(e => {
        if (e.name === 'AbortError') return;
        if (session !== replaySessionCounter) return;
        showToast(`Failed to open clip: ${e.message || e}`);
        closeReplayModal();
      });
  }

  if (btnReplayPlay) {
    btnReplayPlay.addEventListener('click', () => {
      if (replayPlaying) pauseReplayPlayback();
      else startReplayPlayback();
    });
  }

  if (replayScrubber) {
    replayScrubber.addEventListener('input', () => {
      pauseReplayPlayback();
      if (!currentReplayClip) return;
      const target = parseInt(replayScrubber.value, 10);
      if (Number.isFinite(target)) {
        requestFramePresentation(target);
      }
    });
  }

  if (btnStepPrev) {
    btnStepPrev.addEventListener('click', () => {
      pauseReplayPlayback();
      if (!currentReplayClip) return;
      const target = Math.max(0, currentReplayFrame - 1);
      requestFramePresentation(target);
    });
  }

  if (btnStepNext) {
    btnStepNext.addEventListener('click', () => {
      pauseReplayPlayback();
      if (!currentReplayClip) return;
      const target = Math.min(currentReplayClip.total_frames - 1, currentReplayFrame + 1);
      requestFramePresentation(target);
    });
  }

  document.querySelectorAll('.btn-speed').forEach(btn => {
    btn.addEventListener('click', () => {
      document.querySelectorAll('.btn-speed').forEach(b => b.classList.remove('active'));
      btn.classList.add('active');
      replaySpeed = parseFloat(btn.dataset.speed);
      showToast(`Slow-Mo Speed: ${replaySpeed}×`);
      if (replayPlaying) {
        pauseReplayPlayback();
        startReplayPlayback();
      }
    });
  });

  if (btnCloseReplay) {
    btnCloseReplay.addEventListener('click', () => {
      closeReplayModal();
    });
  }

  if (replayModal) {
    replayModal.addEventListener('click', (e) => {
      if (e.target === replayModal) {
        closeReplayModal();
      }
    });
  }
  // --- Zoom & Minimap Handlers ---
  zoomBtns.forEach(btn => {
    btn.addEventListener('click', () => {
      zoomBtns.forEach(b => b.classList.remove('active'));
      btn.classList.add('active');
      activeZoom = btn.dataset.zoom;
      postDisplay({ zoom_mode: activeZoom });
      updateMinimap();
    });
  });

  if (chkPeaking) {
    chkPeaking.addEventListener('change', () => postDisplay({ focus_peaking: chkPeaking.checked }));
  }
  if (selectPeakingColor) {
    selectPeakingColor.addEventListener('change', () => postDisplay({ peaking_color: selectPeakingColor.value }));
  }
  if (chkZebra) {
    chkZebra.addEventListener('change', () => postDisplay({ zebra_stripes: chkZebra.checked }));
  }
  if (selectGrid) {
    selectGrid.addEventListener('change', () => postDisplay({ grid_mode: selectGrid.value }));
  }
  if (chkFalseColor) {
    chkFalseColor.addEventListener('change', () => {
      postDisplay({ false_color: chkFalseColor.checked });
      showToast(`False Color: ${chkFalseColor.checked ? 'ON' : 'OFF'}`);
    });
  }
  if (selectViewFps) {
    selectViewFps.addEventListener('change', () => postDisplay({ fps_limit: parseInt(selectViewFps.value, 10) }));
  }

  // Viewport Click-to-Center Pan
  if (viewportWrap) {
    viewportWrap.addEventListener('click', (e) => {
      if (activeZoom === 'fit') return;
      const rect = viewportWrap.getBoundingClientRect();
      activeCenterX = (e.clientX - rect.left) / rect.width;
      activeCenterY = (e.clientY - rect.top) / rect.height;
      postDisplay({ center_x: activeCenterX, center_y: activeCenterY });
      updateMinimap();
    });
  }

  // --- Sliders & Controls Event Listeners ---
  if (rangeExp) {
    rangeExp.addEventListener('input', () => {
      const us = parseInt(rangeExp.value, 10);
      if (valExpUs) valExpUs.textContent = us;
      if (valExpMs) valExpMs.textContent = (us / 1000).toFixed(2) + ' ms';
      currentExposureUs = us;
      updateBlurCalculator();
      document.querySelectorAll('.btn-chip[data-exp]').forEach(b => {
        b.classList.toggle('active', parseInt(b.dataset.exp, 10) === us);
      });
      postControls({ exposure_us: us });
    });
  }

  document.querySelectorAll('.btn-chip[data-exp]').forEach(btn => {
    btn.addEventListener('click', () => {
      const us = parseInt(btn.dataset.exp, 10);
      rangeExp.value = us;
      rangeExp.dispatchEvent(new Event('input'));
    });
  });

  if (rangeGain) {
    rangeGain.addEventListener('input', () => {
      const g = parseFloat(rangeGain.value);
      if (valGain) valGain.textContent = g.toFixed(2);
      document.querySelectorAll('.btn-chip[data-gain]').forEach(b => {
        b.classList.toggle('active', Math.abs(parseFloat(b.dataset.gain) - g) < 0.1);
      });
      postControls({ gain: g });
    });
  }

  document.querySelectorAll('.btn-chip[data-gain]').forEach(btn => {
    btn.addEventListener('click', () => {
      const g = parseFloat(btn.dataset.gain);
      rangeGain.value = g;
      rangeGain.dispatchEvent(new Event('input'));
    });
  });

  document.querySelectorAll('input[name="ae-mode"]').forEach(r => {
    r.addEventListener('change', () => {
      const isAuto = r.value === 'auto';
      document.querySelectorAll('.seg-group input[name="ae-mode"]').forEach(inp => {
        inp.closest('.seg-btn').classList.toggle('active', inp.checked);
      });
      if (manualExpGroup) manualExpGroup.style.opacity = isAuto ? '0.4' : '1.0';
      if (manualGainGroup) manualGainGroup.style.opacity = isAuto ? '0.4' : '1.0';
      if (aeOptions) aeOptions.style.display = isAuto ? 'block' : 'none';
      postControls({ auto_exposure: isAuto });
    });
  });

  if (rangeAeMaxExp) {
    rangeAeMaxExp.addEventListener('input', () => {
      valAeMaxExp.textContent = rangeAeMaxExp.value + ' \u03bcs';
      postControls({ ae_max_exposure_us: parseInt(rangeAeMaxExp.value, 10) });
    });
  }
  if (rangeAeMaxGain) {
    rangeAeMaxGain.addEventListener('input', () => {
      valAeMaxGain.textContent = parseFloat(rangeAeMaxGain.value).toFixed(1) + '\u00d7';
      postControls({ ae_max_gain: parseFloat(rangeAeMaxGain.value) });
    });
  }
  if (chkAeLock) chkAeLock.addEventListener('change', () => postControls({ ae_lock: chkAeLock.checked }));
  if (rangeEv) {
    rangeEv.addEventListener('input', () => {
      const ev = parseFloat(rangeEv.value);
      valEv.textContent = (ev >= 0 ? '+' : '') + ev.toFixed(2) + ' EV';
      postControls({ ev_compensation: ev });
    });
  }
  if (selectAntibanding) selectAntibanding.addEventListener('change', () => postControls({ antibanding: selectAntibanding.value }));
  if (selectAwb) selectAwb.addEventListener('change', () => postControls({ awb_mode: selectAwb.value }));
  if (chkAwbLock) chkAwbLock.addEventListener('change', () => postControls({ awb_lock: chkAwbLock.checked }));
  if (rangeIspGain) {
    rangeIspGain.addEventListener('input', () => {
      valIspGain.textContent = parseFloat(rangeIspGain.value).toFixed(1) + '\u00d7';
      postControls({ isp_digital_gain: parseFloat(rangeIspGain.value) });
    });
  }
  if (selectDenoise) selectDenoise.addEventListener('change', () => postControls({ denoise_mode: selectDenoise.value }));
  if (selectEdge) selectEdge.addEventListener('change', () => postControls({ edge_mode: selectEdge.value }));

  // Presets load/save
  function loadPresetList() {
    if (!presetSelect) return;
    fetch('/api/presets')
      .then(r => r.json())
      .then(list => {
        presetSelect.innerHTML = '<option value="">Preset...</option>';
        list.forEach(name => {
          const opt = document.createElement('option');
          opt.value = name;
          opt.textContent = name;
          presetSelect.appendChild(opt);
        });
      })
      .catch(console.error);
  }

  if (btnLoadPreset) {
    btnLoadPreset.addEventListener('click', () => {
      const name = presetSelect ? presetSelect.value : '';
      if (!name) return;
      applyPreset(name);
    });
  }

  if (btnSavePreset) {
    btnSavePreset.addEventListener('click', () => {
      const name = prompt('Preset name:');
      if (!name) return;
      fetch('/api/state')
        .then(r => r.json())
        .then(state => {
          const payload = {
            description: 'User saved preset',
            sensor_mode: state.capabilities?.active_sensor_mode !== undefined ? state.capabilities.active_sensor_mode : activeSensorMode,
            controls: state.controls,
            display: state.display_settings
          };
          return fetch(`/api/presets/${encodeURIComponent(name)}`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload)
          });
        })
        .then(async r => {
          const data = await r.json().catch(() => ({}));
          if (!r.ok || data.status === 'error') {
            throw new Error(data.error || 'Server rejected preset name');
          }
          showToast(`Preset saved: ${name}`);
          loadPresetList();
        })
        .catch(e => showToast(`Failed to save preset: ${e.message || e}`));
    });
  }

  // --- Initial Launch ---
  fetch('/api/state').then(r => r.json()).then(applyStateToUI).catch(console.error);
  fetch('/api/pitch/status').then(r => r.json()).then(updatePitchStatusUI).catch(() => {});
  setupSSE();
  updateBlurCalculator();
  loadPresetList();
  loadMediaGallery();
  if (window.location.hash) {
    const initTab = document.querySelector(`.itab-btn[data-tab="${window.location.hash.substring(1)}"]`);
    if (initTab) initTab.click();
  }
})();
