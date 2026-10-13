// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 YUjinEDU

(function () {
  'use strict';

  // DOM Elements
  const connPill = document.getElementById('conn-pill');
  const streamImg = document.getElementById('stream-img');
  const viewportWrap = document.getElementById('viewport-wrap');
  const hudRes = document.getElementById('hud-res');
  const hudFps = document.getElementById('hud-fps');
  const toast = document.getElementById('toast');

  // Metrics elements
  const metricSensorFps = document.getElementById('metric-sensor-fps');
  const metricHostFps = document.getElementById('metric-host-fps');
  const metricDisplayFps = document.getElementById('metric-display-fps');
  const metricViewers = document.getElementById('metric-viewers');
  const metricGaps = document.getElementById('metric-gaps');
  const metricBadIv = document.getElementById('metric-bad-iv');
  const metricIvRange = document.getElementById('metric-iv-range');
  const metricTotalFrames = document.getElementById('metric-total-frames');
  const metricUptime = document.getElementById('metric-uptime');
  const telemetryTime = document.getElementById('telemetry-time');

  // Controls elements
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

  // AE elements
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

  // Blur Calc
  const inputBallSpeed = document.getElementById('input-ball-speed');
  const inputFovWidth = document.getElementById('input-fov-width');
  const calcSpeedMs = document.getElementById('calc-speed-ms');
  const calcBlurCm = document.getElementById('calc-blur-cm');
  const calcBlurPx = document.getElementById('calc-blur-px');

  // Assist Tools
  const zoomBtns = document.querySelectorAll('.btn-tool[data-zoom]');
  const chkPeaking = document.getElementById('chk-peaking');
  const chkZebra = document.getElementById('chk-zebra');
  const selectGrid = document.getElementById('select-grid');
  const selectViewFps = document.getElementById('select-view-fps');

  // Presets & Snapshots
  const presetSelect = document.getElementById('preset-select');
  const btnLoadPreset = document.getElementById('btn-load-preset');
  const btnSavePreset = document.getElementById('btn-save-preset');
  const btnSnapshot = document.getElementById('btn-snapshot');

  let currentExposureUs = 4000;
  let isDraggingSlider = false;
  let postControlsTimer = null;
  let postDisplayTimer = null;
  let peakFocus = 10.0;

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

  // --- Motion Blur Calculator ---
  function updateBlurCalculator() {
    const kmh = parseFloat(inputBallSpeed.value) || 140;
    const fovM = parseFloat(inputFovWidth.value) || 3.0;
    const speedMs = kmh / 3.6;
    calcSpeedMs.textContent = speedMs.toFixed(1) + ' m/s';

    const expSec = currentExposureUs / 1e6;
    const blurMeters = speedMs * expSec;
    const blurCm = blurMeters * 100;
    const blurPx = (blurMeters / fovM) * 1332;

    calcBlurCm.textContent = blurCm.toFixed(1) + ' cm';
    calcBlurPx.textContent = Math.round(blurPx) + ' px';
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

      // Color gradient from dark to light
      const intensity = Math.floor((i / 64) * 200) + 55;
      histCtx.fillStyle = `rgb(${intensity}, ${intensity}, ${intensity})`;
      histCtx.fillRect(x, y, barW - 0.5, barH);
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

        // Telemetry
        metricSensorFps.textContent = cm.sensor_fps > 0 ? cm.sensor_fps.toFixed(1) : '--';
        metricHostFps.textContent = cm.host_fps > 0 ? cm.host_fps.toFixed(1) : '--';
        metricDisplayFps.textContent = dm.display_fps > 0 ? dm.display_fps.toFixed(1) : '--';
        metricViewers.textContent = dm.active_viewers;
        hudFps.textContent = (dm.display_fps > 0 ? dm.display_fps.toFixed(0) : '--') + ' FPS';

        metricGaps.textContent = cm.sequence_gaps;
        metricGaps.className = 'metric-number ' + (cm.sequence_gaps === 0 ? 'success' : 'warning');

        metricBadIv.textContent = cm.bad_intervals;
        metricIvRange.textContent = `${cm.min_interval_ms.toFixed(1)}..${cm.max_interval_ms.toFixed(1)} ms`;

        metricTotalFrames.textContent = cm.total_frames.toLocaleString();
        const uptimeSec = Math.floor((cm.uptime_ms || 0) / 1000);
        const m = Math.floor(uptimeSec / 60);
        const s = uptimeSec % 60;
        metricUptime.textContent = `Uptime: ${m}m ${s}s`;
        telemetryTime.textContent = new Date().toLocaleTimeString();

        valActualCct.textContent = (cm.actual_cct || '--') + ' K';

        // Update current exposure for blur calculator
        currentExposureUs = ctrl.auto_exposure ? cm.actual_exposure_us : ctrl.exposure_us;
        updateBlurCalculator();

        // Instruments: Focus
        if (dm.focus_score !== undefined) {
          focusScoreVal.textContent = Math.round(dm.focus_score);
          peakFocus = Math.max(peakFocus, dm.focus_score);
          const pct = Math.min(100, Math.max(0, (dm.focus_score / peakFocus) * 100));
          focusMeterFill.style.width = pct.toFixed(1) + '%';
          focusMeterPeak.style.left = pct.toFixed(1) + '%';
        }

        // Instruments: Histogram
        if (dm.histogram_64) {
          drawHistogram(dm.histogram_64);
        }

        // Sync inputs if not user-dragging
        if (!isDraggingSlider) {
          if (!ctrl.auto_exposure) {
            rangeExp.value = ctrl.exposure_us;
            valExpUs.textContent = ctrl.exposure_us + ' \u03bcs';
            valExpMs.textContent = '(' + (ctrl.exposure_us / 1000).toFixed(2) + ' ms)';
            rangeGain.value = ctrl.gain;
            valGain.textContent = ctrl.gain.toFixed(2) + '\u00d7';
          }
        }
      } catch (err) {
        console.error('SSE parse error:', err);
      }
    };
  }

  // --- Slider and Control Listeners ---
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

  // AE Mode Radio
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

  // Blur Calc Inputs
  inputBallSpeed.addEventListener('input', updateBlurCalculator);
  inputFovWidth.addEventListener('input', updateBlurCalculator);

  // Assist Tools
  zoomBtns.forEach(btn => {
    btn.addEventListener('click', () => {
      zoomBtns.forEach(b => b.classList.remove('active'));
      btn.classList.add('active');
      const zoom = btn.dataset.zoom;
      postDisplay({ zoom_mode: zoom });
    });
  });

  chkPeaking.addEventListener('change', () => {
    postDisplay({ focus_peaking: chkPeaking.checked });
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
    const activeZoom = document.querySelector('.btn-tool[data-zoom].active').dataset.zoom;
    if (activeZoom === 'fit') return;

    const rect = viewportWrap.getBoundingClientRect();
    const nx = (e.clientX - rect.left) / rect.width;
    const ny = (e.clientY - rect.top) / rect.height;
    postDisplay({ center_x: nx, center_y: ny });
  });

  // Snapshot
  btnSnapshot.addEventListener('click', () => {
    btnSnapshot.disabled = true;
    btnSnapshot.textContent = '\u23F3 Saving...';
    fetch('/api/snapshot', { method: 'POST' })
      .then(r => r.json())
      .then(d => {
        if (d.status === 'ok') {
          showToast(`Snapshot saved: ${d.path}`);
        } else {
          showToast(`Snapshot failed: ${d.error || 'Unknown error'}`);
        }
      })
      .catch(e => showToast('Error: ' + e))
      .finally(() => {
        btnSnapshot.disabled = false;
        btnSnapshot.textContent = '\uD83D\uDCF7 Take Snapshot';
      });
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

  // Init
  setupSSE();
  updateBlurCalculator();
  loadPresetList();
})();
