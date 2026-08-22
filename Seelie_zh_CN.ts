<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="zh_CN">
<context>
    <name>CharacterPackManager</name>
    <message>
        <location filename="src/CharacterPackManager.cpp" line="223"/>
        <source>Could not open the pack archive. It may be corrupt or unreadable.</source>
        <translation>无法打开包压缩文件。可能已损坏或无法读取。</translation>
    </message>
    <message>
        <location filename="src/CharacterPackManager.cpp" line="231"/>
        <source>Archive is missing manifest.json — not a valid Seelie pack.</source>
        <translation>压缩包缺少 manifest.json — 不是有效的 Seelie 包。</translation>
    </message>
    <message>
        <location filename="src/CharacterPackManager.cpp" line="240"/>
        <source>manifest.json is unreadable or unreasonably large (&gt;10 MB).</source>
        <translation>manifest.json 无法读取或过大 (&gt;10 MB)。</translation>
    </message>
    <message>
        <location filename="src/CharacterPackManager.cpp" line="247"/>
        <source>Could not read manifest.json from the archive.</source>
        <translation>无法从压缩包中读取 manifest.json。</translation>
    </message>
    <message>
        <location filename="src/CharacterPackManager.cpp" line="258"/>
        <source>manifest.json is not valid JSON.</source>
        <translation>manifest.json 不是有效的 JSON。</translation>
    </message>
    <message>
        <location filename="src/CharacterPackManager.cpp" line="266"/>
        <source>manifest.json is missing the required &quot;id&quot; field.</source>
        <translation>manifest.json 缺少必需的 &quot;id&quot; 字段。</translation>
    </message>
    <message>
        <location filename="src/CharacterPackManager.cpp" line="282"/>
        <source>Could not create the staging directory at %1. Check disk space and permissions.</source>
        <translation>无法在 %1 创建暂存目录。请检查磁盘空间和权限。</translation>
    </message>
    <message>
        <location filename="src/CharacterPackManager.cpp" line="300"/>
        <source>Archive contains an unsafe path (&quot;%1&quot;) and was rejected.</source>
        <translation>压缩包含有不安全路径 (&quot;%1&quot;) 已被拒绝。</translation>
    </message>
    <message>
        <location filename="src/CharacterPackManager.cpp" line="313"/>
        <source>Failed to extract &quot;%1&quot; — disk full or permission denied.</source>
        <translation>解压 &quot;%1&quot; 失败 — 磁盘已满或权限不足。</translation>
    </message>
    <message>
        <location filename="src/CharacterPackManager.cpp" line="331"/>
        <source>Could not remove the previously installed copy at %1.</source>
        <translation>无法移除位于 %1 的旧安装版本。</translation>
    </message>
    <message>
        <location filename="src/CharacterPackManager.cpp" line="337"/>
        <source>Could not finalise the install at %1.</source>
        <translation>无法在 %1 完成安装。</translation>
    </message>
    <message>
        <location filename="src/CharacterPackManager.cpp" line="357"/>
        <source>Pack &quot;%1&quot; is not installed.</source>
        <translation>包 &quot;%1&quot; 尚未安装。</translation>
    </message>
    <message>
        <location filename="src/CharacterPackManager.cpp" line="366"/>
        <source>Cannot uninstall built-in pack &quot;%1&quot;.</source>
        <translation>无法卸载内置包 &quot;%1&quot;。</translation>
    </message>
    <message>
        <location filename="src/CharacterPackManager.cpp" line="373"/>
        <source>Switch to a different pack before uninstalling &quot;%1&quot;.</source>
        <translation>卸载 &quot;%1&quot; 前请先切换到其他包。</translation>
    </message>
    <message>
        <location filename="src/CharacterPackManager.cpp" line="381"/>
        <source>Could not delete %1 — permission denied or file in use.</source>
        <translation>无法删除 %1 — 权限不足或文件被占用。</translation>
    </message>
    <message>
        <location filename="src/CharacterPackManager.cpp" line="388"/>
        <source>Could not delete %1 — some files may be in use or read-only.</source>
        <translation>无法删除 %1 — 部分文件可能正在使用或为只读。</translation>
    </message>
</context>
<context>
    <name>ConfigExporter</name>
    <message>
        <source>No configuration files found to export.</source>
        <translation type="vanished">未找到可导出的配置文件。</translation>
    </message>
    <message>
        <source>Failed to create temporary staging directory.</source>
        <translation type="vanished">创建临时暂存目录失败。</translation>
    </message>
    <message>
        <source>Failed to copy %1 to staging area.</source>
        <translation type="vanished">复制 %1 到暂存区域失败。</translation>
    </message>
    <message>
        <source>Failed to write manifest.json.</source>
        <translation type="vanished">写入 manifest.json 失败。</translation>
    </message>
    <message>
        <source>Failed to start ZIP process. Is &apos;zip&apos; installed?</source>
        <translation type="vanished">启动 ZIP 进程失败。是否已安装 &apos;zip&apos;？</translation>
    </message>
    <message>
        <source>ZIP process timed out.</source>
        <translation type="vanished">ZIP 进程超时。</translation>
    </message>
    <message>
        <source>ZIP creation failed: %1</source>
        <translation type="vanished">ZIP 创建失败: %1</translation>
    </message>
</context>
<context>
    <name>ConfigImporter</name>
    <message>
        <source>Archive file not found.</source>
        <translation type="vanished">未找到压缩包文件。</translation>
    </message>
    <message>
        <source>Failed to create temporary directory.</source>
        <translation type="vanished">创建临时目录失败。</translation>
    </message>
    <message>
        <source>Failed to start unzip process. Is &apos;unzip&apos; installed?</source>
        <translation type="vanished">启动解压进程失败。是否已安装 &apos;unzip&apos;？</translation>
    </message>
    <message>
        <source>Unzip process timed out.</source>
        <translation type="vanished">解压进程超时。</translation>
    </message>
    <message>
        <source>Archive extraction failed: %1</source>
        <translation type="vanished">压缩包解压失败: %1</translation>
    </message>
    <message>
        <source>Invalid archive: manifest.json not found.</source>
        <translation type="vanished">无效的压缩包: 未找到 manifest.json。</translation>
    </message>
    <message>
        <source>Cannot read manifest.json.</source>
        <translation type="vanished">无法读取 manifest.json。</translation>
    </message>
    <message>
        <source>Invalid manifest: missing required fields.</source>
        <translation type="vanished">无效的清单: 缺少必填字段。</translation>
    </message>
    <message>
        <source>Archive schema version %1 is not supported. Please update Seelie.</source>
        <translation type="vanished">不支持压缩包架构版本 %1。请更新 Seelie。</translation>
    </message>
    <message>
        <source>Invalid archive: config/Seelie.ini not found.</source>
        <translation type="vanished">无效的压缩包: 未找到 config/Seelie.ini。</translation>
    </message>
    <message>
        <source>Failed to create temporary extraction directory.</source>
        <translation type="vanished">创建临时解压目录失败。</translation>
    </message>
    <message>
        <source>Failed to start unzip process.</source>
        <translation type="vanished">启动解压进程失败。</translation>
    </message>
    <message>
        <source>Unzip timed out.</source>
        <translation type="vanished">解压超时。</translation>
    </message>
    <message>
        <source>Extraction failed: %1</source>
        <translation type="vanished">解压失败: %1</translation>
    </message>
    <message>
        <source>Failed to backup existing config file.</source>
        <translation type="vanished">备份现有配置文件失败。</translation>
    </message>
    <message>
        <source>Failed to backup existing memory database.</source>
        <translation type="vanished">备份现有记忆数据库失败。</translation>
    </message>
    <message>
        <source>Failed to copy new configuration files.</source>
        <translation type="vanished">复制新配置文件失败。</translation>
    </message>
