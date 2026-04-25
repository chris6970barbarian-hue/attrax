# 嘴替 · 感知端

表情识别（MediaPipe FaceMesh 几何规则）→ WebSocket 推事件给机械臂控制端。
支持图形化校准 + 图形化监视 + 一键部署。

## 快速开始（首次用户）

**第一次**按顺序双击 `bin/` 目录里的 3 个脚本：

1. `**install.command`** — 装环境（只做一次，约 3–5 分钟自动装 Homebrew + uv + Python 依赖）
2. `**run_calibrate.command`** — 打开图形校准向导，跟着界面做 4 次表情（中性/皱眉/哈欠/紧张），结果存到 `config/personal.yaml`
3. `**run.command`** — 启动 Demo，会同时开监视器 GUI + 感知端主程序

换人用？重跑 `**run_calibrate.command**` 即可。

## 目录结构

```
JiXieBi/
├── 使用说明.txt                   解压后第一眼看的极简上手
├── bin/                          一键启动器 (.command, macOS 双击可跑)
│   ├── install.command           装环境
│   ├── run.command               Demo 主入口 (= 监视器 + 感知端)
│   ├── run_calibrate.command     表情校准
│   └── run_monitor_only.command  只开监视器 (对接真 Pi 时用)
├── config/
│   ├── config.yaml               主配置 (阈值、WS URL、摄像头)
│   ├── personal.yaml             个人校准结果 (由 calibrate_gui.py 写入)
│   └── personal.yaml.example     示范模板
├── src/                          感知端实现
│   ├── main.py
│   ├── camera/ detector/ comm/ service/ utils/
│   └── ...
├── tools/
│   ├── calibrate_gui.py          图形校准向导
│   ├── monitor_gui.py            图形状态监视器
│   ├── visualize.py              (调试) 实时 FaceMesh + 状态 HUD
│   └── mock_pi_server.py         (调试) CLI 版 Pi 模拟器
├── pi/                           树莓派端部署包
│   ├── install.sh                apt + pip + 虚拟环境
│   ├── pi_server.py              WS 服务端骨架,嵌入式同伴接 ESP32
│   ├── systemd/zuiti-pi.service  开机自启 (可选)
│   └── README.md                 以太网连接指南
├── docs/
│   └── protocol.md               完整接口文档 (消息 schema + 枚举)
├── zuiti-dashboard/              本机情绪面板 (Flask, 可选, 与 Pi 无直接 HTTP 依赖)
│   ├── dashboard_server.py       API + 静态页
│   ├── static/ebti-types/          EBTI 四格 16 种立绘, 见下方「情绪面板」
│   └── config.yaml               面板端口、番茄钟时长等
└── models/snack_tm/              Teachable Machine 模型 (Phase 5 下线,保留文件)
```

## 树莓派对接说明（与情绪面板的关系）

- **与机械臂/树莓派控制端的唯一正式通道**仍是 **[WebSocket 消息协议](docs/protocol.md)**（`event/*`、`heartbeat`、`cmd/*`），Pi 上跑 `pi/pi_server.py` 的对接方式**未因情绪面板而改变**。
- **情绪面板 `zuiti-dashboard/`** 跑在 **Mac/本机**（默认可 `http://127.0.0.1:5050`），由感知端在并行路径上向面板 **HTTP POST** 数据；**不经过树莓派**，也**不要求**树莓派实现 HTTP 才能联调机械臂。

若你**只负责树莓派/机械臂固件与协议实现**：以 `docs/protocol.md` 为准即可；下节是写给「同时管 Mac 端展示」的队友参考。

## 消息协议（给机械臂队友）

完整参考见 `[docs/protocol.md](docs/protocol.md)`。核心：

**感知端 → Pi（触发事件）**

- `event/feed_trigger` — 要投喂，带 `suggest_snack` 字段提示零食类型
- `event/bite_trigger` — 护主咬
- `heartbeat` — 1Hz 状态快照

**Pi → 感知端（控制）**

- `cmd/pause` / `cmd/resume` — 暂停/恢复识别
- `cmd/switch_camera_mode` — 切换摄像头角色

**业务枚举**

- `face_state`：`neutral / frown / yawn / tired / tense`
- `suggest_snack`：`chocolate / chips / gummy / coffee_candy / empty`

## 连接树莓派

