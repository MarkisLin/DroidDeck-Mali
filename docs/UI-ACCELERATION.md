# UI 硬件加速：自建 GPU 通道的设计

## 1. 问题

Steam UI 由 CEF 渲染。在这台设备上它只能用软件光栅（Skia + llvmpipe），原因是一条链：

* 无 root → 没有 DRM render node；
* 没有 render node → X11 没有 DRI3，Wayland 没有 dmabuf 导出；
* CEF 的 GPU 合成需要 DRI3（`cef_log.txt`: `dri3 extension not supported`），没有就退回共享内存；
* Mesa EGL 在 X11/Wayland 平台上没有硬件驱动（Zink 只在我称的 Device/离屏平台能拿到 PanVK）。

实测（见 `docs/DRIVER.md` 与实验记录）：

```
dri3probe:  DRI3 ABSENT, DRI2 ABSENT, PRESENT ABSENT
eglinfo:    X11/Wayland/Surfaceless = llvmpipe; Device platform = zink Vulkan (PanVK)
vkcube:     Vulkan 呈现可用（PanVK xcb WSI 正常）
```

所以缺的不是驱动能力，而是一条**把宿主机 GPU 渲染结果交给合成器**的通道。

## 2. 目标

让来宾里的 GL/GLES 客户端（CEF 的 GPU 进程）把渲染交给宿主机（Android 进程，能用 Mali 厂商
GL/EGL 与 gralloc），结果以零拷贝 buffer 交给合成器。

## 3. 架构

```
来宾（glibc, proot）
  Steam / CEF GPU 进程
    → libEGL/libGLESv2 桩（本项目提供）
    → 序列化 GL 命令 → unix socket（abstract 或 /dev/socket 风格路径）

宿主机（Android app 进程，bionic）
  → 通道服务：反序列化 → 厂商 EGL/GLES 渲染 → AHardwareBuffer
  → 合成器直接导入该 AHardwareBuffer（零拷贝）

控制面：握手（版本/能力/分辨率/格式），每帧 present 通知
```

关键点：

* 宿主机进程本来就在用 Vulkan 合成，导入 `AHardwareBuffer` 是合成器的常规能力；
* 来宾侧不需要 DRM、不需要 dmabuf 导出——buffer 由宿主分配、宿主持有；
* 协议参考 gfxstream（Android 模拟器的 GPU 栈，开源）而不是从零发明。

## 4. 协议草案

传输：`SOCK_SEQPACKET` unix socket，fd 通过 `SCM_RIGHTS` 传递。

消息：

```
HELLO      { version, gl_es_version, max_texture, max_vp }        guest → host
HELLO_ACK  { status, caps }                                       host → guest
ALLOC      { w, h, format, usage }                                 guest → host
ALLOC_ACK  { buffer_id, fd }                                       host → guest
GL         { opcode, args... }                                     guest → host
FLUSH      { }                                                     guest → host
PRESENT    { buffer_id, seq }                                      guest → host
RELEASE    { buffer_id }                                           host → guest（缓冲可复用）
```

第一版只实现：`HELLO`、`ALLOC`、`GL` 的一个子集（清屏、三角形、纹理上传、FBO blit）、`FLUSH`、
`PRESENT`。

## 5. 里程碑

**M0 通道（1 周）**
- 宿主机 socket 服务 + 来宾 C 客户端；
- 握手、fd 传递、心跳；
- 验收：来宾打印宿主返回的能力串，宿主收到并回应。
- **状态：已完成**（`GpuChannel` / `tools/gpuchannel/gpuclient.c`，握手返回
  `gles=3.2;ahb=1;transport=abstract;version=1`）。

**M1 离屏渲染（2 周）**
- 来宾最小 EGL 桩：`eglGetDisplay/eglInitialize/eglCreateContext/eglMakeCurrent/eglCreateWindowSurface`；
- `glClear` + `glDrawArrays` 转发到宿主厂商 GL；
- 验收：来宾调用即宿主渲染出颜色，`PRESENT` 后合成器显示该 buffer（先全屏一块纯色）。
- **状态：传输层已完成**：宿主 `AHardwareBuffer_allocate` 成功，fd 通过 `SCM_RIGHTS` 交给来宾
  （`alloc: status=0 1280x720 stride=1280 format=1`）。注意本机 `AHardwareBuffer_getNativeHandle`
  的 ABI 是**单参数返回 handle 指针**（不是 NDK 文档里的 `int (buf, &handle)`），需用
  `dlopen("libandroid.so")` + 单参数签名调用。
- 宿主已能对 AHB 加锁写像素（`AHardwareBuffer_lock` + 填绿色成功）；来宾对同一 fd `mmap`
  返回 `ENODEV`（gralloc dma-buf 不暴露 mmap），因此**像素只能由宿主侧读取/呈现**，来宾不回读。
- **端到端已跑通**：来宾 `ALLOC` → 宿主分配 AHB 并写像素 → `SessionActivity` 通过
  `SurfaceControl.Transaction#setBuffer` 把该 AHB 作为探针层显示，屏幕上出现绿色矩形
  （`gpu probe layer shown 1280x720`）。下一步把 CPU 填色换成厂商 GL 渲染，并把探针层换成
  合成器导入。

**M2 GLES 子集（3–4 周）**
- CEF GPU 进程所需的最小调用集：shader 编译/链接、VAO/VBO、纹理、FBO、`glBlitFramebuffer`；
- 验收：CEF 的 GPU 进程能用桩初始化（`cef_log.txt` 无 DRI3 相关失败），UI 出现且非黑屏。

**M3 接入与性能（2–3 周）**
- 接入 CEF：`-cef-ozone-platform=wayland` 或保持 X11，但 GL 走桩；
- 验收：UI fps ≥ 30、CPU 占用显著低于软件光栅、连续 30 分钟不崩。

## 6. 风险

* **协议面太大**：GL 有几百个入口，第一版必须严格限定子集，否则项目失控；
* **CEF 的 GL 用法不透明**：需要用它自己的日志驱动实现顺序；
* **同步语义**：GL 是异步的，跨进程要实现 fence/同步，否则画面撕裂或卡死；
* **零拷贝未必一次成功**：先允许一次拷贝（AHB → 合成器纹理），再优化；
* **工作量**：M0–M3 乐观估计 2 个月，且需要持续的真机回归。

## 7. 备选：复用 gfxstream

gfxstream 已经有成熟的 guest 编码器和 host 渲染器。若时间优先，可以：

* 取 gfxstream 的协议与 host renderer 代码，把传输层从 virtio-gpu 换成 unix socket；
* 来宾侧用 Mesa 的 gfxstream 驱动（若可用）或直接用其 guest 库。

代价是引入一个较大的第三方代码库，但省掉 M2 的大部分协议实现。