</context>
<context>
    <name>ECGWidget</name>
    <message>
        <location filename="src/ECGWidget.cpp" line="248"/>
        <source>ECG MONITOR</source>
        <translation>心电监护</translation>
    </message>
    <message>
        <location filename="src/ECGWidget.cpp" line="344"/>
        <source>STANDBY</source>
        <translation>待机</translation>
    </message>
    <message>
        <location filename="src/ECGWidget.cpp" line="348"/>
        <source>ASYSTOLE</source>
        <translation>心搏停止</translation>
    </message>
    <message>
        <location filename="src/ECGWidget.cpp" line="351"/>
        <source>HR %1 BPM</source>
        <translation>心率 %1 BPM</translation>
    </message>
    <message>
        <location filename="src/ECGWidget.cpp" line="361"/>
        <source> (MUTED)</source>
        <translation>（静音）</translation>
    </message>
    <message>
        <location filename="src/ECGWidget.cpp" line="417"/>
        <source>PWR</source>
        <translation>PWR</translation>
    </message>
    <message>
        <location filename="src/ECGWidget.cpp" line="418"/>
        <source>ALM</source>
        <translation>ALM</translation>
    </message>
</context>
<context>
    <name>EditLLMProfileDialog</name>
    <message>
        <location filename="src/EditLLMProfileDialog.cpp" line="10"/>
        <source>LLM Profile</source>
        <translation>LLM 配置</translation>
    </message>
    <message>
        <location filename="src/EditLLMProfileDialog.cpp" line="23"/>
        <source>OpenAI Chat</source>
        <translation>OpenAI Chat</translation>
    </message>
    <message>
        <location filename="src/EditLLMProfileDialog.cpp" line="24"/>
        <source>OpenAI Responses</source>
        <translation>OpenAI Responses</translation>
    </message>
    <message>
        <location filename="src/EditLLMProfileDialog.cpp" line="25"/>
        <source>Anthropic Messages</source>
        <translation>Anthropic 消息</translation>
    </message>
    <message>
        <location filename="src/EditLLMProfileDialog.cpp" line="35"/>
        <source>Name:</source>
        <translation>名称:</translation>
    </message>
    <message>
        <location filename="src/EditLLMProfileDialog.cpp" line="36"/>
        <source>Protocol:</source>
        <translation>协议:</translation>
    </message>
    <message>
        <location filename="src/EditLLMProfileDialog.cpp" line="37"/>
        <source>Base URL:</source>
        <translation>基础地址:</translation>
    </message>
    <message>
        <location filename="src/EditLLMProfileDialog.cpp" line="38"/>
        <source>API key:</source>
        <translation>API 密钥:</translation>
    </message>
    <message>
        <location filename="src/EditLLMProfileDialog.cpp" line="39"/>
        <source>Model:</source>
        <translation>模型:</translation>
    </message>
</context>
<context>
    <name>IdleBehaviorEngine</name>
    <message>
        <location filename="src/IdleBehaviorEngine.cpp" line="128"/>
        <source>Idle musing</source>
        <translation>随想</translation>
    </message>
</context>
<context>
    <name>MainWindow</name>
    <message>
        <location filename="src/mainwindow.cpp" line="631"/>
        <source>Hide</source>
        <translation>隐藏</translation>
    </message>
    <message>
        <location filename="src/mainwindow.cpp" line="631"/>
        <source>Show</source>
        <translation>显示</translation>
    </message>
    <message>
        <location filename="src/mainwindow.cpp" line="642"/>
        <source>About</source>
        <translation>关于</translation>
    </message>
    <message>
        <location filename="src/mainwindow.cpp" line="636"/>
        <source>Settings</source>
        <translation>设置</translation>
    </message>
    <message>
        <location filename="src/mainwindow.cpp" line="662"/>
        <source>Quit</source>
        <translation>退出</translation>
    </message>
    <message>
        <location filename="src/mainwindow.cpp" line="991"/>
        <source>Gaming Mode activated!</source>
        <translation>游戏模式已启用！</translation>
    </message>
    <message>
        <location filename="src/mainwindow.cpp" line="992"/>
        <source>Seelie will hide when fullscreen apps are detected.</source>
        <translation>检测到全屏应用时 Seelie 会自动隐藏。</translation>
    </message>
    <message>
        <location filename="src/mainwindow.cpp" line="825"/>
        <source>Export Configuration</source>
        <translation>导出配置</translation>
    </message>
    <message>
        <location filename="src/mainwindow.cpp" line="840"/>
        <source>Export Failed</source>
        <translation>导出失败</translation>
    </message>
    <message>
        <location filename="src/mainwindow.cpp" line="848"/>
        <source>Export Complete</source>
        <translation>导出完成</translation>
    </message>
    <message>
        <location filename="src/mainwindow.cpp" line="849"/>
        <source>Configuration exported to:
%1</source>
        <translation>配置已导出到:
%1</translation>
    </message>
    <message>
        <location filename="src/mainwindow.cpp" line="858"/>
        <source>Import Configuration</source>
        <translation>导入配置</translation>
    </message>
    <message>
        <location filename="src/mainwindow.cpp" line="828"/>
        <location filename="src/mainwindow.cpp" line="860"/>
        <source>ZIP Archives (*.zip)</source>
        <translation>ZIP 压缩包 (*.zip)</translation>
    </message>
    <message>
        <location filename="src/mainwindow.cpp" line="871"/>
        <source>Invalid Archive</source>
        <translation>无效的压缩包</translation>
    </message>
    <message>
        <location filename="src/mainwindow.cpp" line="879"/>
        <source>Version Mismatch</source>
        <translation>版本不匹配</translation>
    </message>
    <message>
        <location filename="src/mainwindow.cpp" line="880"/>
        <source>This archive was created with Seelie %1, but you are running %2.
