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
  const hudZoom = document.getElementById('hud-zoom');
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
  const replayVideo = document.getElementById('replay-video');
  const replayModalTitle = document.getElementById('replay-modal-title');
  const btnCloseReplay = document.getElementById('btn-close-replay');
  const btnStepPrev = document.getElementById('btn-step-prev');
  const btnStepNext = document.getElementById('btn-step-next');

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
  // --- Stream Auto-Reconnect ---
  let isFallbackLoopRunning = false;
  function startFrameLoopFallback() {
    if (isFallbackLoopRunning) return;
    isFallbackLoopRunning = true;
    let busy = false;
    async function step() {
      if (!busy) {
        busy = true;
        try {
          const res = await fetch('/api/frame.jpg?t=' + Date.now());
          if (res.ok) {
            const blob = await res.blob();
            const url = URL.createObjectURL(blob);
            const oldUrl = streamImg.src;
            streamImg.src = url;
            if (oldUrl && oldUrl.startsWith('blob:')) {
              setTimeout(() => URL.revokeObjectURL(oldUrl), 800);
            }
          }
        } catch (e) {}
        busy = false;
      }
      requestAnimationFrame(step);
    }
    requestAnimationFrame(step);
  }

  function reloadStream() {
    if (!streamImg) return;
    streamImg.src = '/stream.mjpg?t=' + Date.now();
  }

  // If native MJPEG doesn't paint within 1.2s, auto-switch to frame loop
  setTimeout(() => {
    if (streamImg && (streamImg.naturalWidth === 0 || !streamImg.complete)) {
      console.warn('Native MJPEG stream not painting, switching to frame loop fallback');
      startFrameLoopFallback();
    }
  }, 1200);

  if (streamImg) {
    streamImg.onerror = () => {
      startFrameLoopFallback();
    };
  }


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

  // --- Sensor Mode Switching (4K 30P / 1080 60P / 990 120P) ---
  sensorModeBtns.forEach(btn => {
    btn.addEventListener('click', () => {
      const mode = parseInt(btn.dataset.sensorMode, 10);
      showToast(`Switching to sensor mode ${mode}...`);
      fetch('/api/sensor_mode', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ mode: mode })
      })
      .then(r => r.json())
      .then(d => {
        if (d.status === 'ok') {
          sensorModeBtns.forEach(b => b.classList.toggle('active', parseInt(b.dataset.sensorMode, 10) === mode));
          hudRes.textContent = `${d.width}×${d.height}`;
          hudSensor.textContent = `${d.target_fps.toFixed(0)} FPS`;
          showToast(`Active: ${d.width}×${d.height} @ ${d.target_fps} FPS`);
          // Re-fetch state for new exposure limits
          fetch('/api/state').then(r => r.json()).then(applyStateToUI);
          reloadStream();
        } else {
          showToast(`Mode switch error: ${d.error || 'Failed'}`);
        }
      })
      .catch(e => showToast(`Switch failed: ${e}`));
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
            isRecording = true;
            btnRecord.classList.add('is-recording');
            recLabel.textContent = 'STOP';
            recTimer.style.display = 'inline';
            if (osdRecBadge) osdRecBadge.style.display = 'block';
            recordSeconds = 0;
            updateRecordTimerDisplay();
            recordTimerInterval = setInterval(() => {
              recordSeconds++;
              updateRecordTimerDisplay();
            }, 1000);
            showToast(`● Recording started: ${d.path}`);
          } else {
            showToast(`Record start failed: ${d.error || 'Error'}`);
          }
        })
        .catch(e => showToast(`Record error: ${e}`));
    } else {
      fetch('/api/record/stop', { method: 'POST' })
        .then(r => r.json())
        .then(d => {
          isRecording = false;
          btnRecord.classList.remove('is-recording');
          recLabel.textContent = 'REC';
          recTimer.style.display = 'none';
          if (osdRecBadge) osdRecBadge.style.display = 'none';
          clearInterval(recordTimerInterval);
          showToast(`■ Video saved: ${d.path} (${d.frames} frames)`);
          loadMediaGallery();
        })
        .catch(e => showToast(`Record error: ${e}`));
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

  // --- Pitch Auto-Clip Handlers ---
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
    if (!chkPitchAutoClip || !chkPitchAutoClip.checked) return;
    fetch('/api/pitch/status')
      .then(r => r.json())
      .then(d => {
        if (valPitchMotion) valPitchMotion.textContent = `${d.motion_level} %`;
        if (pitchMotionFill) pitchMotionFill.style.width = `${Math.min(100, d.motion_level)}%`;
        if (valPitchClipsCount) valPitchClipsCount.textContent = d.total_clips;
        if (pitchClipStatusBadge) {
          if (d.is_clipping) {
            pitchClipStatusBadge.textContent = '● 클립 녹화 중 (CLIPPING)';
            pitchClipStatusBadge.className = 'pill pill-danger';
          } else {
            pitchClipStatusBadge.textContent = '대기 중 (IDLE)';
            pitchClipStatusBadge.className = 'pill pill-muted';
          }
        }
      })
      .catch(() => {});
  }, 250);

  // --- Telemetry Drawing (Sparkline & Histogram) ---
  function drawSparkline(val) {
    if (!sparklineCtx) return;
    if (val > 0) {
      fpsHistory.push(val);
      if (fpsHistory.length > 60) fpsHistory.shift();
    }
    if (fpsHistory.length < 2) return;

    const w = sparklineCanvas.width;
    const h = sparklineCanvas.height;
    sparklineCtx.fillStyle = '#edf1f5';
    sparklineCtx.fillRect(0, 0, w, h);

    // Target FPS horizontal guideline
    const targetY = h - (120.0 / 140.0) * h;
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
      const y = h - Math.min(1.0, Math.max(0.0, fpsHistory[i] / 140.0)) * (h - 4);
      if (i === 0) sparklineCtx.moveTo(x, y);
      else sparklineCtx.lineTo(x, y);
    }
    sparklineCtx.strokeStyle = '#0284c7';
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
      if (caps.target_fps && hudSensor) hudSensor.textContent = `${caps.target_fps.toFixed(0)} FPS`;
      if (caps.active_sensor_mode !== undefined) {
        sensorModeBtns.forEach(b => b.classList.toggle('active', parseInt(b.dataset.sensorMode, 10) === caps.active_sensor_mode));
      }
      if (caps.max_exposure_us && rangeExp) {
        rangeExp.max = caps.max_exposure_us;
      }
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
      items.reverse().slice(0, 16).forEach(item => {
        const card = document.createElement('div');
        card.className = 'gallery-card';
        const isVid = item.type === 'video';
        card.innerHTML = `
          <a href="${item.url}" class="${isVid ? 'replay-link' : ''}" data-url="${item.url}" data-name="${item.filename}" ${isVid ? '' : 'target="_blank"'}>
            ${isVid ? `<div style="height:90px;background:#0f172a;color:#fff;display:flex;align-items:center;justify-content:center;font-weight:700;font-size:11px;cursor:pointer;">▶ SLOW-MO REPLAY</div>` : `<img src="${item.url}" alt="${item.filename}" loading="lazy">`}
          </a>
          <div class="gallery-meta">
            <span class="${isVid ? 'replay-link' : ''}" data-url="${item.url}" data-name="${item.filename}" style="${isVid ? 'cursor:pointer;color:var(--accent);font-weight:700;' : ''}">${item.filename.length > 20 ? item.filename.substring(0, 18) + '..' : item.filename}</span>
            <a href="${item.url}" download style="color:var(--accent);text-decoration:none;font-weight:700;">Save</a>
          </div>
        `;
        galleryContainer.appendChild(card);
      });

      document.querySelectorAll('.replay-link').forEach(link => {
        link.addEventListener('click', (e) => {
          e.preventDefault();
          openReplayModal(link.dataset.url, link.dataset.name);
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
  function openReplayModal(url, title) {
    if (!replayModal || !replayVideo) return;
    if (replayModalTitle) replayModalTitle.textContent = `⚾ ${title || 'Instant Replay'}`;
    replayVideo.src = url;
    replayVideo.playbackRate = 0.25; // default slow-motion
    replayModal.style.display = 'flex';
    replayVideo.play().catch(() => {});
    document.querySelectorAll('.btn-speed').forEach(b => b.classList.toggle('active', b.dataset.speed === '0.25'));
  }

  if (btnCloseReplay) {
    btnCloseReplay.addEventListener('click', () => {
      replayVideo.pause();
      replayVideo.src = '';
      replayModal.style.display = 'none';
    });
  }

  if (replayModal) {
    replayModal.addEventListener('click', (e) => {
      if (e.target === replayModal) {
        replayVideo.pause();
        replayVideo.src = '';
        replayModal.style.display = 'none';
      }
    });
  }

  document.querySelectorAll('.btn-speed').forEach(btn => {
    btn.addEventListener('click', () => {
      document.querySelectorAll('.btn-speed').forEach(b => b.classList.remove('active'));
      btn.classList.add('active');
      const rate = parseFloat(btn.dataset.speed);
      if (replayVideo) replayVideo.playbackRate = rate;
      showToast(`Replay Speed: ${rate}×`);
    });
  });

  if (btnStepPrev) {
    btnStepPrev.addEventListener('click', () => {
      if (replayVideo) {
        replayVideo.pause();
        replayVideo.currentTime = Math.max(0, replayVideo.currentTime - (1.0 / 30.0));
      }
    });
  }
  if (btnStepNext) {
    btnStepNext.addEventListener('click', () => {
      if (replayVideo) {
        replayVideo.pause();
        replayVideo.currentTime = Math.min(replayVideo.duration, replayVideo.currentTime + (1.0 / 30.0));
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
      const name = presetSelect.value;
      if (!name) return;
      fetch(`/api/presets/${encodeURIComponent(name)}`)
        .then(r => r.json())
        .then(data => {
          applyStateToUI(data);
          if (data.controls) postControls(data.controls);
          if (data.display) postDisplay(data.display);
          showToast(`Loaded preset: ${name}`);
        })
        .catch(e => showToast('Failed to load preset: ' + e));
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
  setupSSE();
  updateBlurCalculator();
  loadPresetList();
  loadMediaGallery();

  if (window.location.hash) {
    const initTab = document.querySelector(`.itab-btn[data-tab="${window.location.hash.substring(1)}"]`);
    if (initTab) initTab.click();
  }
})();