MacBook ↔ Pi 直接用网线连，默认 mDNS（`raspberrypi.local:8765`）免配置：

1. Pi 端 `bash pi/install.sh` 装 WebSocket 依赖
2. Pi 上启动 `python3 pi/pi_server.py`
3. Mac 上 `bin/run.command`，主 URL 自动连接；如果 mDNS 不通（5 秒超时）会自动 fallback 到 `127.0.0.1:8765`（本地监视器）

固定 IP 方案、systemd 自启、嵌入式 hook 接入详见 `[pi/README.md](pi/README.md)`。

## 情绪面板（zuiti-dashboard，可选，Mac/本机）

**用途**：本机 Web 看「我的情绪实时状态」、按人脸累计的**专注时长/番茄钟**、今日投喂与 EBTI 等；**与树莓派 WebSocket 独立**，不影响 `pi_server.py` 协议对接。

1. **安装与启动**（在仓库内）  
   - 依赖：见 `zuiti-dashboard/requirements.txt`，可 `cd zuiti-dashboard && ../.venv/bin/python -m pip install -r requirements.txt` 或使用项目 venv。  
   - 启动：  
     `ZUITI_DASHBOARD_PORT=5050 python dashboard_server.py`（macOS 上 **5000 常被系统占用**，常用 **5050**；也可用环境变量覆盖）。  
2. **感知端上报开关**（`config/config.yaml`）  
   - `dashboard.enabled: true`  
   - `dashboard.base_url: "http://127.0.0.1:5050"`（与上面端口一致）  
   - `dashboard.suggest_snack_to_slot`：零食名 → 槽位，与 `zuiti-dashboard/config.yaml` 的 `snack_slot_labels` 一致即可。  
3. **感知端在做什么**（`src/comm/dashboard_client.py`）  
   - 约每 **1s** 与主程序心跳同步：`POST /api/event`，`type: snapshot`，带当前 `face_state`（`tired` 在面板中记为「疲惫」/键名 `fatigue`）、`face` 是否有人脸。  
   - 发生与 WS 同名的业务事件时同步：`feed_trigger` → `type: feed`；`bite_trigger` → `type: bite`。  
4. **联调/无摄像头自测**  
   - `uv run python tools/push_perception_to_dashboard.py` 或加参数如 `frown`；仅用于验证面板有数据。  
5. **EBTI 16 格立绘**（「我的 EBTI」大头像）  
   - 路径：`zuiti-dashboard/static/ebti-types/{CODE}.png`（`CODE` 为当日 `/api/ebti` 返回的 **4 位大写**，如 `SBEA.png`、`HMTR.png`），共 16 种组合（见该目录下 `manifest.json`）。**缺图**时回退 `static/ebti-avatar.png**。  
6. **面板自己的配置**（`zuiti-dashboard/config.yaml`）  
   - 番茄钟：默认 `pomodoro_work_seconds: 1500`（25 分钟 × 有脸在画面时每秒 +1，无人脸则本段暂停）。  

树莓派侧**不需要**为情绪面板开 HTTP；若需在同一局域网用浏览器打开面板，将电脑防火墙放行该端口即可。

## 高级用法

**手动改配置**（如果想绕开 GUI 校准）：直接编辑 `config/personal.yaml`，按照 `config/personal.yaml.example` 的结构写。

**命令行跑**（开发调试）：

```bash
uv run python -m src.main --config config/config.yaml    # 感知端
uv run python tools/monitor_gui.py                       # 监视器 GUI
uv run python tools/mock_pi_server.py                    # CLI 版 Pi 模拟
uv run python tools/visualize.py                         # 实时 FaceMesh 调参
uv run python tools/calibrate_gui.py                     # 校准向导
```

**调阈值**：`tools/visualize.py` 开窗显示所有 FaceMesh 几何指标实时值，改完 `config/config.yaml` 或 `config/personal.yaml` 后重启感知端生效。

## 已知限制

- 摄像头权限首次被 macOS 拦截：需去 系统设置 → 隐私与安全性 → 摄像头 → 勾选 Terminal
- 零食识别功能下线（机械臂改用固定槽位映射），代码保留在 `src/detector/snack*.py` 以备回滚
- 默认摄像头模式 `single_switching`（单摄像头分时），双摄像头模式 `dual_dedicated` 可通过 `config/config.yaml` 切换

