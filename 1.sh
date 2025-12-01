#!/bin/bash
# 创建 OpenWrt 配置文件结构
# 运行此脚本生成所需的目录和文件

set -e

echo "创建 OpenWrt 文件结构..."

# 创建目录结构
mkdir -p openwrt/etc/config
mkdir -p openwrt/etc/init.d
mkdir -p openwrt/usr/lib/lua/luci/controller
mkdir -p openwrt/www/luci-static/resources/view/ech-workers

# ============ 配置文件 ============
cat > openwrt/etc/config/ech-workers << 'EOF'
config ech-workers 'config'
	option enabled '0'
	option config_name '默认配置'
	option server ''
	option listen '127.0.0.1:30000'
	option token ''
	option ip ''
	option dns 'dns.alidns.com/dns-query'
	option ech 'cloudflare-ech.com'

config subscription 'subscription'
	option enabled '1'
	option auto_update '0'
	option update_interval '24'
	list urls ''

config nodes 'nodes'
	option current_node ''
	option auto_switch '0'
EOF

# ============ 初始化脚本 ============
cat > openwrt/etc/init.d/ech-workers << 'EOF'
#!/bin/sh /etc/rc.common

START=99
STOP=10

USE_PROCD=1
PROG=/usr/bin/ech-workers
CONFIG=/etc/config/ech-workers

start_service() {
	config_load ech-workers
	
	local enabled
	config_get enabled config enabled 0
	[ "$enabled" -eq 0 ] && {
		echo "ECH Workers is disabled"
		return 1
	}
	
	local server listen token ip dns ech
	config_get server config server
	config_get listen config listen "127.0.0.1:30000"
	config_get token config token
	config_get ip config ip
	config_get dns config dns "dns.alidns.com/dns-query"
	config_get ech config ech "cloudflare-ech.com"
	
	[ -z "$server" ] && {
		echo "Server address is required"
		return 1
	}
	
	# 检查程序是否存在
	[ -x "$PROG" ] || {
		echo "ECH Workers binary not found or not executable"
		return 1
	}
	
	procd_open_instance ech-workers
	procd_set_param command $PROG
	procd_append_param command -f "$server"
	procd_append_param command -l "$listen"
	
	[ -n "$token" ] && procd_append_param command -token "$token"
	[ -n "$ip" ] && procd_append_param command -ip "$ip"
	
	[ "$dns" != "dns.alidns.com/dns-query" ] && {
		procd_append_param command -dns "$dns"
	}
	
	[ "$ech" != "cloudflare-ech.com" ] && {
		procd_append_param command -ech "$ech"
	}
	
	# 检测IP格式DNS，自动添加 -insecure-dns
	case "$dns" in
		[0-9]*)
			procd_append_param command -insecure-dns
			echo "Detected IP format DNS, adding -insecure-dns flag"
			;;
	esac
	
	procd_set_param respawn ${respawn_threshold:-3600} ${respawn_timeout:-5} ${respawn_retry:-5}
	procd_set_param stdout 1
	procd_set_param stderr 1
	procd_set_param pidfile /var/run/ech-workers.pid
	
	procd_close_instance
}

stop_service() {
	killall ech-workers 2>/dev/null || true
}

reload_service() {
	stop
	sleep 2
	start
}

service_triggers() {
	procd_add_reload_trigger "ech-workers"
}

status() {
	local pid=$(pgrep -f "$PROG")
	if [ -n "$pid" ]; then
		echo "ECH Workers is running (PID: $pid)"
		return 0
	else
		echo "ECH Workers is not running"
		return 1
	fi
}
EOF

chmod +x openwrt/etc/init.d/ech-workers

# ============ LuCI 控制器 ============
cat > openwrt/usr/lib/lua/luci/controller/ech-workers.lua << 'EOF'
module("luci.controller.ech-workers", package.seeall)