Importing may cause unexpected behavior. Continue?</source>
        <translation>此压缩包由 Seelie %1 创建，但你正在运行 %2。
导入可能导致意外行为。是否继续？</translation>
    </message>
    <message>
        <location filename="src/mainwindow.cpp" line="889"/>
        <source>Confirm Import</source>
        <translation>确认导入</translation>
    </message>
    <message>
        <location filename="src/mainwindow.cpp" line="890"/>
        <source>This will replace your current configuration with the archive contents.
A backup of your current config will be created automatically.
Seelie will need to be restarted for changes to take full effect.

Continue?</source>
        <translation>这将用压缩包内容替换当前配置。
当前配置的备份将自动创建。
Seelie 需要重启才能使更改完全生效。

是否继续？</translation>
    </message>
    <message>
        <location filename="src/mainwindow.cpp" line="902"/>
        <source>Import Complete</source>
        <translation>导入完成</translation>
    </message>
    <message>
        <location filename="src/mainwindow.cpp" line="903"/>
        <source>Configuration imported successfully.
Please restart Seelie for changes to take full effect.</source>
        <translation>配置导入成功。
请重启 Seelie 以使更改完全生效。</translation>
    </message>
    <message>
        <location filename="src/mainwindow.cpp" line="909"/>
        <source>Import Failed</source>
        <translation>导入失败</translation>
    </message>
    <message>
        <location filename="src/mainwindow.cpp" line="1220"/>
        <source>%1h %2m, %3 events</source>
        <translation>%1 小时 %2 分，%3 个事件</translation>
    </message>
    <message>
        <location filename="src/mainwindow.cpp" line="1170"/>
        <source>First pet!</source>
        <translation>第一次抚摸！</translation>
    </message>
    <message>
        <location filename="src/mainwindow.cpp" line="1171"/>
        <source>You petted Seelie for the first time.</source>
        <translation>你第一次抚摸了 Seelie。</translation>
    </message>
    <message>
        <location filename="src/mainwindow.cpp" line="1191"/>
        <source>First toss!</source>
        <translation>第一次抛出！</translation>
    </message>
    <message>
        <location filename="src/mainwindow.cpp" line="1192"/>
        <source>You threw Seelie across the screen.</source>
        <translation>你把 Seelie 扔了出去。</translation>
    </message>
    <message>
        <location filename="src/mainwindow.cpp" line="639"/>
        <source>What do you remember?</source>
        <translation>你还记得吗？</translation>
    </message>
</context>
<context>
    <name>PackCategories</name>
    <message>
        <location filename="src/SystemTray.cpp" line="317"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1403"/>
        <source>Standalone</source>
        <translation>独立</translation>
    </message>
    <message>
        <location filename="src/SystemTray.cpp" line="318"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1404"/>
        <source>Azur Lane</source>
        <translation>碧蓝航线</translation>
    </message>
    <message>
        <location filename="src/SystemTray.cpp" line="319"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1405"/>
        <source>Girls&apos; Frontline</source>
        <translation>少女前线</translation>
    </message>
    <message>
        <location filename="src/SystemTray.cpp" line="320"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1406"/>
        <source>Idol Dimension</source>
        <translation>偶像次元</translation>
    </message>
    <message>
        <location filename="src/SystemTray.cpp" line="321"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1407"/>
        <source>Konosuba</source>
        <translation>为美好的世界献上祝福！</translation>
    </message>
    <message>
        <location filename="src/SystemTray.cpp" line="322"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1408"/>
        <source>Live2D Samples</source>
        <translation>Live2D 样例</translation>
    </message>
</context>
<context>
    <name>PackManagerWidget</name>
    <message>
        <location filename="src/PackManagerWidget.cpp" line="82"/>
        <source>Models Management</source>
        <translation>模型管理</translation>
    </message>
    <message>
        <location filename="src/PackManagerWidget.cpp" line="87"/>
        <source>×</source>
        <translation>×</translation>
    </message>
    <message>
        <location filename="src/PackManagerWidget.cpp" line="107"/>
        <source>Add</source>
        <translation>添加</translation>
    </message>
    <message>
        <location filename="src/PackManagerWidget.cpp" line="115"/>
        <source>Delete</source>
        <translation>删除</translation>
    </message>
    <message>
        <location filename="src/PackManagerWidget.cpp" line="342"/>
        <source>Pack files (*.spk *.codex-pet)</source>
        <translation>模型文件 (*.spk *.codex-pet)</translation>
    </message>
    <message>
        <location filename="src/PackManagerWidget.cpp" line="343"/>
        <source>SPK files (*.spk)</source>
        <translation>SPK 文件 (*.spk)</translation>
    </message>
    <message>
        <location filename="src/PackManagerWidget.cpp" line="344"/>
        <source>Codex Pet files (*.codex-pet)</source>
        <translation>Codex 宠物文件 (*.codex-pet)</translation>
    </message>
    <message>
        <location filename="src/PackManagerWidget.cpp" line="345"/>
        <source>All files (*)</source>
        <translation>所有文件 (*)</translation>
    </message>
    <message>
        <location filename="src/PackManagerWidget.cpp" line="349"/>
        <source>Select Pack Files to Install</source>
        <translation>选择要安装的模型文件</translation>
    </message>
    <message>
        <location filename="src/PackManagerWidget.cpp" line="384"/>
        <source>Successfully installed %1 pack(s).</source>
        <translation>成功安装 %1 个模型。</translation>
    </message>
    <message>
        <location filename="src/PackManagerWidget.cpp" line="390"/>
        <source>Installation Complete</source>
        <translation>安装完成</translation>
    </message>
    <message>
        <location filename="src/PackManagerWidget.cpp" line="393"/>
        <source>Installation Failed</source>
        <translation>安装失败</translation>
    </message>
    <message>
        <location filename="src/PackManagerWidget.cpp" line="386"/>
        <source>Failed to install %1 file(s):</source>
        <translation>安装失败 %1 个文件:</translation>
    </message>
    <message>
        <location filename="src/PackManagerWidget.cpp" line="404"/>
        <source>No Selection</source>
        <translation>未选择</translation>
    </message>
    <message>
        <location filename="src/PackManagerWidget.cpp" line="405"/>
        <source>Please select one or more packs to delete.</source>
        <translation>请选择一个或多个模型进行删除。</translation>
    </message>
    <message>
        <location filename="src/PackManagerWidget.cpp" line="424"/>
        <source>Cannot Delete Active Pet</source>
        <translation>无法删除当前宠物</translation>
    </message>
    <message>
        <location filename="src/PackManagerWidget.cpp" line="425"/>
        <source>&quot;%1&quot; is currently in use and cannot be deleted.

