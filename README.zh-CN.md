# 基于红米 K Pad（天玑 9400+ / MT8799）的 DroidDeck 支持开发

**当前状态：已实现初步运行目标**——Steam 大屏界面可在平板上运行，Windows 游戏可从库中启动并出
画面，触控/键盘/手柄可进入游戏。

[English](README.md) · [简体中文](README.zh-CN.md)

这是 [DroidDeck](https://github.com/Droid-Deck/DroidDeck) 的分支，面向 **红米 K Pad（25079RPDCC）**：
SoC **MT8799 / 天玑 9400+**，GPU **Mali-G925-Immortalis MC12**（`0xd8300015`，pan_arch 13，
kbase CSF UAPI 1.30），Android 16，bootloader 锁定，无 root。

> 与 Valve、联发科、上游 DroidDeck 项目均无隶属关系。

## 下载

| 内容 | 位置 |
|---|---|
| **源码** | 本仓库（`git clone`） |
| **APK** | [Releases](../../releases) → `DroidDeck-KPad-0.4.0-sl1-debug.apk` |
| **驱动** | [Releases](../../releases) → `DroidDeck-PanVK-G925-main186-p1.zip` |

APK 为 debug 签名；若设备上已安装不同签名的版本，请先卸载。驱动包即 **组件 → GPU 驱动 →
导入 .zip** 所接受的格式。

## 显示链路

无 root 时没有 DRM render node：来宾有 `/dev/mali0`，但没有 `/dev/dri/renderD128`。
Xwayland 的 glamor 需要 EGL + GBM + render node，Chromium/CEF 需要在 surface 上创建 EGL 上下文，
两者都不可用。当前实现把画面渲染到软件 X 服务器，再由 GPU 合成器呈现：

```
Steam / Windows 游戏（Proton ARM64 → Wine → FEX → DXVK）
  → Xvfb :7（swrast GLX，帧缓冲在 /tmp/xvfb-fb）
  → xpresent（mmap → XDamage 脏矩形 → wl_shm → wl_surface）
  → DroidDeck 的 Wayland 合成器（Vulkan + Mali 厂商驱动）
  → SurfaceView

输入：合成器 wl_pointer/wl_keyboard → xpresent → XTEST → Xvfb → Steam/游戏
窗口管理：xraise/xhide（Xvfb 无窗口管理器）
```

Steam 客户端与游戏共用同一个显示（`BL_XWAYLAND_COUNT=1`），覆盖层、焦点与手柄路由因此保持在同一个
X 服务器内。

## 驱动实现

运行时驱动基于 Mesa 的 PanVK kbase 路径，使用 Mesa `5a07217f` 与上游 `csf-v11` 补丁集（179 个），
并附加两个本地补丁：

* **子队列超时保护**——kbase 在子队列 10 秒无进展时判定丢失；补丁在 `extract`/`seqno`/
  `stream_progress` 仍在推进时延长该判定窗口（有上限）。
* **Vulkan 1.4 核心命令别名**——把 `vkCmdBindDescriptorSets2`、`vkCmdPushConstants2` 导出为 KHR
  入口点的别名。

构建步骤、补丁说明、验证方法与工具链见 [docs/DRIVER.md](docs/DRIVER.md)，源码在
[driver/](driver/)。

## 开发配套工具

| 工具 | 位置 | 用途 |
|---|---|---|
| 驱动构建脚本 | `driver/build/00-env.sh` … `40b-verify.sh` | 工具链、打补丁、交叉编译、ELF/符号校验 |
| 测试构建 | `driver/build/60-build-tests.sh`、`63-build-all-tests.sh` | 编译 Vulkan 测试程序 |
| profile 与元数据 | `driver/profile/`、`driver/meta/` | GPU profile 与驱动包元数据 |
| APK 构建 | `tools/release/build-mt8799.ps1` | Windows 下构建 APK |
| 来宾脚本哈希 | `tools/release/runtime_inputs.py` | 修改 `tools/linuxfs/overlay/**` 后记录 `runtime-inputs.json` |
| 呈现器 | `tools/xtools/xpresent.c`（来宾 `/root/xpresent`） | Xvfb 帧缓冲 → Wayland surface |
| 窗口控制 | `tools/xtools/xraise.c`、`xhide.c`（来宾 `/root/xraise`、`/root/xhide`） | 置顶/切换与隐藏/显示 Steam 界面 |
| 会话日志 | `files/logs/<session>/` | `session.log`、`wayland.log`、`xpresent.out` |

## 当前局限

* **性能偏低**：Steam 界面约 13–16 fps，游戏在这条路径上尚未做性能优化，帧生成与拷贝路径仍有
  优化空间。
* Steam 的 CEF 走软件光栅，是界面帧率偏低的主要原因。
* `p8-common-gates` 在近期构建上出现 SIGSEGV，尚未定位。
* 没有 bionic 构建，PanProbe 无法在 Android 进程内加载该驱动。
* 没有 dmabuf/GBM 导出，显示链路只能拷贝帧，无法零拷贝共享。
* G925 在 MT8799 上的组合不在上游验证矩阵内。
* 游戏与 Steam 客户端共用一个 Xvfb，窗口摆放依赖项目自带的最小窗口管理器。

## 构建

需要 JDK 17、Android SDK（compileSdk 34）、NDK 30。修改来宾脚本后：

```bash
python3 tools/release/runtime_inputs.py record
```

Windows：`powershell -File tools/release/build-mt8799.ps1 -Project . -Log build.log`
其他平台：`./gradlew :app:assembleDebug`

## 安装

1. 安装 APK，授予存储权限；
2. **组件 → GPU 驱动 → 导入 .zip**，选择驱动包（运行时驱动）；
3. 在 `Android/data/dev.hayash1.steamoslite/files/droiddeck-env` 写入：

   ```
   MESA_LOG=android,file
   PANVK_KBASE_GPU_SEMAPHORE_WAITS=0
   BL_NO_GAMESCOPE=1
   BL_STEAM_SOFT_CEF=1
   BL_STEAM_XVFB=1
   ```

4. 启动会话并登录 Steam（首次会下载客户端）。

## 贡献者

* 维护者：**Markis Lin** —— 设备适配、应用、测试
* **DeepSeek**（AI 结对编程）—— 驱动研究、调试、显示链路、文档

## 参与贡献

欢迎带可核对证据的复现报告。规则见 [CONTRIBUTING.md](CONTRIBUTING.md)，待办项见
[docs/DRIVER.md](docs/DRIVER.md) 末尾。

## 许可

GPL-3.0，继承自 DroidDeck。第三方组件见 [NOTICE.md](NOTICE.md)。Steam 与 Proton 归 Valve 所有，
本项目不打包它们。