function index()
	if not nixio.fs.access("/etc/config/ech-workers") then
		return
	end

	local page = entry({"admin", "services", "ech-workers"}, 
		alias("admin", "services", "ech-workers", "settings"),
		_("ECH Workers"), 60)
	page.dependent = true
	page.acl_depends = { "luci-app-ech-workers" }
	
	entry({"admin", "services", "ech-workers", "settings"}, 
		view("ech-workers/settings"), _("基本设置"), 10).leaf = true
	
	entry({"admin", "services", "ech-workers", "subscription"}, 
		view("ech-workers/subscription"), _("订阅管理"), 20).leaf = true
	
	entry({"admin", "services", "ech-workers", "nodes"}, 
		view("ech-workers/nodes"), _("节点列表"), 30).leaf = true
	
	entry({"admin", "services", "ech-workers", "status"}, 
		call("action_status")).leaf = true
	
	entry({"admin", "services", "ech-workers", "fetch"}, 
		call("action_fetch")).leaf = true
	
	entry({"admin", "services", "ech-workers", "parse"}, 
		call("action_parse")).leaf = true
end

function action_status()
	local sys = require "luci.sys"
	local uci = require "luci.model.uci".cursor()
	
	local status = {
		running = (sys.call("pgrep -f '/usr/bin/ech-workers' > /dev/null") == 0),
		enabled = uci:get("ech-workers", "config", "enabled") == "1",
		version = sys.exec("/usr/bin/ech-workers -version 2>&1 | head -n1"):gsub("\n", ""),
		uptime = sys.exec("ps -o etime= -p $(pgrep -f '/usr/bin/ech-workers')"):gsub("\n", "")
	}
	
	luci.http.prepare_content("application/json")
	luci.http.write_json(status)
end

function action_fetch()
	local http = require "luci.http"
	local sys = require "luci.sys"
	local url = http.formvalue("url")
	
	if not url or url == "" then
		http.prepare_content("application/json")
		http.write_json({success = false, error = "URL不能为空"})
		return
	end
	
	local cmd = string.format("wget --no-check-certificate -q -O- '%s' 2>&1", url:gsub("'", "'\\''"))
	local data = sys.exec(cmd)
	
	if data and #data > 0 then
		http.prepare_content("application/json")
		http.write_json({success = true, data = data})
	else
		http.prepare_content("application/json")
		http.write_json({success = false, error = "获取订阅失败"})
	end
end

