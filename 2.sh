#!/bin/bash
# OpenWrt IPK 自动打包脚本

set -e

ARCH=${1:-all}
BINARY=${2:-ech-workers}
PKG_NAME="luci-app-ech-workers"
PKG_VERSION="${GITHUB_REF_NAME:-1.5}"
PKG_VERSION="${PKG_VERSION#v}"
PKG_RELEASE="1"
BUILD_DIR="build-${ARCH}"

echo "========================================"
echo "构建 OpenWrt IPK 包"
echo "架构: ${ARCH}"
echo "版本: ${PKG_VERSION}-${PKG_RELEASE}"
echo "========================================"

# 清理旧构建
rm -rf "${BUILD_DIR}"

# 创建目录结构
mkdir -p "${BUILD_DIR}/${PKG_NAME}/CONTROL"
mkdir -p "${BUILD_DIR}/${PKG_NAME}/usr/bin"
mkdir -p "${BUILD_DIR}/${PKG_NAME}/etc/config"
mkdir -p "${BUILD_DIR}/${PKG_NAME}/etc/init.d"
mkdir -p "${BUILD_DIR}/${PKG_NAME}/usr/lib/lua/luci/controller"
mkdir -p "${BUILD_DIR}/${PKG_NAME}/usr/lib/lua/luci/model/cbi"
mkdir -p "${BUILD_DIR}/${PKG_NAME}/www/luci-static/resources/view/ech-workers"

# 复制二进制文件
if [ -f "${BINARY}" ]; then
    echo "复制二进制文件: ${BINARY}"
    cp "${BINARY}" "${BUILD_DIR}/${PKG_NAME}/usr/bin/ech-workers"
    chmod +x "${BUILD_DIR}/${PKG_NAME}/usr/bin/ech-workers"
else
    echo "警告: 未找到二进制文件 ${BINARY}"
fi

# 创建控制文件
cat > "${BUILD_DIR}/${PKG_NAME}/CONTROL/control" << EOF
Package: ${PKG_NAME}
Version: ${PKG_VERSION}-${PKG_RELEASE}
Depends: libc, luci-base, luci-compat, luci-lib-jsonc, wget-ssl
Section: luci
Architecture: ${ARCH}
Installed-Size: $(du -sk "${BUILD_DIR}/${PKG_NAME}" | cut -f1)
Maintainer: ECH Workers Team <https://github.com/yourusername/ech-workers>
Description: ECH Workers Client for OpenWrt
 Web UI for ECH Workers proxy client with subscription support.
 Supports multiple architectures and subscription management.
EOF

# 创建安装后脚本
cat > "${BUILD_DIR}/${PKG_NAME}/CONTROL/postinst" << 'EOF'
#!/bin/sh
[ -n "${IPKG_INSTROOT}" ] || {
    /etc/init.d/ech-workers enable
    /etc/init.d/rpcd restart
    /etc/init.d/uhttpd restart
}
exit 0
EOF

# 创建卸载前脚本
cat > "${BUILD_DIR}/${PKG_NAME}/CONTROL/prerm" << 'EOF'
#!/bin/sh
[ -n "${IPKG_INSTROOT}" ] || {
    /etc/init.d/ech-workers stop
    /etc/init.d/ech-workers disable
}
exit 0
EOF

chmod +x "${BUILD_DIR}/${PKG_NAME}/CONTROL/postinst"
chmod +x "${BUILD_DIR}/${PKG_NAME}/CONTROL/prerm"

# 复制配置文件
echo "创建配置文件..."
cp -r openwrt/root/* "${BUILD_DIR}/${PKG_NAME}/" 2>/dev/null || true
cp -r openwrt/etc/* "${BUILD_DIR}/${PKG_NAME}/etc/" 2>/dev/null || true
cp -r openwrt/usr/* "${BUILD_DIR}/${PKG_NAME}/usr/" 2>/dev/null || true
cp -r openwrt/www/* "${BUILD_DIR}/${PKG_NAME}/www/" 2>/dev/null || true

# 设置权限
find "${BUILD_DIR}/${PKG_NAME}/etc/init.d" -type f -exec chmod +x {} \; 2>/dev/null || true

# 打包 IPK
echo "打包 IPK..."
cd "${BUILD_DIR}"

# 创建 control.tar.gz
tar -czf "${PKG_NAME}/control.tar.gz" -C "${PKG_NAME}/CONTROL" .

# 创建 data.tar.gz
tar -czf "${PKG_NAME}/data.tar.gz" -C "${PKG_NAME}" --exclude=CONTROL .

# 创建 debian-binary
echo "2.0" > "${PKG_NAME}/debian-binary"

# 创建 IPK
cd "${PKG_NAME}"
ar r "../${PKG_NAME}_${PKG_VERSION}-${PKG_RELEASE}_${ARCH}.ipk" \
    debian-binary control.tar.gz data.tar.gz

cd ../..

# 移动到根目录
mv "${BUILD_DIR}/${PKG_NAME}_${PKG_VERSION}-${PKG_RELEASE}_${ARCH}.ipk" .

# 计算 MD5
md5sum "${PKG_NAME}_${PKG_VERSION}-${PKG_RELEASE}_${ARCH}.ipk" > \
    "${PKG_NAME}_${PKG_VERSION}-${PKG_RELEASE}_${ARCH}.ipk.md5"

echo "========================================"
echo "构建完成!"
echo "输出文件: ${PKG_NAME}_${PKG_VERSION}-${PKG_RELEASE}_${ARCH}.ipk"
echo "大小: $(du -h ${PKG_NAME}_${PKG_VERSION}-${PKG_RELEASE}_${ARCH}.ipk | cut -f1)"
echo "========================================"

# 创建安装说明
cat > "INSTALL-${ARCH}.txt" << EOF
ECH Workers OpenWrt 安装说明 (${ARCH})
=====================================

1. 下载文件:
   ${PKG_NAME}_${PKG_VERSION}-${PKG_RELEASE}_${ARCH}.ipk

2. 上传到路由器:
   scp ${PKG_NAME}_${PKG_VERSION}-${PKG_RELEASE}_${ARCH}.ipk root@192.168.1.1:/tmp/

3. SSH 登录路由器并安装:
   ssh root@192.168.1.1
   opkg update
   opkg install /tmp/${PKG_NAME}_${PKG_VERSION}-${PKG_RELEASE}_${ARCH}.ipk

4. 重启服务:
   /etc/init.d/rpcd restart
   /etc/init.d/uhttpd restart

5. 访问 LuCI:
   浏览器打开: http://192.168.1.1
   导航到: 服务 -> ECH Workers

常见架构选择:
- x86_64: 软路由、虚拟机
- aarch64: 树莓派 3/4、NanoPi
- mipsle: 小米路由器、新路由3
- mips: 部分老款路由器
- arm: 老款 ARM 路由器

验证安装:
   /usr/bin/ech-workers -version
   /etc/init.d/ech-workers status

卸载:
   opkg remove ${PKG_NAME}
EOF

echo "安装说明已生成: INSTALL-${ARCH}.txt"