Please switch to another pet first.</source>
        <translation>&quot;%1&quot; 当前正在使用中，无法删除。

请先切换到其他宠物。</translation>
    </message>
    <message>
        <location filename="src/PackManagerWidget.cpp" line="434"/>
        <source>Delete Packs</source>
        <translation>删除模型</translation>
    </message>
    <message>
        <location filename="src/PackManagerWidget.cpp" line="435"/>
        <source>Are you sure you want to delete the following %1 pack(s)?

%2

This action cannot be undone.</source>
        <translation>确定要删除以下 %1 个模型吗？

%2

此操作不可撤销。</translation>
    </message>
    <message>
        <location filename="src/PackManagerWidget.cpp" line="465"/>
        <source>Delete Complete</source>
        <translation>删除完成</translation>
    </message>
    <message>
        <location filename="src/PackManagerWidget.cpp" line="466"/>
        <location filename="src/PackManagerWidget.cpp" line="468"/>
        <source>Successfully deleted %1 pack(s).</source>
        <translation>成功删除 %1 个模型。</translation>
    </message>
    <message>
        <location filename="src/PackManagerWidget.cpp" line="470"/>
        <source>Failed to delete: %1</source>
        <translation>删除失败：%1</translation>
    </message>
    <message>
        <location filename="src/PackManagerWidget.cpp" line="471"/>
        <source>Failed to delete:
%1</source>
        <translation>删除失败:
%1</translation>
    </message>
    <message>
        <location filename="src/PackManagerWidget.cpp" line="473"/>
        <source>Delete Partial</source>
        <translation>部分删除</translation>
    </message>
    <message>
        <location filename="src/PackManagerWidget.cpp" line="494"/>
        <source>ID: %1
Path: %2</source>
        <translation>ID: %1
路径: %2</translation>
    </message>
</context>
<context>
    <name>QHotkey</name>
    <message>
        <location filename="build/_deps/qhotkey-src/QHotkey/qhotkey.cpp" line="294"/>
        <source>Failed to register %1. Error: %2</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <location filename="build/_deps/qhotkey-src/QHotkey/qhotkey.cpp" line="314"/>
        <source>Failed to unregister %1. Error: %2</source>
        <translation type="unfinished"></translation>
    </message>
</context>
<context>
    <name>QObject</name>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="75"/>
        <source>Token</source>
        <translation>密钥</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="76"/>
        <source>BaseUrl</source>
        <translation>基础地址</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="77"/>
        <source>Model</source>
        <translation>模型</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="78"/>
        <source>GroupId</source>
        <translation>Group ID</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="79"/>
        <source>Key</source>
        <translation>订阅密钥</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="80"/>
        <source>Region</source>
        <translation>区域</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="81"/>
        <source>Voice</source>
        <translation>音色</translation>
    </message>
    <message>
        <location filename="src/main.cpp" line="476"/>
        <source>Bond level up!</source>
        <translation>羁绊升级！</translation>
    </message>
    <message>
        <location filename="src/main.cpp" line="477"/>
        <source>Seelie feels closer to you (Lv %1).</source>
        <translation>Seelie 感觉和你更亲近了（Lv %1）。</translation>
    </message>
    <message>
        <location filename="src/ConfigExporter.cpp" line="35"/>
        <source>No configuration files found to export.</source>
        <translation type="unfinished">未找到可导出的配置文件。</translation>
    </message>
    <message>
        <location filename="src/ConfigExporter.cpp" line="57"/>
        <source>Failed to create temporary staging directory.</source>
        <translation type="unfinished">创建临时暂存目录失败。</translation>
    </message>
    <message>
        <location filename="src/ConfigExporter.cpp" line="77"/>
        <source>Failed to copy %1 to staging area.</source>
        <translation type="unfinished">复制 %1 到暂存区域失败。</translation>
    </message>
    <message>
        <location filename="src/ConfigExporter.cpp" line="87"/>
        <source>Failed to write manifest.json.</source>
        <translation type="unfinished">写入 manifest.json 失败。</translation>
    </message>
    <message>
        <location filename="src/ConfigExporter.cpp" line="118"/>
        <source>Failed to start ZIP process. Is &apos;zip&apos; installed?</source>
        <translation type="unfinished">启动 ZIP 进程失败。是否已安装 &apos;zip&apos;？</translation>
    </message>
    <message>
        <location filename="src/ConfigExporter.cpp" line="125"/>
        <source>ZIP process timed out.</source>
        <translation type="unfinished">ZIP 进程超时。</translation>
    </message>
    <message>
        <location filename="src/ConfigExporter.cpp" line="131"/>
        <source>ZIP creation failed: %1</source>
        <translation type="unfinished">ZIP 创建失败: %1</translation>
    </message>
    <message>
        <location filename="src/ConfigImporter.cpp" line="20"/>
        <source>Archive file not found.</source>
        <translation type="unfinished">未找到压缩包文件。</translation>
    </message>
    <message>
        <location filename="src/ConfigImporter.cpp" line="28"/>
        <source>Failed to create temporary directory.</source>
        <translation type="unfinished">创建临时目录失败。</translation>
    </message>
    <message>
        <location filename="src/ConfigImporter.cpp" line="49"/>
        <source>Failed to start unzip process. Is &apos;unzip&apos; installed?</source>
        <translation type="unfinished">启动解压进程失败。是否已安装 &apos;unzip&apos;？</translation>
    </message>
    <message>
        <location filename="src/ConfigImporter.cpp" line="56"/>
        <source>Unzip process timed out.</source>
        <translation type="unfinished">解压进程超时。</translation>
    </message>
    <message>
        <location filename="src/ConfigImporter.cpp" line="62"/>
        <source>Archive extraction failed: %1</source>
        <translation type="unfinished">压缩包解压失败: %1</translation>
    </message>
    <message>
        <location filename="src/ConfigImporter.cpp" line="71"/>
        <source>Invalid archive: manifest.json not found.</source>
        <translation type="unfinished">无效的压缩包: 未找到 manifest.json。</translation>
    </message>
    <message>
        <location filename="src/ConfigImporter.cpp" line="77"/>
        <source>Cannot read manifest.json.</source>
        <translation type="unfinished">无法读取 manifest.json。</translation>
    </message>
    <message>
        <location filename="src/ConfigImporter.cpp" line="84"/>
        <source>Invalid manifest: missing required fields.</source>
        <translation type="unfinished">无效的清单: 缺少必填字段。</translation>
    </message>
    <message>
        <location filename="src/ConfigImporter.cpp" line="90"/>
        <source>Archive schema version %1 is not supported. Please update Seelie.</source>
        <translation type="unfinished">不支持压缩包架构版本 %1。请更新 Seelie。</translation>
    </message>
    <message>
        <location filename="src/ConfigImporter.cpp" line="100"/>
        <source>Invalid archive: config/Seelie.ini not found.</source>
        <translation type="unfinished">无效的压缩包: 未找到 config/Seelie.ini。</translation>
    </message>
    <message>
        <location filename="src/ConfigImporter.cpp" line="140"/>
        <source>Failed to create temporary extraction directory.</source>
        <translation type="unfinished">创建临时解压目录失败。</translation>
    </message>
    <message>
        <location filename="src/ConfigImporter.cpp" line="178"/>
        <source>Failed to start unzip process.</source>
        <translation type="unfinished">启动解压进程失败。</translation>
    </message>
    <message>
        <location filename="src/ConfigImporter.cpp" line="185"/>
        <source>Unzip timed out.</source>
        <translation type="unfinished">解压超时。</translation>
    </message>
    <message>
        <location filename="src/ConfigImporter.cpp" line="191"/>
        <source>Extraction failed: %1</source>
        <translation type="unfinished">解压失败: %1</translation>
    </message>
    <message>
        <location filename="src/ConfigImporter.cpp" line="258"/>
        <source>Failed to backup existing config file.</source>
        <translation type="unfinished">备份现有配置文件失败。</translation>
    </message>
    <message>
        <location filename="src/ConfigImporter.cpp" line="270"/>
        <source>Failed to backup existing memory database.</source>
        <translation type="unfinished">备份现有记忆数据库失败。</translation>
    </message>
    <message>
        <location filename="src/ConfigImporter.cpp" line="288"/>
        <source>Failed to copy new configuration files.</source>
        <translation type="unfinished">复制新配置文件失败。</translation>
    </message>
