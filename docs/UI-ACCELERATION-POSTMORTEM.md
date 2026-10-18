# 为什么 Steam UI 的软件光栅换不掉：一次完整排查复盘

本文记录"把 Steam UI 从 CPU 光栅换成 GPU 渲染"这个目标为什么会卡住，以及中间每一步的前因后果。
结论先行：**不是驱动不行，也不是缺方案，而是我们拿不到"让 Steam 的 CEF 去调用 GL"的控制权。**

---

## 1. 起点与约束

目标很直接：Steam UI 只有 13–16 fps，希望用 GPU 渲染替换 CPU 光栅，让界面更流畅。

设备约束（先摆清楚，后面所有判断都源于此）：

- 红米 K Pad，MT8799 / 天玑 9400+，Mali-G925-Immortalis MC12；
- Android 16，bootloader 锁定，**无 root**；
- 没有 `/dev/dri/renderD128`（无 DRM render node），只有 `/dev/dri/card0`（显示控制器，属主
  `system:system 0660`，应用打不开）；
- GPU 只能通过 `/dev/mali0`（kbase）以用户态 Vulkan（PanVK）访问。

---

## 2. 为什么 UI 现在是软件渲染

Steam 的界面由 CEF（Chromium Embedded Framework）绘制。它要拿到硬件加速，需要一条
"窗口 surface 上的 GL/EGL"路径；在这台设备上这条路径断在三个地方：

1. Chromium 在 X11 上做 GPU 合成需要 **DRI3**（用于共享 GPU buffer）；
2. 来宾里没有 render node，X11 的 EGL 只能落到 **llvmpipe**（CPU 软件光栅）；
3. 我们当前为了让画面出来，给 CEF 传了 `-cef-disable-gpu`，等于直接告诉它"别用 GPU"。

所以"软件光栅"是两层叠加的结果：**环境不具备**（无 DRI3）加上**我们主动关闭**（disable-gpu）。

---

## 3. 排查路径与证据（按时间顺序）

### 3.1 驱动不是瓶颈（第一个意外的好消息）

- `vulkaninfo` 显示 PanVK 支持完整的 WSI：
  `VK_KHR_xcb_surface` / `xlib_surface` / `wayland_surface` / `headless_surface`；
- `vkcube` 在 Xvfb 上连续呈现 60 帧、返回 0；
- 游戏侧 84–91 fps，`kbase: timeout = 0`、`MESA: error = 0`。

结论：Vulkan 呈现链路是通的，原计划"给 PanVK 补 WSI"的工作不需要做。

### 3.2 GL 层没有硬件

- `eglinfo`：X11 / Wayland / Surfaceless 平台都是 **llvmpipe**；
- `MESA_LOADER_DRIVER_OVERRIDE=zink` 时，只有 **Device（离屏）平台**能拿到
  `zink Vulkan 1.4 (Mali-G925 via MESA_PANVK)`；一旦要窗口 surface，就退回 llvmpipe。

原因：Zink 的窗口呈现依赖 Vulkan WSI + dma-buf 导出，而 kbase 不提供可导出的 buffer。

### 3.3 DRI3 根本不存在（决定性事实）

自写探针 `tools/xtools/dri3probe` 的结果：

```
DRI3       ABSENT
DRI2       ABSENT
PRESENT    ABSENT
RANDR      present
MIT-SHM    present
```

配合 `AHardwareBuffer` 的验证：gralloc 的 dma-buf **不允许来宾 mmap**（`ENODEV`），
像素只能由宿主侧读取/呈现。

### 3.4 CEF 的 GPU 进程：加载谁、听谁

- 读 `/proc/<gpu-process>/maps`：它映射的是
  `~/.local/share/Steam/steamrtarm64/libEGL.so` 和 `libGLESv2.so`（**Steam 自带的 ANGLE**），
  外加 Mesa 的 `/usr/lib/libEGL.so.1.1.0`；
- 也就是说，想替换 GL，必须替换这两个库的行为。

### 3.5 第一道墙：Steam 会还原自己的文件

把 `steamrtarm64/libEGL.so` 换成我们的桩，Steam 一启动就校验并**还原**（大小 283472、
时间戳回到发布日）；换成把 `steamwebhelper` 二进制替换成包装脚本，同样被还原。

结论：**文件替换不可持续**。

### 3.6 第二道墙：参数白名单

Steam 丢弃 `-cef-use-gl` / `-cef-use-angle` 这类参数。我们用 `execve`/`posix_spawn` 拦截，
在 Steam 启动 helper 的瞬间追加 `--use-gl=egl`，成功让 **browser 和 utility** 进程拿到
（`type=browser use-gl=1`）。

但真正调用 EGL 的是 **GPU 进程**：

```
pid=16093 type=browser      use-gl=1
pid=16171 type=gpu-process  use-gl=0   ← 被 Chromium 重建命令行时丢掉
```

GPU 进程由 zygote fork 出来，不经过 `execve`，进程钩子无法注入。追加
`--ozone-platform=wayland` 同样无效。

### 3.7 第三道墙：库被加载了，但没人调用

用 `dlopen` 拦截把绝对路径的 ANGLE 加载重定向到我们的桩，结果是：

- 我们的 `libEGL.so` 被加载进 **6 个 CEF 进程**（constructor 日志可证）；
- **一次 EGL 函数调用都没有**（`stub.log` 为空）。

因为 GPU 进程在"没有 GL 开关 + 没有 DRI3"的情况下，直接放弃初始化 GL。

---

## 4. 我们自建的通道做到了什么

在确认 CEF 不可控之后，我们仍然把"一条自己的 GPU 通道"从零做出来了，作为另一条路的基础：

