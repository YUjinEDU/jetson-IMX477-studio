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
  const metricHostFps = document.getElementById('metric-host-fps');
  const metricGaps = document.getElementById('metric-gaps');
  const metricBadIv = document.getElementById('metric-bad-iv');
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
  const aeRadios = document.querySelectorAll('input[name="ae-mode"]');
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
  const histCtx = histCanvas.getContext('2d');
  const sparklineCanvas = document.getElementById('fps-sparkline-canvas');
  const sparklineCtx = sparklineCanvas.getContext('2d');
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

  // Presets & Gallery
  const presetSelect = document.getElementById('preset-select');
  const btnLoadPreset = document.getElementById('btn-load-preset');
  const btnSavePreset = document.getElementById('btn-save-preset');
  const btnSnapshot = document.getElementById('btn-snapshot');
  const galleryContainer = document.getElementById('gallery-container');
  const btnRefreshGallery = document.getElementById('btn-refresh-gallery');

  // Modal
  const shortcutsModal = document.getElementById('shortcuts-modal');
  const btnShortcuts = document.getElementById('btn-shortcuts');
  const btnCloseModal = document.getElementById('btn-close-modal');

  // State
  let currentExposureUs = 4000;
  let isDraggingSlider = false;
  let postControlsTimer = null;
  let postDisplayTimer = null;
  let peakFocus = 10.0;
  let activeZoom = 'fit';
  let activeCenterX = 0.5;
  let activeCenterY = 0.5;
  const fpsHistory = []; // rolling 60 samples

  function showToast(msg) {
    toast.textContent = msg;
    toast.classList.add('show');
    setTimeout(() => toast.classList.remove('show'), 3500);
  }

  // --- API Handlers ---
  function postControls(partial) {
    clearTimeout(postControlsTimer);
    postControlsTimer = setTimeout(() => {
      fetch('/api/controls', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(partial)
      }).catch(err => console.error('Failed to post controls:', err));
    }, 40);
  }

  function postDisplay(partial) {
    clearTimeout(postDisplayTimer);
    postDisplayTimer = setTimeout(() => {
      fetch('/api/display', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(partial)
      }).catch(err => console.error('Failed to post display:', err));
    }, 40);
  }

  // --- Tabs Navigation ---
  document.querySelectorAll('.tab-btn').forEach(btn => {
    btn.addEventListener('click', () => {
      document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
      document.querySelectorAll('.tab-pane').forEach(p => p.classList.remove('active'));
      btn.classList.add('active');
      const target = document.getElementById(btn.dataset.tab);
      if (target) target.classList.add('active');

      if (btn.dataset.tab === 'tab-gallery') {
        loadSnapshotGallery();
      }
    });
  });

  // --- Motion Blur Calculator ---
  function updateBlurCalculator() {
    const kmh = parseFloat(inputBallSpeed.value) || 145;
    const fovM = parseFloat(inputFovWidth.value) || 3.0;
    const speedMs = kmh / 3.6;
    calcSpeedMs.textContent = speedMs.toFixed(1) + ' m/s';

    const expSec = currentExposureUs / 1e6;
    const blurMeters = speedMs * expSec;
    const blurCm = blurMeters * 100;
    const blurPx = (blurMeters / fovM) * 1332;

    calcBlurCm.textContent = blurCm.toFixed(1) + ' cm';
    calcBlurCmBadge.textContent = blurCm.toFixed(1) + ' cm';
    calcBlurPx.textContent = Math.round(blurPx) + ' px';

    // Scale visual streak line on screen
    const maxTrackWidthPx = 180;
    const scaledWidth = Math.min(maxTrackWidthPx, Math.max(10, (blurCm / 20.0) * maxTrackWidthPx));
    blurStreakLine.style.width = scaledWidth + 'px';

    // Shutter recommendation
    if (blurCm <= 4.0) {
      calcBlurRec.textContent = 'Excellent (<= 4cm streak)';
      calcBlurRec.style.color = 'var(--success)';
    } else if (blurCm <= 8.0) {
      calcBlurRec.textContent = 'Moderate streak (<= 8cm)';
      calcBlurRec.style.color = 'var(--warning)';
    } else {
      calcBlurRec.textContent = 'High blur! Recommend <= 1.0ms';
      calcBlurRec.style.color = 'var(--error)';
    }
  }

  document.querySelectorAll('.btn-chip[data-speed]').forEach(btn => {
    btn.addEventListener('click', () => {
      document.querySelectorAll('.btn-chip[data-speed]').forEach(b => b.classList.remove('active'));
      btn.classList.add('active');
      inputBallSpeed.value = btn.dataset.speed;
      updateBlurCalculator();
    });
  });

  // --- FPS Sparkline Drawing ---
  function drawSparkline(val) {
    if (val > 0) {
      fpsHistory.push(val);
      if (fpsHistory.length > 60) fpsHistory.shift();
    }
    if (fpsHistory.length < 2) return;

    const w = sparklineCanvas.width;
    const h = sparklineCanvas.height;
    sparklineCtx.clearRect(0, 0, w, h);

    // Target 120 FPS dashed horizontal guideline
    const targetY = h - (120.0 / 140.0) * h;
    sparklineCtx.strokeStyle = 'rgba(16, 185, 129, 0.35)';
    sparklineCtx.lineWidth = 1;
    sparklineCtx.setLineDash([4, 4]);
    sparklineCtx.beginPath();
    sparklineCtx.moveTo(0, targetY);
    sparklineCtx.lineTo(w, targetY);
    sparklineCtx.stroke();
    sparklineCtx.setLineDash([]);

    // FPS path
    sparklineCtx.beginPath();
    const step = w / 60;
    const offset = 60 - fpsHistory.length;
    for (let i = 0; i < fpsHistory.length; i++) {
      const x = (offset + i) * step;
      const y = h - Math.min(1.0, Math.max(0.0, fpsHistory[i] / 140.0)) * (h - 4);
      if (i === 0) sparklineCtx.moveTo(x, y);
      else sparklineCtx.lineTo(x, y);
    }

    sparklineCtx.strokeStyle = '#3b82f6';
    sparklineCtx.lineWidth = 2;
    sparklineCtx.stroke();

    sparklineVal.textContent = (val > 0 ? val.toFixed(1) : '--') + ' fps';
  }

  // --- Histogram Drawing ---
  function drawHistogram(bins) {
    if (!bins || bins.length !== 64) return;
    const w = histCanvas.width;
    const h = histCanvas.height;
    histCtx.clearRect(0, 0, w, h);

    const maxVal = Math.max(...bins, 1);
    const barW = w / 64;

    for (let i = 0; i < 64; i++) {
      const barH = (bins[i] / maxVal) * (h - 2);
      const x = i * barW;
      const y = h - barH;
      const intensity = Math.floor((i / 64) * 200) + 55;
      histCtx.fillStyle = `rgb(${intensity}, ${intensity}, ${intensity})`;
      histCtx.fillRect(x, y, barW - 0.5, barH);
    }
  }

  // --- Minimap Navigator Update ---
  function updateMinimap() {
    if (activeZoom === 'fit') {
      minimapBox.style.display = 'none';
      hudZoom.textContent = 'ZOOM: FIT';
      return;
    }

    minimapBox.style.display = 'block';
    hudZoom.textContent = `ZOOM: ${activeZoom.toUpperCase()} (${Math.round(activeCenterX*100)}%, ${Math.round(activeCenterY*100)}%)`;

    const cropW = (activeZoom === '1x') ? (960 / 1332) : (480 / 1332);
    const cropH = (activeZoom === '1x') ? (714 / 990) : (356 / 990);

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

    if (ctrl) {
      if (ctrl.auto_exposure !== undefined) {
        const radio = document.querySelector(`input[name="ae-mode"][value="${ctrl.auto_exposure ? 'auto' : 'manual'}"]`);
        if (radio) radio.checked = true;
        manualExpGroup.style.opacity = ctrl.auto_exposure ? '0.4' : '1.0';
        manualGainGroup.style.opacity = ctrl.auto_exposure ? '0.4' : '1.0';
        aeOptions.style.display = ctrl.auto_exposure ? 'block' : 'none';
      }
      if (ctrl.exposure_us !== undefined) {
        rangeExp.value = ctrl.exposure_us;
        valExpUs.textContent = ctrl.exposure_us + ' \u03bcs';
        valExpMs.textContent = '(' + (ctrl.exposure_us / 1000).toFixed(2) + ' ms)';
        currentExposureUs = ctrl.exposure_us;
        updateBlurCalculator();
        document.querySelectorAll('.btn-chip[data-exp]').forEach(b => {
          b.classList.toggle('active', parseInt(b.dataset.exp, 10) === ctrl.exposure_us);
        });
      }
      if (ctrl.gain !== undefined) {
        rangeGain.value = ctrl.gain;
        valGain.textContent = ctrl.gain.toFixed(2) + '\u00d7';
        document.querySelectorAll('.btn-chip[data-gain]').forEach(b => {
          b.classList.toggle('active', Math.abs(parseFloat(b.dataset.gain) - ctrl.gain) < 0.1);
        });
      }
      if (ctrl.ae_max_exposure_us !== undefined) {
        rangeAeMaxExp.value = ctrl.ae_max_exposure_us;
        valAeMaxExp.textContent = ctrl.ae_max_exposure_us + ' \u03bcs';
      }
      if (ctrl.ae_max_gain !== undefined) {
        rangeAeMaxGain.value = ctrl.ae_max_gain;
        valAeMaxGain.textContent = parseFloat(ctrl.ae_max_gain).toFixed(1) + '\u00d7';
      }
      if (ctrl.ae_lock !== undefined) chkAeLock.checked = ctrl.ae_lock;
      if (ctrl.ev_compensation !== undefined) {
        rangeEv.value = ctrl.ev_compensation;
        valEv.textContent = (ctrl.ev_compensation > 0 ? '+' : '') + parseFloat(ctrl.ev_compensation).toFixed(1) + ' EV';
      }
      if (ctrl.antibanding !== undefined) selectAntibanding.value = ctrl.antibanding;
      if (ctrl.awb_mode !== undefined) selectAwb.value = ctrl.awb_mode;
      if (ctrl.awb_lock !== undefined) chkAwbLock.checked = ctrl.awb_lock;
      if (ctrl.isp_digital_gain !== undefined) {
        rangeIspGain.value = ctrl.isp_digital_gain;
        valIspGain.textContent = parseFloat(ctrl.isp_digital_gain).toFixed(1) + '\u00d7';
      }
      if (ctrl.denoise_mode !== undefined) selectDenoise.value = ctrl.denoise_mode;
      if (ctrl.edge_mode !== undefined) selectEdge.value = ctrl.edge_mode;
    }

    if (ds) {
      if (ds.zoom_mode !== undefined) {
        activeZoom = ds.zoom_mode;
        zoomBtns.forEach(b => b.classList.toggle('active', b.dataset.zoom === ds.zoom_mode));
      }
      if (ds.center_x !== undefined) activeCenterX = ds.center_x;
      if (ds.center_y !== undefined) activeCenterY = ds.center_y;
      updateMinimap();

      if (ds.focus_peaking !== undefined) chkPeaking.checked = ds.focus_peaking;
      if (ds.peaking_color !== undefined) selectPeakingColor.value = ds.peaking_color;
      if (ds.zebra_stripes !== undefined) chkZebra.checked = ds.zebra_stripes;
      if (ds.grid_mode !== undefined) selectGrid.value = ds.grid_mode;
      if (ds.fps_limit !== undefined) selectViewFps.value = ds.fps_limit;
    }
  }

  // --- SSE Event Stream ---
  function setupSSE() {
    const es = new EventSource('/events');

    es.onopen = () => {
      connPill.className = 'status-indicator connected';
      connPill.title = 'Connected to 120fps ISP service';
    };

    es.onerror = () => {
      connPill.className = 'status-indicator error';
      connPill.title = 'Reconnecting...';
    };

    es.onmessage = (e) => {
      try {
        const data = JSON.parse(e.data);
        const cm = data.camera_metrics;
        const dm = data.display_metrics;
        const ctrl = data.controls;

        metricSensorFps.textContent = cm.sensor_fps > 0 ? cm.sensor_fps.toFixed(1) : '--';
        metricHostFps.textContent = cm.host_fps > 0 ? cm.host_fps.toFixed(1) : '--';
        hudFps.textContent = (dm.display_fps > 0 ? dm.display_fps.toFixed(0) : '--') + ' FPS VIEW';
        hudSensor.textContent = (cm.sensor_fps > 0 ? cm.sensor_fps.toFixed(1) : '120.0') + ' FPS SENSOR';

        metricGaps.textContent = cm.sequence_gaps;
        metricGaps.className = 'kpi-val ' + (cm.sequence_gaps === 0 ? 'success' : 'warning');

        metricBadIv.textContent = `${cm.bad_intervals} anomalies (${cm.min_interval_ms.toFixed(1)}..${cm.max_interval_ms.toFixed(1)}ms)`;

        metricTotalFrames.textContent = cm.total_frames.toLocaleString();
        const uptimeSec = Math.floor((cm.uptime_ms || 0) / 1000);
        const m = Math.floor(uptimeSec / 60);
        const s = uptimeSec % 60;
        metricUptime.textContent = `${m}m ${s}s up`;

        valActualCct.textContent = (cm.actual_cct || '--') + ' K';

        currentExposureUs = ctrl.auto_exposure ? cm.actual_exposure_us : ctrl.exposure_us;
        updateBlurCalculator();

        if (dm.focus_score !== undefined) {
          focusScoreVal.textContent = Math.round(dm.focus_score);
          peakFocus = Math.max(peakFocus, dm.focus_score);
          const pct = Math.min(100, Math.max(0, (dm.focus_score / peakFocus) * 100));
          focusMeterFill.style.width = pct.toFixed(1) + '%';
          focusMeterPeak.style.left = pct.toFixed(1) + '%';
        }

        if (dm.histogram_64) {
          drawHistogram(dm.histogram_64);
        }

        if (cm.sensor_fps > 0) {
          drawSparkline(cm.sensor_fps);
        }

        if (!isDraggingSlider && !ctrl.auto_exposure) {
          rangeExp.value = ctrl.exposure_us;
          valExpUs.textContent = ctrl.exposure_us + ' \u03bcs';
          valExpMs.textContent = '(' + (ctrl.exposure_us / 1000).toFixed(2) + ' ms)';
          rangeGain.value = ctrl.gain;
          valGain.textContent = ctrl.gain.toFixed(2) + '\u00d7';
        }
      } catch (err) {
        console.error('SSE parse error:', err);
      }
    };
  }

  // --- Snapshot Trigger ---
  function triggerSnapshot() {
    btnSnapshot.disabled = true;
    btnSnapshot.textContent = '\u23F3 Snapping...';

    // Flash animation
    shutterFlash.classList.remove('flash');
    void shutterFlash.offsetWidth; // re-flow
    shutterFlash.classList.add('flash');

    fetch('/api/snapshot', { method: 'POST' })
      .then(r => r.json())
      .then(d => {
        if (d.status === 'ok') {
          showToast(`Snapshot saved: ${d.path}`);
          loadSnapshotGallery();
        } else {
          showToast(`Snapshot failed: ${d.error || 'Unknown error'}`);
        }
      })
      .catch(e => showToast('Error: ' + e))
      .finally(() => {
        btnSnapshot.disabled = false;
        btnSnapshot.textContent = '\uD83D\uDCF7 Snap (Space)';
      });
  }

  btnSnapshot.addEventListener('click', triggerSnapshot);

  // --- Fullscreen Toggle ---
  btnFullscreen.addEventListener('click', () => {
    viewportCard.classList.toggle('is-fullscreen');
    const isFs = viewportCard.classList.contains('is-fullscreen');
    btnFullscreen.innerHTML = isFs ? '&#x2715; Exit FS' : '&#x26F6; Fullscreen';
  });

  // --- Gallery Loader ---
  function loadSnapshotGallery() {
    fetch('/api/snapshots')
      .then(r => r.json())
      .then(list => {
        if (!list || list.length === 0) {
          galleryContainer.innerHTML = '<p class="gallery-empty">No snapshots captured yet. Press Snap or Space to capture.</p>';
          return;
        }

        galleryContainer.innerHTML = '';
        list.slice(-12).reverse().forEach(item => {
          const card = document.createElement('div');
          card.className = 'gallery-card';
          card.innerHTML = `
            <a href="${item.url}" target="_blank" title="Open full 1332x990 JPEG">
              <img src="${item.url}" alt="${item.filename}" loading="lazy">
            </a>
            <div class="gallery-meta">
              <span>${item.filename.replace('snapshot_', '').replace('.jpg', '')}</span>
              <a href="${item.url}" download class="gallery-link" title="Download">&#x2193; Save</a>
            </div>
          `;
          galleryContainer.appendChild(card);
        });
      })
      .catch(console.error);
  }

  btnRefreshGallery.addEventListener('click', loadSnapshotGallery);

  // --- Keyboard Shortcuts Handler ---
  window.addEventListener('keydown', (e) => {
    // Avoid capturing inputs
    if (['INPUT', 'SELECT', 'TEXTAREA'].includes(e.target.tagName)) return;

    if (e.code === 'Space') {
      e.preventDefault();
      triggerSnapshot();
    } else if (e.key === 'f' || e.key === 'F') {
      btnFullscreen.click();
    } else if (e.key === 'p' || e.key === 'P') {
      chkPeaking.checked = !chkPeaking.checked;
      postDisplay({ focus_peaking: chkPeaking.checked });
      showToast(`Peaking: ${chkPeaking.checked ? 'ON' : 'OFF'}`);
    } else if (e.key === 'z' || e.key === 'Z') {
      const modes = ['fit', '1x', '2x'];
      const next = modes[(modes.indexOf(activeZoom) + 1) % modes.length];
      document.querySelector(`.btn-tool[data-zoom="${next}"]`).click();
    } else if (e.key === 'm' || e.key === 'M') {
      const curMode = document.querySelector('input[name="ae-mode"]:checked').value;
      const nextRadio = document.querySelector(`input[name="ae-mode"][value="${curMode === 'manual' ? 'auto' : 'manual'}"]`);
      nextRadio.checked = true;
      nextRadio.dispatchEvent(new Event('change'));
    } else if (e.key === '1') {
      document.querySelector('.btn-chip[data-exp="500"]').click();
    } else if (e.key === '2') {
      document.querySelector('.btn-chip[data-exp="1000"]').click();
    } else if (e.key === '3') {
      document.querySelector('.btn-chip[data-exp="2000"]').click();
    } else if (e.key === '4') {
      document.querySelector('.btn-chip[data-exp="4000"]').click();
    } else if (e.key === '?' || (e.key === '/' && e.shiftKey)) {
      shortcutsModal.style.display = shortcutsModal.style.display === 'none' ? 'flex' : 'none';
    } else if (e.key === 'Escape') {
      shortcutsModal.style.display = 'none';
      if (viewportCard.classList.contains('is-fullscreen')) {
        btnFullscreen.click();
      }
    }
  });

  btnShortcuts.addEventListener('click', () => { shortcutsModal.style.display = 'flex'; });
  btnCloseModal.addEventListener('click', () => { shortcutsModal.style.display = 'none'; });
  shortcutsModal.addEventListener('click', (e) => {
    if (e.target === shortcutsModal) shortcutsModal.style.display = 'none';
  });

  // --- Sliders & Controls ---
  rangeExp.addEventListener('input', () => {
    isDraggingSlider = true;
    const us = parseInt(rangeExp.value, 10);
    valExpUs.textContent = us + ' \u03bcs';
    valExpMs.textContent = '(' + (us / 1000).toFixed(2) + ' ms)';
    currentExposureUs = us;
    updateBlurCalculator();
    document.querySelectorAll('.btn-chip[data-exp]').forEach(b => {
      b.classList.toggle('active', parseInt(b.dataset.exp, 10) === us);
    });
    postControls({ exposure_us: us });
  });

  rangeExp.addEventListener('change', () => { isDraggingSlider = false; });

  document.querySelectorAll('.btn-chip[data-exp]').forEach(btn => {
    btn.addEventListener('click', () => {
      const us = parseInt(btn.dataset.exp, 10);
      rangeExp.value = us;
      valExpUs.textContent = us + ' \u03bcs';
      valExpMs.textContent = '(' + (us / 1000).toFixed(2) + ' ms)';
      currentExposureUs = us;
      updateBlurCalculator();
      document.querySelectorAll('.btn-chip[data-exp]').forEach(b => b.classList.remove('active'));
      btn.classList.add('active');
      postControls({ exposure_us: us });
    });
  });

  rangeGain.addEventListener('input', () => {
    isDraggingSlider = true;
    const g = parseFloat(rangeGain.value);
    valGain.textContent = g.toFixed(2) + '\u00d7';
    document.querySelectorAll('.btn-chip[data-gain]').forEach(b => {
      b.classList.toggle('active', Math.abs(parseFloat(b.dataset.gain) - g) < 0.1);
    });
    postControls({ gain: g });
  });

  rangeGain.addEventListener('change', () => { isDraggingSlider = false; });

  document.querySelectorAll('.btn-chip[data-gain]').forEach(btn => {
    btn.addEventListener('click', () => {
      const g = parseFloat(btn.dataset.gain);
      rangeGain.value = g;
      valGain.textContent = g.toFixed(2) + '\u00d7';
      document.querySelectorAll('.btn-chip[data-gain]').forEach(b => b.classList.remove('active'));
      btn.classList.add('active');
      postControls({ gain: g });
    });
  });

  aeRadios.forEach(radio => {
    radio.addEventListener('change', () => {
      const isAuto = radio.value === 'auto';
      manualExpGroup.style.opacity = isAuto ? '0.4' : '1.0';
      manualGainGroup.style.opacity = isAuto ? '0.4' : '1.0';
      aeOptions.style.display = isAuto ? 'block' : 'none';
      postControls({ auto_exposure: isAuto });
    });
  });

  rangeAeMaxExp.addEventListener('input', () => {
    valAeMaxExp.textContent = rangeAeMaxExp.value + ' \u03bcs';
    postControls({ ae_max_exposure_us: parseInt(rangeAeMaxExp.value, 10) });
  });

  rangeAeMaxGain.addEventListener('input', () => {
    valAeMaxGain.textContent = parseFloat(rangeAeMaxGain.value).toFixed(1) + '\u00d7';
    postControls({ ae_max_gain: parseFloat(rangeAeMaxGain.value) });
  });

  chkAeLock.addEventListener('change', () => {
    postControls({ ae_lock: chkAeLock.checked });
  });

  rangeEv.addEventListener('input', () => {
    valEv.textContent = (parseFloat(rangeEv.value) > 0 ? '+' : '') + parseFloat(rangeEv.value).toFixed(1) + ' EV';
    postControls({ ev_compensation: parseFloat(rangeEv.value) });
  });

  selectAntibanding.addEventListener('change', () => {
    postControls({ antibanding: selectAntibanding.value });
  });

  selectAwb.addEventListener('change', () => {
    postControls({ awb_mode: selectAwb.value });
  });

  chkAwbLock.addEventListener('change', () => {
    postControls({ awb_lock: chkAwbLock.checked });
  });

  rangeIspGain.addEventListener('input', () => {
    valIspGain.textContent = parseFloat(rangeIspGain.value).toFixed(1) + '\u00d7';
    postControls({ isp_digital_gain: parseFloat(rangeIspGain.value) });
  });

  selectDenoise.addEventListener('change', () => {
    postControls({ denoise_mode: selectDenoise.value });
  });

  selectEdge.addEventListener('change', () => {
    postControls({ edge_mode: selectEdge.value });
  });

  inputBallSpeed.addEventListener('input', updateBlurCalculator);
  inputFovWidth.addEventListener('input', updateBlurCalculator);

  // Zoom Modes & Minimap
  zoomBtns.forEach(btn => {
    btn.addEventListener('click', () => {
      zoomBtns.forEach(b => b.classList.remove('active'));
      btn.classList.add('active');
      activeZoom = btn.dataset.zoom;
      postDisplay({ zoom_mode: activeZoom });
      updateMinimap();
    });
  });

  chkPeaking.addEventListener('change', () => {
    postDisplay({ focus_peaking: chkPeaking.checked });
  });

  selectPeakingColor.addEventListener('change', () => {
    postDisplay({ peaking_color: selectPeakingColor.value });
  });

  chkZebra.addEventListener('change', () => {
    postDisplay({ zebra_stripes: chkZebra.checked });
  });

  selectGrid.addEventListener('change', () => {
    postDisplay({ grid_mode: selectGrid.value });
  });

  selectViewFps.addEventListener('change', () => {
    postDisplay({ fps_limit: parseInt(selectViewFps.value, 10) });
  });

  // Viewport Click-to-Center (for 1x and 2x zoom)
  viewportWrap.addEventListener('click', (e) => {
    if (activeZoom === 'fit') return;
    const rect = viewportWrap.getBoundingClientRect();
    activeCenterX = (e.clientX - rect.left) / rect.width;
    activeCenterY = (e.clientY - rect.top) / rect.height;
    postDisplay({ center_x: activeCenterX, center_y: activeCenterY });
    updateMinimap();
  });

  // Presets
  function loadPresetList() {
    fetch('/api/presets')
      .then(r => r.json())
      .then(list => {
        presetSelect.innerHTML = '';
        list.forEach(name => {
          const opt = document.createElement('option');
          opt.value = name;
          opt.textContent = name;
          presetSelect.appendChild(opt);
        });
      })
      .catch(console.error);
  }

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

  btnSavePreset.addEventListener('click', () => {
    const name = prompt('Preset name (alphanumeric/hyphen):');
    if (!name) return;
    fetch('/api/state')
      .then(r => r.json())
      .then(state => {
        const payload = {
          description: 'User saved studio preset',
          controls: state.controls,
          display: state.display_settings
        };
        return fetch(`/api/presets/${encodeURIComponent(name)}`, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(payload)
        });
      })
      .then(r => r.json())
      .then(() => {
        showToast(`Preset saved: ${name}`);
        loadPresetList();
      })
      .catch(e => showToast('Failed to save preset: ' + e));
  });

  // --- Initialization ---
  fetch('/api/state')
    .then(r => r.json())
    .then(applyStateToUI)
    .catch(console.error);

  setupSSE();
  updateBlurCalculator();
  loadPresetList();
  loadSnapshotGallery();
})();