</context>
<context>
    <name>RecallDialog</name>
    <message>
        <location filename="src/RecallDialog.cpp" line="19"/>
        <source>What do you remember?</source>
        <translation>你还记得吗？</translation>
    </message>
    <message>
        <location filename="src/RecallDialog.cpp" line="46"/>
        <source>Close</source>
        <translation>关闭</translation>
    </message>
    <message>
        <location filename="src/RecallDialog.cpp" line="35"/>
        <source>Search memories…</source>
        <translation>搜索回忆…</translation>
    </message>
    <message>
        <location filename="src/RecallDialog.cpp" line="133"/>
        <source>Searching…</source>
        <translation>正在搜索…</translation>
    </message>
    <message>
        <location filename="src/RecallDialog.cpp" line="83"/>
        <source>Bond L%1 · Affection %2/100 · %3 memories</source>
        <translation>羁绊 L%1 · 好感 %2/100 · %3 条回忆</translation>
    </message>
    <message>
        <location filename="src/RecallDialog.cpp" line="86"/>
        <source>Known %1 for %2 days · Bond L%3 · Affection %4/100 · %5 memories</source>
        <translation>认识 %1 已经 %2 天 · 羁绊 L%3 · 好感 %4/100 · %5 条回忆</translation>
    </message>
    <message>
        <location filename="src/RecallDialog.cpp" line="76"/>
        <source>Memory unavailable.</source>
        <translation>记忆不可用。</translation>
    </message>
    <message>
        <location filename="src/RecallDialog.cpp" line="93"/>
        <location filename="src/RecallDialog.cpp" line="98"/>
        <source>No memories yet — chat with me more!</source>
        <translation>还没有回忆，多陪我聊聊吧！</translation>
    </message>
    <message>
        <location filename="src/RecallDialog.cpp" line="179"/>
        <location filename="src/RecallDialog.cpp" line="192"/>
        <source>Nothing like that yet.</source>
        <translation>还没有相关的回忆。</translation>
    </message>
