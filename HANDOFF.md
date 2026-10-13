# 인수인계 — jetson-IMX477-studio

기록: 2026-10-13 18:45 KST. 사용량 부족으로 M0 스파이크 도중 중단.
다음 에이전트는 이 문서 → `spikes/` → 아래 "다음 할 일" 순서로 이어가면 된다.

## 1. 목표 (사용자와 합의된 내용)

IMX477 120fps 카메라를 코드 없이 제어하는 GUI. 매번 C++/Python 코드를 고치는 게 불편해서 시작.

| 항목 | 결정 |
|---|---|
| 리포 | **새 리포** `jetson-IMX477-studio` (드라이버 리포 `IMX477-120fps`와 분리) |
| 위치 | `/home/yujin/codespace/jetson-IMX477-studio` |
| GitHub | `YUjinEDU/jetson-IMX477-studio`, **바로 public**. 아직 원격 생성/푸시 안 함 (`gh` 로그인 확인됨) |
| 라이선스 | **Apache-2.0** (`LICENSE` 다운로드 완료) |
| 사용 환경 | 젯슨 모니터 로컬 + 노트북 원격 **둘 다** |
| UI 방식 | **C++ 코어(카메라 소유) + 웹 UI**. 로컬은 젯슨 브라우저에서 localhost 접속 |
| YOLO 동시 실행 | **필요**. 코어가 공유메모리로 프레임 fan-out |
| 이번 범위 | P1(뷰어·제어·통계) + P2(촬영 보조)까지. 녹화·모드 전환은 이후 |

### 참고 요구사항 (원문 요지, 반드시 충족)
- Direct Libargus **mode 2, 1332×990@120fps** 수신
- 시작값 **노출 4ms · 아날로그 게인 2x · AWB AUTO**
- 노출·게인 조절, **실제 입력 FPS·프레임 누락 표시**
- **화면 표시는 입력과 분리**
- **드라이버·NITO·부팅 설정 변경 금지** (Argus API만 사용)

## 2. 현재 리포 상태

```
jetson-IMX477-studio/          (git init, main, 커밋 없음)
├── LICENSE                    Apache-2.0 원문
├── HANDOFF.md                 이 문서
├── spikes/spike.cpp           M0 스파이크 1: 단일 스트림 + copyToNvBuffer 축소
├── spikes/spike2.cpp          M0 스파이크 2: ISP 출력 2개 (컴파일만 됨, 미실행)
└── third_party/
    ├── cpp-httplib/httplib.h  v0.18.3 + LICENSE (MIT)
    └── nlohmann/json.hpp      v3.11.3 + LICENSE.MIT
```

스파이크 빌드:
```bash
g++ -O2 -std=c++17 -pthread spikes/spike2.cpp -o /tmp/spike2 \
  -I/usr/src/jetson_multimedia_api/include -L/usr/lib/aarch64-linux-gnu/nvidia \
  -lnvargus -lnvbufsurface -lEGL
```

## 3. 환경 사실 (확인됨)

- Orin Nano Super, L4T R39.2.1, X11 `DISPLAY=:1`, 6코어, RAM 7.3GB (평소 5.5GB 사용 중)
- **cmake 없음** → Makefile 사용 (드라이버 리포와 동일 방식)
- Qt dev 헤더·GTK dev 헤더 없음. PyGObject는 있으나 웹 UI로 결정했으므로 무관
- node/npm 없음 → 프론트는 **빌드 없는 순수 HTML/JS**
- Python 웹 라이브러리(aiohttp/fastapi 등) 없음 → HTTP는 **C++ cpp-httplib**
- **하드웨어 영상 인코더 없음** (`/dev/v4l2-nvenc` 없음). 하드웨어 JPEG(`nvjpegenc`, `NvJpegEncoder.cpp`)는 있음
- Argus 샘플: `/usr/src/jetson_multimedia_api/argus/samples/` (`multiStream`, `userAutoExposure`, `histogram`, `denoise` 등 참고 가능)
- mode 2 센서 범위(실측 출력): 노출 13µs–683.7ms, 아날로그 게인 1.00–22.25, frame duration 8.333ms–500ms. 120fps에서는 노출 ≤ 8.33ms
- 카메라는 **한 프로세스만** 열 수 있음. 실행 전 `pgrep -fa 'argus|isp_pipeline|yolo|spike'` 확인
- `nv_imx477` 모듈 언로드/언바인드 절대 금지 (과거 커널 패닉)

## 4. M0 스파이크 결과 (핵심 발견)

모두 mode 2, FIFO 16, AWB AUTO, 4ms/2x. "bad"는 센서 간격이 목표 주기의 0.5배 미만이거나 1.5배 초과인 프레임 수.

