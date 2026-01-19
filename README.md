# XZ Boards Only - 三板子精简项目

这是一个从 xz1-main 和 xz2-main 提取的精简项目，仅包含三个机器人板子。

## 项目结构

本项目包含以下三个板子：

### 1. palqiqi1 (来自 xz1)
- 位置：`main/boards/palqiqi1/`
- 来源：xz1-main/main/boards/palqiqi
- 特点：原始palqiqi配置

### 2. dog1 (来自 xz1)
- 位置：`main/boards/dog1/`
- 来源：xz1-main/main/boards/dog
- 特点：桌面四足机器人

### 3. palqiqi2 (来自 xz2)
- 位置：`main/boards/palqiqi2/`
- 来源：xz2-main/main/boards/palqiqi
- 特点：包含 SPIRAM 优化的完整配置

## 共享模块

项目保留了以下共享代码模块：

- **audio/** - 音频编解码、服务
- **display/** - LCD/OLED显示、LVGL界面
- **protocols/** - MQTT/WebSocket通信协议
- **core/** - 事件总线（EventBus）
- **led/** - LED控制
- **assets/** - 语言包和音频资源
- **boards/common/** - 板子通用基础类

## 已移除的模块

为了精简项目，以下模块已被移除（因为已被注释掉不使用）：

- ~~learning/~~ - 自适应行为学习系统
- ~~pet_system~~ - 宠物系统

## 编译说明

### 1. 配置板子类型

```bash
idf.py menuconfig
```

在菜单中选择：`Xiaozhi Assistant → Board Type`

可选择：
- Palqiqi1 (from xz1)
- Dog1 (from xz1)
- Palqiqi2 (from xz2)

### 2. 编译项目

```bash
idf.py build
```

### 3. 烧录

```bash
idf.py flash
```

## 硬件配置

每个板子的硬件配置都在各自的目录下：

- `boards/palqiqi1/config.h` - palqiqi1 的硬件引脚和参数
- `boards/dog1/config.h` - dog1 的硬件配置
- `boards/palqiqi2/config.h` - palqiqi2 的硬件配置

**更换硬件**：如需更换某个板子的屏幕、麦克风等硬件，只需修改该板子目录下的 `config.h` 文件，不会影响其他板子。

## 项目依赖

- ESP-IDF >= 5.4.0
- 所有依赖已在 `dependencies.lock` 和 `main/idf_component.yml` 中定义

## 注意事项

1. 三个板子共享 audio、display 等模块，但硬件配置完全独立
2. 不包含其他70+个板子的配置
3. 严格按源项目提取，未添加新功能
4. 保留了事件总线（EventBus），用于对话事件通知

## 文件说明

- `CMakeLists.txt` - 项目根配置
- `main/CMakeLists.txt` - 主组件构建配置（仅包含3个板子）
- `main/Kconfig.projbuild` - 配置菜单（仅包含3个板子选项）
- `partitions/v1/16m.csv` - 16MB Flash 分区表
- `scripts/` - 构建脚本

## 版本信息

- 提取时间：2026-01-19
- 源项目：xz1-main, xz2-main
- ESP-IDF 版本要求：>= 5.4.0