</context>
<context>
    <name>SettingsPanelWidget</name>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="283"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1512"/>
        <source>Settings</source>
        <translation>设置</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="288"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1513"/>
        <source>×</source>
        <translation>×</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="318"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1514"/>
        <source>Language</source>
        <translation>语言</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="328"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1515"/>
        <source>English</source>
        <translation>English</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="329"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1516"/>
        <source>简体中文</source>
        <translation>简体中文</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="402"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1517"/>
        <source>Launch at Login</source>
        <translation>登录时启动</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="429"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1518"/>
        <source>Mode</source>
        <translation>模式</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="438"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1519"/>
        <source>Character</source>
        <comment>display mode option</comment>
        <translation>角色</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="439"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1520"/>
        <source>ECG Monitor</source>
        <translation>心电图</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="447"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1521"/>
        <source>Port</source>
        <translation>端口</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="470"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1527"/>
        <source>Model</source>
        <translation>模型</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="481"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1425"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1507"/>
        <source>(no pack)</source>
        <translation>（未选择）</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="559"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1522"/>
        <source>Shortcut</source>
        <translation>快捷键</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="585"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1523"/>
        <source>Global shortcut to show/hide the pet</source>
        <translation>用于显示/隐藏宠物的全局快捷键</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="657"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1581"/>
        <source>Idle Sayings</source>
        <translation>闲聊语录</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="663"/>
        <source>Never</source>
        <translation>从不</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="664"/>
        <source>Rarely</source>
        <translation>很少</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="665"/>
        <source>Sometimes</source>
        <translation>偶尔</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="666"/>
        <source>Often</source>
        <translation>经常</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="714"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1538"/>
        <source>Enable AI persona</source>
        <translation>启用 AI 人格</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="755"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1536"/>
        <source>TTS</source>
        <translation>语音</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="543"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1528"/>
        <source>Application</source>
        <translation>应用</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="551"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1529"/>
        <source>Character</source>
        <comment>settings section title</comment>
        <translation>角色</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="594"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1530"/>
        <source>Interaction</source>
        <translation>交互</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="683"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1531"/>
        <source>AI Features</source>
        <translation>AI 功能</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="844"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1564"/>
        <source>Profiles</source>
        <translation>配置</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="856"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1566"/>
        <source>Add</source>
        <translation>添加</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="857"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1567"/>
        <source>Edit</source>
        <translation>编辑</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="858"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1568"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1796"/>
        <source>Delete</source>
        <translation>删除</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="860"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1571"/>
        <source>Test connection — sends a 1-token request to the selected profile</source>
        <translation>测试连接 — 向所选配置发送一次 1 token 的请求</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="900"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1565"/>
        <source>Privacy</source>
        <translation>隐私</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="905"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1574"/>
        <source>Share memory with AI</source>
        <translation>与 AI 共享记忆</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="906"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1575"/>
        <source>Sends your name, profile bio, relationship stats (bond, affection, sessions), and recent activity summaries to the AI provider with each on-demand event.</source>
        <translation>每次响应事件时，向 AI 服务商发送你的名字、个人简介、关系数据（羁绊、好感、会话数）和近期活动摘要。</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="909"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1578"/>
        <source>Occasional AI idle quips</source>
        <translation>偶尔让 AI 即兴发挥</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="910"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1579"/>
        <source>When the pet is idle, it may occasionally send a short prompt (plus your memory digest if &apos;Share memory with AI&apos; is on) to the configured AI provider to generate a fresh quip.</source>
        <translation>宠物空闲时，可能会偶尔向已配置的 AI 服务商发送一条短提示(若开启了"与 AI 共享记忆"还会附带记忆摘要)，用于生成一句新的闲聊。</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="924"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1583"/>
        <source>Regenerate pool</source>
        <translation>重建缓存</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="927"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1584"/>
        <source>Wipe cached LLM responses for the active pack so they will be regenerated.</source>
        <translation>清除当前包的 LLM 缓存回复,以便重新生成。</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="1880"/>
        <source>Last error: —</source>
        <translation>最近错误: —</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="1539"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1600"/>
        <source>Provider</source>
        <translation>提供商</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="1762"/>
        <source>OpenAI Chat</source>
        <translation>OpenAI Chat</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="1763"/>
        <source>OpenAI Responses</source>
        <translation>OpenAI Responses</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="1764"/>
        <source>Anthropic</source>
        <translation>Anthropic</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="1792"/>
        <source>Set as default</source>
        <translation>设为默认</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="1795"/>
        <source>Edit...</source>
        <translation>编辑…</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="1797"/>
        <source>Test connection</source>
        <translation>测试连接</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="1883"/>
        <source>Select a profile first</source>
        <translation>请先选择一个配置</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="1886"/>
        <source>Testing...</source>
        <translation>测试中…</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="1889"/>
        <source>✓ %1 ms</source>
        <translation>✓ %1 毫秒</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="1896"/>
        <source>✗ %1</source>
        <translation>✗ %1</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="859"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1540"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1570"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1691"/>
        <source>Test</source>
        <translation>测试</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="1713"/>
        <source>Hello. This is a TTS test from Seelie.</source>
        <translation>你好。这是 Seelie 的语音合成测试。</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="1542"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1716"/>
        <source>Clear cache</source>
        <translation>清除缓存</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="1543"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1720"/>
        <source>Delete cached audio so the next utterance is freshly synthesised.</source>
        <translation>删除已缓存的音频，下一次发声将重新合成。</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="1551"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1666"/>
        <source>Enter voice ID</source>
        <translation>输入语音 ID</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="1261"/>
        <source>Authentication failed — check this credential.</source>
        <translation>认证失败，请检查凭证。</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="597"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1524"/>
        <source>Gaming Mode</source>
        <translation>游戏模式</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="617"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1525"/>
        <source>Event Tips</source>
        <translation>事件提示</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="637"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1526"/>
        <source>Touch Reactions</source>
        <translation>触摸互动</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="657"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1527"/>
        <source>Desktop Wandering</source>
        <translation>桌面漫游</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="747"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1532"/>
        <source>General</source>
        <translation>通用</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="769"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1534"/>
        <source>AI</source>
        <translation>AI</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="691"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1537"/>
        <source>Enable TTS</source>
        <translation>启用语音</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="822"/>
        <source>TTS not available</source>
        <translation>语音功能不可用</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="762"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1533"/>
        <source>Profile</source>
        <translation>资料</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="1284"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1556"/>
        <source>Name</source>
        <translation>名字</translation>
    </message>
    <message>
        <source>Display name</source>
        <translation type="vanished">显示名称</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="1289"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1557"/>
        <source>Your name</source>
        <translation>您的名字</translation>
    </message>
    <message>
        <source>Shown in greetings</source>
        <translation type="vanished">在问候语中显示</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="1297"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1558"/>
        <source>About you</source>
        <translation>关于你</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="1302"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1559"/>
        <source>A few sentences — role, working style, anything you&apos;d want a coworker to know. Markdown is fine.</source>
        <translation>用几句话介绍一下自己 —— 你的角色、工作风格、想让同事了解的任何事。支持 Markdown。</translation>
    </message>
    <message>
        <location filename="src/SettingsPanelWidget.cpp" line="1337"/>
        <location filename="src/SettingsPanelWidget.cpp" line="1560"/>
        <source>Save</source>
        <translation>保存</translation>
    </message>