function action_parse()
	local http = require "luci.http"
	local json = require "luci.jsonc"
	
	local data = http.formvalue("data")
	if not data then
		http.prepare_content("application/json")
		http.write_json({success = false, error = "数据为空"})
		return
	end
	
	-- 简单的 ech:// 协议解析
	local nodes = {}
	for line in data:gmatch("[^\r\n]+") do
		if line:match("^ech://") or line:match("^ECH://") then
			local node = parse_ech_url(line)
			if node then
				table.insert(nodes, node)
			end
		end
	end
	
	http.prepare_content("application/json")
	http.write_json({success = true, nodes = nodes, count = #nodes})
end

function parse_ech_url(url)
	-- 解析: ech://server|token|ip|dns|ech#name
	local content, name = url:match("^[eE][cC][hH]://([^#]+)#?(.*)")
	if not content then return nil end
	
	local parts = {}
	for part in content:gmatch("[^|]+") do
		table.insert(parts, part)
	end
	
	return {
		name = url_decode(name) or parts[1] or "未命名节点",
		server = parts[1] or "",
		token = parts[2] or "",
		ip = parts[3] or "",
		dns = parts[4] or "dns.alidns.com/dns-query",
		ech = parts[5] or "cloudflare-ech.com"
	}
end

function url_decode(str)
	if not str then return nil end
	str = str:gsub("+", " ")
	str = str:gsub("%%(%x%x)", function(h)
		return string.char(tonumber(h, 16))
	end)
	return str
end
EOF

# ============ LuCI 视图 - 基本设置 ============
cat > openwrt/www/luci-static/resources/view/ech-workers/settings.js << 'EOF'
'use strict';
'require view';
'require form';
'require uci';
'require rpc';
'require poll';
'require ui';

var callServiceList = rpc.declare({
	object: 'service',
	method: 'list',
	params: ['name'],
	expect: { '': {} }
});

function getServiceStatus() {
	return L.resolveDefault(callServiceList('ech-workers'), {})
		.then(function(res) {
			var isRunning = false;
			try {
				isRunning = res['ech-workers'] && 
					res['ech-workers']['instances'] &&
					res['ech-workers']['instances']['ech-workers'] &&
					res['ech-workers']['instances']['ech-workers']['running'];
			} catch(e) {}
			return isRunning;
		});
}

return view.extend({
	load: function() {
		return Promise.all([
			uci.load('ech-workers'),
			getServiceStatus()
		]);
	},

	render: function(data) {
		var isRunning = data[1];
		var m, s, o;

		m = new form.Map('ech-workers', _('ECH Workers 客户端'),
			_('基于 ECH (Encrypted Client Hello) 技术的代理客户端'));

		// 状态栏
		s = m.section(form.NamedSection, '__status__', 'status', _('运行状态'));
		s.anonymous = true;
		s.render = function() {
			return E('div', { 'class': 'cbi-section' }, [
				E('div', { 'class': 'cbi-value' }, [
					E('label', { 'class': 'cbi-value-title' }, _('当前状态')),
					E('div', { 'class': 'cbi-value-field' }, 
						E('span', { 'style': 'font-weight: bold; color: ' + (isRunning ? 'green' : 'red') },
							isRunning ? _('运行中 ●') : _('已停止 ○'))
					)
				])
			]);
		};

		// 基本配置
		s = m.section(form.TypedSection, 'ech-workers', _('基本配置'));
		s.anonymous = true;
		s.addremove = false;

		o = s.option(form.Flag, 'enabled', _('启用'),
			_('启用 ECH Workers 服务'));
		o.default = '0';
		o.rmempty = false;

		o = s.option(form.Value, 'config_name', _('配置名称'),
			_('当前配置的描述性名称'));
		o.default = '默认配置';
		o.placeholder = '默认配置';

		o = s.option(form.Value, 'server', _('服务地址') + ' *',
			_('远程服务器地址，格式: domain:port 或 IP:port'));
		o.placeholder = 'example.com:443';
		o.rmempty = false;
		o.validate = function(section_id, value) {
			if (!value || value.length === 0)
				return _('服务地址不能为空');
			if (!value.match(/^[a-zA-Z0-9.-]+:\d+$/))
				return _('格式错误，应为 host:port');
			return true;
		};

		o = s.option(form.Value, 'listen', _('监听地址'),
			_('本地 SOCKS5 代理监听地址'));
		o.default = '127.0.0.1:30000';
		o.placeholder = '127.0.0.1:30000';
		o.validate = function(section_id, value) {
			if (!value.match(/^[\d.]+:\d+$/))
				return _('格式错误，应为 IP:port');
			return true;
		};

		// 高级选项
		o = s.option(form.Value, 'token', _('身份令牌'),
			_('服务器认证令牌（可选）'));
		o.password = true;
		o.placeholder = '留空则不使用认证';

		o = s.option(form.Value, 'ip', _('优选IP/域名'),
			_('指定实际连接的IP地址或域名（可选）'));
		o.placeholder = '1.1.1.1';

		o = s.option(form.Value, 'dns', _('DNS服务器'),
			_('DNS over HTTPS 服务器地址（仅域名，无需 https:// 前缀）'));
		o.default = 'dns.alidns.com/dns-query';
		o.placeholder = 'dns.alidns.com/dns-query';

		o = s.option(form.Value, 'ech', _('ECH 域名'),
			_('Encrypted Client Hello 使用的域名'));
		o.default = 'cloudflare-ech.com';
		o.placeholder = 'cloudflare-ech.com';

		return m.render();
	},

	handleSaveApply: null,
	handleSave: null,
	handleReset: null
});
EOF

# ============ LuCI 视图 - 订阅管理 ============
cat > openwrt/www/luci-static/resources/view/ech-workers/subscription.js << 'EOF'
'use strict';
'require view';
'require form';
'require uci';
'require ui';
'require rpc';
'require fs';

var callFetch = rpc.declare({
	object: 'luci.ech-workers',
	method: 'fetch',
	params: ['url'],
	expect: { }
});

var callParse = rpc.declare({
	object: 'luci.ech-workers',
	method: 'parse',
	params: ['data'],
	expect: { }
});

return view.extend({
	load: function() {
		return uci.load('ech-workers');
	},

	render: function() {
		var m, s, o;

		m = new form.Map('ech-workers', _('订阅管理'),
			_('管理 ECH Workers 订阅源并自动更新节点列表'));

		s = m.section(form.TypedSection, 'subscription', _('订阅配置'));
		s.anonymous = true;
		s.addremove = false;

		o = s.option(form.Flag, 'enabled', _('启用订阅功能'));
		o.default = '1';

		o = s.option(form.Flag, 'auto_update', _('自动更新'),
			_('定期自动更新订阅'));
		o.default = '0';

		o = s.option(form.Value, 'update_interval', _('更新间隔（小时）'),
			_('自动更新的时间间隔'));
		o.default = '24';
		o.datatype = 'uinteger';
		o.depends('auto_update', '1');

		o = s.option(form.DynamicList, 'urls', _('订阅链接'),
			_('支持多个订阅源，每行一个 URL'));
		o.placeholder = 'https://example.com/subscribe';

		// 操作按钮
		s = m.section(form.NamedSection, '__actions__', 'actions', _('操作'));
		s.anonymous = true;
		s.render = L.bind(function(view, section_id) {
			return E('div', { 'class': 'cbi-section' }, [
				E('div', { 'class': 'cbi-value' }, [
					E('button', {
						'class': 'btn cbi-button cbi-button-action',
						'click': ui.createHandlerFn(this, this.handleUpdateAll)
					}, _('更新所有订阅')),
					' ',
					E('button', {
						'class': 'btn cbi-button cbi-button-reset',
						'click': ui.createHandlerFn(this, this.handleClearNodes)
					}, _('清空节点列表'))
				])
			]);
		}, this);

		return m.render();
	},

	handleUpdateAll: function() {
		var urls = uci.get('ech-workers', 'subscription', 'urls') || [];
		
		if (urls.length === 0) {
			ui.addNotification(null, E('p', _('请先添加订阅链接')), 'warning');
			return;
		}

		ui.showModal(_('更新订阅'), [
			E('p', { 'class': 'spinning' }, _('正在获取订阅数据，请稍候...'))
		]);

		var fetchPromises = urls.map(function(url) {
			return L.resolveDefault(
				L.Request.get('http://' + window.location.host + 
					'/cgi-bin/luci/admin/services/ech-workers/fetch', {
					url: url
				}), null
			);
		});

		return Promise.all(fetchPromises)
			.then(function(results) {
				var allData = '';
				var successCount = 0;
				
				results.forEach(function(result) {
					if (result && result.success && result.data) {
						allData += result.data + '\n';
						successCount++;
					}
				});

				if (allData) {
					return L.Request.post(
						'http://' + window.location.host + 
						'/cgi-bin/luci/admin/services/ech-workers/parse',
						{ data: allData }
					).then(function(parseResult) {
						ui.hideModal();
						if (parseResult && parseResult.success) {
							ui.addNotification(null, 
								E('p', _('订阅更新成功！共获取 %d 个订阅源，解析出 %d 个节点')
									.format(successCount, parseResult.count || 0)),
								'info');
						} else {
							ui.addNotification(null,
								E('p', _('节点解析失败')),
								'error');
						}
					});
				} else {
					ui.hideModal();
					ui.addNotification(null, 
						E('p', _('获取订阅失败，请检查网络和订阅链接')), 
						'error');
				}
			})
			.catch(function(err) {
				ui.hideModal();
				ui.addNotification(null, 
					E('p', _('更新失败: %s').format(err.message)), 
					'error');
			});
	},

	handleClearNodes: function() {
		return ui.showModal(_('确认操作'), [
			E('p', _('确定要清空所有节点吗？此操作不可恢复。')),
			E('div', { 'class': 'right' }, [
				E('button', {
					'class': 'btn cbi-button',
					'click': ui.hideModal
				}, _('取消')),
				' ',
				E('button', {
					'class': 'btn cbi-button-negative',
					'click': ui.createHandlerFn(this, function() {
						// 清空节点逻辑
						fs.remove('/etc/ech-workers/nodes.json').then(function() {
							ui.hideModal();
							ui.addNotification(null, E('p', _('节点列表已清空')), 'info');
						});
					})
				}, _('确定'))
			])
		]);
	}
});
EOF

# ============ LuCI 视图 - 节点列表 ============
cat > openwrt/www/luci-static/resources/view/ech-workers/nodes.js << 'EOF'
'use strict';
'require view';
'require ui';
'require rpc';
'require fs';

return view.extend({
	load: function() {
		return L.resolveDefault(fs.read('/etc/ech-workers/nodes.json'), '[]')
			.then(function(content) {
				try {
					return JSON.parse(content);
				} catch(e) {
					return [];
				}
			});
	},

	render: function(nodes) {
		var table = E('div', { 'class': 'table cbi-section-table' }, [
			E('div', { 'class': 'tr table-titles' }, [
				E('div', { 'class': 'th' }, _('节点名称')),
				E('div', { 'class': 'th' }, _('服务地址')),
				E('div', { 'class': 'th' }, _('延迟')),
				E('div', { 'class': 'th' }, _('操作'))
			])
		]);

		if (nodes.length === 0) {
			table.appendChild(
				E('div', { 'class': 'tr placeholder' },
					E('div', { 'class': 'td', 'colspan': 4 },
						E('em', _('暂无节点，请先更新订阅'))
					)
				)
			);
		} else {
			nodes.forEach(function(node, index) {
				table.appendChild(
					E('div', { 'class': 'tr' }, [
						E('div', { 'class': 'td' }, node.name || '未命名'),
						E('div', { 'class': 'td' }, node.server || '-'),
						E('div', { 'class': 'td' }, '-'),
						E('div', { 'class': 'td' }, [
							E('button', {
								'class': 'btn cbi-button cbi-button-apply',
								'click': function() {
									ui.addNotification(null, 
										E('p', _('切换节点功能开发中...')), 
										'info');
								}
							}, _('使用'))
						])
					])
				);
			});
		}

		return E('div', { 'class': 'cbi-map' }, [
			E('h2', {}, _('节点列表')),
			E('div', { 'class': 'cbi-section' }, [
				E('div', { 'class': 'cbi-section-node' }, table)
			])
		]);
	},

	handleSaveApply: null,
	handleSave: null,
	handleReset: null
});
EOF

echo "OpenWrt 文件结构创建完成！"
echo ""
echo "目录结构:"
echo "openwrt/"
echo "├── etc/"
echo "│   ├── config/ech-workers"
echo "│   └── init.d/ech-workers"
echo "├── usr/"
echo "│   └── lib/lua/luci/controller/ech-workers.lua"
echo "└── www/"
echo "    └── luci-static/resources/view/ech-workers/"
echo "        ├── settings.js"
echo "        ├── subscription.js"
echo "        └── nodes.js"