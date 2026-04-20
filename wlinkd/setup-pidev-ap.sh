#!/bin/bash

# 실행되는 명령어를 출력하고, 실패 시 즉시 중단
set -euxo pipefail

AP_IFACE="${AP_IFACE:-wlan0}"
AP_IP="${AP_IP:-192.168.4.1}"
AP_PREFIX="${AP_PREFIX:-24}"
AP_NETMASK="255.255.255.0"
AP_DHCP_START="${AP_DHCP_START:-192.168.4.2}"
AP_DHCP_END="${AP_DHCP_END:-192.168.4.20}"
AP_SSID="${AP_SSID:-Pi4AP}"
AP_PASSPHRASE="${AP_PASSPHRASE:-5blue@straw}"
WLAN_COUNTRY="${WLAN_COUNTRY:-KR}"
UPLINK_IFACE="${UPLINK_IFACE:-$(ip route show default 2>/dev/null | awk '{print $5}' | head -n1)}"
IP_BIN="$(command -v ip)"

if [[ ${#AP_PASSPHRASE} -lt 8 || ${#AP_PASSPHRASE} -gt 63 ]]; then
    echo "[ERROR] AP_PASSPHRASE 길이는 8~63자여야 합니다."
    exit 1
fi

if [[ ! -x "$IP_BIN" ]]; then
    echo "[ERROR] ip 명령을 찾을 수 없습니다."
    exit 1
fi

if [[ $EUID -eq 0 ]]; then
    SUDO=""
else
    SUDO="sudo"
fi

# 필수 패키지 설치
$SUDO apt update
$SUDO apt install -y hostapd dnsmasq build-essential libnl-3-dev libnl-genl-3-dev
$SUDO env DEBIAN_FRONTEND=noninteractive apt install -y iptables-persistent
$SUDO apt install -y iperf3 python3-matplotlib rfkill iw
if ! $SUDO apt install -y "linux-headers-$(uname -r)"; then
    $SUDO apt install -y raspberrypi-kernel-headers || true
fi

# 부팅 로그 레벨 상향 (경로가 있는 경우만)
for cmdline in /boot/firmware/cmdline.txt /boot/cmdline.txt; do
    if [[ -f "$cmdline" ]] && ! grep -q "loglevel=7" "$cmdline"; then
        $SUDO sed -i 's/$/ loglevel=7/' "$cmdline"
    fi
done

# Wi-Fi 차단 해제 및 국가 코드 설정
$SUDO rfkill unblock wlan || true
if command -v raspi-config >/dev/null 2>&1; then
    $SUDO raspi-config nonint do_wifi_country "$WLAN_COUNTRY" || true
fi
$SUDO iw reg set "$WLAN_COUNTRY" || true

# wlan0가 다른 서비스에 잡혀 있으면 AP 모드 전환이 실패하므로 정리
if command -v nmcli >/dev/null 2>&1; then
    $SUDO mkdir -p /etc/NetworkManager/conf.d
    cat <<EOF | $SUDO tee /etc/NetworkManager/conf.d/99-${AP_IFACE}-unmanaged.conf >/dev/null
[keyfile]
unmanaged-devices=interface-name:${AP_IFACE}
EOF
    $SUDO nmcli device set "$AP_IFACE" managed no || true
    $SUDO systemctl restart NetworkManager || true
fi

$SUDO systemctl stop "wpa_supplicant@${AP_IFACE}.service" || true
$SUDO systemctl stop wpa_supplicant.service || true

if ! $IP_BIN link show "$AP_IFACE" >/dev/null 2>&1; then
    echo "[ERROR] ${AP_IFACE} 인터페이스를 찾을 수 없습니다. USB 동글 또는 내장 Wi‑Fi 상태를 확인하세요."
    exit 1
fi

# 재부팅 후에도 AP IP가 유지되도록 systemd 서비스 생성
cat <<EOF | $SUDO tee /etc/systemd/system/ap-${AP_IFACE}.service >/dev/null
[Unit]
Description=Configure ${AP_IFACE} static IP for AP mode
Wants=network-pre.target
Before=hostapd.service dnsmasq.service
After=sys-subsystem-net-devices-${AP_IFACE}.device
BindsTo=sys-subsystem-net-devices-${AP_IFACE}.device

[Service]
Type=oneshot
ExecStart=${IP_BIN} link set ${AP_IFACE} down
ExecStart=${IP_BIN} addr flush dev ${AP_IFACE}
ExecStart=${IP_BIN} link set ${AP_IFACE} up
ExecStart=${IP_BIN} addr add ${AP_IP}/${AP_PREFIX} dev ${AP_IFACE}
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
EOF

$SUDO systemctl daemon-reload
$SUDO systemctl enable "ap-${AP_IFACE}.service"
$SUDO systemctl restart "ap-${AP_IFACE}.service"

# IP 포워딩 활성화
cat <<EOF | $SUDO tee /etc/sysctl.d/90-ip-forward.conf >/dev/null
net.ipv4.ip_forward=1
EOF
$SUDO sysctl -p /etc/sysctl.d/90-ip-forward.conf

# dnsmasq 설정
cat <<EOF | $SUDO tee /etc/dnsmasq.conf >/dev/null
interface=${AP_IFACE}
bind-interfaces
listen-address=${AP_IP}
dhcp-range=${AP_DHCP_START},${AP_DHCP_END},${AP_NETMASK},24h
domain-needed
bogus-priv
EOF

# hostapd 설정
cat <<EOF | $SUDO tee /etc/hostapd/hostapd.conf >/dev/null
ctrl_interface=/var/run/hostapd
ctrl_interface_group=0
country_code=${WLAN_COUNTRY}
interface=${AP_IFACE}
driver=nl80211
ssid=${AP_SSID}
hw_mode=g
channel=6
ieee80211n=1
wmm_enabled=1
macaddr_acl=0
auth_algs=1
ignore_broadcast_ssid=0
wpa=2
wpa_passphrase=${AP_PASSPHRASE}
wpa_key_mgmt=WPA-PSK
rsn_pairwise=CCMP

eap_server=1
wps_state=2
ap_setup_locked=0
device_name=Pi4
manufacturer=RaspberryPi

model_name=WAP
model_number=123
serial_number=12345
device_type=6-0050F204-1
os_version=01020300
config_methods=push_button pbc
upnp_iface=${AP_IFACE}
EOF

cat <<EOF | $SUDO tee /etc/default/hostapd >/dev/null
DAEMON_CONF="/etc/hostapd/hostapd.conf"
EOF

# NAT 및 iptables 설정 (uplink 인터페이스가 있는 경우만 적용)
if [[ -n "${UPLINK_IFACE}" && "${UPLINK_IFACE}" != "${AP_IFACE}" ]]; then
    $SUDO iptables -t nat -C POSTROUTING -o "$UPLINK_IFACE" -j MASQUERADE 2>/dev/null || \
        $SUDO iptables -t nat -A POSTROUTING -o "$UPLINK_IFACE" -j MASQUERADE
    $SUDO iptables -C FORWARD -i "$UPLINK_IFACE" -o "$AP_IFACE" -m state --state RELATED,ESTABLISHED -j ACCEPT 2>/dev/null || \
        $SUDO iptables -A FORWARD -i "$UPLINK_IFACE" -o "$AP_IFACE" -m state --state RELATED,ESTABLISHED -j ACCEPT
    $SUDO iptables -C FORWARD -i "$AP_IFACE" -o "$UPLINK_IFACE" -j ACCEPT 2>/dev/null || \
        $SUDO iptables -A FORWARD -i "$AP_IFACE" -o "$UPLINK_IFACE" -j ACCEPT
    $SUDO systemctl enable netfilter-persistent
    $SUDO sh -c 'iptables-save > /etc/iptables/rules.v4'
else
    echo "[INFO] 업링크 인터페이스를 찾지 못해 NAT 설정은 건너뜁니다."
fi

# 서비스 재시작 및 상태 검증
$SUDO systemctl unmask hostapd
$SUDO systemctl enable hostapd dnsmasq
$SUDO systemctl restart hostapd
$SUDO systemctl restart dnsmasq

$SUDO systemctl --no-pager --full status "ap-${AP_IFACE}.service" hostapd dnsmasq || true

if ! $SUDO systemctl is-active --quiet hostapd; then
    echo "[ERROR] hostapd 시작 실패"
    $SUDO journalctl -u hostapd -n 50 --no-pager || true
    exit 1
fi

if ! $SUDO systemctl is-active --quiet dnsmasq; then
    echo "[ERROR] dnsmasq 시작 실패"
    $SUDO journalctl -u dnsmasq -n 50 --no-pager || true
    exit 1
fi

echo "[OK] ${AP_IFACE} AP 모드 활성화 완료: SSID=${AP_SSID}, IP=${AP_IP}/${AP_PREFIX}"