</context>
<context>
    <name>StatisticsDialog</name>
    <message>
        <location filename="src/StatisticsDialog.cpp" line="31"/>
        <source>Statistics</source>
        <translation>统计</translation>
    </message>
    <message>
        <location filename="src/StatisticsDialog.cpp" line="46"/>
        <source>TTS Cache</source>
        <translation>TTS 缓存</translation>
    </message>
    <message>
        <location filename="src/StatisticsDialog.cpp" line="47"/>
        <source>Requests:</source>
        <translation>请求:</translation>
    </message>
    <message>
        <location filename="src/StatisticsDialog.cpp" line="48"/>
        <source>Hits:</source>
        <translation>命中:</translation>
    </message>
    <message>
        <location filename="src/StatisticsDialog.cpp" line="50"/>
        <source>AI Persona</source>
        <translation>AI 人格</translation>
    </message>
    <message>
        <location filename="src/StatisticsDialog.cpp" line="51"/>
        <source>Refills ok / fail:</source>
        <translation>补缓存 成功 / 失败:</translation>
    </message>
    <message>
        <location filename="src/StatisticsDialog.cpp" line="52"/>
        <source>On-demand ok / fail:</source>
        <translation>按需 成功 / 失败:</translation>
    </message>
    <message>
        <location filename="src/StatisticsDialog.cpp" line="53"/>
        <source>Tokens in / out:</source>
        <translation>Token 输入 / 输出:</translation>
    </message>
    <message>
        <location filename="src/StatisticsDialog.cpp" line="54"/>
        <source>Last LLM error:</source>
        <translation>最近 LLM 错误:</translation>
    </message>
    <message>
        <location filename="src/StatisticsDialog.cpp" line="56"/>
        <source>Events</source>
        <translation>事件</translation>
    </message>
    <message>
        <location filename="src/StatisticsDialog.cpp" line="57"/>
        <source>Total received:</source>
        <translation>累计收到:</translation>
    </message>
    <message>
        <location filename="src/StatisticsDialog.cpp" line="58"/>
        <source>Last event:</source>
        <translation>最近事件:</translation>
    </message>
    <message>
        <location filename="src/StatisticsDialog.cpp" line="60"/>
        <source>IPC</source>
        <translation>IPC</translation>
    </message>
    <message>
        <location filename="src/StatisticsDialog.cpp" line="61"/>
        <source>Packets received:</source>
        <translation>收到数据包:</translation>
    </message>
    <message>
        <location filename="src/StatisticsDialog.cpp" line="62"/>
        <source>Decode errors:</source>
        <translation>解码错误:</translation>
    </message>
    <message>
        <location filename="src/StatisticsDialog.cpp" line="65"/>
        <source>Refresh</source>
        <translation>刷新</translation>
    </message>
    <message>
        <location filename="src/StatisticsDialog.cpp" line="66"/>
        <source>Reset stats</source>
        <translation>重置统计</translation>
    </message>
    <message>
        <location filename="src/StatisticsDialog.cpp" line="67"/>
        <source>Close</source>
        <translation>关闭</translation>
    </message>
    <message>
        <location filename="src/StatisticsDialog.cpp" line="160"/>
        <source>Reset stats?</source>
        <translation>重置统计?</translation>
    </message>
    <message>
        <location filename="src/StatisticsDialog.cpp" line="161"/>
        <source>This clears only the stats counters. Milestones, name, and other memory data are preserved. Continue?</source>
        <translation>此操作仅清除统计计数器。里程碑、名字以及其他记忆数据会保留。是否继续?</translation>
    </message>
</context>
<context>
    <name>StyledAlertWidget</name>
    <message>
        <location filename="src/StyledAlertWidget.cpp" line="98"/>
        <location filename="src/StyledAlertWidget.cpp" line="134"/>
        <location filename="src/StyledAlertWidget.cpp" line="164"/>
        <source>OK</source>
        <translation>确定</translation>
    </message>
    <message>
        <location filename="src/StyledAlertWidget.cpp" line="108"/>
        <source>Cancel</source>
        <translation>取消</translation>
    </message>
    <message>
        <location filename="src/StyledAlertWidget.cpp" line="149"/>
        <source>Yes</source>
        <translation>是</translation>
    </message>
    <message>
        <location filename="src/StyledAlertWidget.cpp" line="150"/>
        <source>No</source>
        <translation>否</translation>
    </message>
</context>
<context>
    <name>SystemTray</name>
    <message>
        <location filename="src/SystemTray.cpp" line="28"/>
        <location filename="src/SystemTray.cpp" line="391"/>
        <source>Seelie Desktop Pet</source>
        <translation>Seelie 桌面宠物</translation>
    </message>
    <message>
        <location filename="src/SystemTray.cpp" line="101"/>
        <location filename="src/SystemTray.cpp" line="393"/>
        <source>Show/Hide</source>
        <translation>显示/隐藏</translation>
    </message>
    <message>
        <location filename="src/SystemTray.cpp" line="114"/>
        <location filename="src/SystemTray.cpp" line="396"/>
        <source>Gaming Mode</source>
        <translation>游戏模式</translation>
    </message>
    <message>
        <location filename="src/SystemTray.cpp" line="131"/>
        <location filename="src/SystemTray.cpp" line="399"/>
        <source>Model</source>
        <translation>模型</translation>
    </message>
    <message>
        <location filename="src/SystemTray.cpp" line="141"/>
        <location filename="src/SystemTray.cpp" line="411"/>
        <source>Statistics...</source>
        <translation>统计…</translation>
    </message>
    <message>
        <location filename="src/SystemTray.cpp" line="149"/>
        <location filename="src/SystemTray.cpp" line="417"/>
        <source>Config</source>
        <translation>配置</translation>
    </message>
    <message>
        <location filename="src/SystemTray.cpp" line="151"/>
        <location filename="src/SystemTray.cpp" line="420"/>
        <source>Export...</source>
        <translation>导出…</translation>
    </message>
    <message>
        <location filename="src/SystemTray.cpp" line="152"/>
        <location filename="src/SystemTray.cpp" line="423"/>
        <source>Import...</source>
        <translation>导入…</translation>
    </message>
    <message>
        <location filename="src/SystemTray.cpp" line="159"/>
        <location filename="src/SystemTray.cpp" line="402"/>
        <source>Check for Updates</source>
        <translation>检查更新</translation>
    </message>
    <message>
        <location filename="src/SystemTray.cpp" line="137"/>
        <location filename="src/SystemTray.cpp" line="408"/>
        <source>Manage Models</source>
        <translation>管理模型</translation>
    </message>
    <message>
        <location filename="src/SystemTray.cpp" line="171"/>
        <location filename="src/SystemTray.cpp" line="405"/>
        <source>Quit</source>
        <translation>退出</translation>
    </message>
    <message>
        <location filename="src/SystemTray.cpp" line="243"/>
        <source>Update Available</source>
        <translation>有可用更新</translation>
    </message>
    <message>
        <location filename="src/SystemTray.cpp" line="240"/>
        <source>Version %1 is available (current: %2)</source>
        <translation>版本 %1 可用（当前版本：%2）</translation>
    </message>
    <message>
        <location filename="src/SystemTray.cpp" line="262"/>
        <source>You are running the latest version (%1)</source>
        <translation>你已经在使用最新版本（%1）</translation>
    </message>
    <message>
        <location filename="src/SystemTray.cpp" line="264"/>
        <source>You&apos;re up to date</source>
        <translation>已是最新版本</translation>
    </message>
    <message>
        <location filename="src/SystemTray.cpp" line="286"/>
        <source>Update Check Failed</source>
        <translation>检查更新失败</translation>
    </message>
    <message>
        <location filename="src/SystemTray.cpp" line="284"/>
        <source>Could not check for updates: %1</source>
        <translation>无法检查更新：%1</translation>
    </message>
    <message>
        <location filename="src/SystemTray.cpp" line="145"/>
        <location filename="src/SystemTray.cpp" line="414"/>
        <source>What do you remember?</source>
        <translation>你还记得吗？</translation>
    </message>
