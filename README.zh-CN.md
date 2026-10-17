# 基于红米 K Pad（天玑 9400+ / MT8799）的 DroidDeck 支持开发

**已实现初步运行目标：把 SteamOS 跑在联发科 Mali 平板上。**

[English](README.md) · [简体中文](README.zh-CN.md)

这是 [DroidDeck](https://github.com/Droid-Deck/DroidDeck) 的一个分支，开发设备是 **红米 K Pad
（25079RPDCC）**：SoC **MT8799 / 天玑 9400+**，GPU **Mali-G925-Immortalis MC12**（`0xd8300015`，
pan_arch 13，kbase CSF UAPI 1.30），Android 16，bootloader 锁定，**无 root**。

目前达到的阶段：Steam 大屏界面（Big Picture）能在平板上运行，Windows 游戏能从库中启动并出画面，
触控/键盘/手柄能进入游戏。项目仍是实验性的，离"完善"还很远——缺点列在下面。

> 与 Valve、联发科、上游 DroidDeck 均无隶属关系。

## 下载

| 内容 | 位置 |
|---|---|
| **源码** | 本仓库（`git clone`） |
| **APK** | [Releases](../../releases) → `DroidDeck-KPad-0.4.0-sl1-debug.apk` |
| **驱动** | [Releases](../../releases) → `DroidDeck-PanVK-G925-main186-p1.zip` |

APK 使用 debug 签名（没有发布 release 密钥）；如果设备上已有不同签名的版本，请先卸载再装。驱动包就是
**组件 → GPU 驱动 → 导入 .zip** 需要的格式（`libvulkan_panfrost.so` + `icd.json` + `meta.json`）。

## 为什么需要一套独立的显示链路

无 root 时来宾拿不到 DRM render node：有 `/dev/mali0`，但没有 `/dev/dri/renderD128`。后果是：

* Xwayland 的 glamor 需要 EGL + GBM + render node → 没有 GLX → Wine 建不出窗口 → Steam 的
  VGUI 直接断言崩溃；
* Chromium/CEF 走 Wayland 需要在 surface 上建 EGL 上下文 → 来宾只有 surfaceless 平台 →
  CEF 永远不提交 buffer。

最终跑通的链路：

```
Steam / Windows 游戏（Proton ARM64 → Wine → FEX → DXVK）
  → Xvfb :7（swrast GLX，帧缓冲在 /tmp/xvfb-fb）
  → xpresent（mmap → XDamage 脏矩形 → wl_shm → wl_surface）
  → DroidDeck 的 Wayland 合成器（Vulkan + Mali 厂商驱动）
  → SurfaceView

输入：合成器 wl_pointer/wl_keyboard → xpresent → XTEST → Xvfb → Steam/游戏
窗口管理：xraise/xhide（Xvfb 没有窗口管理器）
```

游戏与 Steam 客户端留在**同一个显示**上（`BL_XWAYLAND_COUNT=1`），这样 Steam 的覆盖层、焦点和
手柄路由都发生在同一个 X 服务器里——这是之前"画面切了但手柄还留在大屏"的根因。

## 驱动是怎么写出来的

驱动并不是从零写的：它是 Mesa 的 PanVK kbase 路径，在 WSL2 里交叉编译，套 179 个上游补丁 + 两个
本地补丁（看门狗续期、Vulkan 1.4 核心命令别名）。

但第一版走过弯路，而且很有教育意义：手写 `meta.json` 冒充正确的 GPU 身份、照抄补丁族、把没开插桩的
`marks 0x0` 当成"固件没开始执行"。完整过程、错误、证据和教训写在
[docs/DRIVER-JOURNEY.md](docs/DRIVER-JOURNEY.md)，构建方法在 [driver/](driver/)。

## 已知缺点（诚实清单）

* `p8-common-gates` 在 RC3 及之后的构建上 SIGSEGV——**尚未定位**；
* P1 看门狗补丁没有复现器能证明它有用；
* 没有 bionic 构建，PanProbe 无法在 Android 进程里加载这个驱动；
* G925 在 MT8799 上的组合从未进入上游的验证矩阵；
* 没有 dmabuf/GBM 导出，所以显示只能拷贝帧，不能零拷贝共享；
* Steam 的 CEF 是软件光栅：界面约 13–16 fps（呈现器本身约 59 fps，XDamage 省掉约 80% 拷贝）；
* 游戏与 Steam 客户端共用一个 Xvfb，窗口摆放靠我们自己的最小窗口管理器。

## 构建

需要 JDK 17、Android SDK（compileSdk 34）、NDK 30。改动 `tools/linuxfs/overlay/**` 之后：

```bash
python3 tools/release/runtime_inputs.py record
```

Windows：`powershell -File tools/release/build-mt8799.ps1 -Project . -Log build.log`
其他平台：`./gradlew :app:assembleDebug`

## 安装

1. 安装 APK，授予存储权限；
2. **组件 → GPU 驱动 → 导入 .zip** 选择驱动包（运行时驱动）；
3. 在 `Android/data/dev.hayash1.steamoslite/files/droiddeck-env` 里写：

   ```
   MESA_LOG=android,file
   PANVK_KBASE_GPU_SEMAPHORE_WAITS=0
   BL_NO_GAMESCOPE=1
   BL_STEAM_SOFT_CEF=1
   BL_STEAM_XVFB=1
   ```

4. 启动会话并登录 Steam（首次会下载客户端）。

## 贡献者

* 维护者：**Markis Lin** —— 设备上的工作、应用、测试
* **DeepSeek**（AI 结对编程）—— 驱动研究、调试、显示链路、文档

## 参与贡献

欢迎带**可核对判据**的复现报告。规则见 [CONTRIBUTING.md](CONTRIBUTING.md)，待办清单在
[docs/DRIVER-JOURNEY.md](docs/DRIVER-JOURNEY.md) 末尾。

## 许可

GPL-3.0（继承自 DroidDeck）；第三方组件见 [NOTICE.md](NOTICE.md)。Steam 与 Proton 归 Valve
所有，本项目不打包它们。