| 구성 (`spike <frames> <ai_w> <change_every> <disp_every>`) | 프레임 | 센서 FPS | 번호 gap | bad 간격 | copy 평균/최대 |
|---|---:|---:|---:|---:|---:|
| 복사 없음 | 1200 | 120.162 | 0 | 0 | — |
| 풀해상도 복사 4프레임마다(≈30fps) | 1200 | 120.162 | 0 | 0 | 1.72 / 7.71ms (1회 ≈ 7ms) |
| 매 프레임 1332→320 축소 복사 | 1200 | **105.3** | 0 | 168 | 9.22 / 38.5ms |
| 매 프레임 1332→640 축소 복사 | 1200 | **117.4** | 0 | 28 | 8.26 / 20.0ms |
| 320 축소 + 30fps 풀복사 + 240프레임마다 노출/게인 변경 | 7261 | **89.5** | 0 | 2460 | 10.87 / 24.8ms |

결론:
1. **`IImageNativeBuffer::copyToNvBuffer`로 매 프레임 축소하면 120fps 유지 불가** (1회 8~10ms). AI fan-out을 이 방식으로 하면 안 됨.
2. **30fps 풀해상도 복사(표시용)는 안전.** 입력 120.162fps, 누락 0.
3. **소비가 느리면 frame number gap은 0인데 센서 간격이 깨진다.** FIFO가 차서 캡처 자체가 지연되기 때문. → UI의 "누락" 지표는 **번호 gap + 센서 간격 이상 둘 다** 보여줘야 함. gap만 보면 문제를 놓친다.
4. `repeat()` 재호출(노출/게인 변경) 자체는 최대 0.05ms로 빠름. 메타데이터에 새 노출이 반영되기까지 최대 19프레임 관측 (단, 루프가 밀린 상태에서 측정한 값이라 정상 상태에서 다시 재야 함).
5. 예전 YOLO 320/640이 120fps를 낸 경로는 **ISP 출력 자체가 320/640**이고 복사가 같은 크기였음. 그래서 다음 후보는 **ISP 출력 스트림 2개**(1332×990 + 320/640).

## 5. 다음 할 일

### 5.1 M0 마무리 (최우선, 결과가 설계를 결정)
```bash
/tmp/spike2 7261 320 2000 4   # 풀+320 두 스트림, 2초마다 노출/게인 변경, 30fps 표시 복사
/tmp/spike2 7261 640 2000 4
/tmp/spike2 1200 320 100 4    # 슬라이더 드래그 흉내 (10Hz 변경)
```
합격 기준: 두 스트림 모두 센서 FPS ≈120.16, gap 0, bad 0, 60초 이상.
- 통과 → 아래 설계 그대로 진행
- 실패 → AI 스트림만 FIFO 대신 MAILBOX 모드, AI 해상도 640 고정, 또는 풀복사(7ms) 후 `NvBufSurfTransform`(VIC) 직접 호출 등 대안 측정

### 5.2 코어 설계 (스파이크 2 통과 가정)

```mermaid
flowchart LR
  REQ[Argus Request mode2 120fps] --> A[스트림 A 1332x990<br/>스레드: 통계 + 표시/스냅샷 복사]
  REQ --> B[스트림 B 320 or 640<br/>스레드: 같은크기 복사 → shm ring]
  A -->|30fps 최신 1장| D[표시 스레드: NV12→BGR, 축소/확대,<br/>오버레이, JPEG]
  D -->|MJPEG| WEB[웹 UI]
  WEB -->|POST /api/controls| CTL[제어 큐 → Request 수정 + repeat]
  A -->|SSE 4Hz| WEB
  B --> SHM[/dev/shm ring] --> PY[Python 클라이언트 / YOLO]
```

- **스트림 A 스레드**: 프레임마다 번호·센서 ts·노출·게인·ISP 디지털 게인·CCT·AWB 게인 기록 후 즉시 반환. 표시 스레드가 요청하면(핸드셰이크 플래그) 풀해상도 pitch NV12 버퍼로 복사. 제어 명령은 이 스레드에서 프레임 사이에 적용.
- **표시 스레드**: 접속한 클라이언트가 없으면 쉰다. `cv::cvtColorTwoPlane`(pitch 고려) → 960×714 축소 또는 확대 영역 crop → 포커스 피킹/제브라/그리드 → `cv::imencode` JPEG. 히스토그램(64 bin)과 포커스 점수도 여기서 계산. CPU JPEG가 부족하면 그때 NVJPG로 교체.
- **HTTP (cpp-httplib)**: 기본 `127.0.0.1:8477`, 원격은 `--host 0.0.0.0` (인증 없음 → 문서에 경고).
  - `GET /` 정적 파일(`web/`)
  - `GET /stream.mjpg` multipart, 클라이언트별 최신 프레임만 전송 (느린 클라이언트는 프레임 생략)
  - `GET /events` SSE 통계 JSON
  - `GET /api/state` 현재 설정 + 센서 범위(caps)
  - `POST /api/controls` 부분 업데이트 JSON
  - `POST /api/snapshot` 풀해상도 JPEG + 메타데이터 JSON 저장
  - `GET/POST/DELETE /api/presets` (`$XDG_CONFIG_HOME/imx477-studio/presets/*.json`)
  - `CPPHTTPLIB_THREAD_POOL_COUNT`를 늘릴 것 (MJPEG 연결마다 스레드 점유)