</context>
<context>
    <name>TTSEngine</name>
    <message>
        <location filename="src/TTSEngine.cpp" line="174"/>
        <source>Unknown TTS provider: %1</source>
        <translation>未知 TTS 提供商：%1</translation>
    </message>
    <message>
        <location filename="src/TTSEngine.cpp" line="183"/>
        <source>Failed to construct TTS provider: %1</source>
        <translation>创建 TTS 提供商失败：%1</translation>
    </message>
    <message>
        <location filename="src/TTSEngine.cpp" line="276"/>
        <source>No TTS provider configured</source>
        <translation>未配置 TTS 提供商</translation>
    </message>
    <message>
        <location filename="src/TTSEngine.cpp" line="295"/>
        <source>TTS provider &quot;%1&quot; is missing required fields: %2. Open Settings → TTS to configure.</source>
        <translation>TTS 提供方 &quot;%1&quot; 缺少必填字段: %2。打开 设置 → TTS 进行配置。</translation>
    </message>
    <message>
        <location filename="src/TTSEngine.cpp" line="385"/>
        <source>TTS authentication failed (HTTP %1)</source>
        <translation>TTS 认证失败（HTTP %1）</translation>
    </message>
    <message>
        <location filename="src/TTSEngine.cpp" line="395"/>
        <source>TTS request failed</source>
        <translation>TTS 请求失败</translation>
    </message>
    <message>
        <location filename="src/TTSEngine.cpp" line="480"/>
        <source>Failed to start audio playback</source>
        <translation>音频播放启动失败</translation>
    </message>
</context>
<context>
    <name>TipsEngine</name>
    <message>
        <location filename="src/TipsEngine.cpp" line="95"/>
        <source>Having trouble?</source>
        <translation>遇到困难了吗？</translation>
    </message>
    <message>
        <location filename="src/TipsEngine.cpp" line="96"/>
        <source>It looks like you&apos;re running into repeated errors. Try checking the error messages carefully.</source>
        <translation>看起来你遇到了重复的错误，试着仔细检查一下错误信息。</translation>
    </message>
    <message>
        <location filename="src/TipsEngine.cpp" line="111"/>
        <source>Working on tests?</source>
        <translation>在写测试吗？</translation>
    </message>
    <message>
        <location filename="src/TipsEngine.cpp" line="112"/>
        <source>It looks like you&apos;re editing test files. Remember to run your tests after making changes!</source>
        <translation>看起来你在编辑测试文件，别忘了改完之后跑一下测试！</translation>
    </message>
    <message>
        <location filename="src/TipsEngine.cpp" line="128"/>
        <source>Updating configuration?</source>
        <translation>在更新配置吗？</translation>
    </message>
    <message>
        <location filename="src/TipsEngine.cpp" line="129"/>
        <source>It looks like you&apos;re making configuration changes. Don&apos;t forget to restart your services if needed!</source>
        <translation>看起来你在修改配置，别忘了需要的话重启一下服务！</translation>
    </message>
    <message>
        <location filename="src/TipsEngine.cpp" line="145"/>
        <source>Getting started!</source>
        <translation>开始编码吧！</translation>
    </message>
    <message>
        <location filename="src/TipsEngine.cpp" line="146"/>
        <source>It looks like you&apos;re making your first edit in this session. Happy coding!</source>
        <translation>看起来这是本次会话中你的第一次编辑，祝编码愉快！</translation>
    </message>
    <message>
        <location filename="src/TipsEngine.cpp" line="162"/>
        <source>Making lots of changes?</source>
        <translation>修改了很多内容吗？</translation>
    </message>
    <message>
        <location filename="src/TipsEngine.cpp" line="163"/>
        <source>It looks like you&apos;re making rapid edits. Consider committing your work to save progress!</source>
        <translation>看起来你在快速修改，考虑提交一下代码保存进度吧！</translation>
    </message>
    <message>
        <location filename="src/TipsEngine.cpp" line="176"/>
        <source>Taking a break?</source>
        <translation>在休息吗？</translation>
    </message>
    <message>
        <location filename="src/TipsEngine.cpp" line="177"/>
        <source>It looks like you&apos;ve been idle. Let me know when you&apos;re ready to continue!</source>
        <translation>看起来你闲下来了，准备好继续的时候叫我！</translation>
    </message>
    <message>
        <location filename="src/TipsEngine.cpp" line="193"/>
        <source>Permission issues?</source>
        <translation>权限问题？</translation>
    </message>
    <message>
        <location filename="src/TipsEngine.cpp" line="194"/>
        <source>It looks like you&apos;ve denied permissions multiple times. You can configure auto-approval in your tool settings.</source>
        <translation>看起来你多次拒绝了权限请求，你可以在工具设置中配置自动批准。</translation>
    </message>
    <message>
        <source>Using git?</source>
        <translation type="vanished">在使用 Git 吗？</translation>
    </message>
    <message>
        <source>It looks like you&apos;re working with git. Remember to pull before pushing!</source>
        <translation type="vanished">看起来你在用 Git，记得推送之前先拉取！</translation>
    </message>
    <message>
        <location filename="src/TipsEngine.cpp" line="71"/>
        <source>Seelie noticed something!</source>
        <translation>Seelie 注意到了一些事情！</translation>
    </message>
    <message>
        <location filename="src/TipsEngine.cpp" line="72"/>
        <source>I&apos;ll try to give helpful tips while you work.</source>
        <translation>我会在你工作时给出有用的建议。</translation>
    </message>
</context>
<context>
    <name>Tts</name>
    <message>
        <location filename="src/tts/TTSProviderRegistry.cpp" line="24"/>
        <source>Cixingnansheng (male)</source>
        <translation>磁性男声</translation>
    </message>
    <message>
        <location filename="src/tts/TTSProviderRegistry.cpp" line="27"/>
        <source>Linjiajiejie (female)</source>
        <translation>邻家姐姐</translation>
    </message>
    <message>
        <location filename="src/tts/TTSProviderRegistry.cpp" line="47"/>
        <source>Female young (shaonv)</source>
        <translation>少女音</translation>
    </message>
    <message>
        <location filename="src/tts/TTSProviderRegistry.cpp" line="50"/>
        <source>Male qingse</source>
        <translation>青涩男声</translation>
    </message>
    <message>
        <location filename="src/tts/TTSProviderRegistry.cpp" line="70"/>
        <source>Xiaoxiao (zh-CN, female)</source>
        <translation>晓晓（普通话，女）</translation>
    </message>
    <message>
        <location filename="src/tts/TTSProviderRegistry.cpp" line="73"/>
        <source>Jenny (en-US, female)</source>
        <translation>Jenny（美式英语，女）</translation>
    </message>
</context>
</TS>