| 阶段 | 内容 | 验证 |
|---|---|---|
| M0 | `@droiddeck-gpu` abstract socket，版本握手 | `caps=gles=3.2;ahb=1;transport=abstract` |
| M1 | 宿主分配 `AHardwareBuffer`，fd 经 `SCM_RIGHTS` 交给来宾；宿主用**厂商 GL**（EGL + `EGLImage`）渲染进 AHB；`SurfaceControl` 上屏 | `gl renderer=Mali-G925-Immortalis MC12`、屏幕出现品红/绿/蓝帧 |
| M2 | 来宾 `libEGL`/`libGLESv2` 桩（`tools/gpustub/`），把 clear+swap 序列化到通道 | `swap 1..3 ok`，屏幕显示请求的颜色 |

其中踩到的几个坑（都已修掉，值得记住）：

- `eglCreateImageKHR` 不能直接强转 AHB，必须先用 `eglGetNativeClientBufferANDROID`；
- AHB 的 `EGLImage`/texture/FBO **必须按 buffer 缓存**，每帧重建会在第二帧卡死；
- AHB 的 dma-buf **只能由 SurfaceControl 呈现**，合成器的 `vkp_image_from_dmabuf` 导不进来
  （4 种 DRM 格式 × 支持的 modifier 全失败）；
- 全局 `LD_PRELOAD` 下拦截 `dlsym` 会让 `/bin/sleep` 段错误（递归），已移除。

---

## 5. 为什么这条通道救不了 CEF

通道解决的是"渲染给谁"的问题：它能让**我们自己写的程序**用上 GPU。

但 CEF 要的是"在它自己的 GPU 进程里调用 GL"，而我们能控制的只有：

- 文件（被 Steam 还原）；
- 环境变量和预加载库（能改库，但 GPU 进程不初始化 GL）；
- 进程启动参数（GPU 进程不经过 exec）。

**缺的是控制权，不是实现。** 再写多少 GL 桩，都不会被调用。

---

## 5.1 对外部方案文档的验证（2026-10-18）

一份流传的调研文档（`Steam_UI_GPU加速_GitHub调研与解决方案.docx`）提出了两条我们没试过的线索，
都在本设备上验证过：

**线索 A：raw dma-buf 的 X11 WSI（funnymdzz/mesa 的 PanVK kbase 变体，`WSI_X11_TERMUX=1` /
`PANVK_KBASE_DRI3=raw`），配合 Termux:X11 / Winlator 的私有 DRI3 协议。**

- 卡点 1：该方案用 `/dev/dma_heap/*` 分配 dma-buf。实测本机 `/dev/dma_heap/{system,mtk_mm,...}`
  是 `0444`，应用 uid **只能读、不能写**，分配 ioctl 无法执行；
- 卡点 2：还需要一个实现私有 DRI3 协议的 X Server（Termux:X11 是 Android 应用，不在我们的会话里）；
- 卡点 3：即便两者齐备，CEF 仍不初始化 GL（见下条），所以只会让"别的 GL 程序"用上 GPU，而不是 Steam UI。
- 可能的替代：用我们已能分配的 `AHardwareBuffer`（gralloc dma-buf）代替 dma-heap，但同样受卡点 2、3 限制。

**线索 B：`-cef-force-glx`（steam-for-linux issue #12694 提到的恢复硬件加速的开关）。**

- 实测：把 `-cef-force-glx` 传给 Steam 后，**没有任何 CEF 进程的命令行出现它**
  （browser / utility / gpu-process 全部 `force-glx=0`），说明同样被 Steam 的白名单过滤；
- 因此它无法成为控制 CEF 的后门。

两条线索都不改变第 3 节的结论；文档自己也注明这些是"未在本设备验证的外部方案"。
## 6. 出路与各自前提

| 方向 | 前提 | 代价 |
|---|---|---|
| 接受软件 UI，优化其他方面 | 无 | 收益有限（降分辨率/减动画，约 13→18 fps） |
| 解锁/root 拿 DRM render node | bootloader 可解锁 | 违背项目"无 root"前提；可彻底解决 DRI3 |
| Valve 侧支持 | CEF 开放 GL 后端参数或渲染钩子 | 不可控，只能等 |
| 自建渲染进程（用我们已做好的通道） | 不需要 CEF | 等于重写 Steam UI，不现实 |
| ARM 类 SteamOS（如 Armada） | 需要 DRM + gamescope | 是另一条产品路线，不是本项目的修补 |

---

## 7. 复盘：如果重来一次

1. **先验证控制权，再投入实现**：CEF 是否可被注入/替换，应该在第 1 天用一小时的实验回答，而不是
   在通道做完之后；
2. **识别"供应商闭环"**：Steam 客户端会校验并还原自己的文件，这是设计而非 bug，任何依赖改文件的
   方案都应立即止损；
3. **区分"驱动能力"与"消费方意愿"**：PanVK 完全可用，但消费方（CEF 的 GPU 进程）选择不用；
4. **把实验证据落盘**：本次所有关键结论都来自 `/proc/<pid>/maps`、`cef_log.txt`、自写探针，而不是
   猜测——这让"此路不通"的结论可以被复核。

---

## 8. 复现方式

- 驱动与 WSI：`vulkaninfo`、`vkcube`、`dri3probe`（`tools/xtools/`）；
- 通道：`GpuChannel`（`app/src/main/cpp/waylandcomp/src/gpu_channel.c`）+ `tools/gpuchannel/gpuclient.c`；
- 来宾 GL 桩：`tools/gpustub/`；
- CEF 侧证据：`/proc/<gpu-process>/maps`、`~/.local/share/Steam/logs/cef_log.txt`、
  `steamwebhelper` 的命令行。

相关设计文档：[UI-ACCELERATION.md](UI-ACCELERATION.md)。