- **제어 항목** (Argus): 수동/AE(노출 상한·게인 상한), AE lock, EV 보정, **antibanding**(실내 조명 120Hz 깜빡임 대응), AWB 모드/lock/수동 게인, ISP 디지털 게인 범위, denoise·edge enhance 모드/강도, 채도. 범위는 하드코딩하지 말고 `ISensorMode`에서 읽어 caps로 노출(노출은 frame duration 이하로 제한).
- **통계 (SSE)**: 센서 FPS, 호스트 FPS(1초 창), 누적 프레임, 번호 gap 합계, 센서 간격 이상 수, 최근 최대 간격, 요청값 vs 실제 적용값(노출·게인·ISP 게인·CCT·AWB 게인), 표시 FPS, JPEG 인코딩 ms, 접속 수, shm 쓰기 상태, 히스토그램, 포커스 점수.
- **공유메모리 ring** (`/dev/shm/imx477-studio-ai`): 헤더(magic, version, w, h, slot 수 16, slot stride, NV12, `atomic<u64> write_seq`, futex용 `atomic<u32>`, writer pid, session id, 리더 heartbeat ns) + 슬롯(seqlock 카운터, 프레임 번호, 센서 ts, 호스트 ts, 노출, 게인, flags) + NV12 데이터. 리더 heartbeat가 2초 이상 없으면 쓰기 생략(자원 절약).
  - Python 리더는 순수 Python 대신 **작은 C 라이브러리 + ctypes**로 구현 (ARM 메모리 순서 때문에 seqlock acquire 보장, futex 대기로 폴링 제거, ctypes 호출 중 GIL 해제).
  - Python API: `StudioCamera(mode="fifo"|"latest").read()` → `(bgr, metadata)`. metadata 키는 기존 `imx477_camera.Camera`와 맞춤 (`sequence`, `sensor_timestamp_ns`, `host_receive_ns` 등). fifo는 따라잡히면 overrun 오류/카운트, latest는 생략 수 카운트.

### 5.3 웹 UI (`web/`, 빌드 없음)
- 영상: `fetch` 스트림으로 MJPEG를 파싱(Content-Length 기준) → `createImageBitmap` → canvas. 브라우저 실제 표시 FPS를 측정할 수 있어서 `<img>` 대신 이 방식.
- 패널: 입력 FPS/누락 카드, 노출(µs 슬라이더+숫자)·게인, AE/AWB, ISP, 촬영 보조(피킹 on/색/임계값, 제브라 on/임계값, 그리드 없음/3분할/십자선, 확대 1:1·2x와 클릭 위치 중심), 히스토그램, **모션 블러 계산기**(공 속도 km/h × 노출 → cm; 화면 가로가 덮는 실제 폭 m 입력 시 px도 표시. 예: 140km/h에서 4ms ≈ 15.6cm, 1ms ≈ 3.9cm), 스냅샷, 프리셋.
- 표시 설정(확대·오버레이)은 전역 공유 (클라이언트별 렌더링 안 함, 문서에 명시).

### 5.4 YOLO 연동
`baseball-trajectory-tracking/src/inference/yolo_benchmark.py`의 `live()`는 `isp_pipeline` 서브프로세스 파이프에 묶여 있음. `--source studio`를 추가해서 reader 스레드만 `StudioCamera`로 바꾸고, 캡처 요약(`frames`, `sensor_fps`, `missing_sequences`, `buffer_errors`)은 메타데이터로 클라이언트에서 계산. 합격 기준: **GUI가 열린 상태로** YOLO26n/s 320 FIFO8 60초에서 7261/7261 전 프레임 처리 (기존 결과 120.18fps 재현).

### 5.5 검증 / 마무리
- 하드웨어 soak: 코어를 띄우고 스크립트로 HTTP 제어를 반복 변경하면서 60초 이상(7261프레임) → 센서 FPS ≈120.16, gap 0, 간격 이상 0.
- 브라우저 확인: 젯슨 로컬 `http://127.0.0.1:8477`, 원격 `--host 0.0.0.0`으로 다른 기기 접속.
- 문서: `README.md`(영어, 전제조건: IMX477-120fps 드라이버 + mode2 NITO 설치), `docs/api.md`, `docs/architecture.md`, `CHANGELOG.md`, 서드파티 라이선스용 `NOTICE`.
- `spikes/`는 결과를 문서에 옮긴 뒤 삭제할지 판단.
- 워크스페이스 `/home/yujin/codespace/README.md` 구조 설명에 새 리포 추가.
- `gh repo create YUjinEDU/jetson-IMX477-studio --public --source . --push`

## 6. 하지 말 것
- 드라이버/DT/NITO/부팅 설정/시스템 라이브러리 변경, `nv_imx477` 언로드
- 카메라 클라이언트 동시 실행, `killall`/SIGKILL 남발 (확인한 PID에 SIGTERM)
- 매 프레임 `copyToNvBuffer` 축소 (4장 참조)
- frame number gap만으로 "누락 0" 주장 (센서 간격도 함께)
- 센서 ts와 호스트 시계를 빼서 지연 시간 주장
- 기존 `IMX477-120fps`의 `isp_pipeline`/`imx477_camera.py` 이동·삭제 (YOLO 벤치마크가 사용 중)
