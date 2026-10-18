# L4D2 启动即退：Proton 32 位支持问题

## 现象

《Left 4 Dead 2》（AppID 550）在库里点"开始游戏"后，Steam 显示运行中，几秒内退出，没有画面。

## 排查过程

### 1. 原始路径：原生 Linux 版 + FEX + SteamLinuxRuntime

`console_log.txt` 里的启动命令：

```
FEX-Emu/fex-compat-tool waitforexitandrun --
  SteamLinuxRuntime_soldier/_v2-entry-point --verb=waitforexitandrun --
  SteamLinuxRuntime/scout-on-soldier-entry-point-v2 --
  ./hl2.sh -game left4dead2 -steam
```

L4D2 有原生 Linux 版，Steam 默认选它。这条链依赖：

- FEX 运行 x86_64 Linux 程序；
- SteamLinuxRuntime 的 pressure-vessel（bwrap 沙箱）。

在本项目的 proot 环境里，pressure-vessel 需要 user namespace，起不来，进程几秒内退出（退出码 10）。

### 2. 强制走我们的 Proton 工具

给 550 写入映射：

```
CompatToolMapping → "550" → droiddeck-proton-arm64
```

之后启动链变为：

```
reaper → compatibilitytools.d/droiddeck-proton-arm64/droiddeck-proton waitforexitandrun
       → 'Left 4 Dead 2/left4dead2.exe' -steam
```

Windows 版文件存在（`left4dead2.exe` 约 365 KB，19:00 下载）。但游戏仍然几秒内退出。

### 3. 缺少可诊断的日志

`droiddeck-proton-run` 直接用 `os.execve` 启动 Proton，Proton 的 stderr 没有被保存，失败原因不可见。
这是本次要修的问题。

## 根因判断

L4D2 是 **32 位（i386）** 游戏，而本项目的 Proton 前缀是 `default_pfx_arm64`（64 位 ARM64 + FEX）。
丝之歌是 64 位，能正常跑；L4D2 很可能卡在 **缺少 32 位支持**（FEX 的 i386 支持 / Wine 的 32 位库）。

确认方式：读取本次修复后生成的 `/tmp/droiddeck-proton-550.log`，看是否出现
`wine: 32-bit`、`failed to load`、`i386`、`FEX` 相关错误。

## 本版本的修复

`droiddeck-proton-run` 现在会把 Proton 的 stderr 写入：

```
/tmp/droiddeck-proton-<AppID>.log
```

该日志随 APK 自动安装（`SessionFiles`），不需要更新运行时包。

## 后续可选方案

1. **补 32 位支持**：确认 Proton ARM64 是否带 i386 库；若没有，需要 FEX 的 32 位 guest 支持；
2. **原生 Linux 版绕开 SLR**：直接以 FEX 运行 `hl2.sh`，跳过 pressure-vessel；但 L4D2 的 Linux 版同样是 32 位，
   仍需 i386 支持；
3. **游戏层面**：优先验证 64 位 Windows 游戏（如丝之歌可运行），把 32 位游戏列为已知限制。

## 复现与检查

```sh
# 启动（在运行中的会话里）
/root/.local/share/Steam/steamrtarm64/steam steam://rungameid/550

# 读日志
run-as ... cat /tmp/droiddeck-proton-550.log
```
