#!/bin/bash
# 自动应用 VADNet1 缓存修复补丁

echo "🔧 检查 model_path.c 是否需要修复..."

TARGET_FILE="managed_components/espressif__esp-sr/src/model_path.c"

if grep -q "\[PATCH\] Clear old static_srmodels cache" "$TARGET_FILE"; then
    echo "✅ 补丁已存在，无需重新应用"
    exit 0
fi

echo "⚠️  补丁丢失，正在重新应用..."

# 备份原始文件
cp "$TARGET_FILE" "$TARGET_FILE.backup"

# 在 srmodel_mmap_init 函数开头插入补丁
sed -i '' '/srmodel_list_t \*srmodel_mmap_init/,/^{/{
/^{/a\
    // 🎯 强制清除旧缓存，确保每次都从 Flash 重新加载最新模型\
    // 修复：设备重启后 vadnet1_medium 无法加载的问题\
    if (static_srmodels != NULL) {\
        printf("[PATCH] Clear old static_srmodels cache before loading from Flash\\n");\
        static_srmodels = NULL;\
    }\
    
}' "$TARGET_FILE"

echo "✅ 补丁已重新应用！"
echo "ℹ️  备份保存在: $TARGET_FILE.backup"
