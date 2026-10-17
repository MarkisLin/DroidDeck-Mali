# 驱动是怎么来的：一段失败、复盘与重建的记录

> TL;DR (English): This document explains how the Mali PanVK driver for MT8799/G925 came to be, what
> we got wrong, what we learned, and what is still broken. It is written so that someone else can
> pick up the work: the build recipe is in `driver/`, the display path is in the main README, and the
> open problems are listed at the end. Contributions are welcome.

这份文档不是"成果展示"，而是一份**踩坑记录**。项目现在能跑，是因为中间有几次彻底的失败把
方向纠正了过来；把这些写出来，比只写"怎么用"更有用。

---

## 1. 起点：为什么非要自己编驱动

设备是 Redmi K Pad（MT8799 / Mali-G925-Immortalis MC12 / Android 16 / bootloader 锁定 / 无 root）。
DroidDeck 上游明确不支持 Mali，而这块 GPU 上可行的路只剩一条：

- 厂商的 `vulkan.mali.so` 是闭源 DDK，只服务厂商自己的栈；
- 没有 root，没有 DRM render node，panthor（上游新的 Mali 内核驱动）用不了；
- 社区只有 PanVK 的 **kbase 用户态**实验路径，且官方 profile 只做到 `g720-v12`。

于是只能交叉编译 Mesa 的 PanVK（kbase 后端）。

## 2. 第一版：移植 + 假 meta（走了弯路）

第一版其实没有"写驱动"，只做了三件事：

1. 拿上游 `g615-v11-csf-v0.1.0-beta.4` 的 glibc 产物，二进制一个字节没改；
2. 自己写 `meta.json`，声称 `maliProfile: g925-v13-csf`、`panArch: 13`、UAPI 1.30——而它的
   真实身份是 G615 / pan_arch 11 / UAPI 1.21；
3. 所谓"自编 v13"是把 `csf-v11` 补丁族 `cp -r` 成 `csf-v13`，再自造一个 profile，同一 commit 编。

结果：能出首帧、能进 Big Picture，但会 wedge；而且劣化很快（110 秒 → 22 秒 → 11 秒）。

**代价**：假 meta 让后续所有判断都建立在错误前提上；照抄补丁集让"换 v13"这个主攻方向从实验
设计上就注定无效。这是本项目最贵的一课。

## 3. 根因：固件静默卡死，而不是"arch 错配"

挂死时驱动打出的诊断（v11 移植版与自编 v13 **逐字节相同**）：

```
kbase: timeout on subqueue 0: seqno 9, marks pre/post-call/post-wait 0x0/0x0/0x0,
       stream progress 0x0, insert 1920, extract 1840, error 0x0
```

- `extract` 停在最后一条 entry 的**内部**：固件读到一半停了；
- `error 0x0`：没有 fault，没有上报，是静默卡死；
- 两份完全不同的代码在同一 job、同一环偏移卡死 → 触发点是一条确定的指令流形状，与 shader
  架构版本无关。

结构原因：GPU 软件栈是**配对封闭系统**（厂商用户态 ↔ 内核 kbase ↔ MCU 固件）。社区逆向的
kbase 用户态能过 ioctl 层，但固件不认识这种 ring 语义。

**教训**：`marks 全 0` 一度被我当成"固件连开始都没开始"的证据，后来在补丁注释里发现这些
mark 是 `PANVK_DEBUG=kbase_diag` 才写的插桩——默认就是 0。**仪表没开，读数不能当证据。**

## 4. 方法论转折：先把"判据"立起来

被问"你写驱动的思路是什么"之后，才被迫承认四个问题：没有 ground truth、元数据在撒谎、
把"没有报错"当成功、在脏 GPU 上做 A/B。

之后定下的纪律，后来每一条都救过场：

1. **补丁集必须锁**：`patchSeriesId` 对齐 `sources.lock`，不许手动拼补丁；
2. **干净基线**：重启后第一场；一次只改一个变量；
3. **判据可核对**：用例级 PASS、帧上屏、像素读回，绝不用"没有报错"；
4. **先证伪再押注**：环签名对比这种廉价实验要先做。

配套还写了**裸 CSF 探针**（不经过 Mesa/Vulkan/Steam，直接 open `/dev/mali0`、建队列、kick、
读回 `0xDEADBEEF`）。它证明了两件事：固件和 kbase 是健康的；"只能重启手机"是错的——进程
死亡即回收上下文，真正坏的是用户态命令流。

