# 驱动开发说明：PanVK（kbase）在 Mali-G925 / MT8799 上

本文件说明本项目运行时驱动的构建方式、补丁内容、验证方法和配套工具。驱动源码来自 Mesa，
本项目维护的是构建配置、补丁集与验证流程。

## 1. 目标与约束

设备：红米 K Pad（25079RPDCC），MT8799（天玑 9400+），Mali-G925-Immortalis MC12（`0xd8300015`），
pan_arch 13，kbase CSF UAPI 1.30，Android 16，无 root。

约束条件决定了技术路线：

* 没有 DRM render node，无法使用 panthor/GBM 路径；
* 厂商 `vulkan.mali.so` 为闭源 DDK，不能作为通用运行时驱动；
* 可用的开源路径是 Mesa PanVK 的 **kbase** 后端（直接经 `/dev/mali0` 提交 CSF 命令流）。

## 2. 构建流程

环境：WSL2（Ubuntu 24.04），`gcc-aarch64-linux-gnu`，meson、ninja。脚本位于 `driver/build/`：

| 脚本 | 作用 |
|---|---|
| `00-env.sh` | 安装/校验交叉工具链与 arm64 multiarch |
| `10b-extract-patch-native.sh` | 解包 Mesa `5a07217f`，应用上游 `csf-v11` 补丁集 |
| `20b-host-tools.sh` | 构建主机端生成器（`mesa_clc`、`vtn_bindgen2` 等） |
| `30b-cross-build.sh` | 交叉编译 → `dist-glibc-g925/libvulkan_panfrost.so` |
| `40b-verify.sh` | ELF 架构、glibc 版本、PanVK 符号与 ICD 结构校验 |
| `60-build-tests.sh` / `63-build-all-tests.sh` | 编译 Vulkan 测试程序 |

本地补丁在交叉编译前应用（`driver/patches/`）：

* `p1-watchdog-rearm.patch`（子队列超时保护）
* `p2-core-command-aliases.patch`（Vulkan 1.4 核心命令别名）

产物打包为 `libvulkan_panfrost.so` + `icd.json` + `meta.json`，由应用的
**组件 → GPU 驱动 → 导入 .zip** 安装。

## 3. 补丁说明

**子队列超时保护。** kbase 在子队列 10 秒无进展时判定其丢失并触发 timeout。补丁在判定前检查
`extract`、`seqno` 与 `stream_progress` 是否仍在推进；若在推进则延长判定窗口（有次数上限），
避免长时间计算被误判。

**Vulkan 1.4 核心命令别名。** Vulkan 1.4 将 `vkCmdBindDescriptorSets2` 与
`vkCmdPushConstants2` 提升为核心命令，PanVK 只导出 KHR 拼写。补丁在 `panvk_GetInstanceProcAddr`
/ `panvk_GetDeviceProcAddr` 中补充核心名到 KHR 实现的映射。

## 4. 验证方法

判定标准必须可核对，不能以"没有报错"为准：

1. **设备侧探针**：PanProbe（`dev.zenithblue.panvktest`）在 Android 进程内加载驱动并跑用例，
   结果写入 `files/autorun.txt`；需要 bionic 构建。
2. **来宾侧测试**：交叉编译 `tests/**` 下的 Vulkan 程序，经 proot 运行并传入 ICD 路径；
   判据为逐用例 PASS、`*_FAILS=N`、`mismatch=N` 以及像素读回值。
3. **系统健康度**：日志中 `kbase: timeout` 与 `MESA: error` 计数应为 0；GPU 侧以
   `wayland.log` 的 `frames on screen` 为准。
4. **基线要求**：每次对比前重启设备并保留干净基线，单次只改一个变量。

## 5. 开发配套工具

| 工具 | 位置 | 说明 |
|---|---|---|
| 环境脚本 | `driver/build/00-env.sh` | 交叉工具链与依赖 |
| 补丁与构建 | `driver/build/10b…30b` | Mesa 解包、打补丁、交叉编译 |
| 校验脚本 | `driver/build/40b-verify.sh` | ELF/glibc/符号检查 |
| 测试构建 | `driver/build/60-build-tests.sh`、`63-build-all-tests.sh` | 批量编译 Vulkan 测试 |
| 测试程序 | Mesa `tests/**`（按 `63` 脚本的列表） | 逐用例验证 |
| profile | `driver/profile/g925-v13-csf-profile.json` | PanVK 设备 profile |
| 包元数据 | `driver/meta/main186_p1.json` | 驱动包 `meta.json` 模板 |
| 应用构建 | `tools/release/build-mt8799.ps1` | 生成 APK（Windows） |
| 来宾脚本哈希 | `tools/release/runtime_inputs.py` | 生成 `runtime-inputs.json` |
| 呈现与窗口 | 来宾 `/root/xpresent`、`xraise`、`xhide` | 画面呈现与窗口管理 |
| 日志 | `files/logs/<session>/` | `session.log`、`wayland.log`、`xpresent.out` |

## 6. 当前局限

* **性能偏低**：Steam 界面约 13–16 fps，游戏尚未针对该路径优化；
* Steam 的 CEF 为软件光栅，是界面帧率偏低的主要原因；
* `p8-common-gates` 在近期构建上出现 SIGSEGV，尚未定位；
* 没有 bionic 构建，PanProbe 不能加载驱动；
* 没有 dmabuf/GBM 导出，显示链路只能拷贝；
* G925/MT8799 不在上游验证矩阵内；
* 测试覆盖以通用 Vulkan 用例为主，针对 kbase 的契约测试不足。

## 7. 后续方向

1. 定位 `p8` 崩溃并给出最小复现；
2. 补齐 bionic 构建，使设备侧探针可用；
3. 建立 kbase 契约测试（寄存器数、内存布局、SLC 行为）；
4. 推进 dmabuf/GBM 导出，为 Xwayland/CEF 的硬件路径与零拷贝做准备；
5. 性能优化：降低拷贝开销、调整帧调度，提高界面与游戏帧率；
6. 将 G925/MT8799 的验证结果回报上游。