## 5. 真正的构建闭环

- 环境：WSL2（Ubuntu 24.04）+ `gcc-aarch64-linux-gnu` + meson + ninja；
- 源码：Mesa `5a07217f` + 上游 `csf-v11` 系列（179 个补丁，零 drift）；
- 编译：50 秒一版（24 核），产物 `libvulkan_panfrost.so`（glibc, aarch64）；
- 测试：adb 直接复刻 App 的 proot，在来宾里跑 44+ 个 Vulkan 测试程序，判据是
  `*_FAILS=N`、`mismatch=N`、`RESULT PASS`、读回 RGBA；
- 两个本地补丁：**P1 看门狗续期**（判丢前检查 extract/seqno 是否仍在推进，推进则续期，上限
  12 次）和 **P2 核心命令别名**（Vulkan 1.4 把 `vkCmdBindDescriptorSets2` 提升为核心命令，
  PanVK 只导出 KHR 拼写，补别名表）。

## 6. 关键判定：有三个"失败"其实不是驱动的错

1. **`vkd3d_heap` 超时**：测试在 `fork()` 之前建了 instance，子进程继承内存但不继承磁盘缓存
   的 4 个线程，销毁时 `util_queue_destroy` join 幽灵线程，永久挂起。三方对照（fork+缓存=挂、
   fork+关缓存=过、不 fork=过）证明是**测试框架 bug**。
2. **`triangle_const` / `triangle_vb` 画错**：两个测试用的着色器完全相同，只是采样点不同；
   Vulkan 的 NDC `y=-1` 对应帧缓冲第 0 行，失败的两个测试照抄了 OpenGL 的上下约定。**上游测试
   的期望值写错了。**
3. **`p8-common-gates` SIGSEGV**：beta.4 正常，RC3 起崩，崩点每次略不同（内存破坏）。排除了
   设备创建、扩展枚举、命令导出，**至今未定位**——这是目前唯一的开放性驱动线索。

## 7. 现在的缺点（诚实清单）

- **P1 补丁的收益未被证明**：当前驱动已经不再 wedge，没有复现器；它只是保险。
- **`p8` SIGSEGV 未解**：RC3 之后引入的回归候选，需要更细的插桩或上游介入。
- **bionic 版没有产出**：PanProbe 在 Android 进程内加载，需要 Linux x86_64 的 NDK 工具链，
  原构建机没有，所以只能跑 glibc 测试。
- **G925 从未被上游验证**：`arch 13` 在 MT6991 上有过 37/37 的记录，但 MT8799 的
  `mali_csffw.bin` 不同；上游的验证矩阵里没有这台机器。
- **没有 dmabuf/GBM 导出**：所以显示只能走 Xvfb + 拷贝，CEF 拿不到硬件 GL，UI 只有 13–16 fps。
- **测试覆盖偏斜**：45→67 项里大量是上游用例；真正针对 kbase/MTK 的契约测试（结构体布局、
  SLC、寄存器数）还不够系统。

## 8. 想请社区一起做的事

按价值排序：

1. **复现并定位 `p8` 崩溃**：需要一份干净基线上的最小复现（设备、驱动 sha256、测试命令）。
2. **验证 P1**：找一个仍会 wedge 的构建/场景，对比开/关 P1 的行为。
3. **把 bionic 构建补上**：有了 Linux NDK 就能编 PanProbe 用的版本，测试面会大很多。
4. **kbase 契约测试**：`CS_GET_GLB_IFACE` 的寄存器数（本机 128，参考 114，panthor 96）、
   memory layout、SLC 行为——这些比"多跑几个游戏"更能防止回归。
5. **dmabuf/GBM 导出**：一旦 PanVK 能把 buffer 导出成 dmabuf，Xwayland/CEF 的硬件路径和零拷贝
   才真正可能。
6. **给上游回报**：v13 × RC3 的结果、G925 的命名、`p8` 的复现。

## 9. 怎么复现

- 构建与打包：`driver/README.md`，脚本在 `driver/build/`，补丁在 `driver/patches/`；
- 显示链路：主 README 的 “Why a separate display pipeline”；
- 应用侧：`src` 里的 `XvfbHost`、`WindowTools`、`droiddeck-steamos` 的 xvfb 分支。

如果你在这条路上踩到新的坑，欢迎开 issue 或 PR——尤其是带**可核对判据**的复现。
